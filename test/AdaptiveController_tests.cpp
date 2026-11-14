// SPDX-License-Identifier: GPL-3.0-or-later
#include <gtest/gtest.h>
#include "Quick2Fixtures.h"
#include "engine/adaptive/Controller.h"
#include "engine/adaptive/MemoryPolicy.h"
#include "engine/adaptive/NativeFeedback.h"
#include "engine/adaptive/Quick2.h"
#include "engine/adaptive/StandardRecovery.h"
#include "supercpn/weather_routing/QuickEngine.h"

namespace a = weather_routing::adaptive;
namespace wr = supercpn::weather_routing;
using weather_routing::RoutingEngine;
namespace {
wr::RoutingResult Good(const wr::RoutingRequest& request, int seconds) {
  wr::RoutingResult r;
  r.status = wr::RoutingStatus::Complete;
  r.validation.passed = true;
  wr::RouteLeg leg;
  leg.start = request.start;
  leg.end = request.destination;
  leg.startTime = request.departure;
  leg.endTime = request.departure + wr::Duration{seconds};
  r.legs.push_back(leg);
  r.metrics.elapsed = wr::Duration{seconds};
  return r;
}
void SameTrack(const wr::RoutingResult& x, const wr::RoutingResult& y) {
  ASSERT_EQ(x.status, y.status);
  ASSERT_EQ(x.legs.size(), y.legs.size());
  EXPECT_EQ(x.metrics.elapsed, y.metrics.elapsed);
  for (std::size_t i = 0; i < x.legs.size(); ++i) {
    EXPECT_DOUBLE_EQ(x.legs[i].end.latitude, y.legs[i].end.latitude);
    EXPECT_DOUBLE_EQ(x.legs[i].end.longitude, y.legs[i].end.longitude);
    EXPECT_EQ(x.legs[i].endTime, y.legs[i].endTime);
  }
}
TEST(AdaptiveController, FirstAcceptedStopsBeforeOptionalComparison) {
  auto f = quick2_fixtures::make("short");
  const auto saved = f.request.options;
  a::Features features;
  features.offshore = features.windAtStart = features.sketchResolved = true;
  features.directNm = 1000;
  features.hours = 100;
  a::Options options;
  options.nativeAllowance = 1000000;
  unsigned calls = 0;
  const auto solve = [&](const auto& request, const auto&, auto, auto,
                         const auto&) {
    return a::AttemptResult{Good(request, ++calls == 1 ? 200 : 100), 100};
  };
  auto first = a::Run(options, f.request, features, solve);
  EXPECT_EQ(calls, 1U);
  EXPECT_EQ(first.route.metrics.elapsed.count(), 200);
  options.objective = a::Objective::BestCompleted;
  calls = 0;
  auto best = a::Run(options, f.request, features, solve);
  EXPECT_GT(calls, 1U);
  EXPECT_EQ(best.route.metrics.elapsed.count(), 100);
  EXPECT_EQ(f.request.options.timeStep, saved.timeStep);
  EXPECT_EQ(f.request.options.headingStepDegrees, saved.headingStepDegrees);
}
TEST(AdaptiveController, RejectedCandidateCannotStopAutoOrReplaceAnIncumbent) {
  auto f = quick2_fixtures::make("short");
  a::Features features;
  features.offshore = features.windAtStart = true;
  features.directNm = 1000;
  features.hours = 100;
  a::Options options;
  options.nativeAllowance = 1000000;
  unsigned accepted = 0;
  const auto solve = [&](const auto& r, const auto&, auto, auto, const auto&) {
    return a::AttemptResult{Good(r, 100), 100};
  };
  auto result = a::Run(options, f.request, features, solve,
                       [&](auto&) { return ++accepted > 1; });
  EXPECT_EQ(accepted, 2U);
  EXPECT_TRUE(result.route.validation.passed);
  options.objective = a::Objective::BestCompleted;
  accepted = 0;
  result = a::Run(options, f.request, features, solve,
                  [&](auto&) { return ++accepted == 1; });
  EXPECT_GT(accepted, 1U);
  EXPECT_TRUE(result.route.validation.passed);
}
TEST(AdaptiveController,
     UnresolvedCoastalSketchRetainsQualifiedQuick2Allowance) {
  auto f = quick2_fixtures::make("short");
  a::Features features;
  features.coastalFraction = 1;
  a::Options options;
  options.engine = RoutingEngine::Quick2;
  options.nativeAllowance = 1000000;
  options.memory = {true, 8192, 819, 1024, 256};
  const auto dry = [&](a::Options o) {
    return a::Run(o, f.request, features, {}, {}, a::Clock::now());
  };
  EXPECT_EQ(dry(options).allowance, 2000000U);
  options.hardWorkCeiling = 1500000;
  EXPECT_EQ(dry(options).allowance, 1500000U);
  options.hardWorkCeiling = 0;
  options.memory.known = false;
  EXPECT_EQ(dry(options).allowance, 1000000U);
}
TEST(AdaptiveController, RecoveryReserveDoesNotPostponeOriginalFirstPhase) {
  auto f = quick2_fixtures::make("short");
  a::Features features;
  features.chordBlocked = true;
  a::Options o;
  o.engine = RoutingEngine::Quick;
  o.nativeAllowance = 600000;
  o.memory = {true, 8192, 819, 1024, 256};
  auto r = a::Run(o, f.request, features, {}, {}, a::Clock::now());
  EXPECT_EQ(r.allowance, 900000U);
  EXPECT_EQ(r.standardFirstPhase, 400000U);
  o.hardWorkCeiling = 600000;
  r = a::Run(o, f.request, features, {}, {}, a::Clock::now());
  EXPECT_EQ(r.allowance, 600000U);
  EXPECT_EQ(r.standardFirstPhase, 0U);
}
TEST(AdaptiveController, WorkerAdmissionUsesSelectedEngineWorkingSet) {
  weather_routing::RoutingEngineSettings settings;
  settings.engine = RoutingEngine::Auto;
  settings.autoEngines = weather_routing::EngineBit(RoutingEngine::Quick2);
  EXPECT_EQ(a::EstimatedWorkerMiB(settings), 256);
  settings.autoEngines |= weather_routing::EngineBit(RoutingEngine::Main);
  EXPECT_EQ(a::EstimatedWorkerMiB(settings), 1664);
  settings.engine = RoutingEngine::Original;
  settings.original.memoryBudgetMiB = 1024;
  EXPECT_EQ(a::EstimatedWorkerMiB(settings), 1152);
}
TEST(AdaptiveController,
     CurrentVectorsChooseHourlyWithoutClaimingSpatialCoverage) {
  auto tidal = quick2_fixtures::make("tidal");
  tidal.request.options.timeStep = wr::Duration{21600};
  auto f = a::Characterize(tidal.request, tidal.environment,
                           *tidal.environment.landAndBoundaries);
  ASSERT_TRUE(f.currentAtStart);
  EXPECT_TRUE(f.rapidCurrent);
  EXPECT_GT(f.currentTemporalPairs, 0U);
  EXPECT_GT(f.currentKnots / 1.9438444924406, 0.5);  // m/s
  EXPECT_FALSE(f.currentSpatialCoverageBounded);
  a::StepSchedule schedule(f, tidal.request);
  EXPECT_EQ(schedule.cap(tidal.request.start), wr::Duration{3600});
  a::Options options;
  auto recipes = a::InitialRecipes(options, tidal.request, f, schedule);
  ASSERT_FALSE(recipes.empty());
  EXPECT_LE(recipes.front().step, wr::Duration{3600});
  auto ocean = quick2_fixtures::make("ocean");
  ocean.request.environment.useCurrent = true;
  f = a::Characterize(ocean.request, ocean.environment,
                      *ocean.environment.landAndBoundaries);
  EXPECT_FALSE(f.rapidCurrent);
  EXPECT_FALSE(
      f.currentSpatialCoverageBounded);  // Sparse calm samples are not a bound.
  a::StepSchedule quiet(f, ocean.request);
  EXPECT_EQ(quiet.cap(ocean.request.start), wr::Duration{21600});
}
TEST(AdaptiveController,
     FallingMemoryHeadroomRetainsAcceptedRouteAndSkipsComparisons) {
  auto f = quick2_fixtures::make("short");
  a::Features features;
  features.windAtStart = features.offshore = true;
  features.directNm = 1000;
  features.hours = 100;
  a::Options options;
  options.nativeAllowance = 1000000;
  options.objective = a::Objective::BestCompleted;
  options.memory.reserveMiB = 512;
  options.availableMemoryMiB = [] { return 400; };
  unsigned calls = 0;
  const auto r =
      a::Run(options, f.request, features,
             [&](const auto& request, const auto&, auto, auto, const auto&) {
               ++calls;
               return a::AttemptResult{Good(request, 100), 100};
             });
  EXPECT_EQ(calls, 1U);
  EXPECT_TRUE(weather_routing::native::AcceptedRoute(r.route));
}
TEST(AdaptiveController, NativeStopTranslationIsNarrowAndExcludesCancellation) {
  wr::RoutingResult r;
  r.status = wr::RoutingStatus::ResourceLimitReached;
  r.message = "candidate validation allowance reached";
  r.diagnostics.stageStopReasons = {"prefix repair failed at leg 11",
                                    "prefix repair failed at leg 11"};
  EXPECT_EQ(a::NativeStopCause(r), a::StopCause::RepeatedChronologicalRepair);
  r.status = wr::RoutingStatus::Cancelled;
  EXPECT_EQ(a::NativeStopCause(r), a::StopCause::Other);
  r.status = wr::RoutingStatus::ResourceLimitReached;
  r.message = "retained lineage limit";
  EXPECT_EQ(a::NativeStopCause(r), a::StopCause::Other);
}
TEST(AdaptiveVariants, EmptyAndHourlyScheduleMatchNativeQuick2) {
  for (const auto* name : {"short", "island", "tidal"}) {
    SCOPED_TRACE(name);
    auto f = quick2_fixtures::make(name);
    quick2_routing::Options native;
    native.step = wr::Duration{3600};
    quick2_adaptive::Options adaptive;
    adaptive.step = native.step;
    auto x = quick2_routing::Engine{}.route(f.request, f.environment, native);
    SameTrack(x.route, quick2_adaptive::Engine{}
                           .route(f.request, f.environment, adaptive)
                           .route);
    adaptive.step = wr::Duration{21600};
    adaptive.localStepCap = [](auto) { return wr::Duration{3600}; };
    SameTrack(x.route, quick2_adaptive::Engine{}
                           .route(f.request, f.environment, adaptive)
                           .route);
  }
}
TEST(AdaptiveVariants, DefaultRecoverySplitMatchesNativeStandard) {
  auto f = quick2_fixtures::make("short");
  wr::QuickRoutingOptions native;
  native.headingStepDegrees = 10;
  wr::AdaptiveStandardOptions adaptive;
  adaptive.headingStepDegrees = 10;
  SameTrack(
      wr::QuickRoutingEngine{}.route(f.request, f.environment, native).route,
      wr::AdaptiveStandardEngine{}
          .route(f.request, f.environment, adaptive)
          .route);
}
}  // namespace
