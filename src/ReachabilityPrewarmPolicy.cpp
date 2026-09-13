/***************************************************************************
 * Copyright (C) 2026 OpenCPN contributors
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 2 of the License, or (at your option)
 * any later version.
 ***************************************************************************/

#include "ReachabilityPrewarmPolicy.h"

#include <algorithm>
#include <cmath>

namespace weather_routing {

ReachabilityPrewarmPlan BuildReachabilityPrewarmPlan(
    double direct_distance_nm) {
  ReachabilityPrewarmPlan plan;
  plan.direct_distance_nm = direct_distance_nm;
  if (!std::isfinite(direct_distance_nm) || direct_distance_nm <= 0.0 ||
      direct_distance_nm >= 600.0)
    return plan;

  // The broad coastal envelope is deliberately much wider than the direct
  // route or a single scout. The cap prevents medium-length passages from
  // turning a prefetch into an unbounded ocean raster.
  plan.maximum_cross_track_nm =
      std::clamp(direct_distance_nm * 0.45, 12.0, 75.0);
  const double semi_focal_distance = direct_distance_nm * 0.5;
  const double semi_major_axis = std::hypot(
      semi_focal_distance, plan.maximum_cross_track_nm);
  plan.maximum_path_length_nm = 2.0 * semi_major_axis;
  plan.enabled = true;
  return plan;
}

}  // namespace weather_routing
