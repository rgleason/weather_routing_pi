// SPDX-License-Identifier: GPL-3.0-or-later
#include "RouteWaypointValidation.h"
#include <cmath>
#include <algorithm>
#include <wx/intl.h>

namespace weather_routing {
bool PositionsCoincide(double lat1, double lon1, double lat2, double lon2) {
  if (!std::isfinite(lat1) || !std::isfinite(lon1) ||
      !std::isfinite(lat2) || !std::isfinite(lon2) ||
      std::abs(lat1) > 90 || std::abs(lat2) > 90) return false;
  constexpr double radians = 0.017453292519943295;
  const double dlat = (lat1 - lat2) * radians;
  const double dlon = std::remainder(lon1 - lon2, 360.0) * radians;
  const double halfChord = std::hypot(std::sin(dlat / 2),
      std::sqrt(std::max(0.0, std::cos(lat1 * radians) *
                                  std::cos(lat2 * radians))) * std::sin(dlon / 2));
  return halfChord <= std::sin(1e-10 * radians / 2);
}
std::optional<RouteDiagnostic> CheckRouteLegPositions(
    const RouteWaypointInfo& from, const RouteWaypointInfo& to, int legIndex) {
  if (!PositionsCoincide(from.lat, from.lon, to.lat, to.lon)) return {};
  const auto label = [](const RouteWaypointInfo& point) {
    return point.name.empty()
        ? wxString::Format(_("unnamed waypoint %d"), point.index)
        : point.name;
  };
  return RouteDiagnostic{"setup.coincident_waypoints", DiagnosticSeverity::Error,
      wxString::Format(_("Waypoints '%s' and '%s' occupy the same position"),
                       label(from), label(to)),
      wxString::Format(_("Leg %d has no distance to route. Remove or move one of these consecutive waypoints in the OpenCPN route, then recreate the Weather Routing legs. Other visits to the same position are allowed."), legIndex),
      _("Edit route waypoints"), {}, true};
}
std::optional<RouteDiagnostic> CheckConsecutiveRouteWaypoints(
    const std::vector<RouteWaypointInfo>& points) {
  for (size_t i = 1; i < points.size(); ++i)
    if (auto issue = CheckRouteLegPositions(points[i - 1], points[i], int(i)))
      return issue;
  return {};
}
}  // namespace weather_routing
