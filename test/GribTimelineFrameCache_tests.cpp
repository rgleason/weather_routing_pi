#include <gtest/gtest.h>

#include <atomic>
#include <future>
#include <thread>

#include "GribTimelineFrameCache.h"

using namespace std::chrono_literals;

namespace {
using Cache = weather_routing::GribTimelineFrameCache;
using Status = Cache::Status;

Cache::Reply Frame(unsigned id) {
  return {Shared_GribRecordSet(std::make_shared<WR_GribRecordSet>(id)),
          Status::Available};
}

bool WaitForLookups(Cache& cache, std::uint64_t count) {
  const auto deadline = std::chrono::steady_clock::now() + 1s;
  while (cache.Stats().lookups < count && std::chrono::steady_clock::now() < deadline)
    std::this_thread::sleep_for(1ms);
  return cache.Stats().lookups >= count;
}
}  // namespace

TEST(GribTimelineFrameCache, SmallFramesCanUseTheAdmittedBudgetBeyond512Keys) {
  Cache cache;
  cache.Configure(64, true, 32768);
  cache.SetRequestWakeup([&] {
    const auto request = cache.TakePendingRequest();
    ASSERT_TRUE(request);
    EXPECT_TRUE(cache.Publish(*request, Frame(1)));
  });
  for (int pass = 0; pass < 2; ++pass)
    for (std::int64_t key = 0; key < 2048; ++key)
      EXPECT_EQ(cache.Acquire(key, 10, {}).status, Status::Available);
  EXPECT_EQ(cache.Stats().misses, 2048U);
  EXPECT_EQ(cache.Stats().hits, 2048U);
  EXPECT_EQ(cache.Stats().positive_entries, 2048U);
  EXPECT_EQ(cache.Stats().evictions, 0U);
}

TEST(GribTimelineFrameCache, DifferentTimesQueueTogetherAndSameTimeSharesAReply) {
  Cache cache;
  std::promise<void> first, second;
  auto firstWake = first.get_future(), secondWake = second.get_future();
  std::atomic<unsigned> wakeups{};
  cache.SetRequestWakeup([&] {
    const auto count = ++wakeups;
    if (count == 1) first.set_value();
    if (count == 2) second.set_value();
  });

  auto a = std::async(std::launch::async, [&] { return cache.Acquire(100, 2000, {}); });
  ASSERT_EQ(firstWake.wait_for(1s), std::future_status::ready);
  auto b = std::async(std::launch::async, [&] { return cache.Acquire(200, 2000, {}); });
  ASSERT_EQ(secondWake.wait_for(1s), std::future_status::ready);
  auto duplicate = std::async(std::launch::async, [&] {
    return cache.Acquire(100, 2000, {});
  });
  ASSERT_TRUE(WaitForLookups(cache, 3));
  const auto firstRequest = cache.TakePendingRequest();
  const auto secondRequest = cache.TakePendingRequest();
  ASSERT_TRUE(firstRequest);
  ASSERT_TRUE(secondRequest);
  EXPECT_EQ(firstRequest->key, 100);
  EXPECT_EQ(secondRequest->key, 200);
  EXPECT_FALSE(cache.TakePendingRequest());

  EXPECT_TRUE(cache.Publish(*secondRequest, Frame(2)));
  const auto secondFrame = b.get().frame.GetSharedGribRecordSet();
  ASSERT_TRUE(secondFrame);
  EXPECT_EQ(secondFrame->m_ID, 2U);
  EXPECT_EQ(a.wait_for(0ms), std::future_status::timeout);
  EXPECT_TRUE(cache.Publish(*firstRequest, Frame(1)));
  const auto originalFrame = a.get().frame.GetSharedGribRecordSet();
  ASSERT_TRUE(originalFrame);
  EXPECT_EQ(originalFrame->m_ID, 1U);
  EXPECT_EQ(duplicate.get().frame.GetSharedGribRecordSet(), originalFrame);
  EXPECT_EQ(wakeups.load(), 2U);
  EXPECT_EQ(cache.Stats().misses, 2U);
  EXPECT_EQ(cache.Stats().joined_requests, 1U);
  EXPECT_EQ(cache.Stats().peak_pending_requests, 2U);
}

TEST(GribTimelineFrameCache, CancellingFirstCallerPreservesRequestForOtherWorkers) {
  Cache cache;
  std::atomic<bool> cancelled{};
  auto a = std::async(std::launch::async, [&] {
    return cache.Acquire(100, 2000, [&] { return cancelled.load(); });
  });
  ASSERT_TRUE(WaitForLookups(cache, 1));
  auto b = std::async(std::launch::async, [&] { return cache.Acquire(100, 2000, {}); });
  ASSERT_TRUE(WaitForLookups(cache, 2));
  cancelled.store(true);
  cache.NotifyAll();
  ASSERT_EQ(a.wait_for(250ms), std::future_status::ready);
  EXPECT_EQ(a.get().status, Status::NoResult);
  const auto request = cache.TakePendingRequest();
  ASSERT_TRUE(request);
  EXPECT_TRUE(cache.Publish(*request, Frame(5)));
  EXPECT_EQ(b.get().status, Status::Available);
  EXPECT_EQ(cache.Stats().misses, 1U);
}

TEST(GribTimelineFrameCache, LateReplyCannotCompleteNewRequestForTheSameTime) {
  Cache cache;
  std::optional<Cache::Request> oldRequest;
  cache.SetRequestWakeup([&] { oldRequest = cache.TakePendingRequest(); });
  EXPECT_EQ(cache.Acquire(100, 10, {}).status, Status::NoResult);
  ASSERT_TRUE(oldRequest);
  EXPECT_EQ(cache.Stats().timeouts, 1U);
  EXPECT_EQ(cache.Stats().negative_entries, 0U);

  cache.SetRequestWakeup([&] {
    const auto request = cache.TakePendingRequest();
    ASSERT_TRUE(request);
    EXPECT_NE(request->id, oldRequest->id);
    EXPECT_TRUE(cache.Publish(*oldRequest, Frame(1)));
    Shared_GribRecordSet resident;
    EXPECT_FALSE(cache.TryResident(100, &resident));
    EXPECT_TRUE(cache.Publish(*request, Frame(2)));
  });
  const auto reply = cache.Acquire(100, 100, {});
  ASSERT_EQ(reply.status, Status::Available);
  EXPECT_EQ(reply.frame.GetGribRecordSet()->m_ID, 2U);
}

TEST(GribTimelineFrameCache, PreCancelledLookupDoesNotEnqueue) {
  Cache cache;
  EXPECT_EQ(cache.Acquire(100, 100, [] { return true; }).status, Status::NoResult);
  EXPECT_FALSE(cache.TakePendingRequest());
}

TEST(GribTimelineFrameCache, RetainsMissingMonthAcrossEngineAttempts) {
  Cache cache;
  cache.Configure(2048, false, 32768);
  unsigned requests = 0;
  cache.SetRequestWakeup([&] {
    const auto request = cache.TakePendingRequest();
    ASSERT_TRUE(request);
    ++requests;
    // Use the host's record-handling path for a confirmed missing frame.
    cache.PublishFrame(*request, nullptr);
  });
  constexpr std::int64_t kSlices = 30 * 24 * 4;
  for (int attempt = 0; attempt < 3; ++attempt) {
    for (std::int64_t slice = 0; slice < kSlices; ++slice) {
      const auto reply = cache.Acquire(slice * 900, 100, {});
      EXPECT_EQ(reply.status, Status::NoFrame);
      EXPECT_EQ(reply.frame.GetGribRecordSet(), nullptr);
    }
  }
  EXPECT_EQ(requests, unsigned(kSlices));
  EXPECT_EQ(cache.Stats().negative_hits, 2U * kSlices);
  EXPECT_EQ(cache.Stats().negative_entries, std::size_t(kSlices));
  EXPECT_EQ(cache.Stats().evictions, 0U);
  EXPECT_EQ(cache.TotalWeight(), 0U);
}

TEST(GribTimelineFrameCache, UnansweredServiceIsRetriedInANewBatch) {
  Cache cache;
  unsigned requests = 0;
  cache.SetRequestWakeup([&] {
    const auto request = cache.TakePendingRequest();
    ASSERT_TRUE(request);
    ++requests;
    cache.PublishUnavailable(*request);
  });
  EXPECT_EQ(cache.Acquire(100, 100, {}).status, Status::NoReply);
  EXPECT_EQ(cache.Acquire(200, 100, {}).status, Status::NoResult);
  EXPECT_EQ(requests, 1U);
  EXPECT_EQ(cache.Stats().unavailable_skips, 1U);
  EXPECT_EQ(cache.Size(), 0U);
  cache.Clear();
  EXPECT_EQ(cache.Acquire(100, 100, {}).status, Status::NoReply);
  EXPECT_EQ(requests, 2U);
}
