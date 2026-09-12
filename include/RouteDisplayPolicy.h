/***************************************************************************
 * Small, GUI-independent rules for presenting route calculation results.
 ***************************************************************************/

#ifndef WEATHER_ROUTING_ROUTE_DISPLAY_POLICY_H
#define WEATHER_ROUTING_ROUTE_DISPLAY_POLICY_H

namespace weather_routing {

// RouteMapOverlay::EndTime() advances with the current frontier while a route
// is being calculated.  It becomes a publishable ETA only when the route has
// finished and actually reached its destination.
inline bool HasPublishableRouteResult(bool finished, bool reached_destination,
                                      bool end_time_valid) {
  return finished && reached_destination && end_time_valid;
}

}  // namespace weather_routing

#endif
