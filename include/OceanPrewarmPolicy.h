/***************************************************************************
 * Copyright (C) 2026 OpenCPN contributors
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 2 of the License, or (at your option)
 * any later version.
 ***************************************************************************/

#ifndef WEATHER_ROUTING_OCEAN_PREWARM_POLICY_H
#define WEATHER_ROUTING_OCEAN_PREWARM_POLICY_H

namespace weather_routing {

struct OceanPrewarmPlan {
  bool enabled{false};
  double raster_margin_nm{0.0};
  double inner_diversion_nm{0.0};
  double outer_diversion_nm{0.0};
  int corridor_count{0};
};

/**
 * Returns sparse cache-prefetch geometry for a long ocean passage.
 *
 * This policy never bounds solver exploration. It only chooses which
 * authoritative chart tiles to populate before on-demand segment checks.
 */
OceanPrewarmPlan BuildOceanPrewarmPlan(double passage_length_nm);

}  // namespace weather_routing

#endif
