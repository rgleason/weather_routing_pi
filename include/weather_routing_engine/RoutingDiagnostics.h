/***************************************************************************
 *   Copyright (C) 2026 by OpenCPN contributors                            *
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 3 of the License, or     *
 *   (at your option) any later version.                                   *
 ***************************************************************************/

#ifndef _WEATHER_ROUTING_ENGINE_ROUTING_DIAGNOSTICS_H_
#define _WEATHER_ROUTING_ENGINE_ROUTING_DIAGNOSTICS_H_

namespace weather_routing_engine {

struct RoutingDiagnostics {
  long generatedMoves;
  long acceptedMoves;
  long chartLandRejections;
  long missingSafetyTiles;
  long fineTilesBuilt;
  long persistentCacheHits;
  long chartApiCallsFromWorkers;
  long finalValidationMs;

  RoutingDiagnostics()
      : generatedMoves(-1),
        acceptedMoves(-1),
        chartLandRejections(-1),
        missingSafetyTiles(-1),
        fineTilesBuilt(-1),
        persistentCacheHits(-1),
        chartApiCallsFromWorkers(-1),
        finalValidationMs(-1) {}
};

}  // namespace weather_routing_engine

#endif
