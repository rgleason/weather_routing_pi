/***************************************************************************
 * Copyright (C) 2026 OpenCPN contributors
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 2 of the License, or (at your option)
 * any later version.
 ***************************************************************************/

#include "ChartSafetyHost.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <cstdlib>
#include <cstdint>
#include <memory>
#include <set>
#include <utility>
#include <vector>

#include <wx/log.h>

#include "ChartHazardEvaluator.h"
#include "ChartSafetyCache.h"
#include "georef.h"

#ifdef _WIN32
#include <windows.h>
#else
#include <dlfcn.h>
#endif

namespace {

void* ResolveHostSymbol(const char* name) {
#ifdef _WIN32
  HMODULE process = GetModuleHandle(nullptr);
  return process ? reinterpret_cast<void*>(GetProcAddress(process, name))
                 : nullptr;
#else
  return dlsym(RTLD_DEFAULT, name);
#endif
}

template <typename T>
T Resolve(const char* name) {
  return reinterpret_cast<T>(ResolveHostSymbol(name));
}

using RegisterFn = bool (*)(
    const PlugInSegmentSafetyTileCacheCallbacks*);
using IdentityFn = bool (*)(char*, int);
using CheckFn = bool (*)(double, double, double, double,
                         const PlugInSegmentSafetyOptions*,
                         PlugInSegmentSafetyResult*);
using RawTilesFn = bool (*)(const long*, const long*, int, int,
                            PlugInSegmentSafetyResult*);
using SnapshotFn = bool (*)(double, double, double, double, int, int,
                            const PlugInSegmentSafetyOptions*,
                            PlugInSegmentSafetyResult*);
using ServiceFn = bool (*)(int, int,
                           PlugInSegmentSafetyRequestServiceResult*);
using ReleaseFn = void (*)();
using SetPersistentFn = bool (*)(int);
using SavePersistentFn = bool (*)();
using ClearPersistentFn = bool (*)();

struct HostFunctions {
  RegisterFn register_cache{nullptr};
  IdentityFn get_identity{nullptr};
  CheckFn check{nullptr};
  RawTilesFn raw_tiles{nullptr};
  SnapshotFn snapshot{nullptr};
  ServiceFn service{nullptr};
  ReleaseFn release{nullptr};
  SetPersistentFn set_persistent{nullptr};
  SavePersistentFn save_persistent{nullptr};
  ClearPersistentFn clear_persistent{nullptr};
  bool available{false};
  bool registered{false};
};

HostFunctions g_host;
weather_routing::ChartSafetyCache* g_cache = nullptr;
std::unique_ptr<weather_routing::ChartHazardEvaluator> g_evaluator;
std::atomic<const std::atomic_bool*> g_prewarm_cancellation{nullptr};

bool ConfirmHostIdentity() {
  if (!g_cache || !g_host.get_identity) return false;
  char identity[4096] = {};
  if (!g_host.get_identity(identity, sizeof(identity)) || !identity[0])
    return false;
  g_cache->SetIdentity(identity);
  return true;
}

constexpr double kRawTileDegrees = 0.05;
constexpr double kRawResolutionDegrees = 0.00125;
constexpr int kRawCellsPerTile = 40;
constexpr double kPi = 3.14159265358979323846;

double NormalizeBearing(double bearing) {
  bearing = std::fmod(bearing, 360.0);
  return bearing < 0.0 ? bearing + 360.0 : bearing;
}

std::pair<long, long> TileAt(double lat, double lon) {
  return {static_cast<long>(std::floor(lat / kRawTileDegrees)),
          static_cast<long>(std::floor(lon / kRawTileDegrees))};
}

void AddTileHalo(long lat_tile, long lon_tile, int radius,
                 std::set<std::pair<long, long>>* tiles) {
  for (int dlat = -radius; dlat <= radius; ++dlat)
    for (int dlon = -radius; dlon <= radius; ++dlon)
      tiles->insert({lat_tile + dlat, lon_tile + dlon});
}

void AddCorridorTiles(double lat1, double lon1, double lat2, double lon2,
                      double corridor_margin_nm,
                      std::set<std::pair<long, long>>* tiles) {
  if (!tiles || !std::isfinite(lat1) || !std::isfinite(lon1) ||
      !std::isfinite(lat2) || !std::isfinite(lon2))
    return;

  double bearing = 0.0;
  double distance_nm = 0.0;
  ll_gc_ll_reverse(lat1, lon1, lat2, lon2, &bearing, &distance_nm);
  if (!std::isfinite(distance_nm) || distance_nm < 0.0) return;

  if (corridor_margin_nm <= 0.0) {
    long y = std::lround(lat1 / kRawResolutionDegrees);
    long x = std::lround(lon1 / kRawResolutionDegrees);
    const long y1 = std::lround(lat2 / kRawResolutionDegrees);
    const long x1 = std::lround(lon2 / kRawResolutionDegrees);
    const long dx = std::labs(x1 - x);
    const long dy = std::labs(y1 - y);
    const long sx = x < x1 ? 1 : -1;
    const long sy = y < y1 ? 1 : -1;
    long error = dx - dy;
    for (;;) {
      tiles->insert(TileAt(y * kRawResolutionDegrees,
                           x * kRawResolutionDegrees));
      if (x == x1 && y == y1) break;
      const long twice_error = 2 * error;
      if (twice_error > -dy) {
        error -= dy;
        x += sx;
      }
      if (twice_error < dx) {
        error += dx;
        y += sy;
      }
    }
    return;
  }

  const int samples = std::max(
      2, std::min(1024, static_cast<int>(std::ceil(distance_nm / 1.5)) + 1));
  const int offset_count =
      static_cast<int>(std::ceil(2.0 * corridor_margin_nm / 2.0));
  for (int offset_index = 0; offset_index <= offset_count; ++offset_index) {
    const double offset_nm =
        offset_count == 0
            ? 0.0
            : -corridor_margin_nm +
                  2.0 * corridor_margin_nm * offset_index / offset_count;
    for (int sample = 0; sample < samples; ++sample) {
      const double sample_distance =
          distance_nm * sample / static_cast<double>(samples - 1);
      double lat = lat1;
      double lon = lon1;
      if (sample_distance > 0.0)
        ll_gc_ll(lat1, lon1, bearing, sample_distance, &lat, &lon);
      if (std::abs(offset_nm) > 0.0) {
        double offset_lat = lat;
        double offset_lon = lon;
        ll_gc_ll(lat, lon,
                 NormalizeBearing(
                     bearing + (offset_nm < 0.0 ? -90.0 : 90.0)),
                 std::abs(offset_nm), &offset_lat, &offset_lon);
        lat = offset_lat;
        lon = offset_lon;
      }
      tiles->insert(TileAt(lat, lon));
    }
  }

  constexpr int kRadialBearings = 24;
  const int radial_steps =
      std::max(1, static_cast<int>(std::ceil(corridor_margin_nm / 2.0)));
  const double endpoint_lats[2] = {lat1, lat2};
  const double endpoint_lons[2] = {lon1, lon2};
  for (int endpoint = 0; endpoint < 2; ++endpoint) {
    for (int radial_step = 0; radial_step <= radial_steps; ++radial_step) {
      const double radius_nm =
          corridor_margin_nm * radial_step / radial_steps;
      for (int direction = 0; direction < kRadialBearings; ++direction) {
        double lat = endpoint_lats[endpoint];
        double lon = endpoint_lons[endpoint];
        if (radius_nm > 0.0)
          ll_gc_ll(endpoint_lats[endpoint], endpoint_lons[endpoint],
                   360.0 * direction / kRadialBearings, radius_nm, &lat,
                   &lon);
        tiles->insert(TileAt(lat, lon));
      }
    }
  }
}

double NormalizeLongitude(double longitude) {
  longitude = std::fmod(longitude + 180.0, 360.0);
  if (longitude < 0.0) longitude += 360.0;
  return longitude - 180.0;
}

bool RequestRawTiles(const std::set<std::pair<long, long>>& raw_tiles,
                     const PlugInSegmentSafetyOptions* options,
                     PlugInSegmentSafetyResult* result) {
  if (raw_tiles.empty() || !options || !ConfirmHostIdentity()) return false;

  std::vector<long> lat_tiles;
  std::vector<long> lon_tiles;
  lat_tiles.reserve(raw_tiles.size());
  lon_tiles.reserve(raw_tiles.size());
  for (const auto& tile : raw_tiles) {
    lat_tiles.push_back(tile.first);
    lon_tiles.push_back(tile.second);
  }
  const auto* cancellation = g_prewarm_cancellation.load(
      std::memory_order_acquire);
  if (!cancellation)
    return g_host.raw_tiles(lat_tiles.data(), lon_tiles.data(),
                            static_cast<int>(lat_tiles.size()),
                            options->check_depth != 0 ? 1 : 0, result);

  constexpr std::size_t kExternalPrewarmBatchTiles = 16;
  PlugInSegmentSafetyResult batch_result = {};
  batch_result.struct_size = sizeof(batch_result);
  for (std::size_t offset = 0; offset < lat_tiles.size();
       offset += kExternalPrewarmBatchTiles) {
    if (cancellation->load(std::memory_order_relaxed)) return false;
    const std::size_t count =
        std::min(kExternalPrewarmBatchTiles, lat_tiles.size() - offset);
    if (!g_host.raw_tiles(lat_tiles.data() + offset, lon_tiles.data() + offset,
                          static_cast<int>(count),
                          options->check_depth != 0 ? 1 : 0, &batch_result))
      return false;
  }
  if (result) {
    *result = batch_result;
    result->prewarm_requested_tiles = static_cast<int>(lat_tiles.size());
  }
  return !cancellation->load(std::memory_order_relaxed);
}

bool PrewarmRawTiles(
    const double* latitudes, const double* longitudes,
    const int* point_counts, int polyline_count, double corridor_margin_nm,
    int fine_tile_halo, const PlugInSegmentSafetyOptions* options,
    PlugInSegmentSafetyResult* result) {
  if (!g_host.available || !g_host.raw_tiles || !latitudes || !longitudes ||
      !point_counts || polyline_count <= 0 || !options)
    return false;

  std::set<std::pair<long, long>> route_tiles;
  int point_offset = 0;
  for (int polyline = 0; polyline < polyline_count; ++polyline) {
    const int count = point_counts[polyline];
    if (count < 0 || count > 1000000 - point_offset) return false;
    if (count == 1) {
      AddCorridorTiles(latitudes[point_offset], longitudes[point_offset],
                       latitudes[point_offset], longitudes[point_offset],
                       std::max(0.0, corridor_margin_nm), &route_tiles);
    } else {
      for (int point = 1; point < count; ++point)
        AddCorridorTiles(latitudes[point_offset + point - 1],
                         longitudes[point_offset + point - 1],
                         latitudes[point_offset + point],
                         longitudes[point_offset + point],
                         std::max(0.0, corridor_margin_nm), &route_tiles);
    }
    point_offset += count;
  }
  if (route_tiles.empty()) return false;

  fine_tile_halo = std::clamp(fine_tile_halo, 0, 8);
  if (fine_tile_halo > 0) {
    const auto exact = route_tiles;
    for (const auto& tile : exact)
      AddTileHalo(tile.first, tile.second, fine_tile_halo, &route_tiles);
  }

  std::set<std::pair<long, long>> raw_tiles;
  for (const auto& tile : route_tiles) {
    const double mid_lat =
        tile.first * kRawTileDegrees + kRawTileDegrees / 2.0;
    const double cell_nm =
        std::min(kRawResolutionDegrees * 60.0,
                 kRawResolutionDegrees * 60.0 *
                     std::max(0.1, std::abs(std::cos(mid_lat * kPi / 180.0))));
    int cell_halo =
        options->safety_margin_nm > 0.0
            ? static_cast<int>(std::ceil(options->safety_margin_nm /
                                         std::max(0.01, cell_nm)))
            : 0;
    cell_halo = std::min(cell_halo, 128);
    const int margin_tile_halo =
        (cell_halo + kRawCellsPerTile - 1) / kRawCellsPerTile;
    AddTileHalo(tile.first, tile.second, margin_tile_halo, &raw_tiles);
  }

  return RequestRawTiles(raw_tiles, options, result);
}

}  // namespace

namespace weather_routing {
namespace chart_safety_host {

bool Initialize(ChartSafetyCache* cache) {
  Shutdown();
  g_cache = cache;
  g_host.register_cache = Resolve<RegisterFn>(
      "PlugIn_RegisterSegmentSafetyTileCache");
  g_host.get_identity = Resolve<IdentityFn>(
      "PlugIn_GetSegmentSafetyChartIdentity");
  g_host.check = Resolve<CheckFn>("PlugIn_CheckSegmentSafety");
  g_host.raw_tiles = Resolve<RawTilesFn>(
      "PlugIn_PrewarmSegmentSafetyRawTiles");
  g_host.snapshot = Resolve<SnapshotFn>(
      "PlugIn_PrewarmSegmentSafetyHazardSnapshot");
  g_host.service = Resolve<ServiceFn>(
      "PlugIn_ServicePendingSegmentSafetyRequests");
  g_host.release = Resolve<ReleaseFn>(
      "PlugIn_ReleaseSegmentSafetyRouteMaskPins");
  g_host.set_persistent = Resolve<SetPersistentFn>(
      "PlugIn_SetSegmentSafetyPersistentCacheEnabled");
  g_host.save_persistent = Resolve<SavePersistentFn>(
      "PlugIn_SaveSegmentSafetyPersistentCache");
  g_host.clear_persistent = Resolve<ClearPersistentFn>(
      "PlugIn_ClearSegmentSafetyPersistentCache");

  g_host.available =
      cache && g_host.register_cache && g_host.get_identity && g_host.check &&
      g_host.raw_tiles && g_host.snapshot && g_host.service && g_host.release;
  if (!g_host.available) return false;

  PlugInSegmentSafetyTileCacheCallbacks callbacks = {};
  callbacks.struct_size = sizeof(callbacks);
  callbacks.context = cache;
  callbacks.lookup = &ChartSafetyCache::LookupCallback;
  callbacks.store = &ChartSafetyCache::StoreCallback;
  callbacks.identity_changed = &ChartSafetyCache::IdentityCallback;
  if (!g_host.register_cache(&callbacks)) {
    g_host.available = false;
    return false;
  }
  g_host.registered = true;

  // Registration immediately reports the core's current identity. Treat it
  // as provisional because later-loaded chart providers (notably o-charts)
  // are not necessarily registered yet. The first main-thread prewarm
  // confirms the post-load identity before persistent storage is touched.
  g_evaluator =
      std::make_unique<weather_routing::ChartHazardEvaluator>(*cache);
  return true;
}

void Shutdown() {
  g_evaluator.reset();
  if (g_host.registered && g_host.register_cache)
    g_host.register_cache(nullptr);
  g_host = HostFunctions();
  g_cache = nullptr;
}

bool Available() { return g_host.available; }

std::string Status() {
  return Available()
             ? "Full chart-aware safety available; plugin RAM and disk cache "
               "active."
             : "Enhanced chart-safety host capability is unavailable.";
}

bool FlushCache() {
  bool plugin_ok = true;
  if (g_cache) {
    plugin_ok = g_cache->Flush(false);
    const ChartSafetyCacheStats stats = g_cache->Stats();
    wxLogMessage(
        "WR_PLUGIN_CHART_CACHE flush_ok=%d ram_hits=%llu disk_hits=%llu "
        "misses=%llu stores=%llu evictions=%llu ram_entries=%llu "
        "ram_mib=%.1f budget_mib=%.1f disk_entries=%llu "
        "disk_read_mib=%.1f disk_written_mib=%.1f disk_file_mib=%.1f "
        "dirty=%llu flushes=%llu",
        plugin_ok ? 1 : 0, static_cast<unsigned long long>(stats.ram_hits),
        static_cast<unsigned long long>(stats.disk_hits),
        static_cast<unsigned long long>(stats.misses),
        static_cast<unsigned long long>(stats.stores),
        static_cast<unsigned long long>(stats.evictions),
        static_cast<unsigned long long>(stats.ram_entries),
        stats.ram_bytes / (1024.0 * 1024.0),
        stats.ram_budget_bytes / (1024.0 * 1024.0),
        static_cast<unsigned long long>(stats.disk_entries),
        stats.disk_bytes_read / (1024.0 * 1024.0),
        stats.disk_bytes_written / (1024.0 * 1024.0),
        stats.disk_file_bytes / (1024.0 * 1024.0),
        static_cast<unsigned long long>(stats.dirty_entries),
        static_cast<unsigned long long>(stats.flushes));
  }
  const bool host_ok = !g_host.save_persistent || g_host.save_persistent();
  return plugin_ok && host_ok;
}

bool SetPersistentCacheEnabled(bool enabled) {
  return !g_host.set_persistent || g_host.set_persistent(enabled ? 1 : 0);
}

bool SavePersistentCache() {
  return !g_host.save_persistent || g_host.save_persistent();
}

bool ClearPersistentCache() {
  return !g_host.clear_persistent || g_host.clear_persistent();
}

void InvalidateDerivedMasks() {
  if (g_evaluator) g_evaluator->ClearDerivedMasks();
}

void SetPrewarmCancellationFlag(const std::atomic_bool* flag) {
  g_prewarm_cancellation.store(flag, std::memory_order_release);
}

bool PrewarmCancellationRequested() {
  const auto* flag =
      g_prewarm_cancellation.load(std::memory_order_acquire);
  return flag && flag->load(std::memory_order_relaxed);
}

bool CheckSegment(double lat1, double lon1, double lat2, double lon2,
                  const PlugInSegmentSafetyOptions* options,
                  PlugInSegmentSafetyResult* result) {
  if (g_evaluator && options && result &&
      g_evaluator->CheckSegment(lat1, lon1, lat2, lon2, *options, result))
    return true;
  return g_host.available &&
         g_host.check(lat1, lon1, lat2, lon2, options, result);
}

bool PrewarmHazardSnapshot(double min_lat, double min_lon, double max_lat,
                           double max_lon, int enable_fast_path,
                           int shadow_compare,
                           const PlugInSegmentSafetyOptions* options,
                           PlugInSegmentSafetyResult* result) {
  return g_host.available && ConfirmHostIdentity() &&
         g_host.snapshot(min_lat, min_lon, max_lat, max_lon, enable_fast_path,
                         shadow_compare, options, result);
}

bool PrewarmRouteMaskForSegment(
    double lat1, double lon1, double lat2, double lon2,
    double corridor_margin_nm, const PlugInSegmentSafetyOptions* options,
    PlugInSegmentSafetyResult* result) {
  const double latitudes[2] = {lat1, lat2};
  const double longitudes[2] = {lon1, lon2};
  const int point_count = 2;
  return PrewarmRawTiles(latitudes, longitudes, &point_count, 1,
                         corridor_margin_nm, 0, options, result);
}

bool PrewarmReachabilityEnvelope(
    double start_lat, double start_lon, double end_lat, double end_lon,
    double maximum_path_length_nm,
    const PlugInSegmentSafetyOptions* options,
    PlugInSegmentSafetyResult* result) {
  if (!g_host.available || !g_host.raw_tiles || !options ||
      !std::isfinite(start_lat) || !std::isfinite(start_lon) ||
      !std::isfinite(end_lat) || !std::isfinite(end_lon) ||
      !std::isfinite(maximum_path_length_nm))
    return false;

  double bearing = 0.0;
  double direct_distance_nm = 0.0;
  ll_gc_ll_reverse(start_lat, start_lon, end_lat, end_lon, &bearing,
                   &direct_distance_nm);
  if (!std::isfinite(direct_distance_nm) || direct_distance_nm < 0.0 ||
      maximum_path_length_nm + 1e-6 < direct_distance_nm)
    return false;

  // Work in an unwrapped longitude interval so passages near the date line
  // do not accidentally request almost the whole world.
  start_lon = NormalizeLongitude(start_lon);
  end_lon = NormalizeLongitude(end_lon);
  while (end_lon - start_lon > 180.0) end_lon -= 360.0;
  while (end_lon - start_lon < -180.0) end_lon += 360.0;

  const double safety_margin_nm = std::max(0.0, options->safety_margin_nm);
  const double semi_major_nm = maximum_path_length_nm * 0.5;
  const double latitude_radius_deg =
      (semi_major_nm + safety_margin_nm + 3.0) / 60.0;
  const double min_lat =
      std::max(-89.95, std::min(start_lat, end_lat) - latitude_radius_deg);
  const double max_lat =
      std::min(89.95, std::max(start_lat, end_lat) + latitude_radius_deg);
  const double extreme_lat =
      std::min(89.0, std::max(std::abs(min_lat), std::abs(max_lat)));
  const double longitude_radius_deg =
      latitude_radius_deg /
      std::max(0.05, std::cos(extreme_lat * kPi / 180.0));
  const double min_lon = std::min(start_lon, end_lon) - longitude_radius_deg;
  const double max_lon = std::max(start_lon, end_lon) + longitude_radius_deg;

  const long min_lat_tile =
      static_cast<long>(std::floor(min_lat / kRawTileDegrees));
  const long max_lat_tile =
      static_cast<long>(std::floor(max_lat / kRawTileDegrees));
  const long min_lon_tile =
      static_cast<long>(std::floor(min_lon / kRawTileDegrees));
  const long max_lon_tile =
      static_cast<long>(std::floor(max_lon / kRawTileDegrees));
  const std::uint64_t candidate_count =
      static_cast<std::uint64_t>(max_lat_tile - min_lat_tile + 1) *
      static_cast<std::uint64_t>(max_lon_tile - min_lon_tile + 1);
  constexpr std::uint64_t kMaximumEnvelopeCandidates = 2000000;
  constexpr std::size_t kMaximumEnvelopeTiles = 100000;
  if (candidate_count == 0 || candidate_count > kMaximumEnvelopeCandidates)
    return false;

  std::set<std::pair<long, long>> tiles;
  for (long lat_tile = min_lat_tile; lat_tile <= max_lat_tile; ++lat_tile) {
    const double centre_lat =
        lat_tile * kRawTileDegrees + kRawTileDegrees * 0.5;
    const double half_height_nm = kRawTileDegrees * 30.0;
    const double half_width_nm =
        half_height_nm *
        std::max(0.0, std::cos(centre_lat * kPi / 180.0));
    const double half_diagonal_nm =
        std::hypot(half_height_nm, half_width_nm);
    const double inclusion_limit_nm = maximum_path_length_nm +
                                      2.0 * safety_margin_nm +
                                      2.0 * half_diagonal_nm;
    for (long unwrapped_lon_tile = min_lon_tile;
         unwrapped_lon_tile <= max_lon_tile; ++unwrapped_lon_tile) {
      const double centre_lon = NormalizeLongitude(
          unwrapped_lon_tile * kRawTileDegrees + kRawTileDegrees * 0.5);
      double unused = 0.0;
      double from_start_nm = 0.0;
      double to_end_nm = 0.0;
      ll_gc_ll_reverse(start_lat, start_lon, centre_lat, centre_lon, &unused,
                       &from_start_nm);
      ll_gc_ll_reverse(centre_lat, centre_lon, end_lat,
                       NormalizeLongitude(end_lon), &unused, &to_end_nm);
      if (!std::isfinite(from_start_nm) || !std::isfinite(to_end_nm) ||
          from_start_nm + to_end_nm > inclusion_limit_nm)
        continue;
      const long canonical_lon_tile = static_cast<long>(
          std::floor(centre_lon / kRawTileDegrees));
      tiles.insert({lat_tile, canonical_lon_tile});
      if (tiles.size() > kMaximumEnvelopeTiles) return false;
    }
  }
  return RequestRawTiles(tiles, options, result);
}

bool PrewarmRouteMaskForPolylinesWithTileHalo(
    const double* latitudes, const double* longitudes,
    const int* point_counts, int polyline_count, double corridor_margin_nm,
    int fine_tile_halo, const PlugInSegmentSafetyOptions* options,
    PlugInSegmentSafetyResult* result) {
  return PrewarmRawTiles(latitudes, longitudes, point_counts, polyline_count,
                         corridor_margin_nm, fine_tile_halo, options, result);
}

bool ServicePendingRequests(
    int max_requests, int max_milliseconds,
    PlugInSegmentSafetyRequestServiceResult* result) {
  return g_host.available &&
         g_host.service(max_requests, max_milliseconds, result);
}

void ReleaseRouteMaskPins() {
  if (g_host.available) g_host.release();
}

}  // namespace chart_safety_host
}  // namespace weather_routing
