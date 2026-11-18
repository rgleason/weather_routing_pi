// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <algorithm>
#include <array>
#include <atomic>
#include <condition_variable>
#include <exception>
#include <future>
#include <mutex>
#include <thread>
#include <vector>

#include "RoutingWorkerBudget.h"
#include "engine/native/RoutingEngineSequence.h"

namespace weather_routing::native {

using MethodResult = supercpn::weather_routing::RoutingResult;
using MethodClock = std::chrono::steady_clock;
using MethodStop = std::shared_ptr<std::atomic_bool>;
using SolveMethod = std::function<MethodResult(
    SequenceEngine, const MethodStop&, const std::function<void()>&)>;
using AdmitMethod = std::function<std::optional<RoutingWorkerBudget::Slot>()>;

inline constexpr std::array kMethodOrder{
    SequenceEngine::Original, SequenceEngine::Quick2, SequenceEngine::Atlas,
    SequenceEngine::Quick, SequenceEngine::Alternative, SequenceEngine::Main};

// The route owns one worker slot. Extra lanes are admitted from the same
// budget and process pending methods in order. Results are harvested by the
// ordinary sequence so All retains its stable comparison and Quick3 stage.
class ParallelAllMethods {
public:
  ParallelAllMethods(unsigned enabled, unsigned limit, SolveMethod solve,
                     const AdmitMethod& admit, std::function<void()> wake,
                     supercpn::weather_routing::CancellationToken cancel)
      : solve_(std::move(solve)), wake_(std::move(wake)), cancel_(std::move(cancel)) {
    std::size_t count = 0;
    for (std::size_t i = 0; i < kMethodOrder.size(); ++i) {
      if (!(enabled & EngineBit(SequenceRoutingEngine(kMethodOrder[i])))) continue;
      futures_[i] = promises_[i].get_future();
      if (count++) pending_.push_back(i);
    }
    threads_.reserve(std::min<std::size_t>(limit, pending_.size()));
    for (unsigned lane = 1; lane < limit && lane < count; ++lane) {
      try {
        auto slot = admit();
        if (!slot) break;
        threads_.emplace_back([this, slot = std::move(*slot)]() mutable {
          for (const auto index : pending_) {
            if (cancel_.cancelled() || stopped_->load(std::memory_order_relaxed)) break;
            if (!claimed_[index].exchange(true)) calculate(index);
          }
          // Release admission as soon as this lane exits.
          auto release = std::move(slot);
        });
      } catch (const std::system_error&) {
        break;
      } catch (const std::bad_alloc&) {
        break;
      }
    }
  }

  ~ParallelAllMethods() {
    stopped_->store(true, std::memory_order_relaxed);
    if (!threads_.empty()) wake_();
    for (auto& thread : threads_) thread.join();
  }

  std::size_t LaneCount() const { return 1 + threads_.size(); }

  MethodResult Take(SequenceEngine engine) {
    const auto index = indexOf(engine);
    if (!claimed_[index].exchange(true)) calculate(index);
    return futures_[index].get();
  }
  double Seconds(SequenceEngine engine) const { return seconds_[indexOf(engine)]; }

private:
  static std::size_t indexOf(SequenceEngine engine) {
    return static_cast<std::size_t>(
        std::find(kMethodOrder.begin(), kMethodOrder.end(), engine) - kMethodOrder.begin());
  }

  void calculate(std::size_t index) {
    try {
      const auto started = MethodClock::now();
      auto result = solve_(kMethodOrder[index], stopped_, {});
      seconds_[index] = std::chrono::duration<double>(MethodClock::now() - started).count();
      promises_[index].set_value(std::move(result));
    } catch (...) {
      promises_[index].set_exception(std::current_exception());
    }
  }

  SolveMethod solve_;
  std::function<void()> wake_;
  supercpn::weather_routing::CancellationToken cancel_;
  MethodStop stopped_{std::make_shared<std::atomic_bool>(false)};
  std::array<std::atomic_bool, kMethodOrder.size()> claimed_{};
  std::array<double, kMethodOrder.size()> seconds_{};
  std::array<std::promise<MethodResult>, kMethodOrder.size()> promises_;
  std::array<std::future<MethodResult>, kMethodOrder.size()> futures_;
  std::vector<std::size_t> pending_;
  std::vector<std::thread> threads_;
};

struct AutoFastResults {
  std::optional<MethodResult> quick;
  std::optional<MethodResult> quick2;
  std::array<double, 2> seconds{};
  bool competed() const { return quick2.has_value(); }
};

inline AutoFastResults RunStaggeredFastMethods(
    const SolveMethod& solve, RoutingWorkerBudget::Slot extraSlot,
    const supercpn::weather_routing::CancellationToken& cancel,
    const std::atomic_bool& keepResult, const std::function<void()>& wake,
    const std::function<void(const MethodResult&)>& publish,
    std::chrono::milliseconds hedge = std::chrono::milliseconds(750),
    std::chrono::milliseconds grace = std::chrono::seconds(10)) {
  struct Job {
    MethodStop stop{std::make_shared<std::atomic_bool>(false)};
    MethodResult result;
    std::exception_ptr failure;
    MethodClock::time_point returned{};
    bool delivered{};
    double seconds{};
  };
  std::array<Job, 2> jobs;
  std::mutex mutex;
  std::condition_variable ready;
  bool repair = false;
  const auto quickStarted = MethodClock::now();
  const auto requestHedge = [&] {
    std::lock_guard<std::mutex> lock(mutex);
    repair = true;
    ready.notify_one();
  };
  const auto calculate = [&](std::size_t index) {
    const auto started = MethodClock::now();
    MethodResult result;
    std::exception_ptr failure;
    try {
      result = solve(kMethodOrder[index], jobs[index].stop, index == 0
          ? std::function<void()>(requestHedge) : std::function<void()>{});
    } catch (...) {
      failure = std::current_exception();
    }
    std::lock_guard<std::mutex> lock(mutex);
    jobs[index].result = std::move(result);
    jobs[index].failure = failure;
    jobs[index].returned = MethodClock::now();
    jobs[index].seconds = std::chrono::duration<double>(jobs[index].returned - started).count();
    ready.notify_one();
  };

  std::thread quick, quick2;
  struct Join {
    std::thread& first;
    std::thread& second;
    ~Join() {
      if (first.joinable()) first.join();
      if (second.joinable()) second.join();
    }
  } join{quick, quick2};
  try {
    quick = std::thread([&] { calculate(0); });
  } catch (const std::system_error&) {
    return {};
  }

  const auto stopActive = [&] {
    for (auto& job : jobs) {
      if (job.returned == MethodClock::time_point{} && !job.stop->exchange(true))
        wake();
    }
  };
  MethodClock::time_point until = MethodClock::time_point::max();
  const MethodResult* incumbent = nullptr;
  bool quick2Unavailable = false;
  std::unique_lock<std::mutex> lock(mutex);
  try {
    for (;;) {
      const auto now = MethodClock::now();
      const auto finished = [&](std::size_t i) {
        return jobs[i].returned != MethodClock::time_point{};
      };
      if (cancel.cancelled()) stopActive();
      if (incumbent && keepResult.load(std::memory_order_relaxed))
        until = std::min(until, now);
      for (std::size_t i : {jobs[1].returned < jobs[0].returned ? 1u : 0u,
                            jobs[1].returned < jobs[0].returned ? 0u : 1u}) {
        auto& job = jobs[i];
        if (!finished(i) || job.delivered) continue;
        job.delivered = true;
        if (!AcceptedRoute(job.result) || cancel.cancelled()) continue;
        if (job.returned > until) {
          CancelRoutingResultKeepingDiagnostics(job.result);
          continue;
        }
        if (!incumbent) until = job.returned + grace;
        if (!incumbent || job.result.legs.back().endTime < incumbent->legs.back().endTime) {
          incumbent = &job.result;
          lock.unlock();
          publish(*incumbent);
          lock.lock();
        }
      }

      if (finished(0) && (!quick2.joinable() || finished(1))) break;
      if (incumbent && (keepResult.load(std::memory_order_relaxed) || now >= until))
        stopActive();
      if (!quick2.joinable() && !quick2Unavailable && !finished(0) && !incumbent &&
          !cancel.cancelled() && (repair || now >= quickStarted + hedge)) {
        try {
          quick2 = std::thread([&] { calculate(1); });
        } catch (const std::system_error&) {
          quick2Unavailable = true;
        }
        continue;
      }
      // Only external cancel and the UI's Keep Results are sampled. Method
      // completion and the repair trigger wake the coordinator directly.
      auto next = now + std::chrono::milliseconds(25);
      if (!quick2.joinable() && !quick2Unavailable && now < quickStarted + hedge)
        next = std::min(next, quickStarted + hedge);
      if (incumbent && now < until) next = std::min(next, until);
      ready.wait_until(lock, next);
    }
  } catch (...) {
    if (!lock.owns_lock()) lock.lock();
    stopActive();
    lock.unlock();
    throw;
  }

  AutoFastResults result;
  for (std::size_t i = 0; i < jobs.size(); ++i) {
    auto& job = jobs[i];
    if (job.failure) std::rethrow_exception(job.failure);
    if (job.returned != MethodClock::time_point{}) {
      result.seconds[i] = job.seconds;
      if (i == 0) result.quick = std::move(job.result);
      else result.quick2 = std::move(job.result);
    }
  }
  return result;
}

}  // namespace weather_routing::native
