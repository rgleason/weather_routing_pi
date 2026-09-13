// SPDX-License-Identifier: Apache-2.0
#include "supercpn/weather_routing/Engine.h"
#ifndef MAIN_BASELINE_ONLY
#include "supercpn/weather_routing/QuickEngine.h"
#endif
#include <bit>
#include <chrono>
#include <cmath>
#include <iomanip>
#include <iostream>
#include <stdexcept>

using namespace supercpn::weather_routing;
namespace {
class VariableWeather final : public WeatherProvider {
public:
  explicit VariableWeather(TimePoint epoch, bool tidal)
      : epoch_(epoch), tidal_(tidal) {}
  ParameterCoverage windCoverage() const override {
    ParameterCoverage c;
    c.available = true;
    c.area = {-180, -90, 180, 90};
    c.begins = epoch_ - std::chrono::hours{24};
    c.ends = epoch_ + std::chrono::hours{24 * 40};
    return c;
  }
  ParameterCoverage currentCoverage() const override { return windCoverage(); }
  ParameterCoverage waveCoverage() const override { return windCoverage(); }
  WindSample wind(GeoPoint p, TimePoint t) const override {
    const double h =
        std::chrono::duration<double, std::ratio<3600>>(t - epoch_).count();
    return {true,
            speedDirectionToVector(12 + 2 * std::sin(h / 8),
                                   140 + 35 * std::sin(h / 12 + p.longitude)),
            {}};
  }
  CurrentSample current(GeoPoint, TimePoint t) const override {
    const double h =
        std::chrono::duration<double, std::ratio<3600>>(t - epoch_).count();
    return {
        true,
        tidal_ ? Vector2{3 * std::sin(h * 6.283185307 / 12.42), .4} : Vector2{},
        {}};
  }
  WaveSample waves(GeoPoint, TimePoint) const override {
    WaveSample w;
    w.available = true;
    w.significantHeightMetres = .8;
    w.periodSeconds = 7;
    return w;
  }
  std::string identity() const override { return "head-to-head-variable-v1"; }

private:
  TimePoint epoch_;
  bool tidal_;
};
class Prediction final : public CurrentPredictionProvider {
public:
  explicit Prediction(bool limited) : limited_(limited) {}
  ParameterCoverage coverage() const override {
    ParameterCoverage c;
    c.available = ready_;
    c.area = {-180, -90, 180, 90};
    return c;
  }
  CoverageResult ensureCoverage(const GeoEnvelope&, TimePoint, TimePoint,
                                Duration, const CancellationToken&) override {
    if (limited_)
      return {CoverageStatus::ResourceLimit, "fixture prediction budget", 0};
    ready_ = true;
    return {CoverageStatus::Ready, "", 1};
  }
  CurrentSample sample(GeoPoint, TimePoint) const override {
    return {ready_, Vector2{.5, 0}, {}};
  }
  std::string identity() const override { return "fixture-current-prediction"; }

private:
  bool limited_, ready_{};
};
class DepthWater final : public LandAndBoundaryProvider {
public:
  explicit DepthWater(bool absent) : absent_(absent) {}
  bool pointForbidden(GeoPoint) const override { return false; }
  bool segmentForbidden(GeoPoint, GeoPoint, double) const override {
    return false;
  }
  double distanceToForbiddenNm(GeoPoint) const override { return 100; }
  std::optional<double> depthMetres(GeoPoint) const override {
    return absent_ ? std::optional<double>{} : std::optional<double>{20};
  }
  std::string identity() const override { return "head-to-head-depth"; }

private:
  bool absent_;
};
}  // namespace

int main(int argc, char** argv) {
  try {
    const std::string engine = argc > 1 ? argv[1] : "quick";
    const std::string name = argc > 2 ? argv[2] : "coastal";
    RoutingRequest r;
    r.start = {53.341, -4.620887};
    r.destination = {53.311102, -6.122185};
    r.departure = TimePoint{Duration{1784419200}};
    PerformanceProfile polar;
    polar.identity = "head-to-head-sailing";
    polar.rows = {
        {5, {{0, 0}, {35, 3.3}, {45, 4.5}, {90, 6}, {135, 6}, {180, 5.1}}},
        {15,
         {{0, 0}, {35, 3.9}, {45, 5.4}, {90, 6.9}, {135, 6.6}, {180, 5.7}}}};
    r.vessel.profiles = {polar};
    r.vessel.tackPenalty = Duration{300};
    r.vessel.gybePenalty = Duration{300};
    r.environment.useCurrent = false;
    r.environment.useWaves = false;
    r.constraints.maximumTrueWindKnots = 50;
    r.options.maximumSearchAngleDegrees = 180;
    r.options.timeStep = Duration{10800};
    r.options.headingStepDegrees = 10;
    r.options.minimumTimeStep = Duration{600};
    r.options.graphCorridorWidthNm = 20;
    r.options.maximumGraphCorridorWidthNm = 200;
    r.options.spatialCellNm = 3;
    r.options.destinationToleranceNm = .35;
    r.limits.maximumRouteDuration = Duration{30 * 86400};
    r.limits.maximumGeneratedStates = 450000;
    r.limits.maximumRetainedStates = 60000;
    r.limits.maximumGraphLabels = 50000;
    UniformWeatherProvider::Configuration wc;
    wc.windTowardKnots = speedDirectionToVector(14, 140);
    wc.currentTowardKnots = Vector2{};
    wc.begins = r.departure - std::chrono::hours{24};
    wc.ends = r.departure + std::chrono::hours{24 * 40};
    RoutingEnvironment env;
    env.grib = std::make_shared<UniformWeatherProvider>(wc);
    env.landAndBoundaries = std::make_shared<OpenWaterProvider>();
    bool expected = true;
    if (name == "short")
      r.destination = {53.34, -4.8};
    else if (name == "ocean" || name == "ocean6h") {
      r.start = {35, -45};
      r.destination = {40, -20};
      r.limits.maximumGeneratedStates = 2700000;
      r.limits.maximumRetainedStates = 280000;
      if (name == "ocean6h") {
        r.options.timeStep = Duration{21600};
        r.options.headingStepDegrees = 20;
      }
    } else if (name == "dateline") {
      r.start = {-20, 179};
      r.destination = {-20, -178};
    } else if (name == "high-latitude") {
      r.start = {72, 5};
      r.destination = {72, 12};
    } else if (name == "variable" || name == "tidal") {
      env.grib =
          std::make_shared<VariableWeather>(r.departure, name == "tidal");
      r.environment.useCurrent = name == "tidal";
    } else if (name == "island" || name == "land-endpoint" ||
               name == "margin") {
      std::vector<std::vector<GeoPoint>> rings{
          {{53.22, -5.42}, {53.22, -5.30}, {53.44, -5.30}, {53.44, -5.42}}};
      env.landAndBoundaries = std::make_shared<PolygonBoundaryProvider>(rings);
      if (name == "land-endpoint") {
        r.destination = {53.3, -5.35};
        expected = false;
      }
      if (name == "margin") r.constraints.landSafetyMarginNm = .5;
    } else if (name == "depth" || name == "shallow" ||
               name == "missing-depth") {
      env.landAndBoundaries =
          std::make_shared<DepthWater>(name == "missing-depth");
      r.constraints.minimumDepthMetres = name == "shallow" ? 30 : 10;
      expected = name == "depth";
    } else if (name == "prediction" || name == "prediction-limit") {
      wc.currentTowardKnots.reset();
      env.grib = std::make_shared<UniformWeatherProvider>(wc);
      env.xtdCurrent = std::make_shared<Prediction>(name == "prediction-limit");
      r.environment.useCurrent = true;
      expected = name == "prediction";
    } else if (name == "wave-limit" || name == "missing-wave") {
      if (name == "wave-limit")
        env.grib = std::make_shared<VariableWeather>(r.departure, false);
      r.environment.useWaves = true;
      r.environment.missingWaves = MissingWavePolicy::DisallowWhenConstrained;
      r.constraints.maximumWaveHeightMetres = .5;
      expected = false;
    } else if (name == "motor") {
      r.vessel.propulsion.allowSailing = false;
      r.vessel.propulsion.allowMotor = true;
      r.vessel.propulsion.configuredMotorSpeedKnots = 6;
    } else if (name == "duration-limit") {
      r.limits.maximumRouteDuration = Duration{600};
      expected = false;
    } else if (name == "missing-wind") {
      wc.windTowardKnots.reset();
      env.grib = std::make_shared<UniformWeatherProvider>(wc);
      expected = false;
    } else if (name == "wind-limit") {
      r.constraints.maximumTrueWindKnots = 5;
      expected = false;
    } else if (name == "cancel") {
      r.cancellation.cancel();
      expected = false;
    } else if (name == "invalid") {
      r.start.latitude = 100;
      expected = false;
    } else if (name == "climatology") {
      wc.ends = r.departure + std::chrono::hours{2};
      env.grib = std::make_shared<UniformWeatherProvider>(wc);
      wc.ends.reset();
      wc.source = EnvironmentalSource::Climatology;
      env.climatology = std::make_shared<UniformWeatherProvider>(wc);
      r.environment.climatology = ClimatologyFallbackPolicy::AllowWithWarning;
      r.environment.climatologyAcknowledged = true;
    } else if (name != "coastal" && name != "tiny-budget" &&
               name != "low-memory")
      throw std::runtime_error("unknown fixture");
    RoutingResult result;
    std::uint64_t memory = 0, weather = 0;
    const auto begin = std::chrono::steady_clock::now();
    if (engine == "main") result = RoutingEngine{}.route(r, env);
#ifndef MAIN_BASELINE_ONLY
    else {
      QuickRoutingOptions options;
      if (argc > 3)
        options.memoryBudgetMiB = static_cast<unsigned>(std::stoul(argv[3]));
      if (name == "tiny-budget") {
        options.maximumGeneratedStates = 1;
        expected = false;
      }
      if (name == "low-memory") options.memoryBudgetMiB = 1;
      auto quick = QuickRoutingEngine{}.route(r, env, options);
      memory = quick.quick.peakSearchBytes;
      weather = quick.quick.weatherCalls;
      result = std::move(quick.route);
    }
#endif
    const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                        std::chrono::steady_clock::now() - begin)
                        .count();
    const bool complete = result.validation.passed && !result.legs.empty();
    bool independentlyValid = false;
    if (complete) {
      PolarPerformanceModel performance(r.vessel);
      independentlyValid =
          RouteValidator{}.validate(r, env, performance, result.legs).passed;
    }
    std::uint64_t hash = 1469598103934665603ULL;
    for (const auto& leg : result.legs)
      for (double v :
           {leg.start.latitude, leg.start.longitude, leg.end.latitude,
            leg.end.longitude,
            static_cast<double>(leg.endTime.time_since_epoch().count())}) {
        hash ^= std::bit_cast<std::uint64_t>(v);
        hash *= 1099511628211ULL;
      }
    std::cout << std::setprecision(15) << "{\"engine\":" << std::quoted(engine)
              << ",\"case\":" << std::quoted(name)
              << ",\"status\":" << std::quoted(toString(result.status))
              << ",\"complete\":" << (complete ? "true" : "false")
              << ",\"validated\":" << (independentlyValid ? "true" : "false")
              << ",\"elapsed_ms\":" << ms
              << ",\"passage_seconds\":" << result.metrics.elapsed.count()
              << ",\"distance_nm\":" << result.metrics.distanceNm
              << ",\"generated\":" << result.diagnostics.generatedStates
              << ",\"tracked_search_bytes\":" << memory
              << ",\"weather_calls\":" << weather
              << ",\"fingerprint\":" << std::quoted(std::to_string(hash))
              << ",\"message\":" << std::quoted(result.message) << "}\n";
    return (complete == expected && (!complete || independentlyValid)) ? 0 : 2;
  } catch (const std::exception& e) {
    std::cerr << e.what() << '\n';
    return 1;
  }
}
