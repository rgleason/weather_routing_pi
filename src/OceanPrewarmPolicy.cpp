/***************************************************************************
 * Copyright (C) 2026 OpenCPN contributors
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 2 of the License, or (at your option)
 * any later version.
 ***************************************************************************/

#include "OceanPrewarmPolicy.h"

#include <algorithm>
#include <cmath>

namespace weather_routing {

OceanPrewarmPlan BuildOceanPrewarmPlan(double passage_length_nm) {
  OceanPrewarmPlan plan;
  if (!std::isfinite(passage_length_nm) || passage_length_nm < 600.0)
    return plan;

  plan.enabled = true;
  plan.raster_margin_nm =
      std::clamp(passage_length_nm * 0.002, 6.0, 12.0);
  plan.outer_diversion_nm =
      std::clamp(passage_length_nm * 0.15, 90.0, 900.0);
  plan.inner_diversion_nm = plan.outer_diversion_nm * 0.5;
  plan.corridor_count = 5;
  return plan;
}

}  // namespace weather_routing
