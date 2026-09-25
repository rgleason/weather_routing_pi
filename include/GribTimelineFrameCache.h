/***************************************************************************
 * Copyright (C) 2026 OpenCPN contributors
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 3, or (at your option) any later version.
 ***************************************************************************/

#ifndef WEATHER_ROUTING_GRIB_TIMELINE_FRAME_CACHE_H
#define WEATHER_ROUTING_GRIB_TIMELINE_FRAME_CACHE_H

#include <cstdint>
#include <functional>

#include "WeatherDataProvider.h"
#include "GribTimelineCachePolicy.h"
#include "KeyedRequestCache.h"

namespace weather_routing {

/**
 * Aggregate cache for copied/interpolated GRIB frames used by one complete
 * computation batch.  A batch includes departure-time candidates and
 * multileg continuations, so its configured limit is never multiplied by the
 * number of route workers.
 */
class GribTimelineFrameCache {
public:
  using Cache = KeyedRequestCache<std::int64_t, Shared_GribRecordSet>;
  using Statistics = Cache::Statistics;

  GribTimelineFrameCache();

  GribTimelineCacheAdmission Configure(int requested_mib, bool quick,
                                        std::uint64_t available_mib);
  bool Acquire(const std::int64_t& key, Shared_GribRecordSet* value,
               long timeout_milliseconds,
               const std::function<void(const std::int64_t&)>& request,
               const std::function<bool()>& cancelled);
  bool Publish(std::int64_t key, const Shared_GribRecordSet& value,
               bool valid);
  void NotifyAll();

  /** Release every cached frame and return the estimated bytes released. */
  std::size_t Clear(bool reset_statistics = true);
  std::size_t TotalWeight() const;
  std::size_t Size() const;
  Statistics Stats() const;
  int EffectiveMiB() const { return effective_mib_; }
  int RequestedMiB() const { return requested_mib_; }
  bool IsConfigured() const { return configured_; }

private:
  void ApplyRuntimeMemoryGuard();

  static constexpr std::size_t kFrameCapacity = 512;
  Cache cache_;
  int requested_mib_{kMainGribTimelineCacheDefaultMiB};
  int effective_mib_{kMainGribTimelineCacheDefaultMiB};
  int current_limit_mib_{kMainGribTimelineCacheDefaultMiB};
  int standard_mib_{GribTimelineCacheFallbackMiB(false)};
  std::uint64_t required_reserve_mib_{};
  bool runtime_guard_enabled_{};
  bool configured_{};
  std::int64_t next_memory_check_milliseconds_{};
};

}  // namespace weather_routing

#endif  // WEATHER_ROUTING_GRIB_TIMELINE_FRAME_CACHE_H
