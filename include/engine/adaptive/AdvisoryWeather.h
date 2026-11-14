// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "supercpn/weather_routing/Types.h"

namespace weather_routing::adaptive {
struct AdvisoryWindCurrent {
  supercpn::weather_routing::WindSample wind;
  supercpn::weather_routing::CurrentSample current;
};

// Forecast wind is ground-relative; the prepared callback returns
// water-relative wind using its own current. Preserve a known forecast current
// when only wind needs fallback, rather than subtracting two different currents
// or replacing it.
inline AdvisoryWindCurrent SelectAdvisoryWindCurrent(
    const AdvisoryWindCurrent& forecastGround,
    const AdvisoryWindCurrent& preparedWater) {
  AdvisoryWindCurrent out;
  out.current = forecastGround.current.available ? forecastGround.current
                                                 : preparedWater.current;
  out.wind =
      forecastGround.wind.available ? forecastGround.wind : preparedWater.wind;
  if (!out.wind.available) return out;
  if (!forecastGround.wind.available && preparedWater.current.available) {
    out.wind.velocity.eastKnots += preparedWater.current.velocity.eastKnots;
    out.wind.velocity.northKnots += preparedWater.current.velocity.northKnots;
  }
  if (out.current.available) {
    out.wind.velocity.eastKnots -= out.current.velocity.eastKnots;
    out.wind.velocity.northKnots -= out.current.velocity.northKnots;
  }
  return out;
}
}  // namespace weather_routing::adaptive
