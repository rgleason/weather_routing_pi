// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <algorithm>
#include "RoutingEngineSettings.h"

namespace weather_routing::adaptive {
// Conservative admission estimate for one serial engine sequence. Shared GRIB
// frames/chart caches are admitted separately and are not multiplied here.
// This is not a calibrated whole-application RSS bound. Native byte/count
// guards and the existing measured-memory scheduler remain authoritative.
inline int EstimatedWorkerMiB(const RoutingEngineSettings& settings) {
  int search = 128;
  const auto includes = [&](RoutingEngine e) {
    return IsCombinedEngine(settings.engine)
               ? (settings.SelectedEngines() & EngineBit(e)) != 0
               : settings.engine == e;
  };
  if (includes(RoutingEngine::Original))
    search = std::max(search, settings.original.memoryBudgetMiB);
  if (includes(RoutingEngine::Quick))
    search = std::max(search, settings.quick.memoryBudgetMiB);
  if (includes(RoutingEngine::Atlas)) search = std::max(search, 256);
  if (includes(RoutingEngine::Main) || includes(RoutingEngine::Alternative))
    search = std::max(search, 1536);
  return search +
         128;  // Per-route point cache, numerical map and transient overhead.
}
}  // namespace weather_routing::adaptive
