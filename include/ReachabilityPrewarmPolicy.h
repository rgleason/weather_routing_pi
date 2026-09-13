/***************************************************************************
 * Copyright (C) 2026 OpenCPN contributors
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 2 of the License, or (at your option)
 * any later version.
 ***************************************************************************/

#ifndef WEATHER_ROUTING_REACHABILITY_PREWARM_POLICY_H
#define WEATHER_ROUTING_REACHABILITY_PREWARM_POLICY_H

namespace weather_routing {

struct ReachabilityPrewarmPlan {
  bool enabled{false};
  double direct_distance_nm{0.0};
  double maximum_cross_track_nm{0.0};
  double maximum_path_length_nm{0.0};
};

/**
 * Build a bounded, filled prefetch envelope for a coastal passage.
 *
 * For foci S and G, every point P on a route with total path length no more
 * than maximum_path_length_nm satisfies
 *
 *   d(S, P) + d(P, G) <= maximum_path_length_nm.
 *
 * The resulting ellipse is cache preparation only and never bounds solver
 * exploration. Longer routes continue to request authoritative tiles on
 * demand. Long passages deliberately retain the sparse ocean-fan policy.
 */
ReachabilityPrewarmPlan BuildReachabilityPrewarmPlan(
    double direct_distance_nm);

}  // namespace weather_routing

#endif
