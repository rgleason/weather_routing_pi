#include <gtest/gtest.h>
#include <thread>
#include "RoutingProgressState.h"

TEST(RoutingProgressState, HiddenReadersRetainUpdatesAndRestartRejectsOldWorker) {
  weather_routing::RoutingProgressState<int> state;
  const auto old = state.Begin();
  state.Publish(old, 12);
  const auto first = state.Read();
  EXPECT_EQ(state.Read().value, 12);
  EXPECT_EQ(state.Read().sequence, first.sequence);
  const auto current = state.Begin();
  state.Publish(old, 99);
  EXPECT_EQ(state.Read().sequence, 0U);
  state.Publish(current, 34);
  EXPECT_EQ(state.Read().value, 34);
  EXPECT_GE(state.Read().updated, state.Read().started);
}

TEST(RoutingProgressState, ConcurrentPublicationKeepsEachSnapshotConsistent) {
  struct Pair { int first{}, second{}; };
  weather_routing::RoutingProgressState<Pair> state;
  const auto generation = state.Begin();
  std::thread worker([&] {
    for (int i = 1; i <= 10000; ++i) state.Publish(generation, {i, -i});
  });
  for (int i = 0; i < 10000; ++i) {
    const auto snapshot = state.Read();
    EXPECT_EQ(snapshot.value.first, -snapshot.value.second);
  }
  worker.join();
  EXPECT_EQ(state.Read().sequence, 10000U);
}
