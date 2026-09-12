#include <gtest/gtest.h>

#include <limits>

#include "OceanPrewarmPolicy.h"

TEST(OceanPrewarmPolicy, CoastalPassagesDoNotUseOceanFan) {
  const weather_routing::OceanPrewarmPlan plan =
      weather_routing::BuildOceanPrewarmPlan(599.99);
  EXPECT_FALSE(plan.enabled);
  EXPECT_EQ(plan.corridor_count, 0);
}

TEST(OceanPrewarmPolicy, OceanFanStartsWithUsefulMinimumSpread) {
  const weather_routing::OceanPrewarmPlan plan =
      weather_routing::BuildOceanPrewarmPlan(600.0);
  ASSERT_TRUE(plan.enabled);
  EXPECT_DOUBLE_EQ(plan.outer_diversion_nm, 90.0);
  EXPECT_DOUBLE_EQ(plan.inner_diversion_nm, 45.0);
  EXPECT_DOUBLE_EQ(plan.raster_margin_nm, 6.0);
  EXPECT_EQ(plan.corridor_count, 5);
}

TEST(OceanPrewarmPolicy, SeveralThousandMilePassageScalesToFifteenPercent) {
  const weather_routing::OceanPrewarmPlan plan =
      weather_routing::BuildOceanPrewarmPlan(4000.0);
  ASSERT_TRUE(plan.enabled);
  EXPECT_DOUBLE_EQ(plan.outer_diversion_nm, 600.0);
  EXPECT_DOUBLE_EQ(plan.inner_diversion_nm, 300.0);
  EXPECT_DOUBLE_EQ(plan.raster_margin_nm, 8.0);
  EXPECT_EQ(plan.corridor_count, 5);
}

TEST(OceanPrewarmPolicy, ExtremePassageRemainsSparseAndBounded) {
  const weather_routing::OceanPrewarmPlan plan =
      weather_routing::BuildOceanPrewarmPlan(10000.0);
  ASSERT_TRUE(plan.enabled);
  EXPECT_DOUBLE_EQ(plan.outer_diversion_nm, 900.0);
  EXPECT_DOUBLE_EQ(plan.inner_diversion_nm, 450.0);
  EXPECT_DOUBLE_EQ(plan.raster_margin_nm, 12.0);
  EXPECT_EQ(plan.corridor_count, 5);
}

TEST(OceanPrewarmPolicy, InvalidDistanceCannotTriggerPrefetch) {
  EXPECT_FALSE(weather_routing::BuildOceanPrewarmPlan(
                   std::numeric_limits<double>::quiet_NaN())
                   .enabled);
  EXPECT_FALSE(weather_routing::BuildOceanPrewarmPlan(-1.0).enabled);
}
