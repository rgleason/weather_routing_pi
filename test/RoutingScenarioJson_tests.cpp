#include <wx/wx.h>

#include <gtest/gtest.h>

#include <cstdio>
#include <fstream>

#include <json/json.h>

#include "headless/RoutingScenarioJson.h"

TEST(RoutingScenarioJson, LoadsStabilityCorridorOptions) {
  weather_routing_engine::RoutingScenario scenario;
  wxString error;
  const wxString path = wxString(WEATHER_ROUTING_SOURCE_DIR) +
                        "/testdata/scenarios/"
                        "holyhead_dunlaoghaire_stability.json";
  ASSERT_TRUE(
      weather_routing_headless::LoadRoutingScenarioJson(path, scenario, error))
      << error;
  EXPECT_TRUE(scenario.stabilityCorridor.enabled);
  EXPECT_EQ(6, scenario.departureOptimization.concurrentRoutes);
  EXPECT_EQ("departureCandidates", scenario.stabilityCorridor.source);
  EXPECT_EQ(3, scenario.stabilityCorridor.minimumRoutes);
  EXPECT_DOUBLE_EQ(120.0, scenario.stabilityCorridor.maxEtaPenaltyMinutes);
  EXPECT_DOUBLE_EQ(0.5, scenario.stabilityCorridor.gridResolutionNm);
  EXPECT_DOUBLE_EQ(0.7, scenario.stabilityCorridor.innerAgreementThreshold);
  EXPECT_DOUBLE_EQ(0.4, scenario.stabilityCorridor.outerAgreementThreshold);
  EXPECT_TRUE(scenario.stabilityCorridor.clusterRoutes);
  EXPECT_TRUE(scenario.stabilityCorridor.writeGeoJson);
}

TEST(RoutingScenarioJson, LoadsSelfContainedGuiRegressionSettings) {
  weather_routing_engine::RoutingScenario scenario;
  wxString error;
  const wxString path = wxString(WEATHER_ROUTING_SOURCE_DIR) +
                        "/testdata/scenarios/"
                        "holyhead_dunlaoghaire_gui_regression.json";
  ASSERT_TRUE(
      weather_routing_headless::LoadRoutingScenarioJson(path, scenario, error))
      << error;
  EXPECT_TRUE(scenario.environment.hasUseGrib);
  EXPECT_TRUE(scenario.environment.useGrib);
  EXPECT_TRUE(scenario.environment.useCurrents);
  EXPECT_TRUE(scenario.route.hasBoatFile);
  EXPECT_EQ(
      "~/.opencpn/plugins/weather_routing/boats/"
      "Nicholson_35_conservative.xml",
      scenario.route.boatFile);
  EXPECT_EQ(3600, scenario.route.timeStepSeconds);
  EXPECT_DOUBLE_EQ(40.0, scenario.route.headingFromDegrees);
  EXPECT_DOUBLE_EQ(160.0, scenario.route.headingToDegrees);
  EXPECT_DOUBLE_EQ(5.0, scenario.route.headingStepDegrees);
  EXPECT_TRUE(scenario.route.optimizeTacking);
  EXPECT_DOUBLE_EQ(1.0, scenario.route.upwindEfficiency);
  EXPECT_DOUBLE_EQ(1.0, scenario.route.downwindEfficiency);
  EXPECT_DOUBLE_EQ(1.0, scenario.route.nightEfficiency);
  EXPECT_TRUE(scenario.route.useMotor);
  EXPECT_DOUBLE_EQ(4.5, scenario.route.motorSpeedThresholdKnots);
  EXPECT_DOUBLE_EQ(5.5, scenario.route.motorSpeedKnots);
}

TEST(RoutingScenarioJson, ClimatologyFallbackIsExplicitAndDefaultsOff) {
  const wxString enabledPath = "/tmp/weather-routing-climatology-on.json";
  {
    std::ofstream output(enabledPath.mb_str());
    output << R"({
      "schemaVersion": 1,
      "name": "Climatology fallback contract",
      "start": {"name": "Start", "lat": 50.0, "lon": -5.0},
      "end": {"name": "End", "lat": 51.0, "lon": -4.0},
      "environment": {"useGrib": true, "allowClimatologyFallback": true}
    })";
  }
  weather_routing_engine::RoutingScenario enabled;
  wxString error;
  ASSERT_TRUE(weather_routing_headless::LoadRoutingScenarioJson(enabledPath,
                                                                enabled, error))
      << error;
  EXPECT_TRUE(enabled.environment.hasAllowClimatologyFallback);
  EXPECT_TRUE(enabled.environment.allowClimatologyFallback);
  std::remove(enabledPath.mb_str());

  const wxString absentPath = "/tmp/weather-routing-climatology-absent.json";
  {
    std::ofstream output(absentPath.mb_str());
    output << R"({
      "schemaVersion": 1,
      "name": "No implicit climatology fallback",
      "start": {"name": "Start", "lat": 50.0, "lon": -5.0},
      "end": {"name": "End", "lat": 51.0, "lon": -4.0}
    })";
  }
  weather_routing_engine::RoutingScenario absent;
  ASSERT_TRUE(weather_routing_headless::LoadRoutingScenarioJson(absentPath,
                                                                absent, error))
      << error;
  EXPECT_FALSE(absent.environment.hasAllowClimatologyFallback);
  EXPECT_FALSE(absent.environment.allowClimatologyFallback);
  std::remove(absentPath.mb_str());
}

TEST(RoutingScenarioJson, LoadsRoutingEffortForHardRouteRegression) {
  weather_routing_engine::RoutingScenario scenario;
  wxString error;
  const wxString path = wxString(WEATHER_ROUTING_SOURCE_DIR) +
                        "/testdata/scenarios/"
                        "holyhead_lough_foyle_gui_regression.json";
  ASSERT_TRUE(
      weather_routing_headless::LoadRoutingScenarioJson(path, scenario, error))
      << error;
  EXPECT_TRUE(scenario.route.hasRoutingEffortPercent);
  EXPECT_EQ(400, scenario.route.routingEffortPercent);
}

TEST(RoutingScenarioJson, LoadsReverseReachabilityTargetTime) {
  weather_routing_engine::RoutingScenario scenario;
  wxString error;
  const wxString path = wxString(WEATHER_ROUTING_SOURCE_DIR) +
                        "/testdata/scenarios/"
                        "holyhead_lough_foyle_5m_effort200_20260829.json";
  ASSERT_TRUE(
      weather_routing_headless::LoadRoutingScenarioJson(path, scenario, error))
      << error;
  ASSERT_TRUE(scenario.reverseReachability.hasTargetTime);
  ASSERT_TRUE(scenario.reverseReachability.targetTime.IsValid());
  EXPECT_EQ("2026-08-29T16:42:32Z",
            scenario.reverseReachability.targetTime.ToUTC()
                    .FormatISOCombined('T') +
                "Z");
}

TEST(RoutingScenarioJson, LoadsMinimumDepthAcceptanceSettings) {
  weather_routing_engine::RoutingScenario scenario;
  wxString error;
  const wxString path = wxString(WEATHER_ROUTING_SOURCE_DIR) +
                        "/testdata/scenarios/"
                        "holyhead_lough_foyle_min_depth_5m.json";
  ASSERT_TRUE(
      weather_routing_headless::LoadRoutingScenarioJson(path, scenario, error))
      << error;
  EXPECT_EQ("chart", scenario.safety.mode);
  EXPECT_TRUE(scenario.safety.enforce);
  EXPECT_DOUBLE_EQ(0.4, scenario.safety.landMarginNm);
  EXPECT_DOUBLE_EQ(5.0, scenario.safety.minimumDepthM);
  EXPECT_TRUE(scenario.safety.persistentCertifiedCacheEnabled);
}

TEST(RoutingScenarioJson, LoadsPracticalDepthAwareDepartureAcceptance) {
  weather_routing_engine::RoutingScenario scenario;
  wxString error;
  const wxString path = wxString(WEATHER_ROUTING_SOURCE_DIR) +
                        "/testdata/scenarios/"
                        "holyhead_foyle_5m_departure_optimization.json";
  ASSERT_TRUE(
      weather_routing_headless::LoadRoutingScenarioJson(path, scenario, error))
      << error;
  EXPECT_TRUE(scenario.departureOptimization.enabled);
  EXPECT_EQ(180, scenario.departureOptimization.beforeMinutes);
  EXPECT_EQ(180, scenario.departureOptimization.afterMinutes);
  EXPECT_EQ(60, scenario.departureOptimization.stepMinutes);
  EXPECT_EQ(7, scenario.departureOptimization.concurrentRoutes);
  EXPECT_EQ(100, scenario.route.routingEffortPercent);
  EXPECT_DOUBLE_EQ(5.0, scenario.safety.minimumDepthM);
}

TEST(RoutingScenarioJson, PreservesExplicitUtcAcrossLocalTimezone) {
  const wxString scenarioPath = "/tmp/weather-routing-utc-scenario.json";
  {
    std::ofstream output(scenarioPath.mb_str());
    output << R"({
      "schemaVersion": 1,
      "name": "UTC parsing test",
      "start": {"name": "Start", "lat": 50.0, "lon": -5.0},
      "end": {"name": "End", "lat": 51.0, "lon": -4.0},
      "startTime": "2026-08-01T10:00:00Z"
    })";
  }

  weather_routing_engine::RoutingScenario scenario;
  wxString error;
  ASSERT_TRUE(weather_routing_headless::LoadRoutingScenarioJson(
      scenarioPath, scenario, error))
      << error;
  ASSERT_TRUE(scenario.startTime.IsValid());
  EXPECT_EQ("2026-08-01T10:00:00",
            scenario.startTime.ToUTC().FormatISOCombined('T'));
  std::remove(scenarioPath.mb_str());
}

TEST(RoutingScenarioJson, WritesStabilitySummaryAsValidJson) {
  weather_routing_engine::RoutingResult result;
  result.scenario = "Stability JSON test";
  result.status = "complete";
  weather_routing_engine::RoutingCandidateResult candidate;
  wxDateTime departure;
  ASSERT_TRUE(departure.ParseISOCombined("2026-08-01T10:00:00", 'T'));
  departure.MakeFromTimezone(wxDateTime::UTC);
  candidate.departure = departure;
  candidate.state = "complete";
  candidate.route.emplace_back(53.31, -4.63, departure);
  candidate.route.emplace_back(55.12, -6.95);
  result.candidates.push_back(candidate);
  result.stabilityCorridor.requested = true;
  result.stabilityCorridor.status = "complete";
  result.stabilityCorridor.validRoutes = 7;
  result.stabilityCorridor.excludedRoutes = 2;
  result.stabilityCorridor.routeFamilies = 2;
  result.stabilityCorridor.selectedFamilyId = 1;
  result.stabilityCorridor.dominantFamilyRoutes = 4;
  result.stabilityCorridor.medianWidthNm = 2.4;
  result.stabilityCorridor.maximumWidthNm = 8.7;
  result.stabilityCorridor.etaSpreadMinutes = 43.0;
  result.stabilityCorridor.innerThreshold = 0.7;
  result.stabilityCorridor.outerThreshold = 0.4;
  result.stabilityCorridor.representativeCandidateId = "candidate-0";
  result.stabilityCorridor.geoJsonPath = "/tmp/result.stability.geojson";

  const wxString path = "/tmp/weather-routing-stability-result.json";
  wxString error;
  ASSERT_TRUE(
      weather_routing_headless::SaveRoutingResultJson(path, result, error))
      << error;
  std::ifstream input(path.mb_str());
  Json::Value root;
  Json::Reader reader;
  ASSERT_TRUE(reader.parse(input, root, false));
  ASSERT_TRUE(root["stabilityCorridor"].isObject());
  ASSERT_TRUE(root["candidates"].isArray());
  ASSERT_EQ(1U, root["candidates"].size());
  EXPECT_EQ("2026-08-01T10:00:00Z",
            root["candidates"][0]["departure"].asString());
  ASSERT_EQ(2U, root["candidates"][0]["route"].size());
  EXPECT_DOUBLE_EQ(53.31, root["candidates"][0]["route"][0]
                               ["latitudeDegrees"].asDouble());
  EXPECT_EQ("2026-08-01T10:00:00Z",
            root["candidates"][0]["route"][0]["timeUtc"].asString());
  EXPECT_EQ(7, root["stabilityCorridor"]["validRoutes"].asInt());
  EXPECT_EQ(2, root["stabilityCorridor"]["routeFamilies"].asInt());
  EXPECT_EQ("candidate-0",
            root["stabilityCorridor"]["representativeCandidateId"].asString());
  std::remove(path.mb_str());
}
