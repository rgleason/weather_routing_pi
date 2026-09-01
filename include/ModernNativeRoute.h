#ifndef WEATHER_ROUTING_MODERN_NATIVE_ROUTE_H
#define WEATHER_ROUTING_MODERN_NATIVE_ROUTE_H

#include <wx/string.h>

class RouteMapOverlay;
struct RouteMapConfiguration;

/** Return whether this configuration will use the modern native solver. */
bool ModernNativeRouteEnabled(const RouteMapConfiguration& configuration);

/**
 * Return whether this route still invokes a legacy provider without an
 * immutable/thread-safe snapshot contract.
 */
bool ModernNativeRouteRequiresSerialHostServices(
    const RouteMapConfiguration& configuration);

/** Run the modern value-node/Pareto/recovery solver for one configured leg. */
bool RunModernNativeRoute(RouteMapOverlay& overlay, wxString& error);

#endif
