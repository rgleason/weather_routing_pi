// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <algorithm>
#include <memory>
#include <mutex>
#include <optional>
#include <utility>

namespace weather_routing {

// Ordinary departure workers and extra method workers acquire from one pool.
class RoutingWorkerBudget {
  struct State {
    std::mutex mutex;
    unsigned limit{1};
    unsigned occupied{};
  };

public:
  class Slot {
  public:
    Slot(Slot&& other) noexcept : state_(std::move(other.state_)) {}
    Slot& operator=(Slot&& other) noexcept {
      if (this != &other) {
        release();
        state_ = std::move(other.state_);
      }
      return *this;
    }
    Slot(const Slot&) = delete;
    Slot& operator=(const Slot&) = delete;
    ~Slot() { release(); }

  private:
    friend class RoutingWorkerBudget;
    explicit Slot(std::shared_ptr<State> state) : state_(std::move(state)) {}
    void release() {
      if (!state_) return;
      auto state = std::move(state_);
      std::lock_guard<std::mutex> lock(state->mutex);
      --state->occupied;
    }

    std::shared_ptr<State> state_;
  };

  void SetLimit(unsigned limit) {
    std::lock_guard<std::mutex> lock(state_->mutex);
    state_->limit = std::max(1u, limit);
  }

  unsigned InUse() const {
    std::lock_guard<std::mutex> lock(state_->mutex);
    return state_->occupied;
  }

  std::optional<Slot> TryAcquire() {
    std::lock_guard<std::mutex> lock(state_->mutex);
    if (state_->occupied >= state_->limit) return std::nullopt;
    ++state_->occupied;
    return Slot(state_);
  }

private:
  std::shared_ptr<State> state_{std::make_shared<State>()};
};

}  // namespace weather_routing
