#include <gtest/gtest.h>

#include <cstdint>
#include <cstring>
#include <filesystem>
#include <string>
#include <vector>

#include "AppendOnlyCache.h"
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
    std::strncpy(tile_.dependency_identity, "tile-v1-test-dependency",
                 sizeof(tile_.dependency_identity) - 1);
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
  EXPECT_STREQ(output.dependency_identity, "tile-v1-test-dependency");
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
  EXPECT_STREQ(output.dependency_identity, "tile-v1-test-dependency");
  EXPECT_EQ(reopened.Stats().disk_hits, 1U);
}

TEST_F(ChartSafetyCacheTest, LicensedPluginVectorTilesPersistWhenPermitted) {
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
  ASSERT_TRUE(reopened.Lookup(100, -20, true, &output));
  EXPECT_EQ(output.source, PI_SEGMENT_SAFETY_SOURCE_PLUGIN_VECTOR);
  EXPECT_EQ(depths, depths_);
  EXPECT_EQ(reopened.Stats().disk_hits, 1U);
}

TEST_F(ChartSafetyCacheTest,
       LicensedPluginVectorTileReplacesOlderPersistentProviderForSameCell) {
  weather_routing::ChartSafetyCache cache;
  cache.Configure(path_.string(), 256, true);
  cache.SetIdentity("chart-set-a");
  cache.Store(&tile_);
  ASSERT_TRUE(cache.Flush());

  tile_.source = PI_SEGMENT_SAFETY_SOURCE_PLUGIN_VECTOR;
  cache.Store(&tile_);
  ASSERT_TRUE(cache.Flush());

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
  EXPECT_EQ(output.source, PI_SEGMENT_SAFETY_SOURCE_PLUGIN_VECTOR);
  EXPECT_EQ(reopened.Stats().disk_hits, 1U);
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

TEST_F(ChartSafetyCacheTest,
       ProvisionalStartupIdentityDoesNotDestroyFinalIdentityStore) {
  {
    weather_routing::ChartSafetyCache cache;
    cache.Configure(path_.string(), 256, true);
    cache.SetIdentity("final-chart-set");
    cache.Store(&tile_);
    ASSERT_TRUE(cache.Flush());
  }

  weather_routing::ChartSafetyCache reopened;
  reopened.Configure(path_.string(), 256, true);
  const std::uintmax_t original_size = std::filesystem::file_size(path_);
  reopened.SetProvisionalIdentity("provisional-before-chart-plugins-load");

  std::vector<unsigned short> provisional_hazards(9);
  std::vector<unsigned char> provisional_has_depth(9);
  std::vector<float> provisional_depths(9);
  PlugInSegmentSafetyTile provisional_output = tile_;
  provisional_output.hazard_flags = provisional_hazards.data();
  provisional_output.has_depth = provisional_has_depth.data();
  provisional_output.min_depth_m = provisional_depths.data();
  EXPECT_FALSE(reopened.Lookup(100, -20, true, &provisional_output));
  EXPECT_EQ(std::filesystem::file_size(path_), original_size);

  reopened.SetIdentity("final-chart-set");

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

TEST_F(ChartSafetyCacheTest,
       ProvisionalStoresAndFlushCannotReplaceFinalIdentityStore) {
  {
    weather_routing::ChartSafetyCache cache;
    cache.Configure(path_.string(), 256, true);
    cache.SetIdentity("final-chart-set");
    cache.Store(&tile_);
    ASSERT_TRUE(cache.Flush());
  }
  const std::uintmax_t original_size = std::filesystem::file_size(path_);

  {
    weather_routing::ChartSafetyCache provisional;
    provisional.Configure(path_.string(), 256, true);
    provisional.SetProvisionalIdentity("startup-chart-set");
    provisional.Store(&tile_);
    EXPECT_TRUE(provisional.Flush());
  }
  EXPECT_EQ(std::filesystem::file_size(path_), original_size);

  weather_routing::ChartSafetyCache reopened;
  reopened.Configure(path_.string(), 256, true);
  reopened.SetIdentity("final-chart-set");
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

TEST_F(ChartSafetyCacheTest, ExplicitClearWorksBeforeIdentityConfirmation) {
  {
    weather_routing::ChartSafetyCache cache;
    cache.Configure(path_.string(), 256, true);
    cache.SetIdentity("final-chart-set");
    cache.Store(&tile_);
    ASSERT_TRUE(cache.Flush());
  }
  ASSERT_TRUE(std::filesystem::exists(path_));

  weather_routing::ChartSafetyCache startup;
  startup.Configure(path_.string(), 256, true);
  startup.SetProvisionalIdentity("startup-chart-set");
  EXPECT_TRUE(startup.Clear());
  EXPECT_FALSE(std::filesystem::exists(path_));
}

TEST_F(ChartSafetyCacheTest, ProviderPriorityUpgradeInvalidatesVersionOneStore) {
  weather_routing::AppendOnlyCache legacy;
  std::string error;
  ASSERT_TRUE(legacy.Open(path_.string(),
                          "weather-routing-chart-tile-v1:chart-set-a", 16,
                          &error));
  const weather_routing::AppendOnlyCacheRecord stale = {
      "100:-20", std::vector<unsigned char>{1, 2, 3}};
  ASSERT_TRUE(legacy.PutBatch({stale}, &error));

  weather_routing::ChartSafetyCache upgraded;
  upgraded.Configure(path_.string(), 256, true);
  upgraded.SetIdentity("chart-set-a");
  std::vector<unsigned short> hazards(9);
  std::vector<unsigned char> has_depth(9);
  std::vector<float> depths(9);
  PlugInSegmentSafetyTile output = tile_;
  output.hazard_flags = hazards.data();
  output.has_depth = has_depth.data();
  output.min_depth_m = depths.data();
  EXPECT_FALSE(upgraded.Lookup(100, -20, true, &output));
  EXPECT_EQ(upgraded.Stats().disk_entries, 0U);
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
