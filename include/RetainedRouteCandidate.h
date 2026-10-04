// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <atomic>
#include <memory>
#include "RouteComfort.h"
#include "RouteMap.h"

namespace weather_routing {
struct RetainedRouteCandidate {
  std::string id;
  std::shared_ptr<const wr::RoutingResult> result;
  RouteMapConfiguration configuration;
  RouteComfort comfort;
  RouteComfort windOnlyComfort;
  double processingMilliseconds{};
};
inline RetainedRouteCandidate RetainRouteCandidate(
    const wr::RoutingResult& result, const RouteMapConfiguration& configuration) {
  static std::atomic<std::uint64_t> nextId{0};
  const auto started = std::chrono::steady_clock::now();
  RetainedRouteCandidate candidate;
  candidate.id = "route-" + std::to_string(++nextId);
  candidate.configuration = configuration;
  // Search geometry can be very large. Retain delivered legs and provenance,
  // not each engine's exploratory isochrones, and never retain solver nodes.
  auto compact = std::make_shared<wr::RoutingResult>();
  compact->status = result.status;
  compact->legs = result.legs;
  compact->metrics = result.metrics;
  compact->validation = result.validation;
  compact->environment = result.environment;
  compact->solverPath = result.solverPath;
  compact->warnings = result.warnings;
  compact->sourceTransitions = result.sourceTransitions;
  compact->engineIdentity = result.engineIdentity.empty()
                               ? configuration.EngineSettings.EngineId()
                               : result.engineIdentity;
  compact->message = result.message;
  compact->searchVariant = result.searchVariant;
  compact->diagnostics = result.diagnostics;
  candidate.result = compact;
  candidate.comfort = CalculateRouteComfort(compact->legs);
  candidate.windOnlyComfort = CalculateRouteComfort(compact->legs, true);
  candidate.processingMilliseconds = std::chrono::duration<double, std::milli>(
      std::chrono::steady_clock::now() - started).count();
  return candidate;
}
} // namespace weather_routing
