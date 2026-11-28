// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "supercpn/weather_routing/Engine.h"
#include "supercpn/weather_routing/Providers.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <numbers>
#include <queue>
#include <vector>

namespace corridor_routing {
namespace wr = supercpn::weather_routing;

// A reverse shoreline distance field. Closure of a mesh edge affects search
// priority only. Real sailing motions and the final route have their own checks.
class ConnectedWaterGuide {
  static constexpr double radians = std::numbers::pi / 180.;
  static constexpr double infinity = std::numeric_limits<double>::infinity();
  static constexpr double earthRadiusNm = 3440.065;
  static constexpr std::uint64_t queryLimit = 460000;

  wr::GeoPoint origin_;
  double bearing_;
  std::vector<double> along_, across_, costs_;
  std::vector<wr::GeoPoint> route_;
  std::uint64_t queries_{};
  bool available_{};

  static void extend(std::vector<double>& axis, double end, double step) {
    const auto start = axis.back();
    const auto count = std::max(1, static_cast<int>(std::ceil((end - start) / step)));
    for (int i = 1; i <= count; ++i)
      axis.push_back(std::lerp(start, end, double(i) / count));
  }

  int index(int x, int y) const { return y * static_cast<int>(along_.size()) + x; }

  static int interval(const std::vector<double>& axis, double value) {
    return static_cast<int>(std::upper_bound(axis.begin(), axis.end(), value) -
                            axis.begin()) - 1;
  }

public:
  ConnectedWaterGuide(const wr::RoutingRequest& request,
                      const wr::LandAndBoundaryProvider& land)
      : origin_(request.start),
        bearing_(wr::initialBearingDegrees(origin_, request.destination)) {
    const auto length = wr::distanceNm(origin_, request.destination);
    if (!std::isfinite(length) || length <= 60.) return;
    const auto offshoreStep = std::clamp(length / 320., 5., 30.);
    const auto halfWidth = std::clamp(length * .35, 135., 430.);
    // Fine endpoint cells link the passage-wide field to small harbour exits.
    // The mesh extends behind each endpoint so the search can first sail away.
    along_.push_back(-halfWidth);
    extend(along_, -30., offshoreStep);
    extend(along_, 0., 1.5);
    const auto startX = static_cast<int>(along_.size()) - 1;
    extend(along_, 30., 1.5);
    extend(along_, length - 30., offshoreStep);
    extend(along_, length, 1.5);
    const auto endX = static_cast<int>(along_.size()) - 1;
    extend(along_, length + 30., 1.5);
    extend(along_, length + halfWidth, offshoreStep);
    across_.push_back(-halfWidth);
    extend(across_, -30., offshoreStep);
    extend(across_, 0., 1.5);
    const auto middle = static_cast<int>(across_.size()) - 1;
    extend(across_, 30., 1.5);
    extend(across_, halfWidth, offshoreStep);
    const auto width = static_cast<int>(along_.size());
    const auto height = static_cast<int>(across_.size());
    std::vector<wr::GeoPoint> points(width * height);
    costs_.assign(points.size(), infinity);
    std::vector<int> next(points.size(), -1);
    std::vector<std::int8_t> coastal(points.size(), -1);
    for (int x = 0; x < width; ++x) {
      if (request.cancellation.cancelled()) return;
      const auto along = wr::destinationPoint(origin_, bearing_, along_[x]);
      const auto bearing = x < endX
          ? wr::initialBearingDegrees(along, request.destination)
          : wr::initialBearingDegrees(along, origin_) + 180.;
      for (int y = 0; y < height; ++y)
        points[index(x, y)] = wr::destinationPoint(along, bearing + 90., across_[y]);
    }
    const auto start = index(startX, middle), finish = index(endX, middle);
    points[start] = request.start;
    points[finish] = request.destination;
    const auto margin = request.constraints.landSafetyMarginNm;
    const auto bufferedStart = margin > 0. && land.segmentForbidden(request.start, request.start, margin);
    const auto bufferedFinish = margin > 0. && land.segmentForbidden(request.destination, request.destination, margin);
    const auto nearShore = [&](int cell) {
      if (coastal[cell] < 0 && queries_ < queryLimit) {
        ++queries_;
        coastal[cell] = land.segmentForbidden(points[cell], points[cell], margin + 8.);
      }
      return coastal[cell] == 1;
    };
    using Entry = std::pair<double, int>;
    std::priority_queue<Entry, std::vector<Entry>, std::greater<>> queue;
    costs_[finish] = 0.;
    queue.emplace(0., finish);
    while (!queue.empty() && queries_ < queryLimit && !request.cancellation.cancelled()) {
      const auto [cost, from] = queue.top();
      queue.pop();
      if (cost != costs_[from]) continue;
      if (from == start) available_ = true;
      const int x = from % width, y = from / width;
      const bool fromCoast = nearShore(from);
      for (int row = std::max(0, y - 1); row <= std::min(height - 1, y + 1); ++row) {
        for (int col = std::max(0, x - 1); col <= std::min(width - 1, x + 1); ++col) {
          const auto to = index(col, row);
          const auto distance = std::hypot(along_[col] - along_[x], across_[row] - across_[y]);
          if (cost + distance >= costs_[to] || queries_ >= queryLimit) continue;
          const auto candidate = cost + distance * ((nearShore(to) || fromCoast) ? 4. : 1.);
          if (candidate >= costs_[to] || queries_ >= queryLimit) continue;
          const bool endpoint = (bufferedStart && (from == start || to == start)) ||
                                (bufferedFinish && from == finish);
          ++queries_;
          if (land.segmentForbidden(points[from], points[to], endpoint ? 0. : margin))
            continue;
          costs_[to] = candidate;
          next[to] = from;
          queue.emplace(candidate, to);
        }
      }
    }
    if (!available_ || request.cancellation.cancelled()) return;
    // Preserve turns around land while spacing the offshore checkpoints out.
    // Geometry is advisory even when it is used to advance a search stage.
    for (auto from = start; from != finish && next[from] >= 0;) {
      auto to = next[from];
      for (auto candidate = next[to]; candidate >= 0; candidate = next[candidate]) {
        if (request.cancellation.cancelled()) return;
        const auto leg = wr::distanceNm(points[from], points[candidate]);
        const auto reach = from == start || candidate == finish ? 85. : 350.;
        if (leg > reach || queries_ >= queryLimit) break;
        ++queries_;
        const auto extraClearance = nearShore(from) || nearShore(candidate) ? 0. : 6.;
        if (!land.segmentForbidden(points[from], points[candidate], margin + extraClearance))
          to = candidate;
      }
      route_.push_back(points[to]);
      from = to;
    }
  }

  bool available() const { return available_; }
  std::uint64_t queries() const { return queries_; }
  const std::vector<wr::GeoPoint>& route() const { return route_; }

  double estimate(wr::GeoPoint point, double direct, double fallback) const {
    const auto angle = (wr::initialBearingDegrees(origin_, point) - bearing_) * radians;
    const auto distance = wr::distanceNm(origin_, point) / earthRadiusNm;
    const auto x = std::atan2(std::sin(distance) * std::cos(angle), std::cos(distance)) *
                   earthRadiusNm;
    const auto y = std::asin(std::clamp(std::sin(distance) * std::sin(angle), -1., 1.)) *
                   earthRadiusNm;
    const auto ix = interval(along_, x), iy = interval(across_, y);
    if (ix < 0 || iy < 0 || ix + 1 >= static_cast<int>(along_.size()) ||
        iy + 1 >= static_cast<int>(across_.size()))
      return fallback;
    double remaining = infinity;
    for (int row = iy; row <= iy + 1; ++row)
      for (int col = ix; col <= ix + 1; ++col)
        remaining = std::min(remaining, costs_[index(col, row)] +
            std::hypot(x - along_[col], y - across_[row]));
    if (std::isfinite(remaining)) return std::max(direct, remaining);
    // Give room for a coarse cell to miss water without favouring its dead end.
    return fallback + 2 * (across_.back() - across_.front());
  }
};

}  // namespace corridor_routing
