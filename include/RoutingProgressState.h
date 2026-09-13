#ifndef WEATHER_ROUTING_PROGRESS_STATE_H
#define WEATHER_ROUTING_PROGRESS_STATE_H

#include <chrono>
#include <cstdint>
#include <mutex>

namespace weather_routing {

// Per-route mailbox: readers retain the last update; generation tokens reject
// late callbacks from an old calculation after cancellation or restart.
template <typename Value> class RoutingProgressState {
 public:
  using Clock = std::chrono::steady_clock;
  struct Snapshot {
    Value value{};
    std::uint64_t generation{};
    std::uint64_t sequence{};
    Clock::time_point started{};
    Clock::time_point updated{};
  };
  std::uint64_t Begin() {
    std::lock_guard<std::mutex> guard(mutex_);
    const auto generation = snapshot_.generation + 1;
    snapshot_ = {};
    snapshot_.generation = generation;
    snapshot_.started = Clock::now();
    return generation;
  }
  void Publish(std::uint64_t generation, const Value& value) {
    std::lock_guard<std::mutex> guard(mutex_);
    if (generation != snapshot_.generation) return;
    snapshot_.value = value;
    snapshot_.updated = Clock::now();
    ++snapshot_.sequence;
  }
  Snapshot Read() const {
    std::lock_guard<std::mutex> guard(mutex_);
    return snapshot_;
  }
 private:
  mutable std::mutex mutex_;
  Snapshot snapshot_;
};
}  // namespace weather_routing
#endif
