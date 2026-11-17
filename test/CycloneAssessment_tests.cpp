// SPDX-License-Identifier: GPL-3.0-or-later
#include <gtest/gtest.h>
#include "CycloneAssessment.h"
#include <chrono>
#include <future>
#include <limits>
using namespace weather_routing;
namespace {
CycloneAssessmentRoute Route(std::size_t count = 3) {
  CycloneAssessmentRoute r;
  r.label = "test";
  for (std::size_t i = 0; i < count; ++i)
    r.segments.push_back(
        {10., 20., 10.1, 20.1, 1700000000 + static_cast<std::int64_t>(i), 10});
  return r;
}
CycloneAssessmentProgress Finish(
    const std::shared_ptr<CycloneAssessment::Job>& job) {
  const auto end = std::chrono::steady_clock::now() + std::chrono::seconds(3);
  while (job->Snapshot().status == CycloneAssessmentStatus::Running &&
         std::chrono::steady_clock::now() < end)
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  return job->Snapshot();
}
}  // namespace
TEST(CycloneAssessment, CountsPositiveSegmentsAndPreservesZeroConclusion) {
  CycloneAssessment worker;
  int n = 0;
  auto p = Finish(worker.Start({Route()}, 30, 1, false, [&](const auto&, int) {
    return ++n == 2 ? 1 : 0;
  }));
  ASSERT_EQ(p.routes[0].status, CycloneAssessmentStatus::Complete);
  EXPECT_EQ(p.routes[0].crossing_segments, 1u);
  EXPECT_EQ(p.routes[0].months[10], 1u);
  EXPECT_EQ(p.provider_calls, 3u);
  p = Finish(worker.Start({Route()}, 30, 1, false,
                          [](const auto&, int) { return 0; }));
  EXPECT_EQ(p.routes[0].status, CycloneAssessmentStatus::Complete);
  EXPECT_EQ(p.routes[0].crossing_segments, 0u);
}
TEST(CycloneAssessment, MissingAndThrowingProvidersNeverBecomeZeroCrossings) {
  CycloneAssessment worker;
  auto p = Finish(worker.Start({Route()}, 30, 1, true,
                               [](const auto&, int) { return -1; }));
  EXPECT_EQ(p.routes[0].status, CycloneAssessmentStatus::Unavailable);
  EXPECT_EQ(p.provider_calls, 1u);
  p = Finish(worker.Start({Route()}, 30, 1, true,
                          [](const auto&, int) -> int { throw 1; }));
  EXPECT_EQ(p.routes[0].status, CycloneAssessmentStatus::Unavailable);
  EXPECT_EQ(p.cache_hits, 0u);
}
TEST(CycloneAssessment, CacheIncludesGeometryTimesWindowAndProviderRevision) {
  CycloneAssessment worker;
  auto run = [&](CycloneAssessmentRoute r, int days = 30,
                 std::uint64_t rev = 1) {
    return Finish(
        worker.Start({r}, days, rev, true, [](const auto&, int) { return 0; }));
  };
  EXPECT_EQ(run(Route()).provider_calls, 3u);
  auto renamed = Route();
  renamed.label = "another label";
  auto p = run(renamed);
  EXPECT_EQ(p.cache_hits, 1u);
  EXPECT_EQ(p.provider_calls, 0u);
  EXPECT_EQ(p.routes[0].label, "another label");
  auto changed = Route();
  changed.segments[0].time += 1;
  EXPECT_EQ(run(changed).provider_calls, 3u);
  changed = Route();
  changed.segments[0].lon1 += .01;
  EXPECT_EQ(run(changed).provider_calls, 3u);
  EXPECT_EQ(run(Route(), 31).provider_calls, 3u);
  EXPECT_EQ(run(Route(), 30, 2).provider_calls, 3u);
}
TEST(CycloneAssessment, LegacyProviderWithoutRevisionDoesNotReuseResults) {
  CycloneAssessment worker;
  for (int i = 0; i < 2; ++i) {
    auto p = Finish(worker.Start({Route()}, 30, 1, false,
                                 [](const auto&, int) { return 0; }));
    EXPECT_EQ(p.cache_hits, 0u);
    EXPECT_EQ(p.provider_calls, 3u);
  }
}
TEST(CycloneAssessment,
     CancelRemainsResponsiveDuringAnOutstandingProviderCall) {
  CycloneAssessment worker;
  std::promise<void> entered, release;
  auto wait = release.get_future().share();
  auto job = worker.Start({Route(100)}, 30, 1, true, [&](const auto&, int) {
    entered.set_value();
    wait.wait();
    return 0;
  });
  const auto ready = entered.get_future().wait_for(std::chrono::seconds(2));
  if (ready != std::future_status::ready) {
    release.set_value();
    FAIL() << "worker did not enter callback";
  }
  EXPECT_EQ(job->Snapshot().provider_calls, 1u);
  job->Cancel();
  EXPECT_EQ(job->Snapshot().status, CycloneAssessmentStatus::Cancelled);
  auto next =
      worker.Start({Route(1)}, 30, 1, true, [](const auto&, int) { return 1; });
  // Reading status and queuing a replacement never waits for the slow callback.
  EXPECT_EQ(job->Snapshot().routes[0].status,
            CycloneAssessmentStatus::Cancelled);
  release.set_value();
  auto p = Finish(next);
  EXPECT_EQ(p.routes[0].status, CycloneAssessmentStatus::Complete);
  EXPECT_EQ(p.routes[0].crossing_segments, 1u);
  EXPECT_EQ(p.cache_hits, 0u);
}
TEST(CycloneAssessment, PendingWorkIsCancelledAndLatestRequestWins) {
  CycloneAssessment worker;
  std::promise<void> entered, release;
  auto wait = release.get_future().share();
  auto first = worker.Start({Route(1)}, 30, 1, false, [&](const auto&, int) {
    entered.set_value();
    wait.wait();
    return 0;
  });
  auto ready = entered.get_future().wait_for(std::chrono::seconds(2));
  if (ready != std::future_status::ready) {
    release.set_value();
    FAIL() << "worker did not enter callback";
  }
  std::atomic<int> calls{0};
  auto discarded = worker.Start({Route()}, 30, 2, false, [&](const auto&, int) {
    ++calls;
    return 0;
  });
  auto latest = worker.Start({Route(1)}, 30, 3, false,
                             [](const auto&, int) { return 0; });
  release.set_value();
  Finish(latest);
  EXPECT_EQ(calls.load(), 0);
  EXPECT_EQ(discarded->Snapshot().status, CycloneAssessmentStatus::Cancelled);
}
TEST(CycloneAssessment, InvalidAndEmptySnapshotsAreUnavailable) {
  CycloneAssessment worker;
  auto r = Route();
  r.segments[0].lat1 = std::numeric_limits<double>::quiet_NaN();
  auto p = Finish(worker.Start({r, Route(0)}, 30, 1, true,
                               [](const auto&, int) { return 0; }));
  EXPECT_EQ(p.routes[0].status, CycloneAssessmentStatus::Unavailable);
  EXPECT_EQ(p.routes[1].status, CycloneAssessmentStatus::Unavailable);
}
TEST(CycloneAssessment, CacheIsBounded) {
  CycloneAssessment worker;
  for (int i = 0; i < 66; ++i) {
    auto r = Route(1);
    r.segments[0].time += i;
    EXPECT_EQ(Finish(worker.Start({r}, 30, 1, true,
                                  [](const auto&, int) { return 0; }))
                  .provider_calls,
              1u);
  }
  EXPECT_EQ(Finish(worker.Start({Route(1)}, 30, 1, true,
                                [](const auto&, int) { return 0; }))
                .provider_calls,
            1u);
}

TEST(CycloneAssessment, ShutdownDrainsOutstandingCallbackBeforeReturning) {
  auto worker = std::make_unique<CycloneAssessment>();
  std::promise<void> entered, release, drained;
  auto released = release.get_future().share();
  auto job = worker->Start({Route(100)}, 30, 1, false, [&](const auto&, int) {
    entered.set_value();
    released.wait();
    return 0;
  });
  const auto started = entered.get_future().wait_for(std::chrono::seconds(2));
  if (started != std::future_status::ready) {
    release.set_value();
    FAIL() << "callback did not start";
  }
  auto finished = drained.get_future();
  std::thread shutdown([owned = std::move(worker), &drained]() mutable {
    owned.reset();
    drained.set_value();
  });
  const auto deadline =
      std::chrono::steady_clock::now() + std::chrono::seconds(2);
  while (!job->cancelled.load() && std::chrono::steady_clock::now() < deadline)
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  EXPECT_TRUE(job->cancelled.load());
  EXPECT_EQ(finished.wait_for(std::chrono::milliseconds(1)),
            std::future_status::timeout);
  release.set_value();
  shutdown.join();
  EXPECT_EQ(finished.wait_for(std::chrono::seconds(0)),
            std::future_status::ready);
  EXPECT_EQ(job->Snapshot().provider_calls, 1u);
  EXPECT_EQ(job->Snapshot().status, CycloneAssessmentStatus::Cancelled);
}
