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

/* get data from a position for plotting */
bool RoutePoint::GetPlotData(RoutePoint* next, double dt,
                             RouteMapConfiguration& configuration,
                             PlotData& data) {
  data.lat = lat;
  data.lon = lon;
  data.tacks = tacks;
  data.jibes = jibes;
  data.sail_plan_changes = sail_plan_changes;
  data.polar = polar;

  data.WVHT = WeatherDataProvider::GetSwell(configuration, lat, lon);
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
  int data_mask = 0;  // not used for plotting yet
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
  configuration.grib_is_data_deficient = old;
  return true;
}

bool RoutePoint::GetWindData(RouteMapConfiguration& configuration,
                             double& twdOverWater, double& twsOverWater,
                             int& data_mask) {
  double twdOverGround, twsOverGround, currentDir, currentSpeed;
  climatology_wind_atlas atlas;
  return WeatherDataProvider::ReadWindAndCurrents(
      configuration, this, twdOverGround, twsOverGround, twdOverWater,
      twsOverWater, currentDir, currentSpeed, atlas, data_mask);
}

bool RoutePoint::GetCurrentData(RouteMapConfiguration& configuration,
                                double& currentDir, double& currentSpeed,
                                int& data_mask) {
  double twdOverGround, twsOverGround, twdOverWater, twsOverWater;
  climatology_wind_atlas atlas;
  return WeatherDataProvider::ReadWindAndCurrents(
      configuration, this, twdOverGround, twsOverGround, twdOverWater,
      twsOverWater, currentDir, currentSpeed, atlas, data_mask);
}

bool RoutePoint::ComputeBoatSpeed(
    RouteMapConfiguration& configuration, double timeseconds, double twd,
    double tws, double windDirOverWater, double windSpeedOverWater,
    double currentDir, double currentSpeed, double twa,
    climatology_wind_atlas& atlas, double ctw, double& stw, double& cog,
    double& sog, double& dist, int newpolar, bool bound, const char* caller) {
  if (newpolar < 0 ||
      newpolar >= static_cast<int>(configuration.boat.Polars.size())) {
    // Sanity check - invalid polar index.
    return false;
  }
  Polar& polar = configuration.boat.Polars[newpolar];
  PolarSpeedStatus polar_status;
  bool used_grib = false;  // true if grib data was used, false if climatology.
  if ((data_mask & Position::CLIMATOLOGY_WIND) &&
      (configuration.ClimatologyType == RouteMapConfiguration::CUMULATIVE_MAP ||
       configuration.ClimatologyType ==
           RouteMapConfiguration::CUMULATIVE_MINUS_CALMS)) {
    /* build map */
    stw = 0;
    int windatlas_count = 8;
    for (int i = 0; i < windatlas_count; i++) {
      // Calculate relative wind angle (difference between heading and wind
      // direction).
      double dir = twa - windDirOverWater + atlas.W[i];
      if (dir > 180) dir = 360 - dir;
      double boatSpeed, mind = polar.MinDegreeStep();
      // if tacking
      if (fabs(dir) < mind)
        boatSpeed = polar.Speed(mind, atlas.VW[i], &polar_status, bound,
                                configuration.OptimizeTacking) *
                    cos(deg2rad(mind)) / cos(deg2rad(dir));
      else
        boatSpeed = polar.Speed(dir, atlas.VW[i], &polar_status, bound,
                                configuration.OptimizeTacking);
      // Accumulate weighted boat speed based on probability of each wind
      // direction
      stw += atlas.directions[i] * boatSpeed;
    }

    if (configuration.ClimatologyType ==
        RouteMapConfiguration::CUMULATIVE_MINUS_CALMS)
      stw *= 1 - atlas.calm;
  } else {
    // Direct polar lookup - get boat speed from polar data for current heading
    // and wind speed.
    used_grib = true;
    stw = polar.Speed(twa, windSpeedOverWater, &polar_status, bound,
                      configuration.OptimizeTacking);
  }

  /* failed to determine speed. */
  if (std::isnan(ctw) || std::isnan(stw)) {
    // This can happen if the wind speed is outside the range of the polar data.
    // For example, if the wind speed is too high or too low, or if the wind
    // angle is too close to the polar's minimum angle.
    wxLogMessage(
        "[%s] Failed to get polar speed. windDirOverWater=%f "
        "windSpeedOverWater=%f "
        "twa=%f tws=%f ctw=%f stw=%f bound=%d grib=%d",
        caller, windDirOverWater, windSpeedOverWater, twa, tws, ctw, stw, bound,
        used_grib);
    configuration.polar_status = polar_status;
    return false;  // ctw = stw = 0;
  }

  // Apply upwind/downwind efficiency factors based on wind angle.
  double abs_twa = fabs(twa);
  if (abs_twa <= 90.0) {
    // Upwind sailing (0-90 degrees relative to wind)
    stw *= configuration.UpwindEfficiency;
  } else {
    // Downwind sailing (90-180 degrees relative to wind)
    stw *= configuration.DownwindEfficiency;
  }

  // Determine if it's day or night at the current position and time
  DayLightStatus dayLightStatus =
      SunCalculator::GetInstance().GetDayLightStatus(lat, lon,
                                                     configuration.time);

  if (dayLightStatus == DayLightStatus::Night) {
    // Apply day/night efficiency factor
    stw *= configuration.NightCumulativeEfficiency;
    // Set the NIGHT_TIME flag in data_mask for visual differentiation
    data_mask |= Position::NIGHT_TIME;
  }

  // Calculate boat movement over ground by combining boat speed with current.
  WeatherDataProvider::TransformToGroundFrame(ctw, stw, currentDir,
                                              currentSpeed, cog, sog);

  // Calculate distance traveled over ground based on speed and time.
  dist = sog * timeseconds / 3600.0;
  return true;
}

double RoutePoint::PropagateToPoint(double dlat, double dlon,
                                    RouteMapConfiguration& configuration,
                                    double& heading, int& data_mask, bool end) {
  double swell = WeatherDataProvider::GetSwell(configuration, lat, lon);
  if (swell > configuration.MaxSwellMeters) return NAN;

  if (fabs(lat) > configuration.MaxLatitude) return NAN;

  double twdOverGround, twsOverGround, twdOverWater, twsOverWater, currentDir,
      currentSpeed;
  climatology_wind_atlas atlas;
  if (!WeatherDataProvider::ReadWindAndCurrents(
          configuration, this, twdOverGround, twsOverGround, twdOverWater,
          twsOverWater, currentDir, currentSpeed, atlas, data_mask)) {
    if (!end) {
      wxString txt = _("No wind data for this position at that time");
      configuration.wind_data_status =
          wxString::Format("%s (lat=%f,lon=%f) %s", txt, lat, lon,
                           configuration.time.Format("%Y-%m-%d %H:%M:%S"));
    }
    return NAN;
  }

  if (twsOverWater > configuration.MaxTrueWindKnots) return NAN;

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
  double stw;  // Speed Through Water: Speed of the boat relative to the water

  // This is a starting point for the iterative solver. It's using the wind
  // direction as an initial guess for which way the boat might be heading, but
  // this gets refined in the do-while loop.
  double cog = twdOverWater;
  double sog;
  int iters = 0;
  heading = 0;
  int newpolar = polar;
  bool old = configuration.OptimizeTacking;
  if (end) configuration.OptimizeTacking = true;
  PolarSpeedStatus status;
  do {
    // (bearing - cog) represents the angle between the destination and the
    // where the boat is heading in the iterative process.
    while (bearing - cog > 180) bearing -= 360;
    while (cog - bearing > 180) bearing += 360;

    heading += bearing - cog;
    ctw = twdOverWater + heading; /* rotated relative to true wind */

    double dummy_dist;  // not used

    newpolar = configuration.boat.FindBestPolarForCondition(
        polar, twsOverWater, heading, swell, configuration.OptimizeTacking,
        &status);

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
        newpolar = polar;
        wxLogMessage(
            "Using out-of-bound sail plan. Status: %s. windSpeedOverWater=%f "
            "tws=%f",
            Polar::GetPolarStatusMessage(status), twsOverWater, twsOverGround);
      } else {
        configuration.polar_status = status;
        configuration.OptimizeTacking = old;
        wxLogMessage(
            "Failed to switch polar. Status: %s. windSpeedOverWater=%f "
            "tws=%f",
            Polar::GetPolarStatusMessage(status), twsOverWater, twsOverGround);
        return NAN;
      }
    }

    if (!ComputeBoatSpeed(configuration, 0, twdOverGround, twsOverGround,
                          twdOverWater, twsOverWater, currentDir, currentSpeed,
                          heading, atlas, ctw, stw, cog, sog, dummy_dist,
                          newpolar, inside_polar_bounds /* bound */,
                          "PropagateToPoint") ||
        ++iters == 10  // give up
    ) {
      configuration.OptimizeTacking = old;
      return NAN;
    }
  } while ((bearing - cog) > 1e-3);
  configuration.OptimizeTacking = old;

  /* only allow if we fit in the isochron time.  We could optimize this by
  finding the maximum boat speed once, and using that before computing boat
  speed for this angle, but for now, we don't worry because propagating to
  the end is a small amount of total computation */
  if (end && dist / sog > configuration.UsedDeltaTime / 3600.0) return NAN;

  /* quick test first to avoid slower calculation */
  if (stw + twsOverWater > configuration.MaxApparentWindKnots &&
      Polar::VelocityApparentWind(stw, heading, twsOverWater) >
          configuration.MaxApparentWindKnots)
    return NAN;

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
  if (configuration.AvoidCycloneTracks &&
      RouteMap::ClimatologyCycloneTrackCrossings) {
    int crossings = RouteMap::ClimatologyCycloneTrackCrossings(
        lat, lon, configuration.EndLat, configuration.EndLon,
        configuration.time,
        configuration.CycloneMonths * 30 + configuration.CycloneDays);

    if (crossings > 0) return NAN;
  }
  polar = newpolar;

  return 3600.0 * dist / sog;
}

bool RoutePoint::CrossesLand(double dlat, double dlon) {
  return PlugIn_GSHHS_CrossesLand(lat, lon, dlat, dlon);
}

bool RoutePoint::EntersBoundary(double dlat, double dlon) {
  struct FindClosestBoundaryLineCrossing_t t;
  t.dStartLat = lat, t.dStartLon = heading_resolve(lon);
  t.dEndLat = dlat, t.dEndLon = heading_resolve(dlon);
  t.sBoundaryState = wxT("Active");

  // we request any type
  return RouteMap::ODFindClosestBoundaryLineCrossing(&t);
}
