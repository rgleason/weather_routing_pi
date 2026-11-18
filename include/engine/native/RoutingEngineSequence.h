// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <chrono>
#include <functional>
#include <optional>
#include <utility>
#include <string>
#include "RoutingEngineSettings.h"
#include "RoutingSequenceAudit.h"
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
    unsigned enabled = 0xffffffffU,
    const std::function<std::optional<supercpn::weather_routing::RoutingResult>()>&
        completion = {}, bool competingFastPair = false,
    const std::function<std::optional<double>(SequenceEngine)>& measuredSeconds = {}) {
  namespace wr = supercpn::weather_routing;
  wr::RoutingResult best, last;
  bool haveBest = false;
  std::string failures;
  std::vector<wr::RoutingAttemptFailure> attempts;
  std::vector<wr::RoutingAttempt> history;
  RoutingSequenceAudit audit;
  const auto finish = [&](wr::RoutingResult route) {
    audit.Finish(route);
    return route;
  };
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
      last.attemptFailures = attempts;
      last.engineAttempts = history;
      return finish(std::move(last));
    }
    const auto attemptStarted = std::chrono::steady_clock::now();
    last = solve(engine);
    auto seconds = std::chrono::duration<double>(
        std::chrono::steady_clock::now() - attemptStarted).count();
    if (measuredSeconds) seconds = measuredSeconds(engine).value_or(seconds);
    const auto work = audit.Record(last.diagnostics, seconds);
    last.engineIdentity = SequenceEngineId(engine);
    // Cancellation cancels the complete operation, including any earlier
    // winner.
    const bool competingFast = competingFastPair &&
        (engine == SequenceEngine::Original || engine == SequenceEngine::Quick2);
    if (cancel.cancelled() ||
        (last.status == wr::RoutingStatus::Cancelled && !competingFast)) {
      last.status = wr::RoutingStatus::Cancelled;
      history.push_back({SequenceEngineTitle(engine), last.status, last.message + "\n" + work});
      last.message = "Routing engine sequence cancelled";
      last.attemptFailures = attempts;
      last.engineAttempts = history;
      return finish(std::move(last));
    }
    if (!AcceptedRoute(last) &&
        (last.status == wr::RoutingStatus::Complete ||
         last.status == wr::RoutingStatus::CompleteUsingReverseRecovery ||
         last.status == wr::RoutingStatus::CompleteUsingFrontierRecovery ||
         last.status == wr::RoutingStatus::CompleteUsingGraphFallback))
      last.status = wr::RoutingStatus::ValidationFailure;
    history.push_back({SequenceEngineTitle(engine), last.status,
                       last.message + "\n" + work});
    if (AcceptedRoute(last)) {
      last.attemptFailures = attempts;
      last.engineAttempts = history;
      if (accepted) accepted(engine, last);
      if (!haveBest || last.legs.back().endTime < best.legs.back().endTime) {
        best = std::move(last);
        haveBest = true;
      }
      // Auto returns its first accepted result, including Quick2. All alone
      // continues the selected-engine comparison.
      if (mode == RoutingEngine::Auto && !competingFast) {
        best.attemptFailures = attempts;
        best.engineAttempts = history;
        return finish(std::move(best));
      }
    } else if (!(competingFast && last.status == wr::RoutingStatus::Cancelled)) {
      if (!failures.empty()) failures += "; ";
      std::string detail = last.message.empty() ? "no validated complete route" : last.message;
      if (last.status == wr::RoutingStatus::WaveDataRequired)
        detail = "Required wave-height coverage is unavailable for this route and time";
      else if (engine == SequenceEngine::Quick && detail.find("Quick search did not find") == 0)
        detail = "Search allowance exhausted before finding a validated route";
      attempts.push_back({SequenceEngineTitle(engine), last.status, detail});
      failures += std::string(SequenceEngineTitle(engine)) + ": [" +
          wr::toString(last.status) + "] " + detail;
    }
    if (competingFastPair && engine == SequenceEngine::Quick2 && haveBest) {
      best.attemptFailures = attempts;
      best.engineAttempts = history;
      return finish(std::move(best));
    }
    // A shared completion phase follows the last selected fast method. It
    // never delays Auto after an accepted result; All keeps comparing.
    if (completion && (engine == SequenceEngine::Quick2 ||
        (engine == SequenceEngine::Original &&
         !(enabled & EngineBit(RoutingEngine::Quick2))))) {
      const auto recoveryStarted = std::chrono::steady_clock::now();
      if (auto recovered = completion()) {
        const auto recoverySeconds = std::chrono::duration<double>(
            std::chrono::steady_clock::now()-recoveryStarted).count();
        const auto recoveryWork = audit.Record(recovered->diagnostics,recoverySeconds);
        history.push_back({"Quick3 recovery",recovered->status,
                           recovered->message + "\n" + recoveryWork});
        if (cancel.cancelled() || recovered->status == wr::RoutingStatus::Cancelled) {
          CancelRoutingResultKeepingDiagnostics(*recovered);
          recovered->engineAttempts = history;
          recovered->attemptFailures = attempts;
          return finish(std::move(*recovered));
        }
        if (AcceptedRoute(*recovered)) {
          recovered->engineIdentity = "quick3";
          if (accepted) accepted(engine,*recovered);
          if (!haveBest || recovered->legs.back().endTime < best.legs.back().endTime) {
            best=std::move(*recovered); haveBest=true;
          }
          if (mode == RoutingEngine::Auto) {
            best.engineAttempts=history; best.attemptFailures=attempts;
            return finish(std::move(best));
          }
        } else {
          attempts.push_back({"Quick3 recovery",recovered->status,recovered->message});
        }
      }
    }
  }
  if (haveBest) {
    best.attemptFailures = attempts;
    best.engineAttempts = history;
    return finish(std::move(best));
  }
  last.attemptFailures = std::move(attempts);
  last.engineAttempts = std::move(history);
  last.message = "No validated route found with the selected settings.\n" + failures;
  return finish(std::move(last));
}
}  // namespace weather_routing::native
