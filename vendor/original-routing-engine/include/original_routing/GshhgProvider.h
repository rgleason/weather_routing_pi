// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "original_routing/Engine.h"
#include <filesystem>
#include <memory>
namespace original_routing {
// Direct, bounded, thread-safe GSHHG reader. The file selects the resolution;
// no OpenCPN host callback or implicit downgrade to coarse data is used.
class GshhgProvider final : public wr::LandAndBoundaryProvider {
  struct Impl;
  std::unique_ptr<Impl> impl_;

public:
  explicit GshhgProvider(const std::filesystem::path&,
                         std::size_t cacheBytes = 64 * 1024 * 1024);
  ~GshhgProvider();
  bool pointForbidden(wr::GeoPoint) const override;
  bool segmentForbidden(wr::GeoPoint, wr::GeoPoint, double) const override;
  double distanceToForbiddenNm(wr::GeoPoint) const override;
  std::string identity() const override;
};
}  // namespace original_routing
