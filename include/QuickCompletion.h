// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <span>
#include "supercpn/weather_routing/PartialRoutes.h"

namespace weather_routing {
namespace wr = supercpn::weather_routing;
struct QuickCompletionOptions {
  std::chrono::milliseconds maximumTime{3000};
  std::uint64_t maximumMotionCalls{4000};
  std::size_t maximumSeeds{8}, maximumRetainedStates{2048}, beamWidth{32};
  double maximumRemainingNm{40};
  wr::Duration maximumTailDuration{std::chrono::hours{12}};
  std::function<bool(const wr::RouteLeg&)> deliveredLegAllowed;
};
struct QuickCompletionResult {
  wr::RoutingResult route;
  unsigned tracesExamined{}, prefixesRejected{}, seedsTried{}, candidatesReplayed{};
  std::uint64_t motionCalls{};
  std::size_t peakRetainedStates{};
  bool allowanceReached{};
  double milliseconds{};
};
// Traces are immutable, bounded snapshots. Their owners remain alive during
// this synchronous call; no original engine arena or callback is retained.
QuickCompletionResult CompleteQuickPrefixes(const wr::RoutingRequest&,
    const wr::RoutingEnvironment&,
    std::span<const wr::PartialRouteArchive::Entry* const>,
    const QuickCompletionOptions& = {});
}  // namespace weather_routing
