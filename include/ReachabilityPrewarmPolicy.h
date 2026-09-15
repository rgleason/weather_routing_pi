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
  /** Diagnostic compatibility mode; adaptive frontier prewarm leaves false. */
  bool prewarm_filled_envelope{false};
  double direct_distance_nm{0.0};
  double initial_scout_corridor_nm{0.0};
  double maximum_cross_track_nm{0.0};
  double maximum_path_length_nm{0.0};
};

/**
 * Build the bounds used by adaptive prewarm for a coastal passage.
 *
 * For foci S and G, every point P on a route with total path length no more
 * than maximum_path_length_nm satisfies
 *
 *   d(S, P) + d(P, G) <= maximum_path_length_nm.
 *
 * Adaptive mode prepares the scout corridor and uses these bounds only for
 * diagnostics while the live solver requests additional authoritative tiles
 * as needed. The old filled ellipse is retained for explicit comparison.
 * Neither mode bounds solver exploration. Long passages retain the sparse
 * ocean-fan policy.
 */
ReachabilityPrewarmPlan BuildReachabilityPrewarmPlan(
    double direct_distance_nm, bool request_legacy_filled_envelope = false);

}  // namespace weather_routing

#endif
