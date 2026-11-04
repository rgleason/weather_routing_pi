// SPDX-License-Identifier: GPL-3.0-or-later
#include "Quick2Fixtures.h"
#include "atlas_routing/Engine.h"
#include <gtest/gtest.h>

namespace wr = supercpn::weather_routing;
namespace {
class AtlasPhysicalRoutes : public testing::TestWithParam<const char*> {};

TEST_P(AtlasPhysicalRoutes, ReachesDestinationAndPassesFreshChronologicalReplay) {
  auto f = quick2_fixtures::make(GetParam());
  const auto result = atlas_routing::Engine{}.route(f.request, f.environment);
  ASSERT_EQ(result.route.status, wr::RoutingStatus::Complete) << result.route.message;
  ASSERT_TRUE(result.route.validation.passed);
  ASSERT_FALSE(result.route.legs.empty());
  EXPECT_LT(wr::distanceNm(result.route.legs.back().end, f.request.destination), .05);
  EXPECT_TRUE(wr::RouteValidator{}.validate(
      f.request, f.environment, *f.environment.performance, result.route.legs).passed);
  wr::Duration elapsed{};
  for (const auto& leg : result.route.legs) elapsed += leg.endTime - leg.startTime;
  EXPECT_EQ(elapsed.count(), result.route.metrics.elapsed.count());
}
INSTANTIATE_TEST_SUITE_P(WeatherAndGeography, AtlasPhysicalRoutes,
    testing::Values("short", "upwind", "island", "thin-island", "tidal",
                    "variable", "dateline", "high-latitude"));

TEST(AtlasEngine, RejectsMissingWindForbiddenEndpointAndShallowWater) {
  for (const auto* name : {"missing-wind", "land-endpoint", "shallow"}) {
    SCOPED_TRACE(name);
    auto f = quick2_fixtures::make(name);
    const auto result = atlas_routing::Engine{}.route(f.request, f.environment);
    EXPECT_FALSE(result.route.validation.passed);
    EXPECT_TRUE(result.route.legs.empty());
  }
}

TEST(AtlasEngine, CancellationAndUnsupportedObjectiveCannotReturnACompletedRoute) {
  auto f = quick2_fixtures::make("short");
  f.request.cancellation.cancel();
  EXPECT_EQ(atlas_routing::Engine{}.route(f.request, f.environment).route.status,
            wr::RoutingStatus::Cancelled);
  f = quick2_fixtures::make("short");
  f.request.objective.kind = wr::ObjectiveKind::FuelAware;
  const auto result = atlas_routing::Engine{}.route(f.request, f.environment);
  EXPECT_EQ(result.route.status, wr::RoutingStatus::InvalidVesselConfiguration);
  EXPECT_FALSE(result.route.validation.passed);
  EXPECT_TRUE(result.route.legs.empty());
}

class AtlasStrictReplay final : public wr::LandAndBoundaryProvider {
 public:
  mutable unsigned prepared{};
  bool pointForbidden(wr::GeoPoint) const override { return false; }
  bool segmentForbidden(wr::GeoPoint, wr::GeoPoint, double) const override { return false; }
  double distanceToForbiddenNm(wr::GeoPoint) const override { return 100; }
  bool validationSegmentFromKnownSafeForbiddenAt(
      wr::GeoPoint, wr::GeoPoint, wr::TimePoint, double) const override { return true; }
  bool validationFailureRequiresSearchEscalation() const override { return true; }
  void prepareValidationRoute(std::span<const wr::RouteLeg>, double) const override { ++prepared; }
  std::string identity() const override { return "atlas-strict-authoritative-replay"; }
};

TEST(AtlasEngine, AuthoritativeReplayCanRejectOtherwiseOpenWater) {
  auto f = quick2_fixtures::make("short");
  auto strict = std::make_shared<AtlasStrictReplay>();
  f.environment.landAndBoundaries = strict;
  const auto result = atlas_routing::Engine{}.route(f.request, f.environment);
  EXPECT_GT(strict->prepared, 0U);
  EXPECT_FALSE(result.route.validation.passed);
  EXPECT_TRUE(result.route.legs.empty());
}

TEST(AtlasEngine, RepeatedSolvePreservesArrivalGeometryAndWork) {
  auto f = quick2_fixtures::make("short");
  const auto a = atlas_routing::Engine{}.route(f.request, f.environment);
  const auto b = atlas_routing::Engine{}.route(f.request, f.environment);
  ASSERT_TRUE(a.route.validation.passed && b.route.validation.passed);
  EXPECT_EQ(a.route.metrics.elapsed.count(), b.route.metrics.elapsed.count());
  EXPECT_EQ(a.atlas.edgeAttempts, b.atlas.edgeAttempts);
  ASSERT_EQ(a.route.legs.size(), b.route.legs.size());
  for (std::size_t i = 0; i < a.route.legs.size(); ++i) {
    EXPECT_EQ(a.route.legs[i].end, b.route.legs[i].end);
    EXPECT_EQ(a.route.legs[i].endTime.time_since_epoch().count(),
              b.route.legs[i].endTime.time_since_epoch().count());
  }
}
}  // namespace
