/***************************************************************************
 * Copyright (C) 2026 OpenCPN contributors
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 2 of the License, or (at your option)
 * any later version.
 ***************************************************************************/

#ifndef WEATHER_ROUTING_CHART_SAFETY_ATLAS_H
#define WEATHER_ROUTING_CHART_SAFETY_ATLAS_H

#include <cstdint>
#include <set>
#include <string>
#include <vector>

namespace weather_routing {

struct ChartSafetyAtlasChart {
  int db_index{-1};
  int chart_scale{-1};
  int source{0};
  std::int64_t edition_time{0};
  std::int64_t file_time{0};
  double min_lat{0.0};
  double min_lon{0.0};
  double max_lat{0.0};
  double max_lon{0.0};
  std::string path;
};

struct ChartSafetyAtlasEstimate {
  std::size_t available_charts{0};
  std::size_t selected_charts{0};
  std::uint64_t upper_bound_tiles{0};
  std::uint64_t compact_bytes{0};
  std::uint64_t recommended_quota_bytes{0};
  bool complete{true};
};

constexpr double kChartSafetyAtlasTileDegrees = 0.05;
constexpr int kChartSafetyAtlasCellsPerTile = 40;
// 41 x 41 cells x (uint16 hazard + uint8 depth-present + float depth), plus
// compact record metadata and an average chart dependency identifier.
constexpr std::uint64_t kChartSafetyAtlasEstimatedBytesPerTile = 12288;

enum class ChartSafetyAtlasIdleDecision {
  Disabled,
  PauseForRoute,
  Run,
};

/** Pure lifecycle gate used to guarantee that route work owns priority. */
ChartSafetyAtlasIdleDecision DecideChartSafetyAtlasIdleWork(
    bool atlas_enabled, bool persistent_cache_enabled,
    bool chart_provider_available, bool route_idle);

/**
 * Estimate a composite atlas from chart-table bounds without opening charts.
 *
 * all_charts explicitly distinguishes an all-chart scope from a deliberately
 * empty custom selection. Bounding boxes produce a safe size upper bound;
 * actual chart footprints can only reduce the generated tile count.
 */
ChartSafetyAtlasEstimate EstimateChartSafetyAtlas(
    const std::vector<ChartSafetyAtlasChart>& charts,
    const std::set<std::string>& selected_paths = {},
    bool all_charts = true,
    std::uint64_t maximum_estimate_tiles = 2000000);

/** Estimate storage from a host-produced, coverage-polygon tile plan. */
ChartSafetyAtlasEstimate EstimateChartSafetyAtlasCoverage(
    const std::vector<ChartSafetyAtlasChart>& charts,
    const std::vector<std::pair<long, long>>& coverage_tiles,
    const std::set<std::string>& selected_paths = {},
    bool all_charts = true, bool complete = true);

/** Stable identity of the selected chart metadata and atlas tile semantics. */
std::string ChartSafetyAtlasIdentity(
    const std::vector<ChartSafetyAtlasChart>& charts,
    const std::set<std::string>& selected_paths = {},
    bool all_charts = true);

/** Return the same deduplicated upper-bound tile set used by the estimator. */
std::vector<std::pair<long, long>> ChartSafetyAtlasTiles(
    const std::vector<ChartSafetyAtlasChart>& charts,
    const std::set<std::string>& selected_paths = {},
    bool all_charts = true,
    std::uint64_t maximum_tiles = 2000000, bool* complete = nullptr);

/** Order an exact sparse tile set into provider-sized 6x6 buckets. */
std::vector<std::pair<long, long>> OrderChartSafetyAtlasTiles(
    const std::vector<std::pair<long, long>>& tiles);

}  // namespace weather_routing

#endif
