// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "supercpn/weather_routing/Engine.h"

namespace supercpn::weather_routing {

// A deliberately narrow, Windy-style sector-frontier experiment. The search
// policy differs from Quick; physical motion and final validation do not.
struct AlternativeRoutingOptions {
  Duration timeStep{std::chrono::hours{1}};
  double headingStepDegrees{5.0};
  unsigned sectors{72};
  double arrivalRadiusNm{1.0};
  double preferredLandMarginNm{5.0};
  double motoringPruneFactor{0.4};
  double nearLandPruneFactor{0.6};
  std::uint64_t maximumGeneratedStates{3000000};
  std::uint64_t maximumWeatherSamples{50000000};
  std::uint64_t maximumLineageNodes{100000};
  unsigned maximumValidatedCandidates{16};
};

struct AlternativeRoutingDiagnostics {
  std::uint64_t attemptedMotions{};
  std::uint64_t lineageNodesAllocated{};
  unsigned layers{};
  unsigned validatedCandidates{};
};

struct AlternativeRoutingResult {
  RoutingResult route;
  AlternativeRoutingDiagnostics alternative;
};

class AlternativeRoutingEngine {
public:
  AlternativeRoutingResult route(const RoutingRequest&,
                                 const RoutingEnvironment&,
                                 const AlternativeRoutingOptions& = {}) const;
};

}  // namespace supercpn::weather_routing
