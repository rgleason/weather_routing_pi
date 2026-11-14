#include <gtest/gtest.h>

#include "GribTimelineFrameCache.h"

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
