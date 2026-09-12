#pragma once

#include "supercpn/weather_routing/Engine.h"

namespace supercpn::weather_routing::internal {

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
