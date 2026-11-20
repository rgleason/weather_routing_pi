// SPDX-License-Identifier: GPL-3.0-or-later
#include <gtest/gtest.h>
#include "engine/native/RoutingStepExecutor.h"
#include "Quick2Fixtures.h"
#include "supercpn/weather_routing/Engine.h"
#include <atomic>
#include <numeric>

namespace {
namespace wr = supercpn::weather_routing;
using weather_routing::native::RoutingStepExecutor;
TEST(RoutingStepExecutor, BoundedAdmissionExceptionAndReuse) {
  weather_routing::RoutingWorkerBudget budget;
  budget.SetLimit(2);
  {
    RoutingStepExecutor pool([] { return wr::RoutingEnvironment{}; },
        [&] { return budget.TryAcquire(); });
    std::vector<unsigned> values(128);
    for (unsigned generation = 1; generation <= 20; ++generation) {
      pool.Run(values.size(), [&](std::size_t i, const auto&) { values[i] = generation; });
      EXPECT_EQ(std::accumulate(values.begin(), values.end(), 0u), 128 * generation);
      EXPECT_LE(budget.InUse(), 2u);
    }
    EXPECT_THROW(pool.Run(128, [](std::size_t i, const auto&) {
      if (i == 4) throw std::runtime_error("provider failure");
    }), std::runtime_error);
    pool.Run(values.size(), [&](std::size_t i, const auto&) { values[i] = 1; });
    EXPECT_EQ(std::accumulate(values.begin(), values.end(), 0u), 128u);
  }
  EXPECT_EQ(budget.InUse(), 0u);
}

TEST(RoutingStepExecutor, Quick2ParallelReductionPreservesTimedTrack) {
  for (const char* name : {"open", "variable", "tidal", "island", "dateline"}) {
    auto f = quick2_fixtures::make(name);
    f.environment.performance = std::make_shared<wr::PolarPerformanceModel>(f.request.vessel);
    auto serial = quick2_routing::Engine{}.route(f.request, f.environment).route;
    weather_routing::RoutingWorkerBudget budget;
    budget.SetLimit(2);
    RoutingStepExecutor pool([&] { return f.environment; }, [&] { return budget.TryAcquire(); });
    f.environment.parallelFor = [&](std::size_t count, const RoutingStepExecutor::Task& task) {
      pool.Run(count, task);
    };
    auto parallel = quick2_routing::Engine{}.route(f.request, f.environment).route;
    ASSERT_EQ(serial.status, parallel.status) << name;
    ASSERT_EQ(serial.validation.passed, parallel.validation.passed) << name;
    ASSERT_EQ(serial.legs.size(), parallel.legs.size()) << name;
    for (std::size_t i = 0; i < serial.legs.size(); ++i) {
      EXPECT_DOUBLE_EQ(serial.legs[i].end.latitude, parallel.legs[i].end.latitude);
      EXPECT_DOUBLE_EQ(serial.legs[i].end.longitude, parallel.legs[i].end.longitude);
      EXPECT_EQ(serial.legs[i].endTime, parallel.legs[i].endTime);
    }
  }
}
}  // namespace

TEST(RoutingStepExecutor, StandardParallelReductionPreservesTimedTrack) {
  namespace wr = supercpn::weather_routing;
  auto f = quick2_fixtures::make("island");
  auto serial = wr::RoutingEngine{}.route(f.request, f.environment);
  weather_routing::RoutingWorkerBudget budget;
  budget.SetLimit(2);
  weather_routing::native::RoutingStepExecutor pool([&] { return f.environment; },
      [&] { return budget.TryAcquire(); });
  f.environment.parallelFor = [&](std::size_t n, const auto& task) { pool.Run(n, task); };
  auto parallel = wr::RoutingEngine{}.route(f.request, f.environment);
  ASSERT_EQ(serial.status, parallel.status);
  ASSERT_EQ(serial.validation.passed, parallel.validation.passed);
  ASSERT_EQ(serial.legs.size(), parallel.legs.size());
  for (std::size_t i = 0; i < serial.legs.size(); ++i) {
    EXPECT_DOUBLE_EQ(serial.legs[i].end.latitude, parallel.legs[i].end.latitude);
    EXPECT_DOUBLE_EQ(serial.legs[i].end.longitude, parallel.legs[i].end.longitude);
    EXPECT_EQ(serial.legs[i].endTime, parallel.legs[i].endTime);
  }
}

TEST(RoutingStepExecutor, CancellingParallelSearchCannotPublishAnUnvalidatedTrack) {
  namespace wr = supercpn::weather_routing;
  auto f = quick2_fixtures::make("island");
  weather_routing::RoutingWorkerBudget budget;
  budget.SetLimit(2);
  weather_routing::native::RoutingStepExecutor pool([&] { return f.environment; },
      [&] { return budget.TryAcquire(); });
  f.environment.parallelFor = [&](std::size_t n, const auto& task) {
    f.request.cancellation.cancel();
    pool.Run(n, task);
  };
  auto result = quick2_routing::Engine{}.route(f.request, f.environment).route;
  EXPECT_EQ(result.status, wr::RoutingStatus::Cancelled);
  EXPECT_FALSE(result.validation.passed);
  EXPECT_TRUE(result.legs.empty());
}

TEST(RoutingStepExecutor, NoHelperAdmissionUsesOrdinarySerialEvaluation) {
  namespace wr = supercpn::weather_routing;
  auto f = quick2_fixtures::make("variable");
  f.environment.performance = std::make_shared<wr::PolarPerformanceModel>(f.request.vessel);
  auto serial = quick2_routing::Engine{}.route(f.request, f.environment);
  auto serialStandard = wr::RoutingEngine{}.route(f.request, f.environment);
  weather_routing::native::RoutingStepExecutor pool(
      [&] { return f.environment; }, [] {
        return std::optional<weather_routing::RoutingWorkerBudget::Slot>{};
      });
  EXPECT_FALSE(pool.CanParallelize());
  f.environment.canParallelize = [&] { return pool.CanParallelize(); };
  unsigned parallelCalls = 0;
  f.environment.parallelFor = [&](std::size_t, const auto&) { ++parallelCalls; };
  auto actual = quick2_routing::Engine{}.route(f.request, f.environment);
  auto actualStandard = wr::RoutingEngine{}.route(f.request, f.environment);
  EXPECT_EQ(actualStandard.status, serialStandard.status);
  EXPECT_EQ(parallelCalls, 0u);
  EXPECT_EQ(actual.route.status, serial.route.status);
  EXPECT_EQ(actual.frontier.preparedMotions, actual.frontier.exactMotions);
  ASSERT_EQ(actual.route.legs.size(), serial.route.legs.size());
  for (std::size_t i=0;i<serial.route.legs.size();++i) {
    EXPECT_EQ(actual.route.legs[i].end, serial.route.legs[i].end);
    EXPECT_EQ(actual.route.legs[i].endTime, serial.route.legs[i].endTime);
  }
}
