#include <wx/wx.h>

#include <gtest/gtest.h>

#include "MultiLegRouteOutput.h"

namespace {

PlotData RoutePointAt(double lat, double lon, int minute) {
  PlotData point = PlotData();
  point.lat = lat;
  point.lon = lon;
  point.time = wxDateTime(static_cast<time_t>(minute * 60));
  return point;
}

MultiLegRouteOutputLeg Leg(int index, int count,
                           std::initializer_list<PlotData> points) {
  MultiLegRouteOutputLeg leg;
  leg.index = index;
  leg.count = count;
  leg.points.assign(points.begin(), points.end());
  return leg;
}

}  // namespace

TEST(MultiLegRouteOutput, AssemblesAllLegsInDeclaredOrder) {
  std::vector<MultiLegRouteOutputLeg> legs;
  legs.push_back(Leg(2, 3, {RoutePointAt(53.0, -5.0, 10),
                            RoutePointAt(54.0, -5.0, 20)}));
  legs.push_back(Leg(1, 3, {RoutePointAt(52.0, -5.0, 0),
                            RoutePointAt(53.0, -5.0, 10)}));
  legs.push_back(Leg(3, 3, {RoutePointAt(54.0, -5.0, 20),
                            RoutePointAt(55.0, -5.0, 30)}));

  MultiLegRouteOutputResult result = MultiLegRouteOutput::Assemble(legs);

  ASSERT_TRUE(result.success) << result.failure_reason;
  ASSERT_EQ(4U, result.points.size());
  EXPECT_DOUBLE_EQ(52.0, result.points.front().lat);
  EXPECT_DOUBLE_EQ(55.0, result.points.back().lat);
}

TEST(MultiLegRouteOutput, PreservesEveryInteriorPointFromEveryLeg) {
  std::vector<MultiLegRouteOutputLeg> legs;
  legs.push_back(
      Leg(1, 2, {RoutePointAt(52.0, -5.0, 0),
                 RoutePointAt(52.5, -5.2, 5),
                 RoutePointAt(53.0, -5.0, 10)}));
  legs.push_back(Leg(
      2, 2,
      {RoutePointAt(53.0, -5.0, 10), RoutePointAt(53.5, -4.8, 15),
       RoutePointAt(54.0, -5.0, 20)}));

  MultiLegRouteOutputResult result = MultiLegRouteOutput::Assemble(legs);

  ASSERT_TRUE(result.success) << result.failure_reason;
  ASSERT_EQ(5U, result.points.size());
  EXPECT_DOUBLE_EQ(52.5, result.points[1].lat);
  EXPECT_DOUBLE_EQ(53.0, result.points[2].lat);
  EXPECT_DOUBLE_EQ(53.5, result.points[3].lat);
}

TEST(MultiLegRouteOutput, RejectsMissingLeg) {
  std::vector<MultiLegRouteOutputLeg> legs;
  legs.push_back(Leg(1, 3, {RoutePointAt(52.0, -5.0, 0),
                            RoutePointAt(53.0, -5.0, 10)}));
  legs.push_back(Leg(3, 3, {RoutePointAt(54.0, -5.0, 20),
                            RoutePointAt(55.0, -5.0, 30)}));

  MultiLegRouteOutputResult result = MultiLegRouteOutput::Assemble(legs);

  EXPECT_FALSE(result.success);
  EXPECT_NE(wxNOT_FOUND, result.failure_reason.Find("expected 3 legs"));
}

TEST(MultiLegRouteOutput, RejectsDuplicateLegNumber) {
  std::vector<MultiLegRouteOutputLeg> legs;
  legs.push_back(Leg(1, 2, {RoutePointAt(52.0, -5.0, 0),
                            RoutePointAt(53.0, -5.0, 10)}));
  legs.push_back(Leg(1, 2, {RoutePointAt(53.0, -5.0, 10),
                            RoutePointAt(54.0, -5.0, 20)}));

  MultiLegRouteOutputResult result = MultiLegRouteOutput::Assemble(legs);

  EXPECT_FALSE(result.success);
  EXPECT_NE(wxNOT_FOUND, result.failure_reason.Find("numbering"));
}

TEST(MultiLegRouteOutput, RejectsGeographicGapBetweenLegs) {
  std::vector<MultiLegRouteOutputLeg> legs;
  legs.push_back(Leg(1, 2, {RoutePointAt(52.0, -5.0, 0),
                            RoutePointAt(53.0, -5.0, 10)}));
  legs.push_back(Leg(2, 2, {RoutePointAt(53.1, -5.0, 10),
                            RoutePointAt(54.0, -5.0, 20)}));

  MultiLegRouteOutputResult result = MultiLegRouteOutput::Assemble(legs);

  EXPECT_FALSE(result.success);
  EXPECT_NE(wxNOT_FOUND, result.failure_reason.Find("do not meet"));
}

TEST(MultiLegRouteOutput, JoinsAcrossTheDateLine) {
  std::vector<MultiLegRouteOutputLeg> legs;
  legs.push_back(Leg(1, 2, {RoutePointAt(10.0, 179.0, 0),
                            RoutePointAt(10.0, 180.0, 10)}));
  legs.push_back(Leg(2, 2, {RoutePointAt(10.0, -180.0, 10),
                            RoutePointAt(10.0, -179.0, 20)}));

  MultiLegRouteOutputResult result = MultiLegRouteOutput::Assemble(legs);

  ASSERT_TRUE(result.success) << result.failure_reason;
  EXPECT_EQ(3U, result.points.size());
}
