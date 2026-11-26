// SPDX-License-Identifier: GPL-3.0-or-later
#include "Quick2Fixtures.h"
#include "engine/corridor/Engine.h"

#include <gtest/gtest.h>

namespace wr = supercpn::weather_routing;

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
  auto limited = corridor_routing::search(fixture.request, fixture.environment, options);
  EXPECT_LE(limited.route.diagnostics.generatedStates, 24U);
  EXPECT_TRUE(limited.route.legs.empty());
  EXPECT_FALSE(limited.route.validation.passed);
}
