// SPDX-License-Identifier: GPL-3.0-or-later
#include "CycloneAssessment.h"
#include <chrono>
#include <algorithm>
#include <cmath>
#include <list>
#include <utility>

namespace weather_routing {
CycloneAssessmentProgress CycloneAssessment::Job::Snapshot() const {
  std::lock_guard<std::mutex> lock(mutex);
  return progress;
}
void CycloneAssessment::Job::Cancel() {
  cancelled.store(true);
  std::lock_guard<std::mutex> lock(mutex);
  if (progress.status == CycloneAssessmentStatus::Running) {
    progress.status = CycloneAssessmentStatus::Cancelled;
    for (auto& route : progress.routes)
      if (route.status == CycloneAssessmentStatus::Running ||
          route.status == CycloneAssessmentStatus::NotAssessed)
        route.status = CycloneAssessmentStatus::Cancelled;
  }
}
CycloneAssessment::CycloneAssessment() : worker_([this] { Work(); }) {}
CycloneAssessment::~CycloneAssessment() {
  {
    std::lock_guard<std::mutex> lock(mutex_);
    stop_ = true;
    if (pending_) pending_->Cancel();
    if (active_) active_->Cancel();
  }
  changed_.notify_one();
  worker_.join();  // no UI/route/provider lock is held while draining a call
}
std::shared_ptr<CycloneAssessment::Job> CycloneAssessment::Start(
    std::vector<CycloneAssessmentRoute> routes, int days,
    std::uint64_t revision, bool cacheable, Query query) {
  auto job = std::make_shared<Job>();
  job->routes = std::move(routes);
  job->days = days;
  job->revision = revision;
  job->cacheable = cacheable;
  job->query = std::move(query);
  for (const auto& route : job->routes) {
    job->progress.total += route.segments.size();
    CycloneAssessmentResult result;
    result.label = route.label;
    job->progress.routes.push_back(result);
  }
  {
    std::lock_guard<std::mutex> lock(mutex_);
    if (pending_) pending_->Cancel();
    if (active_) active_->Cancel();
    pending_ = job;
  }
  changed_.notify_one();
  return job;
}
namespace {
template <class T>
void Append(std::string& key, const T& value) {
  key.append(reinterpret_cast<const char*>(&value), sizeof(value));
}
std::string Key(const CycloneAssessmentRoute& route, int days,
                std::uint64_t revision) {
  std::string key;
  Append(key, revision);
  Append(key, days);
  for (const auto& s : route.segments) {
    Append(key, s.lat1);
    Append(key, s.lon1);
    Append(key, s.lat2);
    Append(key, s.lon2);
    Append(key, s.time);
    Append(key, s.month);
  }
  return key;
}
bool Valid(const CycloneAssessmentSegment& s) {
  return std::isfinite(s.lat1) && std::isfinite(s.lon1) &&
         std::isfinite(s.lat2) && std::isfinite(s.lon2) &&
         std::abs(s.lat1) <= 90 && std::abs(s.lat2) <= 90 && s.month >= 0 &&
         s.month < 12;
}
}  // namespace
void CycloneAssessment::Work() {
  using Status = CycloneAssessmentStatus;
  struct Entry {
    std::string key;
    CycloneAssessmentResult result;
  };
  std::list<Entry> cache;
  std::size_t bytes = 0;
  for (;;) {
    std::shared_ptr<Job> job;
    {
      std::unique_lock<std::mutex> lock(mutex_);
      changed_.wait(lock, [&] { return stop_ || pending_; });
      if (stop_) return;
      active_ = job = std::move(pending_);
    }
    for (std::size_t i = 0; i < job->routes.size() && !job->cancelled.load();
         ++i) {
      const auto& route = job->routes[i];
      auto key = Key(route, job->days, job->revision);
      auto hit = cache.end();
      if (job->cacheable)
        for (auto it = cache.begin(); it != cache.end(); ++it)
          if (it->key == key) {
            hit = it;
            break;
          }
      CycloneAssessmentResult result;
      result.label = route.label;
      if (hit != cache.end()) {
        result = hit->result;
        result.label = route.label;
        cache.splice(cache.begin(), cache, hit);
        std::lock_guard<std::mutex> lock(job->mutex);
        if (job->cancelled.load()) break;
        ++job->progress.cache_hits;
        job->progress.checked += route.segments.size();
        job->progress.routes[i] = result;
        continue;
      }
      result.status = Status::Running;
      {
        std::lock_guard<std::mutex> lock(job->mutex);
        if (job->cancelled.load()) break;
        job->progress.routes[i] = result;
      }
      if (route.segments.empty() || job->days < 1 || job->days > 365 ||
          !job->query)
        result.status = Status::Unavailable;
      for (const auto& s : route.segments) {
        if (result.status != Status::Running || job->cancelled.load()) break;
        const auto start = std::chrono::steady_clock::now();
        int crossings = -1;
        if (Valid(s)) {
          {
            std::lock_guard<std::mutex> lock(job->mutex);
            if (job->cancelled.load()) break;
            ++job->progress.provider_calls;
          }
          try {
            crossings = job->query(s, job->days);
          } catch (...) {
          }
        }
        const auto us = static_cast<std::uint64_t>(
            std::chrono::duration_cast<std::chrono::microseconds>(
                std::chrono::steady_clock::now() - start)
                .count());
        std::lock_guard<std::mutex> lock(job->mutex);
        job->progress.wait_microseconds += us;
        if (us > job->progress.max_call_microseconds)
          job->progress.max_call_microseconds = us;
        if (job->cancelled.load()) break;
        ++job->progress.checked;
        if (crossings < 0)
          result.status = Status::Unavailable;
        else if (crossings > 0) {
          ++result.crossing_segments;
          ++result.months[s.month];
        }
        job->progress.routes[i] = result;
      }
      if (job->cancelled.load()) break;
      if (result.status == Status::Running) result.status = Status::Complete;
      {
        std::lock_guard<std::mutex> lock(job->mutex);
        if (job->cancelled.load()) break;
        job->progress.routes[i] = result;
      }
      // Only complete assessments are reusable. Exact binary keys avoid hash
      // collisions; bound both entry count and geometry-key memory.
      if (job->cacheable && result.status == Status::Complete &&
          key.size() <= 4 * 1024 * 1024) {
        bytes += key.size();
        cache.push_front({std::move(key), result});
        while (cache.size() > 64 || bytes > 4 * 1024 * 1024) {
          bytes -= cache.back().key.size();
          cache.pop_back();
        }
      }
    }
    // Results, not the potentially large geometry snapshot, are retained by
    // the hidden report window after a job has finished or been cancelled.
    job->routes.clear();
    job->query = {};
    {
      std::lock_guard<std::mutex> lock(job->mutex);
      if (!job->cancelled.load())
        job->progress.status =
            std::any_of(job->progress.routes.begin(),
                        job->progress.routes.end(),
                        [](const CycloneAssessmentResult& r) {
                          return r.status == Status::Unavailable;
                        })
                ? Status::Unavailable
                : Status::Complete;
    }
    {
      std::lock_guard<std::mutex> lock(mutex_);
      if (active_ == job) active_.reset();
    }
  }
}
}  // namespace weather_routing
