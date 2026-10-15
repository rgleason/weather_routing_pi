// SPDX-License-Identifier: GPL-3.0-or-later
#include "Quick2Fixtures.h"
#include <gtest/gtest.h>
#include "engine/native/RoutingEngineSequence.h"
#include "supercpn/weather_routing/QuickEngine.h"
#include "supercpn/weather_routing/Comfort.h"
#include <iostream>
#include <stdexcept>
namespace wr = supercpn::weather_routing;
static void require(bool v, const std::string& message) {
  if (!v) throw std::runtime_error(message);
}
class StrictReplay final : public wr::LandAndBoundaryProvider {
public:
  mutable unsigned prepared{};
  bool pointForbidden(wr::GeoPoint) const override { return false; }
  bool segmentForbidden(wr::GeoPoint, wr::GeoPoint, double) const override {
    return false;
  }
  double distanceToForbiddenNm(wr::GeoPoint) const override { return 100; }
  bool validationSegmentFromKnownSafeForbiddenAt(wr::GeoPoint, wr::GeoPoint,
                                                 wr::TimePoint,
                                                 double) const override {
    return true;
  }
  bool validationFailureRequiresSearchEscalation() const override {
    return true;
  }
  void prepareValidationRoute(std::span<const wr::RouteLeg>,
                              double) const override {
    ++prepared;
  }
  std::string identity() const override {
    return "strict-authoritative-replay";
  }
};
TEST(Quick2Engine, MotionLandWeatherCancellationAndReplayContracts) {
  try {
    quick2_routing::Engine engine;
    for (const std::string name :
         {"short", "open", "island", "thin-island", "dateline", "tidal"}) {
      auto f = quick2_fixtures::make(name);
      auto r = engine.route(f.request, f.environment);
      require(r.route.validation.passed, name + ": " + r.route.message);
      require(!r.route.legs.empty(), name + ": no legs");
      require(
          wr::distanceNm(r.route.legs.back().end, f.request.destination) < .05,
          name + ": endpoint");
      wr::PolarPerformanceModel p(f.request.vessel);
      require(wr::RouteValidator{}
                  .validate(f.request, f.environment, p, r.route.legs)
                  .passed,
              name + ": second replay");
    }
    for (const std::string name :
         {"land-endpoint", "shallow", "missing-wind"}) {
      auto f = quick2_fixtures::make(name);
      auto r = engine.route(f.request, f.environment);
      require(!r.route.validation.passed && r.route.legs.empty(),
              name + ": unsafe acceptance");
    }
    auto f = quick2_fixtures::make("open");
    f.request.cancellation.cancel();
    require(engine.route(f.request, f.environment).route.status ==
                wr::RoutingStatus::Cancelled,
            "cancellation");
    f = quick2_fixtures::make("open");
    f.request.progress = [&](const auto&) { f.request.cancellation.cancel(); };
    require(engine.route(f.request, f.environment).route.status ==
                wr::RoutingStatus::Cancelled,
            "mid-search cancellation");
    f = quick2_fixtures::make("short");
    auto strict = std::make_shared<StrictReplay>();
    f.environment.landAndBoundaries = strict;
    auto rejected = engine.route(f.request, f.environment);
    require(!rejected.route.validation.passed && rejected.route.legs.empty() &&
                strict->prepared > 0,
            "Strict authoritative replay bypassed");
    f = quick2_fixtures::make("open");
    quick2_routing::Options tiny;
    tiny.maximumProposals = 1;
    require(engine.route(f.request, f.environment, tiny).route.status ==
                wr::RoutingStatus::ResourceLimitReached,
            "budget");
    tiny.headings = 0;
    require(engine.route(f.request, f.environment, tiny).route.status ==
                wr::RoutingStatus::InvalidVesselConfiguration,
            "invalid policy");
    f.request.objective.kind = wr::ObjectiveKind::FuelAware;
    require(engine.route(f.request, f.environment).route.status ==
                wr::RoutingStatus::InvalidVesselConfiguration,
            "unsupported objective silently ignored");
    std::cout << "PASS: 6 valid routes independently replayed; forbidden "
                 "endpoint, shallow water, missing wind, cancellation, "
                 "resource limits and stricter authoritative replay\n";
  } catch (const std::exception& e) {
    FAIL() << e.what();
  }
}

TEST(Quick2Engine, RetainingCandidateDoesNotChangeStandardsSearchOrAllowance) {
  namespace native = weather_routing::native;
  auto fixture = quick2_fixtures::make("island");
  wr::QuickRoutingOptions options;
  options.maximumGeneratedStates = 200000;
  const auto standalone =
      wr::QuickRoutingEngine{}
          .route(fixture.request, fixture.environment, options)
          .route;
  ASSERT_TRUE(native::AcceptedRoute(standalone));
  wr::RoutingResult following;
  const auto before = fixture.request.limits.maximumGeneratedStates;
  const auto result = native::RunRoutingEngineSequence(
      weather_routing::RoutingEngine::Auto, fixture.request.cancellation,
      [&](auto engine) {
        if (engine == native::SequenceEngine::Quick2)
          return quick2_routing::Engine{}
              .route(fixture.request, fixture.environment)
              .route;
        EXPECT_EQ(engine, native::SequenceEngine::Quick);
        EXPECT_EQ(fixture.request.limits.maximumGeneratedStates, before);
        following = wr::QuickRoutingEngine{}
                        .route(fixture.request, fixture.environment, options)
                        .route;
        return following;
      },
      {},
      weather_routing::EngineBit(weather_routing::RoutingEngine::Quick2) |
          weather_routing::EngineBit(weather_routing::RoutingEngine::Quick));
  ASSERT_TRUE(native::AcceptedRoute(result));
  ASSERT_EQ(following.legs.size(), standalone.legs.size());
  EXPECT_EQ(following.diagnostics.generatedStates,
            standalone.diagnostics.generatedStates);
  EXPECT_EQ(following.diagnostics.weatherSamples,
            standalone.diagnostics.weatherSamples);
  for (std::size_t i = 0; i < standalone.legs.size(); ++i) {
    EXPECT_EQ(following.legs[i].endTime.time_since_epoch().count(),
              standalone.legs[i].endTime.time_since_epoch().count());
    EXPECT_DOUBLE_EQ(following.legs[i].end.latitude,
                     standalone.legs[i].end.latitude);
    EXPECT_DOUBLE_EQ(following.legs[i].end.longitude,
                     standalone.legs[i].end.longitude);
  }
}
TEST(Quick2Engine, FailedAndCancelledAttemptsChargeSpentWork) {
  auto f = quick2_fixtures::make("open");
  quick2_routing::Options options;
  options.maximumProposals = 1000;
  auto result =
      quick2_routing::Engine{}.route(f.request, f.environment, options);
  EXPECT_EQ(result.route.status, wr::RoutingStatus::ResourceLimitReached);
  EXPECT_GT(result.frontier.proposals, 0U);
  EXPECT_EQ(result.route.diagnostics.generatedStates,
            result.frontier.proposals);
  EXPECT_EQ(result.route.diagnostics.retainedStates,
            result.frontier.lineageNodes);
}
TEST(Quick2Engine, PreviewCannotChangeDeliveredRouteOrSearchWork) {
  auto f = quick2_fixtures::make("island");
  const auto ordinary =
      quick2_routing::Engine{}.route(f.request, f.environment);
  unsigned frames = 0;
  f.request.preview = [&](auto frame) {
    ++frames;
    EXPECT_LE(frame->frontier.size(), 256U);
    EXPECT_LE(frame->path.size(), 256U);
    throw std::runtime_error("Optional observer failure");
  };
  const auto observed =
      quick2_routing::Engine{}.route(f.request, f.environment);
  ASSERT_GT(frames, 0U);
  EXPECT_EQ(observed.route.status, ordinary.route.status);
  EXPECT_EQ(observed.frontier.proposals, ordinary.frontier.proposals);
  ASSERT_EQ(observed.route.legs.size(), ordinary.route.legs.size());
  for (std::size_t i = 0; i < ordinary.route.legs.size(); ++i) {
    EXPECT_DOUBLE_EQ(observed.route.legs[i].end.latitude,
                     ordinary.route.legs[i].end.latitude);
    EXPECT_DOUBLE_EQ(observed.route.legs[i].end.longitude,
                     ordinary.route.legs[i].end.longitude);
    EXPECT_EQ(observed.route.legs[i].endTime.time_since_epoch().count(),
              ordinary.route.legs[i].endTime.time_since_epoch().count());
  }
}
TEST(Quick2Engine, ComfortCandidateUsesSharedScorerAndKeepsFastestBaseline) {
  auto f = quick2_fixtures::make("wave-band");
  const auto fastest = quick2_routing::Engine{}.route(f.request, f.environment);
  quick2_routing::Options options;
  options.objective = quick2_routing::Objective::Comfortable;
  options.comfortWindOnly = false;
  const auto comfortable =
      quick2_routing::Engine{}.route(f.request, f.environment, options);
  ASSERT_TRUE(fastest.route.validation.passed);
  ASSERT_TRUE(comfortable.route.validation.passed);
  const auto a = wr::routeDiscomfortSeconds(fastest.route.legs, false);
  const auto b = wr::routeDiscomfortSeconds(comfortable.route.legs, false);
  EXPECT_TRUE(std::isfinite(a));
  EXPECT_TRUE(std::isfinite(b));
  EXPECT_NEAR(comfortable.discomfortHours,
              comfortable.route.metrics.elapsed.count() / 3600.0 + b / 3600.0,
              0.05);
}
