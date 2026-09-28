/***************************************************************************
 * Copyright (C) 2026 OpenCPN contributors
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 2 of the License, or (at your option)
 * any later version.
 ***************************************************************************/

#ifndef WEATHER_ROUTING_SYSTEM_MEMORY_H
#define WEATHER_ROUTING_SYSTEM_MEMORY_H

#include <cstdint>

namespace weather_routing {

/** Available physical RAM, excluding swap. Zero means unavailable. */
std::uint64_t AvailablePhysicalMemoryBytes();

/** Whole MiB of headroom, with 1 for known sub-MiB pressure, 0 for unknown. */
std::uint64_t AvailablePhysicalMemoryMiB();

}  // namespace weather_routing

#endif
