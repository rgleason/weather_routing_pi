#include <gtest/gtest.h>

#include <cmath>
#include <limits>

#include "ReachabilityPrewarmPolicy.h"

TEST(ReachabilityPrewarmPolicy, HolyheadScalePassageGetsBroadFilledEnvelope) {
  const weather_routing::ReachabilityPrewarmPlan plan =
      weather_routing::BuildReachabilityPrewarmPlan(137.0);
  ASSERT_TRUE(plan.enabled);
  EXPECT_NEAR(plan.maximum_cross_track_nm, 61.65, 1e-9);
  EXPECT_NEAR(plan.maximum_path_length_nm,
              2.0 * std::hypot(68.5, 61.65), 1e-9);
  EXPECT_GT(plan.maximum_path_length_nm, plan.direct_distance_nm);
}

TEST(ReachabilityPrewarmPolicy, MediumPassageCrossTrackExtentIsCapped) {
  const weather_routing::ReachabilityPrewarmPlan plan =
      weather_routing::BuildReachabilityPrewarmPlan(300.0);
  ASSERT_TRUE(plan.enabled);
  EXPECT_DOUBLE_EQ(plan.maximum_cross_track_nm, 75.0);
  EXPECT_NEAR(plan.maximum_path_length_nm, 2.0 * std::hypot(150.0, 75.0),
              1e-9);
}

TEST(ReachabilityPrewarmPolicy, TinyPassageStillCoversLocalAlternatives) {
  const weather_routing::ReachabilityPrewarmPlan plan =
      weather_routing::BuildReachabilityPrewarmPlan(2.0);
  ASSERT_TRUE(plan.enabled);
  EXPECT_DOUBLE_EQ(plan.maximum_cross_track_nm, 12.0);
}

TEST(ReachabilityPrewarmPolicy, OceanAndInvalidPassagesStaySparse) {
  EXPECT_FALSE(
      weather_routing::BuildReachabilityPrewarmPlan(600.0).enabled);
  EXPECT_FALSE(weather_routing::BuildReachabilityPrewarmPlan(
                   std::numeric_limits<double>::quiet_NaN())
                   .enabled);
  EXPECT_FALSE(
      weather_routing::BuildReachabilityPrewarmPlan(-1.0).enabled);
}
