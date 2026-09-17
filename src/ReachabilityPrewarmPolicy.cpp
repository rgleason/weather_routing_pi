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
    double direct_distance_nm, bool request_legacy_filled_envelope) {
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
  // Prepare enough of the likely front to avoid a GUI hand-off for every
  // fine tile, while keeping short coastal passages local and medium routes
  // far narrower than the compatibility ellipse.
  plan.initial_scout_corridor_nm =
      std::clamp(direct_distance_nm * 0.05, 2.0, 12.0);
  const double semi_focal_distance = direct_distance_nm * 0.5;
  const double semi_major_axis = std::hypot(
      semi_focal_distance, plan.maximum_cross_track_nm);
  plan.maximum_path_length_nm = 2.0 * semi_major_axis;
  plan.enabled = true;
  // The live solver already pauses on a missing authoritative tile, lets the
  // GUI thread build it, and resumes the same retained frontier. Preparing
  // the entire ellipse first therefore adds latency and memory without
  // improving solver eligibility. Keep it only as an explicit diagnostic
  // comparison path.
  plan.prewarm_filled_envelope = request_legacy_filled_envelope;
  return plan;
}

}  // namespace weather_routing
