/***************************************************************************
 * Copyright (C) 2026 OpenCPN contributors
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 2 of the License, or (at your option)
 * any later version.
 ***************************************************************************/

#ifndef WEATHER_ROUTING_CHART_SAFETY_CACHE_H
#define WEATHER_ROUTING_CHART_SAFETY_CACHE_H

#include <cstddef>
#include <cstdint>
#include <list>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

#include "AppendOnlyCache.h"
#include "ocpn_plugin.h"
#include "OptionalChartSafetyApi.h"

namespace weather_routing {

struct ChartSafetyCacheStats {
  std::uint64_t ram_hits{0};
  std::uint64_t disk_hits{0};
  std::uint64_t misses{0};
  std::uint64_t stores{0};
  std::uint64_t evictions{0};
  std::uint64_t rejected_records{0};
  std::uint64_t flushes{0};
  std::uint64_t ram_bytes{0};
  std::uint64_t ram_budget_bytes{0};
  std::uint64_t disk_bytes_read{0};
  std::uint64_t disk_bytes_written{0};
  std::uint64_t disk_file_bytes{0};
  std::uint64_t disk_budget_bytes{0};
  std::size_t ram_entries{0};
  std::size_t disk_entries{0};
  std::size_t dirty_entries{0};
};

struct ChartSafetyAtlasCacheStatus {
  bool store_ready{false};
  bool completion_marker_matches{false};
  bool complete{false};
  std::size_t expected_tiles{0};
  std::size_t present_tiles{0};
  std::vector<std::pair<long, long>> missing_tiles;
  std::string error;
};

/** Immutable plugin-owned chart classification tile used by route workers. */
struct ChartHazardTile {
  int group_index{0};
  long lat_tile{0};
  long lon_tile{0};
  double resolution{0.0};
  int rows{0};
  int cols{0};
  int chart_db_index{-1};
  int chart_scale{-1};
  int source{PI_SEGMENT_SAFETY_SOURCE_NONE};
  std::uint32_t hazard_summary_flags{0};
  bool depth_complete{false};
  std::string chart_path;
  std::vector<unsigned short> hazard_flags;
  std::vector<unsigned char> has_depth;
  std::vector<float> min_depth_m;
};

/**
 * Plugin-owned hot RAM and persistent authoritative chart-safety tile cache.
 *
 * OpenCPN supplies classifications through optional callbacks. This class
 * owns capacity, LRU eviction, persistence, corruption recovery and chart
 * identity invalidation.
 */
class ChartSafetyCache {
public:
  ChartSafetyCache();
  ~ChartSafetyCache();

  void Configure(const std::string& path, int requested_ram_mib,
                 bool persistent_enabled);
  /** Confirm an identity obtained after all chart providers are loaded. */
  void SetIdentity(const std::string& identity);
  /** Record a startup/provider notification without opening disk storage. */
  void SetProvisionalIdentity(const std::string& identity);
  void SetPersistentEnabled(bool enabled);
  void SetRequestedRamMiB(int requested_ram_mib);
  /** Limit the logical persistent semantic cache, including atlas tiles. */
  void SetMaximumDiskMiB(int maximum_disk_mib);

  int RequestedRamMiB() const;
  int EffectiveRamMiB() const;
  bool PersistentEnabled() const;
  bool Ready() const;
  std::string Identity() const;
  std::string LastError() const;
  ChartSafetyCacheStats Stats() const;

  bool Lookup(long lat_tile, long lon_tile, bool require_depth,
              PlugInSegmentSafetyTile* tile);
  bool LookupSnapshot(long lat_tile, long lon_tile, bool require_depth,
                      std::shared_ptr<const ChartHazardTile>* tile);
  void Store(const PlugInSegmentSafetyTile* tile);
  bool Flush(bool allow_compaction = false);
  bool Clear();

  /**
   * Compare an atlas plan with the durable tile index and its in-store
   * completion record. This never treats a config-only marker as proof.
   */
  ChartSafetyAtlasCacheStatus InspectAtlasCoverage(
      const std::string& atlas_identity,
      const std::vector<std::pair<long, long>>& expected_tiles);

  /**
   * Atomically append completion metadata after every expected tile has been
   * flushed to the identity-scoped persistent store.
   */
  bool CommitAtlasCompletion(
      const std::string& atlas_identity,
      const std::vector<std::pair<long, long>>& expected_tiles);

  /** Remove only the atlas completion proof, retaining reusable tiles. */
  bool ClearAtlasCompletion();

  static int LookupCallback(void* context, long lat_tile, long lon_tile,
                            int require_depth,
                            PlugInSegmentSafetyTile* tile);
  static void StoreCallback(void* context,
                            const PlugInSegmentSafetyTile* tile);
  static void IdentityCallback(void* context, const char* identity);

private:
  struct TileData {
    int group_index{0};
    long lat_tile{0};
    long lon_tile{0};
    double resolution{0.0};
    int rows{0};
    int cols{0};
    int chart_db_index{-1};
    int chart_scale{-1};
    int source{PI_SEGMENT_SAFETY_SOURCE_NONE};
    std::uint32_t hazard_summary_flags{0};
    bool depth_complete{false};
    std::string chart_path;
    std::string dependency_identity;
    std::vector<unsigned short> hazard_flags;
    std::vector<unsigned char> has_depth;
    std::vector<float> min_depth_m;
  };

  struct RamEntry {
    TileData tile;
    std::size_t bytes{0};
    std::list<std::string>::iterator lru;
  };

  static std::uint64_t PhysicalMemoryBytes();
  static int ResolveEffectiveRamMiB(int requested_ram_mib);
  static std::string TileKey(long lat_tile, long lon_tile);
  static std::size_t TileBytes(const std::string& key, const TileData& tile);
  static bool ReadExternalTile(const PlugInSegmentSafetyTile* source,
                               TileData* tile);
  static bool WriteExternalTile(const TileData& source,
                                PlugInSegmentSafetyTile* tile);
  static bool Serialize(const TileData& tile,
                        std::vector<unsigned char>* bytes);
  static bool Deserialize(const std::vector<unsigned char>& bytes,
                          TileData* tile);
  static std::uint64_t AtlasCoverageDigest(
      const std::string& atlas_identity,
      const std::vector<std::pair<long, long>>& expected_tiles);
  static bool SerializeAtlasCompletion(
      const std::string& atlas_identity,
      const std::vector<std::pair<long, long>>& expected_tiles,
      std::vector<unsigned char>* bytes);
  static bool AtlasCompletionMatches(
      const std::vector<unsigned char>& bytes,
      const std::string& atlas_identity,
      const std::vector<std::pair<long, long>>& expected_tiles);

  bool OpenStoreLocked();
  void InsertRamLocked(const std::string& key, TileData tile);
  void TouchLocked(std::map<std::string, RamEntry>::iterator entry);
  void EnforceBudgetLocked();
  void UpdateStoreStatsLocked();

  mutable std::mutex mutex_;
  std::string path_;
  std::string identity_;
  std::string last_error_;
  int requested_ram_mib_;
  int effective_ram_mib_;
  bool persistent_enabled_;
  int maximum_disk_mib_;
  bool configured_;
  bool identity_confirmed_;
  bool store_open_;
  AppendOnlyCache store_;
  std::map<std::string, RamEntry> ram_;
  std::list<std::string> lru_;
  std::map<std::string, AppendOnlyCacheRecord> dirty_;
  ChartSafetyCacheStats stats_;
};

}  // namespace weather_routing

#endif  // WEATHER_ROUTING_CHART_SAFETY_CACHE_H
