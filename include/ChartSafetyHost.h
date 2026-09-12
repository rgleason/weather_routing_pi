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
#include <chrono>
#include <string>
#include <vector>

#include "ChartSafetyAtlas.h"
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
/** Confirm and return the post-provider-load global semantic ABI identity. */
std::string ConfirmedIdentity();
bool FlushCache();
bool SetPersistentCacheEnabled(bool enabled);
bool SavePersistentCache();
bool ClearPersistentCache();
void InvalidateDerivedMasks();
/** Return applicable chart metadata without opening/decrypting chart data. */
std::vector<ChartSafetyAtlasChart> AtlasCharts();
/** Return exact coverage-polygon tiles for the selected chart metadata. */
std::vector<std::pair<long, long>> AtlasCoverageTiles(
    const std::vector<ChartSafetyAtlasChart>& charts,
    const std::set<std::string>& selected_paths, bool all_charts,
    std::uint64_t maximum_tiles, bool* complete);

/**
 * Install a non-owning cancellation flag for an externally controlled
 * prewarm. A null flag restores the normal GUI route behaviour.
 */
void SetPrewarmCancellationFlag(const std::atomic_bool* flag);
bool PrewarmCancellationRequested();
// A zero time point disables the cooperative preparation deadline.
void SetPrewarmDeadline(std::chrono::steady_clock::time_point deadline);

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
/** Prebuild an exact, bounded set of 0.05-degree semantic base tiles. */
bool PrewarmAtlasTiles(
    const std::vector<std::pair<long, long>>& tiles,
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
