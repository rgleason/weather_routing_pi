#include <gtest/gtest.h>

#include "RoutingFootprint.h"

TEST(RoutingFootprint, RemovesRepeatedTraceLineagesAndReverseEdges) {
  const RouteMapFrontierSegment ab{53.0, -6.0, 53.1, -5.9};
  const RouteMapFrontierSegment ba{53.1, -5.9, 53.0, -6.0};
  const RouteMapFrontierSegment bc{53.1, -5.9, 53.2, -5.8};
  const auto result = weather_routing_engine::DeduplicateRoutingFootprint(
      {ab, ab, ba, bc, ab, bc});

  ASSERT_EQ(result.size(), 2U);
  EXPECT_DOUBLE_EQ(result[0].lat1, ab.lat1);
  EXPECT_DOUBLE_EQ(result[1].lat2, bc.lat2);
}

TEST(RoutingFootprint, DropsInvalidAndZeroLengthEdges) {
  const double nan = std::numeric_limits<double>::quiet_NaN();
  const auto result = weather_routing_engine::DeduplicateRoutingFootprint(
      {{53.0, -6.0, 53.0, -6.0},
       {nan, -6.0, 53.1, -5.9},
       {53.0, -6.0, 53.1, -5.9}});
  ASSERT_EQ(result.size(), 1U);
}
