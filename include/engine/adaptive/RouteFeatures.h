// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "supercpn/weather_routing/Engine.h"
#include "supercpn/weather_routing/Providers.h"
#include <algorithm>
#include <cmath>
#include <map>
#include <numbers>
#include <chrono>
#include <queue>
#include <set>

namespace weather_routing::adaptive {
namespace wr = supercpn::weather_routing;
struct Features {
  double directNm{}, sketchNm{}, coastalFraction{}, detourRatio{1}, vmgKnots{},
      hours{};
  double currentKnots{}, currentMinKnots{INFINITY}, currentChange3h{},
      currentChange1h{}, currentDirectionSpan{}, currentSpatialDelta{},
      windChange3h{}, twa{};
  double localCurrentGradientKnPerNm{};
  unsigned currentSamples{}, currentMissingSamples{}, currentTemporalPairs{},
      currentProbeLocations{}, currentCompleteLocations{};
  bool chordBlocked{}, sketchResolved{}, windAtStart{}, currentAtStart{},
      rapidCurrent{}, offshore{};
  // Probe counts alone cannot establish a bound over the sailing corridor.
  // A future native-grid envelope may supply this; sparse probes never do.
  bool currentSpatialCoverageBounded{};
  unsigned geometryQueries{}, weatherSamples{};
  double milliseconds{};
  std::vector<wr::GeoPoint> path;
  std::vector<wr::GeoPoint> hourlyCenters;
  std::vector<wr::GeoPoint> localShearCenters, geometryHotspots;
};

inline Features Characterize(
    const wr::RoutingRequest& r, const wr::RoutingEnvironment& env,
    const wr::LandAndBoundaryProvider& polygons, unsigned queryLimit = 4096,
    std::chrono::milliseconds timeLimit = std::chrono::milliseconds{400}) {
  const auto started = std::chrono::steady_clock::now();
  Features f;
  f.directNm = wr::distanceNm(r.start, r.destination);
  f.path = {r.start, r.destination};
  f.sketchNm = f.directNm;
  if (!std::isfinite(r.start.latitude) || !std::isfinite(r.start.longitude) ||
      !std::isfinite(r.destination.latitude) ||
      !std::isfinite(r.destination.longitude) ||
      std::abs(r.start.latitude) > 85 ||
      std::abs(r.destination.latitude) > 85 ||
      std::abs(r.start.longitude) > 180 ||
      std::abs(r.destination.longitude) > 180)
    return f;
  auto allowed = [&] {
    return f.geometryQueries < queryLimit &&
           std::chrono::steady_clock::now() - started < timeLimit;
  };
  bool collectLocalGeometry = false;
  std::vector<wr::GeoPoint> blockedEdges;
  auto blocked = [&](wr::GeoPoint a, wr::GeoPoint b) {
    ++f.geometryQueries;
    const bool hit = polygons.segmentForbidden(a, b, 0.);
    if (hit && collectLocalGeometry && blockedEdges.size() < 64)
      blockedEdges.push_back(wr::destinationPoint(
          a, wr::initialBearingDegrees(a, b), wr::distanceNm(a, b) / 2));
    return hit;
  };
  f.chordBlocked = blocked(r.start, r.destination);
  f.sketchResolved = !f.chordBlocked;
  if (f.chordBlocked && f.directNm > .01 && allowed()) {
    collectLocalGeometry = true;
    // Fixed-size, geodesic corridor A*. It may miss a narrow channel or a
    // large detour. Failure is recorded as unresolved, never as infeasibility.
    constexpr int nx = 40, ny = 25, originX = 4, goalX = 36, middleY = 12;
    const double lateral = std::clamp(f.directNm * .25, 2., 300.);
    const double bearing = wr::initialBearingDegrees(r.start, r.destination);
    std::vector<wr::GeoPoint> points(nx * ny);
    std::vector<double> cost(nx * ny, INFINITY);
    std::vector<int> parent(nx * ny, -1);
    auto point = [&](int x, int y) {
      auto mid = wr::destinationPoint(r.start, bearing,
                                      f.directNm * (x - originX) / 32.);
      return wr::destinationPoint(
          mid, wr::initialBearingDegrees(mid, r.destination) + 90,
          lateral * (y - middleY) / 12.);
    };
    for (int y = 0; y < ny; ++y)
      for (int x = 0; x < nx; ++x) points[y * nx + x] = point(x, y);
    const int first = middleY * nx + originX, last = middleY * nx + goalX;
    points[first] = r.start;
    points[last] = r.destination;
    std::priority_queue<std::pair<double, int>,
                        std::vector<std::pair<double, int>>, std::greater<>>
        queue;
    std::map<std::pair<int, int>, bool> edges;
    cost[first] = 0;
    queue.emplace(f.directNm, first);
    while (!queue.empty() && allowed()) {
      auto [rank, id] = queue.top();
      queue.pop();
      if (rank > cost[id] + wr::distanceNm(points[id], r.destination) + 1e-8)
        continue;
      if (id == last) {
        f.path.clear();
        for (int p = last; p != -1; p = parent[p]) f.path.push_back(points[p]);
        std::reverse(f.path.begin(), f.path.end());
        f.sketchResolved = true;
        f.sketchNm = cost[last];
        break;
      }
      const int x = id % nx, y = id / nx;
      for (int dy = -1; dy <= 1 && allowed(); ++dy)
        for (int dx = -1; dx <= 1 && allowed(); ++dx) {
          if ((dx == 0 && dy == 0) || x + dx < 0 || x + dx >= nx ||
              y + dy < 0 || y + dy >= ny)
            continue;
          const int next = (y + dy) * nx + x + dx;
          if (std::abs(points[next].latitude) >
              r.constraints.maximumLatitudeDegrees)
            continue;
          auto key = std::minmax(id, next);
          auto found = edges.find(key);
          bool hit;
          if (found != edges.end())
            hit = found->second;
          else {
            hit = blocked(points[id], points[next]);
            edges[key] = hit;
          }
          if (hit) continue;
          const double candidate =
              cost[id] + wr::distanceNm(points[id], points[next]);
          if (candidate + 1e-8 < cost[next]) {
            cost[next] = candidate;
            parent[next] = id;
            queue.emplace(
                candidate + wr::distanceNm(points[next], r.destination), next);
          }
        }
    }
  }
  f.detourRatio = f.directNm > 0 ? f.sketchNm / f.directNm : 1.;
  if (f.sketchResolved)
    for (auto p : blockedEdges) {
      if (f.geometryHotspots.size() >= 32) break;
      if (std::any_of(f.path.begin(), f.path.end(), [&](wr::GeoPoint q) {
            return wr::distanceNm(p, q) <= std::max(15., f.directNm / 16.);
          }))
        f.geometryHotspots.push_back(p);
    }
  std::vector<wr::GeoPoint> probes;
  // Arc-length samples along the sketch; never replace the configured route.
  double total = 0;
  std::vector<double> lengths;
  for (std::size_t i = 1; i < f.path.size(); ++i) {
    total += wr::distanceNm(f.path[i - 1], f.path[i]);
    lengths.push_back(total);
  }
  for (int i = 0; i <= 12; ++i) {
    double d = total * i / 12.;
    auto it = std::lower_bound(lengths.begin(), lengths.end(), d);
    std::size_t leg =
        std::min<std::size_t>(it - lengths.begin(), lengths.size() - 1);
    double begin = leg ? lengths[leg - 1] : 0.;
    probes.push_back(wr::destinationPoint(
        f.path[leg], wr::initialBearingDegrees(f.path[leg], f.path[leg + 1]),
        std::max(0., d - begin)));
  }
  unsigned near = 0, coastSamples = 0;
  for (std::size_t i = 1; i + 1 < probes.size(); ++i)
    if (allowed()) {
      ++f.geometryQueries;
      ++coastSamples;
      if (polygons.distanceToForbiddenNm(probes[i]) < 3.) {
        ++near;
        f.hourlyCenters.push_back(probes[i]);
      }
    }
  f.coastalFraction =
      double(near) / std::max<std::size_t>(1, probes.size() - 2);
  for (auto point : {r.start, r.destination})
    if (allowed()) {
      ++f.geometryQueries;
      if (polygons.distanceToForbiddenNm(point) < 5.)
        f.hourlyCenters.push_back(point);
    }
  std::vector<double> speeds;
  const double fallbackSpeed = 6.;
  const auto speedAt = [&](wr::GeoPoint p, wr::TimePoint time, double bearing) {
    ++f.weatherSamples;
    auto wind = env.grib->wind(p, time);
    auto current = r.environment.useCurrent ? env.grib->current(p, time)
                                            : wr::CurrentSample{};
    auto waves =
        r.environment.useWaves ? env.grib->waves(p, time) : wr::WaveSample{};
    if (!wind.available) return 0.;
    const double from =
        std::atan2(wind.velocity.eastKnots, wind.velocity.northKnots) * 180 /
            std::numbers::pi +
        180;
    const double tws =
        std::hypot(wind.velocity.eastKnots, wind.velocity.northKnots);
    double best = 0;
    for (double offset = -r.options.maximumSearchAngleDegrees;
         offset <= r.options.maximumSearchAngleDegrees; offset += 10) {
      double heading = bearing + offset,
             twa = std::abs(wr::angularDifferenceDegrees(heading, from));
      if (twa < r.constraints.minimumTrueWindAngleDegrees ||
          twa > r.constraints.maximumTrueWindAngleDegrees)
        continue;
      for (const auto& c : env.performance->candidates(
               tws, twa, waves, wr::PropulsionMode::Sail, wr::Duration{}))
        if (c.valid) {
          const double projection =
              c.speedThroughWaterKnots *
                  std::cos(offset * std::numbers::pi / 180.) +
              (r.environment.useCurrent && current.available
                   ? current.velocity.eastKnots *
                             std::sin(bearing * std::numbers::pi / 180.) +
                         current.velocity.northKnots *
                             std::cos(bearing * std::numbers::pi / 180.)
                   : 0.);
          best = std::max(best, projection);
        }
    }
    return best;
  };
  auto wind = env.grib->wind(r.start, r.departure);
  auto current = r.environment.useCurrent
                     ? env.grib->current(r.start, r.departure)
                     : wr::CurrentSample{};
  f.windAtStart = wind.available;
  f.currentAtStart = current.available;
  if (wind.available) {
    double from =
        std::atan2(wind.velocity.eastKnots, wind.velocity.northKnots) * 180 /
            std::numbers::pi +
        180;
    f.twa = std::abs(wr::angularDifferenceDegrees(
        wr::initialBearingDegrees(r.start, r.destination), from));
  }
  const double initial = speedAt(
      r.start, r.departure, wr::initialBearingDegrees(r.start, r.destination));
  double preliminaryHours =
      f.sketchNm / std::max(1., initial > 0 ? initial : fallbackSpeed);
  std::vector<std::pair<wr::GeoPoint, wr::Vector2>> spatialCurrents;
  const double width = std::clamp(f.directNm * .2, 2., 10.);
  for (std::size_t i = 0; i < probes.size(); i += 6)
    for (double lateral : {-width, 0., width}) {
      const auto p = wr::destinationPoint(
          probes[i], wr::initialBearingDegrees(probes[i], r.destination) + 90,
          lateral);
      auto time =
          r.departure + wr::Duration{static_cast<long>(
                            preliminaryHours * 3600 * i / (probes.size() - 1))};
      auto v = speedAt(p, time, wr::initialBearingDegrees(p, r.destination));
      if (v > 0) speeds.push_back(v);
      auto prevC = r.environment.useCurrent ? env.grib->current(p, r.departure)
                                            : wr::CurrentSample{};
      auto prevW = env.grib->wind(p, r.departure);
      int previousHour = 0;
      double localDelta = 0, localPeak = 0, localSpan = 0;
      unsigned localCurrentSamples = 0;
      if (r.environment.useCurrent) {
        ++f.currentProbeLocations;
        if (!prevC.available)
          ++f.currentMissingSamples;
        else
          ++localCurrentSamples;
      }
      std::vector<wr::Vector2> vectors;
      if (prevC.available) vectors.push_back(prevC.velocity);
      if (prevC.available) {
        ++f.currentSamples;
        const double speed =
            std::hypot(prevC.velocity.eastKnots, prevC.velocity.northKnots);
        f.currentKnots = std::max(f.currentKnots, speed);
        f.currentMinKnots = std::min(f.currentMinKnots, speed);
        for (auto [position, v] : spatialCurrents) {
          const double difference =
              std::hypot(v.eastKnots - prevC.velocity.eastKnots,
                         v.northKnots - prevC.velocity.northKnots);
          f.currentSpatialDelta = std::max(f.currentSpatialDelta, difference);
          const double distance = wr::distanceNm(position, p);
          if (distance > .01 && distance <= 25) {
            const double gradient = difference / distance;
            f.localCurrentGradientKnPerNm =
                std::max(f.localCurrentGradientKnPerNm, gradient);
            if (gradient *
                    std::max(1.,
                             (initial > 0 ? initial : fallbackSpeed) + speed) >
                .5) {
              f.localShearCenters.push_back(position);
              f.localShearCenters.push_back(p);
            }
          }
        }
        spatialCurrents.push_back({p, prevC.velocity});
      }
      for (int h : {1, 3, 6, 9, 12}) {
        auto c = r.environment.useCurrent
                     ? env.grib->current(p, r.departure + std::chrono::hours{h})
                     : wr::CurrentSample{};
        auto w = env.grib->wind(p, r.departure + std::chrono::hours{h});
        ++f.weatherSamples;
        if (r.environment.useCurrent) {
          if (!c.available)
            ++f.currentMissingSamples;
          else
            ++localCurrentSamples;
        }
        if (c.available) {
          ++f.currentSamples;
          const double speed =
              std::hypot(c.velocity.eastKnots, c.velocity.northKnots);
          localPeak = std::max(localPeak, speed);
          f.currentKnots = std::max(f.currentKnots, speed);
          f.currentMinKnots = std::min(f.currentMinKnots, speed);
          if (prevC.available) {
            ++f.currentTemporalPairs;
            const auto change =
                std::hypot(c.velocity.eastKnots - prevC.velocity.eastKnots,
                           c.velocity.northKnots - prevC.velocity.northKnots) /
                (h - previousHour);
            localDelta = std::max(localDelta, change);
            f.currentChange1h = std::max(f.currentChange1h, change);
            f.currentChange3h = std::max(f.currentChange3h, change * 3);
          }
          for (auto v : vectors)
            if (std::hypot(v.eastKnots, v.northKnots) > .1 && speed > .1)
              localSpan = std::max(
                  localSpan,
                  std::abs(wr::angularDifferenceDegrees(
                      std::atan2(v.eastKnots, v.northKnots) * 180 /
                          std::numbers::pi,
                      std::atan2(c.velocity.eastKnots, c.velocity.northKnots) *
                          180 / std::numbers::pi)));
          vectors.push_back(c.velocity);
        }
        if (w.available && prevW.available)
          f.windChange3h = std::max(
              f.windChange3h,
              std::hypot(w.velocity.eastKnots - prevW.velocity.eastKnots,
                         w.velocity.northKnots - prevW.velocity.northKnots) *
                  3 / (h - previousHour));
        prevW = w;
        prevC = c;
        previousHour = h;
      }
      f.currentDirectionSpan = std::max(f.currentDirectionSpan, localSpan);
      if (localCurrentSamples == 6) ++f.currentCompleteLocations;
      if (localDelta > .5 || (localSpan > 120 && localPeak > .8))
        f.hourlyCenters.push_back(p);
    }
  if (!speeds.empty()) {
    std::sort(speeds.begin(), speeds.end());
    f.vmgKnots = speeds[speeds.size() / 2];
  } else
    f.vmgKnots = initial;
  f.hours = f.vmgKnots > 0 ? f.sketchNm / f.vmgKnots : 0.;
  f.rapidCurrent = r.environment.useCurrent &&
                   (f.currentChange1h > .5 ||
                    (f.currentDirectionSpan > 120 && f.currentKnots > .8));
  f.offshore = f.directNm >= 120 && f.sketchResolved && coastSamples >= 6 &&
               f.coastalFraction < .15 && f.detourRatio < 1.15;
  f.milliseconds = std::chrono::duration<double, std::milli>(
                       std::chrono::steady_clock::now() - started)
                       .count();
  return f;
}
}  // namespace weather_routing::adaptive
