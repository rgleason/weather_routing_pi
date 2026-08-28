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

}  // namespace weather_routing

#endif  // WEATHER_ROUTING_CHART_SAFETY_POLICY_H
