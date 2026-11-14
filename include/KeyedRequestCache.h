/***************************************************************************
 * Copyright (C) 2026 OpenCPN development team
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 3, or (at your option) any later version.
 ***************************************************************************/

#ifndef WEATHER_ROUTING_KEYED_REQUEST_CACHE_H
#define WEATHER_ROUTING_KEYED_REQUEST_CACHE_H

#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <iterator>
#include <list>
#include <limits>
#include <map>
#include <mutex>
#include <new>
#include <utility>

namespace weather_routing {

/**
 * Coalesce synchronous host-service requests and publish replies by key.
 *
 * At most one cache miss is exposed to the host at a time. Concurrent callers
 * for the same key share its result; callers for other keys wait their turn.
 * A reply is always stored under the key supplied by the requester, never
 * under mutable "last request" state.
 */
template <typename Key, typename Value>
class KeyedRequestCache {
public:
  using WeightFunction = std::function<std::size_t(const Value&)>;
  using Clock = std::chrono::steady_clock;
  using NowFunction = std::function<Clock::time_point()>;

  struct Statistics {
    std::uint64_t lookups{};
    std::uint64_t hits{};
    std::uint64_t negative_hits{};
    std::uint64_t misses{};
    std::uint64_t evictions{};
    std::uint64_t reloads_after_eviction{};
    std::uint64_t publications{};
    std::uint64_t unavailable_replies{};
    std::uint64_t unavailable_skips{};
    std::uint64_t timeouts{};
    std::uint64_t wait_microseconds{};
    std::size_t distinct_keys{};
    std::size_t peak_entries{};
    std::size_t peak_weight{};
    std::size_t positive_entries{};
    std::size_t negative_entries{};
  };

  explicit KeyedRequestCache(std::size_t capacity)
      : KeyedRequestCache(capacity, std::numeric_limits<std::size_t>::max(),
                           {}) {}

  KeyedRequestCache(std::size_t capacity, std::size_t maximum_weight,
                    WeightFunction weight_function,
                    std::size_t negative_capacity = 0,
                    NowFunction now = [] { return Clock::now(); })
      : capacity_(capacity),
        negative_capacity_(negative_capacity ? negative_capacity : capacity),
        maximum_weight_(maximum_weight),
        weight_function_(std::move(weight_function)),
        now_(std::move(now)) {}

  KeyedRequestCache(const KeyedRequestCache&) = delete;
  KeyedRequestCache& operator=(const KeyedRequestCache&) = delete;

  // Advisory inspection only: never issue a request, wait for a reply, mark
  // a key missing, change recency or alter ordinary acquisition statistics.
  bool TryResident(const Key& key, Value* value) const {
    if (!value) return false;
    std::unique_lock<std::mutex> lock(mutex_, std::try_to_lock);
    if (!lock.owns_lock()) return false;
    const auto found = entries_.find(key);
    if (found == entries_.end() || !found->second.valid) return false;
    *value = found->second.value;
    return true;
  }

  bool Acquire(const Key& key, Value* value, long timeout_milliseconds,
               const std::function<void(const Key&)>& request,
               const std::function<bool()>& cancelled) {
    if (!value || !request || timeout_milliseconds < 0) return false;
    const auto deadline = std::chrono::steady_clock::now() +
                          std::chrono::milliseconds(timeout_milliseconds);
    bool owns_request = false;
    bool issued_request = false;
    bool counted_lookup = false;

    for (;;) {
      std::unique_lock<std::mutex> lock(mutex_);
      if (!counted_lookup) {
        ++statistics_.lookups;
        counted_lookup = true;
      }
      const auto found = entries_.find(key);
      if (found != entries_.end()) {
        // Reading our own newly published reply is a miss, not a cache hit.
        if (!issued_request) {
          ++statistics_.hits;
          if (!found->second.valid) ++statistics_.negative_hits;
        }
        *value = found->second.value;
        TouchLocked(found->second);
        return found->second.valid;
      }
      if (cancelled && cancelled()) return false;
      // A transport failure says nothing about a particular forecast time.
      // Fail promptly during a bounded provider-wide cooldown, then allow
      // the ordinary single-flight path to probe the provider again.
      if (retry_after_ > now_()) {
        if (!issued_request) ++statistics_.unavailable_skips;
        *value = Value{};
        return false;
      }

      if (!request_active_) {
        const auto seen = seen_keys_.emplace(key, false);
        request_active_ = true;
        active_key_ = key;
        owns_request = true;
        issued_request = true;
        ++statistics_.misses;
        if (!seen.second && seen.first->second) {
          ++statistics_.reloads_after_eviction;
          seen.first->second = false;
        }
      }

      if (owns_request) {
        lock.unlock();
        request(key);
        owns_request = false;
        continue;
      }

      const auto wait_started = Clock::now();
      const auto status = condition_.wait_until(lock, deadline);
      statistics_.wait_microseconds +=
          std::chrono::duration_cast<std::chrono::microseconds>(
              Clock::now() - wait_started).count();
      if (status == std::cv_status::timeout) {
        if (entries_.find(key) != entries_.end()) continue;
        if (retry_after_ > now_()) continue;
        ++statistics_.timeouts;
        if (request_active_ && active_key_ == key) request_active_ = false;
        // A timeout is not a reply. In particular, this caller might have
        // spent its entire deadline queued behind a different key and never
        // issued request(key) at all. Do not turn that transient condition
        // into a cached negative result; only Publish(..., false) may do so.
        condition_.notify_all();
        return false;
      }
    }
  }

  bool Publish(const Key& key, const Value& value, bool valid) {
    std::lock_guard<std::mutex> lock(mutex_);
    try {
      StoreLocked(key, value, valid);
      ++statistics_.publications;
    } catch (const std::bad_alloc&) {
      if (request_active_ && active_key_ == key) request_active_ = false;
      condition_.notify_all();
      return false;
    }
    if (request_active_ && active_key_ == key) request_active_ = false;
    retry_after_ = {};
    retry_delay_ = std::chrono::seconds{1};
    condition_.notify_all();
    return true;
  }

  /** No matching provider reply: retry with 1, 2, 4 ... 30 second backoff.
   * Unlike Publish(..., false), this never caches a missing forecast frame.
   * An actual reply (including confirmed no data) or Clear resets the gate.
   */
  void PublishUnavailable(const Key& key) {
    std::lock_guard<std::mutex> lock(mutex_);
    // A late request must not cancel a newer worker's request.
    if (!request_active_ || active_key_ != key) return;
    ++statistics_.unavailable_replies;
    retry_after_ = now_() + retry_delay_;
    retry_delay_ = std::min(retry_delay_ * 2, std::chrono::seconds{30});
    request_active_ = false;
    condition_.notify_all();
  }

  void NotifyAll() { condition_.notify_all(); }

  std::size_t Size() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return entries_.size();
  }

  std::size_t TotalWeight() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return total_weight_;
  }

  Statistics Stats() const {
    std::lock_guard<std::mutex> lock(mutex_);
    Statistics result = statistics_;
    result.distinct_keys = seen_keys_.size();
    result.positive_entries = positive_lru_.size();
    result.negative_entries = negative_lru_.size();
    return result;
  }

  void Clear(bool reset_statistics = true) {
    std::lock_guard<std::mutex> lock(mutex_);
    entries_.clear();
    positive_lru_.clear();
    negative_lru_.clear();
    seen_keys_.clear();
    total_weight_ = 0;
    request_active_ = false;
    active_key_ = Key{};
    retry_after_ = {};
    retry_delay_ = std::chrono::seconds{1};
    if (reset_statistics) statistics_ = {};
    condition_.notify_all();
  }

  // Adjust only between routes. Eviction retains one oversized reply, as Publish does.
  void SetMaximumWeight(std::size_t maximum) {
    std::lock_guard<std::mutex> lock(mutex_);
    maximum_weight_ = maximum;
    TrimWeightLocked();
  }

private:
  struct Entry {
    Value value;
    bool valid;
    std::size_t weight;
    typename std::list<Key>::iterator position;
  };

  void TouchLocked(Entry& entry) {
    auto& lru = entry.valid ? positive_lru_ : negative_lru_;
    lru.splice(lru.end(), lru, entry.position);
  }

  void EvictLocked(std::list<Key>& lru,
                   typename std::list<Key>::iterator position) {
    const auto victim = entries_.find(*position);
    seen_keys_.find(*position)->second = true;
    total_weight_ -= victim->second.weight;
    entries_.erase(victim);
    lru.erase(position);
    ++statistics_.evictions;
  }

  void TrimWeightLocked() {
    while (total_weight_ > maximum_weight_) {
      if (positive_lru_.size() > 1) {
        EvictLocked(positive_lru_, positive_lru_.begin());
        continue;
      }
      if (positive_lru_.size() == 1 &&
          entries_.find(positive_lru_.front())->second.weight == total_weight_)
        break;
      // Missing GRIB frames have no payload. Do not discard thousands of
      // cheap negative entries just to retain one oversized positive frame.
      // Still account for clients whose negative values own storage.
      auto weighted_negative = std::find_if(
          negative_lru_.begin(), negative_lru_.end(), [&](const Key& key) {
            return entries_.find(key)->second.weight > 0;
          });
      if (weighted_negative == negative_lru_.end() || entries_.size() == 1)
        break;
      EvictLocked(negative_lru_, weighted_negative);
    }
  }

  void StoreLocked(const Key& key, const Value& value, bool valid) {
    const std::size_t weight = weight_function_ ? weight_function_(value) : 0;
    seen_keys_.insert_or_assign(key, false);
    auto& lru = valid ? positive_lru_ : negative_lru_;
    const auto existing = entries_.find(key);
    if (existing != entries_.end()) {
      const std::size_t old_weight = existing->second.weight;
      existing->second.value = value;
      auto& previous_lru = existing->second.valid ? positive_lru_ : negative_lru_;
      lru.splice(lru.end(), previous_lru, existing->second.position);
      existing->second.valid = valid;
      existing->second.weight = weight;
      total_weight_ -= old_weight;
    } else {
      lru.push_back(key);
      const auto position = std::prev(lru.end());
      try {
        entries_.emplace(key, Entry{value, valid, weight, position});
      } catch (...) {
        lru.erase(position);
        throw;
      }
    }
    if (weight > std::numeric_limits<std::size_t>::max() - total_weight_)
      total_weight_ = std::numeric_limits<std::size_t>::max();
    else
      total_weight_ += weight;
    while (positive_lru_.size() > capacity_)
      EvictLocked(positive_lru_, positive_lru_.begin());
    while (negative_lru_.size() > negative_capacity_)
      EvictLocked(negative_lru_, negative_lru_.begin());
    TrimWeightLocked();
    statistics_.peak_entries =
        std::max(statistics_.peak_entries, entries_.size());
    statistics_.peak_weight =
        std::max(statistics_.peak_weight, total_weight_);
  }

  const std::size_t capacity_;
  const std::size_t negative_capacity_;
  std::size_t maximum_weight_;
  const WeightFunction weight_function_;
  const NowFunction now_;
  mutable std::mutex mutex_;
  std::condition_variable condition_;
  std::map<Key, Entry> entries_;
  std::list<Key> positive_lru_;
  std::list<Key> negative_lru_;
  // The flag distinguishes a real eviction from a transient service retry.
  std::map<Key, bool> seen_keys_;
  std::size_t total_weight_{0};
  Statistics statistics_;
  bool request_active_{false};
  Key active_key_{};
  Clock::time_point retry_after_{};
  std::chrono::seconds retry_delay_{1};
};

}  // namespace weather_routing

#endif  // WEATHER_ROUTING_KEYED_REQUEST_CACHE_H
