// SPDX-License-Identifier: GPL-3.0-or-later
#include "Quick2Fixtures.h"
#include "../src/engine/corridor/ArrivalGuide.h"
#include "../src/engine/corridor/ConnectedWaterGuide.h"
#include "engine/corridor/Engine.h"

#include <gtest/gtest.h>

namespace wr = supercpn::weather_routing;

TEST(CorridorEngine, ArrivalGuideFavoursTheClearApproach) {
  auto fixture = quick2_fixtures::make("open");
  const auto target = fixture.request.destination;
  const auto at = [&](double bearing, double distance) {
    return wr::destinationPoint(target, bearing, distance);
  };
  const auto nearEdge = at(0, 4), farEdge = at(0, 8);
  const wr::PolygonBoundaryProvider land({{
      wr::destinationPoint(nearEdge, 90, 4), wr::destinationPoint(farEdge, 90, 4),
      wr::destinationPoint(farEdge, 270, 4), wr::destinationPoint(nearEdge, 270, 4),
  }});
  fixture.request.constraints.landSafetyMarginNm = 0.25;
  const corridor_routing::ArrivalGuide guide(fixture.request, land);
  ASSERT_TRUE(guide.available());
  ASSERT_FALSE(land.pointForbidden(at(0, 12)));
  ASSERT_TRUE(land.segmentForbidden(at(0, 12), target, 0.25));
  ASSERT_FALSE(land.segmentForbidden(at(180, 12), target, 0.25));
  EXPECT_GT(guide.estimate(at(0, 12), 12, 12),
            guide.estimate(at(180, 12), 12, 12));
  EXPECT_DOUBLE_EQ(guide.estimate(at(0, 90), 90, 97), 97);
}

TEST(CorridorEngine, ConnectedWaterRecognisesAnExitAwayFromTheDestination) {
  auto fixture = quick2_fixtures::make("open");
  fixture.request.start = {0, 0};
  fixture.request.destination = {0, 2.5};
  const wr::PolygonBoundaryProvider land({
      {{.06, -.5}, {.12, -.5}, {.12, .25}, {.06, .25}},
      {{-.12, -.5}, {-.06, -.5}, {-.06, .25}, {-.12, .25}},
      {{-.12, .2}, {.12, .2}, {.12, .25}, {-.12, .25}},
  });
  const corridor_routing::ConnectedWaterGuide guide(fixture.request, land);
  ASSERT_TRUE(guide.available());
  const auto estimate = [&](wr::GeoPoint point) {
    const auto direct = wr::distanceNm(point, fixture.request.destination);
    return guide.estimate(point, direct, direct);
  };
  EXPECT_LT(estimate({0, -.12}), estimate({0, .12}));
  EXPECT_GT(estimate(fixture.request.start),
            wr::distanceNm(fixture.request.start, fixture.request.destination));
}

TEST(CorridorEngine, RecoveryPreservesAnAlreadyValidatedOffshoreRoute) {
  auto fixture = quick2_fixtures::make("open");
  fixture.request.destination = wr::destinationPoint(fixture.request.start, 90, 160);
  corridor_routing::Options options;
  const auto baseline = corridor_routing::search(fixture.request, fixture.environment, options);
  options.recoverCoastal = true;
  options.recoverPassage = true;
  const auto result = corridor_routing::search(fixture.request, fixture.environment, options);
  ASSERT_TRUE(baseline.route.validation.passed);
  ASSERT_TRUE(result.route.validation.passed);
  ASSERT_EQ(result.route.legs.size(), baseline.route.legs.size());
  for (std::size_t i = 0; i < baseline.route.legs.size(); ++i) {
    const auto& before = baseline.route.legs[i];
    const auto& after = result.route.legs[i];
    EXPECT_DOUBLE_EQ(after.end.latitude, before.end.latitude);
    EXPECT_DOUBLE_EQ(after.end.longitude, before.end.longitude);
    EXPECT_EQ(after.endTime.time_since_epoch().count(),
              before.endTime.time_since_epoch().count());
  }
  EXPECT_EQ(result.route.diagnostics.generatedStates, baseline.route.diagnostics.generatedStates);
}

TEST(CorridorEngine, ValidatedTracksEndAtTheRequestedDestination) {
  for (const auto name : {"short", "open", "island", "dateline", "tidal"}) {
    SCOPED_TRACE(name);
    auto fixture = quick2_fixtures::make(name);
    fixture.request.limits.maximumGeneratedStates = 250000;
    corridor_routing::Options options;
    options.recoverCoastal = true;
    options.exploreWeatherCorridors = true;
    const auto search = corridor_routing::search(fixture.request, fixture.environment, options);
    const auto& route = search.route;
    ASSERT_TRUE(route.validation.passed) << route.message << " remaining=" << route.diagnostics.closestApproachNm;
    ASSERT_FALSE(route.legs.empty());
    EXPECT_EQ(route.engineIdentity, "corridor");
    EXPECT_LT(wr::distanceNm(route.legs.back().end, fixture.request.destination), 1e-6);
    const wr::PolarPerformanceModel performance(fixture.request.vessel);
    EXPECT_TRUE(wr::RouteValidator{}.validate(fixture.request, fixture.environment,
                                             performance, route.legs).passed);
    EXPECT_LE(route.diagnostics.generatedStates, fixture.request.limits.maximumGeneratedStates);
  }
}

TEST(CorridorEngine, AdvisoryGeometryCannotOverrideLandDepthOrMissingWeather) {
  corridor_routing::Options options;
  options.recoverCoastal = true;
  options.recoverPassage = true;
  options.geography = std::make_shared<wr::PolygonBoundaryProvider>(std::vector<std::vector<wr::GeoPoint>>{});
  for (const auto name : {"land-endpoint", "shallow", "missing-wind"}) {
    SCOPED_TRACE(name);
    const auto fixture = quick2_fixtures::make(name);
    const auto found = corridor_routing::search(fixture.request, fixture.environment, options);
    EXPECT_FALSE(found.route.validation.passed);
    EXPECT_TRUE(found.route.legs.empty());
    EXPECT_EQ(found.coastalRecoveryPasses, 0U);
  }
}

TEST(CorridorEngine, CancellationAndResourceLimitsNeverPublishPartialRoutes) {
  auto fixture = quick2_fixtures::make("open");
  fixture.request.cancellation.cancel();
  auto first = corridor_routing::search(fixture.request, fixture.environment);
  EXPECT_EQ(first.route.status, wr::RoutingStatus::Cancelled);
  EXPECT_TRUE(first.route.legs.empty());

  fixture = quick2_fixtures::make("open");
  fixture.request.destination = wr::destinationPoint(fixture.request.start, 90, 200);
  fixture.request.limits.maximumGeneratedStates = 24;
  corridor_routing::Options options;
  options.recoverCoastal = true;
  options.recoverPassage = true;
  auto limited = corridor_routing::search(fixture.request, fixture.environment, options);
  EXPECT_LE(limited.route.diagnostics.generatedStates, 24U);
  EXPECT_TRUE(limited.route.legs.empty());
  EXPECT_FALSE(limited.route.validation.passed);
}
