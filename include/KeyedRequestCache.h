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
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <iterator>
#include <list>
#include <limits>
#include <map>
#include <memory>
#include <mutex>
#include <new>
#include <optional>
#include <utility>

namespace weather_routing {

/**
 * Coalesce synchronous host-service requests and publish replies by key.
 *
 * Concurrent callers for the same key share its result. Different keys can
 * wait for service independently. A request ID distinguishes a late reply
 * from a new request for the same key.
 */
template <typename Key, typename Value>
class KeyedRequestCache {
public:
  using WeightFunction = std::function<std::size_t(const Value&)>;
  using Clock = std::chrono::steady_clock;
  using NowFunction = std::function<Clock::time_point()>;

  struct Request {
    Key key;
    std::uint64_t id;
    bool prefetch{};
  };

  struct Statistics {
    std::uint64_t lookups{};
    std::uint64_t hits{};
    std::uint64_t negative_hits{};
    std::uint64_t misses{};
    std::uint64_t joined_requests{};
    std::uint64_t evictions{};
    std::uint64_t reloads_after_eviction{};
    std::uint64_t publications{};
    std::uint64_t unavailable_replies{};
    std::uint64_t unavailable_skips{};
    std::uint64_t timeouts{};
    std::uint64_t wait_microseconds{};
    std::size_t distinct_keys{};
    std::size_t peak_pending_requests{};
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
    if (!request) return false;
    return AcquireRequest(key, value, timeout_milliseconds,
        [&](const Request& pending) { request(pending.key); }, cancelled);
  }

  bool AcquireRequest(const Key& key, Value* value, long timeout_milliseconds,
                      const std::function<void(const Request&)>& request,
                      const std::function<bool()>& cancelled,
                      bool* cache_hit = nullptr,
                      std::atomic<unsigned>* waiters = nullptr) {
    if (!value || !request || timeout_milliseconds < 0) return false;
    const auto deadline = Clock::now() +
                          std::chrono::milliseconds(timeout_milliseconds);
    std::unique_lock<std::mutex> lock(mutex_);
    ++statistics_.lookups;
    if (cache_hit) *cache_hit = false;
    const auto found = entries_.find(key);
    if (found != entries_.end()) {
      if (cache_hit) *cache_hit = true;
      ++statistics_.hits;
      if (!found->second.valid) ++statistics_.negative_hits;
      *value = found->second.value;
      TouchLocked(found->second);
      return found->second.valid;
    }
    if (cancelled && cancelled()) return false;
    if (retry_after_ > now_()) {
      ++statistics_.unavailable_skips;
      *value = Value{};
      return false;
    }

    const auto existing = pending_.find(key);
    const bool issue = existing == pending_.end();
    std::shared_ptr<Pending> pending;
    if (issue) {
      const auto seen = seen_keys_.emplace(key, false);
      pending = std::make_shared<Pending>(Request{key, ++next_id_});
      pending_.emplace(key, pending);
      ++statistics_.misses;
      statistics_.peak_pending_requests =
          std::max(statistics_.peak_pending_requests, pending_.size());
      if (!seen.second && seen.first->second) {
        ++statistics_.reloads_after_eviction;
        seen.first->second = false;
      }
    } else {
      pending = existing->second;
      ++pending->waiters;
      ++statistics_.joined_requests;
    }

    struct Waiting {
      std::atomic<unsigned>* count;
      explicit Waiting(std::atomic<unsigned>* count) : count(count) {
        if (count) count->fetch_add(1, std::memory_order_relaxed);
      }
      ~Waiting() {
        if (count) count->fetch_sub(1, std::memory_order_relaxed);
      }
    } waiting(waiters);

    if (issue) {
      lock.unlock();
      try {
        request(pending->request);
      } catch (...) {
        lock.lock();
        ResolveLocked(pending->request, {}, false, false);
        throw;
      }
      lock.lock();
    }

    const auto wait_started = Clock::now();
    const bool replied = condition_.wait_until(lock, deadline, [&] {
      return pending->resolved || retry_after_ > now_() ||
             (cancelled && cancelled());
    });
    statistics_.wait_microseconds +=
        std::chrono::duration_cast<std::chrono::microseconds>(
            Clock::now() - wait_started).count();

    if (--pending->waiters == 0) {
      const auto active = FindRequestLocked(pending->request);
      if (active != pending_.end()) pending_.erase(active);
    }

    if (cancelled && cancelled()) return false;
    if (!replied) ++statistics_.timeouts;
    if (!pending->has_reply && !issue && retry_after_ > now_())
      ++statistics_.unavailable_skips;
    if (pending->has_reply && !issue) {
      ++statistics_.hits;
      if (!pending->valid) ++statistics_.negative_hits;
    }
    *value = pending->value;
    return pending->valid;
  }

  // A speculative request owns no waiter. A real lookup can join it by key.
  std::optional<Request> Prefetch(const Key& key) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (retry_after_ > now_() || entries_.count(key) || pending_.count(key))
      return std::nullopt;
    const Request request{key, ++next_id_, true};
    auto pending = std::make_shared<Pending>(request);
    pending->waiters = 0;
    pending_.emplace(key, std::move(pending));
    return request;
  }

  bool HasWaiters(const Request& request) const {
    std::lock_guard<std::mutex> lock(mutex_);
    const auto found = pending_.find(request.key);
    return found != pending_.end() && found->second->request.id == request.id &&
           found->second->waiters != 0;
  }

  bool DiscardUnusedPrefetch(const Request& request) {
    if (!request.prefetch) return false;
    std::lock_guard<std::mutex> lock(mutex_);
    const auto found = FindRequestLocked(request);
    if (found == pending_.end()) return true;
    if (found->second->waiters) return false;
    pending_.erase(found);
    return true;
  }

  bool IsPending(const Request& request) const {
    std::lock_guard<std::mutex> lock(mutex_);
    const auto found = pending_.find(request.key);
    return found != pending_.end() && found->second->request.id == request.id;
  }

  bool Publish(const Request& request, const Value& value, bool valid,
               const Value& on_allocation_failure = {}) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (FindRequestLocked(request) == pending_.end()) return true;
    return PublishLocked(request.key, value, valid, on_allocation_failure);
  }

  // Also used for storing a frame without an outstanding request.
  bool Publish(const Key& key, const Value& value, bool valid) {
    std::lock_guard<std::mutex> lock(mutex_);
    return PublishLocked(key, value, valid);
  }

  /** No matching provider reply: retry with 1, 2, 4 ... 30 second backoff. */
  void PublishUnavailable(const Request& request, const Value& value) {
    std::lock_guard<std::mutex> lock(mutex_);
    PublishUnavailableLocked(request, value);
  }

  void PublishUnavailable(const Key& key) {
    std::lock_guard<std::mutex> lock(mutex_);
    const auto found = pending_.find(key);
    if (found != pending_.end()) PublishUnavailableLocked(found->second->request, {});
  }

  void Abandon(const Request& request, const Value& value) {
    std::lock_guard<std::mutex> lock(mutex_);
    ResolveLocked(request, value, false, false);
  }

  void NotifyAll() {
    // Serialize an external cancellation notification with wait registration.
    std::lock_guard<std::mutex> lock(mutex_);
    condition_.notify_all();
  }

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
    for (auto& item : pending_) item.second->resolved = true;
    pending_.clear();
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
  struct Pending {
    explicit Pending(Request requested) : request(std::move(requested)) {}
    Request request;
    std::size_t waiters{1};
    bool resolved{};
    bool has_reply{};
    bool valid{};
    Value value{};
  };

  using PendingMap = std::map<Key, std::shared_ptr<Pending>>;

  typename PendingMap::iterator FindRequestLocked(const Request& request) {
    const auto found = pending_.find(request.key);
    return found != pending_.end() && found->second->request.id == request.id
        ? found : pending_.end();
  }

  void ResolveLocked(const Request& request, const Value& value,
                     bool valid, bool has_reply) {
    const auto found = FindRequestLocked(request);
    if (found == pending_.end()) return;
    auto& pending = *found->second;
    pending.value = value;
    pending.valid = valid;
    pending.has_reply = has_reply;
    pending.resolved = true;
    pending_.erase(found);
    condition_.notify_all();
  }

  bool PublishLocked(const Key& key, const Value& value, bool valid,
                     const Value& on_allocation_failure = {}) {
    const auto active = pending_.find(key);
    try {
      StoreLocked(key, value, valid);
      ++statistics_.publications;
    } catch (const std::bad_alloc&) {
      if (active != pending_.end())
        ResolveLocked(active->second->request, on_allocation_failure, false, false);
      return false;
    }
    if (active != pending_.end())
      ResolveLocked(active->second->request, value, valid, true);
    retry_after_ = {};
    retry_delay_ = std::chrono::seconds{1};
    return true;
  }

  void PublishUnavailableLocked(const Request& request, const Value& value) {
    if (FindRequestLocked(request) == pending_.end()) return;
    ++statistics_.unavailable_replies;
    retry_after_ = now_() + retry_delay_;
    retry_delay_ = std::min(retry_delay_ * 2, std::chrono::seconds{30});
    for (auto& item : pending_) {
      item.second->value = value;
      item.second->resolved = true;
    }
    pending_.clear();
    condition_.notify_all();
  }

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
  PendingMap pending_;
  std::uint64_t next_id_{};
  Clock::time_point retry_after_{};
  std::chrono::seconds retry_delay_{1};
};

}  // namespace weather_routing

#endif  // WEATHER_ROUTING_KEYED_REQUEST_CACHE_H
