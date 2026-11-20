// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <atomic>
#include <condition_variable>
#include <exception>
#include <functional>
#include <mutex>
#include <thread>
#include <vector>
#include "RoutingWorkerBudget.h"
#include "supercpn/weather_routing/Providers.h"

namespace weather_routing::native {
// One persistent, bounded pool per active method. Each lane owns its mutable
// provider caches; only immutable snapshots and explicitly synchronized caches
// are shared. Results belong to indexed slots and are merged by the solver.
class RoutingStepExecutor {
 public:
  using Environment = supercpn::weather_routing::RoutingEnvironment;
  using Task = std::function<void(std::size_t, const Environment&)>;
  using Factory = std::function<Environment()>;
  using Admit = std::function<std::optional<RoutingWorkerBudget::Slot>()>;
  RoutingStepExecutor(Factory factory, Admit admit)
      : factory_(std::move(factory)), admit_(std::move(admit)) {}
  ~RoutingStepExecutor() {
    { std::lock_guard<std::mutex> lock(mutex_); stopping_ = true; }
    ready_.notify_all();
    for (auto& thread : threads_) thread.join();
  }
  void Run(std::size_t count, const Task& task) {
    if (!count) return;
    if (environments_.empty()) {
      environments_.push_back(factory_());
      // Leave room for the companion Auto method and UI services. Admission
      // comes from the same budget as methods and departure candidates.
      for (unsigned lane = 1; lane < 3; ++lane) {
        auto slot = admit_();
        if (!slot) break;
        auto environment = factory_();
        try {
          threads_.emplace_back([this, environment = std::move(environment),
                                 slot = std::move(*slot)]() mutable {
            std::uint64_t seen = 0;
            for (;;) {
              std::unique_lock<std::mutex> lock(mutex_);
              ready_.wait(lock, [&] { return stopping_ || generation_ != seen; });
              if (stopping_) return;
              seen = generation_;
              lock.unlock();
              Work(environment);
              lock.lock();
              if (--remaining_ == 0) complete_.notify_one();
            }
          });
        } catch (const std::system_error&) { break; }
      }
    }
    {
      std::lock_guard<std::mutex> lock(mutex_);
      task_ = task; count_ = count; next_ = 0; failure_ = {};
      remaining_ = threads_.size(); ++generation_;
    }
    ready_.notify_all();
    Work(environments_.front());
    std::unique_lock<std::mutex> lock(mutex_);
    complete_.wait(lock, [&] { return remaining_ == 0; });
    task_ = {};
    if (failure_) std::rethrow_exception(failure_);
  }
 private:
  void Work(const Environment& environment) {
    try {
      for (;;) {
        auto index = next_.fetch_add(1, std::memory_order_relaxed);
        if (index >= count_) return;
        task_(index, environment);
      }
    } catch (...) {
      next_.store(count_, std::memory_order_relaxed);
      std::lock_guard<std::mutex> lock(mutex_);
      if (!failure_) failure_ = std::current_exception();
    }
  }
  Factory factory_;
  Admit admit_;
  std::vector<Environment> environments_;
  std::vector<std::thread> threads_;
  std::mutex mutex_;
  std::condition_variable ready_, complete_;
  Task task_;
  std::atomic<std::size_t> next_{};
  std::size_t count_{}, remaining_{};
  std::uint64_t generation_{};
  bool stopping_{};
  std::exception_ptr failure_;
};
}  // namespace weather_routing::native
