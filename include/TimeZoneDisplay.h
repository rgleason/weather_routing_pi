/***************************************************************************
 *   Copyright (C) 2026 by the OpenCPN Weather Routing contributors        *
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 3 of the License, or     *
 *   (at your option) any later version.                                   *
 ***************************************************************************/

#ifndef WEATHER_ROUTING_TIME_ZONE_DISPLAY_H
#define WEATHER_ROUTING_TIME_ZONE_DISPLAY_H

#include <vector>

#include <wx/datetime.h>
#include <wx/string.h>

namespace marine_time {

enum class WallClockStatus {
  Valid,
  Ambiguous,
  Nonexistent,
  Invalid,
};

struct WallClockConversion {
  wxDateTime utc;
  WallClockStatus status = WallClockStatus::Invalid;
};

/** Return the IANA zones supported by the active C++ runtime. */
std::vector<wxString> AvailableTimeZones();

/** Return the operating system's IANA zone, or UTC when unavailable. */
wxString SystemTimeZone();

/** True when zoneName identifies a zone supported by this runtime. */
bool IsTimeZoneAvailable(const wxString& zoneName);

/**
 * Convert a UTC instant to a timezone wall clock.
 *
 * The returned wxDateTime is a field container: read/format it in
 * wxDateTime::UTC to preserve the selected zone's wall-clock fields.
 */
wxDateTime ToWallClock(const wxDateTime& utc, const wxString& zoneName);

/**
 * Convert wall-clock fields in an IANA zone to a UTC instant.
 *
 * Ambiguous autumn times resolve to the earlier occurrence. Nonexistent
 * spring-forward times are rejected.
 */
WallClockConversion FromWallClock(int year, int month, int day, int hour,
                                  int minute, int second,
                                  const wxString& zoneName);

/** Format a UTC instant in an IANA zone and append its abbreviation. */
wxString FormatInTimeZone(const wxDateTime& utc, const wxString& format,
                          const wxString& zoneName,
                          bool appendAbbreviation = true);

/** Return the abbreviation in effect at a UTC instant (for example BST). */
wxString TimeZoneAbbreviation(const wxDateTime& utc,
                              const wxString& zoneName);

}  // namespace marine_time

#endif
