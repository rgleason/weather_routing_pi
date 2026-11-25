// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "supercpn/weather_routing/Engine.h"
#include <algorithm>
#include <cmath>
#include <map>
#include <queue>

namespace meridian_routing {
namespace wr = supercpn::weather_routing;
// Bounded local geometry supplies a soft ordering heuristic, never a route.
// Exact sailing motions can leave this corridor and must pass normal checks.
class GeographyGuide {
  std::vector<wr::GeoPoint> path_;
  std::vector<double> remaining_;
  wr::GeoPoint destination_;
  double tail_{};
public:
  const std::vector<wr::GeoPoint>& path() const { return path_; }
  void adoptPath(std::vector<wr::GeoPoint> path) {
    path_ = std::move(path);
    tail_ = 0;
    remaining_.assign(path_.size(), 0);
    for (std::size_t n = path_.size(); n > 1; --n)
      remaining_[n - 2] = remaining_[n - 1] + wr::distanceNm(path_[n - 2], path_[n - 1]);
  }
  GeographyGuide(const wr::RoutingRequest& request,
                 const wr::LandAndBoundaryProvider* geometry,
                 double maximumReach = 60, bool coarse = false, bool expanded = false)
      : destination_(request.destination) {
    if (!geometry || request.cancellation.cancelled()) return;
    const auto distance = wr::distanceNm(request.start, request.destination);
    const auto bearing = wr::initialBearingDegrees(request.start, request.destination);
    const auto reach = std::min(distance, maximumReach);
    if (reach < .1) return;
    const auto goal = wr::destinationPoint(request.start, bearing, reach);
    const auto margin = request.constraints.landSafetyMarginNm;
    if (!geometry->segmentForbidden(request.start, goal, margin)) return;
    const int nx = coarse ? std::clamp(int(std::ceil(reach / 20)) + 7, 25, 67) : 25;
    const int ny = coarse ? 33 : 25, origin = expanded ? nx / 3 : 3;
    const int finish = nx - 1 - origin, middle = ny / 2;
    const auto lateral = expanded ? 40. : coarse ? std::clamp(reach * .3, 24., 300.)
                               : std::clamp(reach * .4, 3., 24.);
    std::vector<wr::GeoPoint> points(nx * ny);
    for (int y = 0; y < ny; ++y) for (int x = 0; x < nx; ++x) {
      double alongNm = reach * (x - origin) / (finish - origin);
      if (expanded && x < origin) alongNm = 40. * (x - origin) / origin;
      if (expanded && x > finish) alongNm = reach + 40. * (x - finish) / origin;
      const auto along = wr::destinationPoint(request.start, bearing, alongNm);
      points[y * nx + x] = wr::destinationPoint(along, bearing + 90,
                                               lateral * (y - middle) / middle);
    }
    const auto first = middle * nx + origin, last = middle * nx + finish;
    points[first] = request.start;
    points[last] = goal;
    std::vector<double> costs(points.size(), INFINITY);
    std::vector<int> parent(points.size(), -1);
    std::priority_queue<std::pair<double, int>,
        std::vector<std::pair<double, int>>, std::greater<>> open;
    std::map<std::pair<int, int>, bool> edges;
    costs[first] = 0;
    open.emplace(reach, first);
    unsigned queries = 0;
    const unsigned queryLimit = coarse ? 8192 : 4096;
    const bool bufferedStart = margin > 0 &&
        geometry->segmentForbidden(request.start, request.start, margin) &&
        !geometry->pointForbidden(request.start);
    while (!open.empty() && queries < queryLimit && !request.cancellation.cancelled()) {
      const auto [rank, id] = open.top(); open.pop();
      if (rank > costs[id] + wr::distanceNm(points[id], goal) + 1e-8) continue;
      if (id == last) {
        for (int n = last; n != -1; n = parent[n]) path_.push_back(points[n]);
        std::reverse(path_.begin(), path_.end());
        remaining_.resize(path_.size());
        for (std::size_t n = path_.size() - 1; n > 0; --n)
          remaining_[n - 1] = remaining_[n] + wr::distanceNm(path_[n - 1], path_[n]);
        tail_ = wr::distanceNm(goal, destination_);
        return;
      }
      const auto x = id % nx, y = id / nx;
      for (int dy = -1; dy <= 1 && queries < queryLimit; ++dy)
        for (int dx = -1; dx <= 1 && queries < queryLimit; ++dx) {
          if ((!dx && !dy) || x + dx < 0 || x + dx >= nx || y + dy < 0 || y + dy >= ny) continue;
          const auto next = (y + dy) * nx + x + dx;
          const auto edge = std::minmax(id, next);
          auto found = edges.find(edge);
          if (found == edges.end()) {
            ++queries;
            const bool localAccess = bufferedStart && (id == first || next == first) &&
                wr::distanceNm(request.start, points[id]) <= 2 &&
                wr::distanceNm(request.start, points[next]) <= 2;
            found = edges.emplace(edge, geometry->segmentForbidden(points[id], points[next], localAccess ? 0 : margin)).first;
          }
          if (found->second) continue;
          const auto cost = costs[id] + wr::distanceNm(points[id], points[next]);
          if (cost + 1e-8 >= costs[next]) continue;
          costs[next] = cost;
          parent[next] = id;
          open.emplace(cost + wr::distanceNm(points[next], goal), next);
        }
    }
    // Nearby harbours can lie on opposite sides of a long peninsula. A
    // corridor scaled only by endpoint distance cannot represent that detour.
    // Widen advisory geography once; sailing remains free and authoritative.
    if (!coarse && !expanded && !request.cancellation.cancelled())
      *this = GeographyGuide(request, geometry, maximumReach, false, true);
  }
  double estimate(wr::GeoPoint point) const {
    const auto direct = wr::distanceNm(point, destination_);
    if (path_.empty() || direct < tail_) return direct;
    double nearest = INFINITY, estimate = direct;
    for (std::size_t i = 1; i < path_.size(); ++i) {
      const auto a = path_[i - 1], b = path_[i];
      const auto length = wr::distanceNm(a, b);
      const auto bearing = wr::initialBearingDegrees(a, b);
      const auto angle = wr::angularDifferenceDegrees(
          wr::initialBearingDegrees(a, point), bearing) * std::acos(-1) / 180;
      const auto along = std::clamp(wr::distanceNm(a, point) * std::cos(angle), 0., length);
      const auto projected = wr::destinationPoint(a, bearing, along);
      const auto offset = wr::distanceNm(point, projected);
      const auto value = offset + remaining_[i - 1] - along + tail_;
      if (offset < nearest - 1e-8 ||
          (std::abs(offset - nearest) <= 1e-8 && value < estimate)) {
        nearest = offset;
        estimate = value;
      }
    }
    return estimate;
  }
};
}  // namespace meridian_routing
