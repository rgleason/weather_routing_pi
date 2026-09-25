// SPDX-License-Identifier: GPL-3.0-or-later
#include <gtest/gtest.h>
#include "original_routing/Engine.h"
#include "original_routing/VisualizationSampler.h"
#include <future>
#include <cmath>
#include <stdexcept>
#include "Polar.h"

namespace {
namespace wr = supercpn::weather_routing;
struct ConstantBoat final : wr::VesselPerformanceModel {
  explicit ConstantBoat(double speed) : speed(speed) {}
  double speed;
  bool valid(std::string* = nullptr) const override { return true; }
  wr::PerformanceCandidate evaluate(wr::PropulsionMode mode, wr::ProfileRole,
                                    const std::string&, double, double,
                                    const wr::WaveSample&) const override {
    return {mode == wr::PropulsionMode::Sail,
            wr::PropulsionMode::Sail,
            wr::ProfileRole::SailOnly,
            "constant",
            0,
            speed,
            0};
  }
  std::vector<wr::PerformanceCandidate> candidates(
      double ws, double wa, const wr::WaveSample& waves, wr::PropulsionMode,
      wr::Duration) const override {
    return {evaluate(wr::PropulsionMode::Sail, wr::ProfileRole::SailOnly,
                     "constant", ws, wa, waves)};
  }
};
wr::RoutingRequest request() {
  wr::RoutingRequest r;
  r.start = {-20, 179};
  r.destination = {-20, -179};
  r.departure = wr::TimePoint{wr::Duration{1789891200}};
  r.environment.useCurrent = false;
  r.environment.useWaves = false;
  r.constraints.minimumTrueWindAngleDegrees = 40;
  r.constraints.maximumTrueWindAngleDegrees = 160;
  r.constraints.maximumLatitudeDegrees = 90;  // OpenCPN's unrestricted default.
  r.options.timeStep = std::chrono::hours(3);
  r.options.headingStepDegrees = 10;
  r.limits.maximumRetainedStates = 65536;
  r.limits.maximumGeneratedStates = 20000000;
  return r;
}
wr::RoutingEnvironment environment(double speed = 8) {
  wr::UniformWeatherProvider::Configuration weather;
  weather.windTowardKnots = wr::Vector2{0, -15};
  wr::RoutingEnvironment e;
  e.grib = std::make_shared<wr::UniformWeatherProvider>(weather);
  e.performance = std::make_shared<ConstantBoat>(speed);
  e.landAndBoundaries = std::make_shared<wr::OpenWaterProvider>();
  return e;
}
TEST(OriginalEngine,
     PluginDefaultsAndDatelineBothDirectionsRequireValidatedEndpoint) {
  auto r = request();
  for (int i = 0; i < 2; ++i) {
    const auto result = original_routing::Engine{}.route(r, environment());
    ASSERT_EQ(result.status, wr::RoutingStatus::Complete) << result.message;
    ASSERT_TRUE(result.validation.passed);
    ASSERT_FALSE(result.legs.empty());
    EXPECT_LT(wr::distanceNm(result.legs.back().end, r.destination), .003);
    EXPECT_GT(result.diagnostics.validationSamples, 0U);
    std::swap(r.start, r.destination);
  }
}
TEST(OriginalEngine, DisplayIsBoundedAndDoesNotChangeAcceptedRoute) {
  original_routing::Options options;
  options.captureVisualization = true;
  const auto a =
      original_routing::Engine{}.route(request(), environment(), options);
  const auto b = original_routing::Engine{}.route(request(), environment());
  ASSERT_EQ(a.status, wr::RoutingStatus::Complete);
  EXPECT_EQ(a.metrics.elapsed, b.metrics.elapsed);
  EXPECT_EQ(a.diagnostics.generatedStates, b.diagnostics.generatedStates);
  EXPECT_FALSE(a.visualization.isochrones.empty());
  EXPECT_LE(a.visualization.isochrones.size(), 128U);
  EXPECT_TRUE(b.visualization.isochrones.empty());
}
TEST(OriginalEngine, VisualizationSamplesTheEntireVoyageWithinBound) {
  original_routing::VisualizationSampler sampler;
  std::vector<int> displayed;
  for (int front = 0; front < 1024; ++front) {
    if (sampler.shouldCapture(displayed)) displayed.push_back(front);
    ASSERT_LE(displayed.size(), 128U);
  }
  ASSERT_FALSE(displayed.empty());
  EXPECT_EQ(displayed.front(), 0);
  EXPECT_GE(displayed.back(), 1008);
  for (std::size_t i = 1; i < displayed.size(); ++i)
    EXPECT_GT(displayed[i], displayed[i - 1]);
}
TEST(OriginalEngine, MissingRequestedDataAndCancellationCannotComplete) {
  auto r = request();
  r.environment.useCurrent = true;
  r.environment.missingCurrent = wr::MissingCurrentPolicy::Disallow;
  EXPECT_EQ(original_routing::Engine{}.route(r, environment()).status,
            wr::RoutingStatus::CurrentDataRequired);
  r.environment.useCurrent = false;
  r.environment.useWaves = true;
  r.environment.missingWaves = wr::MissingWavePolicy::DisallowWhenConstrained;
  r.constraints.maximumWaveHeightMetres = 2;
  EXPECT_EQ(original_routing::Engine{}.route(r, environment()).status,
            wr::RoutingStatus::WaveDataRequired);
  r.cancellation.cancel();
  auto cancelled = original_routing::Engine{}.route(r, environment());
  EXPECT_EQ(cancelled.status, wr::RoutingStatus::Cancelled);
  EXPECT_TRUE(cancelled.legs.empty());
}
TEST(OriginalEngine, ConcurrentRequestsKeepBoatProvidersIndependent) {
  auto fast = std::async(std::launch::async, [] {
    return original_routing::Engine{}.route(request(), environment(8));
  });
  auto slow = original_routing::Engine{}.route(request(), environment(4));
  auto result = fast.get();
  ASSERT_EQ(result.status, wr::RoutingStatus::Complete);
  ASSERT_EQ(slow.status, wr::RoutingStatus::Complete);
  EXPECT_GT(slow.metrics.elapsed.count(), result.metrics.elapsed.count() * 1.9);
}

// Use the actual first-install polar: unlike ConstantBoat it does not supply
// speed outside 50..150 degrees, even if the configured limits are 40..160.
struct DefaultPolarBoat final : wr::VesselPerformanceModel {
  mutable Polar polar;
  DefaultPolarBoat() {
    wxString error;
    if (!polar.Open(wxString(WEATHER_ROUTING_SOURCE_DIR) +
                        "/data/polars/Example/Test-TWS-0-20+60.pol", error))
      throw std::runtime_error(error.ToStdString());
  }
  bool valid(std::string* = nullptr) const override { return true; }
  wr::PerformanceCandidate evaluate(wr::PropulsionMode mode, wr::ProfileRole,
                                    const std::string&, double wind, double angle,
                                    const wr::WaveSample&) const override {
    const double speed = polar.Speed(angle, wind, nullptr, false, false);
    return {mode == wr::PropulsionMode::Sail && std::isfinite(speed) && speed > 0,
            wr::PropulsionMode::Sail, wr::ProfileRole::SailOnly, "default-polar",
            0, speed, 0};
  }
  std::vector<wr::PerformanceCandidate> candidates(
      double wind, double angle, const wr::WaveSample& waves, wr::PropulsionMode,
      wr::Duration) const override {
    auto value = evaluate(wr::PropulsionMode::Sail, wr::ProfileRole::SailOnly,
                          "default-polar", wind, angle, waves);
    return value.valid ? std::vector<wr::PerformanceCandidate>{value}
                       : std::vector<wr::PerformanceCandidate>{};
  }
};

struct TurningWind final : wr::WeatherProvider {
  wr::UniformWeatherProvider base{[] {
    wr::UniformWeatherProvider::Configuration c;
    c.windTowardKnots = wr::Vector2{0, -15};
    return c;
  }()};
  wr::TimePoint epoch;
  double initialFrom;
  TurningWind(wr::TimePoint t, double from) : epoch(t), initialFrom(from) {}
  wr::ParameterCoverage windCoverage() const override {
    return base.windCoverage();
  }
  wr::ParameterCoverage currentCoverage() const override { return {}; }
  wr::ParameterCoverage waveCoverage() const override { return {}; }
  wr::WindSample wind(wr::GeoPoint p, wr::TimePoint t) const override {
    const double hours = (t - epoch).count() / 3600.;
    auto value = base.wind(p, t);
    value.velocity = wr::speedDirectionToVector(
        15 + 2 * std::sin(hours / 8),
        initialFrom + 35 * std::sin(hours / 12 + p.longitude * .2) + 180);
    return value;
  }
  wr::CurrentSample current(wr::GeoPoint, wr::TimePoint) const override {
    return {};
  }
  wr::WaveSample waves(wr::GeoPoint, wr::TimePoint) const override { return {}; }
  std::string identity() const override { return "turning-wind-regression"; }
};

TEST(OriginalEngine, DefaultPolarChangingWindReachesActualDownwindDestination) {
  for (double from : {30., 45., 60.}) {
    SCOPED_TRACE(from);
    auto r = request();
    r.start = {30, -130};
    r.destination = {28, -134};
    r.options.maximumSearchAngleDegrees = 120;
    r.limits.maximumRouteDuration = std::chrono::hours(96);
    r.vessel.tackPenalty = r.vessel.gybePenalty = wr::Duration{};
    auto e = environment();
    e.performance = std::make_shared<DefaultPolarBoat>();
    e.grib = std::make_shared<TurningWind>(r.departure, from);
    const auto result = original_routing::Engine{}.route(r, e);
    ASSERT_EQ(result.status, wr::RoutingStatus::Complete) << result.message;
    ASSERT_FALSE(result.legs.empty());
    EXPECT_TRUE(result.validation.passed);
    EXPECT_LT(wr::distanceNm(result.legs.back().end, r.destination), .003);
    EXPECT_GT(result.metrics.gybeCount, 0U);
    EXPECT_LT(result.metrics.elapsed.count(), 96 * 3600);
    // Independently screen the delivered chords at minute intervals: success
    // must not come from allowing a direct chord outside the polar envelope.
    for (const auto& leg : result.legs) {
      const auto duration = (leg.endTime - leg.startTime).count();
      ASSERT_GT(duration, 0);
      const double bearing = wr::initialBearingDegrees(leg.start, leg.end);
      const double distance = wr::distanceNm(leg.start, leg.end);
      for (std::int64_t second = 0; second < duration; second += 60) {
        const auto p = wr::destinationPoint(
            leg.start, bearing, distance * double(second) / duration);
        const auto wind = e.grib->wind(p, leg.startTime + wr::Duration(second));
        const double angle = wr::trueWindAngleDegrees(wind.velocity, bearing);
        EXPECT_GE(angle, 49.8);
        EXPECT_LE(angle, 150.2);
      }
    }
  }
}
}  // namespace
