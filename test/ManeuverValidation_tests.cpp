#include <gtest/gtest.h>

#include <algorithm>
#include <memory>
#include <vector>

#include "RouteDiagnostics.h"
#include "supercpn/weather_routing/Engine.h"

namespace {
using namespace supercpn::weather_routing;
enum class Change { Tack, Gybe, Sail, Propulsion };

class IntervalWeather final : public WeatherProvider {
public:
  TimePoint begins{Duration{1800000600}};
  Duration interval{600};
  bool windSpike{}, waveSpike{}, opposingCurrent{}, missingWind{}, missingWaves{}, turningHeadwind{};
  bool fallbackDuringInterval{}, allClimatology{}, allMissingWaves{};
  mutable std::vector<TimePoint> windQueries;
  bool during(TimePoint time) const {
    return time >= begins && time < begins + interval;
  }
  ParameterCoverage windCoverage() const override {
    return {true, {}, {}, {-180, -90, 180, 90}, "manoeuvre-test"};
  }
  ParameterCoverage currentCoverage() const override { return windCoverage(); }
  ParameterCoverage waveCoverage() const override { return windCoverage(); }
  WindSample wind(GeoPoint, TimePoint time) const override {
    windQueries.push_back(time);
    WindSample sample;
    sample.available = !allClimatology &&
        !(during(time) && (missingWind || fallbackDuringInterval));
    sample.velocity = {0, windSpike && during(time) ? 50.0 : 10.0};
    if (turningHeadwind && during(time)) sample.velocity = {-10, 0};
    sample.metadata.source = EnvironmentalSource::GribForecast;
    sample.metadata.datasetIdentity = "test-forecast";
    return sample;
  }
  CurrentSample current(GeoPoint, TimePoint time) const override {
    CurrentSample sample;
    sample.available = true;
    sample.velocity = {0, opposingCurrent && during(time) ? -5.0 : 0.0};
    sample.metadata.source = EnvironmentalSource::GribForecast;
    return sample;
  }
  WaveSample waves(GeoPoint, TimePoint time) const override {
    WaveSample sample;
    sample.available = !allMissingWaves && !(missingWaves && during(time));
    sample.significantHeightMetres = waveSpike && during(time) ? 5.0 : 1.0;
    sample.metadata.source = EnvironmentalSource::GribForecast;
    return sample;
  }
  std::string identity() const override { return "manoeuvre-test"; }
};

class TypicalWind final : public ClimatologyProvider {
public:
  ParameterCoverage coverage() const override {
    return {true, {}, {}, {-180, -90, 180, 90}, "typical-test"};
  }
  WindSample wind(GeoPoint, TimePoint) const override {
    WindSample sample;
    sample.available = true;
    sample.velocity = {0, 10};
    sample.metadata.source = EnvironmentalSource::Climatology;
    return sample;
  }
  std::string identity() const override { return "typical-test"; }
};

class ConstantPerformance final : public VesselPerformanceModel {
public:
  bool valid(std::string*) const override { return true; }
  PerformanceCandidate evaluate(PropulsionMode mode, ProfileRole role,
      const std::string& identity, double, double, const WaveSample&) const override {
    PerformanceCandidate value;
    value.valid = true;
    value.mode = mode;
    value.role = role;
    value.profileIdentity = identity;
    value.speedThroughWaterKnots = 5;
    return value;
  }
  std::vector<PerformanceCandidate> candidates(double, double, const WaveSample& waves,
      PropulsionMode mode, Duration) const override {
    return {evaluate(mode, ProfileRole::SailOnly, "test", 10, 90, waves)};
  }
};

class IntervalBoundary final : public LandAndBoundaryProvider {
public:
  TimePoint begins{Duration{1800000600}};
  GeoPoint shallowPoint;
  bool timedHazard{}, shallow{};
  bool pointForbidden(GeoPoint) const override { return false; }
  bool segmentForbidden(GeoPoint, GeoPoint, double) const override { return false; }
  bool validationSegmentFromKnownSafeForbiddenAt(
      GeoPoint a, GeoPoint b, TimePoint time, double) const override {
    return timedHazard && distanceNm(a, b) < 1e-8 &&
        time > begins && time < begins + Duration{600};
  }
  double distanceToForbiddenNm(GeoPoint) const override { return 100; }
  std::optional<double> depthMetres(GeoPoint point) const override {
    return shallow && distanceNm(point, shallowPoint) < 1e-8 ? 1.0 : 100.0;
  }
  std::string identity() const override { return "interval-boundary-test"; }
};

struct Fixture {
  RoutingRequest request;
  std::shared_ptr<IntervalWeather> weather = std::make_shared<IntervalWeather>();
  RoutingEnvironment environment;
  ConstantPerformance performance;
  std::vector<RouteLeg> legs;
  Fixture(Change change = Change::Tack, Duration delay = Duration{600}) {
    request.start = {0, 0};
    request.departure = TimePoint{Duration{1800000000}};
    request.environment.useCurrent = true;
    request.environment.useWaves = true;
    request.environment.climatology = ClimatologyFallbackPolicy::Disallow;
    request.constraints.maximumTrueWindKnots = 20;
    request.constraints.maximumWaveHeightMetres = 3;
    request.options.destinationToleranceNm = .002;
    environment.grib = weather;
    weather->interval = delay;
    RouteLeg first;
    first.start = request.start;
    first.end = destinationPoint(first.start, 90, 5.0 / 6.0);
    first.startTime = request.departure;
    first.endTime = first.startTime + Duration{600};
    first.courseThroughWaterDegrees = first.courseOverGroundDegrees = first.headingDegrees = 90;
    first.speedThroughWaterKnots = first.speedOverGroundKnots = 5;
    first.profileIdentity = "test";
    first.sailPlan = 0;
    legs.push_back(first);
    RouteLeg second = first;
    second.start = first.end;
    second.end = destinationPoint(second.start, 90, 5.0 / 6.0);
    second.startTime = first.endTime;
    second.endTime = second.startTime + delay + Duration{600};
    if (change == Change::Tack) {
      second.tackTransition = true; request.vessel.tackPenalty = delay;
    } else if (change == Change::Gybe) {
      second.gybeTransition = true; request.vessel.gybePenalty = delay;
    } else if (change == Change::Sail) {
      second.sailPlan = 1; request.vessel.sailPlanChangePenalty = delay;
    } else {
      second.propulsionTransition = true;
      second.propulsionMode = PropulsionMode::Motor;
      second.profileRole = ProfileRole::MotorOnly;
      request.vessel.propulsion.allowMotor = true;
      request.vessel.propulsion.modeChangePenalty = delay;
    }
    legs.push_back(second);
    request.destination = second.end;
  }
  RouteValidationResult validate(bool prefix = false) {
    RouteValidator validator;
    return prefix ? validator.validatePrefix(request, environment, performance, legs)
                  : validator.validate(request, environment, performance, legs);
  }
};

class ManeuverValidation : public ::testing::TestWithParam<Change> {};
TEST_P(ManeuverValidation, RejectsWindConfinedToTransitionTime) {
  Fixture f(GetParam()); f.weather->windSpike = true;
  const auto result = f.validate();
  EXPECT_FALSE(result.passed);
  EXPECT_EQ(result.acceptedPrefixLegs, 1U);
  EXPECT_NE(result.failureReason.find("manoeuvre exceeds hard true-wind"), std::string::npos);
  EXPECT_FALSE(f.validate(true).passed);
}
TEST_P(ManeuverValidation, RejectsWavesConfinedToTransitionTime) {
  Fixture f(GetParam()); f.weather->waveSpike = true;
  const auto result = f.validate();
  EXPECT_FALSE(result.passed);
  EXPECT_NE(result.failureReason.find("manoeuvre exceeds hard wave-height"), std::string::npos);
}
TEST_P(ManeuverValidation, AccountsForWholePassageWithoutChangingGeometryOrTime) {
  Fixture f(GetParam());
  const auto result = f.validate();
  ASSERT_TRUE(result.passed) << result.failureReason;
  const auto duration = f.legs.back().endTime - f.request.departure;
  EXPECT_EQ(result.environment.gribWindDuration, duration);
  EXPECT_EQ(result.environment.gribCurrentDuration, duration);
  EXPECT_EQ(result.environment.gribWaveDuration, duration);
  EXPECT_EQ(result.acceptedPrefixLegs, 2U);
  EXPECT_TRUE(std::is_sorted(f.weather->windQueries.begin(), f.weather->windQueries.end()));
}
INSTANTIATE_TEST_SUITE_P(AllTimePenalties, ManeuverValidation,
    ::testing::Values(Change::Tack, Change::Gybe, Change::Sail, Change::Propulsion));

TEST(ManeuverValidationLimits, EnforcesApparentWindAndWindAgainstCurrent) {
  Fixture f; f.weather->windSpike = true;
  f.request.constraints.maximumTrueWindKnots = 100;
  f.request.constraints.maximumApparentWindKnots = 20;
  EXPECT_NE(f.validate().failureReason.find("apparent-wind"), std::string::npos);
  f.request.constraints.maximumApparentWindKnots.reset();
  f.weather->windSpike = false; f.weather->opposingCurrent = true;
  f.request.constraints.maximumOpposingWindCurrent = 25;
  EXPECT_NE(f.validate().failureReason.find("wind-against-current"), std::string::npos);
}
TEST(ManeuverValidationLimits, MissingRequiredWeatherCannotBeSkipped) {
  Fixture f; f.weather->missingWind = true;
  EXPECT_NE(f.validate().failureReason.find("environmental validation failed during manoeuvre"), std::string::npos);
  f.weather->missingWind = false; f.weather->missingWaves = true;
  f.request.environment.missingWaves = MissingWavePolicy::DisallowWhenConstrained;
  EXPECT_FALSE(f.validate().passed);
}
TEST(ManeuverValidationLimits, BoundedSamplesStopBeforeTransitionProviderWork) {
  Fixture f;
  // Ordinary first leg takes four samples. The next manoeuvre cannot fit.
  f.request.limits.maximumValidationSamples = 8;
  const auto result = f.validate();
  EXPECT_FALSE(result.passed);
  EXPECT_EQ(result.failureReason, "validation sample resource limit reached");
  EXPECT_LE(result.samples, f.request.limits.maximumValidationSamples);
  ASSERT_FALSE(f.weather->windQueries.empty());
  EXPECT_LT(f.weather->windQueries.back(), f.weather->begins);
}
TEST(ManeuverValidationLimits, ChecksTimedExclusionsAndDepthAtManeuverPosition) {
  Fixture f;
  auto boundary = std::make_shared<IntervalBoundary>();
  f.environment.landAndBoundaries = boundary;
  boundary->timedHazard = true;
  EXPECT_NE(f.validate().failureReason.find("manoeuvre intersects"), std::string::npos);
  boundary->timedHazard = false; boundary->shallow = true;
  boundary->shallowPoint = f.legs.back().start;
  f.request.constraints.minimumDepthMetres = 3;
  EXPECT_NE(f.validate().failureReason.find("manoeuvre violates hard minimum-depth"), std::string::npos);
}
TEST(ManeuverValidationLimits, AllowsTurningThroughHeadwindWithinWeatherLimits) {
  Fixture f; f.weather->turningHeadwind = true;
  f.request.constraints.minimumTrueWindAngleDegrees = 40;
  const auto result = f.validate();
  EXPECT_TRUE(result.passed) << result.failureReason;
}
TEST(ManeuverValidationUsage, SourceChangeWithinPenaltyRetainsItsTimeAndDuration) {
  Fixture f; f.weather->fallbackDuringInterval = true; f.weather->interval = Duration{300};
  f.environment.climatology = std::make_shared<TypicalWind>();
  f.request.environment.climatology = ClimatologyFallbackPolicy::AllowWithWarning;
  const auto result = f.validate();
  ASSERT_TRUE(result.passed) << result.failureReason;
  EXPECT_EQ(result.environment.climatologyWindDuration, Duration{300});
  EXPECT_EQ(result.environment.gribWindDuration, Duration{1500});
  EXPECT_TRUE(std::any_of(result.sourceTransitions.begin(), result.sourceTransitions.end(),
      [&](const auto& change) { return change.variable == EnvironmentalVariable::Wind &&
          change.newSource == EnvironmentalSource::Climatology && change.time == f.weather->begins; }));
}
TEST(ManeuverValidationUsage, ClimatologyOnlyAndMissingWavesReportWholePassage) {
  Fixture f; f.weather->allClimatology = true; f.weather->allMissingWaves = true;
  f.environment.climatology = std::make_shared<TypicalWind>();
  f.request.environment.climatology = ClimatologyFallbackPolicy::AllowWithWarning;
  f.request.environment.missingWaves = MissingWavePolicy::AllowWithWarning;
  RoutingResult route;
  route.status = RoutingStatus::Complete;
  route.legs = f.legs;
  route.metrics.elapsed = f.legs.back().endTime - f.request.departure;
  route.validation = f.validate();
  ASSERT_TRUE(route.validation.passed) << route.validation.failureReason;
  EXPECT_EQ(route.validation.environment.climatologyWindDuration, route.metrics.elapsed);
  EXPECT_EQ(route.validation.environment.missingWaveDuration, route.metrics.elapsed);
  const auto report = weather_routing::DescribeRoutingResult(route);
  for (const auto& code : {"usage.climatology_wind", "usage.missing_waves"}) {
    const auto item = std::find_if(report.items.begin(), report.items.end(),
        [&](const auto& item) { return item.code == code; });
    ASSERT_NE(item, report.items.end());
    EXPECT_NE(item->detail.Find("100.0%"), wxNOT_FOUND);
  }
}
TEST(ManeuverValidationUsage, OddSecondsAndStationaryWaitsLoseNoTime) {
  for (const auto seconds : {1, 301, 601}) {
    Fixture f(Change::Tack, Duration{seconds});
    const auto result = f.validate();
    ASSERT_TRUE(result.passed) << result.failureReason;
    EXPECT_EQ(result.environment.gribWindDuration, Duration{1200 + seconds});
  }
  Fixture f;
  auto& wait = f.legs.front(); wait.stationaryWait = true; wait.end = wait.start;
  wait.endTime = wait.startTime + Duration{1001};
  f.legs.resize(1); f.request.destination = wait.end;
  f.request.options.allowWaiting = true; f.request.options.maximumWait = Duration{1200};
  const auto result = f.validate();
  ASSERT_TRUE(result.passed) << result.failureReason;
  EXPECT_EQ(result.environment.gribWindDuration, Duration{1001});
}
}  // namespace
