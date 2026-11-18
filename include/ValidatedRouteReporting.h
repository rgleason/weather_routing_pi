// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "supercpn/weather_routing/Types.h"

namespace weather_routing {
// A search leg can straddle a weather-source boundary. Chronological replay
// samples inside the leg and is authoritative for delivered source usage.
inline void UseValidatedSourceReporting(
    supercpn::weather_routing::RoutingResult& result) {
  namespace wr = supercpn::weather_routing;
  if (!result.validation.passed || result.legs.empty()) return;
  switch (result.status) {
    case wr::RoutingStatus::Complete:
    case wr::RoutingStatus::CompleteUsingReverseRecovery:
    case wr::RoutingStatus::CompleteUsingFrontierRecovery:
    case wr::RoutingStatus::CompleteUsingGraphFallback:
      result.environment = result.validation.environment;
      result.sourceTransitions = result.validation.sourceTransitions;
      break;
    default: break;
  }
}
}  // namespace weather_routing
