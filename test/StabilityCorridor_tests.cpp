#include <wx/wx.h>

#include <gtest/gtest.h>

#include <cstdio>
#include <fstream>
#include <sstream>

#include <json/json.h>

#include "weather_routing_engine/StabilityCorridor.h"

namespace {

using weather_routing_engine::RouteFamily;
using weather_routing_engine::StabilityCorridorCalculator;
using weather_routing_engine::StabilityCorridorOptions;
using weather_routing_engine::StabilityCorridorResult;
using weather_routing_engine::StabilityPoint;
using weather_routing_engine::StabilityRoute;

StabilityRoute MakeRoute(const wxString& id, double latitudeOffset,
                         double longitudeOffset = 0.0,
                         long elapsedSeconds = 3600) {
  StabilityRoute route;
  route.id = id;
  route.complete = true;
  route.finalValidationPass = true;
  route.elapsedSeconds = elapsedSeconds;
  route.departure = wxDateTime(static_cast<time_t>(0));
  route.eta = wxDateTime(static_cast<time_t>(elapsedSeconds));
  for (int i = 0; i <= 12; ++i)
    route.points.push_back(StabilityPoint(53.0 + latitudeOffset,
                                          -6.0 + longitudeOffset + i * 0.02));
  return route;
}

bool CellIntersectsRectangle(const weather_routing_engine::StabilityCell& cell,
                             double minLat, double minLon, double maxLat,
                             double maxLon) {
  return !(cell.maxLat <= minLat || cell.minLat >= maxLat ||
           cell.maxLon <= minLon || cell.minLon >= maxLon);
}

}  // namespace

TEST(StabilityCorridor, IdenticalRoutesProduceOneNarrowFamily) {
  std::vector<StabilityRoute> routes;
  routes.push_back(MakeRoute("a", 0.0));
  routes.push_back(MakeRoute("b", 0.0));
  routes.push_back(MakeRoute("c", 0.0));

  StabilityCorridorResult result = StabilityCorridorCalculator::Calculate(
      routes, StabilityCorridorOptions());

  ASSERT_TRUE(result.success) << result.failureReason;
  ASSERT_EQ(1u, result.families.size());
  EXPECT_EQ(3u, result.families.front().routeIndices.size());
  EXPECT_LT(result.families.front().maximumWidthNm, 0.01);
  EXPECT_LT(result.families.front().representativeRouteIndex, routes.size());
  EXPECT_FALSE(result.families.front().innerCells.empty());
}

TEST(StabilityCorridor, ParallelVariationProducesContinuousFamily) {
  std::vector<StabilityRoute> routes;
  routes.push_back(MakeRoute("south", -0.004));
  routes.push_back(MakeRoute("middle", 0.0));
  routes.push_back(MakeRoute("north", 0.004));

  StabilityCorridorOptions options;
  options.gridResolutionNm = 0.25;
  StabilityCorridorResult result =
      StabilityCorridorCalculator::Calculate(routes, options);

  ASSERT_TRUE(result.success) << result.failureReason;
  ASSERT_EQ(1u, result.families.size());
  EXPECT_GT(result.families.front().maximumWidthNm, 0.4);
  EXPECT_FALSE(result.families.front().outerCells.empty());
}

TEST(StabilityCorridor, OppositeSidesOfIslandRemainSeparate) {
  std::vector<StabilityRoute> routes;
  for (int i = 0; i < 3; ++i)
    routes.push_back(
        MakeRoute(wxString::Format("north-%d", i), 0.030 + i * 0.001));
  for (int i = 0; i < 3; ++i)
    routes.push_back(
        MakeRoute(wxString::Format("south-%d", i), -0.030 - i * 0.001));

  const double blockedMinLat = 52.99;
  const double blockedMaxLat = 53.01;
  const double blockedMinLon = -5.90;
  const double blockedMaxLon = -5.82;
  auto connectorSafe = [&](const StabilityPoint& a, const StabilityPoint& b) {
    if ((a.lat < blockedMinLat && b.lat > blockedMaxLat) ||
        (b.lat < blockedMinLat && a.lat > blockedMaxLat)) {
      const double minLon = std::min(a.lon, b.lon);
      const double maxLon = std::max(a.lon, b.lon);
      if (maxLon >= blockedMinLon && minLon <= blockedMaxLon) return false;
    }
    return true;
  };
  auto cellSafe = [&](double minLat, double minLon, double maxLat,
                      double maxLon) {
    return maxLat <= blockedMinLat || minLat >= blockedMaxLat ||
           maxLon <= blockedMinLon || minLon >= blockedMaxLon;
  };

  StabilityCorridorResult result = StabilityCorridorCalculator::Calculate(
      routes, StabilityCorridorOptions(), connectorSafe, cellSafe);

  ASSERT_TRUE(result.success) << result.failureReason;
  ASSERT_EQ(2u, result.families.size());
  for (const RouteFamily& family : result.families) {
    EXPECT_EQ(3u, family.routeIndices.size());
    EXPECT_LT(family.representativeRouteIndex, routes.size());
    for (const auto& cell : family.outerCells)
      EXPECT_FALSE(CellIntersectsRectangle(cell, blockedMinLat, blockedMinLon,
                                           blockedMaxLat, blockedMaxLon));
  }
}

TEST(StabilityCorridor, OutlierFormsNoEligibleFamily) {
  std::vector<StabilityRoute> routes;
  routes.push_back(MakeRoute("a", 0.0));
  routes.push_back(MakeRoute("b", 0.002));
  routes.push_back(MakeRoute("c", -0.002));
  routes.push_back(MakeRoute("outlier", 0.20));

  StabilityCorridorResult result = StabilityCorridorCalculator::Calculate(
      routes, StabilityCorridorOptions());

  ASSERT_TRUE(result.success) << result.failureReason;
  ASSERT_EQ(1u, result.families.size());
  EXPECT_EQ(3u, result.families.front().routeIndices.size());
  EXPECT_EQ(-1, StabilityCorridorCalculator::FindFamilyForRoute(result, 3));
}

TEST(StabilityCorridor, ExcludesFailedUnsafeAndSlowRoutes) {
  std::vector<StabilityRoute> routes;
  routes.push_back(MakeRoute("a", 0.0, 0.0, 3600));
  routes.push_back(MakeRoute("b", 0.001, 0.0, 3700));
  routes.push_back(MakeRoute("c", -0.001, 0.0, 3800));
  StabilityRoute failed = MakeRoute("failed", 0.0);
  failed.complete = false;
  routes.push_back(failed);
  StabilityRoute unsafe = MakeRoute("unsafe", 0.0);
  unsafe.finalValidationPass = false;
  routes.push_back(unsafe);
  routes.push_back(MakeRoute("slow", 0.0, 0.0, 20000));

  StabilityCorridorOptions options;
  options.maxEtaPenaltyMinutes = 60.0;
  StabilityCorridorResult result =
      StabilityCorridorCalculator::Calculate(routes, options);

  ASSERT_TRUE(result.success) << result.failureReason;
  EXPECT_EQ(3, result.validRoutes);
  EXPECT_EQ(3, result.excludedRoutes);
  EXPECT_EQ(1, result.etaExcludedRoutes);
}

TEST(StabilityCorridor, TooFewRoutesReturnsClearFailure) {
  std::vector<StabilityRoute> routes;
  routes.push_back(MakeRoute("a", 0.0));
  routes.push_back(MakeRoute("b", 0.001));

  StabilityCorridorResult result = StabilityCorridorCalculator::Calculate(
      routes, StabilityCorridorOptions());

  EXPECT_FALSE(result.success);
  EXPECT_NE(wxNOT_FOUND, result.failureReason.Find("fewer than minimum"));
}

TEST(StabilityCorridor, WritesGeoJsonWithBandsAndRealMedoid) {
  std::vector<StabilityRoute> routes;
  routes.push_back(MakeRoute("candidate-a", 0.0));
  routes.push_back(MakeRoute("candidate-b", 0.001));
  routes.push_back(MakeRoute("candidate-c", -0.001));
  StabilityCorridorResult result = StabilityCorridorCalculator::Calculate(
      routes, StabilityCorridorOptions());
  ASSERT_TRUE(result.success);

  const wxString path = "/tmp/weather-routing-stability-test.geojson";
  wxString error;
  ASSERT_TRUE(weather_routing_engine::WriteStabilityCorridorGeoJson(
      path, routes, result, error))
      << error;
  std::ifstream input(path.mb_str());
  Json::Value geojson;
  Json::Reader reader;
  ASSERT_TRUE(reader.parse(input, geojson, false));
  ASSERT_EQ("FeatureCollection", geojson["type"].asString());
  ASSERT_TRUE(geojson["features"].isArray());
  ASSERT_FALSE(geojson["features"].empty());
  bool sawInner = false;
  bool sawOuter = false;
  bool sawMedoid = false;
  for (const Json::Value& feature : geojson["features"]) {
    ASSERT_TRUE(feature["properties"]["familyId"].isInt());
    const std::string band = feature["properties"]["band"].asString();
    const Json::Value& geometry = feature["geometry"];
    if (band == "medoid") {
      sawMedoid = true;
      EXPECT_EQ("LineString", geometry["type"].asString());
      EXPECT_EQ(0u, feature["properties"]["candidateId"].asString().find(
                        "candidate-"));
      ASSERT_GE(geometry["coordinates"].size(), 2u);
      continue;
    }
    sawInner = sawInner || band == "inner";
    sawOuter = sawOuter || band == "outer";
    EXPECT_TRUE(band == "inner" || band == "outer");
    EXPECT_EQ("Polygon", geometry["type"].asString());
    ASSERT_EQ(1u, geometry["coordinates"].size());
    const Json::Value& ring = geometry["coordinates"][0];
    ASSERT_EQ(5u, ring.size());
    EXPECT_EQ(ring[0], ring[ring.size() - 1]);
  }
  EXPECT_TRUE(sawInner);
  EXPECT_TRUE(sawOuter);
  EXPECT_TRUE(sawMedoid);
  input.clear();
  input.seekg(0);
  std::ostringstream contents;
  contents << input.rdbuf();
  EXPECT_NE(std::string::npos, contents.str().find("FeatureCollection"));
  EXPECT_NE(std::string::npos, contents.str().find("\"band\":\"outer\""));
  EXPECT_NE(std::string::npos, contents.str().find("\"band\":\"medoid\""));
  EXPECT_NE(std::string::npos, contents.str().find("candidate-"));
  std::remove(path.mb_str());
}
