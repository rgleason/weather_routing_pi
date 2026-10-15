// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "quick2_routing/Engine.h"
#include <cmath>
#include <memory>
#include <string>
namespace quick2_fixtures {
namespace wr = supercpn::weather_routing;
struct Fixture {
  wr::RoutingRequest request;
  wr::RoutingEnvironment environment;
};
class Weather final : public wr::WeatherProvider {
public:
  wr::TimePoint epoch;
  bool variable{}, tidal{}, waveBand{}, missing{};
  wr::ParameterCoverage windCoverage() const override {
    wr::ParameterCoverage c;
    c.available = !missing;
    c.area = {-180, -90, 180, 90};
    c.begins = epoch - std::chrono::hours{24};
    c.ends = epoch + std::chrono::hours{24 * 30};
    return c;
  }
  wr::ParameterCoverage currentCoverage() const override {
    return windCoverage();
  }
  wr::ParameterCoverage waveCoverage() const override { return windCoverage(); }
  wr::WindSample wind(wr::GeoPoint p, wr::TimePoint t) const override {
    const double h =
        std::chrono::duration<double, std::ratio<3600>>(t - epoch).count();
    return {!missing,
            wr::speedDirectionToVector(
                variable ? 14 + 3 * std::sin(h / 8) : 14,
                variable ? 140 + 35 * std::sin(h / 12 + p.longitude) : 140),
            {}};
  }
  wr::CurrentSample current(wr::GeoPoint, wr::TimePoint t) const override {
    const double h =
        std::chrono::duration<double, std::ratio<3600>>(t - epoch).count();
    return {true,
            tidal ? wr::Vector2{2.5 * std::sin(h * 6.283185307 / 12.42), .3}
                  : wr::Vector2{},
            {}};
  }
  wr::WaveSample waves(wr::GeoPoint p, wr::TimePoint) const override {
    wr::WaveSample w;
    w.available = true;
    w.periodSeconds = 5;
    w.significantHeightMetres =
        waveBand ? 3.5 * std::exp(-std::pow(p.latitude / .09, 2)) : .8;
    return w;
  }
  std::string identity() const override {
    return "frontier-benchmark-synthetic-v1";
  }
};
class Shallow final : public wr::LandAndBoundaryProvider {
public:
  bool pointForbidden(wr::GeoPoint) const override { return false; }
  bool segmentForbidden(wr::GeoPoint, wr::GeoPoint, double) const override {
    return false;
  }
  double distanceToForbiddenNm(wr::GeoPoint) const override { return 100; }
  std::optional<double> depthMetres(wr::GeoPoint) const override { return 1.; }
  std::string identity() const override { return "shallow-fixture"; }
};
inline Fixture make(const std::string& name) {
  Fixture f;
  auto& r = f.request;
  auto& e = f.environment;
  r.start = {0, -.35};
  r.destination = {0, .35};
  r.departure = wr::TimePoint{wr::Duration{1792022400}};
  wr::PerformanceProfile polar;
  polar.identity = "fixture-polar";
  polar.rows = {
      {5, {{0, 0}, {35, 2}, {45, 3}, {90, 4}, {135, 4}, {180, 3}}},
      {15, {{0, 0}, {35, 4}, {45, 5}, {90, 7}, {135, 6.5}, {180, 5.5}}},
      {30, {{0, 0}, {35, 4}, {45, 5}, {90, 7}, {135, 6.5}, {180, 5.5}}}};
  r.vessel.profiles = {polar};
  r.vessel.tackPenalty = wr::Duration{120};
  r.vessel.gybePenalty = wr::Duration{120};
  r.environment.useCurrent = false;
  r.environment.useWaves = false;
  r.options.maximumSearchAngleDegrees = 180;
  r.options.timeStep = wr::Duration{3600};
  r.options.minimumTimeStep = wr::Duration{300};
  r.options.headingStepDegrees = 10;
  r.options.spatialCellNm = 2;
  r.options.destinationToleranceNm = .25;
  r.options.graphCorridorWidthNm = 30;
  r.options.maximumGraphCorridorWidthNm = 120;
  r.limits.maximumGeneratedStates = 2000000;
  r.limits.maximumRetainedStates = 100000;
  r.limits.maximumRouteDuration = std::chrono::hours{24 * 10};
  auto w = std::make_shared<Weather>();
  w->epoch = r.departure;
  e.grib = w;
  e.landAndBoundaries = std::make_shared<wr::OpenWaterProvider>();
  if (name == "short") r.destination = {0, -.25};
  if (name == "coastal") {
    r.start = {53.4, -4.6};
    r.destination = {53.3, -6.1};
  }
  // Offshore endpoints: intermediate-scale shorelines do not resolve harbours.
  if (name == "anglesey") {
    r.start = {53.35, -3.75};
    r.destination = {53.3, -6.1};
  }
  if (name == "wight") {
    r.start = {50.7, -.95};
    r.destination = {50.65, -1.65};
  }
  if (name == "mallorca") {
    r.start = {39.2, 2.8};
    r.destination = {40, 3.4};
  }
  if (name == "strait") {
    r.start = {35.9, -5.8};
    r.destination = {36.1, -5.1};
  }
  if (name == "channel") {
    r.start = {50, -3};
    r.destination = {50, 0};
  }
  if (name == "ocean") {
    r.start = {35, -45};
    r.destination = {40, -35};
    r.options.timeStep = wr::Duration{10800};
  }
  if (name == "dateline") {
    r.start = {-20, 179.65};
    r.destination = {-20, -179.65};
  }
  if (name == "high-latitude") {
    r.start = {72, 5};
    r.destination = {72, 7};
  }
  if (name == "variable" || name == "tidal") w->variable = true;
  if (name == "tidal") {
    w->tidal = true;
    r.environment.useCurrent = true;
  }
  if (name == "upwind") {
    r.start = {.2, .2};
    r.destination = {-.2, -.2};
  }
  if (name == "wave-band") {
    w->waveBand = true;
    r.environment.useWaves = true;
  }
  if (name == "missing-wind") w->missing = true;
  if (name == "shallow") {
    e.landAndBoundaries = std::make_shared<Shallow>();
    r.constraints.minimumDepthMetres = 5;
  }
  if (name == "island" || name == "thin-island" || name == "margin" ||
      name == "land-endpoint") {
    const double width = name == "thin-island" ? .002 : .05;
    std::vector<std::vector<wr::GeoPoint>> rings{
        {{-.12, -width}, {-.12, width}, {.12, width}, {.12, -width}}};
    e.landAndBoundaries = std::make_shared<wr::PolygonBoundaryProvider>(rings);
    if (name == "margin") r.constraints.landSafetyMarginNm = .5;
    if (name == "land-endpoint") r.destination = {0, 0};
  }
  e.performance = std::make_shared<wr::PolarPerformanceModel>(r.vessel);
  return f;
}
}  // namespace quick2_fixtures
