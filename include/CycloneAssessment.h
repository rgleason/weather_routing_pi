// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <array>
#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace weather_routing {
struct CycloneAssessmentSegment {
  double lat1{}, lon1{}, lat2{}, lon2{};
  std::int64_t time{};
  int month{};
};
struct CycloneAssessmentRoute {
  std::string label;
  std::vector<CycloneAssessmentSegment> segments;
};
enum class CycloneAssessmentStatus {
  NotAssessed,
  Running,
  Complete,
  Unavailable,
  Cancelled
};
struct CycloneAssessmentResult {
  std::string label;
  CycloneAssessmentStatus status{CycloneAssessmentStatus::NotAssessed};
  std::size_t crossing_segments{};
  std::array<std::size_t, 12> months{};
};
struct CycloneAssessmentProgress {
  CycloneAssessmentStatus status{CycloneAssessmentStatus::Running};
  std::size_t checked{}, total{}, provider_calls{}, cache_hits{};
  std::uint64_t wait_microseconds{}, max_call_microseconds{};
  std::vector<CycloneAssessmentResult> routes;
};
// One bounded queue (latest request wins). Worker code owns values and never
// calls windows or dereferences route objects. Poll results from the UI timer.
class CycloneAssessment {
public:
  using Query = std::function<int(const CycloneAssessmentSegment&, int)>;
  struct Job {
    std::atomic<bool> cancelled{false};
    mutable std::mutex mutex;
    CycloneAssessmentProgress progress;
    CycloneAssessmentProgress Snapshot() const;
    void Cancel();

  private:
    friend class CycloneAssessment;
    std::vector<CycloneAssessmentRoute> routes;
    Query query;
    std::uint64_t revision{};
    int days{};
    bool cacheable{};
  };
  CycloneAssessment();
  ~CycloneAssessment();
  std::shared_ptr<Job> Start(std::vector<CycloneAssessmentRoute>, int days,
                             std::uint64_t revision, bool cacheable, Query);

private:
  void Work();
  std::mutex mutex_;
  std::condition_variable changed_;
  bool stop_{};
  std::shared_ptr<Job> pending_, active_;
  std::thread worker_;
};
}  // namespace weather_routing
