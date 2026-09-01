#include <wx/wx.h>

#include <gtest/gtest.h>

#include "RouteSimplifier.h"

namespace {

PlotData MakePlotData(double lat, double lon, int minute) {
  PlotData point = PlotData();
  point.lat = lat;
  point.lon = lon;
  point.time = wxDateTime(static_cast<time_t>(minute * 60));
  point.polar = 0;
  point.cog = 90.0;
  point.twdOverWater = 0.0;
  point.twsOverWater = 10.0;
  point.currentDir = 0.0;
  point.currentSpeed = 0.0;
  return point;
}

RouteSimplifier::SafetyCheck Safe() {
  return [](const PlotData&, const PlotData&, wxString*) { return true; };
}

RouteSimplifier::FeasibilityCheck Feasible() {
  return [](const PlotData&, const PlotData&, double, double* eta_change,
            wxString*) {
    *eta_change = 0.0;
    return true;
  };
}

}  // namespace

TEST(RouteSimplifier, ReducesStraightRouteToEndpoints) {
  std::vector<PlotData> points;
  for (int i = 0; i < 6; ++i)
    points.push_back(MakePlotData(53.0, -6.0 + i * 0.01, i));

  RouteSimplificationOptions options;
  RouteSimplificationResult result =
      RouteSimplifier::Simplify(points, options, Safe(), Feasible());

  ASSERT_TRUE(result.success);
  EXPECT_EQ(2, result.simplified_points);
  EXPECT_EQ(1, result.feasibility_checks);
  EXPECT_EQ(points.front().lon, result.points.front().lon);
  EXPECT_EQ(points.back().lon, result.points.back().lon);
}

TEST(RouteSimplifier, PreservesTackAndSailPlanTransitions) {
  std::vector<PlotData> points;
  for (int i = 0; i < 6; ++i)
    points.push_back(MakePlotData(53.0, -6.0 + i * 0.01, i));
  points[3].tacks = 1;
  points[4].tacks = 1;
  points[5].tacks = 1;

  RouteSimplificationOptions options;
  RouteSimplificationResult result =
      RouteSimplifier::Simplify(points, options, Safe(), Feasible());

  ASSERT_TRUE(result.success);
  ASSERT_GE(result.simplified_points, 3);
  bool found_tack = false;
  for (size_t i = 0; i < result.points.size(); ++i)
    found_tack = found_tack || result.points[i].tacks == 1;
  EXPECT_TRUE(found_tack);
}

TEST(RouteSimplifier, DoesNotUseUnsafeShortcut) {
  std::vector<PlotData> points;
  for (int i = 0; i < 5; ++i)
    points.push_back(MakePlotData(53.0, -6.0 + i * 0.01, i));
  RouteSimplifier::SafetyCheck safety = [](const PlotData& first,
                                           const PlotData& last, wxString*) {
    return last.lon - first.lon <= 0.021;
  };

  RouteSimplificationOptions options;
  RouteSimplificationResult result =
      RouteSimplifier::Simplify(points, options, safety, Feasible());

  ASSERT_TRUE(result.success);
  EXPECT_GE(result.simplified_points, 3);
  EXPECT_GT(result.safety_checks, 0);
}

TEST(RouteSimplifier, RespectsCrossTrackTolerance) {
  std::vector<PlotData> points;
  points.push_back(MakePlotData(53.0, -6.0, 0));
  points.push_back(MakePlotData(53.01, -5.99, 1));
  points.push_back(MakePlotData(53.0, -5.98, 2));

  RouteSimplificationOptions options;
  options.max_cross_track_error_nm = 0.1;
  RouteSimplificationResult result =
      RouteSimplifier::Simplify(points, options, Safe(), Feasible());

  ASSERT_TRUE(result.success);
  EXPECT_EQ(3, result.simplified_points);
}

TEST(RouteSimplifier, DoesNotUseWeatherInfeasibleShortcut) {
  std::vector<PlotData> points;
  for (int i = 0; i < 5; ++i)
    points.push_back(MakePlotData(53.0, -6.0 + i * 0.01, i));
  RouteSimplifier::FeasibilityCheck feasibility =
      [](const PlotData& first, const PlotData& last, double,
         double* eta_change, wxString*) {
        *eta_change = 0.0;
        return last.lon - first.lon <= 0.021;
      };

  RouteSimplificationOptions options;
  RouteSimplificationResult result =
      RouteSimplifier::Simplify(points, options, Safe(), feasibility);

  ASSERT_TRUE(result.success);
  EXPECT_GE(result.simplified_points, 3);
  EXPECT_GT(result.feasibility_checks, 0);
}
