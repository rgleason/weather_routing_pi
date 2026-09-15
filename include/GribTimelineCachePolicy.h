/***************************************************************************
 * Copyright (C) 2026 OpenCPN contributors
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 2 of the License, or (at your option)
 * any later version.
 ***************************************************************************/

#ifndef WEATHER_ROUTING_GRIB_TIMELINE_CACHE_POLICY_H
#define WEATHER_ROUTING_GRIB_TIMELINE_CACHE_POLICY_H

#include <algorithm>
#include <cstdint>

namespace weather_routing {

constexpr int kMainGribTimelineCacheDefault64BitMiB = 512;
constexpr int kGribTimelineCacheMaximum32BitMiB = 192;
constexpr int kMainGribTimelineCacheDefaultMiB =
    sizeof(void*) <= 4 ? kGribTimelineCacheMaximum32BitMiB
                       : kMainGribTimelineCacheDefault64BitMiB;
constexpr int kQuickGribTimelineCacheDefaultMiB = 64;
constexpr int kGribTimelineCacheMaximum64BitMiB = 8192;
constexpr std::uint64_t kGribTimelineCacheBaseReserveMiB = 2048;

inline int NormalizeGribTimelineCacheMiB(int requested, bool quick,
                                         unsigned process_bits =
                                             sizeof(void*) * 8U) {
  const int fallback =
      quick ? kQuickGribTimelineCacheDefaultMiB
            : (process_bits <= 32 ? kGribTimelineCacheMaximum32BitMiB
                                  : kMainGribTimelineCacheDefault64BitMiB);
  if (requested <= 0) requested = fallback;
  const int maximum = process_bits <= 32
                          ? kGribTimelineCacheMaximum32BitMiB
                          : kGribTimelineCacheMaximum64BitMiB;
  return std::clamp(requested, 16, maximum);
}

struct GribTimelineCacheAdmission {
  int requested_mib{};
  int effective_mib{};
  std::uint64_t available_mib{};
  std::uint64_t required_before_mib{};
  std::uint64_t required_reserve_mib{};
  bool large_cache_requested{};
  bool approved{};
  bool memory_known{};
};

/**
 * Admit an explicitly enlarged timeline cache only when physical RAM can
 * contain the complete cache and still leave 2 GiB plus twice its size free.
 * Standard historical limits remain available on machines where memory
 * reporting is unavailable or smaller, preserving existing routing support.
 */
inline GribTimelineCacheAdmission EvaluateGribTimelineCacheAdmission(
    int requested_mib, bool quick, std::uint64_t available_mib,
    unsigned process_bits = sizeof(void*) * 8U) {
  GribTimelineCacheAdmission result;
  result.requested_mib = NormalizeGribTimelineCacheMiB(
      requested_mib, quick, process_bits);
  const int standard =
      quick ? kQuickGribTimelineCacheDefaultMiB
            : (process_bits <= 32 ? kGribTimelineCacheMaximum32BitMiB
                                  : kMainGribTimelineCacheDefault64BitMiB);
  result.effective_mib = result.requested_mib;
  result.available_mib = available_mib;
  result.memory_known = available_mib != 0;
  result.large_cache_requested = result.requested_mib > standard;
  result.required_reserve_mib =
      kGribTimelineCacheBaseReserveMiB +
      2ULL * static_cast<std::uint64_t>(result.requested_mib);
  result.required_before_mib =
      result.required_reserve_mib +
      static_cast<std::uint64_t>(result.requested_mib);

  if (process_bits <= 32) {
    result.effective_mib = std::min(result.requested_mib,
                                    kGribTimelineCacheMaximum32BitMiB);
    result.approved = result.effective_mib == result.requested_mib;
    return result;
  }
  if (!result.large_cache_requested) {
    result.approved = true;
    return result;
  }
  if (!result.memory_known || available_mib < result.required_before_mib) {
    result.effective_mib = standard;
    result.approved = false;
    return result;
  }
  result.approved = true;
  return result;
}

}  // namespace weather_routing

#endif
