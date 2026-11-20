#pragma once

#include "supercpn/weather_routing/Engine.h"

namespace supercpn::weather_routing::internal {
// Only evaluation counters are reduced here. Search budgets, lineage and
// pruning stay on the coordinator and are committed in serial order.
inline void mergeEvaluationDiagnostics(RoutingDiagnostics& into,
                                       const RoutingDiagnostics& from) {
  into.weatherSamples += from.weatherSamples;
  into.xtdSamples += from.xtdSamples;
  into.xtdCoverageExpansions += from.xtdCoverageExpansions;
  into.landChecks += from.landChecks;
  into.landRejections += from.landRejections;
  into.constraintRejections += from.constraintRejections;
  into.propulsionRejections += from.propulsionRejections;
}


struct ResolvedEnvironment {
  EnvironmentalSnapshot snapshot;
  RoutingStatus failureStatus{RoutingStatus::Complete};
  std::string failureReason;
  std::vector<RoutingWarning> warnings;
};

ResolvedEnvironment resolveEnvironment(const RoutingRequest& request,
                                       const RoutingEnvironment& environment,
                                       GeoPoint position, TimePoint time);
TransitionReason transitionReason(EnvironmentalSource previous,
                                  EnvironmentalSource next);
void appendTransitions(std::vector<EnvironmentalSourceTransition>& transitions,
                       const EnvironmentalSnapshot* previous,
                       const EnvironmentalSnapshot& current, GeoPoint position,
                       TimePoint time, const EnvironmentalPolicy& policy);

}  // namespace supercpn::weather_routing::internal
