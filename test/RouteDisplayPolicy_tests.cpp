#include <gtest/gtest.h>

#include "RouteDisplayPolicy.h"

TEST(RouteDisplayPolicy, PublishesEtaOnlyForACompletedDestinationRoute) {
  EXPECT_FALSE(
      weather_routing::HasPublishableRouteResult(false, false, false));
  EXPECT_FALSE(
      weather_routing::HasPublishableRouteResult(false, false, true));
  EXPECT_FALSE(
      weather_routing::HasPublishableRouteResult(false, true, true));
  EXPECT_FALSE(
      weather_routing::HasPublishableRouteResult(true, false, true));
  EXPECT_FALSE(
      weather_routing::HasPublishableRouteResult(true, true, false));
  EXPECT_TRUE(
      weather_routing::HasPublishableRouteResult(true, true, true));
}
