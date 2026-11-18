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
             [](const Reply& reply) {
               return reply.frame.EstimatedMemoryBytes();
             }, kMissingFrameCapacity) {}

GribTimelineCacheAdmission GribTimelineFrameCache::Configure(
    int requested_mib, bool quick, std::uint64_t available_mib) {
  const auto admission = EvaluateGribTimelineCacheAdmission(
      requested_mib, quick, available_mib);
  requested_mib_ = admission.requested_mib;
  effective_mib_ = admission.effective_mib;
  current_limit_mib_ = effective_mib_;
  standard_mib_ = GribTimelineCacheFallbackMiB(quick);
  required_reserve_mib_ = kGribTimelineCacheBaseReserveMiB +
                          2ULL * effective_mib_;
  // A reduced allowance may still exceed the historical floor and needs the
  // same runtime guard as a fully admitted request.
  runtime_guard_enabled_ = true;
  configured_ = true;
  next_memory_check_milliseconds_ = 0;
  cache_.SetMaximumWeight(MiBToBytes(effective_mib_));
  return admission;
}

GribTimelineFrameCache::Reply GribTimelineFrameCache::Acquire(
    std::int64_t key, long timeout_milliseconds,
    const std::function<bool()>& cancelled, bool* cache_hit,
    std::atomic<unsigned>* waiters) {
  Reply reply;
  cache_.AcquireRequest(key, &reply, timeout_milliseconds,
      [this](const Request& request) {
        std::function<void()> wakeup;
        {
          std::lock_guard<std::mutex> guard(queue_mutex_);
          requests_.push_back(request);
          wakeup = request_wakeup_;
        }
        if (wakeup) wakeup();
      }, cancelled, cache_hit, waiters);
  return reply;
}

std::optional<GribTimelineFrameCache::Request>
GribTimelineFrameCache::TakePendingRequest(bool allowPrefetch) {
  std::lock_guard<std::mutex> guard(queue_mutex_);
  while (!requests_.empty()) {
    const auto request = requests_.front();
    requests_.pop_front();
    if (cache_.IsPending(request)) return request;
  }
  if (next_slice_ && (allowPrefetch || cache_.HasWaiters(*next_slice_))) {
    const auto request = std::exchange(next_slice_, std::nullopt);
    if (cache_.IsPending(*request)) return request;
  }
  return std::nullopt;
}

void GribTimelineFrameCache::WakeHost() {
  std::function<void()> wakeup;
  {
    std::lock_guard<std::mutex> guard(queue_mutex_);
    wakeup = request_wakeup_;
  }
  if (wakeup) wakeup();
}

void GribTimelineFrameCache::PrefetchNextSlice(const Request& request, std::size_t bytes) {
  // Canonical native forecast sampling uses quarter-hour buckets. Keep one
  // speculative slice in reserve, with room for several ordinary replies.
  constexpr std::int64_t cadence = 15 * 60;
  if (request.prefetch || request.key % cadence || !configured_) return;
  const auto limit = MiBToBytes(current_limit_mib_);
  if (bytes > limit / 4 || cache_.TotalWeight() > limit - 4 * bytes) return;
  try {
    std::lock_guard<std::mutex> guard(queue_mutex_);
    if (!next_slice_ || !cache_.IsPending(*next_slice_))
      next_slice_ = cache_.Prefetch(request.key + cadence);
  } catch (const std::bad_alloc&) {
    // Lookahead is optional. The next demand request uses the normal path.
  }
}

void GribTimelineFrameCache::SetRequestWakeup(std::function<void()> wakeup) {
  std::lock_guard<std::mutex> guard(queue_mutex_);
  request_wakeup_ = std::move(wakeup);
}

bool GribTimelineFrameCache::TryResident(std::int64_t key,
                                         Shared_GribRecordSet* value) const {
  Reply reply;
  if (!value || !cache_.TryResident(key, &reply)) return false;
  *value = std::move(reply.frame);
  return true;
}

bool GribTimelineFrameCache::Publish(const Request& request,
                                     const Reply& reply) {
  ApplyRuntimeMemoryGuard();
  return cache_.Publish(request, reply, reply.status == Status::Available,
                        {{}, Status::OutOfMemory});
}

void GribTimelineFrameCache::PublishFrame(
    const Request& request, const GribRecordSet* grib) {
  if (!grib || !grib->m_GribRecordPtrArray[Idx_WIND_VX] ||
      !grib->m_GribRecordPtrArray[Idx_WIND_VY]) {
    if (cache_.DiscardUnusedPrefetch(request)) return;
    Publish(request, {{}, grib ? Status::NoWind : Status::NoFrame});
    return;
  }

  const auto* wind = grib->m_GribRecordPtrArray[Idx_WIND_VX];
  // The ABI does not expose the provider's record-set ID.
  const unsigned int id = static_cast<unsigned int>(wind->getRecordRefDate()) ^
      (static_cast<unsigned int>(wind->getIdCenter()) << 24) ^
      (static_cast<unsigned int>(wind->getNi()) << 16);
  /* copy the grib record set */
  try {
    auto copied = std::make_shared<WR_GribRecordSet>(id);
    copied->m_Reference_Time = grib->m_Reference_Time;
    for (int i = 0; i < Idx_COUNT; i++) {
      switch (i) {
        case Idx_HTSIGW:
        case Idx_WVDIR:
        case Idx_WVPER:
        case Idx_WIND_GUST:
        case Idx_WIND_VX:
        case Idx_WIND_VY:
        case Idx_SEACURRENT_VX:
        case Idx_SEACURRENT_VY:
        case Idx_AIR_TEMP:
        case Idx_CAPE:
        case Idx_CLOUD_TOT:
        case Idx_HUMID_RE:
        case Idx_PRECIP_TOT:
        case Idx_SEA_TEMP:
        case Idx_PRESSURE:
        case Idx_COMP_REFL:
          if (grib->m_GribRecordPtrArray[i]) {
            copied->SetUnRefGribRecord(
                i, new GribRecord(*grib->m_GribRecordPtrArray[i]));
          }
          break;
        default:
          break;
      }
    }
    Shared_GribRecordSet frame(std::move(copied));
    const auto bytes = frame.EstimatedMemoryBytes();
    if (request.prefetch) {
      ApplyRuntimeMemoryGuard();
      const auto limit = MiBToBytes(current_limit_mib_);
      if ((bytes > limit || cache_.TotalWeight() > limit - bytes) &&
          cache_.DiscardUnusedPrefetch(request))
        return;
    }
    if (Publish(request, {frame, Status::Available})) PrefetchNextSlice(request, bytes);
  } catch (const std::bad_alloc&) {
    PublishOutOfMemory(request);
  }
}

void GribTimelineFrameCache::PublishUnavailable(const Request& request) {
  if (!cache_.DiscardUnusedPrefetch(request))
    cache_.PublishUnavailable(request, {{}, Status::NoReply});
}

void GribTimelineFrameCache::PublishOutOfMemory(const Request& request) {
  cache_.Abandon(request, {{}, Status::OutOfMemory});
}

void GribTimelineFrameCache::NotifyAll() { cache_.NotifyAll(); }

std::size_t GribTimelineFrameCache::Clear(bool reset_statistics) {
  const std::size_t released = cache_.TotalWeight();
  std::lock_guard<std::mutex> guard(queue_mutex_);
  requests_.clear();
  next_slice_.reset();
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

  const std::uint64_t available_mib = AvailablePhysicalMemoryMiB();
  if (!available_mib) return;
  const std::uint64_t current_mib = (cache_.TotalWeight() + kMiB - 1) / kMiB;
  const std::uint64_t growable_mib =
      available_mib > required_reserve_mib_
          ? available_mib - required_reserve_mib_
          : 0;
  const std::uint64_t safe_mib = std::min<std::uint64_t>(
      effective_mib_, current_mib + growable_mib);
  int guarded_mib = static_cast<int>(std::max<std::uint64_t>(
      standard_mib_, safe_mib));
  guarded_mib = std::min(guarded_mib, current_limit_mib_);
  if (available_mib < kGribTimelineCacheBaseReserveMiB)
    guarded_mib = PressureCacheLimitMiB(current_limit_mib_, available_mib);
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
