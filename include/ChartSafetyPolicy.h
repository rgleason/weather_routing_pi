/***************************************************************************
 * Copyright (C) 2026 OpenCPN development team
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 3, or (at your option) any later version.
 ***************************************************************************/

#ifndef WEATHER_ROUTING_CHART_SAFETY_POLICY_H
#define WEATHER_ROUTING_CHART_SAFETY_POLICY_H

#include <cmath>

#include "ocpn_plugin.h"
#include "OptionalChartSafetyApi.h"

namespace weather_routing {

/**
 * Apply the route's minimum-depth policy to one host chart-safety request.
 *
 * A value at or below zero disables depth checking.  Invalid values fail back
 * to the disabled representation instead of placing NaN in a plugin ABI
 * structure.  Keeping this normalization in one small function prevents the
 * prewarm, propagation and final-validation paths from silently disagreeing.
 */
inline void ApplyMinimumDepthPolicy(PlugInSegmentSafetyOptions& options,
                                    double minimum_depth_m) {
  const bool enabled =
      std::isfinite(minimum_depth_m) && minimum_depth_m > 0.0;
  options.check_depth = enabled ? 1 : 0;
  options.minimum_depth_m = enabled ? minimum_depth_m : 0.0;
}

/** Build the fail-closed request used before a route is shown or exported. */
inline PlugInSegmentSafetyOptions MakeFinalRouteSegmentSafetyOptions(
    double safety_margin_nm, double minimum_depth_m) {
  PlugInSegmentSafetyOptions options = {};
  options.struct_size = sizeof(options);
  options.safety_margin_nm = safety_margin_nm;
  options.check_land = 1;
  ApplyMinimumDepthPolicy(options, minimum_depth_m);
  options.allow_gshhs_fallback = 0;
  options.force_authoritative_fine_validation = 1;
  return options;
}

/**
 * Decide whether a deliverable route search must use authoritative chart
 * hazards from its first production state.
 *
 * GSHHS is suitable for a bounded geometry scout and for the legacy fallback
 * when the optional host capability is absent.  It is not evidence that a
 * production edge is navigable: its generalized shoreline can omit islands,
 * headlands and narrow coastal detail.  Consequently an enabled, enforced
 * chart policy always starts the real solver on the authoritative raster.
 */
inline bool ShouldUseAuthoritativeChartSearch(bool detect_land,
                                              bool use_chart_safety,
                                              bool enforce_chart_safety,
                                              bool scout_preview) {
  return detect_land && use_chart_safety && enforce_chart_safety &&
         !scout_preview;
}

struct ScoutArrivalWindowHours {
  bool valid{false};
  double earliest{0.0};
  double latest{0.0};
};

/**
 * Project a deliberately broad arrival window from a partial scout frontier.
 * The result is an advisory reverse-search seed, never a feasibility bound.
 */
inline ScoutArrivalWindowHours EstimatePartialScoutArrivalWindowHours(
    double elapsed_hours, double progress_nm, double leg_nm) {
  ScoutArrivalWindowHours result;
  if (!std::isfinite(elapsed_hours) || !std::isfinite(progress_nm) ||
      !std::isfinite(leg_nm) || elapsed_hours <= 0.0 || progress_nm <= 0.0 ||
      leg_nm <= 0.0)
    return result;

  const double observed_sog =
      std::fmax(0.5, std::fmin(15.0, progress_nm / elapsed_hours));
  const double projected_hours =
      std::fmax(elapsed_hours, std::fmin(30.0 * 24.0, leg_nm / observed_sog));
  result.earliest = std::fmax(elapsed_hours, projected_hours * 0.70);
  result.latest =
      std::fmax(result.earliest + 1.0, projected_hours * 1.75 + 2.0);
  result.valid = std::isfinite(result.earliest) &&
                 std::isfinite(result.latest) &&
                 result.latest > result.earliest;
  return result;
}

/**
 * Broad chart-tile prewarming belongs to an authoritative production search.
 * Scouts remain cheap and chart-free, but their union is prepared before any
 * deliverable route worker starts.
 */
inline bool ShouldPrewarmAuthoritativeChartSearch(
    bool detect_land, bool use_chart_safety, bool enforce_chart_safety,
    bool use_chart_safety_for_propagation, int missing_tile_retry_count) {
  return detect_land && use_chart_safety && enforce_chart_safety &&
         use_chart_safety_for_propagation && missing_tile_retry_count == 0;
}

}  // namespace weather_routing

#endif  // WEATHER_ROUTING_CHART_SAFETY_POLICY_H
