// SPDX-License-Identifier: GPL-3.0-or-later
#include <gtest/gtest.h>
#include <map>
#include "engine/native/RoutingEngineSequence.h"
#include "original_routing/Engine.h"
#include "supercpn/weather_routing/Engine.h"
#include "supercpn/weather_routing/QuickEngine.h"
#include "supercpn/weather_routing/AlternativeEngine.h"

namespace {
namespace wr = supercpn::weather_routing;
namespace native = weather_routing::native;
using native::SequenceEngine;
using weather_routing::RoutingEngine;

wr::RoutingResult Success(int seconds, bool validated = true) {
  wr::RoutingResult result;
  result.status = wr::RoutingStatus::Complete;
  result.validation.passed = validated;
  wr::RouteLeg leg;
  leg.startTime = wr::TimePoint{wr::Duration{1000}};
  leg.endTime = leg.startTime + wr::Duration{seconds};
  result.legs.push_back(leg);
  result.metrics.elapsed = wr::Duration{seconds};
  return result;
}
wr::RoutingResult Failure(
    wr::RoutingStatus status = wr::RoutingStatus::NoFeasibleRoute) {
  wr::RoutingResult result;
  result.status = status;
  result.message = "fixture failure";
  return result;
}

TEST(RoutingEngineSequence, AutoStopsAtQuickSuccess) {
  std::vector<SequenceEngine> calls;
  const auto result = native::RunRoutingEngineSequence(
      RoutingEngine::Auto, {}, [&](auto engine) {
        calls.push_back(engine);
        return Success(100);
      });
  EXPECT_EQ(calls, (std::vector{SequenceEngine::Original}));
  EXPECT_EQ(result.engineIdentity, "original");
}
TEST(RoutingEngineSequence, AutoFallsBackOnMissingWavesAndStopsAtStandard) {
  std::vector<SequenceEngine> calls;
  const auto result = native::RunRoutingEngineSequence(
      RoutingEngine::Auto, {}, [&](auto engine) {
        calls.push_back(engine);
        return engine == SequenceEngine::Original
                   ? Failure(wr::RoutingStatus::WaveDataRequired)
                   : Success(100);
      });
  EXPECT_EQ(calls,
            (std::vector{SequenceEngine::Original, SequenceEngine::Quick}));
  EXPECT_EQ(result.engineIdentity, "quick");
}
TEST(RoutingEngineSequence,
     AutoRetriesRejectedCompleteRoutesAndNeverCallsAlternative) {
  std::vector<SequenceEngine> calls;
  const auto result = native::RunRoutingEngineSequence(
      RoutingEngine::Auto, {}, [&](auto engine) {
        calls.push_back(engine);
        return engine == SequenceEngine::Main ? Success(300)
                                              : Success(100, false);
      });
  EXPECT_EQ(calls, (std::vector{SequenceEngine::Original, SequenceEngine::Quick,
                                SequenceEngine::Main}));
  EXPECT_EQ(result.engineIdentity, "main");
  EXPECT_TRUE(result.validation.passed);
}
TEST(RoutingEngineSequence, AllRunsFourAndChoosesEarliestValidatedArrival) {
  std::vector<SequenceEngine> calls;
  const auto result = native::RunRoutingEngineSequence(
      RoutingEngine::All, {}, [&](auto engine) {
        calls.push_back(engine);
        if (engine == SequenceEngine::Original) return Success(100, false);
        if (engine == SequenceEngine::Quick) return Success(300);
        if (engine == SequenceEngine::Alternative) return Success(200);
        return Failure(wr::RoutingStatus::ResourceLimitReached);
      });
  EXPECT_EQ(calls,
            (std::vector{SequenceEngine::Original, SequenceEngine::Quick,
                         SequenceEngine::Alternative, SequenceEngine::Main}));
  EXPECT_EQ(result.engineIdentity, "alternative");
  EXPECT_EQ(result.metrics.elapsed, wr::Duration{200});
}
TEST(RoutingEngineSequence,
     AllComparesArrivalTimesRatherThanUntrustedSummaryMetrics) {
  const auto result =
      native::RunRoutingEngineSequence(RoutingEngine::All, {}, [](auto engine) {
        auto result = Success(engine == SequenceEngine::Main ? 100 : 200);
        result.metrics.elapsed =
            wr::Duration{engine == SequenceEngine::Original ? 1 : 999};
        return result;
      });
  EXPECT_EQ(result.engineIdentity, "main");
}
TEST(RoutingEngineSequence, EqualArrivalsPreferEarlierEngineDeterministically) {
  const auto result = native::RunRoutingEngineSequence(
      RoutingEngine::All, {}, [](auto) { return Success(100); });
  EXPECT_EQ(result.engineIdentity, "original");
}
TEST(RoutingEngineSequence, CompleteWithoutLegsIsRejected) {
  const auto result =
      native::RunRoutingEngineSequence(RoutingEngine::All, {}, [](auto) {
        auto result = Success(100);
        result.legs.clear();
        return result;
      });
  EXPECT_EQ(result.status, wr::RoutingStatus::ValidationFailure);
}
TEST(RoutingEngineSequence,
     CancellationDiscardsEarlierSuccessAndStopsSequence) {
  wr::CancellationToken token;
  std::vector<SequenceEngine> calls;
  const auto result = native::RunRoutingEngineSequence(
      RoutingEngine::All, token, [&](auto engine) {
        calls.push_back(engine);
        if (engine == SequenceEngine::Quick) token.cancel();
        return Success(100);
      });
  EXPECT_EQ(calls.size(), 2u);
  EXPECT_EQ(result.status, wr::RoutingStatus::Cancelled);
}
TEST(RoutingEngineSequence, AlreadyCancelledDoesNotStartAnyEngine) {
  wr::CancellationToken token;
  token.cancel();
  int calls = 0;
  const auto result =
      native::RunRoutingEngineSequence(RoutingEngine::Auto, token, [&](auto) {
        ++calls;
        return Success(100);
      });
  EXPECT_EQ(calls, 0);
  EXPECT_EQ(result.status, wr::RoutingStatus::Cancelled);
}
TEST(RoutingEngineSequence, FailedSequenceReportsAllAttemptReasons) {
  const auto result = native::RunRoutingEngineSequence(
      RoutingEngine::Auto, {}, [](auto) { return Failure(); });
  EXPECT_NE(result.message.find("Quick:"), std::string::npos);
  EXPECT_NE(result.message.find("Standard:"), std::string::npos);
  EXPECT_NE(result.message.find("Professional:"), std::string::npos);
  EXPECT_EQ(result.message.find("Alternative:"), std::string::npos);
}
TEST(RoutingEngineSequence,
     AutoProfessionalCeilingDoesNotOverwriteSavedEffort) {
  struct Configuration {
    weather_routing::RoutingEngineSettings EngineSettings;
    int RoutingEffortPercent{150};
  } base;
  base.EngineSettings.engine = RoutingEngine::Auto;
  const auto professional =
      native::ConfigurationForSequenceEngine(base, SequenceEngine::Main);
  EXPECT_EQ(professional.RoutingEffortPercent, 400);
  EXPECT_EQ(professional.EngineSettings.engine, RoutingEngine::Main);
  EXPECT_EQ(base.RoutingEffortPercent, 150);
  EXPECT_EQ(base.EngineSettings.engine, RoutingEngine::Auto);
  base.EngineSettings.engine = RoutingEngine::All;
  EXPECT_EQ(native::ConfigurationForSequenceEngine(base, SequenceEngine::Main)
                .RoutingEffortPercent,
            150);
}

// Real solvers exercise their shared chronological validator, not just the
// selector's callback contract. A short, uniform open-water passage is bounded.
TEST(RoutingEngineSequenceIntegration,
     FourRealEnginesReturnEarliestValidatedRoute) {
  wr::RoutingRequest request;
  request.start = {60, 0};
  request.destination = {60, 0.1};
  request.departure = wr::TimePoint{wr::Duration{1784419200}};
  wr::PerformanceProfile profile;
  profile.identity = "sequence-fixture";
  profile.rows = {{5, {{0, 0}, {35, 4}, {90, 6}, {180, 5}}},
                  {20, {{0, 0}, {35, 4}, {90, 6}, {180, 5}}}};
  request.vessel.profiles = {profile};
  request.environment.useCurrent = false;
  request.options.timeStep = std::chrono::minutes{15};
  request.options.minimumTimeStep = std::chrono::minutes{5};
  request.options.headingStepDegrees = 10;
  request.options.maximumSearchAngleDegrees = 180;
  request.limits.maximumRouteDuration = std::chrono::hours{6};
  request.limits.maximumGeneratedStates = 100000;
  request.limits.maximumRetainedStates = 20000;
  wr::UniformWeatherProvider::Configuration weather;
  weather.source = wr::EnvironmentalSource::SyntheticTestField;
  weather.windTowardKnots = wr::speedDirectionToVector(12, 180);
  weather.begins = request.departure;
  weather.ends = request.departure + std::chrono::hours{12};
  wr::WaveSample wave;
  wave.available = true;
  wave.significantHeightMetres = 0.5;
  wave.periodSeconds = 8;
  weather.wave = wave;
  wr::RoutingEnvironment environment;
  environment.grib = std::make_shared<wr::UniformWeatherProvider>(weather);
  environment.landAndBoundaries = std::make_shared<wr::OpenWaterProvider>();
  environment.performance = std::make_shared<wr::PolarPerformanceModel>(request.vessel);
  std::map<SequenceEngine, wr::RoutingResult> candidates;
  const auto result = native::RunRoutingEngineSequence(
      RoutingEngine::All, request.cancellation, [&](auto engine) {
        wr::RoutingResult candidate;
        if (engine == SequenceEngine::Original)
          candidate = original_routing::Engine{}.route(request, environment);
        else if (engine == SequenceEngine::Quick)
          candidate =
              wr::QuickRoutingEngine{}.route(request, environment).route;
        else if (engine == SequenceEngine::Alternative)
          candidate =
              wr::AlternativeRoutingEngine{}.route(request, environment).route;
        else
          candidate = wr::RoutingEngine{}.route(request, environment);
        candidates.emplace(engine, candidate);
        return candidate;
      });
  ASSERT_EQ(candidates.size(), 4u);
  ASSERT_TRUE(native::AcceptedRoute(result)) << result.message;
  for (const auto& [engine, candidate] : candidates) {
    ASSERT_TRUE(native::AcceptedRoute(candidate))
        << native::SequenceEngineTitle(engine) << ": "
        << wr::toString(candidate.status) << " legs=" << candidate.legs.size()
        << " validation=" << candidate.validation.passed << " "
        << candidate.message;
    EXPECT_LE(result.legs.back().endTime, candidate.legs.back().endTime);
  }
}
}  // namespace
