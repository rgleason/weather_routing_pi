// SPDX-License-Identifier: GPL-3.0-or-later
#include <gtest/gtest.h>
#include "RouteComfort.h"
#include "engine/native/RoutingEngineSequence.h"

namespace {
namespace wr = supercpn::weather_routing;
wr::RouteLeg Leg(std::int64_t start, int seconds, double wind, bool waves = true) {
  wr::RouteLeg leg;
  leg.startTime = wr::TimePoint{wr::Duration{start}};
  leg.endTime = leg.startTime + wr::Duration{seconds};
  leg.wind = {0, -wind};
  leg.courseThroughWaterDegrees = 120;
  leg.waves.available = waves;
  leg.waves.significantHeightMetres = 1;
  leg.start = {53.4, -5.5}; leg.end = {53.4, -5.4};
  return leg;
}
weather_routing::RouteComparisonMetric Metric(const char* id, int time, double exposure) {
  weather_routing::RouteComparisonMetric metric;
  metric.id = id; metric.elapsedSeconds = time;
  metric.comfort.durationSeconds = time;
  metric.comfort.categorySeconds[1] = time;
  metric.comfort.exposureHours = exposure;
  return metric;
}
}

TEST(RouteComfort, ExposureWeightsTimeNotLegCount) {
  const auto one = weather_routing::CalculateRouteComfort({Leg(0, 3600, 24)});
  std::vector<wr::RouteLeg> split;
  for (int i = 0; i < 6; ++i) split.push_back(Leg(i * 600, 600, 24));
  const auto six = weather_routing::CalculateRouteComfort(split);
  EXPECT_EQ(one.categorySeconds, six.categorySeconds);
  EXPECT_NEAR(one.exposureHours, weather_routing::ConditionSeverity(24, 120, 1), 1e-12);
  EXPECT_DOUBLE_EQ(one.exposureHours, six.exposureHours);
}
TEST(RouteComfort, PreservesWorstLegIndependentlyOfTotalExposure) {
  const auto c = weather_routing::CalculateRouteComfort(
      {Leg(0, 3600, 24), Leg(3600, 30, 52), Leg(3630, 3600, 12)});
  ASSERT_TRUE(c.comparable());
  EXPECT_EQ(c.worstLegIndex, 1U);
  EXPECT_EQ(c.worstCategory, 3);
  EXPECT_EQ((c.worstLegEndTime - c.worstLegStartTime).count(), 30);
  EXPECT_EQ(c.categorySeconds[3], 30);
  EXPECT_NEAR(c.exposureHours,
      weather_routing::ConditionSeverity(24, 120, 1) +
      weather_routing::ConditionSeverity(52, 120, 1) / 120 +
      weather_routing::ConditionSeverity(12, 120, 1), 1e-12);
  EXPECT_DOUBLE_EQ(c.worstLegStart.latitude, 53.4);
}
TEST(RouteComfort, LongestDifficultSpellRequiresContiguousIntervals) {
  const auto c = weather_routing::CalculateRouteComfort(
      {Leg(0, 600, 35), Leg(600, 1200, 35), Leg(1800, 600, 12),
       Leg(2400, 900, 35), Leg(4000, 900, 35)});
  EXPECT_EQ(c.categorySeconds[3], 3600);
  EXPECT_EQ(c.longestDifficultSeconds, 1800);
}
TEST(RouteComfort, MissingWavesAreNotCalmAndWindOnlyIsExplicit) {
  const auto legs = std::vector{Leg(0, 600, 24, false), Leg(600, 600, 24)};
  const auto full = weather_routing::CalculateRouteComfort(legs);
  const auto wind = weather_routing::CalculateRouteComfort(legs, true);
  EXPECT_FALSE(full.comparable());
  EXPECT_EQ(full.categorySeconds[0], 600);
  EXPECT_EQ(full.waveCoveredSeconds, 600);
  EXPECT_TRUE(wind.comparable());
  EXPECT_TRUE(wind.windOnly);
  EXPECT_EQ(wind.waveCoveredSeconds, 600);
}
TEST(RouteComfort, NonFiniteWindCannotWinComfortComparison) {
  auto leg = Leg(0, 600, 24);
  leg.wind.eastKnots = std::numeric_limits<double>::quiet_NaN();
  EXPECT_FALSE(weather_routing::CalculateRouteComfort({leg}).comparable());
}
TEST(RouteComfort, SliderReachesIntermediateAndEndpointRoutes) {
  const std::vector routes{Metric("A", 36000, 8), Metric("B", 39600, 5),
                           Metric("C", 46800, 3)};
  EXPECT_EQ(weather_routing::SelectComfortCandidate(routes, 0).selected, 0U);
  EXPECT_EQ(weather_routing::SelectComfortCandidate(routes, 33).selected, 0U);
  const auto middle = weather_routing::SelectComfortCandidate(routes, 34);
  EXPECT_EQ(middle.selected, 1U);
  EXPECT_EQ(middle.allowanceSeconds, 3672);
  EXPECT_EQ(weather_routing::SelectComfortCandidate(routes, 100).selected, 2U);
}
TEST(RouteComfort, DominatedRouteNeverWinsSlider) {
  const std::vector routes{Metric("A", 1000, 8), Metric("B", 1100, 10),
                           Metric("C", 1300, 3)};
  for (int i = 0; i <= 100; ++i)
    EXPECT_NE(weather_routing::SelectComfortCandidate(routes, i).selected, 1U);
}
TEST(RouteComfort, ComfortTieChoosesFasterAndTimeTieChoosesComfort) {
  const std::vector routes{Metric("A", 1000, 8), Metric("B", 1000, 5),
                           Metric("C", 1300, 5)};
  EXPECT_EQ(weather_routing::SelectComfortCandidate(routes, 0).selected, 1U);
  const auto selected = weather_routing::SelectComfortCandidate(routes, 100);
  EXPECT_EQ(selected.selected, 1U);
  EXPECT_FALSE(selected.tradeOffAvailable);
}
TEST(RouteComfort, UnknownCoverageRemainsFastestEligibleButCannotWinComfort) {
  auto unknown = Metric("A", 1000, 0);
  unknown.comfort.categorySeconds[0] = 1000;
  const std::vector routes{unknown, Metric("B", 1300, 3)};
  EXPECT_EQ(weather_routing::SelectComfortCandidate(routes, 0).selected, 0U);
  EXPECT_EQ(weather_routing::SelectComfortCandidate(routes, 100).selected, 1U);
}
TEST(RouteComfort, OneRouteHasNoTradeoff) {
  const auto selection = weather_routing::SelectComfortCandidate({Metric("A", 1000, 3)}, 50);
  EXPECT_FALSE(selection.tradeOffAvailable);
  EXPECT_TRUE(selection.comfortAvailable);
}
TEST(RouteComfort, RetentionObserverSeesOnlyValidatedRoutesAndAutoStops) {
  using namespace weather_routing::native;
  std::vector<SequenceEngine> observed;
  auto solve = [](SequenceEngine engine) {
    wr::RoutingResult result;
    result.status = wr::RoutingStatus::Complete;
    result.validation.passed = engine != SequenceEngine::Quick;
    result.legs.push_back(Leg(0, 600, 24));
    return result;
  };
  const auto observer = [&](SequenceEngine engine, const wr::RoutingResult&) {
    observed.push_back(engine);
  };
  RunRoutingEngineSequence(weather_routing::RoutingEngine::All, wr::CancellationToken{}, solve, observer);
  EXPECT_EQ(observed, (std::vector{SequenceEngine::Original, SequenceEngine::Alternative, SequenceEngine::Main}));
  observed.clear();
  RunRoutingEngineSequence(weather_routing::RoutingEngine::Auto, wr::CancellationToken{}, solve, observer);
  EXPECT_EQ(observed, (std::vector{SequenceEngine::Original}));
}
