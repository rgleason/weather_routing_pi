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

constexpr int kMainGribTimelineCacheDefault64BitMiB = 2048;
constexpr int kQuickGribTimelineCacheDefault64BitMiB = 2048;
constexpr int kMainGribTimelineCacheFallback64BitMiB = 512;
constexpr int kQuickGribTimelineCacheFallbackMiB = 64;
constexpr int kGribTimelineCacheMaximum32BitMiB = 192;
constexpr int kMainGribTimelineCacheDefaultMiB =
    sizeof(void*) <= 4 ? kGribTimelineCacheMaximum32BitMiB
                       : kMainGribTimelineCacheDefault64BitMiB;
constexpr int kQuickGribTimelineCacheDefaultMiB =
    sizeof(void*) <= 4 ? kQuickGribTimelineCacheFallbackMiB
                       : kQuickGribTimelineCacheDefault64BitMiB;
constexpr int kGribTimelineCacheMaximum64BitMiB = 8192;
constexpr std::uint64_t kGribTimelineCacheBaseReserveMiB = 2048;

// Separate the preferred first-use limit from the historical low-memory floor.
// Raising a default must never implicitly exempt it from memory admission.
inline int GribTimelineCacheFallbackMiB(
    bool quick, unsigned process_bits = sizeof(void*) * 8U) {
  return quick ? kQuickGribTimelineCacheFallbackMiB
               : (process_bits <= 32 ? kGribTimelineCacheMaximum32BitMiB
                                     : kMainGribTimelineCacheFallback64BitMiB);
}

inline int NormalizeGribTimelineCacheMiB(int requested, bool quick,
                                         unsigned process_bits =
                                             sizeof(void*) * 8U) {
  const int fallback =
      quick ? (process_bits <= 32 ? kQuickGribTimelineCacheFallbackMiB
                                  : kQuickGribTimelineCacheDefault64BitMiB)
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
 * Admit a larger timeline cache only when physical RAM can
 * contain the complete cache and still leave 2 GiB plus twice its size free.
 * Step down to a smaller power-of-two allowance when necessary. Historical
 * floors remain available when memory reporting is unavailable or smaller.
 */
inline GribTimelineCacheAdmission EvaluateGribTimelineCacheAdmission(
    int requested_mib, bool quick, std::uint64_t available_mib,
    unsigned process_bits = sizeof(void*) * 8U) {
  GribTimelineCacheAdmission result;
  result.requested_mib = NormalizeGribTimelineCacheMiB(
      requested_mib, quick, process_bits);
  const int standard = GribTimelineCacheFallbackMiB(quick, process_bits);
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
    for (int candidate = standard * 2; candidate < result.requested_mib;
         candidate *= 2) {
      if (available_mib < kGribTimelineCacheBaseReserveMiB + 3ULL * candidate)
        break;
      result.effective_mib = candidate;
    }
    result.approved = false;
    return result;
  }
  result.approved = true;
  return result;
}

}  // namespace weather_routing

#endif
