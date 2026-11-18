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
#include <deque>
#include <optional>
#include <mutex>

#include "WeatherDataProvider.h"
#include "GribRecordSet.h"
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
  enum class Status { NoResult, Available, NoFrame, NoWind, NoReply, OutOfMemory };
  struct Reply {
    Shared_GribRecordSet frame;
    Status status{Status::NoResult};
  };

  using Cache = KeyedRequestCache<std::int64_t, Reply>;
  using Statistics = Cache::Statistics;
  using Request = Cache::Request;

  GribTimelineFrameCache();

  GribTimelineCacheAdmission Configure(int requested_mib, bool quick,
                                        std::uint64_t available_mib);
  Reply Acquire(std::int64_t key, long timeout_milliseconds,
                const std::function<bool()>& cancelled,
                bool* cache_hit = nullptr,
                std::atomic<unsigned>* waiters = nullptr);
  // Dispatch from the host thread. Unclaimed requests survive cancellation
  // of the first caller as long as another worker still needs the frame.
  std::optional<Request> TakePendingRequest(bool allowPrefetch = false);
  void WakeHost();
  void SetRequestWakeup(std::function<void()> wakeup);
  bool TryResident(std::int64_t key, Shared_GribRecordSet* value) const;
  void PublishFrame(const Request& request, const GribRecordSet* grib);
  bool Publish(const Request& request, const Reply& reply);
  void PublishUnavailable(const Request& request);
  void PublishOutOfMemory(const Request& request);
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
  void PrefetchNextSlice(const Request&, std::size_t bytes);

  // Retained grid bytes, admitted against physical/process memory, are the
  // primary limit. A 512-entry ceiling discarded small frames long before
  // that budget was used. Keep only a generous metadata safety ceiling.
  static constexpr std::size_t kFrameCapacity = 32768;
  // A missing frame owns no GRIB grids. Retain its status separately across
  // long passages and engine attempts without increasing the frame budget.
  static constexpr std::size_t kMissingFrameCapacity = 32768;
  Cache cache_;
  std::mutex queue_mutex_;
  std::deque<Request> requests_;
  std::optional<Request> next_slice_;
  std::function<void()> request_wakeup_;
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
