#include <gtest/gtest.h>

#include <array>
#include <cstdio>
#include <future>
#include <memory>
#include <vector>

#include "ChartHazardEvaluator.h"
#include "ChartSafetyCache.h"

namespace {

constexpr int kSide = 41;
constexpr double kResolution = 0.00125;

void StoreTile(weather_routing::ChartSafetyCache& cache, long lat_tile,
               long lon_tile, int land_row = -1, int land_col = -1,
               int shallow_row = -1, int shallow_col = -1) {
  std::vector<unsigned short> hazards(kSide * kSide, 0);
  std::vector<unsigned char> has_depth(kSide * kSide, 1);
  std::vector<float> depth(kSide * kSide, 20.0f);
  if (land_row >= 0 && land_col >= 0)
    hazards[land_row * kSide + land_col] = 1u << 0;
  if (shallow_row >= 0 && shallow_col >= 0)
    depth[shallow_row * kSide + shallow_col] = 1.0f;

  PlugInSegmentSafetyTile tile = {};
  tile.struct_size = sizeof(tile);
  tile.group_index = 0;
  tile.lat_tile = lat_tile;
  tile.lon_tile = lon_tile;
  tile.resolution = kResolution;
  tile.rows = kSide;
  tile.cols = kSide;
  tile.chart_db_index = 7;
  tile.chart_scale = 20000;
  tile.source = PI_SEGMENT_SAFETY_SOURCE_VECTOR_CHART;
  tile.hazard_summary_flags =
      land_row >= 0 && land_col >= 0 ? 1u << 0 : 0;
  tile.depth_complete = 1;
  std::snprintf(tile.chart_path, sizeof(tile.chart_path), "synthetic.000");
  tile.hazard_flags = hazards.data();
  tile.has_depth = has_depth.data();
  tile.min_depth_m = depth.data();
  tile.cell_capacity = kSide * kSide;
  cache.Store(&tile);
}

PlugInSegmentSafetyOptions Options(double margin_nm = 0.0) {
  PlugInSegmentSafetyOptions options = {};
  options.struct_size = sizeof(options);
  options.check_land = 1;
  options.safety_margin_nm = margin_nm;
  return options;
}

void StoreTileHalo(weather_routing::ChartSafetyCache& cache) {
  for (long lat = -1; lat <= 1; ++lat)
    for (long lon = -1; lon <= 1; ++lon) StoreTile(cache, lat, lon);
}

TEST(ChartHazardEvaluator, AnswersFromPluginOwnedRawTiles) {
  weather_routing::ChartSafetyCache cache;
  cache.Configure("", 64, false);
  cache.SetIdentity("chart-generation-A");
  StoreTile(cache, 0, 0, 16, 16);
  weather_routing::ChartHazardEvaluator evaluator(cache);

  PlugInSegmentSafetyResult land = {};
  land.struct_size = sizeof(land);
  ASSERT_TRUE(evaluator.CheckSegment(0.02, 0.02, 0.02, 0.02, Options(),
                                     &land));
  EXPECT_EQ(land.status, PI_SEGMENT_SAFETY_CROSSES_LAND);
  EXPECT_EQ(land.source, PI_SEGMENT_SAFETY_SOURCE_VECTOR_CHART);

  PlugInSegmentSafetyResult water = {};
  water.struct_size = sizeof(water);
  ASSERT_TRUE(evaluator.CheckSegment(0.01, 0.01, 0.01, 0.01, Options(),
                                     &water));
  EXPECT_EQ(water.status, PI_SEGMENT_SAFETY_SAFE);
}

TEST(ChartHazardEvaluator, MissingRawEvidenceIsNotTreatedAsSafe) {
  weather_routing::ChartSafetyCache cache;
  cache.Configure("", 64, false);
  cache.SetIdentity("chart-generation-A");
  weather_routing::ChartHazardEvaluator evaluator(cache);
  PlugInSegmentSafetyResult result = {};
  result.struct_size = sizeof(result);

  EXPECT_FALSE(evaluator.CheckSegment(0.01, 0.01, 0.01, 0.01, Options(),
                                      &result));
}

TEST(ChartHazardEvaluator, DilatesHazardsUsingCompleteNeighbourTileEvidence) {
  weather_routing::ChartSafetyCache cache;
  cache.Configure("", 64, false);
  cache.SetIdentity("chart-generation-A");
  StoreTileHalo(cache);
  StoreTile(cache, 0, 0, 16, 16);
  weather_routing::ChartHazardEvaluator evaluator(cache);

  PlugInSegmentSafetyResult result = {};
  result.struct_size = sizeof(result);
  ASSERT_TRUE(evaluator.CheckSegment(0.0225, 0.02, 0.0225, 0.02,
                                     Options(0.1), &result));
  EXPECT_EQ(result.status, PI_SEGMENT_SAFETY_WITHIN_LAND_MARGIN);
}

TEST(ChartHazardEvaluator, AppliesMinimumDepthInsidePluginMask) {
  weather_routing::ChartSafetyCache cache;
  cache.Configure("", 64, false);
  cache.SetIdentity("chart-generation-A");
  StoreTile(cache, 0, 0, -1, -1, 16, 16);
  weather_routing::ChartHazardEvaluator evaluator(cache);

  PlugInSegmentSafetyOptions options = Options();
  options.check_depth = 1;
  options.minimum_depth_m = 2.0;
  PlugInSegmentSafetyResult result = {};
  result.struct_size = sizeof(result);
  ASSERT_TRUE(evaluator.CheckSegment(0.02, 0.02, 0.02, 0.02, options,
                                     &result));
  EXPECT_EQ(result.status, PI_SEGMENT_SAFETY_TOO_SHALLOW);
}

TEST(ChartHazardEvaluator, ChartIdentityChangeNeverReusesOldDerivedMask) {
  weather_routing::ChartSafetyCache cache;
  cache.Configure("", 64, false);
  cache.SetIdentity("chart-generation-A");
  StoreTile(cache, 0, 0);
  weather_routing::ChartHazardEvaluator evaluator(cache);

  PlugInSegmentSafetyResult first = {};
  first.struct_size = sizeof(first);
  ASSERT_TRUE(evaluator.CheckSegment(0.01, 0.01, 0.01, 0.01, Options(),
                                     &first));
  ASSERT_EQ(first.status, PI_SEGMENT_SAFETY_SAFE);

  cache.SetIdentity("chart-generation-B");
  PlugInSegmentSafetyResult second = {};
  second.struct_size = sizeof(second);
  EXPECT_FALSE(evaluator.CheckSegment(0.01, 0.01, 0.01, 0.01, Options(),
                                      &second));
}

TEST(ChartHazardEvaluator, ConcurrentReadersShareImmutableDerivedMasks) {
  weather_routing::ChartSafetyCache cache;
  cache.Configure("", 64, false);
  cache.SetIdentity("chart-generation-A");
  StoreTile(cache, 0, 0, 16, 16);
  weather_routing::ChartHazardEvaluator evaluator(cache);

  std::array<std::future<int>, 8> futures;
  for (int index = 0; index < 8; ++index) {
    futures[index] = std::async(std::launch::async, [&evaluator, index] {
      PlugInSegmentSafetyResult result = {};
      result.struct_size = sizeof(result);
      const double coordinate = index % 2 == 0 ? 0.02 : 0.01;
      if (!evaluator.CheckSegment(coordinate, coordinate, coordinate,
                                  coordinate, Options(), &result))
        return -1;
      return result.status;
    });
  }
  for (int index = 0; index < 8; ++index) {
    EXPECT_EQ(futures[index].get(), index % 2 == 0
                                        ? PI_SEGMENT_SAFETY_CROSSES_LAND
                                        : PI_SEGMENT_SAFETY_SAFE);
  }
  EXPECT_EQ(evaluator.DerivedMaskCount(), 1U);
}

}  // namespace
