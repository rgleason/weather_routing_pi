#include <gtest/gtest.h>

#include "RoutingQualityPolicy.h"

TEST(RoutingQualityPolicy, FinalRetainsFullConfiguredQuality) {
  const auto policy =
      weather_routing::SelectRoutingQualityPolicy(false, 5.0, 900);
  EXPECT_EQ(policy.time_step_seconds, 900);
  EXPECT_DOUBLE_EQ(policy.heading_step_degrees, 5.0);
  EXPECT_DOUBLE_EQ(policy.refined_heading_step_degrees, 2.5);
  EXPECT_EQ(policy.labels_per_cell, 10U);
  EXPECT_EQ(policy.retry_stages, 7U);
  EXPECT_TRUE(policy.preserve_route_families);
}

TEST(RoutingQualityPolicy, PreviewIsCoarseAndExplicitlySeparate) {
  const auto policy =
      weather_routing::SelectRoutingQualityPolicy(true, 5.0, 900);
  EXPECT_EQ(policy.time_step_seconds, 1800);
  EXPECT_DOUBLE_EQ(policy.heading_step_degrees, 15.0);
  EXPECT_DOUBLE_EQ(policy.refined_heading_step_degrees, 5.0);
  EXPECT_EQ(policy.labels_per_cell, 6U);
  EXPECT_EQ(policy.retry_stages, 5U);
  EXPECT_FALSE(policy.preserve_route_families);
}
