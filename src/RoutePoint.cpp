/***************************************************************************
 *   Copyright (C) 2016 by OpenCPN development team                        *
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

#include "RoutePoint.h"
#include "WeatherDataProvider.h"
#include "RouteMap.h"
#include "Utilities.h"
#include "SunCalculator.h"
#include "ocpn_plugin.h"
#include "georef.h"

WeatherData::WeatherData(RoutePoint* position)
    : lat(position->lat),
      lon(position->lon),
      twdOverGround(0),
      twsOverGround(0),
      twdOverWater(0),
      twsOverWater(0),
      currentDir(0),
      currentSpeed(0),
      swell(0) {
  // Silence warning about uninitialized fields
  atlas.storm = 0;
  atlas.calm = 0;
}

/* get data from a position for plotting */
bool RoutePoint::GetPlotData(RoutePoint* next, double dt,
                             RouteMapConfiguration& configuration,
                             PlotData& data) const {
  data.lat = lat;
  data.lon = lon;
  data.tacks = tacks;
  data.jibes = jibes;
  data.sail_plan_changes = sail_plan_changes;
  data.polar = polar;

  data.WVHT = WeatherDataProvider::GetSwell(configuration, lat, lon);
  data.WVDIR = WeatherDataProvider::GetWaveDirection(configuration, lat, lon);
  data.WVPER = WeatherDataProvider::GetWavePeriod(configuration, lat, lon);
  data.VW_GUST = WeatherDataProvider::GetGust(configuration, lat, lon);
  data.delta = dt;

  data.cloud_cover =
      WeatherDataProvider::GetCloudCover(configuration, lat, lon);
  data.rain_mm_per_hour =
      WeatherDataProvider::GetRainfall(configuration, lat, lon);
  data.air_temp =
      WeatherDataProvider::GetAirTemperature(configuration, lat, lon);
  data.sea_surface_temp =
      WeatherDataProvider::GetSeaTemperature(configuration, lat, lon);
  data.cape = WeatherDataProvider::GetCAPE(configuration, lat, lon);
  data.relative_humidity =
      WeatherDataProvider::GetRelativeHumidity(configuration, lat, lon);
  data.reflectivity =
      WeatherDataProvider::GetReflectivity(configuration, lat, lon);
  data.air_pressure =
      WeatherDataProvider::GetAirPressure(configuration, lat, lon);

  climatology_wind_atlas atlas;

  DataMask data_mask = DataMask::NONE;
  bool old = configuration.grib_is_data_deficient;
  configuration.grib_is_data_deficient = grib_is_data_deficient;
  if (!WeatherDataProvider::ReadWindAndCurrents(
          configuration, this, data.twdOverGround, data.twsOverGround,
          data.twdOverWater, data.twsOverWater, data.currentDir,
          data.currentSpeed, atlas, data_mask)) {
    // I don't think this can ever be hit, because the data should have been
    // there for the position be be created in the first place
    printf("Wind/Current data failed for position!!!\n");
    configuration.grib_is_data_deficient = old;
    return false;
  }

  // Combine the current RoutePoint's data_mask (which includes MOTOR_USED)
  // with the local data_mask (which includes wind/current sources)
  data.data_mask = this->data_mask | data_mask;

  // Calculate the great circle distance and initial bearing between this route
  // point and the next one.
  ll_gc_ll_reverse(lat, lon, next->lat, next->lon, &data.cog, &data.sog);
  if (dt == 0)
    data.sog = 0;
  else
    data.sog *= 3600 / dt;

  WeatherDataProvider::GroundToWaterFrame(data.cog, data.sog, data.currentDir,
                                          data.currentSpeed, data.ctw,
                                          data.stw);

  // CTW is the track through water, but heading should account for current and
  // leeway.
  data.hdg = data.ctw;

  // Calculate wave direction relative to boat heading
  if (!std::isnan(data.WVDIR) && !std::isnan(data.hdg)) {
    data.WVREL = heading_resolve(data.WVDIR - data.hdg);
  } else {
    data.WVREL = NAN;
  }

  configuration.grib_is_data_deficient = old;
  return true;
}

bool RoutePoint::GetWindData(RouteMapConfiguration& configuration,
                             double& twdOverWater, double& twsOverWater,

                             DataMask& data_mask) {
  double twdOverGround, twsOverGround, currentDir, currentSpeed;
  climatology_wind_atlas atlas;
  return WeatherDataProvider::ReadWindAndCurrents(
      configuration, this, twdOverGround, twsOverGround, twdOverWater,
      twsOverWater, currentDir, currentSpeed, atlas, data_mask);
}

bool RoutePoint::GetCurrentData(RouteMapConfiguration& configuration,
                                double& currentDir, double& currentSpeed,

                                DataMask& data_mask) {
  double twdOverGround, twsOverGround, twdOverWater, twsOverWater;
  climatology_wind_atlas atlas;
  return WeatherDataProvider::ReadWindAndCurrents(
      configuration, this, twdOverGround, twsOverGround, twdOverWater,
      twsOverWater, currentDir, currentSpeed, atlas, data_mask);
}

bool BoatData::GetBoatSpeedForPolar(RouteMapConfiguration& configuration,
                                    const WeatherData& weather_data,
                                    double timeseconds, int newpolar,
                                    double twa, double ctw, DataMask& data_mask,
                                    bool bound, const char* caller) {
  if (newpolar < 0 ||
      newpolar >= static_cast<int>(configuration.boat.Polars.size())) {
    // Sanity check - invalid polar index.
    return false;
  }
  Polar& polar = configuration.boat.Polars[newpolar];
  PolarSpeedStatus polar_status;
  bool used_grib = false;  // true if grib data was used, false if climatology.
  bool using_motor = false;  // true if motor is being used instead of sailing
  if ((data_mask & DataMask::CLIMATOLOGY_WIND) &&
      (configuration.ClimatologyType == RouteMapConfiguration::CUMULATIVE_MAP ||
       configuration.ClimatologyType ==
           RouteMapConfiguration::CUMULATIVE_MINUS_CALMS)) {
    /* build map */
    stw = 0;
    int windatlas_count = 8;
    for (int i = 0; i < windatlas_count; i++) {
      // Calculate relative wind angle (difference between heading and wind
      // direction).
      double dir = twa - weather_data.twdOverWater + weather_data.atlas.W[i];
      if (dir > 180) dir = 360 - dir;
      double boatSpeed, mind = polar.MinDegreeStep();
      // if tacking
      if (fabs(dir) < mind)
        boatSpeed = polar.Speed(mind, weather_data.atlas.VW[i], &polar_status,
                                bound, configuration.OptimizeTacking) *
                    cos(deg2rad(mind)) / cos(deg2rad(dir));
      else
        boatSpeed = polar.Speed(dir, weather_data.atlas.VW[i], &polar_status,
                                bound, configuration.OptimizeTacking);
      // Accumulate weighted boat speed based on probability of each wind
      // direction
      stw += weather_data.atlas.directions[i] * boatSpeed;
    }

    if (configuration.ClimatologyType ==
        RouteMapConfiguration::CUMULATIVE_MINUS_CALMS)
      stw *= 1 - weather_data.atlas.calm;
  } else {
    // Direct polar lookup - get boat speed from polar data for current heading
    // and wind speed.
    used_grib = true;
    stw = polar.Speed(twa, weather_data.twsOverWater, &polar_status, bound,
                      configuration.OptimizeTacking);
  }

  /* failed to determine speed. */
  if (std::isnan(ctw) || std::isnan(stw)) {
    // This can happen if the wind speed is outside the range of the polar data.
    // For example, if the wind speed is too high or too low, or if the wind
    // angle is too close to the polar's minimum angle.
    wxLogDebug(
        "[%s] Failed to get polar speed. windDirOverWater=%f "
        "windSpeedOverWater=%f "
        "twa=%f tws=%f ctw=%f stw=%f bound=%d grib=%d",
        caller, weather_data.twdOverWater, weather_data.twsOverWater, twa,
        weather_data.twsOverGround, ctw, stw, bound, used_grib);
    configuration.polar_status = polar_status;
    return false;  // ctw = stw = 0;
  }

  // Check if motor should be used for low sailing speeds
  if (configuration.UseMotor && stw < configuration.MotorSpeedThreshold) {
    stw = configuration.MotorSpeed;
    using_motor = true;
    data_mask |= DataMask::MOTOR_USED;
    // When motoring, the boat can go in any direction regardless of wind
    // The course through water (ctw) remains as requested
    // Note: All other constraints (land, boundaries, weather) still apply
  }

  // Apply upwind/downwind efficiency factors based on wind angle.
  // Skip efficiency factors when motoring as they don't apply to engine power
  if (!using_motor) {
    double abs_twa = fabs(twa);
    if (abs_twa <= 90.0) {
      // Upwind sailing (0-90 degrees relative to wind)
      stw *= configuration.UpwindEfficiency;
    } else {
      // Downwind sailing (90-180 degrees relative to wind)
      stw *= configuration.DownwindEfficiency;
    }
  }

  if (configuration.NightCumulativeEfficiency != 1.0) {
    // Determine if it's day or night at the current position and time.
    DayLightStatus dayLightStatus =
        SunCalculator::GetInstance().GetDayLightStatus(
            weather_data.lat, weather_data.lon, configuration.time);

    if (dayLightStatus == DayLightStatus::Night) {
      if (!using_motor) {
        // Apply day/night efficiency factor only if not motoring.
        // Skip night efficiency when motoring as engine power is consistent.
        stw *= configuration.NightCumulativeEfficiency;
      }
      // Set the NIGHT_TIME flag in data_mask for visual differentiation
      data_mask |= DataMask::NIGHT_TIME;
    }
  }

  // Calculate boat movement over ground by combining boat speed with current.
  WeatherDataProvider::TransformToGroundFrame(
      ctw, stw, weather_data.currentDir, weather_data.currentSpeed, cog, sog);

  // Calculate distance traveled over ground based on speed and time.
  dist = sog * timeseconds / 3600.0;
  return true;
}

bool WeatherData::ReadWeatherDataAndCheckConstraints(
    RouteMapConfiguration& configuration, RoutePoint* position,
    DataMask& data_mask, PropagationError& error_code, bool end) {
  if (!ConstraintChecker::CheckSwellConstraint(configuration, lat, lon, swell,
                                               error_code)) {
    return false;
  }
  if (!ConstraintChecker::CheckMaxLatitudeConstraint(configuration, lat,
                                                     error_code)) {
    return false;
  }

  // Read wind and current data
  if (!WeatherDataProvider::ReadWindAndCurrents(
          configuration, position, twdOverGround, twsOverGround, twdOverWater,
          twsOverWater, currentDir, currentSpeed, atlas, data_mask)) {
    error_code = PROPAGATION_WIND_DATA_FAILED;
    if (!end) {
      wxString txt = _("No wind data for this position at that time");
      configuration.wind_data_status =
          wxString::Format("%s (lat=%f,lon=%f) %s", txt, lat, lon,
                           configuration.time.Format("%Y-%m-%d %H:%M:%S"));
    }
    return false;
  }

  // Check if wind exceeds configured maximum limit (safety limit)
  if (!ConstraintChecker::CheckMaxTrueWindConstraint(
          configuration, twsOverWater, error_code)) {
    return false;
  }

  // If wind exceeds polar data but is within safety limits, we'll continue with
  // modified wind speed This is handled in the GetBoatSpeedForPolar function by
  // passing inside_polar_bounds=false
  if (!ConstraintChecker::CheckWindVsCurrentConstraint(
          configuration, twsOverWater, twdOverWater, currentSpeed, currentDir,
          error_code)) {
    return false;
  }

  return true;
}

bool BoatData::GetBestPolarAndBoatSpeed(RouteMapConfiguration& configuration,
                                        const WeatherData& weather_data,
                                        double twa, double ctw,
                                        double parent_heading,
                                        DataMask& data_mask, int polar,
                                        int& newpolar, double& timeseconds) {
  Reset();
  PolarSpeedStatus status;
  newpolar = configuration.boat.FindBestPolarForCondition(
      polar, weather_data.twsOverWater, twa, weather_data.swell,
      configuration.OptimizeTacking, &status);
  bool inside_polar_bounds = true;
  if (newpolar == -1 || status != PolarSpeedStatus::POLAR_SPEED_SUCCESS) {
    if (newpolar == -1 && polar >= 0) {
      newpolar = polar;
    }
    if (status == PolarSpeedStatus::POLAR_SPEED_WIND_TOO_LIGHT ||
        status == PolarSpeedStatus::POLAR_SPEED_WIND_TOO_STRONG) {
      // In light winds, FindBestPolarForCondition() may return a polar
      // where the heading and wind are not in the sail plan, but this is
      // the best we can do. This is not an error, the boat will just not
      // move in this direction, and perhaps the wind will pick up later.
      // case PolarSpeedStatus::POLAR_SPEED_ANGLE_TOO_LOW:
      // case PolarSpeedStatus::POLAR_SPEED_ANGLE_TOO_HIGH:
      // For strong wind, we use the max wind in the polar
      // If the polar goes to 30 knots and the wind is 31 knots,
      // we use the boat speed for 30 knots.
      inside_polar_bounds = false;
    } else {
      configuration.polar_status = status;
      return false;  // failed to switch polar
    }
  }
  if (polar > 0 && newpolar != polar) {
    // Apply penalty for changing sail plan.
    timeseconds -= configuration.SailPlanChangeTime;
    this->sail_plan_changed = true;
  }
  if (parent_heading != NAN) {
    /* did we tack thru the wind? apply penalty */
    if (parent_heading * twa < 0 && fabs(parent_heading - twa) < 180) {
      timeseconds -= configuration.TackingTime;
      this->tacked = true;
    }

    /* did we jibe through the wind? apply penalty */
    if (parent_heading * twa < 0 && fabs(parent_heading - twa) >= 180) {
      timeseconds -= configuration.JibingTime;
      this->jibed = true;
    }
  }

  // In light winds, we don't want to check the polar bounds, because
  // we already know the wind is too light for the polar.

  if (!GetBoatSpeedForPolar(configuration, weather_data, timeseconds, newpolar,
                            twa, ctw, data_mask,
                            inside_polar_bounds, /* when using out-of-bound sail
              plan, set bound=false */
                            "Propagate")) {
    return false;
  }
  return true;
}

double calculateRhumbBearing(const double dlon, const double lon,
                             const double dlat, const double lat) {
  // We need to calculate the constant bearing for a rhumb line
  double y = sin(deg2rad(dlon - lon)) * cos(deg2rad(dlat));
  double x = cos(deg2rad(lat)) * sin(deg2rad(dlat)) -
             sin(deg2rad(lat)) * cos(deg2rad(dlat)) * cos(deg2rad(dlon - lon));
  double rhumbBearing = rad2deg(atan2(y, x));

  // Normalize to 0-360
  return heading_resolve(rhumbBearing);
}

double RoutePoint::RhumbLinePropagateToPoint(
    double dlat, double dlon, RouteMapConfiguration& configuration,
    std::vector<RoutePoint*>& intermediatePoints, DataMask& data_mask,
    double& totalDistance, double& averageSpeed, double maxSegmentLength) {
  // Calculate rhumb line distance
  double rhumbDistance = DistLoxodrome(lat, lon, dlat, dlon);

  totalDistance = rhumbDistance;

  // If distance is very short, use regular PropagateToPoint
  if (rhumbDistance <= maxSegmentLength) {
    double heading;
    double time =
        PropagateToPoint(dlat, dlon, configuration, heading, data_mask, true);
    if (!std::isnan(time)) {
      // If the destination is reachable, add it to intermediate points
      RoutePoint* endpoint =
          new RoutePoint(dlat, dlon, polar, tacks, jibes, sail_plan_changes,
                         data_mask, configuration.grib_is_data_deficient);
      intermediatePoints.push_back(endpoint);
    }
    return time;
  }

  // Calculate number of segments needed
  int numSegments = std::ceil(rhumbDistance / maxSegmentLength);

  // Propagate through each segment sequentially
  double totalTime = 0;
  RoutePoint* currentPoint = this;

  // Store current configuration time to restore if needed
  wxDateTime originalTime = configuration.time;

  for (int i = 1; i <= numSegments; i++) {
    double nextLat, nextLon;
    bool isLastSegment = (i == numSegments);

    if (isLastSegment) {
      // Use exact destination for last segment
      nextLat = dlat;
      nextLon = dlon;
    } else {
      // Calculate intermediate point along the rhumb line
      double fraction = static_cast<double>(i) / numSegments;

      // For a rhumb line, we need to account for the distortion in the Mercator
      // projection
      double lat1 = deg2rad(lat);
      double lat2 = deg2rad(dlat);
      double dLon = deg2rad(dlon - lon);

      // Handle crossing the anti-meridian
      if (fabs(dLon) > M_PI) {
        dLon = (dLon > 0) ? -(2 * M_PI - dLon) : (2 * M_PI + dLon);
      }

      // Calculate intermediate latitude using linear interpolation
      double lat_i = lat1 + fraction * (lat2 - lat1);

      // Calculate the meridional parts for the latitudes
      auto meridionalParts = [](double lat) -> double {
        return log(tan(M_PI / 4 + lat / 2));
      };

      // Calculate intermediate longitude
      double lon_i;
      if (fabs(lat2 - lat1) < 1e-10) {
        // Special case for constant latitude (parallel)
        lon_i = deg2rad(lon) + fraction * dLon;
      } else {
        double q = (lat_i - lat1) / (lat2 - lat1);
        lon_i = deg2rad(lon) + q * dLon;
      }

      // Convert back to degrees
      nextLat = rad2deg(lat_i);
      nextLon = rad2deg(lon_i);
      // Normalize longitude to -180 to 180
      while (nextLon > 180) nextLon -= 360;
      while (nextLon <= -180) nextLon += 360;
    }

    double heading;
    // Attempt to propagate to the next point
    double segmentTime = currentPoint->PropagateToPoint(
        nextLat, nextLon, configuration, heading, data_mask, isLastSegment);

    if (std::isnan(segmentTime)) {
      // Could not reach this waypoint, propagation failed
      // Clean up any intermediate points we created
      for (RoutePoint* point : intermediatePoints) {
        delete point;
      }
      intermediatePoints.clear();

      // Restore original configuration time
      configuration.time = originalTime;
      return NAN;
    }

    totalTime += segmentTime;

    // Create a new RoutePoint to represent this waypoint
    RoutePoint* waypoint = new RoutePoint(
        nextLat, nextLon, currentPoint->polar, currentPoint->tacks,
        currentPoint->jibes, currentPoint->sail_plan_changes, data_mask,
        configuration.grib_is_data_deficient);

    intermediatePoints.push_back(waypoint);

    // If not at the last waypoint, prepare for next segment
    if (!isLastSegment) {
      // Update current point to the new position
      currentPoint = waypoint;

      // Update configuration time for next segment to get proper weather data
      configuration.time += wxTimeSpan::Seconds(segmentTime);
    }
  }
  // Calculate equivalent constant speed if requested
  if (totalTime > 0) {
    // Speed = Distance / Time (converting time from seconds to hours)
    averageSpeed = rhumbDistance / (totalTime / 3600.0);
  }

  return totalTime;
}

double RoutePoint::PropagateToPoint(double dlat, double dlon,
                                    RouteMapConfiguration& configuration,
                                    double& heading, DataMask& data_mask,
                                    bool end) {
  PropagationError error_code;
  WeatherData weather_data(this);
  if (!weather_data.ReadWeatherDataAndCheckConstraints(
          configuration, this, data_mask, error_code, end)) {
    return NAN;
  }

  /* todo: we should make sure we don't tack if we are already at the max tacks,
  possibly perform other tests and/or switch sail polar? */
  double bearing, dist;
  // Calculate the great circle distance and initial bearing between this route
  // point and the input destination point.
  ll_gc_ll_reverse(lat, lon, dlat, dlon, &bearing, &dist);

  /* figure out bearing and distance to go, because it is a non-linear problem
  if compounded with currents solve iteratively (without currents only one
  iteration will ever occur */
  double ctw;  // Course Through Water: Direction the boat is traveling relative
  // to the water.

  // This is a starting point for the iterative solver. It's using the wind
  // direction as an initial guess for which way the boat might be heading, but
  // this gets refined in the do-while loop.
  double cog = weather_data.twdOverWater;
  int iters = 0;
  heading = 0;
  int newpolar = polar;
  bool old = configuration.OptimizeTacking;
  if (end) configuration.OptimizeTacking = true;
  PolarSpeedStatus status;
  BoatData boat_data;
  do {
    // (bearing - cog) represents the angle between the destination and the
    // where the boat is heading in the iterative process.
    while (bearing - cog > 180) bearing -= 360;
    while (cog - bearing > 180) bearing += 360;

    heading += bearing - cog;
    ctw =
        weather_data.twdOverWater + heading; /* rotated relative to true wind */

    double timeseconds = 0.0;  // Set to negative values for sail plan changes,
                               // tacking and jibing

    if (!boat_data.GetBestPolarAndBoatSpeed(
            configuration, weather_data, heading, ctw, NAN /*parent_heading*/,
            data_mask, this->polar, newpolar,
            timeseconds) ||
        ++iters == 10  // give up
    ) {
      configuration.OptimizeTacking = old;
      return NAN;
    }
  } while ((bearing - cog) > 1e-3);
  configuration.OptimizeTacking = old;

  /* only allow if we fit in the isochrone time.  We could optimize this by
  finding the maximum boat speed once, and using that before computing boat
  speed for this angle, but for now, we don't worry because propagating to
  the end is a small amount of total computation */
  if (end && dist / boat_data.sog > configuration.UsedDeltaTime / 3600.0)
    return NAN;

  /* quick test first to avoid slower calculation */
  if (!ConstraintChecker::CheckMaxApparentWindConstraint(
          configuration, boat_data.stw, heading, weather_data.twsOverWater,
          error_code)) {
    return NAN;
  }

  /* landfall test if we are within 60 miles (otherwise it's very slow) */
  if (configuration.DetectLand && dist < 60 && CrossesLand(dlat, dlon)) {
    if (!end) configuration.land_crossing = true;
    return NAN;
  }

  /* Boundary test */
  if (configuration.DetectBoundary && EntersBoundary(dlat, dlon)) {
    if (!end) configuration.boundary_crossing = true;
    return NAN;
  }

  /* crosses cyclone track(s)? */
  if (!ConstraintChecker::CheckCycloneTrackConstraint(configuration, lat, lon,
                                                      configuration.EndLat,
                                                      configuration.EndLon)) {
    return NAN;
  }
  polar = newpolar;

  return 3600.0 * dist / boat_data.sog;
}

bool RoutePoint::CrossesLand(double dlat, double dlon) const {
  return PlugIn_GSHHS_CrossesLand(lat, lon, dlat, dlon);
}

bool RoutePoint::EntersBoundary(double dlat, double dlon) const {
  struct FindClosestBoundaryLineCrossing_t t;
  t.dStartLat = lat, t.dStartLon = heading_resolve(lon);
  t.dEndLat = dlat, t.dEndLon = heading_resolve(dlon);
  t.sBoundaryState = wxT("Active");

  // we request any type
  return RouteMap::ODFindClosestBoundaryLineCrossing(&t);
}

  void RoutePoint::toJson(Json::Value &json) const {
    json["lat"] = lat;
    json["lon"] = lon;
    json["polar"] = polar;
    json["tacks"] = tacks;
    json["jibes"] = jibes;
    json["sail_plan_changes"] = sail_plan_changes;
    json["grib_is_data_deficient"] = grib_is_data_deficient;
    json["data_mask"] = static_cast<uint32_t>(data_mask);
  }