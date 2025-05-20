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

void SunCalculator::CalculateSun(double latit, double longit, int dayOfYear,
                                 wxDateTime& sunrise, wxDateTime& sunset) {
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

  3. calculate the Sun's mean anomaly

  M = (0.9856 * t) - 3.289
  */
  double mris = (0.9856 * tris) - 3.289;
  double mset = (0.9856 * tset) - 3.289;
  /*
  4. calculate the Sun's true longitude

  L = M + (1.916 * sin(M)) + (0.020 * sin(2 * M)) + 282.634
  NOTE: L potentially needs to be adjusted into the range [0,360) by
  adding/subtracting 360
  */
  double lris = mris + (1.916 * sin(DEGREE * mris)) +
                (0.020 * sin(2 * DEGREE * mris)) + 282.634;
  if (lris > 360) lris -= 360;
  if (lris < 0) lris += 360;
  double lset = mset + (1.916 * sin(DEGREE * mset)) +
                (0.020 * sin(2 * DEGREE * mset)) + 282.634;
  if (lset > 360) lset -= 360;
  if (lset < 0) lset += 360;
  /*
  5a. calculate the Sun's right ascension

  RA = atan(0.91764 * tan(L))
  NOTE: RA potentially needs to be adjusted into the range [0,360) by
  adding/subtracting 360
  */
  double raris = RADIAN * atan(0.91764 * tan(DEGREE * lris));
  if (raris > 360) raris -= 360;
  if (raris < 0) raris += 360;
  double raset = RADIAN * atan(0.91764 * tan(DEGREE * lset));
  if (raset > 360) raset -= 360;
  if (raset < 0) raset += 360;
  /*
  5b. right ascension value needs to be in the same quadrant as L

  Lquadrant  = (floor( L/90)) * 90
  RAquadrant = (floor(RA/90)) * 90
  RA = RA + (Lquadrant - RAquadrant)
  */
  double lqris = (floor(lris / 90)) * 90;
  double raqris = (floor(raris / 90)) * 90;
  raris = raris + (lqris - raqris);
  double lqset = (floor(lset / 90)) * 90;
  double raqset = (floor(raset / 90)) * 90;
  raset = raset + (lqset - raqset);
  /*
  5c. right ascension value needs to be converted into hours

  RA = RA / 15
  */
  raris = raris / 15;
  raset = raset / 15;
  /*
  6. calculate the Sun's declination

  sinDec = 0.39782 * sin(L)
  cosDec = cos(asin(sinDec))
  */
  double sinDecris = 0.39782 * sin(DEGREE * lris);
  double cosDecris = cos(asin(sinDecris));
  double sinDecset = 0.39782 * sin(DEGREE * lset);
  double cosDecset = cos(asin(sinDecset));
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
  // ilegal... correct me if i'm wrong
  bool neversets = false;
  if (coshset < -1) neversets = true;
  if (coshset > 1)
    neversets = true;  // nohal - it's cosine - even value greater than 1 is
  // ilegal... correct me if i'm wrong
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
DayLightStatus SunCalculator::GetDayLightStatus(double lat, double lon,
                                                const wxDateTime& time) {
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

      // Handle edge cases
      if (sunriseYear == 999) {
        // Sun never rises (polar night)
        return DayLightStatus::Night;
      } else if (sunsetYear == 999) {
        // Sun never sets (midnight sun)
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

  // Determine day/night status with new calculation
  if (sunriseYear == 999) {
    return DayLightStatus::Night;  // Polar night
  } else if (sunsetYear == 999) {
    return DayLightStatus::Day;  // Midnight sun
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