/***************************************************************************
 *   Copyright (C) 2016 by Sean D'Epagnier                                 *
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
 **************************************************************************/

#include <wx/wx.h>

#include <functional>

#include "json/json.h"

#include "RoutePoint.h"
#include "WeatherDataProvider.h"
#include "RouteMap.h"
#include "Utilities.h"
#include "ocpn_plugin.h"

#define distance(X, Y) sqrt((X) * (X) + (Y) * (Y))  // much faster than hypot

extern Json::Value g_ReceivedJSONMsg;
extern wxString g_ReceivedMessage;

static Json::Value RequestGRIB(const wxDateTime& t, const wxString& what,
                               double lat, double lon) {
  Json::Value error;
  Json::Value v;
  Json::FastWriter writer;
  // brain dead wx is expecting time in local time
  wxDateTime time = t.FromUTC();
  if (!time.IsValid()) return error;

  v["Day"] = time.GetDay();
  v["Month"] = time.GetMonth();
  v["Year"] = time.GetYear();
  v["Hour"] = time.GetHour();
  v["Minute"] = time.GetMinute();
  v["Second"] = time.GetSecond();

  v["Source"] = "WEATHER_ROUTING_PI";
  v["Type"] = "Request";
  v["Msg"] = "GRIB_VALUES_REQUEST";
  v["lat"] = lat;
  v["lon"] = lon;
  v[what] = 1;

  SendPluginMessage("GRIB_VALUES_REQUEST", writer.write(v));
  if (g_ReceivedMessage != wxEmptyString &&
      g_ReceivedJSONMsg["Type"].asString() == "Reply") {
    return g_ReceivedJSONMsg;
  }
  return error;
}

/**
 * Retrieves wind data from GRIB file at a specified location.
 *
 * Fetches wind direction and speed at the given latitude and longitude from a
 * GRIB file.
 *
 * @param configuration Route map configuration containing GRIB data and
 * settings
 * @param lat Latitude in degrees
 * @param lon Longitude in degrees
 * @param twdOverGround [out] True Wind Direction over ground in degrees
 * (meteorological convention)
 * @param twsOverGround [out] True Wind Speed over ground in knots
 *
 * @return true if wind data was successfully retrieved, false otherwise
 */
bool WeatherDataProvider::GetGribWind(RouteMapConfiguration& configuration,
                                      double lat, double lon,
                                      double& twdOverGround,
                                      double& twsOverGround) {
  WR_GribRecordSet* grib = configuration.grib;

  if (!grib && !configuration.RouteGUID.IsEmpty() && configuration.UseGrib) {
    Json::Value r = RequestGRIB(configuration.time, "WIND SPEED", lat, lon);
    if (!r.isMember("WIND SPEED")) return false;
    twsOverGround = r["WIND SPEED"].asDouble();

    if (!r.isMember("WIND DIR")) return false;
    twdOverGround = r["WIND DIR"].asDouble();
  } else if (!grib)
    return false;

  else if (!GribRecord::getInterpolatedValues(
               twsOverGround, twdOverGround,
               grib->m_GribRecordPtrArray[Idx_WIND_VX],
               grib->m_GribRecordPtrArray[Idx_WIND_VY], lon, lat))
    return false;

  twsOverGround *= 3.6 / 1.852;  // knots
#if 0
// test
tws = 0.;
twd = 0.;
#endif
  return true;
}

enum { WIND, CURRENT };

static bool GribCurrent(RouteMapConfiguration& configuration, double lat,
                        double lon, double& currentDir, double& currentSpeed) {
  WR_GribRecordSet* grib = configuration.grib;

  if (!grib && !configuration.RouteGUID.IsEmpty() && configuration.UseGrib) {
    Json::Value r = RequestGRIB(configuration.time, "CURRENT SPEED", lat, lon);
    if (!r.isMember("CURRENT SPEED")) return false;
    currentSpeed = r["CURRENT SPEED"].asDouble();

    if (!r.isMember("CURRENT DIR")) return false;
    currentDir = r["CURRENT DIR"].asDouble();
  } else if (!grib)
    return false;

  else if (!GribRecord::getInterpolatedValues(
               currentSpeed, currentDir,
               grib->m_GribRecordPtrArray[Idx_SEACURRENT_VX],
               grib->m_GribRecordPtrArray[Idx_SEACURRENT_VY], lon, lat))
    return false;

  currentSpeed *= 3.6 / 1.852;  // knots
  currentDir += 180;
  if (currentDir > 360) currentDir -= 360;
  return true;
}

bool WeatherDataProvider::GetCurrent(RouteMapConfiguration& configuration,
                                     double lat, double lon, double& currentDir,
                                     double& currentSpeed,
                                     DataMask& data_mask) {
  if (!configuration.grib_is_data_deficient &&
      GribCurrent(configuration, lat, lon, currentDir, currentSpeed)) {
    data_mask |= DataMask::GRIB_CURRENT;
    return true;
  }

  if (configuration.ClimatologyType != RouteMapConfiguration::DISABLED &&
      RouteMap::ClimatologyData &&
      RouteMap::ClimatologyData(CURRENT, configuration.time, lat, lon,
                                currentDir, currentSpeed)) {
    data_mask |= DataMask::CLIMATOLOGY_CURRENT;
    return true;
  }

#if 0  // for now disable deficient current data as it's usefulness is not known
// use deficient grib current if climatology is not available
// unlike wind, we don't use current data from a different location
// so only current data from a different time is allowed
if(configuration.AllowDataDeficient &&
configuration.grib_is_data_deficient && GribCurrent(configuration, lat, lon, currentDir, currentSpeed)) {
data_mask |= DataMask::GRIB_CURRENT | DataMask::DATA_DEFICIENT_CURRENT;
return true;
}
#endif

  return false;
}

/**
 * Transforms a vector from ground-relative to water-relative coordinate system.
 *
 * This function converts a vector measured relative to ground into a vector
 * relative to moving water by accounting for sea currents. Can be used for:
 * 1. Converting true wind (over ground) to wind over water
 * 2. Converting vessel motion over ground (COG/SOG) to motion through water
 * (CTW/STW)
 *
 * @param groundDir Direction of vector relative to ground (degrees,
 * meteorological convention)
 * @param groundMag Magnitude of vector relative to ground (typically knots)
 * @param currentDir Current Direction (degrees, meteorological convention)
 * @param currentMag Current Magnitude (knots) - Note: for correct calculation,
 *                   this should be negative of actual current speed
 * @param waterDir [out] Direction of vector relative to water (degrees,
 * meteorological convention)
 * @param waterMag [out] Magnitude of vector relative to water (same units as
 * groundMag)
 */
void WeatherDataProvider::GroundToWaterFrame(double groundDir, double groundMag,
                                             double currentDir,
                                             double currentMag,
                                             double& waterDir,
                                             double& waterMag) {
  if (currentMag == 0) {  // short-cut if no currents
    waterDir = groundDir, waterMag = groundMag;
    return;
  }

  double Cx = currentMag * cos(deg2rad(currentDir)),
         Cy = currentMag * sin(deg2rad(currentDir));
  double Wx = groundMag * cos(deg2rad(groundDir)) - Cx,
         Wy = groundMag * sin(deg2rad(groundDir)) - Cy;
  waterDir = rad2deg(atan2(Wy, Wx));
  waterMag = distance(Wx, Wy);
}

/**
 * Converts a vector measured relative to water into a vector relative to ground
 * by adding current effects.
 *
 * This function transforms any vector (wind, vessel movement, etc.) from
 * water-relative to ground-relative reference frame by vector addition with the
 * water current.
 *
 * For example, this can be used to transform the vessel's motion relative to
 * water (CTW and STW) into motion relative to ground (COG and SOG) by
 * incorporating the effects of water currents.
 *
 * @param directionWater Direction of the vector relative to water (degrees)
 * @param magnitudeWater Magnitude of the vector relative to water (typically
 * knots)
 * @param currentDir Current direction (degrees, meteorological convention: FROM
 * direction)
 * @param currentSpeed Current speed (knots)
 * @param directionGround [out] Direction of the vector relative to ground
 * (degrees)
 * @param magnitudeGround [out] Magnitude of the vector relative to ground (same
 * units as magnitudeWater)
 *
 * The calculation uses basic vector addition:
 * [Velocity over ground] = [Velocity through water] + [Current vector]
 *
 * If there are no currents (currentSpeed = 0), the function simply copies
 * currentDir to directionGround and magnitudeWater to magnitudeGround.
 */
void WeatherDataProvider::TransformToGroundFrame(
    double directionWater, double magnitudeWater, double currentDir,
    double currentSpeed, double& directionGround, double& magnitudeGround) {
  if (currentSpeed == 0) {  // short-cut if no currents
    directionGround = directionWater, magnitudeGround = magnitudeWater;
    return;
  }

  double Cx = currentSpeed * cos(deg2rad(currentDir)),
         Cy = currentSpeed * sin(deg2rad(currentDir));
  double BGx = magnitudeWater * cos(deg2rad(directionWater)) + Cx,
         BGy = magnitudeWater * sin(deg2rad(directionWater)) + Cy;
  directionGround = rad2deg(atan2(BGy, BGx));
  magnitudeGround = distance(BGx, BGy);
}

/**
 * Retrieves wind and current data for a specific location at a particular time.
 *
 * This function attempts to obtain both wind and current information at the
 * specified location for the time defined in the configuration parameter.
 * It uses multiple data sources in order of preference:
 * 1. GRIB files (if available and not data deficient).
 * 2. Climatology average data.
 * 3. Climatology wind atlas data.
 * 4. Data deficient GRIB (if allowed).
 * 5. Parent position data (fallback).
 *
 * The function calculates both ground-relative (true) and water-relative
 * (apparent) wind values by accounting for current effects.
 *
 * @param configuration Route map configuration containing GRIB data,
 * climatology settings, and the specific time for which to retrieve weather
 * data.
 * @param position Pointer to the route point for which to retrieve data
 * @param twdOverGround [out] True Wind Direction over ground (degrees). This is
 * the forecast value from the GRIB file or climatology data.
 * @param twsOverGround [out] True Wind Speed over ground (knots). This is the
 * forecast value from the GRIB file or climatology data.
 * @param twdOverWater [out] True Wind Direction over water (degrees). This is
 * the calculated value based on the current and wind over ground. Same as
 * twdOverGround if current is 0 or not available.
 * @param twsOverWater [out] True Wind Speed over water (knots). This is the
 * calculated value based on the current and wind over ground. Same as
 * twsOverGround if current is 0 or not available.
 * @param currentDir [out] Current Direction (degrees). This is the forecast
 * value from the GRIB file or climatology data.
 * @param currentSpeed [out] Current Speed (knots). This is the forecast value
 * from the GRIB file or climatology data.
 * @param atlas [out] Climatology wind atlas data (filled if climatology data is
 * used)
 * @param data_mask [in/out] Bit flags indicating what data sources were used
 *
 * @return true if wind and current data were successfully retrieved, false
 * otherwise
 */
bool WeatherDataProvider::ReadWindAndCurrents(
    RouteMapConfiguration& configuration, const RoutePoint* position,
    /* normal data */
    double& twdOverGround, double& twsOverGround, double& twdOverWater,
    double& twsOverWater, double& currentDir, double& currentSpeed,

    climatology_wind_atlas& atlas, DataMask& data_mask) {
  /* read current data */
  if (!configuration.Currents ||
      !GetCurrent(configuration, position->lat, position->lon, currentDir,
                  currentSpeed, data_mask))
    currentDir = currentSpeed = 0;

  for (;;) {
    if (!configuration.grib_is_data_deficient &&
        GetGribWind(configuration, position->lat, position->lon, twdOverGround,
                    twsOverGround)) {
      data_mask |= DataMask::GRIB_WIND;
      break;
    }

    if (configuration.ClimatologyType == RouteMapConfiguration::AVERAGE &&
        RouteMap::ClimatologyData &&
        RouteMap::ClimatologyData(WIND, configuration.time, position->lat,
                                  position->lon, twdOverGround,
                                  twsOverGround)) {
      twdOverGround = heading_resolve(twdOverGround);

      data_mask |= DataMask::CLIMATOLOGY_WIND;

      break;
    } else if (configuration.ClimatologyType >
                   RouteMapConfiguration::CURRENTS_ONLY &&
               RouteMap::ClimatologyWindAtlasData) {
      int windatlas_count = 8;
      double speeds[8];
      if (RouteMap::ClimatologyWindAtlasData(
              configuration.time, position->lat, position->lon, windatlas_count,
              atlas.directions, speeds, atlas.storm, atlas.calm)) {
        /* compute wind speeds over water with the given current */
        for (int i = 0; i < windatlas_count; i++) {
          double twd = i * 360 / windatlas_count;
          double tws = speeds[i] * configuration.WindStrength;
          GroundToWaterFrame(twd, tws, currentDir, -currentSpeed, atlas.W[i],
                             atlas.VW[i]);
        }

        /* find most likely wind direction */
        double max_direction = 0;
        int maxi = 0;
        for (int i = 0; i < windatlas_count; i++)
          if (atlas.directions[i] > max_direction) {
            max_direction = atlas.directions[i];
            maxi = i;
          }

        /* now compute next most likely wind octant (adjacent to most likely)
           and linearly interpolate speed and direction from these two octants,
           we use this as the most likely wind, and base wind direction for the
           map */
        int maxia = maxi + 1, maxib = maxi - 1;
        if (maxia == windatlas_count) maxia = 0;
        if (maxib < 0) maxib = windatlas_count - 1;

        if (atlas.directions[maxia] < atlas.directions[maxib]) maxia = maxib;

        double maxid =
            1 / (atlas.directions[maxi] / atlas.directions[maxia] + 1);
        double angle1 = atlas.W[maxia], angle2 = atlas.W[maxi];
        while (angle1 - angle2 > 180) angle1 -= 360;
        while (angle2 - angle1 > 180) angle2 -= 360;
        twdOverWater = heading_resolve(maxid * angle1 + (1 - maxid) * angle2);
        twsOverWater = maxid * atlas.VW[maxia] + (1 - maxid) * atlas.VW[maxi];

        TransformToGroundFrame(twdOverWater, twsOverWater, currentDir,
                               currentSpeed, twdOverGround, twsOverGround);

        data_mask |= DataMask::CLIMATOLOGY_WIND;
        return true;
      }
    }

    if (!configuration.AllowDataDeficient) return false;

    /* try deficient grib if climatology failed */
    if (configuration.grib_is_data_deficient &&
        GetGribWind(configuration, position->lat, position->lon, twdOverGround,
                    twsOverGround)) {
      data_mask |= DataMask::GRIB_WIND | DataMask::DATA_DEFICIENT_WIND;

      break;
    }
    const Position* n = dynamic_cast<const Position*>(position);
    if (!n || !n->parent) return false;
    position = n->parent;
  }
  twsOverGround *= configuration.WindStrength;

  GroundToWaterFrame(twdOverGround, twsOverGround, currentDir, -currentSpeed,
                     twdOverWater, twsOverWater);
  return true;
}

/**
 * Helper function for retrieving weather data from GRIB file or requesting it
 * remotely.
 *
 * This function handles the common boilerplate code for various weather
 * parameter retrieval functions (CloudCover, Rainfall, AirTemperature,
 * SeaTemperature, CAPE, RelativeHumidity, AirPressure).
 *
 * @param configuration RouteMapConfiguration containing GRIB data and settings
 * @param lat Latitude in degrees
 * @param lon Longitude in degrees
 * @param requestKey GRIB request key (e.g., "CLOUD", "RAIN", etc.)
 * @param gribIndex Index in the GRIB record array to access the relevant data
 * @param returnOnEmpty Value to return if no data is available
 * @param postProcessFn Optional function to post-process the retrieved value
 *
 * @return The requested weather parameter value, or returnOnEmpty if no data is
 * available
 */
double WeatherDataProvider::GetWeatherParameter(
    RouteMapConfiguration& configuration, double lat, double lon,
    const wxString& requestKey, int gribIndex, double returnOnEmpty,
    std::function<double(double)> postProcessFn) {
  WR_GribRecordSet* grib = configuration.grib;

  // Try to get data from remote GRIB if local one is not available
  if (!grib && !configuration.RouteGUID.IsEmpty() && configuration.UseGrib) {
    Json::Value r = RequestGRIB(configuration.time, requestKey, lat, lon);
    if (!r.isMember(requestKey)) return returnOnEmpty;
    double value = r[requestKey].asDouble();
    return postProcessFn ? postProcessFn(value) : value;
  }

  // Return early if no GRIB data at all
  if (!grib) return returnOnEmpty;

  // Try to retrieve the data from local GRIB
  std::shared_ptr<GribRecord> grh = grib->m_GribRecordPtrArray[gribIndex];
  if (!grh) return returnOnEmpty;

  double value = grh->getInterpolatedValue(lon, lat, true);
  if (value == GRIB_NOTDEF) return returnOnEmpty;

  return postProcessFn ? postProcessFn(value) : value;
}

/**
 * Return the swell height at the specified lat/long location.
 * @return the swell height in meters. 0 if no data is available.
 */
double WeatherDataProvider::GetSwell(RouteMapConfiguration& configuration,
                                     double lat, double lon) {
  return GetWeatherParameter(
      configuration, lat, lon, "SWELL", Idx_HTSIGW, NAN,
      [](double height) { return height < 0 ? 0 : height; });
}

/**
 * Return the wave direction at the specified lat/long location.
 * @return the wave direction in degrees.
 */
double WeatherDataProvider::GetWaveDirection(
    RouteMapConfiguration& configuration, double lat, double lon) {
  return GetWeatherParameter(
      configuration, lat, lon, "WAVE DIR", Idx_WVDIR, NAN,
      [](double height) { return height < 0 ? 0 : height; });
}

/**
 * Return the wave period at the specified lat/long location.
 * @return the wave period in seconds.
 */
double WeatherDataProvider::GetWavePeriod(RouteMapConfiguration& configuration,
                                          double lat, double lon) {
  return GetWeatherParameter(
      configuration, lat, lon, "WAVE PERIOD", Idx_WVPER, NAN,
      [](double height) { return height < 0 ? 0 : height; });
}

/**
 * Return the wind gust speed for the specified lat/long location, in knots.
 * @return the wind gust speed in knots. 0 if no data is available.
 */
double WeatherDataProvider::GetGust(RouteMapConfiguration& configuration,
                                    double lat, double lon) {
  return GetWeatherParameter(
      configuration, lat, lon, "GUST", Idx_WIND_GUST, NAN,
      [](double gust) { return gust * 3.6 / 1.852; });  // Convert to knots
}

/**
 * Return the cloud cover as percentage.
 */
double WeatherDataProvider::GetCloudCover(RouteMapConfiguration& configuration,
                                          double lat, double lon) {
  return GetWeatherParameter(configuration, lat, lon, "CLOUD", Idx_CLOUD_TOT,
                             NAN);
}

/**
 * Return the rainfall rate at the specified lat/long location.
 * @return the rainfall rate in mm/h. 0 if no data is available.
 */
double WeatherDataProvider::GetRainfall(RouteMapConfiguration& configuration,
                                        double lat, double lon) {
  return GetWeatherParameter(configuration, lat, lon, "RAIN", Idx_PRECIP_TOT,
                             NAN);
}

/**
 * Return the air temperature at the specified lat/long location.
 * @return the air temperature in degrees Celsius. 0 if no data is available.
 */
double WeatherDataProvider::GetAirTemperature(
    RouteMapConfiguration& configuration, double lat, double lon) {
  return GetWeatherParameter(configuration, lat, lon, "AIR TEMP", Idx_AIR_TEMP,
                             NAN);
}

/**
 * Return the sea temperature at the specified lat/long location.
 * @return the sea temperature in degrees Celsius. 0 if no data is available.
 */
double WeatherDataProvider::GetSeaTemperature(
    RouteMapConfiguration& configuration, double lat, double lon) {
  return GetWeatherParameter(configuration, lat, lon, "SEA TEMP", Idx_SEA_TEMP,
                             NAN);
}

/**
 * Return the CAPE (Convective Available Potential Energy) at the specified
 * lat/long location.
 * @return the CAPE in J/kg. 0 if no data is available.
 */
double WeatherDataProvider::GetCAPE(RouteMapConfiguration& configuration,
                                    double lat, double lon) {
  return GetWeatherParameter(configuration, lat, lon, "CAPE", Idx_CAPE, NAN);
}

/**
 * Return the relative humidity at the specified lat/long location.
 * @return the relative humidity in percent. 0 if no data is available.
 */
double WeatherDataProvider::GetRelativeHumidity(
    RouteMapConfiguration& configuration, double lat, double lon) {
  return GetWeatherParameter(configuration, lat, lon, "REL HUM", Idx_HUMID_RE,
                             NAN);
}

/**
 * Return the air surface pressure at the specified lat/long location.
 * @return the air pressure in hPa. NAN if no data is available.
 */
double WeatherDataProvider::GetAirPressure(RouteMapConfiguration& configuration,
                                           double lat, double lon) {
  return GetWeatherParameter(configuration, lat, lon, "PRESSURE", Idx_PRESSURE,
                             NAN);
}

/**
 * Return the reflectivity at the specified lat/long location.
 * @return the reflectivity in dBZ. NAN if no data is available.
 */
double WeatherDataProvider::GetReflectivity(
    RouteMapConfiguration& configuration, double lat, double lon) {
  return GetWeatherParameter(configuration, lat, lon, "REFLECTIVITY",
                             Idx_COMP_REFL, NAN);
}
