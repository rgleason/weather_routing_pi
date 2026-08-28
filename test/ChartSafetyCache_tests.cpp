#include <gtest/gtest.h>

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

#include "ChartSafetyCache.h"

namespace {

class ChartSafetyCacheTest : public ::testing::Test {
protected:
  void SetUp() override {
    directory_ = std::filesystem::temp_directory_path() /
                 ("weather-routing-tile-cache-" +
                  std::to_string(reinterpret_cast<std::uintptr_t>(this)));
    std::filesystem::create_directories(directory_);
    path_ = directory_ / "tiles.cache";
    hazards_.assign(9, 0);
    has_depth_.assign(9, 1);
    depths_.assign(9, 20.0F);
    tile_.struct_size = sizeof(tile_);
    tile_.group_index = 0;
    tile_.lat_tile = 100;
    tile_.lon_tile = -20;
    tile_.resolution = 0.00125;
    tile_.rows = 3;
    tile_.cols = 3;
    tile_.chart_db_index = 7;
    tile_.chart_scale = 50000;
    tile_.source = PI_SEGMENT_SAFETY_SOURCE_VECTOR_CHART;
    tile_.depth_complete = 1;
    tile_.hazard_flags = hazards_.data();
    tile_.has_depth = has_depth_.data();
    tile_.min_depth_m = depths_.data();
    tile_.cell_capacity = 9;
  }

  void TearDown() override { std::filesystem::remove_all(directory_); }

  std::filesystem::path directory_;
  std::filesystem::path path_;
  std::vector<unsigned short> hazards_;
  std::vector<unsigned char> has_depth_;
  std::vector<float> depths_;
  PlugInSegmentSafetyTile tile_{};
};

TEST_F(ChartSafetyCacheTest, WarmRamLookupReturnsExactAuthoritativePayload) {
  weather_routing::ChartSafetyCache cache;
  cache.Configure(path_.string(), 256, true);
  cache.SetIdentity("chart-set-a");
  cache.Store(&tile_);

  std::vector<unsigned short> hazards(9);
  std::vector<unsigned char> has_depth(9);
  std::vector<float> depths(9);
  PlugInSegmentSafetyTile output = tile_;
  output.hazard_flags = hazards.data();
  output.has_depth = has_depth.data();
  output.min_depth_m = depths.data();
  ASSERT_TRUE(cache.Lookup(100, -20, true, &output));
  EXPECT_EQ(output.chart_db_index, 7);
  EXPECT_EQ(depths, depths_);
  EXPECT_EQ(cache.Stats().ram_hits, 1U);
}

TEST_F(ChartSafetyCacheTest, FlushPersistsAcrossPluginInstances) {
  {
    weather_routing::ChartSafetyCache cache;
    cache.Configure(path_.string(), 256, true);
    cache.SetIdentity("chart-set-a");
    cache.Store(&tile_);
    ASSERT_TRUE(cache.Flush());
  }
  weather_routing::ChartSafetyCache reopened;
  reopened.Configure(path_.string(), 256, true);
  reopened.SetIdentity("chart-set-a");
  std::vector<unsigned short> hazards(9);
  std::vector<unsigned char> has_depth(9);
  std::vector<float> depths(9);
  PlugInSegmentSafetyTile output = tile_;
  output.hazard_flags = hazards.data();
  output.has_depth = has_depth.data();
  output.min_depth_m = depths.data();
  ASSERT_TRUE(reopened.Lookup(100, -20, true, &output));
  EXPECT_EQ(reopened.Stats().disk_hits, 1U);
}

TEST_F(ChartSafetyCacheTest, LicensedPluginVectorTilesRemainRamOnly) {
  tile_.source = PI_SEGMENT_SAFETY_SOURCE_PLUGIN_VECTOR;
  {
    weather_routing::ChartSafetyCache cache;
    cache.Configure(path_.string(), 256, true);
    cache.SetIdentity("chart-set-a");
    cache.Store(&tile_);

    std::vector<unsigned short> hazards(9);
    std::vector<unsigned char> has_depth(9);
    std::vector<float> depths(9);
    PlugInSegmentSafetyTile output = tile_;
    output.hazard_flags = hazards.data();
    output.has_depth = has_depth.data();
    output.min_depth_m = depths.data();
    EXPECT_TRUE(cache.Lookup(100, -20, true, &output));
    EXPECT_TRUE(cache.Flush());
  }

  weather_routing::ChartSafetyCache reopened;
  reopened.Configure(path_.string(), 256, true);
  reopened.SetIdentity("chart-set-a");
  std::vector<unsigned short> hazards(9);
  std::vector<unsigned char> has_depth(9);
  std::vector<float> depths(9);
  PlugInSegmentSafetyTile output = tile_;
  output.hazard_flags = hazards.data();
  output.has_depth = has_depth.data();
  output.min_depth_m = depths.data();
  EXPECT_FALSE(reopened.Lookup(100, -20, true, &output));
}

TEST_F(ChartSafetyCacheTest, ChartIdentityChangeFailsClosed) {
  weather_routing::ChartSafetyCache cache;
  cache.Configure(path_.string(), 256, true);
  cache.SetIdentity("chart-set-a");
  cache.Store(&tile_);
  ASSERT_TRUE(cache.Flush());
  cache.SetIdentity("chart-set-b");

  std::vector<unsigned short> hazards(9);
  std::vector<unsigned char> has_depth(9);
  std::vector<float> depths(9);
  PlugInSegmentSafetyTile output = tile_;
  output.hazard_flags = hazards.data();
  output.has_depth = has_depth.data();
  output.min_depth_m = depths.data();
  EXPECT_FALSE(cache.Lookup(100, -20, true, &output));
}

TEST_F(ChartSafetyCacheTest, IncompleteDepthTileCannotSatisfyDepthRequest) {
  weather_routing::ChartSafetyCache cache;
  cache.Configure(path_.string(), 256, false);
  cache.SetIdentity("chart-set-a");
  tile_.depth_complete = 0;
  cache.Store(&tile_);

  std::vector<unsigned short> hazards(9);
  std::vector<unsigned char> has_depth(9);
  std::vector<float> depths(9);
  PlugInSegmentSafetyTile output = tile_;
  output.hazard_flags = hazards.data();
  output.has_depth = has_depth.data();
  output.min_depth_m = depths.data();
  EXPECT_FALSE(cache.Lookup(100, -20, true, &output));
  EXPECT_TRUE(cache.Lookup(100, -20, false, &output));
}

TEST_F(ChartSafetyCacheTest, AutoBudgetIsBoundedAndExplicitValueIsRemembered) {
  weather_routing::ChartSafetyCache cache;
  cache.Configure(path_.string(), 0, false);
  EXPECT_EQ(cache.RequestedRamMiB(), 0);
  EXPECT_GE(cache.EffectiveRamMiB(), 256);
  EXPECT_LE(cache.EffectiveRamMiB(), 2048);
  cache.SetRequestedRamMiB(4096);
  EXPECT_EQ(cache.RequestedRamMiB(), 4096);
  EXPECT_EQ(cache.EffectiveRamMiB(), 4096);
}

TEST_F(ChartSafetyCacheTest, ClearRemovesDiskCacheWhilePersistenceDisabled) {
  {
    weather_routing::ChartSafetyCache cache;
    cache.Configure(path_.string(), 256, true);
    cache.SetIdentity("chart-set-a");
    cache.Store(&tile_);
    ASSERT_TRUE(cache.Flush());
  }
  ASSERT_TRUE(std::filesystem::exists(path_));

  weather_routing::ChartSafetyCache cache;
  cache.Configure(path_.string(), 256, false);
  cache.SetIdentity("chart-set-a");
  ASSERT_TRUE(cache.Clear());
  EXPECT_FALSE(std::filesystem::exists(path_));
  EXPECT_EQ(cache.Stats().disk_file_bytes, 0U);
}

}  // namespace
