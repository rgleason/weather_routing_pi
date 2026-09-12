#pragma once

#include <algorithm>
#include <cmath>
#include "ChartLongitude.h"
#include "supercpn/weather_routing/Types.h"

namespace weather_routing::native {

// Intersect vector-component grids before advertising wind coverage. GRIBs
// can describe a date-line-spanning grid with longitudes outside -180..180.
inline supercpn::weather_routing::ParameterCoverage WindGridCoverage(
    double west, double south, double east, double north, double step,
    double otherWest, double otherSouth, double otherEast, double otherNorth,
    double otherStep) {
  namespace wr = supercpn::weather_routing;
  wr::ParameterCoverage result;
  result.datasetIdentity = "OpenCPN GRIB wind at departure";
  if (!std::isfinite(west) || !std::isfinite(east) ||
      !std::isfinite(south) || !std::isfinite(north) ||
      !std::isfinite(otherWest) || !std::isfinite(otherEast) ||
      !std::isfinite(otherSouth) || !std::isfinite(otherNorth) ||
      !std::isfinite(step) || !std::isfinite(otherStep) ||
      west > east || otherWest > otherEast || south > north ||
      otherSouth > otherNorth) return result;
  const bool global = east - west + std::abs(step) >= 360.0 - 1e-8;
  const bool otherGlobal = otherEast - otherWest + std::abs(otherStep)
                           >= 360.0 - 1e-8;
  const double lower = std::max(south, otherSouth);
  const double upper = std::min(north, otherNorth);
  if (lower > upper) return result;
  if (global && otherGlobal) {
    result.available = true;
    result.area = {-180, lower, 180, upper};
    return result;
  }
  if (global) { west = otherWest; east = otherEast; }
  else if (!otherGlobal) {
    const double shift = 360.0 * std::round(
        ((west + east) - (otherWest + otherEast)) / 720.0);
    west = std::max(west, otherWest + shift);
    east = std::min(east, otherEast + shift);
    if (west > east) return result;
  }
  result.available = true;
  result.area = {CanonicalChartLongitude(west), lower,
                  CanonicalChartLongitude(east), upper};
  return result;
}

}  // namespace weather_routing::native
