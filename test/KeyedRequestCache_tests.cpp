#include <atomic>
#include <chrono>
#include <future>
#include <map>
#include <thread>
#include <vector>

#include <gtest/gtest.h>

#include "KeyedRequestCache.h"

using namespace std::chrono_literals;

TEST(KeyedRequestCache, ResidentProbeHasNoAcquisitionOrRecencySideEffects) {
  weather_routing::KeyedRequestCache<int,int> cache(2);
  int value=0;
  EXPECT_FALSE(cache.TryResident(1,&value));
  const auto before=cache.Stats();
  cache.Publish(1,10,true); cache.Publish(2,20,true);
  EXPECT_TRUE(cache.TryResident(1,&value)); EXPECT_EQ(value,10);
  cache.Publish(3,30,true);
  EXPECT_FALSE(cache.TryResident(1,&value)); // Inspection did not promote LRU.
  EXPECT_TRUE(cache.TryResident(2,&value));
  const auto after=cache.Stats();
  EXPECT_EQ(before.lookups,after.lookups); EXPECT_EQ(before.misses,after.misses);
  EXPECT_EQ(before.hits,after.hits);
  cache.Publish(9,0,false); EXPECT_FALSE(cache.TryResident(9,&value));
}

TEST(KeyedRequestCache, CoalescesConcurrentCallersForOneKey) {
  weather_routing::KeyedRequestCache<int, int> cache(8);
  std::atomic<int> requests{0};
  std::promise<void> request_started;
  std::promise<void> release_promise;
  std::shared_future<void> release = release_promise.get_future().share();

  constexpr int kCallers = 20;
  std::vector<std::future<std::pair<bool, int>>> callers;
  callers.reserve(kCallers);
  for (int i = 0; i < kCallers; ++i) {
    callers.push_back(std::async(std::launch::async, [&] {
      int value = 0;
      const bool ok = cache.Acquire(
          42, &value, 2000,
          [&](const int&) {
            if (requests.fetch_add(1) == 0) request_started.set_value();
            release.wait();
            cache.Publish(42, 314, true);
          },
          [] { return false; });
      return std::make_pair(ok, value);
    }));
  }

  ASSERT_EQ(request_started.get_future().wait_for(1s),
            std::future_status::ready);
  release_promise.set_value();
  for (auto& caller : callers) {
    const auto result = caller.get();
    EXPECT_TRUE(result.first);
    EXPECT_EQ(result.second, 314);
  }
  EXPECT_EQ(requests.load(), 1);
}

TEST(KeyedRequestCache, SerializesDifferentKeysAndPublishesByExplicitKey) {
  weather_routing::KeyedRequestCache<int, int> cache(8);
  std::mutex order_mutex;
  std::vector<int> request_order;

  auto acquire = [&](int key) {
    int value = 0;
    const bool ok = cache.Acquire(
        key, &value, 2000,
        [&](const int& requested_key) {
          {
            std::lock_guard<std::mutex> lock(order_mutex);
            request_order.push_back(requested_key);
          }
          cache.Publish(requested_key, requested_key * 10, true);
        },
        [] { return false; });
    return std::make_pair(ok, value);
  };

  auto first = std::async(std::launch::async, acquire, 11);
  auto second = std::async(std::launch::async, acquire, 12);
  const auto first_result = first.get();
  const auto second_result = second.get();

  EXPECT_TRUE(first_result.first);
  EXPECT_EQ(first_result.second, 110);
  EXPECT_TRUE(second_result.first);
  EXPECT_EQ(second_result.second, 120);
  ASSERT_EQ(request_order.size(), 2u);
  EXPECT_NE(request_order[0], request_order[1]);
}

TEST(KeyedRequestCache, NegativeResultIsCoalescedWithoutRequestStorm) {
  weather_routing::KeyedRequestCache<int, int> cache(8);
  int requests = 0;
  int value = 99;
  const auto request = [&](const int& key) {
    ++requests;
    cache.Publish(key, 0, false);
  };

  EXPECT_FALSE(cache.Acquire(7, &value, 100, request, [] { return false; }));
  EXPECT_EQ(value, 0);
  value = 99;
  EXPECT_FALSE(cache.Acquire(7, &value, 100, request, [] { return false; }));
  EXPECT_EQ(value, 0);
  EXPECT_EQ(requests, 1);
}

TEST(KeyedRequestCache, TimedOutQueuedKeyIsNotCachedAsMissingWeather) {
  weather_routing::KeyedRequestCache<int, int> cache(4);
  std::atomic<int> requests{0};
  std::promise<void> first_request_started;
  auto first_started = first_request_started.get_future();

  std::thread first([&] {
    int value = 0;
    EXPECT_TRUE(cache.Acquire(
        1, &value, 500,
        [&](const int&) {
          ++requests;
          first_request_started.set_value();
        },
        [] { return false; }));
    EXPECT_EQ(value, 11);
  });
  ASSERT_EQ(first_started.wait_for(std::chrono::seconds(1)),
            std::future_status::ready);

  int value = 0;
  EXPECT_FALSE(cache.Acquire(
      2, &value, 10, [&](const int&) { ++requests; }, [] { return false; }));
  EXPECT_EQ(requests.load(), 1);

  // Complete the first request, then prove key 2 is requested rather than
  // returned from a false negative inserted by its earlier queue timeout.
  cache.Publish(1, 11, true);
  first.join();
  EXPECT_TRUE(cache.Acquire(
      2, &value, 100,
      [&](const int&) {
        ++requests;
        cache.Publish(2, 22, true);
      },
      [] { return false; }));
  // Publish is deliberately synchronous in this test; Acquire sees it on its
  // next iteration and returns the valid result.
  EXPECT_EQ(requests.load(), 2);
  EXPECT_TRUE(
      cache.Acquire(2, &value, 10, [](const int&) {}, [] { return false; }));
  EXPECT_EQ(value, 22);
}

TEST(KeyedRequestCache, RetainsOneHundredTwentyEightHourQuarterHourWorkingSet) {
  constexpr std::size_t kFrames = 512;
  weather_routing::KeyedRequestCache<int, int> cache(kFrames);
  int requests = 0;
  for (int key = 0; key < static_cast<int>(kFrames); ++key) {
    int value = 0;
    EXPECT_TRUE(cache.Acquire(
        key, &value, 100,
        [&](const int& requested) {
          ++requests;
          cache.Publish(requested, requested * 2, true);
        },
        [] { return false; }));
    EXPECT_EQ(value, key * 2);
  }
  for (int key = static_cast<int>(kFrames) - 1; key >= 0; --key) {
    int value = 0;
    EXPECT_TRUE(cache.Acquire(
        key, &value, 10, [&](const int&) { ++requests; },
        [] { return false; }));
    EXPECT_EQ(value, key * 2);
  }
  EXPECT_EQ(requests, static_cast<int>(kFrames));
}

TEST(KeyedRequestCache, EvictionBoundsMemoryWithoutLimitingKeyHorizon) {
  weather_routing::KeyedRequestCache<int, int> cache(2);
  std::map<int, int> requests;

  const auto acquire = [&](int key) {
    int value = 0;
    const bool ok = cache.Acquire(
        key, &value, 100,
        [&](const int& requested) {
          ++requests[requested];
          cache.Publish(requested, requested * 10, true);
        },
        [] { return false; });
    EXPECT_TRUE(ok);
    EXPECT_EQ(value, key * 10);
  };

  acquire(1);
  acquire(2);
  acquire(3);
  EXPECT_EQ(cache.Size(), 2u);
  EXPECT_EQ(requests[1], 1);
  EXPECT_EQ(requests[2], 1);
  EXPECT_EQ(requests[3], 1);

  // Key 1 has fallen outside the retained working set. Reacquiring it must
  // issue another host request and succeed: capacity bounds memory, not the
  // temporal range over which callers can request weather.
  acquire(1);
  EXPECT_EQ(cache.Size(), 2u);
  EXPECT_EQ(requests[1], 2);

  // A key arbitrarily farther into the future remains requestable as well.
  acquire(1000000);
  EXPECT_EQ(cache.Size(), 2u);
  EXPECT_EQ(requests[1000000], 1);
}

TEST(KeyedRequestCache, EvictsLeastRecentlyUsedEntriesByWeight) {
  weather_routing::KeyedRequestCache<int, int> cache(
      8, 10, [](const int& value) { return static_cast<std::size_t>(value); });

  cache.Publish(1, 4, true);
  cache.Publish(2, 4, true);
  cache.Publish(3, 4, true);

  EXPECT_EQ(cache.Size(), 2u);
  EXPECT_EQ(cache.TotalWeight(), 8u);

  int value = 0;
  int requests = 0;
  EXPECT_TRUE(cache.Acquire(
      2, &value, 10, [&](const int&) { ++requests; }, [] { return false; }));
  EXPECT_EQ(value, 4);
  EXPECT_EQ(requests, 0);

  cache.Publish(4, 5, true);
  EXPECT_EQ(cache.Size(), 2u);
  EXPECT_EQ(cache.TotalWeight(), 9u);

  EXPECT_TRUE(cache.Acquire(
      2, &value, 10, [&](const int&) { ++requests; }, [] { return false; }));
  EXPECT_EQ(requests, 0);
}

TEST(KeyedRequestCache, RetainsOneReplyLargerThanWeightBudget) {
  weather_routing::KeyedRequestCache<int, int> cache(
      8, 10, [](const int& value) { return static_cast<std::size_t>(value); });
  cache.Publish(7, 20, true);

  EXPECT_EQ(cache.Size(), 1u);
  EXPECT_EQ(cache.TotalWeight(), 20u);

  int value = 0;
  int requests = 0;
  EXPECT_TRUE(cache.Acquire(
      7, &value, 10, [&](const int&) { ++requests; }, [] { return false; }));
  EXPECT_EQ(value, 20);
  EXPECT_EQ(requests, 0);
}

TEST(KeyedRequestCache, LoweredQuickBudgetEvictsAndRestoresMainCapacity) {
  weather_routing::KeyedRequestCache<int, int> cache(10, 100, [](const int&) { return 20U; });
  for (int i=0; i<5; ++i) ASSERT_TRUE(cache.Publish(i, i, true));
  EXPECT_EQ(cache.TotalWeight(), 100U);
  cache.SetMaximumWeight(40);
  EXPECT_EQ(cache.Size(), 2U);
  EXPECT_EQ(cache.TotalWeight(), 40U);
  cache.SetMaximumWeight(100);
  for (int i=5; i<8; ++i) ASSERT_TRUE(cache.Publish(i, i, true));
  EXPECT_EQ(cache.Size(), 5U);
  cache.SetMaximumWeight(1);
  EXPECT_EQ(cache.Size(), 1U);
  EXPECT_EQ(cache.TotalWeight(), 20U);
}

TEST(KeyedRequestCache, ReportsReloadsAndExplicitlyReleasesAllStorage) {
  weather_routing::KeyedRequestCache<int, int> cache(
      2, 8, [](const int&) { return 4U; });
  ASSERT_TRUE(cache.Publish(1, 10, true));
  ASSERT_TRUE(cache.Publish(2, 20, true));

  int value = 0;
  EXPECT_TRUE(cache.Acquire(1, &value, 10, [](const int&) {},
                            [] { return false; }));
  EXPECT_EQ(value, 10);
  ASSERT_TRUE(cache.Publish(3, 30, true));
  EXPECT_EQ(cache.Stats().evictions, 1U);

  EXPECT_TRUE(cache.Acquire(
      2, &value, 10,
      [&](const int& key) { cache.Publish(key, 20, true); },
      [] { return false; }));
  EXPECT_EQ(cache.Stats().reloads_after_eviction, 1U);
  EXPECT_GT(cache.TotalWeight(), 0U);

  cache.Clear();
  EXPECT_EQ(cache.Size(), 0U);
  EXPECT_EQ(cache.TotalWeight(), 0U);
  EXPECT_EQ(cache.Stats().hits, 0U);
  EXPECT_EQ(cache.Stats().misses, 0U);
  EXPECT_EQ(cache.Stats().evictions, 0U);
  EXPECT_EQ(cache.Stats().reloads_after_eviction, 0U);
}

TEST(KeyedRequestCache, MissingMonthSurvivesFrameEvictionAndMemoryPressure) {
  weather_routing::KeyedRequestCache<int, int> cache(
      512, 10, [](const int& value) { return std::size_t(value); }, 32768);
  int requests = 0;
  constexpr int kMonth = 30 * 24 * 4;
  const auto missing = [&](int key) {
    int value = 99;
    EXPECT_FALSE(cache.Acquire(
        key, &value, 100,
        [&](const int& requested) {
          ++requests;
          EXPECT_TRUE(cache.Publish(requested, 0, false));
        }, [] { return false; }));
    EXPECT_EQ(value, 0);
  };
  for (int key = 0; key < kMonth; ++key) missing(key);
  // Real grids churn through their independent payload budget. Negative
  // replies must survive, including when the remaining grid is oversized.
  for (int key = kMonth; key < kMonth + 100; ++key)
    ASSERT_TRUE(cache.Publish(key, 8, true));
  ASSERT_TRUE(cache.Publish(kMonth + 100, 20, true));
  cache.SetMaximumWeight(1);
  for (int pass = 0; pass < 2; ++pass)
    for (int key = 0; key < kMonth; ++key) missing(key);
  EXPECT_EQ(requests, kMonth);
  EXPECT_EQ(cache.Stats().negative_entries, std::size_t(kMonth));
  EXPECT_EQ(cache.Stats().positive_entries, 1U);
  EXPECT_EQ(cache.Stats().negative_hits, 2U * kMonth);
  EXPECT_EQ(cache.Stats().misses, std::uint64_t(kMonth));
  EXPECT_EQ(cache.Stats().reloads_after_eviction, 0U);
}

TEST(KeyedRequestCache, MissingEntriesHaveTheirOwnBoundAndCanBecomeValid) {
  weather_routing::KeyedRequestCache<int, int> cache(2, 100, {}, 3);
  for (int i = 0; i < 6; ++i) ASSERT_TRUE(cache.Publish(i, 0, false));
  EXPECT_EQ(cache.Stats().negative_entries, 3U);
  EXPECT_EQ(cache.Stats().evictions, 3U);
  ASSERT_TRUE(cache.Publish(3, 30, true));
  ASSERT_TRUE(cache.Publish(4, 40, true));
  EXPECT_EQ(cache.Stats().negative_entries, 1U);
  EXPECT_EQ(cache.Stats().positive_entries, 2U);
  ASSERT_TRUE(cache.Publish(4, 0, false));
  EXPECT_EQ(cache.Stats().negative_entries, 2U);
  EXPECT_EQ(cache.Stats().positive_entries, 1U);
  int value = 0;
  EXPECT_TRUE(cache.Acquire(3, &value, 10, [](const int&) {},
                            [] { return false; }));
  EXPECT_EQ(value, 30);
}

TEST(KeyedRequestCache, UnansweredProviderDoesNotReceiveEveryNewTimestamp) {
  using Cache = weather_routing::KeyedRequestCache<int, int>;
  Cache::Clock::time_point now{};
  Cache cache(512, 1024, {}, 32768, [&] { return now; });
  int requests = 0;
  for (int key = 0; key < 10000; ++key) {
    int value = 99;
    EXPECT_FALSE(cache.Acquire(
        key, &value, 100,
        [&](const int& requested) {
          ++requests;
          cache.PublishUnavailable(requested);
        }, [] { return false; }));
    EXPECT_EQ(value, 0);
  }
  EXPECT_EQ(requests, 1);
  EXPECT_EQ(cache.Size(), 0U);  // No invented "missing frame" entries.
  EXPECT_EQ(cache.Stats().unavailable_replies, 1U);
  EXPECT_EQ(cache.Stats().unavailable_skips, 9999U);
  EXPECT_EQ(cache.Stats().distinct_keys, 1U);

  now += 1s;
  int value = 0;
  EXPECT_TRUE(cache.Acquire(
      0, &value, 100,
      [&](const int& requested) {
        ++requests;
        cache.Publish(requested, 42, true);
      }, [] { return false; }));
  EXPECT_EQ(value, 42);
  EXPECT_EQ(requests, 2);
  EXPECT_EQ(cache.Stats().reloads_after_eviction, 0U);
}

TEST(KeyedRequestCache, ProviderBackoffIsBoundedAndAnActualReplyResetsIt) {
  using Cache = weather_routing::KeyedRequestCache<int, int>;
  Cache::Clock::time_point now{};
  Cache cache(8, 100, {}, 8, [&] { return now; });
  int requests = 0;
  int value = 0;
  const auto unavailable = [&](const int& key) {
    ++requests;
    cache.PublishUnavailable(key);
  };
  int key = 0;
  for (const auto delay : {1s, 2s, 4s, 8s, 16s, 30s, 30s}) {
    EXPECT_FALSE(cache.Acquire(key++, &value, 100, unavailable,
                               [] { return false; }));
    const int before = requests;
    now += delay - 1ms;
    EXPECT_FALSE(cache.Acquire(-1, &value, 100, unavailable,
                               [] { return false; }));
    EXPECT_EQ(requests, before);
    now += 1ms;
  }
  EXPECT_EQ(requests, 7);
  // Confirmed no data is still a successful conversation with the provider.
  EXPECT_FALSE(cache.Acquire(
      100, &value, 100,
      [&](const int& requested) {
        ++requests;
        cache.Publish(requested, 0, false);
      }, [] { return false; }));
  EXPECT_FALSE(cache.Acquire(101, &value, 100, unavailable,
                             [] { return false; }));
  now += 1s;
  EXPECT_FALSE(cache.Acquire(102, &value, 100, unavailable,
                             [] { return false; }));
  EXPECT_EQ(requests, 10);
}

TEST(KeyedRequestCache, ClearingBatchResetsProviderBackoff) {
  using Cache = weather_routing::KeyedRequestCache<int, int>;
  Cache::Clock::time_point now{};
  Cache cache(8, 100, {}, 8, [&] { return now; });
  int value = 0;
  EXPECT_FALSE(cache.Acquire(
      1, &value, 100, [&](const int& key) { cache.PublishUnavailable(key); },
      [] { return false; }));
  cache.Clear();
  EXPECT_TRUE(cache.Acquire(
      1, &value, 100,
      [&](const int& key) { cache.Publish(key, 11, true); },
      [] { return false; }));
  EXPECT_EQ(value, 11);
  EXPECT_EQ(cache.Stats().unavailable_replies, 0U);
}

TEST(KeyedRequestCache, UnavailableReplyWakesConcurrentQueuedCallers) {
  weather_routing::KeyedRequestCache<int, int> cache(8);
  std::atomic<int> requests{0};
  std::promise<void> started;
  std::promise<void> release;
  auto released = release.get_future().share();
  auto first = std::async(std::launch::async, [&] {
    int value = 0;
    return cache.Acquire(
        1, &value, 2000,
        [&](const int& key) {
          ++requests;
          started.set_value();
          released.wait();
          cache.PublishUnavailable(key);
        }, [] { return false; });
  });
  ASSERT_EQ(started.get_future().wait_for(1s), std::future_status::ready);
  std::vector<std::future<bool>> queued;
  for (int key = 2; key < 22; ++key)
    queued.push_back(std::async(std::launch::async, [&, key] {
      int value = 0;
      return cache.Acquire(
          key, &value, 2000,
          [&](const int& requested) {
            ++requests;
            cache.PublishUnavailable(requested);
          }, [] { return false; });
    }));
  release.set_value();
  EXPECT_FALSE(first.get());
  for (auto& caller : queued) EXPECT_FALSE(caller.get());
  EXPECT_EQ(requests.load(), 1);
  EXPECT_EQ(cache.Size(), 0U);
}

TEST(KeyedRequestCache, ProviderFailureDoesNotHideCachedForecastOrMissingFrame) {
  weather_routing::KeyedRequestCache<int, int> cache(8);
  ASSERT_TRUE(cache.Publish(1, 42, true));
  ASSERT_TRUE(cache.Publish(2, 0, false));
  int value = 0;
  EXPECT_FALSE(cache.Acquire(
      3, &value, 100, [&](const int& key) { cache.PublishUnavailable(key); },
      [] { return false; }));
  EXPECT_TRUE(cache.Acquire(1, &value, 100, [](const int&) {},
                            [] { return false; }));
  EXPECT_EQ(value, 42);
  EXPECT_FALSE(cache.Acquire(2, &value, 100, [](const int&) {},
                             [] { return false; }));
  EXPECT_EQ(cache.Stats().negative_hits, 1U);
}
