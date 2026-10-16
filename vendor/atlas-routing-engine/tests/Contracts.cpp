// SPDX-License-Identifier: GPL-3.0-or-later
#include "Fixtures.h"
#include <future>
#include <iostream>
#include <stdexcept>

namespace wr = fixture::wr;
using Engine = atlas_routing::Engine;
using Options = atlas_routing::Options;
unsigned checks{};
void require(bool test, const std::string& message) {
  ++checks;
  if (!test) throw std::runtime_error(message);
}
wr::RoutingEnvironment environment(const Json::Value& j,
                                   const wr::RoutingRequest& r) {
  wr::RoutingEnvironment e;
  e.grib = std::make_shared<fixture::Weather>(j, r.start);
  e.landAndBoundaries = std::make_shared<fixture::Land>(j, r.start);
  return e;
}
atlas_routing::Result run(Json::Value j, Options o = {}) {
  auto r = fixture::configure(j);
  return Engine{}.route(r, environment(j, r), o);
}
void audit(const wr::RoutingResult& route, const Json::Value& j) {
  auto r = fixture::configure(j);
  auto e = environment(j, r);
  wr::PolarPerformanceModel p(r.vessel);
  require(route.status == wr::RoutingStatus::Complete, route.message);
  require(route.validation.passed, "native validation");
  require(wr::RouteValidator{}.validate(r, e, p, route.legs).passed,
          "fresh independent chronological replay");
  wr::Duration elapsed{};
  double distance{};
  for (const auto& leg : route.legs) {
    elapsed += leg.endTime - leg.startTime;
    distance += wr::distanceNm(leg.start, leg.end);
  }
  require(elapsed == route.metrics.elapsed,
          "elapsed metric equals delivered legs");
  require(std::abs(distance - route.metrics.distanceNm) < 1e-8,
          "distance metric is not accumulated across incumbents");
  require(route.metrics.waitingTime <= elapsed,
          "waiting metric is not accumulated across incumbents");
  require(route.metrics.sailingTime + route.metrics.motorOnlyTime +
                  route.metrics.motorSailingTime + route.metrics.waitingTime ==
              elapsed,
          "propulsion/wait durations partition delivered passage");
}
void mathematics() {
  Options o;
  o.improvementAttempts = 0;
  auto j = fixture::scenario("uniform");
  auto a = run(j, o);
  audit(a.route, j);
  require(std::abs(a.route.metrics.elapsed.count() - 12000) <= 2,
          "six-knot analytic reach oracle");
  auto repeat = run(j, o);
  require(repeat.route.metrics.elapsed == a.route.metrics.elapsed,
          "deterministic ETA");
  require(repeat.atlas.edgeAttempts == a.atlas.edgeAttempts,
          "deterministic work");
  require(repeat.route.legs.front().end == a.route.legs.front().end,
          "deterministic geometry");
  j["east_nm"] = 0;
  j["north_nm"] = 20;
  auto up = run(j, o);
  audit(up.route, j);
  double vmg = 0;
  for (double theta = 40; theta <= 90; theta += .001) {
    auto speed = theta <= 50 ? 4 + (theta - 40) / 10 : 5 + (theta - 50) / 40;
    vmg = std::max(vmg, speed * std::cos(theta * std::numbers::pi / 180));
  }
  const double reference = 20 / vmg * 3600 + 120;
  require(std::abs(up.route.metrics.elapsed.count() / reference - 1) < .005,
          "two-control upwind envelope oracle");
  require(up.route.metrics.tackCount >= 1,
          "upwind route contains explicit tacking");
  j["start"][0] = -20;
  j["start"][1] = 179.9;
  j["east_nm"] = 30;
  j["north_nm"] = 0;
  j["isotropic"] = true;
  audit(run(j, o).route, j);
}
void preflight() {
  Options o;
  o.improvementAttempts = 0;
  o.maximumEdgeAttempts = 8;
  auto j = fixture::scenario("preflight");
  auto r = fixture::configure(j);
  auto e = environment(j, r);
  auto bad = o;
  bad.minimumCellNm = std::nan("");
  require(Engine{}.route(r, e, bad).route.status ==
              wr::RoutingStatus::InvalidVesselConfiguration,
          "NaN options rejected");
  bad = o;
  bad.neighbours = 0;
  require(Engine{}.route(r, e, bad).route.status ==
              wr::RoutingStatus::InvalidVesselConfiguration,
          "invalid connectivity rejected");
  auto unsupported = r;
  unsupported.objective.kind = wr::ObjectiveKind::FuelAware;
  require(Engine{}.route(unsupported, e, o).route.status ==
              wr::RoutingStatus::InvalidVesselConfiguration,
          "unsupported objective explicit");
  auto cancelled = r;
  cancelled.cancellation = wr::CancellationToken{};
  cancelled.cancellation.cancel();
  require(Engine{}.route(cancelled, e, o).route.status ==
              wr::RoutingStatus::Cancelled,
          "initial cancellation");
  auto invalid = r;
  invalid.start.latitude = std::nan("");
  require(Engine{}.route(invalid, e, o).route.status ==
              wr::RoutingStatus::InvalidStart,
          "NaN endpoint rejected");
  invalid = r;
  invalid.destination.latitude = 86;
  require(Engine{}.route(invalid, e, o).route.status ==
              wr::RoutingStatus::InvalidDestination,
          "unsupported latitude explicit");
  j["missing_wind"] = true;
  require(run(j, o).route.status == wr::RoutingStatus::WindForecastRequired,
          "missing wind fails closed");
  j.removeMember("missing_wind");
  j["currents"] = true;
  j["missing_current"] = true;
  require(run(j, o).route.status == wr::RoutingStatus::CurrentDataRequired,
          "missing current fails closed");
  j.removeMember("missing_current");
  j["wave_height"] = 4;
  j["max_wave"] = 2;
  require(!run(j, o).route.validation.passed, "hard wave ceiling");
  j = fixture::scenario("depth");
  j["depth"] = 2;
  j["min_depth"] = 5;
  require(!run(j, o).route.validation.passed, "hard depth ceiling");
}
void resources() {
  auto j = fixture::scenario("resources");
  Options o;
  o.maximumEdgeAttempts = 1;
  o.improvementAttempts = 20;
  require(run(j, o).route.validation.passed,
          "validated incumbent survives work exhaustion");
  auto cappedRequest = fixture::configure(j);
  cappedRequest.limits.maximumRetainedStates = 2;
  require(Engine{}.route(cappedRequest, environment(j, cappedRequest))
                  .atlas.labelsPeak <= 2,
          "preliminary candidates obey the retained-label allowance");
  o.memoryBudgetBytes = 4096;
  auto tiny = run(j, o);
  require(tiny.atlas.memoryPeakBytes <= 4096, "managed search memory cap");
  o = {};
  o.maximumWeatherQueries = 1;
  auto weather = run(j, o);
  require(weather.route.status == wr::RoutingStatus::ResourceLimitReached &&
              !weather.route.validation.passed,
          "weather allowance enforced");
  o = {};
  o.maximumGeometryQueries = 1;
  require(run(j, o).route.status == wr::RoutingStatus::ResourceLimitReached,
          "geometry allowance enforced");
  auto r = fixture::configure(j);
  auto e = environment(j, r);
  o = {};
  o.maximumEdgeAttempts = 5;
  r.limits.maximumValidationSamples = 1;
  require(!Engine{}.route(r, e, o).route.validation.passed,
          "validation sample cap cannot be bypassed");
  r = fixture::configure(j);
  r.cancellation = r.cancellation.bounded(
      std::chrono::steady_clock::now() - std::chrono::seconds{1}, {});
  require(Engine{}.route(r, e, o).route.status == wr::RoutingStatus::Cancelled,
          "deadline enforced");
}
void waiting() {
  auto j = fixture::scenario("gate");
  j["gate"] = true;
  j["gate_opens_hours"] = 4;
  auto a = run(j);
  audit(a.route, j);
  require(a.route.metrics.elapsed.count() >= 20400 &&
              a.route.metrics.elapsed.count() <= 20700,
          "analytic gate oracle");
  require(a.route.metrics.waitingTime > wr::Duration{}, "explicit waiting leg");
  require(a.atlas.incumbentSeconds.size() > 1,
          "anytime hold relocation improves a valid incumbent");
  for (std::size_t i = 1; i < a.atlas.incumbentSeconds.size(); ++i)
    require(a.atlas.incumbentSeconds[i] < a.atlas.incumbentSeconds[i - 1],
            "incumbents monotonically improve");
  j["waiting"] = false;
  auto loiter = run(j);
  audit(loiter.route, j);
  require(loiter.route.metrics.waitingTime == wr::Duration{},
          "no stationary waiting when disabled");
  require(loiter.route.metrics.elapsed.count() <= 20800,
          "continuous sailing loop reaches tidal gate efficiently");
  j["waiting"] = true;
  j["east_nm"] = 50;
  j["gate_east_nm"] = 25;
  j["gate_opens_hours"] = 7.5;
  auto late = run(j);
  audit(late.route, j);
  require(late.route.metrics.elapsed.count() >= 42000 &&
              late.route.metrics.elapsed.count() < 42030,
          "gate later than maximum departure hold is solved by waiting at a "
          "safe event");
}
void weather() {
  auto j = fixture::scenario("jet");
  j["east_nm"] = 40;
  j["currents"] = true;
  j["jet"] = true;
  j["current_to"] = 90;
  auto jet = run(j);
  audit(jet.route, j);
  require(jet.route.metrics.elapsed.count() < 18000,
          "coupled shooting finds current jet detour");
  j["east_nm"] = 23.689317711279404;
  auto band = run(j);
  audit(band.route, j);
  require(
      band.atlas.laneProbes > 0 && band.route.metrics.elapsed.count() < 13200,
      "weather-band shape optimisation improves an already feasible direct "
      "route");
  j = fixture::scenario("wind reversal");
  j["east_nm"] = 60;
  j["wind_shift"] = true;
  auto change = run(j);
  audit(change.route, j);
  require(change.atlas.valueFieldCells > 0,
          "time-dependent Bellman guidance used");
  j = fixture::scenario("localised moving storm");
  j["east_nm"] = 40;
  j["storm"] = true;
  j["max_wind"] = 30;
  auto storm = run(j);
  audit(storm.route, j);
  require(
      storm.route.metrics.elapsed.count() < 25500,
      "broad spatial choices recover a route around a localised moving hazard");
  j = fixture::scenario("short forecast");
  j["coverage_hours"] = .5;
  Options o;
  o.maximumEdgeAttempts = 16;
  require(!run(j, o).route.validation.passed,
          "forecast cannot be extrapolated silently");
}
void coastal() {
  auto terminal = fixture::scenario("exact coastal terminal replay");
  terminal["margin_nm"] = .2;
  audit(run(terminal).route, terminal);
  auto j = fixture::scenario("offset narrow");
  Json::Reader reader;
  require(reader.parse("[[8,-20,12,1.9],[8,2.1,12,20]]", j["rectangles_nm"]),
          "fixture parse");
  auto a = run(j);
  audit(a.route, j);
  require(a.route.metrics.elapsed.count() < 13000,
          "mixed-cell refinement preserves narrow portal");
  j = fixture::scenario("strict");
  j["validation_block"] = true;
  Options o;
  o.maximumEdgeAttempts = 10;
  auto strict = run(j, o);
  require(!strict.route.validation.passed && strict.route.legs.empty(),
          "stricter authoritative validator cannot be bypassed");
}
void propulsion() {
  auto j = fixture::scenario("motor");
  j["wind_speed"] = 0;
  j["motor"] = true;
  j["east_nm"] = 10;
  Options o;
  o.improvementAttempts = 0;
  auto a = run(j, o);
  audit(a.route, j);
  require(a.route.metrics.motorOnlyTime > wr::Duration{},
          "calm routing uses motor");
  auto r = fixture::configure(j);
  auto e = environment(j, r);
  r.vessel.propulsion.maximumMotorTime = wr::Duration{600};
  o.maximumEdgeAttempts = 8;
  require(!Engine{}.route(r, e, o).route.validation.passed,
          "motor time hard constraint");
  r = fixture::configure(j);
  r.vessel.propulsion.fuelConsumptionLitresPerHour = 5;
  r.vessel.propulsion.maximumFuelLitres = 1;
  require(!Engine{}.route(r, e, o).route.validation.passed,
          "fuel hard constraint");
}
void observers() {
  auto j = fixture::scenario("observers");
  auto r = fixture::configure(j);
  auto e = environment(j, r);
  r.progress = [](auto) { throw std::runtime_error("observer"); };
  r.preview = [](auto) { throw std::runtime_error("preview"); };
  auto a = Engine{}.route(r, e);
  audit(a.route, j);
  auto normal = run(j);
  require(a.route.metrics.elapsed == normal.route.metrics.elapsed,
          "observer exceptions cannot change route");
  Options live;
  std::vector<std::int64_t> observed;
  live.onIncumbent = [&](const wr::RoutingResult& candidate) {
    audit(candidate, j);
    observed.push_back(candidate.metrics.elapsed.count());
    throw std::runtime_error("incumbent observer");
  };
  auto anytime = Engine{}.route(r, e, live);
  audit(anytime.route, j);
  require(!observed.empty() && observed == anytime.atlas.incumbentSeconds,
          "only independently valid improving incumbents are published");
  require(anytime.route.metrics.elapsed == normal.route.metrics.elapsed,
          "incumbent observer exceptions cannot change route");
  wr::PolarPerformanceModel p(r.vessel);
  auto corrupt = a.route.legs;
  corrupt.front().startTime += wr::Duration{1};
  require(!wr::RouteValidator{}.validate(r, e, p, corrupt).passed,
          "independent validator rejects timestamp mutation");
}
void concurrency() {
  auto j = fixture::scenario("concurrent");
  Options o;
  o.improvementAttempts = 0;
  auto serial = run(j, o);
  auto first = std::async(std::launch::async, [=] { return run(j, o); });
  auto second = std::async(std::launch::async, [=] { return run(j, o); });
  for (auto* f : {&first, &second}) {
    auto result = f->get();
    require(result.route.metrics.elapsed == serial.route.metrics.elapsed,
            "reentrant results");
    require(result.atlas.weatherQueries == serial.atlas.weatherQueries,
            "reentrant query counts");
  }
}
void providers() {
  class Prediction final : public wr::CurrentPredictionProvider {
  public:
    wr::CoverageStatus status{wr::CoverageStatus::Ready};
    wr::TimePoint first{}, last{};
    wr::Duration step{};
    unsigned prepared{};
    mutable std::uint64_t samples{};
    wr::ParameterCoverage coverage() const override {
      return {true, {}, {}, {-180, -85, 180, 85}, "contract-prediction"};
    }
    wr::CoverageResult ensureCoverage(const wr::GeoEnvelope&, wr::TimePoint a,
                                      wr::TimePoint b, wr::Duration d,
                                      const wr::CancellationToken&) override {
      ++prepared;
      first = a;
      last = b;
      step = d;
      return {status};
    }
    wr::CurrentSample sample(wr::GeoPoint, wr::TimePoint time) const override {
      ++samples;
      return {prepared && time >= first && time <= last,
              {1, 0},
              {wr::EnvironmentalSource::XtdCurrentPrediction}};
    }
    std::string identity() const override { return "contract-prediction"; }
  };
  auto j = fixture::scenario("prediction current");
  j["currents"] = true;
  j["missing_current"] = true;
  auto r = fixture::configure(j);
  auto e = environment(j, r);
  auto prediction = std::make_shared<Prediction>();
  e.xtdCurrent = prediction;
  Options o;
  o.improvementAttempts = 0;
  auto routed = Engine{}.route(r, e, o);
  require(routed.route.validation.passed,
          "prepared prediction current is used");
  require(prediction->prepared == 1 && prediction->samples > 0,
          "prediction preparation and sampling forwarded");
  require(std::abs(routed.route.metrics.elapsed.count() - 20. / 7 * 3600) < 5,
          "prediction current contributes to ground speed");
  require((prediction->last - prediction->first).count() /
                      prediction->step.count() +
                  1 <=
              r.limits.maximumCurrentCacheSlices,
          "prediction slice allowance respected");
  wr::PolarPerformanceModel polar(r.vessel);
  require(wr::RouteValidator{}.validate(r, e, polar, routed.route.legs).passed,
          "original prediction provider independently replays the result");
  prediction->status = wr::CoverageStatus::ResourceLimit;
  require(Engine{}.route(r, e, o).route.status ==
              wr::RoutingStatus::ResourceLimitReached,
          "prediction preparation cannot bypass provider resource refusal");
  prediction->status = wr::CoverageStatus::Cancelled;
  require(Engine{}.route(r, e, o).route.status == wr::RoutingStatus::Cancelled,
          "prediction preparation cancellation is preserved");
  r.limits.maximumPredictionAreaSquareDegrees = .01;
  const auto previous = prediction->prepared;
  require(Engine{}.route(r, e, o).route.status ==
                  wr::RoutingStatus::ResourceLimitReached &&
              prediction->prepared == previous,
          "oversized prediction area rejected before provider work");
  class Climate final : public wr::ClimatologyProvider {
  public:
    wr::ParameterCoverage coverage() const override {
      return {true, {}, {}, {-180, -85, 180, 85}, "contract-climate"};
    }
    wr::WindSample wind(wr::GeoPoint, wr::TimePoint) const override {
      return {true, {0, -14}, {wr::EnvironmentalSource::Climatology}};
    }
    std::string identity() const override { return "contract-climate"; }
  };
  j = fixture::scenario("climatology");
  j["missing_wind"] = true;
  r = fixture::configure(j);
  e = environment(j, r);
  e.climatology = std::make_shared<Climate>();
  require(!Engine{}.route(r, e, o).route.validation.passed,
          "unacknowledged climatology is not substituted");
  r.environment.climatology = wr::ClimatologyFallbackPolicy::AllowWithWarning;
  auto climate = Engine{}.route(r, e, o);
  require(climate.route.validation.passed,
          "authorised climatology is forwarded");
  require(wr::RouteValidator{}.validate(r, e, polar, climate.route.legs).passed,
          "climatology result replays against original providers");
}
void guidance() {
  class BlockedHint final : public wr::LandAndBoundaryProvider {
  public:
    bool pointForbidden(wr::GeoPoint) const override { return true; }
    bool segmentForbidden(wr::GeoPoint, wr::GeoPoint, double) const override {
      return true;
    }
    double distanceToForbiddenNm(wr::GeoPoint) const override { return 0; }
    std::string identity() const override {
      return "deliberately wrong blocked map";
    }
  };
  auto j = fixture::scenario("misleading guidance slalom");
  Json::Reader parser;
  require(parser.parse("[[6,-12,7,4],[13,-4,14,12]]", j["rectangles_nm"]),
          "slalom parse");
  Options o;
  o.geographyGuidance = std::make_shared<BlockedHint>();
  auto r = run(j, o);
  audit(r.route, j);
  require(r.atlas.waypoints > 2 && r.atlas.guidanceGeometryQueries > 0,
          "coarse blocked hints cannot reject actually admissible vertices");
  j = fixture::scenario("misleading safe guidance");
  j["validation_block"] = true;
  o.geographyGuidance = std::make_shared<wr::OpenWaterProvider>();
  o.maximumEdgeAttempts = 8;
  unsigned callbacks = 0;
  o.onIncumbent = [&](const auto&) { ++callbacks; };
  auto unsafe = run(j, o);
  require(!unsafe.route.validation.passed && unsafe.route.legs.empty() &&
              callbacks == 0,
          "safe hints cannot bypass authoritative validation or publish an "
          "invalid incumbent");
  j = fixture::scenario("misleading safe depth");
  j["depth"] = 2;
  j["min_depth"] = 5;
  require(!run(j, o).route.validation.passed,
          "guidance cannot bypass actual depth limits");
}
int main(int argc, char** argv) {
  const std::map<std::string, void (*)()> tests{
      {"math", mathematics},        {"preflight", preflight},
      {"resources", resources},     {"waiting", waiting},
      {"weather", weather},         {"coastal", coastal},
      {"propulsion", propulsion},   {"observers", observers},
      {"concurrency", concurrency}, {"providers", providers},
      {"guidance", guidance}};
  try {
    for (const auto& [name, test] : tests)
      if (argc == 1 || argv[1] == name) {
        test();
        std::cout << name << " passed\n";
      }
    require(checks > 0, "unknown test group");
    std::cout << checks << " checks passed\n";
  } catch (const std::exception& e) {
    std::cerr << e.what() << '\n';
    return 1;
  }
}
