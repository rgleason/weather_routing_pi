// SPDX-License-Identifier: Apache-2.0
#pragma once

// Shared physical operations. Search policy remains in each engine.
#include "supercpn/weather_routing/Engine.h"

namespace supercpn::weather_routing::internal {
struct MotionState {
  GeoPoint position;
  TimePoint time{};
  double incomingHeading{};
  Tack tack{Tack::Unknown};
  PropulsionMode mode{PropulsionMode::Sail};
  ProfileRole role{ProfileRole::SailOnly};
  std::string profileIdentity;
  int sailPlan{-1};
  Duration modeDuration{}, motorDuration{}, waitDuration{};
  bool departureEgressActive{};
  double fuel{}, risk{};
  unsigned manoeuvres{};
};
struct MotionCandidate {
  MotionState state;
  RouteLeg leg;
};
std::vector<MotionCandidate> checkedMotion(const RoutingRequest&,
                                           const RoutingEnvironment&,
                                           const VesselPerformanceModel&,
                                           const MotionState&, double heading,
                                           Duration step, Duration slice,
                                           RoutingDiagnostics&, RoutingStatus*);
std::optional<MotionCandidate> checkedConnection(const RoutingRequest&,
                                                 const RoutingEnvironment&,
                                                 const VesselPerformanceModel&,
                                                 const MotionState&,
                                                 Duration window,
                                                 RoutingDiagnostics&);
std::optional<MotionCandidate> checkedWait(const RoutingRequest&,
                                           const RoutingEnvironment&,
                                           const MotionState&, Duration,
                                           RoutingDiagnostics&);
RoutingStatus failedPreflightStatus(const RoutingPreflightResult&);
void summariseRoute(RoutingResult&);
}  // namespace supercpn::weather_routing::internal
