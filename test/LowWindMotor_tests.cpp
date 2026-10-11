#include <gtest/gtest.h>
#include <cmath>
#include "Polar.h"
#include "original_routing/Engine.h"
#include "supercpn/weather_routing/Engine.h"
#include "supercpn/weather_routing/QuickEngine.h"

namespace {
namespace wr = supercpn::weather_routing;
class Boat final : public wr::VesselPerformanceModel {
public:
  mutable std::vector<Polar> polars{1};
  double threshold{4}, upwind{1}, downwind{1}, night{1};
  bool motor{true};
  Boat() {
    wxString error;
    EXPECT_TRUE(polars[0].Open(wxString(TESTDATADIR) +
        "/polars/LowWindMotor_test.pol", error)) << error;
  }
  bool valid(std::string* = nullptr) const override { return true; }
  std::optional<double> bestSailingSpeedAt(wr::GeoPoint, wr::TimePoint,
      double wind, const wr::WaveSample&, double lo, double hi) const override {
    return BestSailingSpeedForRouting(polars, wind, lo, hi, upwind, downwind, night);
  }
  wr::PerformanceCandidate evaluate(wr::PropulsionMode mode, wr::ProfileRole,
      const std::string&, double wind, double angle, const wr::WaveSample&) const override {
    if (mode == wr::PropulsionMode::Motor)
      return {motor, mode, wr::ProfileRole::MotorOnly, "motor", -1, 5.5};
    const double speed = PolarSpeedForRouting(polars, 0, angle, wind);
    if (!std::isfinite(speed) || speed <= 0) return {};
    if (motor && speed < threshold)
      return {true, wr::PropulsionMode::Motor, wr::ProfileRole::MotorOnly, "motor", -1, 5.5};
    return {true, wr::PropulsionMode::Sail, wr::ProfileRole::SailOnly, "sail", 0, speed};
  }
  std::vector<wr::PerformanceCandidate> candidates(double wind, double angle,
      const wr::WaveSample& waves, wr::PropulsionMode, wr::Duration) const override {
    auto value = evaluate(wr::PropulsionMode::Sail, wr::ProfileRole::SailOnly,
                          "sail", wind, angle, waves);
    if (!value.valid && motor)
      value = evaluate(wr::PropulsionMode::Motor, wr::ProfileRole::MotorOnly,
                       "motor", wind, angle, waves);
    return value.valid ? std::vector{value} : std::vector<wr::PerformanceCandidate>{};
  }
};

wr::RoutingRequest request(double threshold = 4) {
  wr::RoutingRequest r;
  r.start = {22, -160};
  r.destination = wr::destinationPoint(r.start, 0, 2);
  r.vessel.propulsion.allowMotor = true;
  r.vessel.propulsion.configuredMotorSpeedKnots = 5.5;
  r.vessel.propulsion.motorBelowSailingSpeedKnots = threshold;
  r.constraints.minimumTrueWindAngleDegrees = 40;
  r.constraints.maximumTrueWindAngleDegrees = 160;
  r.environment.useCurrent = false;
  r.environment.useWaves = false;
  r.options.timeStep = std::chrono::minutes{10};
  r.options.minimumTimeStep = std::chrono::minutes{5};
  r.options.headingStepDegrees = 10;
  r.options.destinationToleranceNm = 0.05;
  r.options.maximumSearchAngleDegrees = 120;
  r.limits.maximumRouteDuration = std::chrono::hours{4};
  r.limits.maximumGeneratedStates = 200000;
  r.limits.maximumRetainedStates = 50000;
  r.limits.maximumGraphLabels = 50000;
  return r;
}
wr::RoutingEnvironment environment(std::shared_ptr<Boat> boat, double wind,
                                   double current = 0) {
  wr::UniformWeatherProvider::Configuration weather;
  weather.windTowardKnots = wr::Vector2{0, -wind};
  weather.currentTowardKnots = wr::Vector2{0, -current};
  wr::RoutingEnvironment e;
  e.grib = std::make_shared<wr::UniformWeatherProvider>(weather);
  e.performance = std::move(boat);
  e.landAndBoundaries = std::make_shared<wr::OpenWaterProvider>();
  return e;
}

TEST(LowWindMotor, UsesBestActualSailingSpeedAndConfiguredThreshold) {
  Boat boat;
  auto r = request();
  EXPECT_NEAR(*boat.bestSailingSpeedAt({}, {}, 2, {}, 40, 160), 1.12, 1e-6);
  EXPECT_NEAR(*boat.bestSailingSpeedAt({}, {}, 4, {}, 40, 160), 2.94, 1e-6);
  EXPECT_NEAR(*boat.bestSailingSpeedAt({}, {}, 6, {}, 40, 160), 4.05, 1e-6);
  auto permitted = [&](double wind) { return wr::sailingAngleAllowed(r, boat,
      {}, {}, wind, 0, {}, wr::PropulsionMode::Motor, wr::ProfileRole::MotorOnly); };
  EXPECT_TRUE(permitted(4));
  EXPECT_FALSE(permitted(6));
  r.vessel.propulsion.motorBelowSailingSpeedKnots = 2;
  EXPECT_FALSE(permitted(4));
  r.vessel.propulsion.motorBelowSailingSpeedKnots = 4.5;
  EXPECT_TRUE(permitted(6));
  EXPECT_FALSE(permitted(8));
  r.vessel.propulsion.motorBelowSailingSpeedKnots =
      *boat.bestSailingSpeedAt({}, {}, 6, {}, 40, 160);
  EXPECT_FALSE(permitted(6)); // At the threshold is not below it.
}

TEST(LowWindMotor, SailingMotorSailingDisabledMotorAndMissingPolarKeepAngles) {
  Boat boat;
  auto r = request();
  EXPECT_FALSE(wr::sailingAngleAllowed(r, boat, {}, {}, 2, 0, {},
      wr::PropulsionMode::Sail, wr::ProfileRole::SailOnly));
  EXPECT_FALSE(wr::sailingAngleAllowed(r, boat, {}, {}, 2, 0, {},
      wr::PropulsionMode::MotorSail, wr::ProfileRole::MotorSailing));
  r.vessel.propulsion.allowMotor = false;
  EXPECT_FALSE(wr::sailingAngleAllowed(r, boat, {}, {}, 2, 0, {},
      wr::PropulsionMode::Motor, wr::ProfileRole::MotorOnly));
  r.vessel.propulsion.allowMotor = true;
  boat.polars.clear();
  EXPECT_FALSE(wr::sailingAngleAllowed(r, boat, {}, {}, 2, 0, {},
      wr::PropulsionMode::Motor, wr::ProfileRole::MotorOnly));
}

TEST(LowWindMotor, AppliesEfficiencyAndUsesEveryUsableSail) {
  Boat boat;
  boat.upwind = 0.8;
  boat.downwind = 0.8;
  EXPECT_NEAR(*boat.bestSailingSpeedAt({}, {}, 6, {}, 40, 160), 4.05 * 0.8, 1e-6);
  boat.night = 0.5;
  EXPECT_NEAR(*boat.bestSailingSpeedAt({}, {}, 6, {}, 40, 160), 4.05 * 0.4, 1e-6);
  boat.polars.push_back(boat.polars.front());
  wxString error;
  ASSERT_TRUE(boat.polars.back().Open(wxString(WEATHER_ROUTING_SOURCE_DIR) +
      "/data/polars/Example/Example-6-24.pol", error));
  boat.polars.back().lowWindPolicy = LowWindPolicy::Taper;
  // Use unscaled factors; changing the second sail must prevent selecting a
  // slower first sail as evidence that all sailing is below the threshold.
  EXPECT_GT(*BestSailingSpeedForRouting(boat.polars, 4, 40, 160), 4);
}

TEST(LowWindMotor, RejectedWindRangesStayUnknownAndExplicitCalmZeroIsUsable) {
  Boat boat;
  EXPECT_EQ(*BestSailingSpeedForRouting(boat.polars, 0, 40, 160), 0);
  wxString error;
  ASSERT_TRUE(boat.polars[0].Open(wxString(WEATHER_ROUTING_SOURCE_DIR) +
      "/data/polars/Example/Test-TWS-0-20+60.pol", error));
  boat.polars[0].lowWindPolicy = LowWindPolicy::Strict;
  EXPECT_FALSE(BestSailingSpeedForRouting(boat.polars, 2, 40, 160));
  boat.polars[0].highWindPolicy = HighWindPolicy::Strict;
  EXPECT_FALSE(BestSailingSpeedForRouting(boat.polars, 80, 40, 160));
  EXPECT_FALSE(BestSailingSpeedForRouting(boat.polars, NAN, 40, 160));
}

class Engines : public testing::TestWithParam<int> {
protected:
  wr::RoutingResult route(const wr::RoutingRequest& r, const wr::RoutingEnvironment& e) {
    if (GetParam() == 0) return original_routing::Engine{}.route(r, e);
    if (GetParam() == 1) return wr::QuickRoutingEngine{}.route(r, e).route;
    return wr::RoutingEngine{}.route(r, e);
  }
};
TEST_P(Engines, MotorsIntoWindWithoutWaitingInCalmAndLightWind) {
  for (double wind : {0., 2., 4.}) {
    auto r = request();
    const auto e = environment(std::make_shared<Boat>(), wind);
    const auto result = route(r, e);
    ASSERT_TRUE(result.validation.passed) << result.message;
    EXPECT_EQ(result.metrics.waitingTime.count(), 0);
    EXPECT_LT(result.metrics.elapsed.count(), 1800);
    ASSERT_FALSE(result.legs.empty());
    for (const auto& leg : result.legs) {
      EXPECT_EQ(leg.propulsionMode, wr::PropulsionMode::Motor);
      EXPECT_DOUBLE_EQ(leg.speedThroughWaterKnots, 5.5);
    }
    EXPECT_TRUE(wr::RouteValidator{}.validate(r, e, *e.performance, result.legs).passed);
  }
}
TEST_P(Engines, StrongerWindKeepsAnglesEvenWhenDirectPolarSpeedIsZero) {
  auto r = request();
  const auto e = environment(std::make_shared<Boat>(), 8);
  const auto result = route(r, e);
  ASSERT_TRUE(result.validation.passed) << result.message;
  for (const auto& leg : result.legs) {
    if (leg.stationaryWait) continue;
    EXPECT_GE(leg.trueWindAngleDegrees + 1e-8, 40);
    EXPECT_LE(leg.trueWindAngleDegrees - 1e-8, 160);
  }
}
TEST_P(Engines, AdverseCurrentDoesNotChangeSTWGateOrConfiguredMotorSpeed) {
  auto r = request(); r.environment.useCurrent = true;
  // Quick's terminal reconstruction is bounded to two search steps. Allow
  // the 48-minute passage at 2.5 knots SOG while retaining the 5.5-knot STW.
  r.options.timeStep = std::chrono::minutes{30};
  const auto result = route(r, environment(std::make_shared<Boat>(), 2, 3));
  std::string reasons;
  for (const auto& reason : result.diagnostics.stageStopReasons) reasons += reason + "; ";
  ASSERT_TRUE(result.validation.passed) << result.message << " " << reasons;
  EXPECT_EQ(result.metrics.waitingTime.count(), 0);
  EXPECT_GT(result.metrics.elapsed.count(), 2500);
  for (const auto& leg : result.legs) EXPECT_DOUBLE_EQ(leg.speedThroughWaterKnots, 5.5);
}
TEST_P(Engines, MotorAngleExceptionCannotBypassWeatherLimits) {
  auto r = request(); r.constraints.maximumTrueWindKnots = 1;
  const auto result = route(r, environment(std::make_shared<Boat>(), 2));
  EXPECT_FALSE(result.validation.passed);
}
TEST_P(Engines, IndependentReplayRejectsMotorHeadingWhenSailingWindReturns) {
  auto r = request();
  const auto boat = std::make_shared<Boat>();
  const auto e = environment(boat, 2);
  const auto result = route(r, e);
  ASSERT_TRUE(result.validation.passed) << result.message;
  const auto strong = environment(boat, 8);
  const auto replay = wr::RouteValidator{}.validate(r, strong, *boat, result.legs);
  EXPECT_FALSE(replay.passed);
  EXPECT_NE(replay.failureReason.find("true-wind-angle"), std::string::npos);
  r.vessel.propulsion.motorBelowSailingSpeedKnots = 1;
  const auto changedThreshold = wr::RouteValidator{}.validate(r, e, *boat, result.legs);
  EXPECT_FALSE(changedThreshold.passed);
}

class ChangingWind final : public wr::WeatherProvider {
public:
  wr::TimePoint change;
  std::shared_ptr<const wr::WeatherProvider> light, strong;
  wr::ParameterCoverage windCoverage() const override { return light->windCoverage(); }
  wr::ParameterCoverage currentCoverage() const override { return light->currentCoverage(); }
  wr::ParameterCoverage waveCoverage() const override { return light->waveCoverage(); }
  wr::WindSample wind(wr::GeoPoint p, wr::TimePoint t) const override {
    return (t < change ? light : strong)->wind(p, t);
  }
  wr::CurrentSample current(wr::GeoPoint p, wr::TimePoint t) const override {
    return light->current(p, t);
  }
  wr::WaveSample waves(wr::GeoPoint p, wr::TimePoint t) const override {
    return light->waves(p, t);
  }
  std::string identity() const override { return "light wind becoming sailing wind"; }
};
TEST_P(Engines, ReplayChecksWindDuringTheLegInsteadOfOnlyAtDeparture) {
  const auto r = request();
  const auto boat = std::make_shared<Boat>();
  auto e = environment(boat, 2);
  const auto result = route(r, e);
  ASSERT_TRUE(result.validation.passed) << result.message;
  auto changing = std::make_shared<ChangingWind>();
  changing->change = r.departure + std::chrono::minutes{5};
  changing->light = e.grib;
  changing->strong = environment(boat, 8).grib;
  e.grib = changing;
  const auto replay = wr::RouteValidator{}.validate(r, e, *boat, result.legs);
  EXPECT_FALSE(replay.passed);
  EXPECT_NE(replay.failureReason.find("true-wind-angle"), std::string::npos);
}
INSTANTIATE_TEST_SUITE_P(QuickStandardProfessional, Engines, testing::Values(0, 1, 2));
} // namespace
