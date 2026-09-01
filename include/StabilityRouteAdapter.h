#ifndef _WEATHER_ROUTING_STABILITY_ROUTE_ADAPTER_H_
#define _WEATHER_ROUTING_STABILITY_ROUTE_ADAPTER_H_

#include <vector>

#include "weather_routing_engine/StabilityCorridor.h"

class RouteMapOverlay;
struct RouteMapConfiguration;

std::vector<weather_routing_engine::StabilityRoute>
BuildValidatedStabilityRoutes(const std::vector<RouteMapOverlay*>& routeMaps);

std::vector<weather_routing_engine::StabilityRoute>
BuildValidatedMultiLegStabilityRoutes(
    const std::vector<std::vector<RouteMapOverlay*> >& candidateRoutes);

bool CheckStabilitySafetySegment(
    const RouteMapConfiguration& configuration,
    const weather_routing_engine::StabilityPoint& first,
    const weather_routing_engine::StabilityPoint& last);

bool CheckStabilitySafetyCell(const RouteMapConfiguration& configuration,
                              double minLat, double minLon, double maxLat,
                              double maxLon);

#endif
