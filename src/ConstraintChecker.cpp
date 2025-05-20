/***************************************************************************
 *   Copyright (C) 2015 by OpenCPN development team                        *
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 3 of the License, or     *
 *   (at your option) any later version.                                   *
 *                                                                         *
 *   This program is distributed in the hope that it will be useful,       *
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of        *
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the         *
 *   GNU General Public License for more details.                          *
 *                                                                         *
 *   You should have received a copy of the GNU General Public License     *
 *   along with this program; if not, write to the                         *
 *   Free Software Foundation, Inc.,                                       *
 *   51 Franklin Street, Fifth Floor, Boston, MA 02110-1301,  USA.         *
 ***************************************************************************/

#include <wx/wx.h>
#include <wx/log.h>

#include <unordered_map>
#include <cmath>
#include <sstream>
#include <cstdint>

#include "ConstraintChecker.h"
#include "WeatherDataProvider.h"
#include "RouteMap.h"
#include "Utilities.h"

#include "georef.h"
#include "ocpn_plugin.h"

// Quantize to 1e-5 deg (about 1m)
constexpr double QUANT = 1e5;
struct SegmentKey {
  uint64_t packed;
  SegmentKey(double lat1, double lon1, double lat2, double lon2) {
    int la1 = static_cast<int>(std::round(lat1 * QUANT));
    int lo1 = static_cast<int>(std::round(lon1 * QUANT));
    int la2 = static_cast<int>(std::round(lat2 * QUANT));
    int lo2 = static_cast<int>(std::round(lon2 * QUANT));
    // Always store with smaller endpoint first for symmetry
    if (la1 > la2 || (la1 == la2 && lo1 > lo2)) {
      std::swap(la1, la2);
      std::swap(lo1, lo2);
    }
    // Pack four 16-bit signed ints into a 64-bit value
    packed =
        (static_cast<uint64_t>(static_cast<uint32_t>(la1) & 0xFFFF) << 48) |
        (static_cast<uint64_t>(static_cast<uint32_t>(lo1) & 0xFFFF) << 32) |
        (static_cast<uint64_t>(static_cast<uint32_t>(la2) & 0xFFFF) << 16) |
        (static_cast<uint64_t>(static_cast<uint32_t>(lo2) & 0xFFFF));
  }
  bool operator==(const SegmentKey& o) const { return packed == o.packed; }
};

template <>
struct std::hash<SegmentKey> {
  std::size_t operator()(const SegmentKey& k) const {
    // Use std::hash for uint64_t
    return std::hash<uint64_t>()(k.packed);
  }
};

static std::unordered_map<SegmentKey, bool> land_cache;
static size_t cache_hits = 0, cache_misses = 0, cache_queries = 0;
constexpr size_t LOG_INTERVAL = 1000;

void log_cache_stats() {
  double hitrate = cache_queries ? (double)cache_hits / cache_queries : 0.0;
  std::ostringstream oss;
  wxLogMessage(
      "[WeatherRouting] land cache: "
      "queries=%d, hits=%d, misses=%d, size=%zu, hitrate=%.2f%%",
      cache_queries, cache_hits, cache_misses, land_cache.size(),
      100.0 * hitrate);
}

void clear_land_cache() {
  land_cache.clear();
  cache_hits = cache_misses = cache_queries = 0;
}

// Wrapper for PlugIn_GSHHS_CrossesLand with caching
bool Cached_CrossesLand(double lat1, double lon1, double lat2, double lon2) {
  SegmentKey key(lat1, lon1, lat2, lon2);
  ++cache_queries;
  auto it = land_cache.find(key);
  if (it != land_cache.end()) {
    ++cache_hits;
    return it->second;
  }
  ++cache_misses;
  bool result = PlugIn_GSHHS_CrossesLand(lat1, lon1, lat2, lon2);
  land_cache[key] = result;
  return result;
}

bool ConstraintChecker::CheckSwellConstraint(
    RouteMapConfiguration& configuration, double lat, double lon, double& swell,
    PropagationError& error_code) {
  swell = WeatherDataProvider::GetSwell(configuration, lat, lon);
  if (swell > configuration.MaxSwellMeters) {
    error_code = PROPAGATION_EXCEEDED_MAX_SWELL;
    return false;
  }
  return true;
}

bool ConstraintChecker::CheckMaxLatitudeConstraint(
    RouteMapConfiguration& configuration, double lat,
    PropagationError& error_code) {
  if (fabs(lat) > configuration.MaxLatitude) {
    error_code = PROPAGATION_EXCEEDED_MAX_LATITUDE;
    return false;
  }
  return true;
}

bool ConstraintChecker::CheckCycloneTrackConstraint(
    RouteMapConfiguration& configuration, double lat, double lon, double dlat,
    double dlon) {
  if (configuration.AvoidCycloneTracks &&
      RouteMap::ClimatologyCycloneTrackCrossings) {
    int crossings = RouteMap::ClimatologyCycloneTrackCrossings(
        lat, lon, dlat, dlon, configuration.time,
        configuration.CycloneMonths * 30 + configuration.CycloneDays);
    if (crossings > 0) {
      return false;
    }
  }
  return true;
}

bool ConstraintChecker::CheckMaxCourseAngleConstraint(
    RouteMapConfiguration& configuration, double dlat, double dlon) {
  if (configuration.MaxCourseAngle < 180) {
    double bearing;
    // this is faster than gc distance, and actually works better in higher
    // latitudes
    double d1 = dlat - configuration.StartLat,
           d2 = dlon - configuration.StartLon;
    d2 *= cos(deg2rad(dlat)) / 2;  // correct for latitude
    bearing = rad2deg(atan2(d2, d1));

    if (fabs(heading_resolve(configuration.StartEndBearing - bearing)) >
        configuration.MaxCourseAngle) {
      return false;
    }
  }
  return true;
}

bool ConstraintChecker::CheckMaxDivertedCourse(
    RouteMapConfiguration& configuration, double dlat, double dlon) {
  if (configuration.MaxDivertedCourse < 180) {
    double bearing, dist;
    double bearing1, dist1;

    double d1 = dlat - configuration.EndLat, d2 = dlon - configuration.EndLon;
    d2 *= cos(deg2rad(dlat)) / 2;  // correct for latitude
    bearing = rad2deg(atan2(d2, d1));
    dist = sqrt(pow(d1, 2) + pow(d2, 2));

    d1 = configuration.StartLat - dlat, d2 = configuration.StartLon - dlon;
    bearing1 = rad2deg(atan2(d2, d1));
    dist1 = sqrt(pow(d1, 2) + pow(d2, 2));

    double term = (dist1 + dist) / dist;
    term = pow(term / 16, 4) + 1;  // make 1 until the end, then make big

    if (fabs(heading_resolve(bearing1 - bearing)) >
        configuration.MaxDivertedCourse * term) {
      return false;
    }
  }
  return true;
}

bool ConstraintChecker::CheckLandConstraint(
    RouteMapConfiguration& configuration, double lat, double lon, double dlat1,
    double dlon1, double cog) {
  if (configuration.DetectLand) {
    double ndlon1 = dlon1;
    if (ndlon1 > 360) {
      ndlon1 -= 360;
    }
    if (Cached_CrossesLand(lat, lon, dlat1, ndlon1)) {
      return false;
    }
    double distSecure = configuration.SafetyMarginLand;
    double latBorderUp1, lonBorderUp1, latBorderUp2, lonBorderUp2;
    double latBorderDown1, lonBorderDown1, latBorderDown2, lonBorderDown2;
    ll_gc_ll(lat, lon, heading_resolve(cog) - 90, distSecure, &latBorderUp1,
             &lonBorderUp1);
    ll_gc_ll(dlat1, dlon1, heading_resolve(cog) - 90, distSecure, &latBorderUp2,
             &lonBorderUp2);
    ll_gc_ll(lat, lon, heading_resolve(cog) + 90, distSecure, &latBorderDown1,
             &lonBorderDown1);
    ll_gc_ll(dlat1, dlon1, heading_resolve(cog) + 90, distSecure,
             &latBorderDown2, &lonBorderDown2);
    if (Cached_CrossesLand(latBorderUp1, lonBorderUp1, latBorderUp2,
                           lonBorderUp2) ||
        Cached_CrossesLand(latBorderDown1, lonBorderDown1, latBorderDown2,
                           lonBorderDown2) ||
        Cached_CrossesLand(latBorderUp1, lonBorderUp1, latBorderDown2,
                           lonBorderDown2) ||
        Cached_CrossesLand(latBorderDown1, lonBorderDown1, latBorderUp2,
                           lonBorderUp2)) {
      return false;
    }
  }
  return true;
}

bool ConstraintChecker::CheckMaxTrueWindConstraint(
    RouteMapConfiguration& configuration, double twsOverWater,
    PropagationError& error_code) {
  if (twsOverWater > configuration.MaxTrueWindKnots) {
    error_code = PROPAGATION_EXCEEDED_MAX_WIND;
    return false;
  }
  return true;
}

bool ConstraintChecker::CheckMaxApparentWindConstraint(
    RouteMapConfiguration& configuration, double stw, double twa,
    double twsOverWater, PropagationError& error_code) {
  if (stw + twsOverWater > configuration.MaxApparentWindKnots &&
      Polar::VelocityApparentWind(stw, twa, twsOverWater) >
          configuration.MaxApparentWindKnots) {
    return false;
  }
  return true;
}

bool ConstraintChecker::CheckWindVsCurrentConstraint(
    RouteMapConfiguration& configuration, double twsOverWater,
    double twdOverWater, double currentSpeed, double currentDir,
    PropagationError& error_code) {
  if (configuration.WindVSCurrent) {
    /* Calculate the wind vector (Wx, Wy) and ocean current vector (Cx, Cy). */
    /* these are already computed in GroundToWaterFrame could optimize by
     * reusing them
     */
    double Wx = twsOverWater * cos(deg2rad(twdOverWater)),
           Wy = twsOverWater * sin(deg2rad(twdOverWater));
    double Cx = currentSpeed * cos(deg2rad(currentDir) + M_PI),
           Cy = currentSpeed * sin(deg2rad(currentDir) + M_PI);

    if (Wx * Cx + Wy * Cy + configuration.WindVSCurrent < 0) {
      error_code = PROPAGATION_EXCEEDED_WIND_VS_CURRENT;
      return false;
    }
  }
  return true;
}