// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "atlas_routing/Engine.h"
#include <json/json.h>
#include <cmath>
#include <map>
#include <numbers>

namespace fixture {
namespace wr = supercpn::weather_routing;
struct QueryTimer {
  std::uint64_t& total;
  bool enabled;
  std::chrono::steady_clock::time_point started;
  QueryTimer(std::uint64_t& output, bool on) : total(output), enabled(on) {
    if (enabled) started = std::chrono::steady_clock::now();
  }
  ~QueryTimer() {
    if (enabled)
      total += std::chrono::duration_cast<std::chrono::nanoseconds>(
                   std::chrono::steady_clock::now() - started)
                   .count();
  }
};
inline wr::TimePoint epoch{wr::Duration{1789862400}};
inline wr::GeoPoint offset(wr::GeoPoint origin, double east, double north) {
  return wr::destinationPoint(origin,
                              std::atan2(east, north) * 180 / std::numbers::pi,
                              std::hypot(east, north));
}
inline wr::RoutingRequest request() {
  wr::RoutingRequest r;
  r.start = {30, -140};
  r.destination = offset(r.start, 20, 0);
  r.departure = epoch;
  r.environment.useCurrent = false;
  r.environment.useWaves = false;
  r.options.timeStep = std::chrono::hours{1};
  r.options.minimumTimeStep = std::chrono::minutes{5};
  r.options.maximumSearchAngleDegrees = 180;
  r.options.destinationToleranceNm = .05;
  r.options.spatialCellNm = .5;
  r.options.graphCorridorWidthNm = 10;
  r.options.maximumGraphCorridorWidthNm = 80;
  r.limits.maximumRouteDuration = std::chrono::hours{48};
  r.limits.maximumGeneratedStates = 1000000;
  r.limits.maximumRetainedStates = 80000;
  r.limits.maximumGraphLabels = 100000;
  wr::PerformanceProfile profile;
  profile.identity = "synthetic-sail";
  for (double wind : {0., 10., 20., 40.})
    profile.rows.push_back({wind,
                            {{0, 0},
                             {30, 0},
                             {40, 4},
                             {50, 5},
                             {90, 6},
                             {130, 6},
                             {160, 5},
                             {180, 4}}});
  for (auto& p : profile.rows.front().points) p.boatSpeedKnots = 0;
  r.vessel.profiles.push_back(profile);
  r.vessel.tackPenalty = wr::Duration{120};
  r.vessel.gybePenalty = wr::Duration{120};
  r.constraints.minimumTrueWindAngleDegrees = 40;
  r.constraints.maximumTrueWindAngleDegrees = 170;
  return r;
}
inline Json::Value scenario(std::string name) {
  Json::Value j;
  j["name"] = name;
  j["start"].append(30.);
  j["start"].append(-140.);
  j["east_nm"] = 20.;
  j["north_nm"] = 0.;
  j["wind_from"] = 0.;
  return j;
}

class Weather final : public wr::WeatherProvider {
public:
  Json::Value config;
  wr::GeoPoint origin;
  mutable std::uint64_t calls{}, hits{};
  using CacheKey = std::tuple<int, double, double, std::int64_t>;
  mutable std::map<CacheKey, wr::Vector2> cache;
  bool cacheEnabled{};
  explicit Weather(Json::Value j, wr::GeoPoint start)
      : config(std::move(j)),
        origin(start),
        cacheEnabled(config.get("cache", false).asBool()) {}
  wr::ParameterCoverage coverage(bool available = true) const {
    wr::ParameterCoverage p;
    p.available = available;
    p.area = {-180, -85, 180, 85};
    p.datasetIdentity = "analytic-fixture";
    return p;
  }
  wr::ParameterCoverage windCoverage() const override {
    return coverage(!config.get("missing_wind", false).asBool());
  }
  wr::ParameterCoverage currentCoverage() const override {
    return coverage(!config.get("missing_current", false).asBool());
  }
  wr::ParameterCoverage waveCoverage() const override {
    return coverage(config.isMember("wave_height"));
  }
  bool covered(wr::TimePoint t) const {
    return (t - epoch).count() <=
           config.get("coverage_hours", 10000).asDouble() * 3600;
  }
  wr::Vector2 vector(int which, wr::GeoPoint p, wr::TimePoint t) const {
    ++calls;
    CacheKey key{which, p.latitude, p.longitude, t.time_since_epoch().count()};
    if (cacheEnabled)
      if (auto i = cache.find(key); i != cache.end()) {
        ++hits;
        return i->second;
      }
    const double hours = (t - epoch).count() / 3600.;
    auto bearing =
        wr::initialBearingDegrees(origin, p) * std::numbers::pi / 180;
    auto distance = wr::distanceNm(origin, p);
    const double east = distance * std::sin(bearing),
                 north = distance * std::cos(bearing);
    wr::Vector2 value;
    if (which == 0) {
      double speed = config.get("wind_speed", 14.).asDouble(),
             from = config.get("wind_from", 0.).asDouble();
      if (config.get("variable", false).asBool()) {
        from += 50 * std::sin(hours / 5 + east / 35);
        speed += 3 * std::sin(hours / 3 + north / 18);
      }
      if (config.get("wind_shift", false).asBool())
        from += hours >= config.get("shift_hour", 3.).asDouble() ? 110 : 0;
      if (config.get("storm", false).asBool() &&
          std::hypot(east - 10 - hours, north) < 3)
        speed = 60;
      value = wr::speedDirectionToVector(speed, from + 180);
    } else {
      double speed = config.get("current_speed", 0.).asDouble(),
             to = config.get("current_to", 90.).asDouble();
      if (config.get("tidal", false).asBool())
        speed *= std::sin(2 * std::numbers::pi * hours / 12.42 +
                          config.get("tidal_phase", 0.).asDouble());
      if (config.get("jet", false).asBool())
        speed += 8 * std::exp(-std::pow((north - 6) / 1.8, 2));
      value = wr::speedDirectionToVector(speed, to);
    }
    if (cacheEnabled) {
      if (cache.size() >= 100000) cache.erase(cache.begin());
      cache.emplace(key, value);
    }
    return value;
  }
  wr::WindSample wind(wr::GeoPoint p, wr::TimePoint t) const override {
    auto v = vector(0, p, t);
    wr::WindSample s;
    s.available = covered(t) && !config.get("missing_wind", false).asBool();
    s.velocity = v;
    s.metadata.source = wr::EnvironmentalSource::SyntheticTestField;
    s.metadata.datasetIdentity = "analytic-fixture";
    return s;
  }
  wr::CurrentSample current(wr::GeoPoint p, wr::TimePoint t) const override {
    auto v = vector(1, p, t);
    wr::CurrentSample s;
    s.available = covered(t) && !config.get("missing_current", false).asBool();
    s.velocity = v;
    s.metadata.source = wr::EnvironmentalSource::SyntheticTestField;
    s.metadata.datasetIdentity = "analytic-fixture";
    return s;
  }
  wr::WaveSample waves(wr::GeoPoint, wr::TimePoint t) const override {
    ++calls;
    wr::WaveSample s;
    s.available = covered(t) && config.isMember("wave_height");
    s.significantHeightMetres = config.get("wave_height", 0.).asDouble();
    return s;
  }
  std::string identity() const override { return "analytic-synthetic-field"; }
};

class Land final : public wr::LandAndBoundaryProvider {
public:
  std::shared_ptr<const wr::LandAndBoundaryProvider> base;
  Json::Value config;
  wr::GeoPoint origin;
  mutable std::uint64_t calls{}, validationCalls{};
  mutable std::uint64_t queryNanoseconds{};
  bool profileEnabled{};
  Land(Json::Value j, wr::GeoPoint start,
       std::shared_ptr<const wr::LandAndBoundaryProvider> supplied = {})
      : config(std::move(j)), origin(start) {
    profileEnabled = config.get("profile_providers", false).asBool();
    if (supplied) {
      base = std::move(supplied);
      return;
    }
    std::vector<std::vector<wr::GeoPoint>> polygons;
    for (const auto& p : config["rectangles_nm"]) {
      double a = p[0].asDouble(), b = p[1].asDouble(), c = p[2].asDouble(),
             d = p[3].asDouble();
      polygons.push_back({offset(start, a, b), offset(start, c, b),
                          offset(start, c, d), offset(start, a, d)});
    }
    for (const auto& poly : config["polygons"]) {
      std::vector<wr::GeoPoint> p;
      for (const auto& q : poly)
        p.push_back({q[0].asDouble(), q[1].asDouble()});
      polygons.push_back(std::move(p));
    }
    base = std::make_shared<wr::PolygonBoundaryProvider>(std::move(polygons));
  }
  bool pointForbidden(wr::GeoPoint p) const override {
    QueryTimer timer(queryNanoseconds, profileEnabled);
    ++calls;
    return base->pointForbidden(p);
  }
  bool segmentForbidden(wr::GeoPoint a, wr::GeoPoint b,
                        double m) const override {
    QueryTimer timer(queryNanoseconds, profileEnabled);
    ++calls;
    return base->segmentForbidden(a, b, m);
  }
  bool segmentFromKnownSafeForbidden(wr::GeoPoint a, wr::GeoPoint b,
                                     double m) const override {
    QueryTimer timer(queryNanoseconds, profileEnabled);
    ++calls;
    return base->segmentFromKnownSafeForbidden(a, b, m);
  }
  double east(wr::GeoPoint p) const {
    return wr::distanceNm(origin, p) *
           std::sin(wr::initialBearingDegrees(origin, p) * std::numbers::pi /
                    180);
  }
  bool segmentFromKnownSafeForbiddenAt(wr::GeoPoint a, wr::GeoPoint b,
                                       wr::TimePoint t,
                                       double m) const override {
    QueryTimer timer(queryNanoseconds, profileEnabled);
    ++calls;
    if (config.get("gate", false).asBool() &&
        (t - epoch).count() <
            config.get("gate_opens_hours", 4.).asDouble() * 3600 &&
        std::min(east(a), east(b)) <
            config.get("gate_east_nm", 10.).asDouble() &&
        std::max(east(a), east(b)) >=
            config.get("gate_east_nm", 10.).asDouble())
      return true;
    return base->segmentFromKnownSafeForbiddenAt(a, b, t, m);
  }
  bool validationSegmentFromKnownSafeForbiddenAt(wr::GeoPoint a, wr::GeoPoint b,
                                                 wr::TimePoint t,
                                                 double m) const override {
    ++validationCalls;
    if (config.get("validation_block", false).asBool()) return true;
    return segmentFromKnownSafeForbiddenAt(a, b, t, m);
  }
  double distanceToForbiddenNm(wr::GeoPoint p) const override {
    QueryTimer timer(queryNanoseconds, profileEnabled);
    ++calls;
    return base->distanceToForbiddenNm(p);
  }
  std::optional<double> depthMetres(wr::GeoPoint p) const override {
    QueryTimer timer(queryNanoseconds, profileEnabled);
    ++calls;
    if (config.get("missing_depth", false).asBool()) return {};
    if (config.isMember("depth")) return config["depth"].asDouble();
    // Real shoreline fixtures must preserve unavailable chart depth. The
    // synthetic open-water fixtures explicitly assume a deep, flat seabed.
    if (config.isMember("shoreline")) return base->depthMetres(p);
    return 1000.;
  }
  std::string identity() const override { return base->identity(); }
};

inline wr::RoutingRequest configure(const Json::Value& j) {
  auto r = request();
  r.start = {j["start"][0].asDouble(), j["start"][1].asDouble()};
  r.destination =
      j.isMember("end")
          ? wr::GeoPoint{j["end"][0].asDouble(), j["end"][1].asDouble()}
          : offset(r.start, j.get("east_nm", 20.).asDouble(),
                   j.get("north_nm", 0.).asDouble());
  r.environment.useCurrent = j.get("currents", false).asBool();
  r.environment.useWaves = j.isMember("wave_height") || j.isMember("max_wave");
  r.options.timeStep = wr::Duration{j.get("step_seconds", 3600).asInt64()};
  r.options.allowWaiting = j.get("waiting", true).asBool();
  r.options.maximumWait = wr::Duration{
      static_cast<long>(3600 * j.get("maximum_wait_hours", 6.).asDouble())};
  r.limits.maximumRouteDuration = wr::Duration{
      static_cast<long>(j.get("max_hours", 48.).asDouble() * 3600)};
  r.limits.maximumGeneratedStates =
      j.get("max_states", Json::UInt64(1000000)).asUInt64();
  r.constraints.landSafetyMarginNm = j.get("margin_nm", 0.).asDouble();
  r.constraints.minimumTrueWindAngleDegrees =
      j.get("from_angle", 40.).asDouble();
  r.constraints.maximumTrueWindAngleDegrees =
      j.get("to_angle", 170.).asDouble();
  r.constraints.maximumTrueWindKnots = j.get("max_wind", 50.).asDouble();
  if (j.isMember("max_wave"))
    r.constraints.maximumWaveHeightMetres = j["max_wave"].asDouble();
  if (j.isMember("min_depth"))
    r.constraints.minimumDepthMetres = j["min_depth"].asDouble();
  r.vessel.tackPenalty = wr::Duration{j.get("tack_seconds", 120).asInt64()};
  r.vessel.gybePenalty = wr::Duration{j.get("gybe_seconds", 120).asInt64()};
  if (j.get("isotropic", false).asBool()) {
    for (auto& row : r.vessel.profiles.front().rows)
      for (auto& p : row.points) p.boatSpeedKnots = 6;
    r.constraints.minimumTrueWindAngleDegrees = 0;
    r.constraints.maximumTrueWindAngleDegrees = 180;
    r.vessel.tackPenalty = {};
    r.vessel.gybePenalty = {};
  }
  if (j.get("motor", false).asBool()) {
    r.vessel.propulsion.allowMotor = true;
    r.vessel.propulsion.configuredMotorSpeedKnots = 6;
    r.vessel.propulsion.motorBelowSailingSpeedKnots = 5;
  }
  return r;
}
inline atlas_routing::Options options(const Json::Value& j) {
  atlas_routing::Options o;
  o.maximumWaypoints = j.get("atlas_waypoints", o.maximumWaypoints).asUInt();
  o.neighbours = j.get("atlas_neighbours", o.neighbours).asUInt();
  o.refinementPasses = j.get("atlas_passes", o.refinementPasses).asUInt();
  o.improvementAttempts =
      j.get("atlas_improvement", o.improvementAttempts).asUInt();
  o.maximumEdgeAttempts =
      j.get("atlas_edges", Json::UInt64(o.maximumEdgeAttempts)).asUInt64();
  o.minimumCellNm = j.get("atlas_cell", o.minimumCellNm).asDouble();
  o.timeDependentGuidance =
      j.get("atlas_value_field", o.timeDependentGuidance).asBool();
  o.weatherFirstPilot =
      j.get("atlas_weather_pilot", o.weatherFirstPilot).asBool();
  o.weatherLaneProbes =
      j.get("atlas_weather_lanes", o.weatherLaneProbes).asBool();
  o.averageGuidanceTiles =
      j.get("atlas_average_tiles", o.averageGuidanceTiles).asBool();
  o.alwaysBuildWeatherField =
      j.get("atlas_always_field", o.alwaysBuildWeatherField).asBool();
  o.coastalCellNm = std::max(
      o.minimumCellNm, j.get("atlas_coastal_cell", o.coastalCellNm).asDouble());
  return o;
}
}  // namespace fixture
