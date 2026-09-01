#include <atomic>
#include <chrono>
#include <future>
#include <map>
#include <thread>
#include <vector>

#include <gtest/gtest.h>

#include "KeyedRequestCache.h"

using namespace std::chrono_literals;

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
