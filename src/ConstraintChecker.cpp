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
#include <cstdint>
#include <chrono>
#include <vector>
#include <algorithm>
#include <mutex>
#include <atomic>

#include "ConstraintChecker.h"
#include "WeatherDataProvider.h"
#include "RouteMap.h"
#include "Utilities.h"

#include "georef.h"
#include "ocpn_plugin.h"

// Quantize to 1e-5 deg (about 1m)
constexpr double QUANT = 1e5;

/**
 * Thread-safe segment cache with size limit.
 *
 * All cache operations are protected by land_cache_mutex to ensure thread
 * safety during concurrent isochron propagations. Statistics counters use
 * std::atomic for lock-free updates.
 */
struct SegmentCacheEntry {
  bool crosses_land;
  std::chrono::steady_clock::time_point last_used;

  SegmentCacheEntry()
      : crosses_land(false), last_used(std::chrono::steady_clock::now()) {}

  SegmentCacheEntry(bool crosses)
      : crosses_land(crosses), last_used(std::chrono::steady_clock::now()) {}
};

constexpr size_t MAX_SEGMENT_CACHE_SIZE = 10000;

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

static std::unordered_map<SegmentKey, SegmentCacheEntry> land_cache;
static std::mutex land_cache_mutex;

static std::atomic<size_t> segment_cache_hits{0}, segment_cache_misses{0},
    segment_cache_queries{0};
static std::atomic<size_t> df_hits{0}, df_misses{0}, df_queries{0},
    df_safe_water_optimizations{0};
static std::atomic<size_t> segment_evictions{0}, distance_field_evictions{0};
constexpr size_t LOG_INTERVAL = 1000;

// LRU eviction for segment cache
// Note: This function assumes the caller already holds land_cache_mutex
void evict_oldest_segment_entries() {
  if (land_cache.size() <= MAX_SEGMENT_CACHE_SIZE) return;

  std::vector<std::pair<SegmentKey, std::chrono::steady_clock::time_point>>
      entries;
  entries.reserve(land_cache.size());

  for (const auto& pair : land_cache) {
    entries.emplace_back(pair.first, pair.second.last_used);
  }

  // Sort by last_used time (oldest first)
  std::sort(
      entries.begin(), entries.end(),
      [](const std::pair<SegmentKey, std::chrono::steady_clock::time_point>& a,
         const std::pair<SegmentKey, std::chrono::steady_clock::time_point>&
             b) { return a.second < b.second; });

  // Remove oldest 20% of entries
  size_t to_remove = land_cache.size() - MAX_SEGMENT_CACHE_SIZE * 0.8;
  for (size_t i = 0; i < to_remove && i < entries.size(); ++i) {
    land_cache.erase(entries[i].first);
  }
  segment_evictions.fetch_add(to_remove);
}

void log_cache_stats() {
  size_t queries = segment_cache_queries.load();
  size_t hits = segment_cache_hits.load();
  size_t misses = segment_cache_misses.load();
  size_t evictions = segment_evictions.load();

  double segment_hit_rate = queries ? (double)hits / queries : 0.0;

  // Note: land_cache.size() must be called under mutex protection
  std::lock_guard<std::mutex> lock(land_cache_mutex);
  wxLogMessage(
      "WeatherRouting Segment cache: queries=%zu, hits=%zu, misses=%zu, "
      "size=%zu, hitrate=%.1f%%, evictions=%zu",
      queries, hits, misses, land_cache.size(), 100.0 * segment_hit_rate,
      evictions);
}

void maintain_land_cache() {
#if 0
  static std::atomic<size_t> last_log_queries{0};
  size_t current_queries = segment_cache_queries.load();
  size_t last_queries = last_log_queries.load();

  {
    std::lock_guard<std::mutex> lock(land_cache_mutex);
    evict_oldest_segment_entries();
  }

  if (current_queries - last_queries > LOG_INTERVAL) {
    last_log_queries.store(current_queries);
    log_cache_stats();
  }
#endif
}

/**
 * Determine if a route segment crosses land or is entirely in navigable water.
 *
 * @param lat1 Latitude of segment start point (degrees)
 * @param lon1 Longitude of segment start point (degrees)
 * @param lat2 Latitude of segment end point (degrees)
 * @param lon2 Longitude of segment end point (degrees)
 * @return true if segment crosses land or is entirely on land (invalid route),
 *         false if segment is entirely in navigable water (valid route)
 *
 * Segment cache check (o(1)):
 * - First check if we've computed this exact segment before
 * - If cache hit, return immediately with known result
 *
 * Exact computation fallback:
 * - For near-coastline segments or on-land grid points, use expensive
 * PlugIn_GSHHS_CrossesLand()
 * - Cache the exact result for future use
 */
bool Cached_CrossesLand(double lat1, double lon1, double lat2, double lon2) {
#if 0
  SegmentKey key(lat1, lon1, lat2, lon2);
  segment_cache_queries.fetch_add(1);

  // Check segment cache first.
  {
    std::lock_guard<std::mutex> lock(land_cache_mutex);
    auto it = land_cache.find(key);
    if (it != land_cache.end()) {
      it->second.last_used = std::chrono::steady_clock::now();
      segment_cache_hits.fetch_add(1);
      return it->second.crosses_land;
    }
  }

  segment_cache_misses.fetch_add(1);
  bool result = PlugIn_GSHHS_CrossesLand(lat1, lon1, lat2, lon2);
  // Cache the result
  {
    std::lock_guard<std::mutex> lock(land_cache_mutex);
    if (land_cache.size() >= MAX_SEGMENT_CACHE_SIZE) {
      evict_oldest_segment_entries();
    }
    land_cache.emplace(key, SegmentCacheEntry(result));
  }
#endif
  bool result = PlugIn_GSHHS_CrossesLand(lat1, lon1, lat2, lon2);
  return result;
}

void clear_land_cache() {
  std::lock_guard<std::mutex> lock(land_cache_mutex);
  land_cache.clear();
  segment_cache_hits.store(0);
  segment_cache_misses.store(0);
  segment_cache_queries.store(0);
  segment_evictions.store(0);
}

bool ConstraintChecker::CheckSwellConstraint(
    RouteMapConfiguration& configuration, double lat, double lon, double& swell,
    PropagationError& error_code) {
  swell = WeatherDataProvider::GetSwell(configuration, lat, lon);
  if (swell > configuration.MaxSwellMeters) {
    wxLogGeneric(
        wxLOG_Debug,
        "WeatherRouting: Swell constraint exceeded at lat=%.6f lon=%.6f: %.2fm "
        "> %.2fm max",
        lat, lon, swell, configuration.MaxSwellMeters);
    error_code = PROPAGATION_EXCEEDED_MAX_SWELL;
    return false;
  }
  return true;
}

bool ConstraintChecker::CheckMaxLatitudeConstraint(
    RouteMapConfiguration& configuration, double lat,
    PropagationError& error_code) {
  if (fabs(lat) > configuration.MaxLatitude) {
    wxLogGeneric(
        wxLOG_Debug,
        "WeatherRouting: Max latitude constraint exceeded: %.6f > %.6f",
        fabs(lat), configuration.MaxLatitude);
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
    /*
     * Calculate the wind vector (Wx, Wy) and ocean current vector (Cx, Cy).
     * these are already computed in GroundToWaterFrame could optimize by
     * reusing them.
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
