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
#include <deque>
#include <functional>
#include <limits>
#include <map>
#include <mutex>
#include <new>
#include <set>
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

  struct Statistics {
    std::uint64_t hits{};
    std::uint64_t misses{};
    std::uint64_t evictions{};
    std::uint64_t reloads_after_eviction{};
    std::uint64_t publications{};
    std::size_t distinct_keys{};
    std::size_t peak_entries{};
    std::size_t peak_weight{};
  };

  explicit KeyedRequestCache(std::size_t capacity)
      : capacity_(capacity),
        maximum_weight_(std::numeric_limits<std::size_t>::max()) {}

  KeyedRequestCache(std::size_t capacity, std::size_t maximum_weight,
                    WeightFunction weight_function)
      : capacity_(capacity),
        maximum_weight_(maximum_weight),
        weight_function_(std::move(weight_function)) {}

  KeyedRequestCache(const KeyedRequestCache&) = delete;
  KeyedRequestCache& operator=(const KeyedRequestCache&) = delete;

  bool Acquire(const Key& key, Value* value, long timeout_milliseconds,
               const std::function<void(const Key&)>& request,
               const std::function<bool()>& cancelled) {
    if (!value || !request || timeout_milliseconds < 0) return false;
    const auto deadline = std::chrono::steady_clock::now() +
                          std::chrono::milliseconds(timeout_milliseconds);
    bool owns_request = false;

    for (;;) {
      std::unique_lock<std::mutex> lock(mutex_);
      const auto found = entries_.find(key);
      if (found != entries_.end()) {
        ++statistics_.hits;
        *value = found->second.value;
        TouchLocked(key);
        return found->second.valid;
      }
      if (cancelled && cancelled()) return false;

      if (!request_active_) {
        request_active_ = true;
        active_key_ = key;
        owns_request = true;
        ++statistics_.misses;
        if (seen_keys_.find(key) != seen_keys_.end())
          ++statistics_.reloads_after_eviction;
      }

      if (owns_request) {
        lock.unlock();
        request(key);
        owns_request = false;
        continue;
      }

      if (condition_.wait_until(lock, deadline) == std::cv_status::timeout) {
        if (entries_.find(key) != entries_.end()) continue;
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
    condition_.notify_all();
    return true;
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
    return result;
  }

  void Clear(bool reset_statistics = true) {
    std::lock_guard<std::mutex> lock(mutex_);
    entries_.clear();
    lru_.clear();
    seen_keys_.clear();
    total_weight_ = 0;
    request_active_ = false;
    active_key_ = Key{};
    if (reset_statistics) statistics_ = {};
    condition_.notify_all();
  }

  // Adjust only between routes. Eviction retains one oversized reply, as Publish does.
  void SetMaximumWeight(std::size_t maximum) {
    std::lock_guard<std::mutex> lock(mutex_);
    maximum_weight_ = maximum;
    while (entries_.size() > 1 && total_weight_ > maximum_weight_ && !lru_.empty()) {
      const auto victim = entries_.find(lru_.front());
      if (victim != entries_.end()) {
        total_weight_ -= victim->second.weight;
        entries_.erase(victim);
        ++statistics_.evictions;
      }
      lru_.pop_front();
    }
  }

private:
  struct Entry {
    Value value;
    bool valid;
    std::size_t weight;
  };

  void TouchLocked(const Key& key) {
    lru_.erase(std::remove(lru_.begin(), lru_.end(), key), lru_.end());
    lru_.push_back(key);
  }

  void StoreLocked(const Key& key, const Value& value, bool valid) {
    const std::size_t weight = weight_function_ ? weight_function_(value) : 0;
    const auto existing = entries_.find(key);
    if (existing != entries_.end()) {
      const std::size_t old_weight = existing->second.weight;
      existing->second = Entry{value, valid, weight};
      total_weight_ -= old_weight;
    } else {
      entries_.emplace(key, Entry{value, valid, weight});
    }
    if (weight > std::numeric_limits<std::size_t>::max() - total_weight_)
      total_weight_ = std::numeric_limits<std::size_t>::max();
    else
      total_weight_ += weight;
    TouchLocked(key);
    seen_keys_.insert(key);
    while ((entries_.size() > capacity_ || total_weight_ > maximum_weight_) &&
           !lru_.empty()) {
      // Retain a single valid result even when it is larger than the byte
      // budget. Otherwise the publishing request would immediately evict its
      // own reply and every waiter would request the same frame again.
      if (entries_.size() == 1 && total_weight_ > maximum_weight_ &&
          entries_.size() <= capacity_)
        break;
      const auto victim = entries_.find(lru_.front());
      if (victim != entries_.end()) {
        total_weight_ -= victim->second.weight;
        entries_.erase(victim);
        ++statistics_.evictions;
      }
      lru_.pop_front();
    }
    statistics_.peak_entries =
        std::max(statistics_.peak_entries, entries_.size());
    statistics_.peak_weight =
        std::max(statistics_.peak_weight, total_weight_);
  }

  const std::size_t capacity_;
  std::size_t maximum_weight_;
  const WeightFunction weight_function_;
  mutable std::mutex mutex_;
  std::condition_variable condition_;
  std::map<Key, Entry> entries_;
  std::deque<Key> lru_;
  std::set<Key> seen_keys_;
  std::size_t total_weight_{0};
  Statistics statistics_;
  bool request_active_{false};
  Key active_key_{};
};

}  // namespace weather_routing

#endif  // WEATHER_ROUTING_KEYED_REQUEST_CACHE_H
