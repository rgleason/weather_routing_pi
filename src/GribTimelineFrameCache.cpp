/***************************************************************************
 * Copyright (C) 2026 OpenCPN contributors
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 3, or (at your option) any later version.
 ***************************************************************************/

#include "GribTimelineFrameCache.h"

#include <algorithm>
#include <chrono>
#include <limits>

#include <wx/log.h>

#include "SystemMemory.h"

namespace {

constexpr std::uint64_t kMiB = 1024ULL * 1024ULL;

std::int64_t MonotonicMilliseconds() {
  return std::chrono::duration_cast<std::chrono::milliseconds>(
             std::chrono::steady_clock::now().time_since_epoch())
      .count();
}

std::size_t MiBToBytes(std::uint64_t mib) {
  if (mib > std::numeric_limits<std::size_t>::max() / kMiB)
    return std::numeric_limits<std::size_t>::max();
  return static_cast<std::size_t>(mib * kMiB);
}

}  // namespace

namespace weather_routing {

GribTimelineFrameCache::GribTimelineFrameCache()
    : cache_(kFrameCapacity,
             MiBToBytes(kMainGribTimelineCacheDefaultMiB),
             [](const Shared_GribRecordSet& frame) {
               return frame.EstimatedMemoryBytes();
             }) {}

GribTimelineCacheAdmission GribTimelineFrameCache::Configure(
    int requested_mib, bool quick, std::uint64_t available_mib) {
  const auto admission = EvaluateGribTimelineCacheAdmission(
      requested_mib, quick, available_mib);
  requested_mib_ = admission.requested_mib;
  effective_mib_ = admission.effective_mib;
  current_limit_mib_ = effective_mib_;
  standard_mib_ = quick ? kQuickGribTimelineCacheDefaultMiB
                        : kMainGribTimelineCacheDefaultMiB;
  required_reserve_mib_ = admission.required_reserve_mib;
  runtime_guard_enabled_ = admission.approved && admission.large_cache_requested;
  configured_ = true;
  next_memory_check_milliseconds_ = 0;
  cache_.SetMaximumWeight(MiBToBytes(effective_mib_));
  return admission;
}

bool GribTimelineFrameCache::Acquire(
    const std::int64_t& key, Shared_GribRecordSet* value,
    long timeout_milliseconds,
    const std::function<void(const std::int64_t&)>& request,
    const std::function<bool()>& cancelled) {
  return cache_.Acquire(key, value, timeout_milliseconds, request, cancelled);
}

bool GribTimelineFrameCache::Publish(std::int64_t key,
                                     const Shared_GribRecordSet& value,
                                     bool valid) {
  ApplyRuntimeMemoryGuard();
  return cache_.Publish(key, value, valid);
}

void GribTimelineFrameCache::NotifyAll() { cache_.NotifyAll(); }

std::size_t GribTimelineFrameCache::Clear(bool reset_statistics) {
  const std::size_t released = cache_.TotalWeight();
  cache_.Clear(reset_statistics);
  next_memory_check_milliseconds_ = 0;
  return released;
}

std::size_t GribTimelineFrameCache::TotalWeight() const {
  return cache_.TotalWeight();
}

std::size_t GribTimelineFrameCache::Size() const { return cache_.Size(); }

GribTimelineFrameCache::Statistics GribTimelineFrameCache::Stats() const {
  return cache_.Stats();
}

void GribTimelineFrameCache::ApplyRuntimeMemoryGuard() {
  if (!runtime_guard_enabled_) return;
  const std::int64_t now = MonotonicMilliseconds();
  if (now < next_memory_check_milliseconds_) return;
  next_memory_check_milliseconds_ = now + 1000;

  const std::uint64_t available_bytes = AvailablePhysicalMemoryBytes();
  if (!available_bytes) return;
  const std::uint64_t available_mib = available_bytes / kMiB;
  const std::uint64_t current_mib = (cache_.TotalWeight() + kMiB - 1) / kMiB;
  const std::uint64_t growable_mib =
      available_mib > required_reserve_mib_
          ? available_mib - required_reserve_mib_
          : 0;
  const std::uint64_t safe_mib = std::min<std::uint64_t>(
      effective_mib_, current_mib + growable_mib);
  const int guarded_mib = static_cast<int>(std::max<std::uint64_t>(
      standard_mib_, safe_mib));
  if (guarded_mib != current_limit_mib_) {
    wxLogMessage(
        "WR_GRIB_TIMELINE_CACHE_GUARD previous_limit_mib=%d "
        "new_limit_mib=%d retained_mib=%llu available_mib=%llu "
        "required_reserve_mib=%llu",
        current_limit_mib_, guarded_mib,
        static_cast<unsigned long long>(current_mib),
        static_cast<unsigned long long>(available_mib),
        static_cast<unsigned long long>(required_reserve_mib_));
    current_limit_mib_ = guarded_mib;
  }
  cache_.SetMaximumWeight(MiBToBytes(guarded_mib));
}

}  // namespace weather_routing
