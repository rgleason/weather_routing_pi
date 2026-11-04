#include <gtest/gtest.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <random>
#include <wx/filename.h>
#include "Polar.h"
#include "original_routing/Engine.h"
#include "supercpn/weather_routing/Engine.h"
#include "supercpn/weather_routing/QuickEngine.h"

namespace {
std::vector<Polar> TestPolars() {
  std::vector<Polar> polars(1);
  wxString message;
  EXPECT_TRUE(polars[0].Open(wxString(TESTDATADIR) +
      "/polars/WindPolicy_test.pol", message)) << message;
  return polars;
}

TEST(PolarRoutingPolicy, DefaultBoatAcceptsQuintonsLightWindWithoutEditingPolar) {
  std::vector<Polar> polars(1);
  wxString message;
  ASSERT_TRUE(polars[0].Open(wxString(WEATHER_ROUTING_SOURCE_DIR) +
      "/data/polars/Example/Test-TWS-0-20+60.pol", message));
  bool estimated = false;
  EXPECT_NEAR(PolarSpeedForRouting(polars, 0, 60, 5.7099, nullptr, false,
                                   &estimated), 5.7 * 5.7099 / 6.0, 1e-6);
  EXPECT_TRUE(estimated);
  EXPECT_DOUBLE_EQ(PolarSpeedForRouting(polars, 0, 60, 60), 0);
  EXPECT_DOUBLE_EQ(PolarSpeedForRouting(polars, 0, 60, 70), 0);
}

TEST(PolarRoutingPolicy, LowAndHighPoliciesAreIndependentAndStrictIsAvailable) {
  auto polars = TestPolars();
  PolarSpeedStatus status;
  polars[0].lowWindPolicy = LowWindPolicy::Strict;
  EXPECT_TRUE(std::isnan(PolarSpeedForRouting(polars, 0, 90, 2, &status)));
  EXPECT_EQ(status, POLAR_SPEED_WIND_TOO_LIGHT);
  EXPECT_DOUBLE_EQ(PolarSpeedForRouting(polars, 0, 90, 20), 8);
  polars[0].lowWindPolicy = LowWindPolicy::Taper;
  polars[0].highWindPolicy = HighWindPolicy::Strict;
  EXPECT_DOUBLE_EQ(PolarSpeedForRouting(polars, 0, 90, 2), 1);
  EXPECT_TRUE(std::isnan(PolarSpeedForRouting(polars, 0, 90, 20, &status)));
  EXPECT_EQ(status, POLAR_SPEED_WIND_TOO_STRONG);
}

TEST(PolarRoutingPolicy, SuppliedZeroCellsAndEndpointZerosAreRespected) {
  auto polars = TestPolars();
  // Interior zero, low-wind zero and high-wind zero are authoritative for
  // direct sailing; no missing-data substitution is applied to these cells.
  EXPECT_DOUBLE_EQ(PolarSpeedForRouting(polars, 0, 60, 4), 0);
  EXPECT_DOUBLE_EQ(PolarSpeedForRouting(polars, 0, 60, 2), 0);
  EXPECT_DOUBLE_EQ(PolarSpeedForRouting(polars, 0, 45, 16), 0);
  EXPECT_DOUBLE_EQ(PolarSpeedForRouting(polars, 0, 45, 30), 0);
  EXPECT_DOUBLE_EQ(PolarSpeedForRouting(polars, 0, 90, 0), 0);
  for (double wind : {0.0, 2.0, 4.0, 8.0, 16.0, 40.0}) {
    EXPECT_DOUBLE_EQ(PolarSpeedForRouting(polars, 0, 0, wind), 0);
    EXPECT_DOUBLE_EQ(PolarSpeedForRouting(polars, 0, 180, wind), 0);
  }
}

TEST(PolarRoutingPolicy, BlankCellsCanInterpolateButAllNumericZerosRemainZero) {
  std::vector<Polar> polars(1);
  wxString message;
  ASSERT_TRUE(polars[0].Open(wxString(TESTDATADIR) +
      "/polars/BlankAndZero_test.pol", message)) << message;
  for (double wind : {2.0, 4.0, 8.0, 16.0, 40.0}) {
    EXPECT_DOUBLE_EQ(PolarSpeedForRouting(polars, 0, 0, wind), 0);
    EXPECT_DOUBLE_EQ(PolarSpeedForRouting(polars, 0, 60, wind), 0);
  }
  // A space-only missing cell is filled from its wind neighbours.
  EXPECT_DOUBLE_EQ(PolarSpeedForRouting(polars, 0, 90, 8), 4);
  // At the same wind speed a numeric zero must not be filled.
  EXPECT_DOUBLE_EQ(PolarSpeedForRouting(polars, 0, 120, 8), 0);
  // No lower/higher angles or wind measurements bracket this empty row.
  EXPECT_TRUE(std::isnan(PolarSpeedForRouting(polars, 0, 180, 8)));
  const wxString saved = wxFileName::CreateTempFileName("wr-blank-zero-");
  ASSERT_TRUE(polars[0].Save(saved));
  Polar reloaded;
  ASSERT_TRUE(reloaded.Open(saved, message));
  EXPECT_DOUBLE_EQ(reloaded.Speed(0, 4), 0);
  EXPECT_DOUBLE_EQ(reloaded.Speed(120, 8), 0);
  EXPECT_DOUBLE_EQ(reloaded.Speed(90, 8), 4);
  EXPECT_TRUE(std::isnan(reloaded.Speed(180, 8)));
  wxRemoveFile(saved);
}

TEST(PolarRoutingPolicy, HighWindHoldsFinalColumnAtAngleIncludingDecliningSpeed) {
  auto polars = TestPolars();
  bool estimated = false;
  EXPECT_DOUBLE_EQ(PolarSpeedForRouting(polars, 0, 120, 8), 8);
  EXPECT_DOUBLE_EQ(PolarSpeedForRouting(polars, 0, 120, 30, nullptr, false,
                                        &estimated), 6);
  EXPECT_TRUE(estimated);
  EXPECT_DOUBLE_EQ(PolarSpeedForRouting(polars, 0, 105, 30), 7);
}

TEST(PolarRoutingPolicy, GeneratedPolarUsesItsNewMaximumSpeed) {
  auto polars = TestPolars();
  polars[0].Generate({PolarMeasurement(10, 90, 1)});
  double generatedMaximum = 0;
  for (double wind : {4.0, 8.0, 16.0})
    for (double angle : {0.0, 45.0, 60.0, 90.0, 120.0, 180.0}) {
      const double speed = polars[0].Speed(angle, wind, nullptr, true);
      if (std::isfinite(speed)) generatedMaximum = std::max(generatedMaximum, speed);
    }
  ASSERT_GT(generatedMaximum, 0);
  ASSERT_NE(generatedMaximum, 8);
  EXPECT_DOUBLE_EQ(polars[0].MaximumSpeed(), generatedMaximum);
  EXPECT_DOUBLE_EQ(PolarSpeedForRouting(polars, 0, 90, 8),
                   polars[0].Speed(90, 8, nullptr, true));
}

TEST(PolarRoutingPolicy, InRangeAndEndpointInterpolationRemainsUnchanged) {
  auto polars = TestPolars();
  for (double wind : {4.0, 5.0, 8.0, 9.25, 16.0})
    for (double angle : {0.0, 45.0, 52.5, 60.0, 90.0, 105.0, 120.0, 180.0}) {
      bool estimated = true;
      EXPECT_DOUBLE_EQ(PolarSpeedForRouting(polars, 0, angle, wind, nullptr,
                                            false, &estimated),
                       polars[0].Speed(angle, wind, nullptr, true));
      EXPECT_FALSE(estimated);
    }
}

TEST(PolarRoutingPolicy, MultiSailAutomaticPreservesUpperLimitsAndWindGaps) {
  auto polars = TestPolars();
  Polar heavy;
  wxString message;
  ASSERT_TRUE(heavy.Open(wxString(WEATHER_ROUTING_SOURCE_DIR) +
      "/data/polars/Example/Example-24-60.pol", message));
  polars.push_back(heavy);
  EXPECT_DOUBLE_EQ(PolarSpeedForRouting(polars, 0, 90, 2), 1);
  EXPECT_TRUE(std::isnan(PolarSpeedForRouting(polars, 1, 90, 2)));
  for (double wind : {20.0, 70.0})
    for (std::size_t index : {0U, 1U})
      EXPECT_TRUE(std::isnan(PolarSpeedForRouting(polars, index, 90, wind)));
  polars[0].highWindPolicy = HighWindPolicy::Hold;
  EXPECT_DOUBLE_EQ(PolarSpeedForRouting(polars, 0, 90, 20), 8);
  polars[1].lowWindPolicy = LowWindPolicy::Taper;
  EXPECT_GT(PolarSpeedForRouting(polars, 1, 90, 20), 0);
}

TEST(PolarRoutingPolicy, LightWindSailCannotCompeteInStrongWindByDefault) {
  std::vector<Polar> polars(2);
  wxString message;
  ASSERT_TRUE(polars[0].Open(wxString(WEATHER_ROUTING_SOURCE_DIR) +
      "/data/polars/Example/Example-0-10.pol", message));
  ASSERT_TRUE(polars[1].Open(wxString(WEATHER_ROUTING_SOURCE_DIR) +
      "/data/polars/Example/Example-15-30.pol", message));
  EXPECT_TRUE(std::isnan(PolarSpeedForRouting(polars, 0, 60, 18.2)));
  EXPECT_NEAR(PolarSpeedForRouting(polars, 1, 60, 18.2), 7.912, 1e-6);
  polars[0].highWindPolicy = HighWindPolicy::Hold;
  EXPECT_NEAR(PolarSpeedForRouting(polars, 0, 60, 18.2), 6.5, 1e-6);
}

TEST(PolarRoutingPolicy, SeededWindAndAngleCoverageCannotInventExcessSpeed) {
  auto polars = TestPolars();
  std::mt19937 generator(1281);
  std::uniform_real_distribution<double> winds(0, 100), angles(0, 360);
  for (int i = 0; i < 2000; ++i) {
    const double angle = angles(generator);
    const double wind = winds(generator);
    const double speed = PolarSpeedForRouting(polars, 0, angle, wind);
    ASSERT_TRUE(std::isfinite(speed)) << "angle=" << angle << " wind=" << wind;
    EXPECT_GE(speed, 0);
    EXPECT_LE(speed, 8);
  }
  EXPECT_TRUE(std::isnan(PolarSpeedForRouting(polars, 0, 90, -1)));
  EXPECT_TRUE(std::isnan(PolarSpeedForRouting(polars, 0, NAN, 8)));
  EXPECT_TRUE(std::isnan(PolarSpeedForRouting(polars, 0, 90, INFINITY)));
  EXPECT_TRUE(std::isnan(PolarSpeedForRouting(polars, 1, 90, 8)));
}

TEST(PolarRoutingPolicy, FractionalAnglesBeyondZeroRowsUseTheNextInterval) {
  auto polars = TestPolars();
  EXPECT_DOUBLE_EQ(PolarSpeedForRouting(polars, 0, 60, 4), 0);
  EXPECT_NEAR(PolarSpeedForRouting(polars, 0, 60.5, 4), 2.0 / 60.0, 1e-6);
  EXPECT_NEAR(PolarSpeedForRouting(polars, 0, 299.5, 4), 2.0 / 60.0, 1e-6);
  wxString message;
  ASSERT_TRUE(polars[0].Open(wxString(TESTDATADIR) +
      "/polars/WindPolicy_fractional_test.pol", message));
  EXPECT_DOUBLE_EQ(PolarSpeedForRouting(polars, 0, 60.25, 4), 0);
  EXPECT_NEAR(PolarSpeedForRouting(polars, 0, 60.5, 4), 0.5 / 29.75, 1e-6);
}

namespace wr = supercpn::weather_routing;
class RoutingPolicyBoat final : public wr::VesselPerformanceModel {
public:
  mutable std::vector<Polar> polars{TestPolars()};
  bool valid(std::string* = nullptr) const override { return true; }
  wr::PerformanceCandidate evaluate(wr::PropulsionMode, wr::ProfileRole,
      const std::string&, double wind, double angle,
      const wr::WaveSample&) const override {
    bool estimated = false;
    const double speed = PolarSpeedForRouting(polars, 0, angle, wind, nullptr,
                                              false, &estimated);
    return {std::isfinite(speed) && speed > 0.0, wr::PropulsionMode::Sail,
        wr::ProfileRole::SailOnly, "wind-policy-fixture", 0, speed, 0, estimated};
  }
  std::vector<wr::PerformanceCandidate> candidates(double wind, double angle,
      const wr::WaveSample& waves, wr::PropulsionMode mode,
      wr::Duration) const override {
    const auto candidate = evaluate(mode, wr::ProfileRole::SailOnly, "", wind,
                                     angle, waves);
    return candidate.valid ? std::vector<wr::PerformanceCandidate>{candidate}
                           : std::vector<wr::PerformanceCandidate>{};
  }
};

wr::RoutingEnvironment PolicyEnvironment(double wind,
                                         std::shared_ptr<RoutingPolicyBoat> boat) {
  wr::UniformWeatherProvider::Configuration weather;
  weather.windTowardKnots = wr::speedDirectionToVector(wind, 180);
  weather.currentTowardKnots = wr::Vector2{};
  weather.begins = wr::TimePoint{};
  weather.ends = wr::TimePoint{} + std::chrono::hours{24};
  wr::RoutingEnvironment environment;
  environment.grib = std::make_shared<wr::UniformWeatherProvider>(weather);
  environment.performance = std::move(boat);
  environment.landAndBoundaries = std::make_shared<wr::OpenWaterProvider>();
  return environment;
}

wr::RoutingRequest PolicyRequest(wr::GeoPoint start, wr::GeoPoint end) {
  wr::RoutingRequest request;
  request.start = start;
  request.destination = end;
  request.environment.missingCurrent = wr::MissingCurrentPolicy::AllowAssumedZero;
  request.environment.zeroCurrentAcknowledged = true;
  request.environment.missingWaves = wr::MissingWavePolicy::AllowWithWarning;
  request.constraints.maximumTrueWindKnots = 80;
  request.options.timeStep = std::chrono::hours{1};
  request.options.minimumTimeStep = std::chrono::minutes{10};
  request.options.destinationToleranceNm = 0.2;
  request.options.headingStepDegrees = 10;
  request.options.spatialCellNm = 0.2;
  request.options.maximumSearchAngleDegrees = 120;
  request.limits.maximumRouteDuration = std::chrono::hours{12};
  request.limits.maximumGeneratedStates = 200000;
  request.limits.maximumRetainedStates = 50000;
  request.limits.maximumGraphLabels = 50000;
  return request;
}

class CalmThenWind final : public wr::WeatherProvider {
public:
  std::shared_ptr<const wr::WeatherProvider> weather;
  wr::TimePoint windBegins{wr::TimePoint{} + std::chrono::hours{2}};
  wr::TimePoint calmBegins{wr::TimePoint{}};
  wr::ParameterCoverage windCoverage() const override { return weather->windCoverage(); }
  wr::ParameterCoverage currentCoverage() const override { return weather->currentCoverage(); }
  wr::ParameterCoverage waveCoverage() const override { return weather->waveCoverage(); }
  wr::WindSample wind(wr::GeoPoint position, wr::TimePoint time) const override {
    auto sample = weather->wind(position, time);
    if (time >= calmBegins && time < windBegins) sample.velocity = {};
    return sample;
  }
  wr::CurrentSample current(wr::GeoPoint position, wr::TimePoint time) const override {
    return weather->current(position, time);
  }
  wr::WaveSample waves(wr::GeoPoint position, wr::TimePoint time) const override {
    return weather->waves(position, time);
  }
  std::string identity() const override { return "calm-then-sailable-wind"; }
};

class PolarRoutingEngines : public testing::TestWithParam<int> {
protected:
  wr::RoutingResult route(const wr::RoutingRequest& request,
                           const wr::RoutingEnvironment& environment) {
    switch (GetParam()) {
      case 0: return original_routing::Engine{}.route(request, environment);
      case 1: return wr::QuickRoutingEngine{}.route(request, environment).route;
      default: return wr::RoutingEngine{}.route(request, environment);
    }
  }
};

TEST_P(PolarRoutingEngines, SeededBidirectionalRoutesUseSamePolicyAndReplay) {
  std::mt19937 generator(1281);
  std::uniform_real_distribution<double> latitude(15, 30), longitude(-175, -140);
  std::uniform_real_distribution<double> bearing(80, 100), distance(2, 4);
  for (int seed = 0; seed < 6; ++seed) {
    const wr::GeoPoint start{latitude(generator), longitude(generator)};
    const wr::GeoPoint end = wr::destinationPoint(start, bearing(generator),
                                                 distance(generator));
    const double wind = std::array{2.0, 12.0, 32.0}[seed % 3];
    auto boat = std::make_shared<RoutingPolicyBoat>();
    wxString message;
    const wxString filename = wxString(WEATHER_ROUTING_SOURCE_DIR) +
        (seed % 2 ? "/data/polars/Example/Example-6-24.pol" :
                    "/data/polars/Example/Test-TWS-0-20+60.pol");
    ASSERT_TRUE(boat->polars[0].Open(filename, message)) << message;
    const bool expectedEstimate = std::isnan(boat->polars[0].Speed(90, wind,
                                                                  nullptr, true));
    const auto environment = PolicyEnvironment(wind, boat);
    for (bool reverse : {false, true}) {
      SCOPED_TRACE(testing::Message() << "seed=" << seed << " reverse=" << reverse
                                     << " wind=" << wind);
      const auto request = PolicyRequest(reverse ? end : start,
                                          reverse ? start : end);
      const auto result = route(request, environment);
      ASSERT_TRUE(result.validation.passed) << result.message;
      ASSERT_FALSE(result.legs.empty());
      const auto replay = wr::RouteValidator{}.validate(request, environment,
                                                        *boat, result.legs);
      ASSERT_TRUE(replay.passed) << replay.failureReason;
      for (const auto& leg : result.legs)
        EXPECT_LE(leg.speedThroughWaterKnots, boat->polars[0].MaximumSpeed());
      EXPECT_EQ(replay.estimatedPolarWind, expectedEstimate);
    }
  }
}

TEST_P(PolarRoutingEngines, CappedSpeedDoesNotBypassActualWeatherWindLimit) {
  auto boat = std::make_shared<RoutingPolicyBoat>();
  const auto environment = PolicyEnvironment(24, boat);
  auto request = PolicyRequest({22, -160}, {22, -159.95});
  request.constraints.maximumTrueWindKnots = 20;
  const auto result = route(request, environment);
  EXPECT_FALSE(result.validation.passed);
}

TEST_P(PolarRoutingEngines, WaitsStationaryInZeroWindThenSailsWhenWindReturns) {
  auto boat = std::make_shared<RoutingPolicyBoat>();
  auto environment = PolicyEnvironment(12, boat);
  auto weather = std::make_shared<CalmThenWind>();
  weather->weather = environment.grib;
  environment.grib = weather;
  auto request = PolicyRequest({22, -160}, {22, -159.95});
  request.options.allowWaiting = true;
  request.options.maximumWait = std::chrono::hours{3};
  const auto result = route(request, environment);
  ASSERT_TRUE(result.validation.passed) << result.message;
  ASSERT_FALSE(result.legs.empty());
  wr::Duration waiting{};
  bool sailed = false;
  for (const auto& leg : result.legs) {
    if (leg.stationaryWait) {
      EXPECT_DOUBLE_EQ(leg.speedThroughWaterKnots, 0);
      EXPECT_LT(wr::distanceNm(leg.start, leg.end), 1e-9);
      waiting += leg.endTime - leg.startTime;
    } else {
      EXPECT_GE(leg.startTime.time_since_epoch().count(),
                weather->windBegins.time_since_epoch().count());
      EXPECT_GT(leg.speedThroughWaterKnots, 0);
      EXPECT_GT(wr::distanceNm(leg.start, leg.end), 0);
      sailed = true;
    }
  }
  EXPECT_GE(waiting.count(), wr::Duration{std::chrono::hours{2}}.count());
  EXPECT_LE(waiting.count(), request.options.maximumWait.count());
  EXPECT_TRUE(sailed);
  const auto replay = wr::RouteValidator{}.validate(request, environment,
                                                   *boat, result.legs);
  EXPECT_TRUE(replay.passed) << replay.failureReason;
}

TEST_P(PolarRoutingEngines, CalmWaitingHonoursDisabledAndMaximumWaitPolicies) {
  auto boat = std::make_shared<RoutingPolicyBoat>();
  auto environment = PolicyEnvironment(12, boat);
  auto weather = std::make_shared<CalmThenWind>();
  weather->weather = environment.grib;
  environment.grib = weather;
  auto request = PolicyRequest({22, -160}, {22, -159.95});
  request.options.maximumWait = std::chrono::hours{1};
  EXPECT_FALSE(route(request, environment).validation.passed);
  request.options.maximumWait = std::chrono::hours{3};
  request.options.allowWaiting = false;
  EXPECT_FALSE(route(request, environment).validation.passed);
}

TEST_P(PolarRoutingEngines, SailsThenWaitsThroughCalmAndResumesAtSamePosition) {
  auto boat = std::make_shared<RoutingPolicyBoat>();
  auto environment = PolicyEnvironment(12, boat);
  auto weather = std::make_shared<CalmThenWind>();
  weather->weather = environment.grib;
  weather->calmBegins = wr::TimePoint{} + std::chrono::hours{1};
  weather->windBegins = wr::TimePoint{} + std::chrono::hours{3};
  environment.grib = weather;
  auto request = PolicyRequest({22, -160}, {22, -159.75});
  // Align the synthetic step changes with the search intervals, so this
  // tests retaining a stopped boat rather than Euler boundary refinement.
  request.options.adaptiveTimeStep = false;
  request.options.maximumWait = std::chrono::hours{3};
  request.limits.maximumGeneratedStates = 2000000;
  const auto result = route(request, environment);
  ASSERT_TRUE(result.validation.passed) << result.message << " "
      << testing::PrintToString(result.diagnostics.stageStopReasons)
      << " closest=" << result.diagnostics.closestApproachNm;
  bool sailedBefore = false, waited = false, sailedAfter = false;
  for (const auto& leg : result.legs) {
    if (leg.stationaryWait) {
      EXPECT_DOUBLE_EQ(leg.speedThroughWaterKnots, 0);
      EXPECT_LT(wr::distanceNm(leg.start, leg.end), 1e-9);
      EXPECT_GT(wr::distanceNm(request.start, leg.start), 0.1);
      waited = true;
    } else if (!waited) {
      EXPECT_LE(leg.endTime.time_since_epoch().count(),
                weather->calmBegins.time_since_epoch().count());
      sailedBefore = true;
    } else {
      EXPECT_GE(leg.startTime.time_since_epoch().count(),
                weather->windBegins.time_since_epoch().count());
      EXPECT_GT(leg.speedThroughWaterKnots, 0);
      sailedAfter = true;
    }
  }
  EXPECT_TRUE(sailedBefore);
  EXPECT_TRUE(waited);
  EXPECT_TRUE(sailedAfter);
  EXPECT_TRUE(wr::RouteValidator{}.validate(request, environment,
                                            *boat, result.legs).passed);
}

TEST_P(PolarRoutingEngines, ExplicitStrictLowWindAndTerminalZerosStillBlockSailing) {
  auto boat = std::make_shared<RoutingPolicyBoat>();
  boat->polars[0].lowWindPolicy = LowWindPolicy::Strict;
  const auto request = PolicyRequest({22, -160}, {22, -159.95});
  EXPECT_FALSE(route(request, PolicyEnvironment(2, boat)).validation.passed);
  wxString message;
  ASSERT_TRUE(boat->polars[0].Open(wxString(WEATHER_ROUTING_SOURCE_DIR) +
      "/data/polars/Example/Test-TWS-0-20+60.pol", message));
  EXPECT_FALSE(route(request, PolicyEnvironment(65, boat)).validation.passed);
}

INSTANTIATE_TEST_SUITE_P(QuickStandardProfessional, PolarRoutingEngines,
                         testing::Values(0, 1, 2));
}  // namespace
