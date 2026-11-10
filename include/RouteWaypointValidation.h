// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "RouteWaypointExtractor.h"
#include "RouteDiagnostics.h"

namespace weather_routing {
// Numerical coincidence only (about 0.01 mm on Earth), never a minimum
// waypoint spacing or a shoreline clearance rule. Longitude wraps at 180/360.
bool PositionsCoincide(double lat1, double lon1, double lat2, double lon2);
std::optional<RouteDiagnostic> CheckRouteLegPositions(
    const RouteWaypointInfo& from, const RouteWaypointInfo& to, int legIndex);
std::optional<RouteDiagnostic> CheckConsecutiveRouteWaypoints(
    const std::vector<RouteWaypointInfo>& points);
}  // namespace weather_routing
