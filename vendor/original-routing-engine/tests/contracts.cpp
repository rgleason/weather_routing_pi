// SPDX-License-Identifier: GPL-3.0-or-later
#include "original_routing/Engine.h"
#include <array>
#include <cmath>
#include "original_routing/GshhgProvider.h"
#include <fstream>
#include <cstring>
#include <future>
#include <iostream>
#include <stdexcept>
#include <thread>
namespace wr = supercpn::weather_routing;
void check(bool value, const std::string& message) {
  if (!value) throw std::runtime_error(message);
}
void u32(std::ostream& out, std::uint32_t n) {
  for (int i = 0; i < 4; ++i) out.put(char(n >> (8 * i)));
}
void coordinate(std::ostream& out, double n) {
  n *= 1e6;
  std::uint64_t bits;
  std::memcpy(&bits, &n, 8);
  u32(out, bits);
  u32(out, bits >> 32);
}
void shorelineTests() {
  auto file =
      std::filesystem::temp_directory_path() /
      ("original-shore-test-" +
       std::to_string(
           std::chrono::steady_clock::now().time_since_epoch().count()) +
       ".dat");
  struct Remove {
    std::filesystem::path file;
    ~Remove() {
      std::error_code e;
      std::filesystem::remove(file, e);
    }
  } remove{file};
  {
    std::ofstream out(file, std::ios::binary);
    for (int n : {237, 1, 1, 0, -90, 360, 90, 1, 2, 3, 4, 5}) u32(out, n);
    const unsigned water = 48 + 64800 * 4, island = water + 16;
    for (int n = 0; n < 64800; ++n) u32(out, n == 90 ? island : water);
    for (int n = 0; n < 4; ++n) u32(out, 0);
    u32(out, 1);
    u32(out, 0);
    u32(out, 4);
    for (auto p : std::array<wr::GeoPoint, 4>{
             {{.004, .02}, {.004, .021}, {.005, .021}, {.005, .02}}}) {
      coordinate(out, p.longitude);
      coordinate(out, p.latitude);
    }
    for (int n = 0; n < 3; ++n) u32(out, 0);
  }
  original_routing::GshhgProvider land(file);
  check(!land.segmentForbidden({0, 0}, {0, .04}, 0),
        "clear centre line rejected");
  check(land.segmentForbidden({0, 0}, {0, .04}, .6),
        "island wholly inside safety buffer missed");
  check(land.segmentForbidden({0, .04}, {0, 0}, .6),
        "buffer depends on direction");
  check(!land.segmentForbidden({-.02, 0}, {-.02, .04}, .6),
        "outside buffer rejected");
  check(land.segmentForbidden({.0045, -1}, {.0045, 2}, 0),
        "land crossing longer than 60 NM missed");
  check(land.segmentForbidden({.0045, 2}, {.0045, -1}, 0),
        "long crossing depends on direction");
  check(land.segmentForbidden({-.1, .02}, {.1, .02}, 0),
        "exact shoreline/grid line missed");
  check(!land.segmentForbidden({0, 179}, {0, -179}, 1),
        "dateline buffer failed");
  std::vector<std::future<bool>> calls;
  for (int n = 0; n < 8; ++n)
    calls.push_back(std::async(std::launch::async, [&] {
      return land.segmentForbidden({0, 0}, {0, .04}, .6);
    }));
  for (auto& c : calls) check(c.get(), "shared shoreline cache race");
}
class Boat final : public wr::VesselPerformanceModel {
public:
  double speed;
  explicit Boat(double s = 8) : speed(s) {}
  bool valid(std::string* = nullptr) const override { return true; }
  wr::PerformanceCandidate evaluate(wr::PropulsionMode m, wr::ProfileRole,
                                    const std::string&, double, double,
                                    const wr::WaveSample&) const override {
    return {m == wr::PropulsionMode::Sail,
            wr::PropulsionMode::Sail,
            wr::ProfileRole::SailOnly,
            "boat",
            0,
            speed,
            0};
  }
  std::vector<wr::PerformanceCandidate> candidates(
      double w, double a, const wr::WaveSample& s, wr::PropulsionMode,
      wr::Duration) const override {
    return {evaluate(wr::PropulsionMode::Sail, wr::ProfileRole::SailOnly,
                     "boat", w, a, s)};
  }
};
class FaultWeather final : public wr::WeatherProvider {
  wr::UniformWeatherProvider base;

public:
  enum Kind { Throw, NaN, Gap, HighWave };
  Kind kind;
  wr::TimePoint epoch;
  FaultWeather(Kind k, wr::TimePoint t)
      : base([] {
          wr::UniformWeatherProvider::Configuration c;
          c.windTowardKnots = wr::Vector2{0, -15};
          c.currentTowardKnots = wr::Vector2{};
          c.wave = wr::WaveSample{true, .5};
          return c;
        }()),
        kind(k),
        epoch(t) {}
  wr::ParameterCoverage windCoverage() const override {
    return base.windCoverage();
  }
  wr::ParameterCoverage currentCoverage() const override {
    return base.currentCoverage();
  }
  wr::ParameterCoverage waveCoverage() const override {
    return base.waveCoverage();
  }
  wr::WindSample wind(wr::GeoPoint p, wr::TimePoint t) const override {
    if (kind == Throw) throw std::runtime_error("test provider failed");
    auto w = base.wind(p, t);
    if (kind == NaN) w.velocity.eastKnots = NAN;
    if (kind == Gap && t - epoch >= wr::Duration(1800) &&
        t - epoch <= wr::Duration(2400))
      w.available = false;
    return w;
  }
  wr::CurrentSample current(wr::GeoPoint p, wr::TimePoint t) const override {
    return base.current(p, t);
  }
  wr::WaveSample waves(wr::GeoPoint p, wr::TimePoint t) const override {
    auto w = base.waves(p, t);
    if (kind == HighWave && t - epoch >= wr::Duration(1800) &&
        t - epoch <= wr::Duration(2400))
      w.significantHeightMetres = 4;
    return w;
  }
  std::string identity() const override { return "fault-injection"; }
};
wr::RoutingEnvironment environment(double speed = 8, bool current = true,
                                   bool wind = true) {
  wr::UniformWeatherProvider::Configuration c;
  if (wind) c.windTowardKnots = wr::Vector2{0, -15};
  if (current) c.currentTowardKnots = wr::Vector2{0, 0};
  wr::RoutingEnvironment e;
  e.grib = std::make_shared<wr::UniformWeatherProvider>(c);
  e.performance = std::make_shared<Boat>(speed);
  e.landAndBoundaries = std::make_shared<wr::OpenWaterProvider>();
  return e;
}
wr::RoutingRequest request(wr::GeoPoint a = {30, -140},
                           wr::GeoPoint b = {30, -138}) {
  wr::RoutingRequest r;
  r.start = a;
  r.destination = b;
  r.departure = wr::TimePoint(wr::Duration(1789923600));
  r.environment.useCurrent = false;
  r.environment.useWaves = false;
  r.environment.missingWaves = wr::MissingWavePolicy::DisallowWhenConstrained;
  r.constraints.minimumTrueWindAngleDegrees = 40;
  r.constraints.maximumTrueWindAngleDegrees = 160;
  r.options.timeStep = std::chrono::hours(3);
  r.options.headingStepDegrees = 10;
  r.limits.maximumRouteDuration = std::chrono::hours(96);
  r.limits.maximumGeneratedStates = 500000;
  r.limits.maximumRetainedStates = 100000;
  r.vessel.tackPenalty = wr::Duration(300);
  r.vessel.gybePenalty = wr::Duration(300);
  return r;
}
wr::RoutingResult solve(wr::RoutingRequest r,
                        wr::RoutingEnvironment e = environment()) {
  original_routing::Options o;
  o.maximumRuntime = std::chrono::seconds(20);
  auto result = original_routing::Engine{}.route(r, e, o);
  std::cout << wr::toString(result.status) << " " << result.message
            << " states=" << result.diagnostics.generatedStates << "\n";
  if (result.status == wr::RoutingStatus::Complete) {
    check(result.validation.passed, "completion without validation");
    check(result.validation.samples > 0 &&
              result.diagnostics.validationSamples >= result.validation.samples,
          "validation accounting missing");
    check(wr::distanceNm(result.legs.back().end, r.destination) < .003,
          "unconnected arrival");
    auto replay =
        wr::RouteValidator{}.validate(r, e, *e.performance, result.legs);
    check(replay.passed, replay.failureReason);
  }
  return result;
}
int main(int argc, char** argv) try {
  shorelineTests();
  {
    auto r = request();
    // Plugin default 90 means no latitude restriction, not an invalid option.
    r.constraints.maximumLatitudeDegrees = 90;
    r.limits.maximumExplorationDistanceNm = 60000;
    original_routing::Options display;
    display.captureVisualization = true;
    auto shown = original_routing::Engine{}.route(r, environment(), display);
    auto hidden = original_routing::Engine{}.route(r, environment());
    check(
        shown.status == wr::RoutingStatus::Complete && shown.validation.passed,
        "display-enabled route failed validation");
    check(shown.metrics.elapsed == hidden.metrics.elapsed &&
              shown.diagnostics.generatedStates ==
                  hidden.diagnostics.generatedStates,
          "display collection changed search");
    check(!shown.visualization.isochrones.empty() &&
              shown.visualization.isochrones.size() <= 128 &&
              hidden.visualization.isochrones.empty(),
          "display layer allowance");
    for (const auto& layer : shown.visualization.isochrones) {
      check(layer.traces.size() <= 4, "display trace allowance");
      for (const auto& trace : layer.traces)
        check(trace.route.size() <= 256 &&
                  wr::distanceNm(trace.route.front(), r.start) < .001,
              "display lineage lost departure");
    }
  }
  {
    auto r = request({30, -140}, {30, -139.5});
    r.constraints.landSafetyMarginNm = .6;
    auto e = environment();
    e.landAndBoundaries = std::make_shared<wr::PolygonBoundaryProvider>(
        std::vector<std::vector<wr::GeoPoint>>{{{30.004, -140.001},
                                                {30.005, -140.001},
                                                {30.005, -139.999},
                                                {30.004, -139.999}}});
    check(solve(r, e).status == wr::RoutingStatus::InvalidStart,
          "departure margin silently waived");
    std::swap(r.start, r.destination);
    check(solve(r, e).status == wr::RoutingStatus::InvalidDestination,
          "arrival margin silently waived");
  }
  if (argc == 2) {
    original_routing::GshhgProvider coast(argv[1]);
    for (auto pair : std::array<std::pair<wr::GeoPoint, wr::GeoPoint>, 3>{
             {{{48, -5}, {48, 5}},
              {{48.001, -5}, {48.001, 5}},
              {{-18.7260670668956, -174.13594408099112},
               {-18.616207410317653, -174.0080853580806}}}}) {
      check(coast.segmentForbidden(pair.first, pair.second, 0),
            "real GSHHG crossing missed");
      check(coast.segmentForbidden(pair.second, pair.first, 0),
            "real GSHHG reverse crossing missed");
    }
    check(!coast.segmentForbidden({30, -140}, {30, -139}, 0),
          "real GSHHG open sea rejected");
  }
  for (auto r :
       {request(), request({30, -138}, {30, -140}),
        request({-20, 179}, {-20, -179}), request({-20, -179}, {-20, 179}),
        request({30, -140}, {31, -140})})
    check(solve(r).status == wr::RoutingStatus::Complete,
          "direction/dateline/upwind route failed");
  auto r = request();
  {
    auto wide = request({30, -140}, {30, -139.5});
    wide.constraints.minimumTrueWindAngleDegrees = 0;
    wide.constraints.maximumTrueWindAngleDegrees = 180;
    wide.options.headingStepDegrees = 5;
    wide.limits.maximumRetainedStates = 10000;
    check(solve(wide).status == wr::RoutingStatus::Complete,
          "bounded broad heading fan failed");
  }
  r.environment.useCurrent = true;
  check(solve(r, environment(8, false)).status ==
            wr::RoutingStatus::CurrentDataRequired,
        "missing current accepted");
  r.environment.missingCurrent = wr::MissingCurrentPolicy::AllowAssumedZero;
  r.environment.zeroCurrentAcknowledged = true;
  auto waived = solve(r, environment(8, false));
  check(
      waived.status == wr::RoutingStatus::Complete && !waived.warnings.empty(),
      "explicit zero current rejected or silent");
  r = request();
  r.environment.useWaves = true;
  r.constraints.maximumWaveHeightMetres = 1.;
  check(solve(r).status == wr::RoutingStatus::WaveDataRequired,
        "missing constrained waves accepted");
  check(solve(request(), environment(8, true, false)).status ==
            wr::RoutingStatus::WindForecastRequired,
        "missing wind accepted");
  r.environment.missingWaves = wr::MissingWavePolicy::AllowWithWarning;
  waived = solve(r);
  check(
      waived.status == wr::RoutingStatus::Complete && !waived.warnings.empty(),
      "waived waves rejected or silent");
  for (auto kind : {FaultWeather::Throw, FaultWeather::NaN, FaultWeather::Gap,
                    FaultWeather::HighWave}) {
    r = request({30, -140}, {30, -140.3});
    r.limits.maximumRouteDuration = std::chrono::hours(6);
    r.environment.useWaves = true;
    r.constraints.maximumWaveHeightMetres = 1;
    auto e = environment();
    e.grib = std::make_shared<FaultWeather>(kind, r.departure);
    auto failed = solve(r, e);
    check(failed.status != wr::RoutingStatus::Complete &&
              !failed.validation.passed && failed.legs.empty(),
          "invalid or interrupted environment accepted");
    if (kind == FaultWeather::Throw)
      check(failed.status == wr::RoutingStatus::InternalError,
            "provider exception escaped");
    if (kind == FaultWeather::Gap)
      check(failed.status == wr::RoutingStatus::WindForecastRequired,
            "wind gap was not diagnosed");
    if (kind == FaultWeather::HighWave)
      check(failed.status == wr::RoutingStatus::NoFeasibleRoute,
            "wave-limit refusal was not diagnosed");
  }
  r = request();
  r.constraints.minimumTrueWindAngleDegrees = NAN;
  check(solve(r).status == wr::RoutingStatus::InvalidVesselConfiguration,
        "NaN option accepted");
  for (auto which : {0, 1, 2}) {
    original_routing::Options o;
    if (which == 0) o.maximumGeometryOperations = 1;
    if (which == 1) o.maximumWeatherQueries = 1;
    if (which == 2) o.maximumRuntime = std::chrono::milliseconds(1);
    auto limited = original_routing::Engine{}.route(
        request({30, -140}, {30, -170}), environment(), o);
    check(limited.status == wr::RoutingStatus::ResourceLimitReached &&
              limited.legs.empty(),
          "work/time budget ignored");
  }
  auto fallback = environment(8, true, false);
  wr::UniformWeatherProvider::Configuration climate;
  climate.windTowardKnots = wr::Vector2{0, -15};
  fallback.climatology = std::make_shared<wr::UniformWeatherProvider>(climate);
  r = request();
  r.environment.climatology = wr::ClimatologyFallbackPolicy::AllowWithWarning;
  waived = solve(r, fallback);
  check(
      waived.status == wr::RoutingStatus::Complete && !waived.warnings.empty(),
      "climatology fallback rejected or silent");
  original_routing::Options budget;
  budget.maximumWeatherQueries = 10;
  fallback.grib.reset();
  check(original_routing::Engine{}.route(r, fallback, budget).status ==
            wr::RoutingStatus::ResourceLimitReached,
        "fallback bypassed query budget");
  r = request();
  r.cancellation.cancel();
  check(solve(r).status == wr::RoutingStatus::Cancelled, "pre-cancel ignored");
  r = request({30, -140}, {30, -165});
  r.limits.maximumGeneratedStates = 10;
  check(solve(r).status == wr::RoutingStatus::ResourceLimitReached,
        "state limit ignored");
  for (unsigned n : {1, 2, 4, 16, 64}) {
    r = request();
    r.limits.maximumRetainedStates = n;
    check(solve(r).status == wr::RoutingStatus::ResourceLimitReached,
          "lineage memory limit ignored");
  }
  std::vector<std::future<wr::RoutingResult>> runs;
  for (unsigned n = 0; n < 8; ++n)
    runs.push_back(std::async(std::launch::async, [n] {
      return original_routing::Engine{}.route(
          request({25., -140.}, {25., n % 2 ? -140.5 : -139.5}),
          environment(5 + n));
    }));
  unsigned speed = 5;
  for (auto& run : runs) {
    auto result = run.get();
    check(result.status == wr::RoutingStatus::Complete &&
              result.validation.passed,
          "concurrent providers contaminated");
    check(std::abs(result.metrics.distanceNm * 3600 /
                       result.metrics.elapsed.count() -
                   speed++) < .1,
          "concurrent boat speed contaminated");
  }
  r = request({30, -140}, {30, -170});
  auto run = std::async(std::launch::async, [r] {
    return original_routing::Engine{}.route(r, environment());
  });
  std::this_thread::sleep_for(std::chrono::milliseconds(20));
  auto t = std::chrono::steady_clock::now();
  r.cancellation.cancel();
  check(run.get().status == wr::RoutingStatus::Cancelled,
        "in-flight cancel ignored");
  check(std::chrono::steady_clock::now() - t < std::chrono::seconds(1),
        "cancellation too slow");
  std::cout << "Original contract tests passed\n";
  return 0;
} catch (const std::exception& e) {
  std::cerr << e.what() << "\n";
  return 1;
}
