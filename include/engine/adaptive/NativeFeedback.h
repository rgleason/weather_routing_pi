// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "Controller.h"

namespace weather_routing::adaptive {
// Translate the frozen engine's existing diagnostics at the adapter boundary.
// The controller consumes a typed cause; no engine implementation is changed.
inline StopCause NativeStopCause(const wr::RoutingResult& route) {
  if (route.status != wr::RoutingStatus::ResourceLimitReached ||
      route.message != "candidate validation allowance reached")
    return StopCause::Other;
  unsigned repairs = 0;
  for (const auto& reason : route.diagnostics.stageStopReasons)
    repairs += reason.starts_with("prefix repair failed at leg ");
  return repairs >= 2 ? StopCause::RepeatedChronologicalRepair
                      : StopCause::Other;
}
}  // namespace weather_routing::adaptive
