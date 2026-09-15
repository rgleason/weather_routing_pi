#include <gtest/gtest.h>

#include "RouteDisplayPolicy.h"

TEST(RouteDisplayPolicy, RejectsPreflightAndScoutResultsWithoutLosingRunningState) {
  using weather_routing::ClassifyRouteOutcome;
  using weather_routing::RouteOutcome;
  EXPECT_EQ(ClassifyRouteOutcome(false, false, false, false, true),
            RouteOutcome::Failed);
  EXPECT_EQ(ClassifyRouteOutcome(false, false, true, true, true),
            RouteOutcome::Failed);
  EXPECT_EQ(ClassifyRouteOutcome(false, true, false, true, true),
            RouteOutcome::Failed);
  EXPECT_EQ(ClassifyRouteOutcome(true, true, false, false, false),
            RouteOutcome::Running);
  EXPECT_EQ(ClassifyRouteOutcome(false, true, false, false, false),
            RouteOutcome::Incomplete);
  EXPECT_EQ(ClassifyRouteOutcome(false, true, true, false, false),
            RouteOutcome::Failed);
  EXPECT_EQ(ClassifyRouteOutcome(false, true, true, true, false),
            RouteOutcome::Complete);
}

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
