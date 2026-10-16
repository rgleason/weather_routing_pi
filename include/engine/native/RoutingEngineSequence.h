// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <functional>
#include <utility>
#include <string>
#include "RoutingEngineSettings.h"
#include "supercpn/weather_routing/Engine.h"

namespace weather_routing::native {
enum class SequenceEngine { Original, Quick2, Quick, Alternative, Main, Atlas };
inline RoutingEngine SequenceRoutingEngine(SequenceEngine engine) {
  switch (engine) {
    case SequenceEngine::Original: return RoutingEngine::Original;
    case SequenceEngine::Quick2: return RoutingEngine::Quick2;
    case SequenceEngine::Quick: return RoutingEngine::Quick;
    case SequenceEngine::Alternative: return RoutingEngine::Alternative;
    case SequenceEngine::Main: return RoutingEngine::Main;
    case SequenceEngine::Atlas: return RoutingEngine::Atlas;
  }
  return RoutingEngine::Unsupported;
}
inline const char* SequenceEngineId(SequenceEngine engine) {
  switch (engine) {
    case SequenceEngine::Original:
      return "original";
    case SequenceEngine::Quick2:
      return "quick2";
    case SequenceEngine::Quick:
      return "quick";
    case SequenceEngine::Alternative:
      return "alternative";
    case SequenceEngine::Main:
      return "main";
    case SequenceEngine::Atlas:
      return "atlas";
  }
  return "";
}
inline const char* SequenceEngineTitle(SequenceEngine engine) {
  switch (engine) {
    case SequenceEngine::Original:
      return "Quick";
    case SequenceEngine::Quick2:
      return "Quick2";
    case SequenceEngine::Quick:
      return "Standard";
    case SequenceEngine::Alternative:
      return "Alternative";
    case SequenceEngine::Main:
      return "Professional";
    case SequenceEngine::Atlas:
      return "Atlas";
  }
  return "";
}
template <typename Configuration>
Configuration ConfigurationForSequenceEngine(const Configuration& base,
                                             SequenceEngine engine) {
  auto candidate = base;
  candidate.EngineSettings.SetEngineId(SequenceEngineId(engine));
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
    const supercpn::weather_routing::CancellationToken& cancel, Solve&& solve,
    const std::function<void(SequenceEngine,
        const supercpn::weather_routing::RoutingResult&)>& accepted = {},
    unsigned enabled = 0xffffffffU) {
  namespace wr = supercpn::weather_routing;
  wr::RoutingResult best, last;
  bool haveBest = false;
  std::string failures;
  std::vector<wr::RoutingAttemptFailure> attempts;
  if (enabled == 0xffffffffU)
    enabled = mode == RoutingEngine::Auto ? kAutoEnginesMask : kAllEnginesMask;
  if (!enabled || (enabled & ~kSelectableEnginesMask)) {
    last.status = wr::RoutingStatus::InvalidVesselConfiguration;
    last.message = "Select at least one routing engine in Advanced";
    return last;
  }
  constexpr std::array engines{SequenceEngine::Original, SequenceEngine::Quick2, SequenceEngine::Atlas,
                               SequenceEngine::Quick, SequenceEngine::Alternative,
                               SequenceEngine::Main};
  for (const auto engine : engines) {
    if (!(enabled & EngineBit(SequenceRoutingEngine(engine)))) continue;
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
      if (accepted) accepted(engine, last);
      if (!haveBest || last.legs.back().endTime < best.legs.back().endTime) {
        best = std::move(last);
        haveBest = true;
      }
      // Quick2 is a provisional fallback, held only here. The callback gets
      // no incumbent or spent budget: selected Standard starts from scratch.
      // A later accepted engine ends Auto with the best result found so far.
      if (mode == RoutingEngine::Auto && engine != SequenceEngine::Quick2)
        return best;
    } else {
      if (!failures.empty()) failures += "; ";
      std::string detail = last.message.empty() ? "no validated complete route" : last.message;
      if (last.status == wr::RoutingStatus::WaveDataRequired)
        detail = "Required wave-height coverage is unavailable for this route and time";
      else if (engine == SequenceEngine::Quick && detail.find("Quick search did not find") == 0)
        detail = "Search allowance exhausted before finding a validated route";
      attempts.push_back({SequenceEngineTitle(engine), last.status, detail});
      failures += std::string(SequenceEngineTitle(engine)) + ": [" +
          wr::toString(last.status) + "] " + detail;
      if (last.status == wr::RoutingStatus::Complete ||
          last.status == wr::RoutingStatus::CompleteUsingReverseRecovery ||
          last.status == wr::RoutingStatus::CompleteUsingFrontierRecovery ||
          last.status == wr::RoutingStatus::CompleteUsingGraphFallback)
        last.status = wr::RoutingStatus::ValidationFailure;
    }
  }
  if (haveBest) return best;
  last.attemptFailures = std::move(attempts);
  last.message = "No validated route found with the selected settings.\n" + failures;
  return last;
}
}  // namespace weather_routing::native
