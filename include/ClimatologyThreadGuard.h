/***************************************************************************
 *   Copyright (C) 2026 by OpenCPN development team                        *
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 3 of the License, or     *
 *   (at your option) any later version.                                   *
 ***************************************************************************/

#ifndef _WEATHER_ROUTING_CLIMATOLOGY_THREAD_GUARD_H_
#define _WEATHER_ROUTING_CLIMATOLOGY_THREAD_GUARD_H_

#include <mutex>

enum class ClimatologyService {
  Wind,
  Current,
  WindAtlas,
  CycloneTracks
};

class ClimatologyThreadGuard {
public:
  static void Reset();
  static void MarkPrepared(ClimatologyService service);
  static bool IsPrepared(ClimatologyService service);
  static bool CanInvoke(ClimatologyService service, bool isMainThread);
  static bool ShouldLogBlocked(ClimatologyService service);
  /**
   * Serialize calls into the legacy climatology callback table.  Preparation
   * makes worker calls legal; this lock makes concurrent calls deterministic
   * even when an installed climatology provider still uses shared scratch
   * storage internally.
   */
  static std::recursive_mutex& InvocationMutex();
};

#endif
