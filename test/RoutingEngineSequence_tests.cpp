// SPDX-License-Identifier: GPL-3.0-or-later
#include <gtest/gtest.h>
#include <map>
#include "engine/native/RoutingEngineSequence.h"
#include "engine/native/RoutingMethodExecution.h"
#include "original_routing/Engine.h"
#include "quick2_routing/Engine.h"
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
TEST(RoutingEngineSequence, CompletionIsSkippedAfterAutoSuccess) {
  unsigned recoveries=0;
  auto result=native::RunRoutingEngineSequence(RoutingEngine::Auto,{},
      [](auto) { return Success(100); },{},weather_routing::kSelectableEnginesMask,
      [&]() -> std::optional<wr::RoutingResult> { ++recoveries;return Success(50); });
  EXPECT_TRUE(native::AcceptedRoute(result));
  EXPECT_EQ(recoveries,0U);
}
TEST(RoutingEngineSequence, AutoCanCompleteBeforeRunningTheLaterMethods) {
  std::vector<SequenceEngine> calls;
  auto result=native::RunRoutingEngineSequence(RoutingEngine::Auto,{},
      [&](auto engine) { calls.push_back(engine);return Failure(); },{},
      weather_routing::kSelectableEnginesMask,
      []() -> std::optional<wr::RoutingResult> { return Success(200); });
  EXPECT_TRUE(native::AcceptedRoute(result));
  EXPECT_EQ(result.engineIdentity,"quick3");
  EXPECT_EQ(calls,(std::vector{SequenceEngine::Original,SequenceEngine::Quick2}));
  ASSERT_EQ(result.engineAttempts.size(),3U);
  EXPECT_EQ(result.engineAttempts.back().engine,"Quick3 recovery");
}
TEST(RoutingEngineSequence, CancellationAfterCompletionDiscardsRouteAndKeepsAttemptHistory) {
  wr::CancellationToken cancel;
  auto result=native::RunRoutingEngineSequence(RoutingEngine::Auto,cancel,
      [](auto){return Failure();},{},weather_routing::kSelectableEnginesMask,
      [&]() -> std::optional<wr::RoutingResult>{cancel.cancel();return Success(100);});
  EXPECT_EQ(result.status,wr::RoutingStatus::Cancelled);
  EXPECT_TRUE(result.legs.empty());
  EXPECT_EQ(result.engineAttempts.size(),3U);
  EXPECT_EQ(result.attemptFailures.size(),2U);
}
TEST(RoutingEngineSequence, AllRetainsRecoveryAndContinuesItsComparison) {
  unsigned calls=0;
  auto result=native::RunRoutingEngineSequence(RoutingEngine::All,{},
      [&](auto engine) { ++calls;return engine==SequenceEngine::Main?Success(150):Failure(); },{},
      weather_routing::kSelectableEnginesMask,
      []() -> std::optional<wr::RoutingResult> { return Success(200); });
  EXPECT_TRUE(native::AcceptedRoute(result));
  EXPECT_EQ(result.engineIdentity,"main");
  EXPECT_EQ(calls,6U);
  EXPECT_EQ(result.engineAttempts.size(),7U);
}
TEST(RoutingEngineSequence, FailedRecoveryDoesNotHideACompletedAllResult) {
  auto result=native::RunRoutingEngineSequence(RoutingEngine::All,{},
      [](auto engine) { return engine==SequenceEngine::Quick2?Success(150):Failure(); },{},
      weather_routing::kSelectableEnginesMask,
      []() -> std::optional<wr::RoutingResult> { return Failure(); });
  EXPECT_TRUE(native::AcceptedRoute(result));
  EXPECT_EQ(result.engineIdentity,"quick2");
}
TEST(RoutingEngineSequence, AutoStopsAtValidatedQuick2) {
  std::vector<SequenceEngine> calls;
  const auto result = native::RunRoutingEngineSequence(
      RoutingEngine::Auto, {}, [&](auto engine) {
        calls.push_back(engine);
        if (engine == SequenceEngine::Original) return Failure();
        if (engine == SequenceEngine::Quick2) return Success(41000);
        if (engine == SequenceEngine::Quick) return Success(19000);
        ADD_FAILURE() << "Auto should stop after accepted Quick2";
        return Failure();
      });
  EXPECT_EQ(calls, (std::vector{SequenceEngine::Original, SequenceEngine::Quick2}));
  EXPECT_EQ(result.engineIdentity, "quick2");
  EXPECT_EQ(result.metrics.elapsed.count(), 41000);
}
TEST(RoutingEngineSequence, AutoDoesNotRunLaterEnginesAfterQuick2) {
  for (bool standardSucceeds : {false, true}) {
    std::vector<SequenceEngine> calls;
    const auto result = native::RunRoutingEngineSequence(
        RoutingEngine::Auto, {}, [&](auto engine) {
          calls.push_back(engine);
          if (engine == SequenceEngine::Quick2) return Success(100);
          if (engine == SequenceEngine::Quick && standardSucceeds) return Success(200);
          return Failure();
        });
    EXPECT_EQ(result.engineIdentity, "quick2");
    EXPECT_TRUE(native::AcceptedRoute(result));
    EXPECT_EQ(std::find(calls.begin(), calls.end(), SequenceEngine::Quick), calls.end());
    EXPECT_EQ(calls.size(), 2U);
  }
}
TEST(RoutingEngineSequence, MembershipSkipsUncheckedAndCanEnableAlternativeInAuto) {
  std::vector<SequenceEngine> calls;
  const auto mask = weather_routing::EngineBit(RoutingEngine::Quick2) |
                    weather_routing::EngineBit(RoutingEngine::Alternative);
  const auto result = native::RunRoutingEngineSequence(
      RoutingEngine::Auto, {}, [&](auto engine) {
        calls.push_back(engine);
        return Success(engine == SequenceEngine::Quick2 ? 200 : 100);
      }, {}, mask);
  EXPECT_EQ(calls, (std::vector{SequenceEngine::Quick2}));
  EXPECT_EQ(result.engineIdentity, "quick2");
}
TEST(RoutingEngineSequence, EmptyMembershipNeverSilentlyRunsAnEngine) {
  int calls = 0;
  const auto result = native::RunRoutingEngineSequence(
      RoutingEngine::All, {}, [&](auto) { ++calls; return Success(100); }, {}, 0);
  EXPECT_EQ(calls, 0);
  EXPECT_EQ(result.status, wr::RoutingStatus::InvalidVesselConfiguration);
}
TEST(RoutingEngineSequence, ExperimentalAtlasIsOptInAndPrecedesProfessional) {
  EXPECT_EQ(weather_routing::kAutoEnginesMask, 23U);
  EXPECT_EQ(weather_routing::kAllEnginesMask, 31U);
  const auto atlas = weather_routing::EngineBit(RoutingEngine::Atlas);
  ASSERT_EQ(atlas, 32U);
  EXPECT_EQ(atlas & weather_routing::kAutoEnginesMask, 0U);
  EXPECT_EQ(atlas & weather_routing::kAllEnginesMask, 0U);
  std::vector<SequenceEngine> calls;
  const auto result = native::RunRoutingEngineSequence(
      RoutingEngine::Auto, {}, [&](auto engine) {
        calls.push_back(engine);
        return Success(100);
      }, {}, atlas | weather_routing::EngineBit(RoutingEngine::Main));
  EXPECT_EQ(calls, (std::vector{SequenceEngine::Atlas}));
  EXPECT_EQ(result.engineIdentity, "atlas");
}
TEST(RoutingEngineSequence, AllCanSelectAtlasForQualityAndRejectItsInvalidRoute) {
  for (bool validated : {false, true}) {
    std::vector<SequenceEngine> calls;
    const auto result = native::RunRoutingEngineSequence(
        RoutingEngine::All, {}, [&](auto engine) {
          calls.push_back(engine);
          return Success(engine == SequenceEngine::Atlas ? 90 : 100,
                         engine != SequenceEngine::Atlas || validated);
        }, {}, weather_routing::kSelectableEnginesMask);
    EXPECT_EQ(calls.size(), 6U);
    EXPECT_EQ(calls, (std::vector{SequenceEngine::Original, SequenceEngine::Quick2,
        SequenceEngine::Atlas, SequenceEngine::Quick, SequenceEngine::Alternative, SequenceEngine::Main}));
    EXPECT_EQ(calls[5], SequenceEngine::Main);
    EXPECT_EQ(result.engineIdentity, validated ? "atlas" : "original");
  }
}
TEST(RoutingEngineSequence, RejectedAtlasInAutoFallsBackToProfessional) {
  std::vector<SequenceEngine> calls;
  const auto result = native::RunRoutingEngineSequence(
      RoutingEngine::Auto, {}, [&](auto engine) {
        calls.push_back(engine);
        return Success(100, engine != SequenceEngine::Atlas);
      }, {}, weather_routing::EngineBit(RoutingEngine::Atlas) |
             weather_routing::EngineBit(RoutingEngine::Main));
  EXPECT_EQ(calls, (std::vector{SequenceEngine::Atlas, SequenceEngine::Main}));
  EXPECT_EQ(result.engineIdentity, "main");
}
TEST(RoutingEngineSequence, AutoStopsAtAtlasAfterEarlierEnginesFail) {
  std::vector<SequenceEngine> calls;
  const auto result = native::RunRoutingEngineSequence(
      RoutingEngine::Auto, {}, [&](auto engine) {
        calls.push_back(engine);
        if (engine == SequenceEngine::Original || engine == SequenceEngine::Quick2)
          return Failure(wr::RoutingStatus::SearchIncomplete);
        return Success(engine == SequenceEngine::Atlas ? 90 : 100);
      }, {}, weather_routing::kSelectableEnginesMask);
  EXPECT_EQ(calls, (std::vector{SequenceEngine::Original,
      SequenceEngine::Quick2, SequenceEngine::Atlas}));
  EXPECT_EQ(result.engineIdentity, "atlas");
}
TEST(RoutingEngineSequence, AutoFallsBackOnMissingWavesAndStopsAtStandard) {
  std::vector<SequenceEngine> calls;
  const auto result = native::RunRoutingEngineSequence(
      RoutingEngine::Auto, {}, [&](auto engine) {
        calls.push_back(engine);
        return engine == SequenceEngine::Original || engine == SequenceEngine::Quick2
                   ? Failure(wr::RoutingStatus::WaveDataRequired)
                   : Success(engine == SequenceEngine::Quick2 ? 200 : 100);
      });
  EXPECT_EQ(calls,
            (std::vector{SequenceEngine::Original, SequenceEngine::Quick2, SequenceEngine::Quick}));
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
  EXPECT_EQ(calls, (std::vector{SequenceEngine::Original, SequenceEngine::Quick2, SequenceEngine::Quick,
                                SequenceEngine::Main}));
  EXPECT_EQ(result.engineIdentity, "main");
  EXPECT_TRUE(result.validation.passed);
}
TEST(RoutingEngineSequence, AllRunsFiveAndChoosesEarliestValidatedArrival) {
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
            (std::vector{SequenceEngine::Original, SequenceEngine::Quick2, SequenceEngine::Quick,
                         SequenceEngine::Alternative, SequenceEngine::Main}));
  EXPECT_EQ(result.engineIdentity, "alternative");
  EXPECT_EQ(result.metrics.elapsed.count(), 200);
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
  EXPECT_EQ(calls.size(), 3u);
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
TEST(RoutingEngineSequence, CancellationBetweenMethodsPreservesKnownWork) {
  wr::CancellationToken token;
  const auto result = native::RunRoutingEngineSequence(
      RoutingEngine::All, token, [](auto) {
        auto route = Success(100);
        route.diagnostics.generatedStates = 123;
        route.diagnostics.retainedStates = 45;
        route.diagnostics.landChecks = 678;
        route.diagnostics.validationSamples = 9;
        return route;
      }, [&](auto, const auto&) { token.cancel(); });
  EXPECT_EQ(result.status, wr::RoutingStatus::Cancelled);
  EXPECT_TRUE(result.legs.empty());
  EXPECT_EQ(result.diagnostics.generatedStates, 123U);
  EXPECT_EQ(result.diagnostics.retainedStates, 45U);
  EXPECT_EQ(result.diagnostics.landChecks, 678U);
  EXPECT_EQ(result.diagnostics.validationSamples, 9U);
  ASSERT_EQ(result.engineAttempts.size(), 1U);
  EXPECT_NE(result.engineAttempts[0].detail.find("Elapsed wall time"), std::string::npos);
}
TEST(RoutingEngineSequence, CancelledMethodIsIncludedWithoutErasingPriorCounts) {
  const auto result = native::RunRoutingEngineSequence(
      RoutingEngine::Auto, {}, [](auto engine) {
        auto route = Failure(engine == SequenceEngine::Quick2
            ? wr::RoutingStatus::Cancelled : wr::RoutingStatus::ResourceLimitReached);
        route.diagnostics.generatedStates = engine == SequenceEngine::Original ? 100 : 20;
        route.diagnostics.retainedStates = engine == SequenceEngine::Original ? 15 : 10;
        return route;
      });
  EXPECT_EQ(result.status, wr::RoutingStatus::Cancelled);
  EXPECT_EQ(result.diagnostics.generatedStates, 120U);
  EXPECT_EQ(result.diagnostics.retainedStates, 15U); // Peak returned count, not a sum.
  ASSERT_EQ(result.engineAttempts.size(), 2U);
  EXPECT_EQ(result.engineAttempts.back().status, wr::RoutingStatus::Cancelled);
  ASSERT_FALSE(result.diagnostics.stageStopReasons.empty());
  EXPECT_NE(result.diagnostics.stageStopReasons.back().find("generated states 120"), std::string::npos);
}
TEST(RoutingEngineSequence, AllAccountingDoesNotChangeWinningRouteCounters) {
  const auto result = native::RunRoutingEngineSequence(
      RoutingEngine::All, {}, [](auto engine) {
        auto route = Success(engine == SequenceEngine::Original ? 100 : 200);
        route.diagnostics.generatedStates = engine == SequenceEngine::Original ? 10 : 20;
        return route;
      }, {}, weather_routing::EngineBit(RoutingEngine::Original) |
             weather_routing::EngineBit(RoutingEngine::Quick));
  EXPECT_EQ(result.engineIdentity, "original");
  EXPECT_EQ(result.metrics.elapsed.count(), 100);
  EXPECT_EQ(result.diagnostics.generatedStates, 10U);
  ASSERT_EQ(result.engineAttempts.size(), 2U);
  EXPECT_NE(result.diagnostics.stageStopReasons.back().find("generated states 30"), std::string::npos);
}
TEST(RoutingEngineSequence, FinalCancellationClearsRouteButKeepsDiagnostics) {
  auto result = Success(100);
  result.diagnostics.generatedStates = 123;
  result.diagnostics.stageStopReasons = {"Earlier method work was retained"};
  weather_routing::CancelRoutingResultKeepingDiagnostics(result);
  EXPECT_EQ(result.status, wr::RoutingStatus::Cancelled);
  EXPECT_TRUE(result.legs.empty());
  EXPECT_FALSE(result.validation.passed);
  EXPECT_EQ(result.metrics.elapsed.count(), 0);
  EXPECT_EQ(result.diagnostics.generatedStates, 123U);
  EXPECT_EQ(result.diagnostics.stageStopReasons,
            std::vector<std::string>{"Earlier method work was retained"});
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
     FiveRealEnginesReturnEarliestValidatedRoute) {
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
        else if (engine == SequenceEngine::Quick2)
          candidate = quick2_routing::Engine{}.route(request, environment).route;
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
  ASSERT_EQ(candidates.size(), 5u);
  ASSERT_TRUE(native::AcceptedRoute(result)) << result.message;
  for (const auto& [engine, candidate] : candidates) {
    ASSERT_TRUE(native::AcceptedRoute(candidate))
        << native::SequenceEngineTitle(engine) << ": "
        << wr::toString(candidate.status) << " legs=" << candidate.legs.size()
        << " validation=" << candidate.validation.passed << " "
        << candidate.message;
    // GoogleTest's chrono formatting uses newer macOS runtime APIs.
    // Comparing the integer ticks preserves the check on the macOS 11 target.
    EXPECT_LE(result.legs.back().endTime.time_since_epoch().count(),
              candidate.legs.back().endTime.time_since_epoch().count());
  }
}
}  // namespace
TEST(RoutingEngineSequence, AllRetainsEveryAttemptIncludingFailuresWithSuccessfulWinner) {
  const auto enabled = weather_routing::EngineBit(RoutingEngine::Original) |
      weather_routing::EngineBit(RoutingEngine::Quick2) |
      weather_routing::EngineBit(RoutingEngine::Quick);
  const auto result = native::RunRoutingEngineSequence(
      RoutingEngine::All, {}, [](auto engine) {
        return engine == SequenceEngine::Original
            ? Failure(wr::RoutingStatus::CurrentDataRequired)
            : Success(engine == SequenceEngine::Quick2 ? 100 : 200);
      }, {}, enabled);
  EXPECT_EQ(result.engineIdentity, "quick2");
  ASSERT_EQ(result.engineAttempts.size(), 3U);
  EXPECT_EQ(result.engineAttempts.front().engine, "Quick");
  EXPECT_EQ(result.engineAttempts.back().engine, "Standard");
  EXPECT_EQ(result.engineAttempts.back().status, wr::RoutingStatus::Complete);
  ASSERT_EQ(result.attemptFailures.size(), 1U);
  EXPECT_EQ(result.attemptFailures.front().status, wr::RoutingStatus::CurrentDataRequired);
}
TEST(RoutingEngineSequence, AutoWinnerRecordsOnlyFailuresBeforeItsSuccess) {
  const auto result = native::RunRoutingEngineSequence(
      RoutingEngine::Auto, {}, [](auto engine) {
        return engine == SequenceEngine::Quick2 ? Success(100) : Failure();
      });
  EXPECT_EQ(result.engineIdentity, "quick2");
  EXPECT_EQ(result.engineAttempts.size(), 2U);
  EXPECT_EQ(result.attemptFailures.size(), 1U);
}
TEST(RoutingEngineSequence, RejectedCompleteCandidateIsRecordedAsValidationFailure) {
  const auto mask = weather_routing::EngineBit(RoutingEngine::Original);
  const auto result = native::RunRoutingEngineSequence(
      RoutingEngine::All, {}, [](auto) { return Success(100, false); }, {}, mask);
  EXPECT_EQ(result.status, wr::RoutingStatus::ValidationFailure);
  ASSERT_EQ(result.engineAttempts.size(), 1U);
  EXPECT_EQ(result.engineAttempts[0].status, wr::RoutingStatus::ValidationFailure);
  ASSERT_EQ(result.attemptFailures.size(), 1U);
  EXPECT_EQ(result.attemptFailures[0].status, wr::RoutingStatus::ValidationFailure);
}

TEST(RoutingEngineSequence, FailureHeadlineSummarisesEveryAttemptHonestly) {
  const auto result = native::RunRoutingEngineSequence(RoutingEngine::Auto, {}, [](auto engine) {
    auto result = Failure(engine == SequenceEngine::Original ? wr::RoutingStatus::InvalidStart
        : engine == SequenceEngine::Quick ? wr::RoutingStatus::SearchIncomplete
        : wr::RoutingStatus::ResourceLimitReached);
    result.message = engine == SequenceEngine::Quick
        ? "Quick search did not find a validated route within its allowance; try the main engine"
        : "fixture reason";
    return result;
  });
  EXPECT_EQ(result.message.find("No validated route found with the selected settings."), 0U);
  EXPECT_EQ(result.attemptFailures.size(), 4U);
  EXPECT_EQ(result.attemptFailures[2].engine, "Standard");
  EXPECT_EQ(result.attemptFailures[3].status, wr::RoutingStatus::ResourceLimitReached);
  EXPECT_EQ(result.message.find("try the main engine"), std::string::npos);
  EXPECT_NE(result.message.find("invalid_start"), std::string::npos);
  EXPECT_NE(result.message.find("resource_limit"), std::string::npos);
}

TEST(RoutingMethodExecution, DeparturesAndMethodsShareSlots) {
  weather_routing::RoutingWorkerBudget budget;
  budget.SetLimit(2);
  auto departure = budget.TryAcquire();
  auto method = budget.TryAcquire();
  ASSERT_TRUE(departure);
  ASSERT_TRUE(method);
  EXPECT_FALSE(budget.TryAcquire());
  budget.SetLimit(1);
  method.reset();
  EXPECT_FALSE(budget.TryAcquire());
  departure.reset();
  EXPECT_TRUE(budget.TryAcquire());
}

TEST(RoutingMethodExecution, ParallelAllPreservesStableComparisonAndBudgets) {
  using namespace std::chrono_literals;
  weather_routing::RoutingWorkerBudget budget;
  budget.SetLimit(2);
  auto departure = budget.TryAcquire();
  std::promise<void> quickStarted, quick2Started;
  auto quickReady = quickStarted.get_future();
  auto quick2Ready = quick2Started.get_future();
  std::array<std::atomic<unsigned>, 6> calls{};
  const unsigned selected = weather_routing::kSelectableEnginesMask;
  native::ParallelAllMethods batch(selected, 6, [&](auto engine, const auto&, const auto&) {
    ++calls[static_cast<unsigned>(engine)];
    if (engine == SequenceEngine::Original) {
      quickStarted.set_value();
      EXPECT_EQ(quick2Ready.wait_for(2s), std::future_status::ready);
    } else if (engine == SequenceEngine::Quick2) {
      quick2Started.set_value();
      EXPECT_EQ(quickReady.wait_for(2s), std::future_status::ready);
    }
    return Success(100);
  }, [&] { return budget.TryAcquire(); }, [] {}, {});
  EXPECT_EQ(batch.LaneCount(), 2u);
  const auto result = native::RunRoutingEngineSequence(
      RoutingEngine::All, {}, [&](auto engine) { return batch.Take(engine); }, {}, selected);
  EXPECT_EQ(result.engineIdentity, "original");
  EXPECT_EQ(result.engineAttempts.size(), 6u);
  for (const auto& count : calls) EXPECT_EQ(count.load(), 1u);
}

TEST(RoutingMethodExecution, StaggeredAutoPublishesThenRetainsImprovement) {
  using namespace std::chrono_literals;
  weather_routing::RoutingWorkerBudget budget;
  auto slot = budget.TryAcquire();
  ASSERT_TRUE(slot);
  std::promise<void> published;
  auto firstPublication = published.get_future();
  std::atomic_bool keep{false};
  std::vector<int> deliveries;
  auto results = native::RunStaggeredFastMethods([&](auto engine, const auto&, const auto& hedge) {
    if (engine == SequenceEngine::Original) {
      hedge();
      EXPECT_EQ(firstPublication.wait_for(2s), std::future_status::ready);
      return Success(80);
    }
    return Success(100);
  }, std::move(*slot), {}, keep, [] {}, [&](const auto& result) {
    deliveries.push_back(result.metrics.elapsed.count());
    if (deliveries.size() == 1) published.set_value();
  }, 1h, 5s);
  ASSERT_TRUE(results.competed());
  EXPECT_EQ(deliveries, (std::vector<int>{100, 80}));
  const auto best = native::RunRoutingEngineSequence(RoutingEngine::Auto, {},
      [&](auto engine) {
        return engine == SequenceEngine::Original ? *results.quick : *results.quick2;
      }, {}, weather_routing::EngineBit(RoutingEngine::Original) |
             weather_routing::EngineBit(RoutingEngine::Quick2), {}, true);
  EXPECT_EQ(best.engineIdentity, "original");
  EXPECT_EQ(best.metrics.elapsed.count(), 80);
}

TEST(RoutingMethodExecution, AutoLoserCancellationDoesNotCancelValidatedRoute) {
  using namespace std::chrono_literals;
  weather_routing::RoutingWorkerBudget budget;
  auto slot = budget.TryAcquire();
  ASSERT_TRUE(slot);
  std::atomic_bool keep{false};
  auto results = native::RunStaggeredFastMethods([&](auto engine, const auto& stop, const auto& hedge) {
    if (engine == SequenceEngine::Original) {
      hedge();
      const auto deadline = native::MethodClock::now() + 2s;
      while (!stop->load() && native::MethodClock::now() < deadline)
        std::this_thread::sleep_for(1ms);
      EXPECT_TRUE(stop->load());
      return Failure(wr::RoutingStatus::Cancelled);
    }
    return Success(100);
  }, std::move(*slot), {}, keep, [] {}, [](const auto&) {}, 1h, 5ms);
  ASSERT_TRUE(results.competed());
  const auto best = native::RunRoutingEngineSequence(RoutingEngine::Auto, {},
      [&](auto engine) {
        return engine == SequenceEngine::Original ? *results.quick : *results.quick2;
      }, {}, weather_routing::EngineBit(RoutingEngine::Original) |
             weather_routing::EngineBit(RoutingEngine::Quick2), {}, true);
  EXPECT_EQ(best.engineIdentity, "quick2");
  EXPECT_TRUE(native::AcceptedRoute(best));
  EXPECT_TRUE(best.attemptFailures.empty());
}
