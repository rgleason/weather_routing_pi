// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <functional>
#include <string>
#include "supercpn/weather_routing/Engine.h"

namespace supercpn::weather_routing::professional_policy {
namespace wr = supercpn::weather_routing;
enum class Policy { Baseline, Sanity };
struct TraceEvent {
  std::string stage, action, reason;
  wr::GeoPoint position;
  wr::TimePoint time{};
  std::uint64_t generated{}, retained{}, landChecks{}, parent{}, node{};
  unsigned effort{}, attempt{};
  double wallMilliseconds{};
};
struct Options {
  Policy policy{Policy::Sanity};
  bool runInternalQuick{true};
  unsigned quickMemoryMiB{1024};
  // Rejected as a default after the first paired real-GRIB regression.
  bool refillSafeFringe{false};
  bool improveCircuitousArrivals{false};
  unsigned maximumArrivalShortcuts{8};
  // A poor incumbent or exhausted search may revisit the original departure
  // with an envelope learned from the integrated states the corridor excluded.
  // These are work limits/quality triggers, never route-validity constraints.
  bool adaptiveCorridor{true};
  // Retain the focused pilot's candidate, but restart difficult/circuitous
  // passages from their original departure without a forward strip or fringe.
  bool unrestrictedRecovery{true};
  // When Quick fails, establish a broad incumbent before a focused comparison.
  // A failed broad attempt retains a fresh full focused recovery ladder.
  bool unrestrictedFirst{true};
  // Research control: compare a resource-bounded forward frontier without
  // the direct-line strip. It does not disable physical/host constraints.
  bool focusForwardCorridor{true};
  // Separate labels whose sail-change or minimum motor-run obligations give
  // them different continuations, even at the same cell/time/heading.
  bool preserveContinuationState{true};
  bool refineBoundedConnections{true};
  // Learn a physical envelope for an independent comparison. Preserve the
  // compatibility search and its incumbent before changing approximate pruning.
  bool deriveMotionEnvelope{true};
  unsigned maximumCorridorPasses{2};
  // Zero uses the deterministic work allowance and whole-request cancellation.
  unsigned maximumCorridorMilliseconds{0};
  double corridorWorkFraction{0.5};
  // Optional research override. Zero uses revisited search cells, not a
  // fixed ratio between sailed distance and the direct chord.
  double corridorQualityDistanceRatio{0.0};
  bool detailedTrace{false};
  std::uint64_t maximumTraceEvents{100000};
  std::function<void(const TraceEvent&)> trace;
};
struct Diagnostics {
  std::uint64_t events{}, droppedEvents{}, observerErrors{};
  std::uint64_t fringeRejected{}, fringeChecked{}, reusedMotionChecks{};
  std::uint64_t graphGoalUpdates{}, graphBoundStops{}, coastalEscalations{};
  std::uint64_t reversePrefixRejections{};
  std::uint64_t initialWeatherReuses{};
  std::uint64_t shortcutAttempts{}, shortcutAccepted{};
  wr::Duration shortcutSaved{};
  bool internalQuickTried{}, internalQuickAccepted{};
  double deferredCrossTrackNm{}, corridorMilliseconds{};
  double motionEnvelopeNm{};
  bool useMotionFringe{};
  std::vector<double> corridorWidthsNm;
  std::uint64_t corridorAccepted{}, corridorGenerated{}, corridorLandChecks{};
  std::uint64_t corridorGeneratedBudget{};
  wr::Duration corridorSaved{};
  bool corridorBudgetExhausted{};
  double quickMilliseconds{}, solveMilliseconds{};
};
namespace detail {
wr::RoutingResult solveNative(const wr::RoutingRequest&,
    const wr::RoutingEnvironment&, const Options&, Diagnostics&);
wr::RoutingResult solveProfessional(const wr::RoutingRequest&,
    const wr::RoutingEnvironment&, const wr::RoutingResult*,
    const Options&, Diagnostics&);
}
}  // namespace supercpn::weather_routing::professional_policy
