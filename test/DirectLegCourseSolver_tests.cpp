#include <gtest/gtest.h>
#include "DirectLegCourseSolver.h"

TEST(DirectLegCourseSolver, ReachesBothSidesOfWindWithoutCurrent) {
  for (double bearing : {30.0, 120.0, 250.0, 359.0}) {
    double heading = 0.0;
    int calls = 0;
    EXPECT_TRUE(weather_routing::SolveDirectLegCourse(bearing, 90.0, heading,
        [&](double, double ctw, double& cog) { ++calls; cog = ctw; return true; }));
    EXPECT_EQ(1, calls);
    EXPECT_NEAR(0.0, std::remainder(90.0 + heading - bearing, 360.0), 1e-9);
  }
}

TEST(DirectLegCourseSolver, CorrectsCurrentAcrossNorth) {
  double heading = 0.0;
  double course = 0.0;
  EXPECT_TRUE(weather_routing::SolveDirectLegCourse(1.0, 350.0, heading,
      [&](double, double ctw, double& cog) {
        const double angle = ctw * std::acos(-1.0) / 180.0;
        // Six knots through water with one knot of east-going current.
        course = cog = std::atan2(6.0 * std::sin(angle) + 1.0,
                                 6.0 * std::cos(angle)) * 180.0 / std::acos(-1.0);
        return true;
      }));
  EXPECT_NEAR(1.0, course, 1e-3);
}

TEST(DirectLegCourseSolver, RejectsUnreachableOrNonfiniteCourses) {
  double heading = 0.0;
  EXPECT_FALSE(weather_routing::SolveDirectLegCourse(90.0, 0.0, heading,
      [](double, double, double& cog) { cog = 0.0; return true; }));
  EXPECT_FALSE(weather_routing::SolveDirectLegCourse(90.0, 0.0, heading,
      [](double, double, double& cog) { cog = NAN; return true; }));
  EXPECT_FALSE(weather_routing::SolveDirectLegCourse(90.0, 0.0, heading,
      [](double, double, double&) { return false; }));
}
