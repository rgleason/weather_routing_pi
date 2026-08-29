/***************************************************************************
 * Copyright (C) 2026 OpenCPN contributors
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 2 of the License, or (at your option)
 * any later version.
 ***************************************************************************/

#ifndef WEATHER_ROUTING_CHART_SAFETY_HOST_H
#define WEATHER_ROUTING_CHART_SAFETY_HOST_H

#include <atomic>
#include <string>

#include "ocpn_plugin.h"
#include "OptionalChartSafetyApi.h"

namespace weather_routing {

class ChartSafetyCache;

namespace chart_safety_host {

/** Detect and attach to the optional enhanced OpenCPN chart-safety service. */
bool Initialize(ChartSafetyCache* cache);
void Shutdown();
bool Available();
std::string Status();
bool FlushCache();
bool SetPersistentCacheEnabled(bool enabled);
bool SavePersistentCache();
bool ClearPersistentCache();
void InvalidateDerivedMasks();

/**
 * Install a non-owning cancellation flag for an externally controlled
 * prewarm. A null flag restores the normal GUI route behaviour.
 */
void SetPrewarmCancellationFlag(const std::atomic_bool* flag);
bool PrewarmCancellationRequested();

bool CheckSegment(double lat1, double lon1, double lat2, double lon2,
                  const PlugInSegmentSafetyOptions* options,
                  PlugInSegmentSafetyResult* result);
bool PrewarmHazardSnapshot(double min_lat, double min_lon, double max_lat,
                           double max_lon, int enable_fast_path,
                           int shadow_compare,
                           const PlugInSegmentSafetyOptions* options,
                           PlugInSegmentSafetyResult* result);
bool PrewarmRouteMaskForSegment(
    double lat1, double lon1, double lat2, double lon2,
    double corridor_margin_nm, const PlugInSegmentSafetyOptions* options,
    PlugInSegmentSafetyResult* result);
bool PrewarmReachabilityEnvelope(
    double start_lat, double start_lon, double end_lat, double end_lon,
    double maximum_path_length_nm,
    const PlugInSegmentSafetyOptions* options,
    PlugInSegmentSafetyResult* result);
bool PrewarmRouteMaskForPolylinesWithTileHalo(
    const double* latitudes, const double* longitudes,
    const int* point_counts, int polyline_count, double corridor_margin_nm,
    int fine_tile_halo, const PlugInSegmentSafetyOptions* options,
    PlugInSegmentSafetyResult* result);
bool ServicePendingRequests(
    int max_requests, int max_milliseconds,
    PlugInSegmentSafetyRequestServiceResult* result);
void ReleaseRouteMaskPins();

}  // namespace chart_safety_host
}  // namespace weather_routing

#endif
