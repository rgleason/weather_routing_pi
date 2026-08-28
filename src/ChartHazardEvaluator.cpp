/***************************************************************************
 * Plugin-owned immutable chart-hazard evaluation for route workers.
 ***************************************************************************/

#include "ChartHazardEvaluator.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <mutex>
#include <utility>

#include "ChartSafetyCache.h"

namespace weather_routing {
namespace {

constexpr double kTileDegrees = 0.05;
constexpr double kExpectedResolutionDegrees = 0.00125;
constexpr std::uint16_t kHazardLand = 1u << 0;
constexpr std::uint16_t kHazardDrying = 1u << 1;
constexpr std::uint16_t kHazardNoChart = 1u << 4;
constexpr std::uint16_t kHazardUnknownClass = 1u << 5;

constexpr std::uint16_t kBlockLand = 1u << 0;
constexpr std::uint16_t kBlockDrying = 1u << 1;
constexpr std::uint16_t kBlockTooShallow = 1u << 2;
constexpr std::uint16_t kBlockUnknownDepth = 1u << 3;
constexpr std::uint16_t kBlockNoChart = 1u << 4;
constexpr std::uint16_t kBlockUnknownClass = 1u << 5;
constexpr std::uint16_t kBlockMargin = 1u << 6;
constexpr std::uint16_t kNeedsTile = 1u << 15;

std::uint16_t BaseFlags(const ChartHazardTile& tile, int index,
                        bool check_depth, double minimum_depth_m) {
  std::uint16_t flags = 0;
  const std::uint16_t hazards =
      index >= 0 && index < static_cast<int>(tile.hazard_flags.size())
          ? tile.hazard_flags[index]
          : kHazardNoChart;
  if (hazards & kHazardLand) flags |= kBlockLand;
  if (hazards & kHazardDrying) flags |= kBlockDrying;
  if (hazards & kHazardNoChart) flags |= kBlockNoChart;
  if (hazards & kHazardUnknownClass) flags |= kBlockUnknownClass;
  if (check_depth) {
    const bool has_depth =
        index >= 0 && index < static_cast<int>(tile.has_depth.size()) &&
        tile.has_depth[index] != 0;
    if (!has_depth) {
      if (!(flags & (kBlockLand | kBlockDrying | kBlockNoChart)))
        flags |= kBlockUnknownDepth;
    } else if (index < static_cast<int>(tile.min_depth_m.size()) &&
               tile.min_depth_m[index] < minimum_depth_m) {
      flags |= kBlockTooShallow;
    }
  }
  return flags;
}

long FloorTileIndex(long cell, int cells_per_tile) {
  return static_cast<long>(
      std::floor(static_cast<double>(cell) / cells_per_tile));
}

void SetMessage(PlugInSegmentSafetyResult* result, const char* message) {
  if (!result) return;
  std::snprintf(result->message, sizeof(result->message), "%s", message);
}

void SetHitResult(PlugInSegmentSafetyResult* result, std::uint16_t flags,
                  const auto& mask,
                  long lat_cell, long lon_cell, int sample, int samples) {
  if (!result) return;
  result->source = mask.source;
  result->diagnostic_reason = PI_SEGMENT_SAFETY_DIAG_CHART_GEOMETRY_HIT;
  result->chart_db_index = mask.chart_db_index;
  result->chart_scale = mask.chart_scale;
  std::snprintf(result->chart_path, sizeof(result->chart_path), "%s",
                mask.chart_path.c_str());
  result->hit_sample_lat = lat_cell * kExpectedResolutionDegrees;
  result->hit_sample_lon = lon_cell * kExpectedResolutionDegrees;
  result->hit_sample_index = sample;
  result->hit_sample_count = samples;
  result->grid_lookups = samples;
  result->segment_sample_count = samples;
  if (flags & kBlockLand) {
    result->status = PI_SEGMENT_SAFETY_CROSSES_LAND;
    SetMessage(result, "plugin chart-hazard mask intersects land");
  } else if (flags & kBlockDrying) {
    result->status = PI_SEGMENT_SAFETY_DRYING_AREA;
    SetMessage(result, "plugin chart-hazard mask intersects drying area");
  } else if (flags & kBlockTooShallow) {
    result->status = PI_SEGMENT_SAFETY_TOO_SHALLOW;
    SetMessage(result, "plugin chart-hazard mask is too shallow");
  } else if (flags & kBlockUnknownDepth) {
    result->status = PI_SEGMENT_SAFETY_UNKNOWN_DEPTH;
    SetMessage(result, "plugin chart-hazard mask has unknown depth");
  } else if (flags & (kBlockNoChart | kBlockUnknownClass | kNeedsTile)) {
    result->status = PI_SEGMENT_SAFETY_NO_DATA;
    result->diagnostic_reason = PI_SEGMENT_SAFETY_DIAG_NO_CANDIDATE_CHART;
    SetMessage(result, "plugin chart-hazard mask has incomplete chart data");
  } else if (flags & kBlockMargin) {
    result->status = PI_SEGMENT_SAFETY_WITHIN_LAND_MARGIN;
    SetMessage(result, "plugin chart-hazard mask is within safety margin");
  } else {
    result->status = PI_SEGMENT_SAFETY_UNSAFE_AREA;
    SetMessage(result, "plugin chart-hazard mask rejected segment");
  }
}

}  // namespace

ChartHazardEvaluator::ChartHazardEvaluator(ChartSafetyCache& cache)
    : cache_(cache) {}

std::string ChartHazardEvaluator::MaskKey(
    long lat_tile, long lon_tile,
    const PlugInSegmentSafetyOptions& options) const {
  const long margin_mm =
      std::lround(std::max(0.0, options.safety_margin_nm) * 1000.0);
  const long depth_cm =
      options.check_depth
          ? std::lround(std::max(0.0, options.minimum_depth_m) * 100.0)
          : 0;
  char key[256];
  std::snprintf(key, sizeof(key), "%s:%ld:%ld:m%ld:d%d:%ld",
                cache_.Identity().c_str(), lat_tile, lon_tile, margin_mm,
                options.check_depth ? 1 : 0, depth_cm);
  return key;
}

std::shared_ptr<const ChartHazardEvaluator::DerivedMask>
ChartHazardEvaluator::GetMask(
    long lat_tile, long lon_tile,
    const PlugInSegmentSafetyOptions& options) {
  const std::string key = MaskKey(lat_tile, lon_tile, options);
  {
    std::shared_lock<std::shared_mutex> lock(mutex_);
    const auto found = masks_.find(key);
    if (found != masks_.end()) return found->second;
  }
  const auto built = BuildMask(lat_tile, lon_tile, options);
  if (!built) return nullptr;
  std::unique_lock<std::shared_mutex> lock(mutex_);
  if (masks_.size() >= 4096) masks_.clear();
  return masks_.emplace(key, built).first->second;
}

std::shared_ptr<const ChartHazardEvaluator::DerivedMask>
ChartHazardEvaluator::BuildMask(
    long lat_tile, long lon_tile,
    const PlugInSegmentSafetyOptions& options) const {
  std::shared_ptr<const ChartHazardTile> base;
  if (!cache_.LookupSnapshot(lat_tile, lon_tile, options.check_depth != 0,
                             &base))
    return nullptr;
  if (!base || base->rows <= 0 || base->cols <= 0 ||
      std::abs(base->resolution - kExpectedResolutionDegrees) > 1e-12)
    return nullptr;

  auto mask = std::make_shared<DerivedMask>();
  mask->lat_tile = lat_tile;
  mask->lon_tile = lon_tile;
  mask->resolution = base->resolution;
  mask->rows = base->rows;
  mask->cols = base->cols;
  mask->source = base->source;
  mask->chart_db_index = base->chart_db_index;
  mask->chart_scale = base->chart_scale;
  mask->chart_path = base->chart_path;
  mask->flags.assign(static_cast<std::size_t>(mask->rows) * mask->cols,
                     kNeedsTile);

  const double mid_lat = lat_tile * kTileDegrees + kTileDegrees / 2.0;
  const double cell_nm =
      std::min(mask->resolution * 60.0,
               mask->resolution * 60.0 *
                   std::max(0.1, std::abs(std::cos(mid_lat * M_PI / 180.0))));
  int halo =
      options.safety_margin_nm > 0.0
          ? static_cast<int>(
                std::ceil(options.safety_margin_nm / std::max(0.01, cell_nm)))
          : 0;
  halo = std::min(halo, 128);

  if (halo == 0) {
    for (int row = 0; row < mask->rows; ++row)
      for (int col = 0; col < mask->cols; ++col) {
        const int index = row * mask->cols + col;
        mask->flags[index] =
            BaseFlags(*base, index, options.check_depth != 0,
                      options.minimum_depth_m);
      }
    return mask;
  }

  const int extended_rows = mask->rows + 2 * halo;
  const int extended_cols = mask->cols + 2 * halo;
  const int cells_per_tile =
      static_cast<int>(std::lround(kTileDegrees / mask->resolution));
  const long first_lat_cell =
      std::lround(lat_tile * kTileDegrees / mask->resolution) - halo;
  const long first_lon_cell =
      std::lround(lon_tile * kTileDegrees / mask->resolution) - halo;
  std::vector<std::uint16_t> extended(
      static_cast<std::size_t>(extended_rows) * extended_cols, kNeedsTile);
  std::map<std::pair<long, long>, std::shared_ptr<const ChartHazardTile>>
      source_tiles;

  for (int row = 0; row < extended_rows; ++row) {
    const long global_lat_cell = first_lat_cell + row;
    const long source_lat_tile =
        FloorTileIndex(global_lat_cell, cells_per_tile);
    const int source_row = static_cast<int>(
        global_lat_cell - source_lat_tile * cells_per_tile);
    for (int col = 0; col < extended_cols; ++col) {
      const long global_lon_cell = first_lon_cell + col;
      const long source_lon_tile =
          FloorTileIndex(global_lon_cell, cells_per_tile);
      const int source_col = static_cast<int>(
          global_lon_cell - source_lon_tile * cells_per_tile);
      const auto id = std::make_pair(source_lat_tile, source_lon_tile);
      auto found = source_tiles.find(id);
      if (found == source_tiles.end()) {
        std::shared_ptr<const ChartHazardTile> source;
        if (!cache_.LookupSnapshot(source_lat_tile, source_lon_tile,
                                   options.check_depth != 0, &source))
          return nullptr;
        found = source_tiles.emplace(id, std::move(source)).first;
      }
      const ChartHazardTile& source = *found->second;
      if (source_row < 0 || source_row >= source.rows || source_col < 0 ||
          source_col >= source.cols)
        return nullptr;
      const int source_index = source_row * source.cols + source_col;
      extended[static_cast<std::size_t>(row) * extended_cols + col] =
          BaseFlags(source, source_index, options.check_depth != 0,
                    options.minimum_depth_m);
    }
  }

  const int prefix_stride = extended_cols + 1;
  std::vector<int> hazard_prefix(
      static_cast<std::size_t>(extended_rows + 1) * prefix_stride, 0);
  std::vector<int> missing_prefix(
      static_cast<std::size_t>(extended_rows + 1) * prefix_stride, 0);
  for (int row = 0; row < extended_rows; ++row) {
    int hazards = 0;
    int missing = 0;
    for (int col = 0; col < extended_cols; ++col) {
      const std::uint16_t flags =
          extended[static_cast<std::size_t>(row) * extended_cols + col];
      hazards +=
          (flags & (kBlockLand | kBlockDrying | kBlockTooShallow)) ? 1 : 0;
      missing += (flags & kNeedsTile) ? 1 : 0;
      const std::size_t index =
          static_cast<std::size_t>(row + 1) * prefix_stride + col + 1;
      hazard_prefix[index] =
          hazard_prefix[static_cast<std::size_t>(row) * prefix_stride + col +
                        1] +
          hazards;
      missing_prefix[index] =
          missing_prefix[static_cast<std::size_t>(row) * prefix_stride + col +
                         1] +
          missing;
    }
  }
  const auto rectangle_sum = [prefix_stride](const std::vector<int>& prefix,
                                              int min_row, int min_col,
                                              int max_row, int max_col) {
    const std::size_t a =
        static_cast<std::size_t>(min_row) * prefix_stride + min_col;
    const std::size_t b =
        static_cast<std::size_t>(min_row) * prefix_stride + max_col + 1;
    const std::size_t c =
        static_cast<std::size_t>(max_row + 1) * prefix_stride + min_col;
    const std::size_t d =
        static_cast<std::size_t>(max_row + 1) * prefix_stride + max_col + 1;
    return prefix[d] - prefix[b] - prefix[c] + prefix[a];
  };

  for (int row = 0; row < mask->rows; ++row) {
    for (int col = 0; col < mask->cols; ++col) {
      const int mask_index = row * mask->cols + col;
      std::uint16_t flags =
          BaseFlags(*base, mask_index, options.check_depth != 0,
                    options.minimum_depth_m);
      const int extended_row = row + halo;
      const int extended_col = col + halo;
      const std::uint16_t centre =
          extended[static_cast<std::size_t>(extended_row) * extended_cols +
                   extended_col];
      const int neighbour_missing =
          rectangle_sum(missing_prefix, extended_row - halo,
                        extended_col - halo, extended_row + halo,
                        extended_col + halo) -
          ((centre & kNeedsTile) ? 1 : 0);
      if (neighbour_missing > 0) {
        flags |= kNeedsTile;
      } else {
        const int neighbour_hazards =
            rectangle_sum(hazard_prefix, extended_row - halo,
                          extended_col - halo, extended_row + halo,
                          extended_col + halo) -
            ((centre & (kBlockLand | kBlockDrying | kBlockTooShallow)) ? 1
                                                                       : 0);
        if (neighbour_hazards > 0) flags |= kBlockMargin;
      }
      mask->flags[mask_index] = flags;
    }
  }
  return mask;
}

bool ChartHazardEvaluator::CheckSegment(
    double lat1, double lon1, double lat2, double lon2,
    const PlugInSegmentSafetyOptions& options,
    PlugInSegmentSafetyResult* result) {
  if (!result || !options.check_land || !std::isfinite(lat1) ||
      !std::isfinite(lon1) || !std::isfinite(lat2) ||
      !std::isfinite(lon2))
    return false;

  const long y0 = std::lround(lat1 / kExpectedResolutionDegrees);
  const long x0 = std::lround(lon1 / kExpectedResolutionDegrees);
  const long y1 = std::lround(lat2 / kExpectedResolutionDegrees);
  const long x1 = std::lround(lon2 / kExpectedResolutionDegrees);
  const long dx = std::labs(x1 - x0);
  const long dy = std::labs(y1 - y0);
  const long sx = x0 < x1 ? 1 : -1;
  const long sy = y0 < y1 ? 1 : -1;
  long error = dx - dy;
  const long steps = std::max(dx, dy) + 1;
  const int cells_per_tile =
      static_cast<int>(std::lround(kTileDegrees /
                                   kExpectedResolutionDegrees));
  int first_source = PI_SEGMENT_SAFETY_SOURCE_NONE;

  long x = x0;
  long y = y0;
  long current_lat_tile = std::numeric_limits<long>::min();
  long current_lon_tile = std::numeric_limits<long>::min();
  std::shared_ptr<const DerivedMask> current_mask;
  for (long sample = 0; sample < steps; ++sample) {
    const long lat_tile = FloorTileIndex(y, cells_per_tile);
    const long lon_tile = FloorTileIndex(x, cells_per_tile);
    if (!current_mask || lat_tile != current_lat_tile ||
        lon_tile != current_lon_tile) {
      current_mask = GetMask(lat_tile, lon_tile, options);
      current_lat_tile = lat_tile;
      current_lon_tile = lon_tile;
    }
    const auto& mask = current_mask;
    if (!mask) return false;
    const int row = static_cast<int>(y - lat_tile * cells_per_tile);
    const int col = static_cast<int>(x - lon_tile * cells_per_tile);
    if (row < 0 || row >= mask->rows || col < 0 || col >= mask->cols)
      return false;
    if (first_source == PI_SEGMENT_SAFETY_SOURCE_NONE)
      first_source = mask->source;
    const std::uint16_t flags = mask->flags[row * mask->cols + col];
    if (flags != 0) {
      SetHitResult(result, flags, *mask, y, x, static_cast<int>(sample),
                   static_cast<int>(steps));
      return true;
    }
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

  result->status = PI_SEGMENT_SAFETY_SAFE;
  result->source = first_source;
  result->diagnostic_reason = PI_SEGMENT_SAFETY_DIAG_CHART_GEOMETRY_CLEAR;
  result->segment_sample_count = static_cast<int>(steps);
  result->grid_lookups = static_cast<int>(steps);
  result->water_tile_shortcuts = 1;
  SetMessage(result, "plugin-owned chart-hazard masks certify segment clear");
  return true;
}

void ChartHazardEvaluator::ClearDerivedMasks() {
  std::unique_lock<std::shared_mutex> lock(mutex_);
  masks_.clear();
}

std::size_t ChartHazardEvaluator::DerivedMaskCount() const {
  std::shared_lock<std::shared_mutex> lock(mutex_);
  return masks_.size();
}

}  // namespace weather_routing
