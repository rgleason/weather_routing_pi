// SPDX-License-Identifier: GPL-3.0-or-later
#include "original_routing/GshhgProvider.h"
#include "shoreline/Dataset.h"
namespace original_routing {
struct GshhgProvider::Impl {
  shoreline::ShorelineDataset data;
  std::string name;
  Impl(const std::filesystem::path& file, std::size_t bytes)
      : data(file, bytes), name("original-gshhg:" + file.string()) {}
};
GshhgProvider::GshhgProvider(const std::filesystem::path& f, std::size_t b)
    : impl_(std::make_unique<Impl>(f, b)) {}
GshhgProvider::~GshhgProvider() = default;
bool GshhgProvider::pointForbidden(wr::GeoPoint p) const {
  return impl_->data.CrossesLand(p.latitude, p.longitude, p.latitude,
                                 p.longitude);
}
bool GshhgProvider::segmentForbidden(wr::GeoPoint a, wr::GeoPoint b,
                                     double margin) const {
  return impl_->data.WithinLandMargin(a.latitude, a.longitude, b.latitude,
                                      b.longitude, margin);
}
double GshhgProvider::distanceToForbiddenNm(wr::GeoPoint p) const {
  if (pointForbidden(p)) return 0;
  // A conservative lower bound suffices for validator sampling density.
  double lower = 0;
  for (double d : {.1, .25, .5, 1., 2., 5., 10., 20.}) {
    if (segmentForbidden(p, p, d)) return lower;
    lower = d;
  }
  return 20;
}
std::string GshhgProvider::identity() const { return impl_->name; }
}  // namespace original_routing
