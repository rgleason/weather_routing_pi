#include <gtest/gtest.h>

#include <limits>

#include "ChartSafetyPolicy.h"

TEST(ChartSafetyPolicy, PositiveMinimumEnablesDepthAtRequestedThreshold) {
  PlugInSegmentSafetyOptions options = {};

  weather_routing::ApplyMinimumDepthPolicy(options, 5.0);

  EXPECT_EQ(options.check_depth, 1);
  EXPECT_DOUBLE_EQ(options.minimum_depth_m, 5.0);
}

TEST(ChartSafetyPolicy, ZeroNegativeAndInvalidMinimumDisableDepth) {
  const double values[] = {
      0.0, -1.0, std::numeric_limits<double>::quiet_NaN(),
      std::numeric_limits<double>::infinity()};

  for (double value : values) {
    PlugInSegmentSafetyOptions options = {};
    options.check_depth = 1;
    options.minimum_depth_m = 99.0;

    weather_routing::ApplyMinimumDepthPolicy(options, value);

    EXPECT_EQ(options.check_depth, 0);
    EXPECT_DOUBLE_EQ(options.minimum_depth_m, 0.0);
  }
}

TEST(ChartSafetyPolicy, FinalRouteRequiresAuthoritativeFineChartEvidence) {
  const PlugInSegmentSafetyOptions options =
      weather_routing::MakeFinalRouteSegmentSafetyOptions(0.4, 5.0);

  EXPECT_EQ(options.struct_size, sizeof(options));
  EXPECT_DOUBLE_EQ(options.safety_margin_nm, 0.4);
  EXPECT_EQ(options.check_land, 1);
  EXPECT_EQ(options.check_depth, 1);
  EXPECT_DOUBLE_EQ(options.minimum_depth_m, 5.0);
  EXPECT_EQ(options.allow_gshhs_fallback, 0);
  EXPECT_EQ(options.force_authoritative_fine_validation, 1);
}

TEST(ChartSafetyPolicy, ProductionSearchUsesAuthoritativeChartsImmediately) {
  EXPECT_TRUE(weather_routing::ShouldUseAuthoritativeChartSearch(
      true, true, true, false));
  EXPECT_FALSE(weather_routing::ShouldUseAuthoritativeChartSearch(
      true, true, true, true));
  EXPECT_FALSE(weather_routing::ShouldUseAuthoritativeChartSearch(
      true, true, false, false));
  EXPECT_FALSE(weather_routing::ShouldUseAuthoritativeChartSearch(
      true, false, true, false));
  EXPECT_FALSE(weather_routing::ShouldUseAuthoritativeChartSearch(
      false, true, true, false));
}

TEST(ChartSafetyPolicy, ScoutDoesNotPrewarmAuthoritativeCorridor) {
  EXPECT_FALSE(weather_routing::ShouldPrewarmAuthoritativeChartSearch(
      true, true, true, false, 0));
}

TEST(ChartSafetyPolicy, ProductionSearchPrewarmsAuthoritativeCorridorOnce) {
  EXPECT_TRUE(weather_routing::ShouldPrewarmAuthoritativeChartSearch(
      true, true, true, true, 0));
  EXPECT_FALSE(weather_routing::ShouldPrewarmAuthoritativeChartSearch(
      true, true, true, true, 1));
  EXPECT_FALSE(weather_routing::ShouldPrewarmAuthoritativeChartSearch(
      true, false, true, true, 0));
}
