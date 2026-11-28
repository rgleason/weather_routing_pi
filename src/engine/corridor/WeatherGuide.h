// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "RoutingInternal.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <numbers>

namespace corridor_routing {
namespace wr = supercpn::weather_routing;

// The forecast estimate only orders proposals. It does not provide a route
// segment, establish transit rights, or participate in arrival validation.
class WeatherGuide {
  static constexpr int stages = 16;
  static constexpr int lanes = 11;
  static constexpr double radians = std::numbers::pi / 180.;
  static constexpr double earthRadiusNm = 3440.065;

  struct Label {
    wr::Duration elapsed{wr::Duration::max()};
    int previous{-1};
  };

  wr::GeoPoint start_;
  double bearing_;
  double distance_;
  double spacing_;
  bool available_{};
  unsigned samples_{};
  double separation_{};
  std::array<double, stages + 1> offsets_{};
  std::array<wr::Duration, stages + 1> remaining_{};

  std::pair<double, double> coordinates(wr::GeoPoint point) const {
    const auto angle = (wr::initialBearingDegrees(start_, point) - bearing_) * radians;
    const auto distance = wr::distanceNm(start_, point) / earthRadiusNm;
    return {earthRadiusNm * std::atan2(std::sin(distance) * std::cos(angle), std::cos(distance)),
            earthRadiusNm * std::asin(std::clamp(std::sin(distance) * std::sin(angle), -1., 1.))};
  }

  static double velocityMadeGood(const wr::VesselPerformanceModel& performance,
      wr::GeoPoint point, wr::TimePoint time, const wr::EnvironmentalSnapshot& weather,
      double bearing) {
    constexpr int headings = 24;
    std::array<wr::Vector2, headings> velocities{};
    const auto tws = wr::vectorMagnitudeKnots(weather.wind.velocity);
    const auto along = wr::speedDirectionToVector(1., bearing);
    const auto across = wr::speedDirectionToVector(1., bearing + 90.);
    int count = 0;
    double best = 0.;
    for (int index = 0; index < headings; ++index) {
      const auto heading = std::fmod(bearing + 15. * index, 360.);
      const auto twa = wr::trueWindAngleDegrees(weather.wind.velocity, heading);
      double speed = 0.;
      for (const auto& candidate : performance.candidatesAt(point, time, tws, twa,
               weather.waves, wr::PropulsionMode::Sail, wr::Duration::zero())) {
        if (candidate.valid) speed = std::max(speed, candidate.speedThroughWaterKnots);
      }
      if (speed <= .05) continue;
      const auto water = wr::speedDirectionToVector(speed, heading);
      const auto east = water.eastKnots + weather.current.velocity.eastKnots;
      const auto north = water.northKnots + weather.current.velocity.northKnots;
      const auto acrossSpeed = east * across.eastKnots + north * across.northKnots;
      const auto alongSpeed = east * along.eastKnots + north * along.northKnots;
      if (std::abs(acrossSpeed) < 1e-6) best = std::max(best, alongSpeed);
      // Alternate opposing cross-track velocities to estimate progress under
      // tacking or crabbing. Real manoeuvre costs are charged by checkedMotion.
      for (int earlier = 0; earlier < count; ++earlier) {
        const auto [otherAcross, otherAlong] = velocities[earlier];
        if (otherAcross * acrossSpeed >= 0.) continue;
        const auto fraction = acrossSpeed / (acrossSpeed - otherAcross);
        best = std::max(best, std::lerp(alongSpeed, otherAlong, fraction));
      }
      velocities[count++] = {acrossSpeed, alongSpeed};
    }
    return best;
  }

public:
  WeatherGuide(const wr::RoutingRequest& request, const wr::RoutingEnvironment& environment,
               const wr::RoutingResult& incumbent, const wr::LandAndBoundaryProvider* land)
      : start_(request.start), bearing_(wr::initialBearingDegrees(start_, request.destination)),
        distance_(wr::distanceNm(start_, request.destination)),
        spacing_(std::clamp(distance_ * .045, 10., 75.)) {
    wr::PolarPerformanceModel fallback(request.vessel);
    const auto& performance = environment.performance ? *environment.performance : fallback;
    std::array<std::array<wr::GeoPoint, lanes>, stages + 1> points{};
    std::array<std::array<Label, lanes>, stages + 1> labels{};
    constexpr int middle = lanes / 2;
    for (int stage = 0; stage <= stages; ++stage) {
      const auto along = wr::destinationPoint(start_, bearing_, distance_ * stage / stages);
      const auto forward = wr::initialBearingDegrees(along, request.destination);
      for (int lane = 0; lane < lanes; ++lane)
        points[stage][lane] = wr::destinationPoint(along, forward + 90., (lane - middle) * spacing_);
    }
    points[0][middle] = start_;
    points[stages][middle] = request.destination;
    labels[0][middle].elapsed = wr::Duration::zero();

    double incumbentOffset = 0.;
    auto nearest = distance_;
    for (const auto& leg : incumbent.legs) {
      const auto [along, across] = coordinates(leg.end);
      if (const auto gap = std::abs(along - distance_ / 2); gap < nearest) {
        incumbentOffset = across;
        nearest = gap;
      }
    }

    for (int stage = 0; stage < stages; ++stage) {
      for (int lane = 0; lane < lanes; ++lane) {
        const auto elapsed = labels[stage][lane].elapsed;
        if (elapsed == wr::Duration::max()) continue;
        const auto from = points[stage][lane];
        for (int next = std::max(0, lane - 2); next <= std::min(lanes - 1, lane + 2); ++next) {
          if (request.cancellation.cancelled()) return;
          if (stage + 1 == stages && next != middle) continue;
          if (stage + 1 == stages / 2 &&
              std::abs((next - middle) * spacing_ - incumbentOffset) < spacing_ * .75)
            continue;
          const auto to = points[stage + 1][next];
          // The timed search handles both coastal approaches. The coarse
          // mesh cannot resolve departure egress or a headland at either end.
          if (land && stage > 0 && stage + 1 < stages &&
              land->segmentForbidden(from, to, 0.))
            continue;
          const auto length = wr::distanceNm(from, to);
          const auto heading = wr::initialBearingDegrees(from, to);
          const auto point = wr::destinationPoint(from, heading, length / 2.);
          const auto time = request.departure + elapsed;
          const auto weather = wr::internal::resolveEnvironment(request, environment, point, time);
          ++samples_;
          if (weather.failureStatus != wr::RoutingStatus::Complete) continue;
          const auto speed = velocityMadeGood(performance, point, time, weather.snapshot,
                                            wr::initialBearingDegrees(point, to));
          if (speed <= .1) continue;
          const auto crossing = length * 3600. / speed;
          if (crossing > (request.limits.maximumRouteDuration - elapsed).count()) continue;
          const auto arrival = elapsed + wr::Duration{static_cast<wr::Duration::rep>(std::ceil(crossing))};
          auto& label = labels[stage + 1][next];
          if (arrival < label.elapsed) label = {arrival, lane};
        }
      }
    }
    const auto arrival = labels.back()[middle].elapsed;
    if (arrival == wr::Duration::max()) return;
    for (int stage = stages, lane = middle; stage >= 0; --stage) {
      offsets_[stage] = (lane - middle) * spacing_;
      remaining_[stage] = arrival - labels[stage][lane].elapsed;
      lane = labels[stage][lane].previous;
    }
    separation_ = std::abs(offsets_[stages / 2] - incumbentOffset);
    available_ = true;
  }

  bool available() const { return available_; }
  unsigned samples() const { return samples_; }
  double separationNm() const { return separation_; }
  wr::Duration estimatedPassage() const { return remaining_.front(); }

  std::pair<double, double> costs(wr::GeoPoint point) const {
    const auto [along, across] = coordinates(point);
    const auto station = std::clamp(along * stages / distance_, 0., double(stages));
    const auto lower = std::min(int(station), stages - 1);
    const auto fraction = station - lower;
    const auto offset = std::lerp(offsets_[lower], offsets_[lower + 1], fraction);
    const auto remaining = std::lerp(double(remaining_[lower].count()),
                                     double(remaining_[lower + 1].count()), fraction);
    const auto deviation = std::abs(across - offset) + std::max(0., -along);
    return {remaining, deviation};
  }
};
}  // namespace corridor_routing
