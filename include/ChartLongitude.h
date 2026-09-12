#pragma once

#include <array>
#include <cmath>

namespace weather_routing {

inline double CanonicalChartLongitude(double longitude) {
  double value = std::fmod(longitude + 180.0, 360.0);
  if (value < 0.0) value += 360.0;
  return value - 180.0;
}

inline void UnwrapChartSegment(double& start, double& end) {
  start = CanonicalChartLongitude(start);
  end = CanonicalChartLongitude(end);
  if (end - start > 180.0) end -= 360.0;
  if (end - start < -180.0) end += 360.0;
}

// Raw chart tiles are 0.05 degrees wide. Keep cache identities identical on
// either side of the date line, including neighbouring tiles used for margins.
inline long CanonicalChartLongitudeTile(long tile) {
  constexpr long half = 3600;
  long value = (tile + half) % (2 * half);
  if (value < 0) value += 2 * half;
  return value - half;
}

struct ChartSegment {
  double lat1, lon1, lat2, lon2;
};

struct ChartSegmentParts {
  std::array<ChartSegment, 2> segments{};
  unsigned count = 0;
};

// Older host implementations rasterise longitude differences directly. Split
// at the date line so each host call also sees a short continuous interval.
inline ChartSegmentParts SplitChartSegment(double lat1, double lon1,
                                           double lat2, double lon2) {
  ChartSegmentParts result;
  if (!std::isfinite(lat1) || !std::isfinite(lon1) ||
      !std::isfinite(lat2) || !std::isfinite(lon2)) return result;
  UnwrapChartSegment(lon1, lon2);
  result.segments[0] = {lat1, lon1, lat2, lon2};
  result.count = 1;
  if (lon2 > 180.0 || lon2 < -180.0) {
    const double seam = lon2 > 180.0 ? 180.0 : -180.0;
    const double latitude = lat1 + (lat2 - lat1) *
        (seam - lon1) / (lon2 - lon1);
    result.segments[0] = {lat1, lon1, latitude, seam};
    result.segments[1] = {latitude, -seam, lat2,
                          CanonicalChartLongitude(lon2)};
    result.count = 2;
  }
  return result;
}

}  // namespace weather_routing
