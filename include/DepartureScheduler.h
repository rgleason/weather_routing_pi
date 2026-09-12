/***************************************************************************
 * Deterministic, bounded scheduling policy for departure-time candidates.
 ***************************************************************************/

#ifndef WEATHER_ROUTING_DEPARTURE_SCHEDULER_H
#define WEATHER_ROUTING_DEPARTURE_SCHEDULER_H

#include <algorithm>
#include <cstdlib>
#include <vector>

namespace weather_routing {

// Zero is stored by the route configuration to mean that the scheduler should
// choose a conservative machine-appropriate value.
constexpr int kAutomaticParallelDepartureCandidates = 0;
constexpr int kMaximumParallelDepartureCandidates = 12;
constexpr int kMaximumAutomaticParallelDepartureCandidates = 4;
constexpr int kFallbackAutomaticParallelDepartureCandidates = 4;
constexpr int kMaximumParallelAuthoritativeChartCandidates = 4;

inline void OrderDepartureOffsets(std::vector<int>& offsets) {
  std::sort(offsets.begin(), offsets.end(), [](int left, int right) {
    const int left_distance = std::abs(left);
    const int right_distance = std::abs(right);
    if (left_distance != right_distance) return left_distance < right_distance;
    return left < right;
  });
}

// A generated result can be selected and edited into the base route for a
// subsequent optimisation.  In that case the user's explicit request to
// optimise takes precedence over the runtime-only candidate marker inherited
// from the previous sweep.
inline bool ShouldPromoteDepartureOptimizationCandidate(
    bool optimization_enabled, bool optimization_candidate) {
  return optimization_enabled && optimization_candidate;
}

// Generated departure candidates carry runtime-only family metadata.  An
// arrival-time route has its own adaptive departure search and can never
// remain a member of the fixed departure-optimisation batch.  Editing a
// candidate into arrival mode must therefore promote it unconditionally,
// even though generated candidates have optimization_enabled set to false.
inline bool ShouldPromoteDepartureOptimizationCandidateForTimeMode(
    bool route_by_arrival, bool optimization_enabled,
    bool optimization_candidate) {
  return optimization_candidate &&
         (route_by_arrival || optimization_enabled);
}

inline int AutomaticDepartureWorkerLimit(int logical_cpu_count) {
  if (logical_cpu_count <= 0)
    return kFallbackAutomaticParallelDepartureCandidates;

  // Leave two logical CPUs available for OpenCPN's UI, chart services and the
  // operating system.  Four concurrent route engines is a deliberately
  // conservative automatic upper bound because each route can retain a large
  // search working set. Users may explicitly select a higher tested value.
  return std::max(
      1, std::min(kMaximumAutomaticParallelDepartureCandidates,
                  logical_cpu_count - 2));
}

inline int EffectiveRouteWorkerLimit(
    int configured_limit, bool departure_candidates_active,
    int requested_departure_workers = kAutomaticParallelDepartureCandidates,
    int logical_cpu_count = 0,
    bool requires_deterministic_host_service_lane = false,
    bool authoritative_chart_search = false) {
  const int valid_limit = std::max(1, configured_limit);
  if (!departure_candidates_active) return valid_limit;
  // Native candidates consume OpenCPN-owned GRIB, GSHHS and chart-safety
  // services.  Those services contain process-wide mutable state and are not
  // an observationally pure parallel environment: two otherwise identical
  // optimisation sweeps have been shown to retain different native
  // frontiers.  Keep authoritative candidates in one deterministic lane.
  // Chart-safety scouts are completed before this scheduler is entered and
  // may still use their own bounded parallelism.
  if (requires_deterministic_host_service_lane) return 1;

  const int departure_limit =
      requested_departure_workers <= kAutomaticParallelDepartureCandidates
          ? AutomaticDepartureWorkerLimit(logical_cpu_count)
          : std::min(requested_departure_workers,
                     kMaximumParallelDepartureCandidates);
  const int safety_aware_limit =
      authoritative_chart_search
          ? std::min(departure_limit,
                     kMaximumParallelAuthoritativeChartCandidates)
          : departure_limit;
  return std::min(valid_limit, safety_aware_limit);
}

}  // namespace weather_routing

#endif
