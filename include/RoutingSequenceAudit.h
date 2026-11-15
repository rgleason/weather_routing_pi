// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <algorithm>
#include <cstdint>
#include <iomanip>
#include <limits>
#include <sstream>
#include <string>
#include <utility>
#include "supercpn/weather_routing/Types.h"

namespace weather_routing {
// A whole-operation stop must discard delivered-route data while preserving
// the reporting information already returned by the method sequence.
inline void CancelRoutingResultKeepingDiagnostics(
    supercpn::weather_routing::RoutingResult& result) {
  auto diagnostics = std::move(result.diagnostics);
  result = {};
  result.diagnostics = std::move(diagnostics);
  result.status = supercpn::weather_routing::RoutingStatus::Cancelled;
  result.message = "Routing cancelled";
}

// Client-side accounting only. A solver's returned counters can omit internal
// retries or unfinished work; they must not be advertised as measured CPU,
// allocated memory, or an exhaustive count of the operation's search.
class RoutingSequenceAudit {
 public:
  std::string Record(const supercpn::weather_routing::RoutingDiagnostics& d,
                     double seconds) {
    ++methods_;
    seconds_ += seconds;
    Add(generated_, d.generatedStates);
    retained_ = std::max(retained_, d.retainedStates);
    Add(landChecks_, d.landChecks);
    Add(validation_, d.validationSamples);
    Add(weather_, d.weatherSamples);
    closest_ = std::min(closest_, d.closestApproachNm);
    std::ostringstream out;
    out << std::fixed << std::setprecision(3)
        << "Elapsed wall time: " << seconds
        << " s (includes service waits); reported generated states: "
        << d.generatedStates << "; reported retained states: "
        << d.retainedStates << "; land checks: " << d.landChecks
        << "; validation samples: " << d.validationSamples;
    return out.str();
  }

  void Finish(supercpn::weather_routing::RoutingResult& result) const {
    if (!methods_) return;
    std::ostringstream out;
    out << std::fixed << std::setprecision(3)
        << "Reported work across " << methods_
        << " routing method invocation(s): elapsed wall time " << seconds_
        << " s; generated states " << generated_
        << "; largest returned retained-state count " << retained_
        << "; land checks " << landChecks_
        << "; validation samples " << validation_;
    result.diagnostics.stageStopReasons.push_back(out.str());
    if (result.status != supercpn::weather_routing::RoutingStatus::Cancelled)
      return;  // Preserve the selected route's original diagnostics.
    // Cancellation still discards a route, but not the known work of earlier
    // methods. The current method's sampled progress is reported separately.
    result.diagnostics.generatedStates = generated_;
    result.diagnostics.retainedStates = retained_;
    result.diagnostics.landChecks = landChecks_;
    result.diagnostics.validationSamples = validation_;
    result.diagnostics.weatherSamples = weather_;
    result.diagnostics.closestApproachNm = closest_;
  }

 private:
  static void Add(std::uint64_t& total, std::uint64_t value) {
    const auto maximum = std::numeric_limits<std::uint64_t>::max();
    total = value > maximum - total ? maximum : total + value;
  }
  unsigned methods_{};
  double seconds_{}, closest_{std::numeric_limits<double>::infinity()};
  std::uint64_t generated_{}, retained_{}, landChecks_{}, validation_{}, weather_{};
};
}  // namespace weather_routing
