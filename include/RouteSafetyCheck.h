/* Copyright (C) 2026 OpenCPN contributors. GPL v3 or later. */
#ifndef WEATHER_ROUTING_ROUTE_SAFETY_CHECK_H
#define WEATHER_ROUTING_ROUTE_SAFETY_CHECK_H

#include <cstddef>
#include <functional>
#include <string>
#include <vector>

#include "ocpn_plugin.h"
#include "OptionalChartSafetyApi.h"

namespace weather_routing {

struct SafetyCheckWaypoint {
  std::string guid;
  std::string name;
  double lat{0};
  double lon{0};
  bool operator==(const SafetyCheckWaypoint& other) const;
};

enum class RouteCheckState { Invalid, Running, Complete, Cancelled, Outdated };

struct RouteCheckFinding {
  std::size_t leg{0};  // One-based leg number.
  int status{PI_SEGMENT_SAFETY_ERROR};
  int source{PI_SEGMENT_SAFETY_SOURCE_NONE};
  double lat{0}, lon{0};
  double from_fraction{0}, to_fraction{0};
  bool has_depth{false};
  double minimum_depth_m{0};
  std::string chart_path, object, message;
};

/** Incremental, read-only check of the rhumb-line legs of a route snapshot.
 * Every chunk is checked as a segment, not as isolated sampled points.
 * The injected query is executed only by Advance(), on its caller's thread.
 */
class RouteSafetyCheck {
public:
  using Query = std::function<bool(double, double, double, double,
      const PlugInSegmentSafetyOptions*, PlugInSegmentSafetyResult*)>;
  RouteSafetyCheck(std::vector<SafetyCheckWaypoint> waypoints,
                   PlugInSegmentSafetyOptions options, Query query);
  void Advance();
  void Cancel();
  void Invalidate(const std::string& reason);
  bool Matches(const std::vector<SafetyCheckWaypoint>& waypoints) const;
  RouteCheckState State() const { return state_; }
  const std::string& Error() const { return error_; }
  const std::vector<RouteCheckFinding>& Findings() const { return findings_; }
  const std::vector<SafetyCheckWaypoint>& Waypoints() const { return waypoints_; }
  const PlugInSegmentSafetyOptions& Options() const { return options_; }
  std::size_t Total() const { return total_; }
  std::size_t Checked() const { return checked_; }
  std::size_t HazardChunks() const { return hazards_; }
  std::size_t UnverifiedChunks() const { return unverified_; }
  bool Clear() const;
  static bool IsUnverified(int status);
  static const char* Reason(int status);
private:
  std::vector<SafetyCheckWaypoint> waypoints_;
  PlugInSegmentSafetyOptions options_{};
  Query query_;
  RouteCheckState state_{RouteCheckState::Invalid};
  std::string error_;
  std::vector<std::size_t> chunks_;
  std::vector<RouteCheckFinding> findings_;
  std::size_t total_{0}, checked_{0}, hazards_{0}, unverified_{0};
  std::size_t leg_{0}, chunk_{0}, pending_retries_{0};
};
}  // namespace weather_routing
#endif
