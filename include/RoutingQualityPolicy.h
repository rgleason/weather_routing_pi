/***************************************************************************
 * Routing quality policy. Preview is explicitly opt-in; final calculations
 * retain the full search/refinement and route-family settings.
 ***************************************************************************/

#ifndef WEATHER_ROUTING_QUALITY_POLICY_H
#define WEATHER_ROUTING_QUALITY_POLICY_H

#include <algorithm>
#include <cstdint>

namespace weather_routing {

struct RoutingQualityPolicy {
  std::int64_t time_step_seconds;
  double heading_step_degrees;
  double refined_heading_step_degrees;
  unsigned labels_per_cell;
  unsigned retry_stages;
  bool preserve_route_families;
};

inline RoutingQualityPolicy SelectRoutingQualityPolicy(
    bool preview, double configured_heading_degrees,
    std::int64_t configured_time_step_seconds) {
  const std::int64_t final_time =
      std::max<std::int64_t>(15 * 60, configured_time_step_seconds);
  const double final_heading =
      std::clamp(configured_heading_degrees, 5.0, 20.0);
  const double refined_heading =
      std::clamp(configured_heading_degrees / 2.0, 2.5, 7.5);
  if (!preview)
    return {final_time, final_heading, refined_heading, 10, 7, true};

  return {std::max<std::int64_t>(30 * 60, final_time),
          std::max(15.0, final_heading),
          std::max(5.0, refined_heading),
          6,
          5,
          false};
}

}  // namespace weather_routing

#endif
