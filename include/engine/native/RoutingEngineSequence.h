// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <utility>
#include <string>
#include "RoutingEngineSettings.h"
#include "supercpn/weather_routing/Types.h"

namespace weather_routing::native {
// Alternative is deliberately internal: it has no persisted/UI engine
// selection.
enum class SequenceEngine { Original, Quick, Alternative, Main };
inline const char* SequenceEngineId(SequenceEngine engine) {
  switch (engine) {
    case SequenceEngine::Original:
      return "original";
    case SequenceEngine::Quick:
      return "quick";
    case SequenceEngine::Alternative:
      return "alternative";
    case SequenceEngine::Main:
      return "main";
  }
  return "";
}
inline const char* SequenceEngineTitle(SequenceEngine engine) {
  switch (engine) {
    case SequenceEngine::Original:
      return "Quick";
    case SequenceEngine::Quick:
      return "Standard";
    case SequenceEngine::Alternative:
      return "Alternative";
    case SequenceEngine::Main:
      return "Professional";
  }
  return "";
}
template <typename Configuration>
Configuration ConfigurationForSequenceEngine(const Configuration& base,
                                             SequenceEngine engine) {
  auto candidate = base;
  candidate.EngineSettings.SetEngineId(engine == SequenceEngine::Alternative
                                           ? "main"
                                           : SequenceEngineId(engine));
  if (base.EngineSettings.engine == RoutingEngine::Auto &&
      engine == SequenceEngine::Main)
    candidate.RoutingEffortPercent = 400;
  return candidate;
}
inline bool AcceptedRoute(
    const supercpn::weather_routing::RoutingResult& route) {
  namespace wr = supercpn::weather_routing;
  const bool complete =
      route.status == wr::RoutingStatus::Complete ||
      route.status == wr::RoutingStatus::CompleteUsingReverseRecovery ||
      route.status == wr::RoutingStatus::CompleteUsingFrontierRecovery ||
      route.status == wr::RoutingStatus::CompleteUsingGraphFallback;
  return complete && route.validation.passed && !route.legs.empty() &&
         route.legs.back().endTime > route.legs.front().startTime;
}

// Run serially inside the existing routing worker; no additional worker or
// host-service concurrency. Equal ETAs keep the earlier engine
// deterministically.
template <typename Solve>
supercpn::weather_routing::RoutingResult RunRoutingEngineSequence(
    RoutingEngine mode,
    const supercpn::weather_routing::CancellationToken& cancel, Solve&& solve) {
  namespace wr = supercpn::weather_routing;
  wr::RoutingResult best, last;
  bool haveBest = false;
  std::string failures;
  constexpr std::array engines{SequenceEngine::Original, SequenceEngine::Quick,
                               SequenceEngine::Alternative,
                               SequenceEngine::Main};
  for (const auto engine : engines) {
    if (mode == RoutingEngine::Auto && engine == SequenceEngine::Alternative)
      continue;
    if (cancel.cancelled()) {
      last = {};
      last.status = wr::RoutingStatus::Cancelled;
      last.message = "Routing engine sequence cancelled";
      return last;
    }
    last = solve(engine);
    last.engineIdentity = SequenceEngineId(engine);
    // Cancellation cancels the complete operation, including any earlier
    // winner.
    if (cancel.cancelled() || last.status == wr::RoutingStatus::Cancelled) {
      last.status = wr::RoutingStatus::Cancelled;
      last.message = "Routing engine sequence cancelled";
      return last;
    }
    if (AcceptedRoute(last)) {
      if (mode == RoutingEngine::Auto) return last;
      if (!haveBest || last.legs.back().endTime < best.legs.back().endTime) {
        best = std::move(last);
        haveBest = true;
      }
    } else {
      if (!failures.empty()) failures += "; ";
      failures +=
          std::string(SequenceEngineTitle(engine)) + ": " +
          (last.message.empty() ? "no validated complete route" : last.message);
      if (last.status == wr::RoutingStatus::Complete ||
          last.status == wr::RoutingStatus::CompleteUsingReverseRecovery ||
          last.status == wr::RoutingStatus::CompleteUsingFrontierRecovery ||
          last.status == wr::RoutingStatus::CompleteUsingGraphFallback)
        last.status = wr::RoutingStatus::ValidationFailure;
    }
  }
  if (haveBest) return best;
  last.message = failures;
  return last;
}
}  // namespace weather_routing::native
