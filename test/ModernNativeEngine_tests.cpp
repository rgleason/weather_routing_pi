#include <gtest/gtest.h>

#include <array>
#include <chrono>
#include <cmath>
#include <future>
#include <limits>
#include <memory>
#include <string>
#include <vector>

#include "engine/native/CoordinateNormalization.h"
#include "engine/native/WeatherCoverage.h"
#include "supercpn/weather_routing/Engine.h"

namespace {
using namespace supercpn::weather_routing;

TEST(ModernNativeAdapterCoordinates,
     NormalizesLegacyWesternPacificLongitudesForEngineInput) {
  // RouteMapConfiguration stores these ordinary western longitudes in its
  // legacy positive-longitude form when both endpoints are west of 90 W.
  const GeoPoint palmerston =
      weather_routing::native::NativeEnginePoint(-18.05, 196.85);
  const GeoPoint niue =
      weather_routing::native::NativeEnginePoint(-19.05, 190.08);

  EXPECT_DOUBLE_EQ(palmerston.latitude, -18.05);
  EXPECT_NEAR(palmerston.longitude, -163.15, 1e-12);
  EXPECT_DOUBLE_EQ(niue.latitude, -19.05);
  EXPECT_NEAR(niue.longitude, -169.92, 1e-12);
  EXPECT_GE(palmerston.longitude, -180.0);
  EXPECT_LE(palmerston.longitude, 180.0);
  EXPECT_GE(niue.longitude, -180.0);
  EXPECT_LE(niue.longitude, 180.0);
}

TEST(ModernNativeAdapterCoordinates, PreservesCanonicalLongitudes) {
  const GeoPoint point =
      weather_routing::native::NativeEnginePoint(53.341, -4.620887);

  EXPECT_DOUBLE_EQ(point.latitude, 53.341);
  EXPECT_NEAR(point.longitude, -4.620887, 1e-12);
}

TimePoint TestTime() { return TimePoint{Duration{1784419200}}; }

PerformanceProfile TestSailingProfile(double speed = 6.0) {
  PerformanceProfile profile;
  profile.role = ProfileRole::SailOnly;
  profile.identity = "native-engine-test";
  profile.rows = {{5.0,
                   {{0.0, 0.0},
                    {35.0, speed * 0.55},
                    {45.0, speed * 0.75},
                    {90.0, speed},
                    {135.0, speed},
                    {180.0, speed * 0.85}}},
                  {15.0,
                   {{0.0, 0.0},
                    {35.0, speed * 0.65},
                    {45.0, speed * 0.9},
                    {90.0, speed * 1.15},
                    {135.0, speed * 1.1},
                    {180.0, speed * 0.95}}}};
  return profile;
}

RoutingRequest TestRequest() {
  RoutingRequest request;
  request.start = {53.341, -4.620887};
  request.destination = {53.311102, -6.122185};
  request.departure = TestTime();
  request.vessel.profiles = {TestSailingProfile()};
  request.vessel.tackPenalty = std::chrono::minutes{5};
  request.vessel.gybePenalty = std::chrono::minutes{5};
  request.environment.missingCurrent = MissingCurrentPolicy::AllowAssumedZero;
  request.environment.zeroCurrentAcknowledged = true;
  request.environment.missingWaves = MissingWavePolicy::AllowWithWarning;
  request.constraints.maximumTrueWindKnots = 50.0;
  request.options.timeStep = std::chrono::minutes{30};
  request.options.minimumTimeStep = std::chrono::minutes{10};
  request.options.headingStepDegrees = 15.0;
  request.options.refinedHeadingStepDegrees = 5.0;
  request.options.maximumSearchAngleDegrees = 180.0;
  request.options.destinationToleranceNm = 0.75;
  request.options.spatialCellNm = 2.0;
  request.options.labelsPerCell = 6;
  request.limits.maximumRouteDuration = std::chrono::hours{72};
  request.limits.maximumGeneratedStates = 500000;
  request.limits.maximumRetainedStates = 100000;
  request.limits.maximumGraphLabels = 100000;
  return request;
}

std::shared_ptr<UniformWeatherProvider> TestWeather() {
  UniformWeatherProvider::Configuration configuration;
  configuration.identity = "irish-sea-uniform";
  configuration.source = EnvironmentalSource::SyntheticTestField;
  configuration.windTowardKnots = speedDirectionToVector(14.0, 140.0);
  configuration.currentTowardKnots = Vector2{};
  WaveSample wave;
  wave.available = true;
  wave.significantHeightMetres = 0.8;
  wave.directionFromDegrees = 300.0;
  wave.periodSeconds = 7.0;
  configuration.wave = wave;
  configuration.begins = TestTime();
  configuration.ends = TestTime() + std::chrono::hours{96};
  return std::make_shared<UniformWeatherProvider>(configuration);
}

RoutingEnvironment TestEnvironment(
    std::shared_ptr<const LandAndBoundaryProvider> boundaries =
        std::make_shared<OpenWaterProvider>()) {
  RoutingEnvironment environment;
  environment.grib = TestWeather();
  environment.landAndBoundaries = std::move(boundaries);
  return environment;
}

class BlockingMeridianProvider final : public LandAndBoundaryProvider {
public:
  bool pointForbidden(GeoPoint) const override { return false; }
  bool segmentForbidden(GeoPoint start, GeoPoint end, double) const override {
    return (start.longitude > -5.5 && end.longitude <= -5.5) ||
           (start.longitude <= -5.5 && end.longitude > -5.5);
  }
  double distanceToForbiddenNm(GeoPoint point) const override {
    return std::abs(point.longitude + 5.5) * 60.0;
  }
  std::string identity() const override { return "blocking-meridian"; }
};

class EndpointDepthProvider final : public LandAndBoundaryProvider {
public:
  EndpointDepthProvider(GeoPoint shallowPoint, double shallowDepthMetres,
                        double otherDepthMetres = 20.0)
      : shallowPoint_(shallowPoint),
        shallowDepthMetres_(shallowDepthMetres),
        otherDepthMetres_(otherDepthMetres) {}

  bool pointForbidden(GeoPoint) const override { return false; }
  bool segmentForbidden(GeoPoint, GeoPoint, double) const override {
    return false;
  }
  double distanceToForbiddenNm(GeoPoint) const override {
    return std::numeric_limits<double>::infinity();
  }
  std::optional<double> depthMetres(GeoPoint point) const override {
    return distanceNm(point, shallowPoint_) < 1e-6 ? shallowDepthMetres_
                                                   : otherDepthMetres_;
  }
  std::string identity() const override { return "endpoint-depth"; }

private:
  GeoPoint shallowPoint_;
  double shallowDepthMetres_;
  double otherDepthMetres_;
};

class MeridianBarrierWithOpenEndsProvider final
    : public LandAndBoundaryProvider {
public:
  MeridianBarrierWithOpenEndsProvider(double longitude, double centreLatitude,
                                      double halfHeightDegrees)
      : longitude_(longitude),
        centreLatitude_(centreLatitude),
        halfHeightDegrees_(halfHeightDegrees) {}

  bool pointForbidden(GeoPoint) const override { return false; }
  bool segmentForbidden(GeoPoint start, GeoPoint end, double) const override {
    const double longitudeDelta = end.longitude - start.longitude;
    if (std::abs(longitudeDelta) < 1e-12) return false;
    const double fraction = (longitude_ - start.longitude) / longitudeDelta;
    if (fraction < 0.0 || fraction > 1.0) return false;
    const double crossingLatitude =
        start.latitude + fraction * (end.latitude - start.latitude);
    return std::abs(crossingLatitude - centreLatitude_) <= halfHeightDegrees_;
  }
  double distanceToForbiddenNm(GeoPoint point) const override {
    return std::abs(point.longitude - longitude_) * 60.0;
  }
  std::string identity() const override {
    return "meridian-barrier-with-open-ends";
  }

private:
  double longitude_;
  double centreLatitude_;
  double halfHeightDegrees_;
};

RoutingRequest GraphDetourRequest(double maximumCorridorWidthNm) {
  auto request = TestRequest();
  request.start = {0.0, 0.0};
  request.destination = {0.0, 0.2};
  request.options.graphHeadingStepDegrees = 10.0;
  request.options.maximumSearchAngleDegrees = 180.0;
  request.options.spatialCellNm = 0.5;
  request.options.forceForwardFailureForTesting = true;
  request.options.forceReverseFailureForTesting = true;
  request.options.graphCorridorWidthNm = 1.0;
  request.options.maximumGraphCorridorWidthNm = maximumCorridorWidthNm;
  // The synthetic field has no favourable current, so this is an admissible
  // A* bound and keeps the recovery-specific regression tests compact.
  request.options.heuristicMaximumSpeedKnots = 20.0;
  request.limits.maximumRouteDuration = std::chrono::hours{6};
  request.limits.maximumGeneratedStates = 100000;
  request.limits.maximumRetainedStates = 30000;
  request.limits.maximumGraphLabels = 50000;
  return request;
}

class ConstantSpeedPerformance final : public VesselPerformanceModel {
public:
  bool valid(std::string*) const override { return true; }
  std::vector<PerformanceCandidate> candidates(double, double,
                                               const WaveSample&,
                                               PropulsionMode,
                                               Duration) const override {
    return {{true, PropulsionMode::Sail, ProfileRole::SailOnly, "constant", 0,
             8.0, 0.0}};
  }
  PerformanceCandidate evaluate(PropulsionMode, ProfileRole, const std::string&,
                                double, double,
                                const WaveSample&) const override {
    return {true, PropulsionMode::Sail, ProfileRole::SailOnly, "constant", 0,
            8.0, 0.0};
  }
  std::vector<PerformanceCandidate> candidatesAt(
      GeoPoint, TimePoint, double, double, const WaveSample& wave,
      PropulsionMode mode, Duration duration) const override {
    return candidates(0.0, 0.0, wave, mode, duration);
  }
  PerformanceCandidate evaluateAt(
      GeoPoint, TimePoint, PropulsionMode mode, ProfileRole role,
      const std::string& identity, double, double,
      const WaveSample& wave) const override {
    return evaluate(mode, role, identity, 0.0, 0.0, wave);
  }
};

class PulsedCurrentWeather final : public WeatherProvider {
public:
  ParameterCoverage windCoverage() const override {
    return {true, TestTime(), TestTime() + std::chrono::hours{4},
            {-180.0, -90.0, 180.0, 90.0}, identity()};
  }
  ParameterCoverage currentCoverage() const override {
    return windCoverage();
  }
  ParameterCoverage waveCoverage() const override {
    return windCoverage();
  }
  WindSample wind(GeoPoint, TimePoint time) const override {
    return {true, speedDirectionToVector(14.0, 0.0),
            {EnvironmentalSource::SyntheticTestField, identity(), identity(),
             {}, time, {}, {}}};
  }
  CurrentSample current(GeoPoint, TimePoint time) const override {
    const auto seconds =
        std::chrono::duration_cast<Duration>(time - TestTime()).count();
    const auto cycleSecond = ((seconds % 300) + 300) % 300;
    // Five knots for the first minute of every five-minute period. A
    // one-minute midpoint replay integrates one knot of mean current, while a
    // five-minute midpoint replay misses every pulse.
    return {true, {cycleSecond < 60 ? 5.0 : 0.0, 0.0},
            {EnvironmentalSource::SyntheticTestField, identity(), identity(),
             {}, time, {}, {}}};
  }
  WaveSample waves(GeoPoint, TimePoint time) const override {
    return {true, 0.5, 0.0, 6.0,
            {EnvironmentalSource::SyntheticTestField, identity(), identity(),
             {}, time, {}, {}}};
  }
  std::string identity() const override { return "pulsed-current"; }
};

RoutingEnvironment GraphDetourEnvironment(
    std::shared_ptr<const LandAndBoundaryProvider> boundaries =
        std::make_shared<OpenWaterProvider>()) {
  auto environment = TestEnvironment(std::move(boundaries));
  environment.performance = std::make_shared<ConstantSpeedPerformance>();
  return environment;
}

class RecordingTimedBoundaryProvider final : public LandAndBoundaryProvider {
public:
  bool pointForbidden(GeoPoint) const override { return false; }
  bool segmentForbidden(GeoPoint, GeoPoint, double) const override {
    ++untimedCalls;
    return false;
  }
  bool segmentFromKnownSafeForbiddenAt(GeoPoint, GeoPoint, TimePoint time,
                                       double) const override {
    observedTimes.push_back(time);
    return false;
  }
  double distanceToForbiddenNm(GeoPoint) const override {
    return std::numeric_limits<double>::infinity();
  }
  std::string identity() const override { return "recording-timed-boundary"; }

  mutable std::vector<TimePoint> observedTimes;
  mutable unsigned untimedCalls{};
};

class SplitSearchValidationProvider final : public LandAndBoundaryProvider {
public:
  bool pointForbidden(GeoPoint) const override { return false; }
  bool segmentForbidden(GeoPoint, GeoPoint, double) const override {
    ++searchCalls;
    return false;
  }
  bool segmentFromKnownSafeForbiddenAt(GeoPoint, GeoPoint, TimePoint,
                                       double) const override {
    ++searchCalls;
    return false;
  }
  bool validationSegmentFromKnownSafeForbiddenAt(GeoPoint, GeoPoint, TimePoint,
                                                 double) const override {
    ++validationCalls;
    validationObservedPreparedRoute = prepareCalls > 0;
    return true;
  }
  void prepareValidationRoute(std::span<const RouteLeg> legs,
                              double safetyMarginNm) const override {
    ++prepareCalls;
    preparedLegs = legs.size();
    preparedSafetyMarginNm = safetyMarginNm;
  }
  bool validationFailureRequiresSearchEscalation() const override {
    return true;
  }
  double distanceToForbiddenNm(GeoPoint) const override {
    return std::numeric_limits<double>::infinity();
  }
  std::string identity() const override {
    return "open-search-blocked-validation";
  }

  mutable unsigned searchCalls{};
  mutable unsigned prepareCalls{};
  mutable unsigned validationCalls{};
  mutable std::size_t preparedLegs{};
  mutable double preparedSafetyMarginNm{};
  mutable bool validationObservedPreparedRoute{};
};

class DestinationValidationBlockProvider final
    : public LandAndBoundaryProvider {
public:
  explicit DestinationValidationBlockProvider(GeoPoint destination)
      : destination_(destination) {}

  bool pointForbidden(GeoPoint) const override { return false; }
  bool segmentForbidden(GeoPoint, GeoPoint, double) const override {
    return false;
  }
  bool validationSegmentFromKnownSafeForbiddenAt(GeoPoint, GeoPoint end,
                                                 TimePoint,
                                                 double) const override {
    return distanceNm(end, destination_) < 0.8;
  }
  double distanceToForbiddenNm(GeoPoint) const override {
    return std::numeric_limits<double>::infinity();
  }
  std::string identity() const override {
    return "destination-validation-block";
  }

private:
  GeoPoint destination_;
};

class CoastalDepartureEgressProvider final : public LandAndBoundaryProvider {
public:
  explicit CoastalDepartureEgressProvider(GeoPoint departure)
      : departure_(departure) {}

  bool pointForbidden(GeoPoint) const override { return false; }
  bool segmentForbidden(GeoPoint, GeoPoint, double) const override {
    return false;
  }
  bool validationSegmentFromKnownSafeForbiddenAt(
      GeoPoint start, GeoPoint, TimePoint,
      double safetyMarginNm) const override {
    // A full-margin chord beginning inside the configured coastal buffer is
    // rejected. The zero-margin egress replay remains clear of actual land.
    return safetyMarginNm > 0.0 && distanceNm(start, departure_) <= 0.002;
  }
  double distanceToForbiddenNm(GeoPoint point) const override {
    return distanceNm(point, departure_) <= 0.002 ? 0.1 : 10.0;
  }
  std::string identity() const override { return "coastal-departure-egress"; }

private:
  GeoPoint departure_;
};

class CurvedCoastalDepartureProvider final : public LandAndBoundaryProvider {
public:
  explicit CurvedCoastalDepartureProvider(GeoPoint departure)
      : departure_(departure) {}

  bool pointForbidden(GeoPoint) const override { return false; }
  bool segmentForbidden(GeoPoint start, GeoPoint end,
                        double safetyMarginNm) const override {
    return segmentFromKnownSafeForbiddenAt(start, end, TestTime(),
                                           safetyMarginNm);
  }
  bool segmentFromKnownSafeForbiddenAt(GeoPoint start, GeoPoint end, TimePoint,
                                       double safetyMarginNm) const override {
    if (safetyMarginNm <= 0.0) return false;
    const GeoPoint midpoint{(start.latitude + end.latitude) / 2.0,
                            (start.longitude + end.longitude) / 2.0};
    const double westNm =
        (departure_.longitude - midpoint.longitude) * 60.0 *
        std::cos(departure_.latitude * 3.14159265358979323846 / 180.0);
    const double northNm =
        (midpoint.latitude - departure_.latitude) * 60.0;
    // The configured stand-off forms a short tongue immediately west of the
    // departure. A safe route must first turn north or south before resuming
    // the direct westerly course. Zero-margin checks remain open water.
    return westNm >= 0.25 && westNm <= 1.5 && std::abs(northNm) <= 0.2;
  }
  bool validationSegmentFromKnownSafeForbiddenAt(
      GeoPoint start, GeoPoint end, TimePoint time,
      double safetyMarginNm) const override {
    return segmentFromKnownSafeForbiddenAt(start, end, time, safetyMarginNm);
  }
  double distanceToForbiddenNm(GeoPoint point) const override {
    const double westNm =
        (departure_.longitude - point.longitude) * 60.0 *
        std::cos(departure_.latitude * 3.14159265358979323846 / 180.0);
    const double northNm = (point.latitude - departure_.latitude) * 60.0;
    if (westNm >= 0.0 && westNm <= 1.5 && std::abs(northNm) <= 0.2)
      return std::max(0.01, 0.1 - westNm * 0.05);
    if (std::abs(northNm) <= 0.3)
      return std::min(10.0, 0.1 + std::abs(northNm) * 1.5);
    return 10.0;
  }
  std::string identity() const override {
    return "curved-coastal-departure";
  }

private:
  GeoPoint departure_;
};

class CoastalDestinationIngressProvider final : public LandAndBoundaryProvider {
public:
  CoastalDestinationIngressProvider(GeoPoint destination,
                                    bool actualLandCrossing = false)
      : destination_(destination), actualLandCrossing_(actualLandCrossing) {}

  bool pointForbidden(GeoPoint) const override { return false; }
  bool segmentForbidden(GeoPoint start, GeoPoint end,
                        double safetyMarginNm) const override {
    return segmentFromKnownSafeForbiddenAt(start, end, TestTime(),
                                           safetyMarginNm);
  }
  bool segmentFromKnownSafeForbiddenAt(GeoPoint, GeoPoint end, TimePoint,
                                       double safetyMarginNm) const override {
    if (actualLandCrossing_ && distanceNm(end, destination_) < 0.2) return true;
    return safetyMarginNm > 0.0 && distanceNm(end, destination_) < 0.5;
  }
  bool validationSegmentFromKnownSafeForbiddenAt(
      GeoPoint start, GeoPoint end, TimePoint time,
      double safetyMarginNm) const override {
    return segmentFromKnownSafeForbiddenAt(start, end, time, safetyMarginNm);
  }
  double distanceToForbiddenNm(GeoPoint point) const override {
    return distanceNm(point, destination_) <= 0.002 ? 0.1 : 10.0;
  }
  std::string identity() const override {
    return "coastal-destination-ingress";
  }

private:
  GeoPoint destination_;
  bool actualLandCrossing_{};
};

class TimeGatePerformance final : public VesselPerformanceModel {
public:
  explicit TimeGatePerformance(TimePoint opens) : opens_(opens) {}

  bool valid(std::string*) const override { return true; }
  std::vector<PerformanceCandidate> candidates(double, double,
                                               const WaveSample&,
                                               PropulsionMode,
                                               Duration) const override {
    return {};
  }
  PerformanceCandidate evaluate(PropulsionMode, ProfileRole, const std::string&,
                                double, double,
                                const WaveSample&) const override {
    return {};
  }
  std::vector<PerformanceCandidate> candidatesAt(GeoPoint, TimePoint time,
                                                 double, double,
                                                 const WaveSample&,
                                                 PropulsionMode,
                                                 Duration) const override {
    if (time < opens_) return {};
    return {{true, PropulsionMode::Sail, ProfileRole::SailOnly, "time-gate", 0,
             6.0, 0.0}};
  }
  PerformanceCandidate evaluateAt(GeoPoint, TimePoint time, PropulsionMode,
                                  ProfileRole, const std::string&, double,
                                  double, const WaveSample&) const override {
    if (time < opens_) return {};
    return {
        true, PropulsionMode::Sail, ProfileRole::SailOnly, "time-gate", 0, 6.0,
        0.0};
  }

private:
  TimePoint opens_;
};

class ReplayEndpointGapProvider final : public LandAndBoundaryProvider {
public:
  explicit ReplayEndpointGapProvider(GeoPoint destination)
      : destination_(destination) {}

  bool pointForbidden(GeoPoint) const override { return false; }
  bool segmentForbidden(GeoPoint, GeoPoint, double) const override {
    return false;
  }
  bool validationSegmentFromKnownSafeForbiddenAt(GeoPoint start, GeoPoint end,
                                                 TimePoint,
                                                 double) const override {
    return distanceNm(end, destination_) <= 0.002 &&
           distanceNm(start, end) < 0.2;
  }
  double distanceToForbiddenNm(GeoPoint) const override {
    return std::numeric_limits<double>::infinity();
  }
  std::string identity() const override { return "replay-endpoint-gap"; }

private:
  GeoPoint destination_;
};

bool Successful(RoutingStatus status) {
  return status == RoutingStatus::Complete ||
         status == RoutingStatus::CompleteUsingReverseRecovery ||
         status == RoutingStatus::CompleteUsingFrontierRecovery ||
         status == RoutingStatus::CompleteUsingGraphFallback;
}

template <typename Clock, typename Duration>
auto ChronoTicks(const std::chrono::time_point<Clock, Duration>& value) {
  return std::chrono::duration_cast<std::chrono::nanoseconds>(
             value.time_since_epoch())
      .count();
}

template <typename Rep, typename Period>
auto ChronoTicks(const std::chrono::duration<Rep, Period>& value) {
  return std::chrono::duration_cast<std::chrono::nanoseconds>(value).count();
}

void ExpectExactlyEquivalent(const RoutingResult& expected,
                             const RoutingResult& actual) {
  EXPECT_EQ(expected.status, actual.status);
  EXPECT_EQ(expected.solverPath, actual.solverPath);
  EXPECT_EQ(expected.message, actual.message);
  EXPECT_EQ(ChronoTicks(expected.metrics.elapsed),
            ChronoTicks(actual.metrics.elapsed));
  EXPECT_DOUBLE_EQ(expected.metrics.distanceNm, actual.metrics.distanceNm);
  EXPECT_EQ(ChronoTicks(expected.metrics.sailingTime),
            ChronoTicks(actual.metrics.sailingTime));
  EXPECT_EQ(ChronoTicks(expected.metrics.motorSailingTime),
            ChronoTicks(actual.metrics.motorSailingTime));
  EXPECT_EQ(ChronoTicks(expected.metrics.motorOnlyTime),
            ChronoTicks(actual.metrics.motorOnlyTime));
  EXPECT_EQ(ChronoTicks(expected.metrics.waitingTime),
            ChronoTicks(actual.metrics.waitingTime));
  EXPECT_EQ(expected.diagnostics.generatedStates,
            actual.diagnostics.generatedStates);
  EXPECT_EQ(expected.diagnostics.retainedStates,
            actual.diagnostics.retainedStates);
  EXPECT_EQ(expected.diagnostics.graphLabels, actual.diagnostics.graphLabels);
  EXPECT_EQ(expected.diagnostics.waitStates, actual.diagnostics.waitStates);
  EXPECT_EQ(expected.validation.passed, actual.validation.passed);
  EXPECT_EQ(expected.validation.failureReason,
            actual.validation.failureReason);
  ASSERT_EQ(expected.legs.size(), actual.legs.size());
  for (std::size_t index = 0; index < expected.legs.size(); ++index) {
    EXPECT_EQ(expected.legs[index].start, actual.legs[index].start);
    EXPECT_EQ(expected.legs[index].end, actual.legs[index].end);
    EXPECT_EQ(ChronoTicks(expected.legs[index].startTime),
              ChronoTicks(actual.legs[index].startTime));
    EXPECT_EQ(ChronoTicks(expected.legs[index].endTime),
              ChronoTicks(actual.legs[index].endTime));
    EXPECT_DOUBLE_EQ(expected.legs[index].headingDegrees,
                     actual.legs[index].headingDegrees);
    EXPECT_DOUBLE_EQ(expected.legs[index].courseThroughWaterDegrees,
                     actual.legs[index].courseThroughWaterDegrees);
    EXPECT_DOUBLE_EQ(expected.legs[index].courseOverGroundDegrees,
                     actual.legs[index].courseOverGroundDegrees);
    EXPECT_DOUBLE_EQ(expected.legs[index].speedThroughWaterKnots,
                     actual.legs[index].speedThroughWaterKnots);
    EXPECT_DOUBLE_EQ(expected.legs[index].speedOverGroundKnots,
                     actual.legs[index].speedOverGroundKnots);
    EXPECT_EQ(expected.legs[index].propulsionMode,
              actual.legs[index].propulsionMode);
    EXPECT_EQ(expected.legs[index].profileIdentity,
              actual.legs[index].profileIdentity);
  }
}

TEST(ModernNativeEngine, RoutesIrishSeaDeterministically) {
  RoutingEngine engine;
  const auto first = engine.route(TestRequest(), TestEnvironment());
  const auto second = engine.route(TestRequest(), TestEnvironment());
  ASSERT_TRUE(Successful(first.status)) << first.message;
  ASSERT_TRUE(first.validation.passed) << first.validation.failureReason;
  EXPECT_EQ(first.validation.acceptedPrefixLegs, first.legs.size());
  EXPECT_EQ(ChronoTicks(first.metrics.elapsed),
            ChronoTicks(second.metrics.elapsed));
  EXPECT_DOUBLE_EQ(first.metrics.distanceNm, second.metrics.distanceNm);
  ASSERT_EQ(first.legs.size(), second.legs.size());
  for (std::size_t index = 0; index < first.legs.size(); ++index) {
    EXPECT_EQ(first.legs[index].start, second.legs[index].start);
    EXPECT_EQ(first.legs[index].end, second.legs[index].end);
    EXPECT_EQ(ChronoTicks(first.legs[index].startTime),
              ChronoTicks(second.legs[index].startTime));
    EXPECT_EQ(ChronoTicks(first.legs[index].endTime),
              ChronoTicks(second.legs[index].endTime));
    EXPECT_DOUBLE_EQ(first.legs[index].headingDegrees,
                     second.legs[index].headingDegrees);
    EXPECT_DOUBLE_EQ(first.legs[index].courseOverGroundDegrees,
                     second.legs[index].courseOverGroundDegrees);
  }
  ASSERT_FALSE(first.legs.empty());
  EXPECT_EQ(first.legs.back().end, TestRequest().destination);
}

TEST(ModernNativeEngine,
     EightConcurrentCandidatesExactlyMatchTheirSerialReferences) {
  constexpr int kCandidateCount = 8;
  std::array<RoutingRequest, kCandidateCount> requests;
  for (int index = 0; index < kCandidateCount; ++index) {
    requests[index] = TestRequest();
    requests[index].destination =
        destinationPoint(requests[index].start, 260.0 + index, 18.0);
    requests[index].departure =
        TestTime() + std::chrono::minutes{index * 15};
    requests[index].options.retryStages = 1;
    requests[index].options.useReverseRecovery = false;
    requests[index].options.useGraphFallback = false;
    requests[index].limits.maximumRouteDuration = std::chrono::hours{12};
    requests[index].limits.maximumGeneratedStates = 100000;
    requests[index].limits.maximumRetainedStates = 30000;
  }

  std::array<RoutingResult, kCandidateCount> serial;
  for (int index = 0; index < kCandidateCount; ++index)
    serial[index] =
        RoutingEngine{}.route(requests[index], TestEnvironment());

  std::array<std::future<RoutingResult>, kCandidateCount> futures;
  for (int index = 0; index < kCandidateCount; ++index) {
    futures[index] = std::async(
        std::launch::async, [request = requests[index]] {
          return RoutingEngine{}.route(request, TestEnvironment());
        });
  }

  for (int index = 0; index < kCandidateCount; ++index) {
    const RoutingResult concurrent = futures[index].get();
    ASSERT_TRUE(Successful(serial[index].status)) << serial[index].message;
    ExpectExactlyEquivalent(serial[index], concurrent);
  }
}

TEST(ModernNativeEngine, IndependentlyValidatesIncompleteRecoveryPrefixes) {
  const RoutingRequest request = TestRequest();
  const RoutingEnvironment environment = TestEnvironment();
  const auto route = RoutingEngine{}.route(request, environment);
  ASSERT_TRUE(Successful(route.status)) << route.message;
  ASSERT_GT(route.legs.size(), 2U);
  const std::span<const RouteLeg> prefix(route.legs.data(),
                                         route.legs.size() / 2);
  PolarPerformanceModel performance(request.vessel);

  const auto completeValidation =
      RouteValidator{}.validate(request, environment, performance, prefix);
  const auto prefixValidation = RouteValidator{}.validatePrefix(
      request, environment, performance, prefix);

  EXPECT_FALSE(completeValidation.passed);
  EXPECT_TRUE(prefixValidation.passed) << prefixValidation.failureReason;
  EXPECT_EQ(prefixValidation.acceptedPrefixLegs, prefix.size());
}

TEST(ModernNativeEngine,
     DenseReplayAllowsBoundedExploratoryIntegrationDifference) {
  auto request = TestRequest();
  request.destination = destinationPoint(request.start, 270.0, 6.1);
  RouteLeg leg;
  leg.start = request.start;
  leg.end = request.destination;
  leg.startTime = request.departure;
  leg.endTime = request.departure + std::chrono::hours{1};
  leg.headingDegrees = 270.0;
  leg.courseThroughWaterDegrees = 270.0;
  leg.courseOverGroundDegrees = 270.0;
  leg.speedThroughWaterKnots = 6.0;
  leg.speedOverGroundKnots = 6.1;
  leg.propulsionMode = PropulsionMode::Sail;
  leg.profileRole = ProfileRole::SailOnly;
  leg.profileIdentity = "time-gate";
  TimeGatePerformance performance(request.departure);

  const auto validation =
      RouteValidator{}.validate(request, TestEnvironment(), performance,
                                std::span<const RouteLeg>(&leg, 1));

  EXPECT_TRUE(validation.passed) << validation.failureReason;
  EXPECT_EQ(validation.acceptedPrefixLegs, 1U);
}

TEST(ModernNativeEngine,
     DenseReplayNeverLeavesAnUncheckedEndpointReconciliationGap) {
  auto request = TestRequest();
  request.destination = destinationPoint(request.start, 270.0, 6.1);
  RouteLeg leg;
  leg.start = request.start;
  leg.end = request.destination;
  leg.startTime = request.departure;
  leg.endTime = request.departure + std::chrono::hours{1};
  leg.headingDegrees = 270.0;
  leg.courseThroughWaterDegrees = 270.0;
  leg.courseOverGroundDegrees = 270.0;
  leg.speedThroughWaterKnots = 6.0;
  leg.speedOverGroundKnots = 6.1;
  leg.propulsionMode = PropulsionMode::Sail;
  leg.profileRole = ProfileRole::SailOnly;
  leg.profileIdentity = "time-gate";
  TimeGatePerformance performance(request.departure);
  auto boundaries =
      std::make_shared<ReplayEndpointGapProvider>(request.destination);

  const auto validation = RouteValidator{}.validate(
      request, TestEnvironment(boundaries), performance,
      std::span<const RouteLeg>(&leg, 1));

  EXPECT_FALSE(validation.passed);
  EXPECT_NE(validation.failureReason.find("endpoint reconciliation"),
            std::string::npos);
}

TEST(ModernNativeEngine, ReplayAccountsForSailPlanChangePenalty) {
  auto request = TestRequest();
  request.vessel.sailPlanChangePenalty = std::chrono::minutes{5};
  request.destination = destinationPoint(
      destinationPoint(request.start, 270.0, 4.0), 270.0, 10.0 / 3.0);
  std::array<RouteLeg, 2> legs;
  legs[0].start = request.start;
  legs[0].end = destinationPoint(request.start, 270.0, 4.0);
  legs[0].startTime = request.departure;
  legs[0].endTime = request.departure + std::chrono::minutes{30};
  legs[0].courseThroughWaterDegrees = 270.0;
  legs[0].propulsionMode = PropulsionMode::Sail;
  legs[0].profileRole = ProfileRole::SailOnly;
  legs[0].profileIdentity = "constant";
  legs[0].sailPlan = 0;
  legs[1] = legs[0];
  legs[1].start = legs[0].end;
  legs[1].end = request.destination;
  legs[1].startTime = legs[0].endTime;
  legs[1].endTime = legs[1].startTime + std::chrono::minutes{30};
  legs[1].sailPlan = 1;

  const auto validation =
      RouteValidator{}.validate(request, TestEnvironment(),
                                ConstantSpeedPerformance{}, legs);

  EXPECT_TRUE(validation.passed) << validation.failureReason;
}

TEST(ModernNativeEngine, ReplayPreservesSearchIntegrationCadence) {
  auto request = TestRequest();
  request.start = {0.0, 0.0};
  request.destination = destinationPoint(request.start, 90.0, 9.0);
  request.departure = TestTime();
  RouteLeg leg;
  leg.start = request.start;
  leg.end = request.destination;
  leg.startTime = request.departure;
  leg.endTime = request.departure + std::chrono::hours{1};
  leg.courseThroughWaterDegrees = 90.0;
  leg.propulsionMode = PropulsionMode::Sail;
  leg.profileRole = ProfileRole::SailOnly;
  leg.profileIdentity = "constant";
  leg.sailPlan = 0;
  leg.integrationMaximumSlice = std::chrono::minutes{1};
  RoutingEnvironment environment;
  environment.grib = std::make_shared<PulsedCurrentWeather>();
  environment.landAndBoundaries = std::make_shared<OpenWaterProvider>();

  const auto validation = RouteValidator{}.validate(
      request, environment, ConstantSpeedPerformance{},
      std::span<const RouteLeg>(&leg, 1));

  EXPECT_TRUE(validation.passed) << validation.failureReason;
}

TEST(ModernNativeEngine, RejectsAChartBlockedPassage) {
  auto request = TestRequest();
  request.options.maximumSearchAngleDegrees = 80.0;
  request.options.useReverseRecovery = false;
  request.options.useGraphFallback = false;
  const auto result = RoutingEngine{}.route(
      request, TestEnvironment(std::make_shared<BlockingMeridianProvider>()));
  EXPECT_FALSE(Successful(result.status));
}

TEST(ModernNativeEngine, UsesAuthoritativeValidationProviderHook) {
  auto request = TestRequest();
  request.destination = destinationPoint(request.start, 270.0, 8.0);
  request.options.retryStages = 1;
  request.options.useReverseRecovery = false;
  request.options.useGraphFallback = false;
  request.limits.maximumRouteDuration = std::chrono::hours{12};
  auto boundaries = std::make_shared<SplitSearchValidationProvider>();

  const auto result =
      RoutingEngine{}.route(request, TestEnvironment(boundaries));

  EXPECT_FALSE(Successful(result.status));
  EXPECT_GT(boundaries->searchCalls, 0U);
  EXPECT_GT(boundaries->prepareCalls, 0U);
  EXPECT_GT(boundaries->preparedLegs, 0U);
  EXPECT_DOUBLE_EQ(boundaries->preparedSafetyMarginNm,
                   request.constraints.landSafetyMarginNm);
  EXPECT_TRUE(boundaries->validationObservedPreparedRoute);
  EXPECT_GT(boundaries->validationCalls, 0U);
  EXPECT_EQ(result.status, RoutingStatus::ValidationFailure);
  EXPECT_NE(result.message.find("detailed-constraint search escalation"),
            std::string::npos);
}

TEST(ModernNativeEngine, SafeRejectedPrefixSkipsWholeRouteRefinement) {
  auto request = TestRequest();
  request.options.useReverseRecovery = false;
  request.limits.maximumGeneratedStates = 500000;
  request.limits.maximumGraphLabels = 1000;
  auto boundaries =
      std::make_shared<DestinationValidationBlockProvider>(request.destination);

  const auto result =
      RoutingEngine{}.route(request, TestEnvironment(boundaries));

  EXPECT_FALSE(Successful(result.status));
  EXPECT_EQ(std::count(result.diagnostics.stagesAttempted.begin(),
                       result.diagnostics.stagesAttempted.end(),
                       SolverPath::AdaptiveIsochrone),
            1);
  EXPECT_TRUE(std::any_of(
      result.diagnostics.stageStopReasons.begin(),
      result.diagnostics.stageStopReasons.end(), [](const std::string& reason) {
        return reason.find("safe recovery prefix") != std::string::npos;
      }));
}

TEST(ModernNativeEngine, AuthoritativeReplayAllowsSafeCoastalDepartureEgress) {
  auto request = TestRequest();
  request.destination = destinationPoint(request.start, 270.0, 8.0);
  request.constraints.landSafetyMarginNm = 0.4;
  request.options.retryStages = 1;
  request.options.useReverseRecovery = false;
  request.options.useGraphFallback = false;
  request.limits.maximumRouteDuration = std::chrono::hours{12};
  auto boundaries =
      std::make_shared<CoastalDepartureEgressProvider>(request.start);

  const auto result =
      RoutingEngine{}.route(request, TestEnvironment(boundaries));

  ASSERT_TRUE(Successful(result.status)) << result.message;
  EXPECT_TRUE(result.validation.passed) << result.validation.failureReason;
  EXPECT_EQ(result.validation.acceptedPrefixLegs, result.legs.size());
}

TEST(ModernNativeEngine,
     CoastalDepartureUsesFineAuthoritativeEgressBeforeOffshoreSearch) {
  auto request = TestRequest();
  request.destination = destinationPoint(request.start, 270.0, 30.0);
  request.vessel.propulsion.allowSailing = false;
  request.vessel.propulsion.allowMotor = true;
  request.vessel.propulsion.configuredMotorSpeedKnots = 6.0;
  request.constraints.landSafetyMarginNm = 0.4;
  request.options.timeStep = std::chrono::hours{1};
  request.options.minimumTimeStep = std::chrono::minutes{10};
  request.options.adaptiveTimeStep = false;
  request.options.retryStages = 1;
  request.options.useReverseRecovery = false;
  request.options.useFrontierRecovery = false;
  request.options.useGraphFallback = false;
  request.limits.maximumRouteDuration = std::chrono::hours{12};
  request.limits.maximumCoastalEndpointGeneratedStates = 500;
  auto boundaries =
      std::make_shared<CurvedCoastalDepartureProvider>(request.start);

  const auto result =
      RoutingEngine{}.route(request, TestEnvironment(boundaries));

  ASSERT_TRUE(Successful(result.status)) << result.message;
  ASSERT_FALSE(result.legs.empty());
  EXPECT_TRUE(result.validation.passed) << result.validation.failureReason;
  EXPECT_TRUE(result.legs.front().coastalDepartureEgress);
  EXPECT_TRUE(std::any_of(result.legs.begin(), result.legs.end(),
                          [](const RouteLeg& leg) {
                            return !leg.coastalDepartureEgress;
                          }));
  bool fullMarginResumed = false;
  for (const RouteLeg& leg : result.legs) {
    if (!leg.coastalDepartureEgress) fullMarginResumed = true;
    EXPECT_FALSE(fullMarginResumed && leg.coastalDepartureEgress)
        << "coastal departure permission reactivated after full margin";
  }
  EXPECT_LE(result.legs.front().endTime - result.legs.front().startTime,
            request.options.minimumTimeStep);
  EXPECT_GT(result.diagnostics.coastalEndpointGeneratedStates, 0U);
  EXPECT_LE(result.diagnostics.coastalEndpointGeneratedStates,
            request.limits.maximumCoastalEndpointGeneratedStates);
}

TEST(ModernNativeEngine, CoastalDepartureEgressHasAnIndependentStateLimit) {
  auto request = TestRequest();
  request.destination = destinationPoint(request.start, 270.0, 30.0);
  request.constraints.landSafetyMarginNm = 0.4;
  request.options.timeStep = std::chrono::hours{1};
  request.options.minimumTimeStep = std::chrono::minutes{10};
  request.options.retryStages = 1;
  request.options.useReverseRecovery = false;
  request.options.useFrontierRecovery = false;
  request.options.useGraphFallback = false;
  request.limits.maximumCoastalEndpointGeneratedStates = 1;
  auto boundaries =
      std::make_shared<CurvedCoastalDepartureProvider>(request.start);

  const auto result =
      RoutingEngine{}.route(request, TestEnvironment(boundaries));

  EXPECT_EQ(result.status, RoutingStatus::ResourceLimitReached);
  EXPECT_EQ(result.diagnostics.coastalEndpointGeneratedStates, 1U);
  EXPECT_NE(result.message.find("coastal endpoint generated-state budget"),
            std::string::npos);
}

TEST(ModernNativeEngine,
     AuthoritativeReplayAllowsSafeCoastalDestinationIngress) {
  auto request = TestRequest();
  request.destination = destinationPoint(request.start, 270.0, 8.0);
  request.constraints.landSafetyMarginNm = 0.4;
  request.options.retryStages = 1;
  request.options.useReverseRecovery = false;
  request.options.useGraphFallback = false;
  request.limits.maximumRouteDuration = std::chrono::hours{12};
  auto boundaries =
      std::make_shared<CoastalDestinationIngressProvider>(request.destination);

  const auto result =
      RoutingEngine{}.route(request, TestEnvironment(boundaries));

  ASSERT_TRUE(Successful(result.status)) << result.message;
  EXPECT_TRUE(result.validation.passed) << result.validation.failureReason;
  EXPECT_EQ(result.validation.acceptedPrefixLegs, result.legs.size());
}

TEST(ModernNativeEngine, CoastalDestinationIngressNeverPermitsLandCrossing) {
  auto request = TestRequest();
  request.destination = destinationPoint(request.start, 270.0, 8.0);
  request.constraints.landSafetyMarginNm = 0.4;
  request.options.retryStages = 1;
  request.options.useReverseRecovery = false;
  request.options.useGraphFallback = false;
  request.limits.maximumRouteDuration = std::chrono::hours{12};
  auto boundaries = std::make_shared<CoastalDestinationIngressProvider>(
      request.destination, true);

  const auto result =
      RoutingEngine{}.route(request, TestEnvironment(boundaries));

  EXPECT_FALSE(Successful(result.status));
}

TEST(ModernNativeEngine, SuppliesChronologicalTimesToDynamicBoundaries) {
  auto boundaries = std::make_shared<RecordingTimedBoundaryProvider>();
  const auto result =
      RoutingEngine{}.route(TestRequest(), TestEnvironment(boundaries));
  ASSERT_TRUE(Successful(result.status)) << result.message;
  ASSERT_FALSE(boundaries->observedTimes.empty());
  EXPECT_TRUE(std::all_of(boundaries->observedTimes.begin(),
                          boundaries->observedTimes.end(),
                          [](TimePoint time) { return time >= TestTime(); }));
  EXPECT_TRUE(std::any_of(boundaries->observedTimes.begin(),
                          boundaries->observedTimes.end(),
                          [](TimePoint time) { return time > TestTime(); }));
}

TEST(ModernNativeEngine, UsesRecoveryCascadeAndIndependentValidation) {
  auto request = TestRequest();
  request.options.forceForwardFailureForTesting = true;
  const auto result = RoutingEngine{}.route(request, TestEnvironment());
  ASSERT_TRUE(Successful(result.status)) << result.message;
  EXPECT_NE(result.solverPath, SolverPath::AdaptiveIsochrone);
  EXPECT_TRUE(result.validation.passed);
  EXPECT_GT(result.diagnostics.validationSamples, 0U);
}

TEST(ModernNativeEngine,
     ReverseRecoveryReplaysForwardWithChronologicalEnvironmentTimes) {
  auto request = TestRequest();
  request.options.forceForwardFailureForTesting = true;
  request.options.useReverseRecovery = true;
  request.options.useFrontierRecovery = false;
  request.options.useGraphFallback = false;
  request.options.retryStages = 6;
  request.limits.maximumForwardGeneratedStates = 500000;
  request.limits.maximumReverseCandidates = 512;
  request.limits.maximumReverseBridgeAttempts = 4096;
  auto boundaries = std::make_shared<RecordingTimedBoundaryProvider>();

  const auto result =
      RoutingEngine{}.route(request, TestEnvironment(boundaries));

  ASSERT_EQ(result.status, RoutingStatus::CompleteUsingReverseRecovery)
      << result.message;
  EXPECT_EQ(result.solverPath, SolverPath::ReverseRecovery);
  EXPECT_TRUE(result.validation.passed) << result.validation.failureReason;
  EXPECT_GT(result.diagnostics.reverseNodes, 0U);
  EXPECT_GT(result.diagnostics.reverseCandidateBridges, 0U);
  ASSERT_FALSE(result.legs.empty());
  EXPECT_EQ(ChronoTicks(result.legs.front().startTime),
            ChronoTicks(request.departure));
  for (std::size_t index = 0; index < result.legs.size(); ++index) {
    const RouteLeg& leg = result.legs[index];
    EXPECT_LT(ChronoTicks(leg.startTime), ChronoTicks(leg.endTime));
    if (index > 0) {
      EXPECT_EQ(ChronoTicks(result.legs[index - 1].endTime),
                ChronoTicks(leg.startTime));
    }
  }
  EXPECT_LE(ChronoTicks(result.legs.back().endTime),
            ChronoTicks(request.departure +
                        request.limits.maximumRouteDuration));
  ASSERT_FALSE(boundaries->observedTimes.empty());
  EXPECT_TRUE(std::all_of(boundaries->observedTimes.begin(),
                          boundaries->observedTimes.end(),
                          [&](TimePoint time) {
                            return time >= request.departure &&
                                   time <= request.departure +
                                               request.limits.maximumRouteDuration;
                          }));
  EXPECT_TRUE(std::any_of(boundaries->observedTimes.begin(),
                          boundaries->observedTimes.end(),
                          [&](TimePoint time) {
                            return time > request.departure;
                          }));
}

TEST(ModernNativeEngine, BoundsReverseRecoveryIndependently) {
  auto request = TestRequest();
  request.options.forceForwardFailureForTesting = true;
  request.options.useFrontierRecovery = false;
  request.options.useGraphFallback = false;
  request.limits.maximumForwardGeneratedStates = 50000;
  request.limits.maximumReverseCandidates = 1;
  request.limits.maximumReverseBridgeAttempts = 1;
  const auto result = RoutingEngine{}.route(
      request, TestEnvironment(std::make_shared<BlockingMeridianProvider>()));

  EXPECT_FALSE(Successful(result.status));
  EXPECT_EQ(result.diagnostics.reverseNodes, 1U);
  EXPECT_EQ(result.diagnostics.reverseCandidateBridges, 1U);
  EXPECT_TRUE(std::any_of(
      result.diagnostics.stageStopReasons.begin(),
      result.diagnostics.stageStopReasons.end(), [](const std::string& reason) {
        return reason.find("reverse bridge integration budget exhausted") !=
               std::string::npos;
      }));
}

TEST(ModernNativeEngine, FrontierRecoveryUsesPreservedForwardLineages) {
  auto request = TestRequest();
  request.destination = destinationPoint(request.start, 270.0, 12.0);
  request.options.forceForwardFailureForTesting = true;
  request.options.forceReverseFailureForTesting = true;
  request.options.useGraphFallback = false;
  request.limits.maximumForwardGeneratedStates = 50000;
  request.limits.maximumFrontierRecoveryGeneratedStates = 300000;
  request.limits.maximumFrontierRecoveryLabels = 100000;
  const auto result = RoutingEngine{}.route(request, TestEnvironment());

  ASSERT_TRUE(Successful(result.status))
      << result.message << " generated=" << result.diagnostics.generatedStates
      << " closest=" << result.diagnostics.closestApproachNm;
  EXPECT_EQ(result.solverPath, SolverPath::FrontierRecovery);
  EXPECT_GT(result.diagnostics.frontierRecoveryGeneratedStates, 0U);
  EXPECT_GT(result.diagnostics.frontierRecoveryLabels, 0U);
  EXPECT_LE(result.diagnostics.frontierRecoveryGeneratedStates,
            request.limits.maximumFrontierRecoveryGeneratedStates);
  EXPECT_TRUE(result.validation.passed) << result.validation.failureReason;
}

TEST(ModernNativeEngine, HigherEffortRetainsExactLowerTierSuccess) {
  const auto baseline =
      RoutingEngine{}.route(TestRequest(), TestEnvironment());
  ASSERT_TRUE(Successful(baseline.status)) << baseline.message;

  auto expandedRequest = TestRequest();
  expandedRequest.options.routingEffortPercent = 400;
  expandedRequest.limits.maximumGeneratedStates *= 4;
  expandedRequest.limits.maximumRetainedStates *= 4;
  expandedRequest.limits.maximumGraphLabels *= 4;
  const auto expanded =
      RoutingEngine{}.route(expandedRequest, TestEnvironment());

  ASSERT_TRUE(Successful(expanded.status)) << expanded.message;
  ASSERT_EQ(expanded.diagnostics.effortTiersAttempted,
            std::vector<unsigned>({100U}));
  EXPECT_EQ(expanded.diagnostics.completedEffortPercent, 100U);
  EXPECT_EQ(ChronoTicks(expanded.metrics.elapsed),
            ChronoTicks(baseline.metrics.elapsed));
  EXPECT_DOUBLE_EQ(expanded.metrics.distanceNm, baseline.metrics.distanceNm);
  ASSERT_EQ(expanded.legs.size(), baseline.legs.size());
  for (std::size_t index = 0; index < baseline.legs.size(); ++index) {
    EXPECT_EQ(expanded.legs[index].start, baseline.legs[index].start);
    EXPECT_EQ(expanded.legs[index].end, baseline.legs[index].end);
    EXPECT_EQ(ChronoTicks(expanded.legs[index].startTime),
              ChronoTicks(baseline.legs[index].startTime));
    EXPECT_EQ(ChronoTicks(expanded.legs[index].endTime),
              ChronoTicks(baseline.legs[index].endTime));
  }
}

TEST(ModernNativeEngine, GraphFallbackStaysInFastCorridorWhenItCanSolve) {
  auto request = GraphDetourRequest(
      std::numeric_limits<double>::infinity());
  request.options.graphCorridorWidthNm = 5.0;
  const auto result = RoutingEngine{}.route(request, GraphDetourEnvironment());

  ASSERT_TRUE(Successful(result.status))
      << result.message << " generated=" << result.diagnostics.generatedStates
      << " labels=" << result.diagnostics.graphLabels
      << " closest=" << result.diagnostics.closestApproachNm;
  ASSERT_EQ(result.solverPath, SolverPath::GraphFallback);
  ASSERT_EQ(result.diagnostics.graphCorridorWidthsNm.size(), 1U);
  EXPECT_DOUBLE_EQ(result.diagnostics.graphCorridorWidthsNm.front(), 5.0);
}

TEST(ModernNativeEngine, GraphFallbackWidensToReachSafeOffCourseRoute) {
  auto request = GraphDetourRequest(5.0);
  auto boundaries = std::make_shared<MeridianBarrierWithOpenEndsProvider>(
      (request.start.longitude + request.destination.longitude) / 2.0,
      (request.start.latitude + request.destination.latitude) / 2.0, 0.03);
  const auto result =
      RoutingEngine{}.route(request, GraphDetourEnvironment(boundaries));

  ASSERT_TRUE(Successful(result.status))
      << result.message << " generated=" << result.diagnostics.generatedStates
      << " labels=" << result.diagnostics.graphLabels
      << " closest=" << result.diagnostics.closestApproachNm;
  ASSERT_EQ(result.solverPath, SolverPath::GraphFallback);
  ASSERT_GE(result.diagnostics.graphCorridorWidthsNm.size(), 2U);
  EXPECT_DOUBLE_EQ(result.diagnostics.graphCorridorWidthsNm.front(), 1.0);
  EXPECT_GT(result.diagnostics.graphCorridorWidthsNm.back(), 1.0);
  EXPECT_LE(result.diagnostics.graphCorridorWidthsNm.back(), 5.0);
  double maximumCrossTrackNm = 0.0;
  for (const auto& leg : result.legs)
    maximumCrossTrackNm =
        std::max(maximumCrossTrackNm,
                 std::abs(crossTrackDistanceNm(request.start,
                                               request.destination, leg.end)));
  EXPECT_GT(maximumCrossTrackNm, 1.5);
  EXPECT_TRUE(result.validation.passed) << result.validation.failureReason;
}

TEST(ModernNativeEngine, UnboundedFinalGraphStageRemovesGraphOnlyLimit) {
  auto request =
      GraphDetourRequest(std::numeric_limits<double>::infinity());
  auto boundaries = std::make_shared<MeridianBarrierWithOpenEndsProvider>(
      (request.start.longitude + request.destination.longitude) / 2.0,
      (request.start.latitude + request.destination.latitude) / 2.0, 0.055);
  const auto result =
      RoutingEngine{}.route(request, GraphDetourEnvironment(boundaries));

  ASSERT_TRUE(Successful(result.status))
      << result.message << " generated=" << result.diagnostics.generatedStates
      << " labels=" << result.diagnostics.graphLabels
      << " closest=" << result.diagnostics.closestApproachNm;
  ASSERT_EQ(result.solverPath, SolverPath::GraphFallback);
  ASSERT_EQ(result.diagnostics.graphCorridorWidthsNm.size(), 4U);
  EXPECT_DOUBLE_EQ(result.diagnostics.graphCorridorWidthsNm[0], 1.0);
  EXPECT_DOUBLE_EQ(result.diagnostics.graphCorridorWidthsNm[1], 2.0);
  EXPECT_DOUBLE_EQ(result.diagnostics.graphCorridorWidthsNm[2], 4.0);
  EXPECT_TRUE(std::isinf(result.diagnostics.graphCorridorWidthsNm[3]));
  EXPECT_TRUE(std::any_of(
      result.diagnostics.stageStopReasons.begin(),
      result.diagnostics.stageStopReasons.end(), [](const std::string& reason) {
        return reason.find("forward isochrone focused corridor") !=
               std::string::npos;
      }));
  double maximumCrossTrackNm = 0.0;
  for (const auto& leg : result.legs)
    maximumCrossTrackNm =
        std::max(maximumCrossTrackNm,
                 std::abs(crossTrackDistanceNm(request.start,
                                               request.destination, leg.end)));
  EXPECT_GT(maximumCrossTrackNm, 2.8);
  EXPECT_TRUE(result.validation.passed) << result.validation.failureReason;
}

TEST(ModernNativeEngine, FavourableCurrentMayExceedPolarSpeed) {
  auto weather = TestWeather();
  UniformWeatherProvider::Configuration configuration;
  configuration.identity = "strong-current";
  configuration.source = EnvironmentalSource::SyntheticTestField;
  configuration.windTowardKnots = speedDirectionToVector(14.0, 140.0);
  configuration.currentTowardKnots = speedDirectionToVector(5.0, 270.0);
  configuration.begins = TestTime();
  configuration.ends = TestTime() + std::chrono::hours{96};
  RoutingEnvironment environment;
  environment.grib =
      std::make_shared<UniformWeatherProvider>(std::move(configuration));
  environment.landAndBoundaries = std::make_shared<OpenWaterProvider>();
  const auto result = RoutingEngine{}.route(TestRequest(), environment);
  ASSERT_TRUE(Successful(result.status)) << result.message;
  EXPECT_GT(result.metrics.distanceNm * 3600.0 /
                static_cast<double>(result.metrics.elapsed.count()),
            6.0);
}

TEST(ModernNativeEngine, WaitsWithinBoundForAWeatherOrTidalGate) {
  auto request = TestRequest();
  request.options.useReverseRecovery = false;
  request.options.useGraphFallback = false;
  request.options.maximumWait = std::chrono::hours{2};
  RoutingEnvironment environment = TestEnvironment();
  environment.performance = std::make_shared<TimeGatePerformance>(
      request.departure + std::chrono::hours{1});
  const auto result = RoutingEngine{}.route(request, environment);
  ASSERT_TRUE(Successful(result.status)) << result.message;
  ASSERT_TRUE(result.validation.passed) << result.validation.failureReason;
  EXPECT_GE(ChronoTicks(result.metrics.waitingTime),
            ChronoTicks(std::chrono::hours{1}));
  EXPECT_GT(result.diagnostics.waitStates, 0U);
  EXPECT_TRUE(
      std::any_of(result.legs.begin(), result.legs.end(),
                  [](const RouteLeg& leg) { return leg.stationaryWait; }));
}

TEST(ModernNativeEngine, RejectsGateBeyondConfiguredWaitingBound) {
  auto request = TestRequest();
  request.options.useReverseRecovery = false;
  request.options.useGraphFallback = false;
  request.options.maximumWait = std::chrono::minutes{30};
  RoutingEnvironment environment = TestEnvironment();
  environment.performance = std::make_shared<TimeGatePerformance>(
      request.departure + std::chrono::hours{2});
  const auto result = RoutingEngine{}.route(request, environment);
  EXPECT_FALSE(Successful(result.status));
}

TEST(ModernNativeEngine, RejectsShallowDestinationBeforeSearch) {
  auto request = TestRequest();
  request.constraints.minimumDepthMetres = 1.7;
  const auto boundaries = std::make_shared<EndpointDepthProvider>(
      request.destination, 1.2);

  const auto result =
      RoutingEngine{}.route(request, TestEnvironment(boundaries));

  EXPECT_EQ(result.status, RoutingStatus::InvalidDestination);
  EXPECT_EQ(result.diagnostics.generatedStates, 0U);
  EXPECT_NE(result.message.find("destination depth 1.2 m"),
            std::string::npos);
  EXPECT_NE(result.message.find("configured minimum 1.7 m"),
            std::string::npos);
}

TEST(ModernNativeEngine, FinalApproachMayOutlastSeveralSearchSteps) {
  auto request = TestRequest();
  request.destination = destinationPoint(request.start, 270.0, 2.2);
  request.vessel.profiles = {TestSailingProfile(3.0)};
  request.options.timeStep = std::chrono::minutes{10};
  request.options.minimumTimeStep = std::chrono::minutes{10};
  request.options.destinationToleranceNm = 0.35;
  request.options.useReverseRecovery = false;
  request.options.useGraphFallback = false;
  request.limits.maximumGeneratedStates = 20;

  const auto result = RoutingEngine{}.route(request, TestEnvironment());
  ASSERT_TRUE(Successful(result.status)) << result.message;
  ASSERT_TRUE(result.validation.passed) << result.validation.failureReason;
  ASSERT_FALSE(result.legs.empty());
  EXPECT_EQ(result.legs.back().end, request.destination);
  EXPECT_GT(ChronoTicks(result.metrics.elapsed),
            ChronoTicks(std::chrono::minutes{30}));
  EXPECT_LE(result.diagnostics.generatedStates, 20U);
}

}  // namespace

TEST(ModernNativeWeatherCoverage, IntersectsWindComponentsAndNormalizesLongitude) {
  using weather_routing::native::WindGridCoverage;
  const auto coverage = WindGridCoverage(185, -20, 187, -18, .1,
                                         -174, -19, -172, -17, .1);
  ASSERT_TRUE(coverage.available);
  EXPECT_DOUBLE_EQ(coverage.area.west, -174);
  EXPECT_DOUBLE_EQ(coverage.area.east, -173);
  EXPECT_DOUBLE_EQ(coverage.area.south, -19);
  EXPECT_DOUBLE_EQ(coverage.area.north, -18);
  EXPECT_FALSE(coverage.begins.has_value());
  EXPECT_FALSE(coverage.ends.has_value());
  EXPECT_FALSE(WindGridCoverage(0, 0, 1, 1, .1,
                                2, 0, 3, 1, .1).available);
  EXPECT_FALSE(WindGridCoverage(0, 0, 1, 1, .1,
                                0, 2, 1, 3, .1).available);
}

TEST(ModernNativeWeatherCoverage, PreservesGlobalAndDateLineGrids) {
  using weather_routing::native::WindGridCoverage;
  const auto global = WindGridCoverage(0, -90, 359, 90, 1,
                                       -180, -90, 179, 90, 1);
  ASSERT_TRUE(global.available);
  EXPECT_DOUBLE_EQ(global.area.west, -180);
  EXPECT_DOUBLE_EQ(global.area.east, 180);
  const auto wrapped = WindGridCoverage(175, -20, 185, -10, .1,
                                        175, -20, 185, -10, .1);
  ASSERT_TRUE(wrapped.available);
  EXPECT_DOUBLE_EQ(wrapped.area.west, 175);
  EXPECT_DOUBLE_EQ(wrapped.area.east, -175);
}

TEST(ModernNativeEngine, RejectsUncoveredDestinationBeforeGeneratingSearchStates) {
  auto request = TestRequest();
  auto environment = TestEnvironment();
  UniformWeatherProvider::Configuration weather;
  weather.windTowardKnots = speedDirectionToVector(14, 140);
  weather.area = {request.start.longitude - .01, request.start.latitude - .01,
                  request.start.longitude + .01, request.start.latitude + .01};
  environment.grib = std::make_shared<UniformWeatherProvider>(weather);
  const auto result = RoutingEngine{}.route(request, environment);
  EXPECT_EQ(result.status, RoutingStatus::WindForecastRequired);
  EXPECT_NE(result.message.find("route endpoints"), std::string::npos);
  EXPECT_EQ(result.diagnostics.generatedStates, 0u);
}
