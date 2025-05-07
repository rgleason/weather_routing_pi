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

#include "ConstraintChecker.h"
#include "WeatherDataProvider.h"
#include "RouteMap.h"
#include "Utilities.h"

#include "georef.h"
#include "ocpn_plugin.h"

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

    // Check first if crossing land.
    if (ndlon1 > 360) {
      ndlon1 -= 360;
    }
    if (PlugIn_GSHHS_CrossesLand(lat, lon, dlat1, ndlon1)) {
      return false;
    }

    // CUSTOMIZATION - Safety distance from land
    // -----------------------------------------
    // Modify the routing according to a safety
    // margin defined by the user from the land.
    // CONFIG: 2 NM as a security distance by default.
    double distSecure = configuration.SafetyMarginLand;
    double latBorderUp1, lonBorderUp1, latBorderUp2, lonBorderUp2;
    double latBorderDown1, lonBorderDown1, latBorderDown2, lonBorderDown2;

    // Test if land is found within a rectangle with
    // dimensions (dist, distSecure). Tests borders, plus diag,
    // and middle of each side...
    //            <- dist ->
    // |-------------------------------|
    // |                               |    ^
    // |                               |    distSafety
    // |-------------------------------|    ^
    // |                               |
    // |                               |
    // |-------------------------------|

    // Fist, find the (lat,long) of each
    // points of the rectangle
    ll_gc_ll(lat, lon, heading_resolve(cog) - 90, distSecure, &latBorderUp1,
             &lonBorderUp1);
    ll_gc_ll(dlat1, dlon1, heading_resolve(cog) - 90, distSecure, &latBorderUp2,
             &lonBorderUp2);
    ll_gc_ll(lat, lon, heading_resolve(cog) + 90, distSecure, &latBorderDown1,
             &lonBorderDown1);
    ll_gc_ll(dlat1, dlon1, heading_resolve(cog) + 90, distSecure,
             &latBorderDown2, &lonBorderDown2);

    // Then, test if there is land
    if (PlugIn_GSHHS_CrossesLand(latBorderUp1, lonBorderUp1, latBorderUp2,
                                 lonBorderUp2) ||
        PlugIn_GSHHS_CrossesLand(latBorderDown1, lonBorderDown1, latBorderDown2,
                                 lonBorderDown2) ||
        PlugIn_GSHHS_CrossesLand(latBorderUp1, lonBorderUp1, latBorderDown2,
                                 lonBorderDown2) ||
        PlugIn_GSHHS_CrossesLand(latBorderDown1, lonBorderDown1, latBorderUp2,
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