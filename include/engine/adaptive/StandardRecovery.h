// SPDX-License-Identifier: Apache-2.0
#pragma once
#include <array>

#include "supercpn/weather_routing/Engine.h"

namespace supercpn::weather_routing {

// Limits apply to Quick's tracked search containers, not GRIB/host memory or
// allocations made inside caller-provided physics/environment callbacks.
struct AdaptiveStandardOptions {
  std::uint32_t memoryBudgetMiB{256};
  // Separate experimental engine. Default preserves the native 2/3 split.
  unsigned initialPhaseNumerator{2};
  unsigned initialPhaseDenominator{3};
  std::uint64_t initialPhaseAbsoluteStates{};
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

struct AdaptiveStandardDiagnostics {
  std::uint32_t policyVersion{1};
  std::uint32_t memoryBudgetMiB{};
  std::uint64_t peakSearchBytes{};
  std::uint64_t weatherCalls{};
  std::uint64_t attemptedMotions{};
  std::uint64_t reclaimedLabels{};
  unsigned attempts{};
  std::array<std::uint64_t, 2> phaseGenerated{}, phaseMotions{};
  unsigned validatedCandidates{};
  unsigned comfortAlternatives{};
  unsigned missingComfortMotions{};
  bool comfortAllowanceReached{};
};

struct AdaptiveStandardResult {
  RoutingResult route;
  AdaptiveStandardDiagnostics quick;
  std::vector<RoutingResult> alternatives;
};

class AdaptiveStandardEngine {
public:
  AdaptiveStandardResult route(const RoutingRequest&, const RoutingEnvironment&,
                               const AdaptiveStandardOptions& = {}) const;
};
}  // namespace supercpn::weather_routing
