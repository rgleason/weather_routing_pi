// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cmath>
#include <limits>
#include <vector>
#include "supercpn/weather_routing/Types.h"

namespace weather_routing::native {
// Capture optional display data after solving, while the calculation's GRIB
// service is still available. Never substitute sustained wind for absent gusts.
template <class Sampler>
void PopulateRouteGusts(
    std::vector<supercpn::weather_routing::RouteLeg>& legs, Sampler sample) {
  for (auto& leg : legs) {
    const double gust = sample(leg.start, leg.startTime);
    leg.windGustKnots = std::isfinite(gust) && gust >= 0.0
                            ? std::optional<double>{gust}
                            : std::nullopt;
  }
}

inline double RouteGustKnots(
    const supercpn::weather_routing::RouteLeg& leg) {
  return leg.windGustKnots.value_or(std::numeric_limits<double>::quiet_NaN());
}
}  // namespace weather_routing::native
