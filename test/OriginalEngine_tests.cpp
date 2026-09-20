// SPDX-License-Identifier: GPL-3.0-or-later
#include <gtest/gtest.h>
#include "original_routing/Engine.h"
#include <future>

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
}  // namespace
