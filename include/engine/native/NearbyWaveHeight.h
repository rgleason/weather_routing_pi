#pragma once

#include <algorithm>
#include <cmath>
#include <optional>
#include "supercpn/weather_routing/Engine.h"

namespace weather_routing::native {
// Only bridge a local missing-cell gap, inside this frame's grid. Never extend
// its time or geographic coverage. The maximum of eligible nearby cells is an
// estimate, not a guaranteed upper bound on the sea at the requested position.
template <class Grid, class ConnectedWater>
std::optional<double> NearbyWaveHeight(const Grid& grid,
    supercpn::weather_routing::GeoPoint position, ConnectedWater connected) {
  namespace wr = supercpn::weather_routing;
  if (grid.getNi() < 2 || grid.getNj() < 2 ||
      !std::isfinite(grid.getDi()) || !std::isfinite(grid.getDj()) ||
      grid.getDi() == 0 || grid.getDj() == 0) return {};
  const double y = (position.latitude - grid.getY(0)) / grid.getDj();
  double x = (position.longitude - grid.getX(0)) / grid.getDi();
  const bool global = std::abs(std::abs(grid.getDi()) * grid.getNi() - 360.) < 1e-6;
  for (int turn : {0, -1, 1}) {
    const double candidate = (position.longitude + turn * 360. - grid.getX(0)) / grid.getDi();
    if (candidate >= 0 && candidate <= grid.getNi() - (global ? 0 : 1)) {
      x = candidate;
      break;
    }
  }
  if (!std::isfinite(x) || !std::isfinite(y) || x < 0 || y < 0 ||
      x > grid.getNi() - (global ? 0 : 1) || y > grid.getNj() - 1) return {};
  const int centreX = static_cast<int>(std::round(x));
  const int centreY = static_cast<int>(std::round(y));
  std::optional<double> maximum;
  // At most nine probes, with an independent 15 NM ceiling. A missing large
  // patch therefore stays unknown even if distant ocean data exists.
  for (int j = centreY - 1; j <= centreY + 1; ++j) {
    if (j < 0 || j >= grid.getNj()) continue;
    for (int i = centreX - 1; i <= centreX + 1; ++i) {
      int column = i;
      if (global) column = (column % grid.getNi() + grid.getNi()) % grid.getNi();
      if (column < 0 || column >= grid.getNi() || !grid.isDefined(column, j)) continue;
      const double height = grid.getValue(column, j);
      if (!std::isfinite(height) || height < 0) continue;
      const wr::GeoPoint cell{grid.getY(j), wr::normalizeLongitude(grid.getX(column))};
      if (wr::distanceNm(position, cell) > 15.0 || !connected(position, cell)) continue;
      maximum = maximum ? std::max(*maximum, height) : height;
    }
  }
  return maximum;
}
} // namespace weather_routing::native
