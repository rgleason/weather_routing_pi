// SPDX-License-Identifier: GPL-3.0-or-later
#include <gtest/gtest.h>
#include "RouteWaypointValidation.h"
#include <limits>
namespace wr = weather_routing;
namespace {
RouteWaypointInfo P(const char* name, double lat, double lon, int i) {
  return {wxString::Format("guid-%d", i), name, lat, lon, i};
}
TEST(RouteWaypointValidation, RicksDifferentWaypointIdsIdentifyTheZeroLengthLeg) {
  auto issue = wr::CheckConsecutiveRouteWaypoints({P("001",53.257286,.723685,1),
      P("002",59.494748,-2.432523,2), P("003",59.494748,-2.432523,3),
      P("004",59.336864,-6.968451,4)});
  ASSERT_TRUE(issue);
  EXPECT_EQ(issue->code, "setup.coincident_waypoints");
  EXPECT_TRUE(issue->title.Contains("002"));
  EXPECT_TRUE(issue->title.Contains("003"));
  EXPECT_TRUE(issue->detail.Contains("Leg 2"));
  EXPECT_EQ(issue->severity, wr::DiagnosticSeverity::Error);
}
TEST(RouteWaypointValidation, LegitimateReturnPassagesAreAccepted) {
  EXPECT_FALSE(wr::CheckConsecutiveRouteWaypoints({P("A",50,0,1),
      P("B",51,1,2),P("A",50,0,3)}));
}
TEST(RouteWaypointValidation, NearbyDistinctPointsAreAcceptedEvenWithinAMillimeter) {
  EXPECT_FALSE(wr::PositionsCoincide(50,0,50+1e-9,0));
  EXPECT_FALSE(wr::PositionsCoincide(50,0,50,1e-8));
  EXPECT_FALSE(wr::CheckConsecutiveRouteWaypoints({P("A",50,0,1),
      P("B",50.000001,0,2)}));
}
TEST(RouteWaypointValidation, EquivalentLongitudesAndPolesAreCoincident) {
  EXPECT_TRUE(wr::PositionsCoincide(50,180,50,-180));
  EXPECT_TRUE(wr::PositionsCoincide(50,1,50,361));
  EXPECT_TRUE(wr::PositionsCoincide(90,20,90,-50));
  EXPECT_TRUE(wr::PositionsCoincide(-90,20,-90,50));
  EXPECT_FALSE(wr::PositionsCoincide(89.999,20,89.999,50));
}
TEST(RouteWaypointValidation, InvalidCoordinatesRemainForThePositionChecks) {
  EXPECT_FALSE(wr::PositionsCoincide(91,0,91,0));
  EXPECT_FALSE(wr::PositionsCoincide(50,std::numeric_limits<double>::quiet_NaN(),50,0));
}
TEST(RouteWaypointValidation, EmptyNamesStillIdentifyTheConsecutivePoints) {
  auto issue=wr::CheckRouteLegPositions(P("",50,0,2), P("",50,0,3),2);
  ASSERT_TRUE(issue);
  EXPECT_TRUE(issue->title.Contains("unnamed waypoint 2"));
  EXPECT_TRUE(issue->title.Contains("unnamed waypoint 3"));
}
}  // namespace
