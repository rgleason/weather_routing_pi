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

#include <math.h>
#include <time.h>
#include <unordered_map>

#include "SunCalculator.h"

#ifndef PI
#define PI 3.1415926535897931160E0 /* pi */
#endif
#define DEGREE (PI / 180.0)
#define RADIAN (180.0 / PI)
#define TPI (2 * PI)

// Zeniths for sunset/sunrise calculation
#define ZENITH_OFFICIAL (90.0 + 50.0 / 60.0)
#define ZENITH_CIVIL 96.0
#define ZENITH_NAUTICAL 102.0
#define ZENITH_ASTRONOMICAL 108.0

// Convert decimal hours in hours and minutes
wxDateTime convHrmn(double dhr) {
  int hr, mn;
  hr = (int)dhr;
  mn = (dhr - (double)hr) * 60;
  return wxDateTime(hr, mn);
};

// Helper function for steps 3-6 of the US Naval Observatory algorithm
// Calculates sun position (mean anomaly, true longitude, right ascension,
// declination)
static void CalculateSunPosition(double t, double& sinDec, double& cosDec,
                                 double& RA) {
  /*
  3. calculate the Sun's mean anomaly

  M = (0.9856 * t) - 3.289
  */
  double M = (0.9856 * t) - 3.289;
  /*
  4. calculate the Sun's true longitude

  L = M + (1.916 * sin(M)) + (0.020 * sin(2 * M)) + 282.634
  NOTE: L potentially needs to be adjusted into the range [0,360) by
  adding/subtracting 360
  */
  double L =
      M + (1.916 * sin(DEGREE * M)) + (0.020 * sin(2 * DEGREE * M)) + 282.634;
  if (L > 360) L -= 360;
  if (L < 0) L += 360;
  /*
  5a. calculate the Sun's right ascension

  RA = atan(0.91764 * tan(L))
  NOTE: RA potentially needs to be adjusted into the range [0,360) by
  adding/subtracting 360
  */
  RA = RADIAN * atan(0.91764 * tan(DEGREE * L));
  if (RA > 360) RA -= 360;
  if (RA < 0) RA += 360;
  /*
  5b. right ascension value needs to be in the same quadrant as L

  Lquadrant  = (floor( L/90)) * 90
  RAquadrant = (floor(RA/90)) * 90
  RA = RA + (Lquadrant - RAquadrant)
  */
  double Lquadrant = (floor(L / 90)) * 90;
  double RAquadrant = (floor(RA / 90)) * 90;
  RA = RA + (Lquadrant - RAquadrant);
  /*
  5c. right ascension value needs to be converted into hours

  RA = RA / 15
  */
  RA = RA / 15;
  /*
  6. calculate the Sun's declination

  sinDec = 0.39782 * sin(L)
  cosDec = cos(asin(sinDec))
  */
  sinDec = 0.39782 * sin(DEGREE * L);
  cosDec = cos(asin(sinDec));
}

void SunCalculator::CalculateSun(double latit, double longit, int dayOfYear,
                                 wxDateTime& sunrise, wxDateTime& sunset,
                                 const wxDateTime* dateTime,
                                 double* sunElevation) {
  /*
  Source:
  Almanac for Computers, 1990
  published by Nautical Almanac Office
  United States Naval Observatory
  Washington, DC 20392

  Inputs:
  day, month, year:      date of sunrise/sunset
  latitude, longitude:   location for sunrise/sunset
  zenith:                Sun's zenith for sunrise/sunset
  official      = 90 degrees 50'
  civil        = 96 degrees
  nautical     = 102 degrees
  astronomical = 108 degrees

  NOTE: longitude is positive for East and negative for West
  NOTE: the algorithm assumes the use of a calculator with the
  trig functions in "degree" (rather than "radian") mode. Most
  programming languages assume radian arguments, requiring back
  and forth conversions. The factor is 180/pi. So, for instance,
  the equation RA = atan(0.91764 * tan(L)) would be coded as RA
  = (180/pi)*atan(0.91764 * tan((pi/180)*L)) to give a degree
  answer with a degree input for L.

  1. first calculate the day of the year

  N1 = floor(275 * month / 9)
  N2 = floor((month + 9) / 12)
  N3 = (1 + floor((year - 4 * floor(year / 4) + 2) / 3))
  N = N1 - (N2 * N3) + day - 30
  */
  /*
  2. convert the longitude to hour value and calculate an approximate time

  lngHour = longitude / 15

  if rising time is desired:
  t = N + ((6 - lngHour) / 24)
  if setting time is desired:
  t = N + ((18 - lngHour) / 24)
  */
  double lngHour = longit / 15;
  double tris = dayOfYear + ((6 - lngHour) / 24);
  double tset = dayOfYear + ((18 - lngHour) / 24);
  /*

  3-6. calculate sun position for sunrise and sunset times
  */
  double sinDecris, cosDecris, raris;
  CalculateSunPosition(tris, sinDecris, cosDecris, raris);

  double sinDecset, cosDecset, raset;
  CalculateSunPosition(tset, sinDecset, cosDecset, raset);
  /*
  7a. calculate the Sun's local hour angle

  cosH = (cos(zenith) - (sinDec * sin(latitude))) / (cosDec * cos(latitude))

  if (cosH >  1)
  the sun never rises on this location (on the specified date)
  if (cosH < -1)
  the sun never sets on this location (on the specified date)
  */
  double cosZenith = cos(DEGREE * ZENITH_OFFICIAL);
  double coshris = (cosZenith - (sinDecris * sin(DEGREE * latit))) /
                   (cosDecris * cos(DEGREE * latit));
  double coshset = (cosZenith - (sinDecset * sin(DEGREE * latit))) /
                   (cosDecset * cos(DEGREE * latit));
  bool neverrises = false;
  if (coshris > 1) neverrises = true;
  if (coshris < -1)
    neverrises = true;  // nohal - it's cosine - even value lower than -1 is
  // illegal... correct me if i'm wrong
  bool neversets = false;
  if (coshset < -1) neversets = true;
  if (coshset > 1)
    neversets = true;  // nohal - it's cosine - even value greater than 1 is
  // illegal... correct me if i'm wrong
  /*
  7b. finish calculating H and convert into hours

  if if rising time is desired:
  H = 360 - acos(cosH)
  if setting time is desired:
  H = acos(cosH)

  H = H / 15
  */
  double hris = 360 - RADIAN * acos(coshris);
  hris = hris / 15;
  double hset = RADIAN * acos(coshset);
  hset = hset / 15;
  /*
  8. calculate local mean time of rising/setting

  T = H + RA - (0.06571 * t) - 6.622
  */
  tris = hris + raris - (0.06571 * tris) - 6.622;
  tset = hset + raset - (0.06571 * tset) - 6.622;
  /*
  9. adjust back to UTC

  UT = T - lngHour
  NOTE: UT potentially needs to be adjusted into the range [0,24) by
  adding/subtracting 24
  */
  double utris = tris - lngHour;
  if (utris > 24) utris -= 24;
  if (utris < 0) utris += 24;
  double utset = tset - lngHour;
  if (utset > 24) utset -= 24;
  if (utset < 0) utset += 24;

  sunrise = convHrmn(utris);
  if (neverrises) sunrise.SetYear(999);
  sunset = convHrmn(utset);
  if (neversets) sunset.SetYear(999);

  /*
  Optional: Calculate sun elevation for a specific time using the same
  algorithm.
  */
  if (dateTime != nullptr && sunElevation != nullptr) {
    // Handle polar cases
    if (neverrises) {
      *sunElevation = -30.0;  // Polar night
      return;
    }
    if (neversets) {
      *sunElevation = 30.0;  // Midnight sun
      return;
    }

    // Extract time and calculate t for the current time (reusing lngHour)
    double currentTimeHours = dateTime->GetHour() +
                              dateTime->GetMinute() / 60.0 +
                              dateTime->GetSecond() / 3600.0;
    double t = dayOfYear + ((currentTimeHours - lngHour) / 24);

    // Use helper function for steps 3-6
    double sinDec, cosDec, RA;
    CalculateSunPosition(t, sinDec, cosDec, RA);

    // Convert UTC time to Local Solar Time using longitude offset
    double localSolarHours = currentTimeHours + (longit / 15.0);

    // Apply equation of time correction (simplified)
    double dayAngle = 2.0 * PI * (dayOfYear - 1) / 365.0;
    double equationOfTime = 0.258 * cos(dayAngle) - 7.416 * sin(dayAngle) -
                            3.648 * cos(2 * dayAngle) -
                            9.228 * sin(2 * dayAngle);
    localSolarHours += equationOfTime / 60.0;  // Convert minutes to hours

    // Normalize to 24-hour cycle
    while (localSolarHours >= 24) localSolarHours -= 24;
    while (localSolarHours < 0) localSolarHours += 24;

    // Hour angle in degrees (15 degrees per hour from solar noon at 12:00)
    double hourAngle = 15.0 * (localSolarHours - 12.0);
    while (hourAngle > 180) hourAngle -= 360;
    while (hourAngle <= -180) hourAngle += 360;

    *sunElevation =
        RADIAN * asin(sinDec * sin(DEGREE * latit) +
                      cosDec * cos(DEGREE * latit) * cos(DEGREE * hourAngle));
  }
  /*
  Optional:
  10. convert UT value to local time zone of latitude/longitude

  localT = UT + localOffset
  */
}

/**
 * Determines if a given time at a particular location is daytime or nighttime.
 *
 * Uses the US Naval Observatory algorithm implemented in calculateSun()
 * and caches results to improve performance during routing calculations.
 *
 * @param lat Latitude of the location (-90 to 90)
 * @param lon Longitude of the location (-180 to 180)
 * @param time UTC time to evaluate
 * @return DayTime enum indicating whether it's day or night
 */
double SunCalculator::GetSunElevation(double lat, double lon,
                                      const wxDateTime& time) {
  if (!time.IsValid() || !std::isfinite(lat) || !std::isfinite(lon))
    return NAN;

  const double julianDay = time.GetTicks() / 86400.0 + 2440587.5;
  const double century = (julianDay - 2451545.0) / 36525.0;
  double geomLong = fmod(280.46646 + century * (36000.76983 +
                                                century * 0.0003032),
                         360.0);
  if (geomLong < 0) geomLong += 360.0;
  const double geomAnomaly =
      357.52911 + century * (35999.05029 - 0.0001537 * century);
  const double eccentricity =
      0.016708634 - century * (0.000042037 + 0.0000001267 * century);
  const double equationCenter =
      sin(DEGREE * geomAnomaly) *
          (1.914602 - century * (0.004817 + 0.000014 * century)) +
      sin(DEGREE * 2.0 * geomAnomaly) * (0.019993 - 0.000101 * century) +
      sin(DEGREE * 3.0 * geomAnomaly) * 0.000289;
  const double trueLong = geomLong + equationCenter;
  const double omega = 125.04 - 1934.136 * century;
  const double apparentLong = trueLong - 0.00569 -
                              0.00478 * sin(DEGREE * omega);
  const double meanObliquity =
      23.0 + (26.0 + (21.448 - century *
                                  (46.815 + century *
                                                (0.00059 - century * 0.001813))) /
                           60.0) /
                 60.0;
  const double obliquity =
      meanObliquity + 0.00256 * cos(DEGREE * omega);
  const double declination =
      asin(sin(DEGREE * obliquity) * sin(DEGREE * apparentLong));

  const double y = tan(DEGREE * obliquity / 2.0) *
                   tan(DEGREE * obliquity / 2.0);
  const double equationMinutes =
      4.0 * RADIAN *
      (y * sin(2.0 * DEGREE * geomLong) -
       2.0 * eccentricity * sin(DEGREE * geomAnomaly) +
       4.0 * eccentricity * y * sin(DEGREE * geomAnomaly) *
           cos(2.0 * DEGREE * geomLong) -
       0.5 * y * y * sin(4.0 * DEGREE * geomLong) -
       1.25 * eccentricity * eccentricity *
           sin(2.0 * DEGREE * geomAnomaly));
  const double utcMinutes = time.GetHour(wxDateTime::UTC) * 60.0 +
                            time.GetMinute(wxDateTime::UTC) +
                            time.GetSecond(wxDateTime::UTC) / 60.0;
  double solarMinutes = fmod(utcMinutes + equationMinutes + 4.0 * lon, 1440.0);
  if (solarMinutes < 0) solarMinutes += 1440.0;
  double hourAngle = solarMinutes / 4.0 - 180.0;
  const double elevation =
      asin(sin(DEGREE * lat) * sin(declination) +
           cos(DEGREE * lat) * cos(declination) *
               cos(DEGREE * hourAngle));
  return RADIAN * elevation;
}

DayLightStatus SunCalculator::GetDayLightStatus(double lat, double lon,
                                                const wxDateTime& time,
                                                double* sunElevation) {
  // Helper lambda to calculate precise sun elevation using US Naval Observatory
  // algorithm
  auto calculateElevation = [&]() -> double {
    if (sunElevation == nullptr) return 0.0;  // Skip calculation if not needed

    // Use the enhanced CalculateSun function with elevation calculation
    wxDateTime sunrise, sunset;
    double elevation;
    CalculateSun(lat, lon, time.GetDayOfYear(), sunrise, sunset, &time,
                 &elevation);
    return elevation;
  };
  // Use coarse-grained grid for caching (1-degree precision)
  // This reduces cache misses while maintaining acceptable accuracy
  int lat_index = static_cast<int>(round(lat));
  int lon_index = static_cast<int>(round(lon));
  int day_of_year = time.GetDayOfYear();
  int hourOfDay = time.GetHour(wxDateTime::UTC);

  wxCriticalSectionLocker lock(m_cacheLock);

  // Increment access counter
  m_accessCounter++;

  // Create a cache key for O(1) lookup
  CacheKey key = CacheKey::Pack(day_of_year, lat_index, lon_index);

  // Check if key exists in the map (O(1) lookup instead of linear search)
  auto mapIt = m_cacheMap.find(key);
  if (mapIt != m_cacheMap.end()) {
    // Cache hit - get the index to the entry in the vector
    size_t cacheIndex = mapIt->second;
    if (cacheIndex < m_cache.size()) {
      // Get the entry from the vector
      SunTimeCache& entry = m_cache[cacheIndex];

      // Update access counter (only occasionally to reduce overhead)
      if (m_accessCounter % 100 == 0) {
        entry.access_counter = m_accessCounter;
      }

      // Use cached year and hour values
      int sunriseYear = entry.sunriseYear;
      int sunsetYear = entry.sunsetYear;
      int sunriseHour = entry.sunriseHour;
      int sunsetHour = entry.sunsetHour;

      // Calculate sun elevation if requested (for cache hits)
      if (sunElevation != nullptr) {
        *sunElevation = calculateElevation();
      }

      // Handle edge cases
      if (sunriseYear == 999) {
        // Sun never rises (polar night) - elevation already set by
        // calculateElevation
        return DayLightStatus::Night;
      } else if (sunsetYear == 999) {
        // Sun never sets (midnight sun) - elevation already set by
        // calculateElevation
        return DayLightStatus::Day;
      }

      // Normal case: compare current time to sunrise/sunset
      if (sunsetHour < sunriseHour) {
        // Sun sets after midnight
        return (hourOfDay >= sunriseHour || hourOfDay < sunsetHour)
                   ? DayLightStatus::Day
                   : DayLightStatus::Night;
      } else {
        // Normal case: sunrise and sunset on same day
        return (hourOfDay >= sunriseHour && hourOfDay < sunsetHour)
                   ? DayLightStatus::Day
                   : DayLightStatus::Night;
      }
    }
  }

  // Cache miss - calculate new values
  wxDateTime sunrise, sunset;
  CalculateSun(lat, lon, day_of_year, sunrise, sunset);

  int sunriseYear = sunrise.GetYear();
  int sunsetYear = sunset.GetYear();
  int sunriseHour = sunrise.GetHour(wxDateTime::UTC);
  int sunsetHour = sunset.GetHour(wxDateTime::UTC);

  // Add to cache using LRU replacement policy if needed
  if (m_cache.size() >= MAX_SUN_CACHE_SIZE) {
    // Find least recently accessed entry
    auto oldest =
        std::min_element(m_cache.begin(), m_cache.end(),
                         [](const SunTimeCache& a, const SunTimeCache& b) {
                           return a.access_counter < b.access_counter;
                         });

    // Get the index of the oldest entry
    size_t oldestIndex = std::distance(m_cache.begin(), oldest);

    // Replace it with new entry
    oldest->day_of_year = day_of_year;
    oldest->lat_index = lat_index;
    oldest->lon_index = lon_index;
    oldest->sunriseYear = sunriseYear;
    oldest->sunsetYear = sunsetYear;
    oldest->sunriseHour = sunriseHour;
    oldest->sunsetHour = sunsetHour;
    oldest->access_counter = m_accessCounter;

    // Update the map with the new key -> same index
    m_cacheMap[key] = oldestIndex;
  } else {
    // Add new entry to the end of the vector
    m_cache.push_back({day_of_year, lat_index, lon_index, sunriseYear,
                       sunsetYear, sunriseHour, sunsetHour, m_accessCounter});

    // Add to the map with the index of the newly added entry
    m_cacheMap[key] = m_cache.size() - 1;
  }

  // Calculate sun elevation if requested (for cache misses)
  if (sunElevation != nullptr) {
    *sunElevation = calculateElevation();
  }

  // Determine day/night status with new calculation
  if (sunriseYear == 999) {
    // Polar night - elevation already set by calculateElevation
    return DayLightStatus::Night;
  } else if (sunsetYear == 999) {
    // Midnight sun - elevation already set by calculateElevation
    return DayLightStatus::Day;
  }
  if (sunsetHour < sunriseHour) {
    // Sun sets after midnight
    return (hourOfDay >= sunriseHour || hourOfDay < sunsetHour)
               ? DayLightStatus::Day
               : DayLightStatus::Night;
  } else {
    // Normal case: sunrise and sunset on same day
    return (hourOfDay >= sunriseHour && hourOfDay < sunsetHour)
               ? DayLightStatus::Day
               : DayLightStatus::Night;
  }
}
