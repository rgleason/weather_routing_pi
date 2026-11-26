// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "supercpn/weather_routing/Providers.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <functional>
#include <limits>
#include <numbers>
#include <queue>
#include <utility>
#include <vector>

namespace corridor_routing {
namespace wr = supercpn::weather_routing;

// Distance over water from the destination, used only to order sailing moves.
class ArrivalGuide {
  static constexpr int halfWidth = 40;
  static constexpr int width = 2 * halfWidth + 1;
  static constexpr double step = 1.5;
  static constexpr double edge = halfWidth * step;
  static constexpr double radians = std::numbers::pi / 180.;
  static constexpr double infinity = std::numeric_limits<double>::infinity();
  static constexpr std::uint64_t queryLimit = 48000;
  wr::GeoPoint destination_;
  std::vector<double> costs_;
  std::uint64_t queries_{};
  bool available_{};

  static int index(int x, int y) { return y * width + x; }

public:
  ArrivalGuide(const wr::RoutingRequest& request,
               const wr::LandAndBoundaryProvider& land)
      : destination_(request.destination), costs_(width * width, infinity) {
    std::vector<wr::GeoPoint> points(width * width);
    for (int y = 0; y < width; ++y) {
      if (request.cancellation.cancelled()) return;
      for (int x = 0; x < width; ++x) {
        const auto east = (x - halfWidth) * step;
        const auto north = (y - halfWidth) * step;
        points[index(x, y)] = wr::destinationPoint(
            destination_, std::atan2(east, north) / radians, std::hypot(east, north));
      }
    }
    const auto target = index(halfWidth, halfWidth);
    points[target] = destination_;
    const auto margin = request.constraints.landSafetyMarginNm;
    const bool buffered = margin > 0 && land.segmentForbidden(destination_, destination_, margin);
    using Entry = std::pair<double, int>;
    std::priority_queue<Entry, std::vector<Entry>, std::greater<>> queue;
    costs_[target] = 0.;
    queue.emplace(0., target);
    while (!queue.empty() && queries_ < queryLimit && !request.cancellation.cancelled()) {
      const auto [distance, from] = queue.top();
      queue.pop();
      if (distance != costs_[from]) continue;
      const int x = from % width, y = from / width;
      for (int row = std::max(0, y - 1); row <= std::min(width - 1, y + 1); ++row) {
        for (int col = std::max(0, x - 1); col <= std::min(width - 1, x + 1); ++col) {
          const auto to = index(col, row);
          const auto candidate = distance + std::hypot(col - x, row - y) * step;
          if (candidate >= costs_[to] || queries_ >= queryLimit) continue;
          // The marked destination can be inside the margin. Apply its local
          // access allowance only to the first advisory edge out of the mark.
          const auto edgeMargin = buffered && from == target ? 0. : margin;
          ++queries_;
          if (land.segmentForbidden(points[from], points[to], edgeMargin)) continue;
          costs_[to] = candidate;
          queue.emplace(candidate, to);
          available_ = true;
        }
      }
    }
  }

  bool available() const { return available_; }
  std::uint64_t queries() const { return queries_; }

  double estimate(wr::GeoPoint point, double direct, double offshoreEstimate) const {
    if (direct >= edge * std::numbers::sqrt2) return offshoreEstimate;
    const auto bearing = wr::initialBearingDegrees(destination_, point) * radians;
    const auto east = std::sin(bearing) * direct;
    const auto north = std::cos(bearing) * direct;
    const auto edgeDistance = edge - std::max(std::abs(east), std::abs(north));
    if (edgeDistance <= 0.) return offshoreEstimate;
    const auto x = halfWidth + east / step;
    const auto y = halfWidth + north / step;
    const auto ix = static_cast<int>(std::floor(x)), iy = static_cast<int>(std::floor(y));
    if (ix < 0 || iy < 0 || ix >= width - 1 || iy >= width - 1)
      return offshoreEstimate;
    double distance = infinity;
    for (int row = iy; row <= iy + 1; ++row)
      for (int col = ix; col <= ix + 1; ++col)
        distance = std::min(distance, costs_[index(col, row)] +
                                          std::hypot(x - col, y - row) * step);
    if (!std::isfinite(distance)) distance = direct + 2 * edge;
    // Blend into the passage-wide estimate at the local field's perimeter.
    return std::lerp(offshoreEstimate, std::max(direct, distance),
                     std::min(1., edgeDistance / 12.));
  }
};
}  // namespace corridor_routing
