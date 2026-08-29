/***************************************************************************
 * Copyright (C) 2026 OpenCPN contributors
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 2 of the License, or (at your option)
 * any later version.
 ***************************************************************************/

#include "ChartSafetyAtlas.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <map>
#include <sstream>
#include <set>

namespace weather_routing {
namespace {

using Tile = std::pair<long, long>;

long BatchBucket(long tile) {
  return static_cast<long>(std::floor(static_cast<double>(tile) / 6.0));
}

bool Selected(const ChartSafetyAtlasChart& chart,
              const std::set<std::string>& selected_paths,
              bool all_charts) {
  return all_charts || selected_paths.count(chart.path) != 0;
}

void HashAdd(std::uint64_t* hash, const std::string& value) {
  for (unsigned char byte : value) {
    *hash ^= byte;
    *hash *= 1099511628211ULL;
  }
  *hash ^= static_cast<unsigned char>('|');
  *hash *= 1099511628211ULL;
}

bool AddLongitudeRange(double min_lat, double max_lat, double min_lon,
                       double max_lon, std::uint64_t maximum_tiles,
                       std::set<Tile>* tiles) {
  if (!tiles || !std::isfinite(min_lat) || !std::isfinite(max_lat) ||
      !std::isfinite(min_lon) || !std::isfinite(max_lon) ||
      min_lat >= max_lat || min_lon >= max_lon)
    return true;

  min_lat = std::clamp(min_lat, -90.0, 90.0);
  max_lat = std::clamp(max_lat, -90.0, 90.0);
  min_lon = std::clamp(min_lon, -180.0, 180.0);
  max_lon = std::clamp(max_lon, -180.0, 180.0);
  if (min_lat >= max_lat || min_lon >= max_lon) return true;

  const long min_lat_tile =
      static_cast<long>(std::floor(min_lat / kChartSafetyAtlasTileDegrees));
  const long max_lat_tile = static_cast<long>(
      std::ceil(max_lat / kChartSafetyAtlasTileDegrees)) - 1;
  const long min_lon_tile =
      static_cast<long>(std::floor(min_lon / kChartSafetyAtlasTileDegrees));
  const long max_lon_tile = static_cast<long>(
      std::ceil(max_lon / kChartSafetyAtlasTileDegrees)) - 1;

  for (long lat_tile = min_lat_tile; lat_tile <= max_lat_tile; ++lat_tile) {
    for (long lon_tile = min_lon_tile; lon_tile <= max_lon_tile; ++lon_tile) {
      tiles->insert({lat_tile, lon_tile});
      if (tiles->size() > maximum_tiles) return false;
    }
  }
  return true;
}

}  // namespace

std::vector<std::pair<long, long>> ChartSafetyAtlasTiles(
    const std::vector<ChartSafetyAtlasChart>& charts,
    const std::set<std::string>& selected_paths,
    bool all_charts,
    std::uint64_t maximum_tiles, bool* complete) {
  std::set<Tile> tiles;
  bool all_added = true;
  if (maximum_tiles == 0) maximum_tiles = 1;

  for (const auto& chart : charts) {
    if (!Selected(chart, selected_paths, all_charts)) continue;
    double min_lon = chart.min_lon;
    double max_lon = chart.max_lon;
    while (min_lon < -180.0) min_lon += 360.0;
    while (min_lon > 180.0) min_lon -= 360.0;
    while (max_lon < -180.0) max_lon += 360.0;
    while (max_lon > 180.0) max_lon -= 360.0;

    if (min_lon <= max_lon) {
      all_added = AddLongitudeRange(
          chart.min_lat, chart.max_lat, min_lon, max_lon, maximum_tiles,
          &tiles);
    } else {
      all_added = AddLongitudeRange(chart.min_lat, chart.max_lat, min_lon,
                                    180.0, maximum_tiles, &tiles) &&
                  AddLongitudeRange(chart.min_lat, chart.max_lat, -180.0,
                                    max_lon, maximum_tiles, &tiles);
    }
    if (!all_added) break;
  }
  if (complete) *complete = all_added;
  // Provider extraction is most efficient for compact rectangular blocks.
  // Preserve the exact sparse tile set but order it into canonical 6x6
  // buckets, including correct floor semantics west/south of zero.
  std::vector<Tile> ordered;
  ordered.reserve(tiles.size());
  std::map<std::pair<long, long>, std::vector<Tile>> buckets;
  for (const auto& tile : tiles)
    buckets[{BatchBucket(tile.first), BatchBucket(tile.second)}].push_back(
        tile);
  for (const auto& bucket : buckets)
    ordered.insert(ordered.end(), bucket.second.begin(), bucket.second.end());
  return ordered;
}

ChartSafetyAtlasEstimate EstimateChartSafetyAtlas(
    const std::vector<ChartSafetyAtlasChart>& charts,
    const std::set<std::string>& selected_paths,
    bool all_charts,
    std::uint64_t maximum_estimate_tiles) {
  ChartSafetyAtlasEstimate result;
  result.available_charts = charts.size();
  for (const auto& chart : charts)
    if (Selected(chart, selected_paths, all_charts))
      ++result.selected_charts;

  bool complete = true;
  const auto tiles = ChartSafetyAtlasTiles(
      charts, selected_paths, all_charts, maximum_estimate_tiles, &complete);
  result.upper_bound_tiles = tiles.size();
  result.compact_bytes =
      result.upper_bound_tiles * kChartSafetyAtlasEstimatedBytesPerTile;
  // Leave headroom for the append-only journal between compactions and for
  // dependency metadata. This is a quota recommendation, not a prediction of
  // the compressed final footprint.
  result.recommended_quota_bytes =
      result.compact_bytes + result.compact_bytes / 4;
  result.complete = complete;
  return result;
}

std::string ChartSafetyAtlasIdentity(
    const std::vector<ChartSafetyAtlasChart>& charts,
    const std::set<std::string>& selected_paths, bool all_charts) {
  std::vector<const ChartSafetyAtlasChart*> selected;
  for (const auto& chart : charts)
    if (Selected(chart, selected_paths, all_charts)) selected.push_back(&chart);
  std::sort(selected.begin(), selected.end(), [](const auto* first,
                                                  const auto* second) {
    return first->path < second->path;
  });
  std::uint64_t hash = 1469598103934665603ULL;
  HashAdd(&hash, "semantic-atlas-v1:tile=0.05:cells=40");
  for (const auto* chart : selected) {
    HashAdd(&hash, chart->path);
    std::ostringstream metadata;
    metadata.precision(17);
    metadata << chart->chart_scale << ':' << chart->source << ':'
             << chart->edition_time << ':' << chart->file_time << ':'
             << chart->min_lat << ':' << chart->min_lon << ':'
             << chart->max_lat << ':' << chart->max_lon;
    HashAdd(&hash, metadata.str());
  }
  std::ostringstream result;
  result << "atlas-v1-" << std::hex << hash;
  return result.str();
}

}  // namespace weather_routing
