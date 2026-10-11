// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <algorithm>
#include <cmath>

#include "supercpn/weather_routing/Engine.h"

namespace supercpn::weather_routing {

// Endpoint access is local, never permission to follow the coast. Keep the
// same bound in search, chronological replay and delivered-geometry checks.
// The 2 NM ceiling is independent of margin. The earlier 1.5 multiplier
// could end local access before a coastal waypoint reached clear water.
inline double coastalEndpointReachNm(double marginNm) {
  if (!std::isfinite(marginNm) || marginNm <= 0.0) return 0.0;
  return std::min(2.0, std::max(0.5, 2.0 * marginNm));
}

// Check the full plotted chord, including the part outside local departure
// access. A leg flag cannot waive the stand-off on an arbitrarily long chord.
// The callback still enforces actual land, exclusions and depth at margin 0.
template <typename Forbidden>
bool coastalDepartureChordForbidden(GeoPoint departure, GeoPoint start,
                                    GeoPoint end, double marginNm,
                                    Forbidden&& forbidden) {
  const double reach = coastalEndpointReachNm(marginNm);
  if (reach == 0.0) return forbidden(start, end, marginNm);
  const auto local = [departure, reach](GeoPoint point) {
    return distanceNm(departure, point) <= reach + 1e-6;
  };
  if (!local(start)) return forbidden(start, end, marginNm);
  if (local(end)) return forbidden(start, end, 0.0);

  const double length = distanceNm(start, end);
  const double bearing = initialBearingDegrees(start, end);
  // Only the local prefix needs partitioning. The remaining offshore chord
  // is checked in one call at the full configured margin.
  GeoPoint prior = start;
  for (double offset = 0.05; offset < length; offset += 0.05) {
    const GeoPoint next = destinationPoint(start, bearing, offset);
    if (!local(next))
      return forbidden(prior, end, marginNm);
    if (forbidden(prior, next, 0.0)) return true;
    prior = next;
  }
  return forbidden(prior, end, marginNm);
}

}  // namespace supercpn::weather_routing
