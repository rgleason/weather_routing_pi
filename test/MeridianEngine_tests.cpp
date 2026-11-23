// SPDX-License-Identifier: GPL-3.0-or-later
#include "Quick2Fixtures.h"
#include "engine/meridian/Engine.h"
#include "engine/meridian/StepPolicy.h"
#include "engine/meridian/GeographyGuide.h"
#include "engine/meridian/StateKey.h"
#include <gtest/gtest.h>

namespace wr = supercpn::weather_routing;
TEST(MeridianEngine, CompleteRoutesRequireIndependentReplayAndExactArrival) {
  for (const auto name : {"short", "open", "island", "thin-island", "dateline", "tidal", "margin", "upwind", "variable", "high-latitude"}) {
    SCOPED_TRACE(name);
    auto f = quick2_fixtures::make(name);
    auto r = meridian_routing::Engine{}.route(f.request, f.environment);
    ASSERT_TRUE(r.validation.passed) << r.message << " remaining=" << r.diagnostics.closestApproachNm;
    ASSERT_FALSE(r.legs.empty());
    EXPECT_LT(wr::distanceNm(r.legs.back().end, f.request.destination), 1e-6);
    EXPECT_EQ(r.engineIdentity, "meridian");
    wr::PolarPerformanceModel performance(f.request.vessel);
    EXPECT_TRUE(wr::RouteValidator{}.validate(f.request, f.environment,
        performance, r.legs).passed);
  }
}
TEST(MeridianEngine, InvalidEndpointsMissingWeatherAndShallowWaterCannotComplete) {
  for (const auto name : {"land-endpoint", "shallow", "missing-wind"}) {
    SCOPED_TRACE(name);
    auto f = quick2_fixtures::make(name);
    const auto r = meridian_routing::Engine{}.route(f.request, f.environment);
    EXPECT_FALSE(r.validation.passed);
    EXPECT_TRUE(r.legs.empty());
  }
}
TEST(MeridianEngine, CancellationAndUnsupportedObjectivesAreExplicit) {
  auto f = quick2_fixtures::make("open");
  f.request.cancellation.cancel();
  EXPECT_EQ(meridian_routing::Engine{}.route(f.request, f.environment).status,
            wr::RoutingStatus::Cancelled);
  f = quick2_fixtures::make("open");
  f.request.objective.kind = wr::ObjectiveKind::FuelAware;
  EXPECT_EQ(meridian_routing::Engine{}.route(f.request, f.environment).status,
            wr::RoutingStatus::InvalidVesselConfiguration);
}
TEST(MeridianEngine, FineRetryReceivesReservedWorkWithoutOvershootingTotal) {
  auto f = quick2_fixtures::make("open");
  meridian_routing::Options options;
  options.maximumProposals = 10;
  std::vector<std::uint64_t> slices;
  options.attemptStarted = [&](unsigned, wr::Duration, wr::Duration, auto allowance) {
    slices.push_back(allowance);
  };
  const auto r = meridian_routing::Engine{}.route(f.request, f.environment, options);
  ASSERT_EQ(slices.size(), 2U);
  EXPECT_EQ(slices[0], 6U);
  EXPECT_EQ(slices[1], 4U);
  ASSERT_EQ(r.engineAttempts.size(), 2U);
  EXPECT_FALSE(r.validation.passed);
  EXPECT_TRUE(r.legs.empty());
  EXPECT_EQ(r.status, wr::RoutingStatus::ResourceLimitReached);
}
TEST(MeridianEngine, ClearanceHintsChooseFineCoastalAndCoarseOffshoreSteps) {
  using Policy = meridian_routing::StepPolicy;
  EXPECT_EQ(Policy::Select(4, 2, wr::Duration{900}, wr::Duration{21600}), wr::Duration{900});
  EXPECT_EQ(Policy::Select(9, 2, wr::Duration{900}, wr::Duration{21600}), wr::Duration{1800});
  EXPECT_EQ(Policy::Select(18, 2, wr::Duration{900}, wr::Duration{21600}), wr::Duration{3600});
  EXPECT_EQ(Policy::Select(80, 2, wr::Duration{900}, wr::Duration{21600}), wr::Duration{21600});
  EXPECT_EQ(Policy::Select(NAN, 2, wr::Duration{300}, wr::Duration{3600}), wr::Duration{300});
}
TEST(MeridianEngine, GeographicGuidanceHasNoArtificialPitBetweenWaypoints) {
  auto f = quick2_fixtures::make("island");
  meridian_routing::GeographyGuide guide(f.request, f.environment.landAndBoundaries.get());
  ASSERT_GT(guide.path().size(), 2U);
  for (std::size_t i = 1; i < guide.path().size(); ++i) {
    const auto a = guide.path()[i - 1], b = guide.path()[i];
    const auto length = wr::distanceNm(a, b);
    const auto middle = wr::destinationPoint(a, wr::initialBearingDegrees(a, b), length / 2);
    EXPECT_NEAR(guide.estimate(middle), guide.estimate(a) - length / 2, .01);
  }
}
TEST(MeridianEngine, FineAndCoarseCellsCannotAliasOrDiscardDifferentWeatherTimes) {
  wr::internal::MotionState a, b;
  a.position = {0, .05};
  b.position = {0, .2};
  EXPECT_NE(meridian_routing::key(a, {}, true), meridian_routing::key(b, {}, false));
  b = a;
  b.time = a.time + wr::Duration{1800};
  EXPECT_NE(meridian_routing::key(a, {}, true), meridian_routing::key(b, {}, true));
}
TEST(MeridianEngine, LatitudeRowKeepsOneLongitudeScale) {
  wr::internal::MotionState a, b;
  a.position = {60.0001, 170.0001};
  b.position = {60.0101, 170.0001};
  const auto first = meridian_routing::key(a, {}, false);
  const auto second = meridian_routing::key(b, {}, false);
  EXPECT_EQ(std::get<1>(first), std::get<1>(second));
  EXPECT_EQ(std::get<2>(first), std::get<2>(second));
}
namespace {
class NarrowingChannel final : public wr::LandAndBoundaryProvider {
  wr::GeoPoint origin_;
  std::pair<double, double> position(wr::GeoPoint p) const {
    const auto distance = wr::distanceNm(origin_, p);
    const auto angle = wr::angularDifferenceDegrees(
        wr::initialBearingDegrees(origin_, p), 320) * std::acos(-1) / 180;
    return {distance * std::cos(angle), distance * std::sin(angle)};
  }
public:
  explicit NarrowingChannel(wr::GeoPoint origin) : origin_(origin) {}
  bool pointForbidden(wr::GeoPoint p) const override {
    const auto [along, side] = position(p);
    return std::abs(side) > (along < 2 ? 4 : .15);
  }
  bool segmentForbidden(wr::GeoPoint a, wr::GeoPoint b, double margin) const override {
    const auto length = wr::distanceNm(a, b);
    const auto bearing = wr::initialBearingDegrees(a, b);
    const auto samples = std::max(1, int(std::ceil(length / .05)));
    for (int i = 0; i <= samples; ++i) {
      const auto [along, side] = position(wr::destinationPoint(a, bearing, length * i / samples));
      if (std::abs(side) + margin > (along < 2 ? 4 : .15)) return true;
    }
    return false;
  }
  double distanceToForbiddenNm(wr::GeoPoint p) const override {
    const auto [along, side] = position(p);
    return std::max(0., (along < 2 ? 4 : .15) - std::abs(side));
  }
  std::string identity() const override { return "narrowing-upwind-channel"; }
};
}
TEST(MeridianEngine, NarrowUpwindArrivalKeepsOriginalDepartureAndRequiresFullReplay) {
  auto f = quick2_fixtures::make("open");
  f.request.destination = wr::destinationPoint(f.request.start, 320, 4);
  f.request.constraints.minimumTrueWindAngleDegrees = 40;
  f.request.options.maximumSearchAngleDegrees = 10;  // Other solvers' pruning cone.
  f.environment.landAndBoundaries = std::make_shared<NarrowingChannel>(f.request.start);
  meridian_routing::Options options;
  options.maximumNodes = 2000;
  f.request.vessel.tackPenalty = wr::Duration{};
  f.request.vessel.gybePenalty = wr::Duration{};
  options.maximumProposals = 100000;
  const auto result = meridian_routing::Engine{}.route(f.request, f.environment, options);
  ASSERT_GE(result.engineAttempts.size(), 1U);
  ASSERT_LE(result.engineAttempts.size(), 2U);
  ASSERT_TRUE(result.validation.passed) << result.message << " remaining=" << result.diagnostics.closestApproachNm;
  ASSERT_FALSE(result.legs.empty());
  EXPECT_EQ(f.request.options.maximumSearchAngleDegrees, 10);
  EXPECT_EQ(result.legs.front().startTime, f.request.departure);
  EXPECT_LT(wr::distanceNm(result.legs.front().start, f.request.start), 1e-6);
  EXPECT_LT(wr::distanceNm(result.legs.back().end, f.request.destination), 1e-6);
  wr::PolarPerformanceModel performance(f.request.vessel);
  EXPECT_TRUE(wr::RouteValidator{}.validate(f.request, f.environment,
                                           performance, result.legs).passed);
}

TEST(MeridianEngine, ShortEndpointDistanceDoesNotRestrictPeninsulaDetour) {
  auto f = quick2_fixtures::make("open");
  f.request.start = {0, -.05};
  f.request.destination = {0, .05};
  std::vector<std::vector<wr::GeoPoint>> rings{
      {{-.35, -.01}, {-.35, .01}, {.35, .01}, {.35, -.01}}};
  f.environment.landAndBoundaries = std::make_shared<wr::PolygonBoundaryProvider>(rings);
  meridian_routing::GeographyGuide guide(f.request, f.environment.landAndBoundaries.get());
  ASSERT_GT(guide.path().size(), 2U);
  const auto result = meridian_routing::Engine{}.route(f.request, f.environment);
  ASSERT_TRUE(result.validation.passed) << result.message;
  ASSERT_FALSE(result.legs.empty());
  EXPECT_GT(result.metrics.distanceNm, 40.);
  wr::PolarPerformanceModel performance(f.request.vessel);
  EXPECT_TRUE(wr::RouteValidator{}.validate(f.request, f.environment,
                                           performance, result.legs).passed);
}

TEST(MeridianEngine, RepeatedTidalSearchKeepsTheSameTimedTrack) {
  auto f = quick2_fixtures::make("tidal");
  const auto a = meridian_routing::Engine{}.route(f.request, f.environment);
  const auto b = meridian_routing::Engine{}.route(f.request, f.environment);
  ASSERT_TRUE(a.validation.passed);
  ASSERT_TRUE(b.validation.passed);
  ASSERT_EQ(a.legs.size(), b.legs.size());
  for (std::size_t i = 0; i < a.legs.size(); ++i) {
    EXPECT_EQ(a.legs[i].startTime, b.legs[i].startTime);
    EXPECT_EQ(a.legs[i].endTime, b.legs[i].endTime);
    EXPECT_LT(wr::distanceNm(a.legs[i].end, b.legs[i].end), 1e-9);
  }
  EXPECT_EQ(a.diagnostics.generatedStates, b.diagnostics.generatedStates);
}
TEST(MeridianEngine, CancellationDuringProgressCannotPublishACompleteRoute) {
  auto f = quick2_fixtures::make("island");
  f.request.progress = [&](const auto&) { f.request.cancellation.cancel(); };
  const auto result = meridian_routing::Engine{}.route(f.request, f.environment);
  EXPECT_EQ(result.status, wr::RoutingStatus::Cancelled);
  EXPECT_FALSE(result.validation.passed);
  EXPECT_TRUE(result.legs.empty());
}
