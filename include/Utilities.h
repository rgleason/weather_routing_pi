/***************************************************************************
 *   Copyright (C) 2015 by Sean D'Epagnier                                 *
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

#ifndef _WEATHER_ROUTING_UTILITIES_H_
#define _WEATHER_ROUTING_UTILITIES_H_

#ifdef __MSVC__
#include <float.h>
#include <iostream>
#include <limits>

#if !defined(M_PI)
#define M_PI 3.14159265358979323846 /* pi */
#endif

#if !defined(NAN)
#define NAN std::numeric_limits<double>::quiet_NaN()
#endif

#if !defined(INFINITY)
#define INFINITY std::numeric_limits<double>::infinity()
#endif

inline double trunc(double d) { return (d > 0) ? floor(d) : ceil(d); }
inline double round(double n) {
  return n < 0.0 ? ceil(n - 0.5) : floor(n + 0.5);
}

#if !defined(snprintf)
#define snprintf _snprintf
#endif
#define vsnprintf _vsnprintf
#define strcasecmp _stricmp
#define strncasecmp _strnicmp

#define strtok_r strtok_s
#endif

#ifdef __MINGW32__
char* strtok_r(char* str, const char* delim, char** save);
#endif

#include <cmath>

/* min must have correct paren to make predence correct */
#ifdef MIN
#undef MIN
#endif
#define MIN(a, b) ((a) < (b) ? (a) : (b))

#ifdef MAX
#undef MAX
#endif
#define MAX(a, b) ((a) > (b) ? (a) : (b))

/**
 * Convert degrees to radians
 * @param degrees Angle in degrees
 * @return Equivalent angle in radians
 */
double deg2rad(double degrees);

/**
 * Convert radians to degrees
 * @param radians Angle in radians
 * @return Equivalent angle in degrees
 */
double rad2deg(double radians);

/**
 * Normalize heading angle to range [-180, 180]
 * @param degrees Angle in degrees (any range)
 * @return Normalized angle in range [-180, 180]
 */
double heading_resolve(double degrees);

/**
 * Convert angle to positive degrees in range [0, 360)
 * @param degrees Angle in degrees (any range)
 * @return Positive angle in range [0, 360)
 */
double positive_degrees(double degrees);

/**
 * Convert radians to positive degrees in range [0, 360)
 * @param radians Angle in radians
 * @return Positive angle in degrees in range [0, 360)
 */
double rad2posdeg(double radians);

#define ft2m(X) (X * .3048)
#define m2ft(X) (X * 3.28084)
#define m_s2knots(X) (X * 1.94384)
#define knots2m_s(X) (X * .514444)

double square(double x);
double cube(double x);

double average_longitude(double lon1, double lon2);

class TiXmlElement;
double AttributeDouble(TiXmlElement* e, const char* name, double def);
int AttributeInt(TiXmlElement* e, const char* name, int def);
bool AttributeBool(TiXmlElement* e, const char* name, bool def);

#include <wx/datetime.h>
#include <wx/string.h>
// CUSTOMIZATION
wxString calculateTimeDelta(wxDateTime startTime, wxDateTime endTime);

#endif
