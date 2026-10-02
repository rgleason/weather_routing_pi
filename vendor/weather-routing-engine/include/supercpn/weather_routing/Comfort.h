// SPDX-License-Identifier: Apache-2.0
#pragma once
#include <cmath>
#include <limits>
#include "supercpn/weather_routing/Types.h"

namespace supercpn::weather_routing {
// Preserve the host's empirical, signed-angle model. Search and comparison
// must use the same severity, rather than a different wave/risk surrogate.
inline double comfortSeverity(double windKnots, double relativeWindDegrees,
                              double waveMetres) {
  if (!std::isfinite(windKnots) || windKnots < 0 ||
      !std::isfinite(relativeWindDegrees))
    return std::numeric_limits<double>::quiet_NaN();
  constexpr double pi = 3.14159265358979323846;
  const double angle = std::remainder(relativeWindDegrees, 360.0);
  const double wind = std::pow(windKnots / 27.0, 3);
  const double upwind = 20.0 / (30.0 * std::sqrt(2.0 * pi)) *
      std::exp(-std::pow(angle - 35.0, 2) / (2.0 * 30.0 * 30.0));
  const double waves = std::isfinite(waveMetres) && waveMetres > 0
                           ? std::pow(waveMetres / 5.0, 2) : 0.0;
  return wind * (1.0 + upwind) * (1.0 + waves);
}
inline double legDiscomfortSeconds(const RouteLeg& leg, bool windOnly) {
  if (!windOnly && (!leg.waves.available ||
      !std::isfinite(leg.waves.significantHeightMetres) ||
      leg.waves.significantHeightMetres < 0))
    return std::numeric_limits<double>::quiet_NaN();
  const double from = std::atan2(leg.wind.eastKnots, leg.wind.northKnots) *
      180.0 / 3.14159265358979323846 + 180.0;
  return comfortSeverity(std::hypot(leg.wind.eastKnots, leg.wind.northKnots),
      leg.courseThroughWaterDegrees - from,
      windOnly ? 0.0 : leg.waves.significantHeightMetres) *
      (leg.endTime - leg.startTime).count();
}
inline double routeDiscomfortSeconds(const std::vector<RouteLeg>& legs,
                                     bool windOnly) {
  double sum = 0;
  for (const auto& leg : legs) sum += legDiscomfortSeconds(leg, windOnly);
  return sum;
}
}  // namespace supercpn::weather_routing
