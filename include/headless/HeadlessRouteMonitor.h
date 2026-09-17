#ifndef WEATHER_ROUTING_HEADLESS_ROUTE_MONITOR_H
#define WEATHER_ROUTING_HEADLESS_ROUTE_MONITOR_H

namespace weather_routing_headless {

enum class HeadlessRouteMonitorDecision { Continue, Complete, Timeout };

inline HeadlessRouteMonitorDecision EvaluateHeadlessRouteMonitor(
    bool route_work_active, long elapsed_ms, long timeout_ms) {
  if (!route_work_active) return HeadlessRouteMonitorDecision::Complete;
  if (elapsed_ms >= timeout_ms) return HeadlessRouteMonitorDecision::Timeout;
  return HeadlessRouteMonitorDecision::Continue;
}

}  // namespace weather_routing_headless

#endif
