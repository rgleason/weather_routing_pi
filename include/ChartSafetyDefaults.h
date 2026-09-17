#ifndef WEATHER_ROUTING_CHART_SAFETY_DEFAULTS_H
#define WEATHER_ROUTING_CHART_SAFETY_DEFAULTS_H

namespace weather_routing::chart_safety_defaults {

// The enhanced host API is deliberately opt-in. Most Weather Routing users
// run an unmodified OpenCPN build, where GSHHS remains the portable baseline.
inline constexpr bool kCheckLoadedCharts = false;
inline constexpr bool kRequireChartDepthChecks = false;

}  // namespace weather_routing::chart_safety_defaults

#endif
