// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "supercpn/weather_routing/Providers.h"
#include <algorithm>
#include <cmath>
#include <map>

namespace meridian_routing {
namespace wr = supercpn::weather_routing;
// A bounded, per-attempt hint cache. It never substitutes for the land test.
// Subtract movement from the sampled location so a cached clearance cannot
// increase as a candidate approaches the coast inside the same cell.
class StepPolicy {
  struct Sample { wr::GeoPoint point; double clearance; };
  std::map<std::pair<int, int>, Sample> samples_;
  const wr::LandAndBoundaryProvider* geography_;
  wr::Duration coastal_, offshore_;
  double margin_;
public:
  StepPolicy(const wr::LandAndBoundaryProvider* geography, wr::Duration coastal,
             wr::Duration offshore, double margin)
      : geography_(geography), coastal_(coastal), offshore_(offshore),
        margin_(std::max(0.0, margin)) {}
  static wr::Duration Select(double clearance, double margin,
                             wr::Duration coastal, wr::Duration offshore) {
    if (std::isnan(clearance) || clearance <= margin + 5) return coastal;
    if (clearance <= margin + 10) return std::min(offshore, coastal * 2);
    if (clearance <= margin + 20) return std::min(offshore, wr::Duration{3600});
    return offshore;
  }
  wr::Duration operator()(wr::GeoPoint point) {
    if (!geography_ || !std::isfinite(point.latitude) ||
        !std::isfinite(point.longitude) || std::abs(point.latitude) > 85)
      return coastal_;
    const auto key = std::pair{int(std::floor(point.latitude * 20)),
        int(std::floor(wr::normalizeLongitude(point.longitude) * 20))};
    auto found = samples_.find(key);
    if (found == samples_.end()) {
      if (samples_.size() >= 8192) samples_.clear();
      const auto clearance = geography_->distanceToForbiddenNm(point);
      found = samples_.emplace(key, Sample{point, clearance}).first;
    }
    const auto& sample = found->second;
    return Select(sample.clearance - wr::distanceNm(sample.point, point),
                  margin_, coastal_, offshore_);
  }
};
}  // namespace meridian_routing
