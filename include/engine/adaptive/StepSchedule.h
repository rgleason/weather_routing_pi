// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "RouteFeatures.h"
#include <unordered_set>

namespace weather_routing::adaptive {
class StepSchedule {
  static constexpr int columns = 1440;
  std::unordered_set<std::int64_t> hourly_;
  bool allHourly_{};
  double bufferNm_{};
  static int longitudeCell(int x) {
    x %= columns;
    if (x < 0) x += columns;
    return x;
  }
  static std::int64_t key(int y, int x) {
    return std::int64_t(y + 360) * 4096 + longitudeCell(x);
  }
  void mark(wr::GeoPoint point, double radiusNm) {
    if (!std::isfinite(point.latitude) || !std::isfinite(point.longitude) ||
        std::abs(point.latitude) > 90) {
      hourly_.clear();
      allHourly_ = true;
      return;
    }
    const int y = static_cast<int>(std::floor(point.latitude * 4));
    const int x = static_cast<int>(
        std::floor(wr::normalizeLongitude(point.longitude) * 4));
    const int dy = std::max(1, int(std::ceil(radiusNm / 15.)));
    const int dx = std::max(
        1, int(std::ceil(
               radiusNm /
               (15. * std::max(.15, std::cos(point.latitude * std::numbers::pi /
                                             180.))))));
    for (int a = -dy; a <= dy; ++a)
      for (int b = -dx; b <= dx; ++b) {
        hourly_.insert(key(y + a, x + b));
        if (hourly_.size() > 80000) {
          hourly_.clear();
          allHourly_ = true;
          return;
        }
      }
  }

public:
  StepSchedule(const Features& f, const wr::RoutingRequest& request) {
    allHourly_ = false;
    double boat = 0;
    for (const auto& p : request.vessel.profiles)
      for (const auto& row : p.rows)
        for (const auto& v : row.points)
          boat = std::max(boat, v.boatSpeedKnots);
    boat = std::max(boat, request.vessel.propulsion.configuredMotorSpeedKnots);
    // Conservative dilation prevents a long proposal from jumping over a
    // sampled hourly region. It is an advisory numeric envelope, not a claim
    // to bound all possible future currents or to validate the sailing path.
    bufferNm_ = std::clamp((boat + f.currentKnots + 2) * 6., 15., 180.);
    for (auto point : f.hourlyCenters) {
      mark(point, bufferNm_);
      if (allHourly_) break;
    }
    for (auto p : f.localShearCenters) {
      mark(p, bufferNm_);
      if (allHourly_) break;
    }
    for (auto p : f.geometryHotspots) {
      mark(p, bufferNm_);
      if (allHourly_) break;
    }
  }
  bool empty() const { return !allHourly_ && hourly_.empty(); }
  wr::Duration cap(wr::GeoPoint point) const {
    if (!std::isfinite(point.latitude) || !std::isfinite(point.longitude) ||
        std::abs(point.latitude) > 90)
      return wr::Duration{3600};
    const int y = static_cast<int>(std::floor(point.latitude * 4)),
              x = static_cast<int>(
                  std::floor(wr::normalizeLongitude(point.longitude) * 4));
    return wr::Duration{allHourly_ || hourly_.contains(key(y, x)) ? 3600
                                                                  : 21600};
  }
};
}  // namespace weather_routing::adaptive
