// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "ShorelineDataset.h"
#include "supercpn/weather_routing/Providers.h"
#include <memory>

namespace meridian_routing {
namespace wr = supercpn::weather_routing;
class ShorelineHints final : public wr::LandAndBoundaryProvider {
  std::shared_ptr<weather_routing::ShorelineDataset> data_;
public:
  explicit ShorelineHints(std::shared_ptr<weather_routing::ShorelineDataset> data)
      : data_(std::move(data)) {}
  bool pointForbidden(wr::GeoPoint p) const override {
    return data_->CrossesLand(p.latitude, p.longitude, p.latitude, p.longitude);
  }
  bool segmentForbidden(wr::GeoPoint a, wr::GeoPoint b, double margin) const override {
    return data_->WithinLandMargin(a.latitude, a.longitude, b.latitude, b.longitude, margin);
  }
  double distanceToForbiddenNm(wr::GeoPoint p) const override {
    if (pointForbidden(p)) return 0;
    double lower = 0;
    for (double radius : {2., 5., 10., 20., 40., 80.}) {
      if (segmentForbidden(p, p, radius)) break;
      lower = radius;
    }
    return lower;
  }
  std::string identity() const override { return "Meridian advisory GSHHG clearance"; }
};
}  // namespace meridian_routing
