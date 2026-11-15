#include <gtest/gtest.h>

#include "GribTimelineFrameCache.h"
#include <atomic>
#include <future>

TEST(GribTimelineFrameCache, WakesOnlyForRealMissesAfterRequestStateIsReady) {
  weather_routing::GribTimelineFrameCache cache;
  unsigned requests = 0, wakeups = 0;
  cache.SetRequestWakeup([&] {
    EXPECT_EQ(requests, ++wakeups);
    EXPECT_EQ(cache.Stats().misses, wakeups); // Callback is outside the cache lock.
  });
  for (bool valid : {false, true}) {
    Shared_GribRecordSet frame;
    const auto key = valid ? 200 : 100;
    for (int read = 0; read < 3; ++read)
      EXPECT_EQ(cache.Acquire(key, &frame, 100,
          [&](auto requested) { ++requests; cache.Publish(requested, {}, valid); },
          [] { return false; }), valid);
  }
  EXPECT_EQ(requests, 2U);
  EXPECT_EQ(wakeups, 2U);
  cache.SetRequestWakeup({});
  Shared_GribRecordSet frame;
  EXPECT_FALSE(cache.Acquire(300, &frame, 100,
      [&](auto key) { ++requests; cache.Publish(key, {}, false); }, [] { return false; }));
  EXPECT_EQ(wakeups, 2U);
}

TEST(GribTimelineFrameCache, ConsecutiveWorkerRequestsEachWakeTheOwner) {
  weather_routing::GribTimelineFrameCache cache;
  std::promise<void> first, second;
  auto firstWake = first.get_future(), secondWake = second.get_future();
  std::atomic<unsigned> wakeups{};
  std::atomic<std::int64_t> pending{};
  cache.SetRequestWakeup([&] {
    if (++wakeups == 1) first.set_value(); else second.set_value();
  });
  auto worker = std::async(std::launch::async, [&] {
    Shared_GribRecordSet frame;
    for (auto key : {100, 200})
      EXPECT_FALSE(cache.Acquire(key, &frame, 2000,
          [&](auto time) { pending.store(time); }, [] { return false; }));
  });
  EXPECT_EQ(firstWake.wait_for(std::chrono::seconds{1}), std::future_status::ready);
  EXPECT_EQ(pending.load(), 100);
  cache.Publish(100, {}, false);
  EXPECT_EQ(secondWake.wait_for(std::chrono::seconds{1}), std::future_status::ready);
  EXPECT_EQ(pending.load(), 200);
  cache.Publish(200, {}, false);
  worker.get();
  EXPECT_EQ(wakeups.load(), 2U);
}

TEST(GribTimelineFrameCache, PreCancelledLookupDoesNotPostARequestWakeup) {
  weather_routing::GribTimelineFrameCache cache;
  unsigned wakeups = 0;
  cache.SetRequestWakeup([&] { ++wakeups; });
  Shared_GribRecordSet frame;
  EXPECT_FALSE(cache.Acquire(100, &frame, 100,
      [](auto) { ADD_FAILURE() << "Cancelled lookup issued a request"; }, [] { return true; }));
  EXPECT_EQ(wakeups, 0U);
}

TEST(GribTimelineFrameCache, RetainsMissingMonthAcrossEngineAttempts) {
  weather_routing::GribTimelineFrameCache cache;
  cache.Configure(2048, false, 32768);
  unsigned requests = 0;
  constexpr std::int64_t kSlices = 30 * 24 * 4;
  for (int attempt = 0; attempt < 3; ++attempt) {
    for (std::int64_t slice = 0; slice < kSlices; ++slice) {
      Shared_GribRecordSet frame;
      EXPECT_FALSE(cache.Acquire(
          slice * 900, &frame, 100,
          [&](const std::int64_t& key) {
            ++requests;
            EXPECT_TRUE(cache.Publish(key, {}, false));
          }, [] { return false; }));
      EXPECT_EQ(frame.GetGribRecordSet(), nullptr);
    }
  }
  EXPECT_EQ(requests, unsigned(kSlices));
  EXPECT_EQ(cache.Stats().negative_hits, 2U * kSlices);
  EXPECT_EQ(cache.Stats().negative_entries, std::size_t(kSlices));
  EXPECT_EQ(cache.Stats().evictions, 0U);
  EXPECT_EQ(cache.TotalWeight(), 0U);
}

TEST(GribTimelineFrameCache, UnansweredServiceIsRetriedInANewBatch) {
  weather_routing::GribTimelineFrameCache cache;
  unsigned requests = 0;
  const auto unavailable = [&](const std::int64_t& key) {
    ++requests;
    cache.PublishUnavailable(key);
  };
  Shared_GribRecordSet frame;
  EXPECT_FALSE(cache.Acquire(100, &frame, 100, unavailable,
                             [] { return false; }));
  EXPECT_FALSE(cache.Acquire(200, &frame, 100, unavailable,
                             [] { return false; }));
  EXPECT_EQ(requests, 1U);
  EXPECT_EQ(cache.Stats().unavailable_skips, 1U);
  EXPECT_EQ(cache.Size(), 0U);
  cache.Clear();
  EXPECT_FALSE(cache.Acquire(100, &frame, 100, unavailable,
                             [] { return false; }));
  EXPECT_EQ(requests, 2U);
}
