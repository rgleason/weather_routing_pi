#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <set>
#include <tuple>
#include <vector>

struct RouteMapFrontierSegment {
  double lat1;
  double lon1;
  double lat2;
  double lon2;
};

namespace weather_routing_engine {

// Safety prewarm consumes an undirected spatial footprint.  Modern isochrone
// traces contain complete parent lineages, so the same edge can appear in
// many traces and layers.  Canonicalising here prevents those repeated
// prefixes from inflating the chart work while preserving every unique edge.
inline std::vector<RouteMapFrontierSegment> DeduplicateRoutingFootprint(
    const std::vector<RouteMapFrontierSegment>& input) {
  constexpr double kCoordinateScale = 10000000.0;
  using Point = std::pair<std::int64_t, std::int64_t>;
  using Edge = std::pair<Point, Point>;

  std::set<Edge> seen;
  std::vector<RouteMapFrontierSegment> output;
  output.reserve(input.size());
  for (const RouteMapFrontierSegment& segment : input) {
    if (!std::isfinite(segment.lat1) || !std::isfinite(segment.lon1) ||
        !std::isfinite(segment.lat2) || !std::isfinite(segment.lon2))
      continue;
    Point first{std::llround(segment.lat1 * kCoordinateScale),
                std::llround(segment.lon1 * kCoordinateScale)};
    Point second{std::llround(segment.lat2 * kCoordinateScale),
                 std::llround(segment.lon2 * kCoordinateScale)};
    if (first == second) continue;
    if (second < first) std::swap(first, second);
    if (seen.insert({first, second}).second) output.push_back(segment);
  }
  return output;
}

}  // namespace weather_routing_engine
