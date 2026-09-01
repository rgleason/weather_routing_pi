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
#include <deque>
#include <functional>
#include <map>
#include <mutex>

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
  explicit KeyedRequestCache(std::size_t capacity) : capacity_(capacity) {}

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
        *value = found->second.value;
        TouchLocked(key);
        return found->second.valid;
      }
      if (cancelled && cancelled()) return false;

      if (!request_active_) {
        request_active_ = true;
        active_key_ = key;
        owns_request = true;
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

  void Publish(const Key& key, const Value& value, bool valid) {
    std::lock_guard<std::mutex> lock(mutex_);
    StoreLocked(key, value, valid);
    if (request_active_ && active_key_ == key) request_active_ = false;
    condition_.notify_all();
  }

  void NotifyAll() { condition_.notify_all(); }

  std::size_t Size() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return entries_.size();
  }

private:
  struct Entry {
    Value value;
    bool valid;
  };

  void TouchLocked(const Key& key) {
    lru_.erase(std::remove(lru_.begin(), lru_.end(), key), lru_.end());
    lru_.push_back(key);
  }

  void StoreLocked(const Key& key, const Value& value, bool valid) {
    entries_[key] = Entry{value, valid};
    TouchLocked(key);
    while (entries_.size() > capacity_ && !lru_.empty()) {
      entries_.erase(lru_.front());
      lru_.pop_front();
    }
  }

  const std::size_t capacity_;
  mutable std::mutex mutex_;
  std::condition_variable condition_;
  std::map<Key, Entry> entries_;
  std::deque<Key> lru_;
  bool request_active_{false};
  Key active_key_{};
};

}  // namespace weather_routing

#endif  // WEATHER_ROUTING_KEYED_REQUEST_CACHE_H
