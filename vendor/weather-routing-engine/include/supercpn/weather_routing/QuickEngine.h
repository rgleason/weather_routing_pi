// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "supercpn/weather_routing/Engine.h"

namespace supercpn::weather_routing {

// Limits apply to Quick's tracked search containers, not GRIB/host memory or
// allocations made inside caller-provided physics/environment callbacks.
struct QuickRoutingOptions {
  std::uint32_t memoryBudgetMiB{256};
  unsigned beamWidth{96};
  unsigned recoveryBeamWidth{192};
  std::uint64_t maximumGeneratedStates{600000};
  std::uint64_t maximumWeatherCalls{120000000};
  unsigned maximumValidatedCandidates{3};
  Duration offshoreStep{std::chrono::hours{3}};
  double headingStepDegrees{20.0};
  // Opt-in exploration used alongside the normal engines. Zero disables it
  // completely; ordinary Standard searches keep first-success behaviour.
  double comfortWeight{};
  bool comfortWindOnly{true};
  unsigned maximumComfortAlternatives{4};
  std::chrono::milliseconds maximumSearchTime{};
};

struct QuickRoutingDiagnostics {
  std::uint32_t policyVersion{1};
  std::uint32_t memoryBudgetMiB{};
  std::uint64_t peakSearchBytes{};
  std::uint64_t weatherCalls{};
  std::uint64_t attemptedMotions{};
  std::uint64_t reclaimedLabels{};
  unsigned attempts{};
  unsigned validatedCandidates{};
  unsigned comfortAlternatives{};
  unsigned missingComfortMotions{};
  bool comfortAllowanceReached{};
};

struct QuickRoutingResult {
  RoutingResult route;
  QuickRoutingDiagnostics quick;
  std::vector<RoutingResult> alternatives;
};

class QuickRoutingEngine {
public:
  QuickRoutingResult route(const RoutingRequest&, const RoutingEnvironment&,
                           const QuickRoutingOptions& = {}) const;
};
}  // namespace supercpn::weather_routing
