/***************************************************************************
 * Copyright (C) 2026 OpenCPN contributors
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 2 of the License, or (at your option)
 * any later version.
 ***************************************************************************/

#include "ChartSafetyCache.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <limits>
#include <sstream>
#include <utility>

#include <wx/utils.h>

#if defined(__unix__) || defined(__APPLE__)
#include <unistd.h>
#endif
#ifdef _WIN32
#include <windows.h>
#endif

namespace {

// Version 2 invalidates tiles created before provider-priority-aware chart
// selection.  Those records could contain CM93 evidence for cells now covered
// by a preferred licensed/plugin vector chart.
constexpr std::uint32_t kTilePayloadVersion = 3;
constexpr std::uint32_t kAtlasCompletionVersion = 1;
constexpr char kAtlasCompletionKey[] = "@atlas-completion-v1";
constexpr std::uint64_t kEstimatedPersistentBytesPerTile = 12288;
constexpr std::size_t kFlushDirtyTiles = 512;
constexpr std::uint64_t kCompactThresholdBytes =
    1024ULL * 1024ULL * 1024ULL;
constexpr int kMinimumRamMiB = 256;
constexpr int kMaximumRamMiB = 8192;
constexpr int kMaximumAutomaticRamMiB = 2048;

template <typename T>
void AppendValue(std::vector<unsigned char>* output, const T& value) {
  const unsigned char* start =
      reinterpret_cast<const unsigned char*>(&value);
  output->insert(output->end(), start, start + sizeof(value));
}

template <typename T>
bool ReadValue(const std::vector<unsigned char>& input, std::size_t* offset,
               T* value) {
  if (!offset || !value || *offset > input.size() ||
      input.size() - *offset < sizeof(*value))
    return false;
  memcpy(value, input.data() + *offset, sizeof(*value));
  *offset += sizeof(*value);
  return true;
}

bool IsValidSource(int source) {
  return source >= PI_SEGMENT_SAFETY_SOURCE_NONE &&
         source <= PI_SEGMENT_SAFETY_SOURCE_PLUGIN_VECTOR;
}

void HashBytes(std::uint64_t* hash, const void* data, std::size_t size) {
  if (!hash || (!data && size)) return;
  const auto* bytes = static_cast<const unsigned char*>(data);
  for (std::size_t index = 0; index < size; ++index) {
    *hash ^= bytes[index];
    *hash *= 1099511628211ULL;
  }
}

template <typename T>
void HashValue(std::uint64_t* hash, const T& value) {
  HashBytes(hash, &value, sizeof(value));
}

}  // namespace

namespace weather_routing {

ChartSafetyCache::ChartSafetyCache()
    : requested_ram_mib_(0),
      effective_ram_mib_(ResolveEffectiveRamMiB(0)),
      persistent_enabled_(true),
      maximum_disk_mib_(2048),
      configured_(false),
      identity_confirmed_(false),
      store_open_(false) {
  stats_.ram_budget_bytes =
      static_cast<std::uint64_t>(effective_ram_mib_) * 1024ULL * 1024ULL;
  stats_.disk_budget_bytes =
      static_cast<std::uint64_t>(maximum_disk_mib_) * 1024ULL * 1024ULL;
}

ChartSafetyCache::~ChartSafetyCache() { Flush(true); }

std::uint64_t ChartSafetyCache::PhysicalMemoryBytes() {
#ifdef _WIN32
  MEMORYSTATUSEX status = {};
  status.dwLength = sizeof(status);
  if (GlobalMemoryStatusEx(&status)) return status.ullTotalPhys;
#elif defined(_SC_PHYS_PAGES) && defined(_SC_PAGESIZE)
  const long pages = sysconf(_SC_PHYS_PAGES);
  const long page_size = sysconf(_SC_PAGESIZE);
  if (pages > 0 && page_size > 0)
    return static_cast<std::uint64_t>(pages) *
           static_cast<std::uint64_t>(page_size);
#endif
  const wxMemorySize available = wxGetFreeMemory();
#if wxUSE_LONGLONG
  const wxLongLong_t value = available.GetValue();
  return value > 0 ? static_cast<std::uint64_t>(value) : 0;
#else
  return available > 0 ? static_cast<std::uint64_t>(available) : 0;
#endif
}

int ChartSafetyCache::ResolveEffectiveRamMiB(int requested_ram_mib) {
  if (requested_ram_mib > 0)
    return std::clamp(requested_ram_mib, kMinimumRamMiB, kMaximumRamMiB);
  const std::uint64_t physical_mib =
      PhysicalMemoryBytes() / (1024ULL * 1024ULL);
  int automatic = static_cast<int>(physical_mib / 32ULL);
  automatic = (automatic / 256) * 256;
  return std::clamp(automatic, kMinimumRamMiB,
                    kMaximumAutomaticRamMiB);
}

void ChartSafetyCache::Configure(const std::string& path,
                                 int requested_ram_mib,
                                 bool persistent_enabled) {
  std::lock_guard<std::mutex> lock(mutex_);
  path_ = path;
  requested_ram_mib_ =
      std::clamp(requested_ram_mib, 0, kMaximumRamMiB);
  effective_ram_mib_ = ResolveEffectiveRamMiB(requested_ram_mib_);
  persistent_enabled_ = persistent_enabled;
  configured_ = true;
  identity_confirmed_ = false;
  store_open_ = false;
  last_error_.clear();
  stats_.ram_budget_bytes =
      static_cast<std::uint64_t>(effective_ram_mib_) * 1024ULL * 1024ULL;
  EnforceBudgetLocked();
}

void ChartSafetyCache::SetIdentity(const std::string& identity) {
  std::lock_guard<std::mutex> lock(mutex_);
  if (identity == identity_) {
    identity_confirmed_ = !identity.empty();
    return;
  }
  identity_ = identity;
  identity_confirmed_ = !identity.empty();
  ram_.clear();
  lru_.clear();
  dirty_.clear();
  stats_.ram_bytes = 0;
  stats_.ram_entries = 0;
  store_open_ = false;
  last_error_.clear();
}

void ChartSafetyCache::SetProvisionalIdentity(const std::string& identity) {
  std::lock_guard<std::mutex> lock(mutex_);
  // A refresh notification for the already-confirmed identity is harmless.
  // This is the normal sequence when the core refreshes its identity at the
  // start of a raw-tile prewarm.
  if (identity == identity_) return;
  identity_ = identity;
  identity_confirmed_ = false;
  ram_.clear();
  lru_.clear();
  dirty_.clear();
  stats_.ram_bytes = 0;
  stats_.ram_entries = 0;
  stats_.dirty_entries = 0;
  store_open_ = false;
  last_error_.clear();
}

void ChartSafetyCache::SetPersistentEnabled(bool enabled) {
  if (!enabled) Flush(false);
  std::lock_guard<std::mutex> lock(mutex_);
  persistent_enabled_ = enabled;
  if (!enabled) store_open_ = false;
}

void ChartSafetyCache::SetRequestedRamMiB(int requested_ram_mib) {
  std::lock_guard<std::mutex> lock(mutex_);
  requested_ram_mib_ =
      std::clamp(requested_ram_mib, 0, kMaximumRamMiB);
  effective_ram_mib_ = ResolveEffectiveRamMiB(requested_ram_mib_);
  stats_.ram_budget_bytes =
      static_cast<std::uint64_t>(effective_ram_mib_) * 1024ULL * 1024ULL;
  EnforceBudgetLocked();
}

void ChartSafetyCache::SetMaximumDiskMiB(int maximum_disk_mib) {
  // Preserve pending records before reopening the index with its new logical
  // entry limit.  The append-only store will evict least-recently-used keys
  // if the reduced capacity no longer fits.
  Flush(false);
  std::lock_guard<std::mutex> lock(mutex_);
  maximum_disk_mib_ = std::clamp(maximum_disk_mib, 256, 16384);
  stats_.disk_budget_bytes =
      static_cast<std::uint64_t>(maximum_disk_mib_) * 1024ULL * 1024ULL;
  store_open_ = false;
}

int ChartSafetyCache::RequestedRamMiB() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return requested_ram_mib_;
}

int ChartSafetyCache::EffectiveRamMiB() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return effective_ram_mib_;
}

bool ChartSafetyCache::PersistentEnabled() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return persistent_enabled_;
}

bool ChartSafetyCache::Ready() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return configured_ && identity_confirmed_ && !identity_.empty() &&
         (!persistent_enabled_ || store_open_);
}

std::string ChartSafetyCache::Identity() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return identity_;
}

std::string ChartSafetyCache::LastError() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return last_error_;
}

ChartSafetyCacheStats ChartSafetyCache::Stats() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return stats_;
}

std::string ChartSafetyCache::TileKey(long lat_tile, long lon_tile) {
  return std::to_string(lat_tile) + ":" + std::to_string(lon_tile);
}

std::size_t ChartSafetyCache::TileBytes(const std::string& key,
                                        const TileData& tile) {
  return sizeof(RamEntry) + key.capacity() + tile.chart_path.capacity() +
         tile.dependency_identity.capacity() +
         tile.hazard_flags.capacity() * sizeof(tile.hazard_flags[0]) +
         tile.has_depth.capacity() * sizeof(tile.has_depth[0]) +
         tile.min_depth_m.capacity() * sizeof(tile.min_depth_m[0]) + 96;
}

bool ChartSafetyCache::ReadExternalTile(
    const PlugInSegmentSafetyTile* source, TileData* tile) {
  if (!source || !tile || source->struct_size <
                              static_cast<int>(sizeof(*source)) ||
      source->rows <= 0 || source->cols <= 0 ||
      source->rows > 256 || source->cols > 256 ||
      source->cell_capacity < source->rows * source->cols ||
      !source->hazard_flags || !source->has_depth ||
      !source->min_depth_m || !IsValidSource(source->source) ||
      !std::isfinite(source->resolution) || source->resolution <= 0.0)
    return false;
  const int cells = source->rows * source->cols;
  TileData result;
  result.group_index = source->group_index;
  result.lat_tile = source->lat_tile;
  result.lon_tile = source->lon_tile;
  result.resolution = source->resolution;
  result.rows = source->rows;
  result.cols = source->cols;
  result.chart_db_index = source->chart_db_index;
  result.chart_scale = source->chart_scale;
  result.source = source->source;
  result.hazard_summary_flags = source->hazard_summary_flags;
  result.depth_complete = source->depth_complete != 0;
  result.chart_path = source->chart_path;
  result.dependency_identity = source->dependency_identity;
  result.hazard_flags.assign(source->hazard_flags,
                             source->hazard_flags + cells);
  result.has_depth.assign(source->has_depth, source->has_depth + cells);
  result.min_depth_m.assign(source->min_depth_m,
                            source->min_depth_m + cells);
  for (int i = 0; i < cells; ++i)
    if (result.has_depth[i] > 1 ||
        (result.has_depth[i] && !std::isfinite(result.min_depth_m[i])))
      return false;
  *tile = std::move(result);
  return true;
}

bool ChartSafetyCache::WriteExternalTile(const TileData& source,
                                         PlugInSegmentSafetyTile* tile) {
  if (!tile || tile->struct_size < static_cast<int>(sizeof(*tile)) ||
      !tile->hazard_flags || !tile->has_depth || !tile->min_depth_m)
    return false;
  const int cells = source.rows * source.cols;
  if (cells <= 0 || tile->cell_capacity < cells ||
      source.hazard_flags.size() != static_cast<std::size_t>(cells) ||
      source.has_depth.size() != static_cast<std::size_t>(cells) ||
      source.min_depth_m.size() != static_cast<std::size_t>(cells))
    return false;
  tile->group_index = source.group_index;
  tile->lat_tile = source.lat_tile;
  tile->lon_tile = source.lon_tile;
  tile->resolution = source.resolution;
  tile->rows = source.rows;
  tile->cols = source.cols;
  tile->chart_db_index = source.chart_db_index;
  tile->chart_scale = source.chart_scale;
  tile->source = source.source;
  tile->hazard_summary_flags = source.hazard_summary_flags;
  tile->depth_complete = source.depth_complete ? 1 : 0;
  const std::size_t chart_path_size =
      std::min(source.chart_path.size(), sizeof(tile->chart_path) - 1);
  memcpy(tile->chart_path, source.chart_path.data(), chart_path_size);
  tile->chart_path[chart_path_size] = '\0';
  const std::size_t dependency_size = std::min(
      source.dependency_identity.size(),
      sizeof(tile->dependency_identity) - 1);
  memcpy(tile->dependency_identity, source.dependency_identity.data(),
         dependency_size);
  tile->dependency_identity[dependency_size] = '\0';
  std::copy(source.hazard_flags.begin(), source.hazard_flags.end(),
            tile->hazard_flags);
  std::copy(source.has_depth.begin(), source.has_depth.end(),
            tile->has_depth);
  std::copy(source.min_depth_m.begin(), source.min_depth_m.end(),
            tile->min_depth_m);
  return true;
}

bool ChartSafetyCache::Serialize(const TileData& tile,
                                 std::vector<unsigned char>* bytes) {
  if (!bytes || tile.rows <= 0 || tile.cols <= 0 ||
      tile.chart_path.size() > 4096 || tile.dependency_identity.size() > 256)
    return false;
  const std::uint32_t cells =
      static_cast<std::uint32_t>(tile.rows * tile.cols);
  if (!cells || tile.hazard_flags.size() != cells ||
      tile.has_depth.size() != cells || tile.min_depth_m.size() != cells)
    return false;
  bytes->clear();
  bytes->reserve(128 + tile.chart_path.size() +
                 cells * (sizeof(unsigned short) + sizeof(unsigned char) +
                          sizeof(float)));
  AppendValue(bytes, kTilePayloadVersion);
  AppendValue(bytes, tile.group_index);
  const std::int64_t lat_tile = tile.lat_tile;
  const std::int64_t lon_tile = tile.lon_tile;
  AppendValue(bytes, lat_tile);
  AppendValue(bytes, lon_tile);
  AppendValue(bytes, tile.resolution);
  AppendValue(bytes, tile.rows);
  AppendValue(bytes, tile.cols);
  AppendValue(bytes, tile.chart_db_index);
  AppendValue(bytes, tile.chart_scale);
  AppendValue(bytes, tile.source);
  AppendValue(bytes, tile.hazard_summary_flags);
  const std::uint8_t depth_complete = tile.depth_complete ? 1 : 0;
  AppendValue(bytes, depth_complete);
  const std::uint32_t path_size =
      static_cast<std::uint32_t>(tile.chart_path.size());
  AppendValue(bytes, path_size);
  bytes->insert(bytes->end(), tile.chart_path.begin(), tile.chart_path.end());
  const std::uint32_t dependency_size =
      static_cast<std::uint32_t>(tile.dependency_identity.size());
  AppendValue(bytes, dependency_size);
  bytes->insert(bytes->end(), tile.dependency_identity.begin(),
                tile.dependency_identity.end());
  AppendValue(bytes, cells);
  const unsigned char* hazards =
      reinterpret_cast<const unsigned char*>(tile.hazard_flags.data());
  bytes->insert(bytes->end(), hazards,
                hazards + cells * sizeof(tile.hazard_flags[0]));
  bytes->insert(bytes->end(), tile.has_depth.begin(), tile.has_depth.end());
  const unsigned char* depths =
      reinterpret_cast<const unsigned char*>(tile.min_depth_m.data());
  bytes->insert(bytes->end(), depths,
                depths + cells * sizeof(tile.min_depth_m[0]));
  return true;
}

bool ChartSafetyCache::Deserialize(const std::vector<unsigned char>& bytes,
                                   TileData* tile) {
  if (!tile) return false;
  std::size_t offset = 0;
  std::uint32_t version = 0;
  TileData result;
  std::int64_t lat_tile = 0;
  std::int64_t lon_tile = 0;
  std::uint8_t depth_complete = 0;
  std::uint32_t path_size = 0;
  std::uint32_t dependency_size = 0;
  std::uint32_t cells = 0;
  if (!ReadValue(bytes, &offset, &version) ||
      version != kTilePayloadVersion ||
      !ReadValue(bytes, &offset, &result.group_index) ||
      !ReadValue(bytes, &offset, &lat_tile) ||
      !ReadValue(bytes, &offset, &lon_tile) ||
      !ReadValue(bytes, &offset, &result.resolution) ||
      !ReadValue(bytes, &offset, &result.rows) ||
      !ReadValue(bytes, &offset, &result.cols) ||
      !ReadValue(bytes, &offset, &result.chart_db_index) ||
      !ReadValue(bytes, &offset, &result.chart_scale) ||
      !ReadValue(bytes, &offset, &result.source) ||
      !ReadValue(bytes, &offset, &result.hazard_summary_flags) ||
      !ReadValue(bytes, &offset, &depth_complete) ||
      !ReadValue(bytes, &offset, &path_size) || path_size > 4096 ||
      offset > bytes.size() || bytes.size() - offset < path_size)
    return false;
  result.lat_tile = static_cast<long>(lat_tile);
  result.lon_tile = static_cast<long>(lon_tile);
  result.depth_complete = depth_complete != 0;
  result.chart_path.assign(
      reinterpret_cast<const char*>(bytes.data() + offset), path_size);
  offset += path_size;
  if (!ReadValue(bytes, &offset, &dependency_size) ||
      dependency_size > 256 || offset > bytes.size() ||
      bytes.size() - offset < dependency_size)
    return false;
  result.dependency_identity.assign(
      reinterpret_cast<const char*>(bytes.data() + offset), dependency_size);
  offset += dependency_size;
  if (!ReadValue(bytes, &offset, &cells) || result.rows <= 0 ||
      result.cols <= 0 || result.rows > 256 || result.cols > 256 ||
      cells != static_cast<std::uint32_t>(result.rows * result.cols) ||
      !IsValidSource(result.source) || !std::isfinite(result.resolution) ||
      result.resolution <= 0.0)
    return false;
  const std::size_t needed =
      cells * (sizeof(unsigned short) + sizeof(unsigned char) + sizeof(float));
  if (offset > bytes.size() || bytes.size() - offset != needed) return false;
  result.hazard_flags.resize(cells);
  memcpy(result.hazard_flags.data(), bytes.data() + offset,
         cells * sizeof(result.hazard_flags[0]));
  offset += cells * sizeof(result.hazard_flags[0]);
  result.has_depth.assign(bytes.begin() + offset,
                          bytes.begin() + offset + cells);
  offset += cells;
  result.min_depth_m.resize(cells);
  memcpy(result.min_depth_m.data(), bytes.data() + offset,
         cells * sizeof(result.min_depth_m[0]));
  for (std::uint32_t i = 0; i < cells; ++i)
    if (result.has_depth[i] > 1 ||
        (result.has_depth[i] && !std::isfinite(result.min_depth_m[i])))
      return false;
  *tile = std::move(result);
  return true;
}

std::uint64_t ChartSafetyCache::AtlasCoverageDigest(
    const std::string& atlas_identity,
    const std::vector<std::pair<long, long>>& expected_tiles) {
  std::uint64_t hash = 1469598103934665603ULL;
  HashBytes(&hash, atlas_identity.data(), atlas_identity.size());
  const std::uint64_t count = expected_tiles.size();
  HashValue(&hash, count);
  for (const auto& tile : expected_tiles) {
    const std::int64_t latitude = tile.first;
    const std::int64_t longitude = tile.second;
    HashValue(&hash, latitude);
    HashValue(&hash, longitude);
  }
  return hash;
}

bool ChartSafetyCache::SerializeAtlasCompletion(
    const std::string& atlas_identity,
    const std::vector<std::pair<long, long>>& expected_tiles,
    std::vector<unsigned char>* bytes) {
  if (!bytes || atlas_identity.empty() || atlas_identity.size() > 65536)
    return false;
  bytes->clear();
  const std::uint64_t count = expected_tiles.size();
  const std::uint64_t digest =
      AtlasCoverageDigest(atlas_identity, expected_tiles);
  const std::uint32_t identity_size =
      static_cast<std::uint32_t>(atlas_identity.size());
  AppendValue(bytes, kAtlasCompletionVersion);
  AppendValue(bytes, count);
  AppendValue(bytes, digest);
  AppendValue(bytes, identity_size);
  bytes->insert(bytes->end(), atlas_identity.begin(), atlas_identity.end());
  return true;
}

bool ChartSafetyCache::AtlasCompletionMatches(
    const std::vector<unsigned char>& bytes,
    const std::string& atlas_identity,
    const std::vector<std::pair<long, long>>& expected_tiles) {
  std::size_t offset = 0;
  std::uint32_t version = 0;
  std::uint64_t count = 0;
  std::uint64_t digest = 0;
  std::uint32_t identity_size = 0;
  if (!ReadValue(bytes, &offset, &version) ||
      version != kAtlasCompletionVersion ||
      !ReadValue(bytes, &offset, &count) ||
      !ReadValue(bytes, &offset, &digest) ||
      !ReadValue(bytes, &offset, &identity_size) ||
      offset > bytes.size() || bytes.size() - offset != identity_size)
    return false;
  const std::string stored_identity(
      reinterpret_cast<const char*>(bytes.data() + offset), identity_size);
  return stored_identity == atlas_identity && count == expected_tiles.size() &&
         digest == AtlasCoverageDigest(atlas_identity, expected_tiles);
}

bool ChartSafetyCache::OpenStoreLocked() {
  if (!persistent_enabled_ || !identity_confirmed_ || path_.empty() ||
      identity_.empty())
    return false;
  last_error_.clear();
  const std::string store_identity =
      "weather-routing-chart-tile-v2:" + identity_;
  const std::uint64_t budget_bytes =
      static_cast<std::uint64_t>(maximum_disk_mib_) * 1024ULL * 1024ULL;
  const std::size_t maximum_entries = static_cast<std::size_t>(
      std::max<std::uint64_t>(1, budget_bytes /
                                    kEstimatedPersistentBytesPerTile));
  store_open_ =
      store_.Open(path_, store_identity, maximum_entries, &last_error_);
  UpdateStoreStatsLocked();
  return store_open_;
}

void ChartSafetyCache::TouchLocked(
    std::map<std::string, RamEntry>::iterator entry) {
  lru_.erase(entry->second.lru);
  lru_.push_back(entry->first);
  entry->second.lru = std::prev(lru_.end());
}

void ChartSafetyCache::InsertRamLocked(const std::string& key,
                                       TileData tile) {
  auto existing = ram_.find(key);
  if (existing != ram_.end()) {
    stats_.ram_bytes -= existing->second.bytes;
    lru_.erase(existing->second.lru);
    ram_.erase(existing);
  }
  lru_.push_back(key);
  RamEntry entry;
  entry.tile = std::move(tile);
  entry.bytes = TileBytes(key, entry.tile);
  entry.lru = std::prev(lru_.end());
  stats_.ram_bytes += entry.bytes;
  ram_[key] = std::move(entry);
  stats_.ram_entries = ram_.size();
  EnforceBudgetLocked();
}

void ChartSafetyCache::EnforceBudgetLocked() {
  while (stats_.ram_bytes > stats_.ram_budget_bytes && !lru_.empty()) {
    const std::string victim = lru_.front();
    lru_.pop_front();
    auto found = ram_.find(victim);
    if (found == ram_.end()) continue;
    stats_.ram_bytes -= found->second.bytes;
    ram_.erase(found);
    ++stats_.evictions;
  }
  stats_.ram_entries = ram_.size();
}

void ChartSafetyCache::UpdateStoreStatsLocked() {
  stats_.disk_entries = store_open_ ? store_.EntryCount() : 0;
  stats_.disk_bytes_read = store_.BytesRead();
  stats_.disk_bytes_written = store_.BytesWritten();
  stats_.disk_file_bytes = store_.FileBytes();
  stats_.dirty_entries = dirty_.size();
}

bool ChartSafetyCache::Lookup(long lat_tile, long lon_tile,
                              bool require_depth,
                              PlugInSegmentSafetyTile* tile) {
  std::lock_guard<std::mutex> lock(mutex_);
  const std::string key = TileKey(lat_tile, lon_tile);
  auto found = ram_.find(key);
  if (found != ram_.end()) {
    if (!require_depth || found->second.tile.depth_complete) {
      TouchLocked(found);
      ++stats_.ram_hits;
      return WriteExternalTile(found->second.tile, tile);
    }
  }
  // Open lazily on the first real tile request.  During OpenCPN startup the
  // host initially reports a provisional identity before later-loaded chart
  // providers (notably o-charts) are registered.  Opening an append-only
  // store for that provisional identity would reset an otherwise valid file
  // belonging to the final chart/provider identity before the provider gets
  // a chance to announce it.
  if (persistent_enabled_ && identity_confirmed_ && !store_open_ &&
      configured_ && !identity_.empty())
    OpenStoreLocked();
  if (!persistent_enabled_ || !store_open_) {
    ++stats_.misses;
    return false;
  }
  std::vector<unsigned char> bytes;
  std::string error;
  if (!store_.Get(key, &bytes, &error)) {
    if (!error.empty()) {
      last_error_ = error;
      ++stats_.rejected_records;
    }
    ++stats_.misses;
    UpdateStoreStatsLocked();
    return false;
  }
  TileData persistent;
  if (!Deserialize(bytes, &persistent) ||
      persistent.lat_tile != lat_tile || persistent.lon_tile != lon_tile ||
      (require_depth && !persistent.depth_complete)) {
    ++stats_.rejected_records;
    ++stats_.misses;
    UpdateStoreStatsLocked();
    return false;
  }
  InsertRamLocked(key, persistent);
  ++stats_.disk_hits;
  UpdateStoreStatsLocked();
  found = ram_.find(key);
  return WriteExternalTile(found != ram_.end() ? found->second.tile
                                                : persistent,
                           tile);
}

bool ChartSafetyCache::LookupSnapshot(
    long lat_tile, long lon_tile, bool require_depth,
    std::shared_ptr<const ChartHazardTile>* snapshot) {
  if (!snapshot) return false;
  // The authoritative contract currently uses a 41x41 grid.  Keep generous
  // capacity here so a future finer host tile can be consumed without changing
  // the worker-facing ownership API.
  constexpr int kMaximumSnapshotCells = 256 * 256;
  auto tile = std::make_shared<ChartHazardTile>();
  tile->hazard_flags.resize(kMaximumSnapshotCells);
  tile->has_depth.resize(kMaximumSnapshotCells);
  tile->min_depth_m.resize(kMaximumSnapshotCells);

  PlugInSegmentSafetyTile external = {};
  external.struct_size = sizeof(external);
  external.lat_tile = lat_tile;
  external.lon_tile = lon_tile;
  external.hazard_flags = tile->hazard_flags.data();
  external.has_depth = tile->has_depth.data();
  external.min_depth_m = tile->min_depth_m.data();
  external.cell_capacity = kMaximumSnapshotCells;
  if (!Lookup(lat_tile, lon_tile, require_depth, &external)) return false;

  const std::size_t cells =
      static_cast<std::size_t>(external.rows) * external.cols;
  if (!cells || cells > kMaximumSnapshotCells) return false;
  tile->group_index = external.group_index;
  tile->lat_tile = external.lat_tile;
  tile->lon_tile = external.lon_tile;
  tile->resolution = external.resolution;
  tile->rows = external.rows;
  tile->cols = external.cols;
  tile->chart_db_index = external.chart_db_index;
  tile->chart_scale = external.chart_scale;
  tile->source = external.source;
  tile->hazard_summary_flags = external.hazard_summary_flags;
  tile->depth_complete = external.depth_complete != 0;
  tile->chart_path = external.chart_path;
  tile->hazard_flags.resize(cells);
  tile->has_depth.resize(cells);
  tile->min_depth_m.resize(cells);
  *snapshot = std::move(tile);
  return true;
}

void ChartSafetyCache::Store(const PlugInSegmentSafetyTile* tile) {
  TileData incoming;
  if (!ReadExternalTile(tile, &incoming)) return;
  std::vector<unsigned char> bytes;
  if (!Serialize(incoming, &bytes)) return;

  bool should_flush = false;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    const std::string key = TileKey(incoming.lat_tile, incoming.lon_tile);
    InsertRamLocked(key, incoming);
    ++stats_.stores;
    // The chart-provider contract permits derived semantic safety tiles to be
    // retained.  Plugin-vector evidence therefore follows the same durable,
    // identity-scoped policy as native vector and CM93 evidence.
    // Only depth-complete records belong in the durable semantic atlas. A
    // later land-only query must never downgrade an authoritative depth tile
    // which was already proven complete and persisted.
    if (persistent_enabled_ && identity_confirmed_ &&
        incoming.depth_complete) {
      dirty_[key] = {key, std::move(bytes)};
      stats_.dirty_entries = dirty_.size();
      should_flush = dirty_.size() >= kFlushDirtyTiles;
    }
  }
  if (should_flush) Flush(false);
}

bool ChartSafetyCache::Flush(bool allow_compaction) {
  std::lock_guard<std::mutex> lock(mutex_);
  if (!persistent_enabled_ || !identity_confirmed_ || dirty_.empty()) {
    UpdateStoreStatsLocked();
    return true;
  }
  if (!store_open_ && !OpenStoreLocked()) return false;
  std::vector<AppendOnlyCacheRecord> records;
  records.reserve(dirty_.size());
  for (const auto& item : dirty_) records.push_back(item.second);
  last_error_.clear();
  if (!store_.PutBatch(records, &last_error_)) {
    UpdateStoreStatsLocked();
    return false;
  }
  dirty_.clear();
  ++stats_.flushes;
  const std::uint64_t disk_budget_bytes =
      static_cast<std::uint64_t>(maximum_disk_mib_) * 1024ULL * 1024ULL;
  if ((allow_compaction && store_.FileBytes() > kCompactThresholdBytes) ||
      store_.FileBytes() > disk_budget_bytes)
    store_.Compact(&last_error_);
  UpdateStoreStatsLocked();
  return last_error_.empty();
}

bool ChartSafetyCache::Clear() {
  std::lock_guard<std::mutex> lock(mutex_);
  ram_.clear();
  lru_.clear();
  dirty_.clear();
  stats_.ram_bytes = 0;
  stats_.ram_entries = 0;
  stats_.dirty_entries = 0;
  if (!store_open_) {
    store_open_ = false;
    last_error_.clear();
    std::error_code error;
    if (!path_.empty()) std::filesystem::remove(path_, error);
    if (error) {
      last_error_ = "unable to remove chart-safety cache: " +
                    error.message();
      return false;
    }
    stats_.disk_entries = 0;
    stats_.disk_file_bytes = 0;
    return true;
  }
  last_error_.clear();
  const bool result = store_.Clear(&last_error_);
  UpdateStoreStatsLocked();
  return result;
}

ChartSafetyAtlasCacheStatus ChartSafetyCache::InspectAtlasCoverage(
    const std::string& atlas_identity,
    const std::vector<std::pair<long, long>>& expected_tiles) {
  ChartSafetyAtlasCacheStatus status;
  status.expected_tiles = expected_tiles.size();
  if (!Flush(false)) {
    status.error = LastError();
    return status;
  }

  std::lock_guard<std::mutex> lock(mutex_);
  if ((!store_open_ && !OpenStoreLocked()) || !persistent_enabled_ ||
      !identity_confirmed_) {
    status.error = last_error_.empty() ? "persistent tile store unavailable"
                                       : last_error_;
    return status;
  }
  status.store_ready = true;
  status.missing_tiles.reserve(expected_tiles.size());
  for (const auto& tile : expected_tiles) {
    const std::string key = TileKey(tile.first, tile.second);
    if (store_.Contains(key))
      ++status.present_tiles;
    else
      status.missing_tiles.push_back(tile);
  }

  std::vector<unsigned char> marker;
  std::string marker_error;
  if (store_.Get(kAtlasCompletionKey, &marker, &marker_error))
    status.completion_marker_matches = AtlasCompletionMatches(
        marker, atlas_identity, expected_tiles);
  else if (!marker_error.empty())
    status.error = marker_error;

  status.complete = status.completion_marker_matches &&
                    status.missing_tiles.empty();
  if (status.completion_marker_matches && !status.complete) {
    std::string erase_error;
    if (!store_.Erase(kAtlasCompletionKey, &erase_error) &&
        status.error.empty())
      status.error = erase_error;
  }
  UpdateStoreStatsLocked();
  return status;
}

bool ChartSafetyCache::CommitAtlasCompletion(
    const std::string& atlas_identity,
    const std::vector<std::pair<long, long>>& expected_tiles) {
  if (!Flush(false)) return false;
  std::lock_guard<std::mutex> lock(mutex_);
  if ((!store_open_ && !OpenStoreLocked()) || !persistent_enabled_ ||
      !identity_confirmed_)
    return false;
  for (const auto& tile : expected_tiles)
    if (!store_.Contains(TileKey(tile.first, tile.second))) {
      last_error_ = "atlas completion refused: expected tile is missing";
      return false;
    }

  std::vector<unsigned char> marker;
  if (!SerializeAtlasCompletion(atlas_identity, expected_tiles, &marker)) {
    last_error_ = "unable to serialize atlas completion";
    return false;
  }
  AppendOnlyCacheRecord record{kAtlasCompletionKey, std::move(marker)};
  if (!store_.PutBatch({record}, &last_error_)) {
    UpdateStoreStatsLocked();
    return false;
  }

  // The completion record itself counts against the logical entry limit.
  // Refuse completion if adding it displaced an expected tile.
  for (const auto& tile : expected_tiles)
    if (!store_.Contains(TileKey(tile.first, tile.second))) {
      std::string erase_error;
      store_.Erase(kAtlasCompletionKey, &erase_error);
      last_error_ = "atlas completion exceeded persistent cache capacity";
      UpdateStoreStatsLocked();
      return false;
    }
  UpdateStoreStatsLocked();
  return true;
}

bool ChartSafetyCache::ClearAtlasCompletion() {
  std::lock_guard<std::mutex> lock(mutex_);
  if ((!store_open_ && !OpenStoreLocked()) || !persistent_enabled_ ||
      !identity_confirmed_)
    return false;
  const bool result = store_.Erase(kAtlasCompletionKey, &last_error_);
  UpdateStoreStatsLocked();
  return result;
}

int ChartSafetyCache::LookupCallback(void* context, long lat_tile,
                                     long lon_tile, int require_depth,
                                     PlugInSegmentSafetyTile* tile) {
  ChartSafetyCache* cache = static_cast<ChartSafetyCache*>(context);
  return cache && cache->Lookup(lat_tile, lon_tile, require_depth != 0, tile)
             ? 1
             : 0;
}

void ChartSafetyCache::StoreCallback(void* context,
                                     const PlugInSegmentSafetyTile* tile) {
  ChartSafetyCache* cache = static_cast<ChartSafetyCache*>(context);
  if (cache) cache->Store(tile);
}

void ChartSafetyCache::IdentityCallback(void* context, const char* identity) {
  ChartSafetyCache* cache = static_cast<ChartSafetyCache*>(context);
  if (cache) cache->SetProvisionalIdentity(identity ? identity : "");
}

}  // namespace weather_routing
