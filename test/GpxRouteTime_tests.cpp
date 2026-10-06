#include <gtest/gtest.h>
#include "navobj_util.h"
#include "TimeZoneDisplay.h"

TEST(GpxRouteTime, DepartureEtDAndPointTimesAreExplicitUtcInstants) {
  const auto departure = marine_time::FromWallClock(2026, 9, 26, 12, 25, 34, "UTC").utc;
  SimpleRoute route;
  route.m_GUID = "test-route";
  route.m_PlannedDeparture = departure;
  auto* start = new SimpleRoutePoint(53.33, -4.61, "circle", "Start", "start");
  start->m_CreateTime = departure;
  start->etd = departure;
  route.AddPoint(start);
  auto* end = new SimpleRoutePoint(53.30, -3.90, "circle", "End", "end");
  end->m_CreateTime = departure + wxTimeSpan::Hours(6);
  end->etd = end->m_CreateTime;
  end->m_seg_vmg = 5.5;
  route.AddPoint(end);
  SimpleNavObjectXML gpx;
  ASSERT_TRUE(gpx.CreateNavObjGPXRoute(route));
  const auto output = gpx.child("gpx").child("rte");
  EXPECT_STREQ(output.child("extensions").child("opencpn:planned_departure").child_value(),
               "2026-09-26T12:25:34Z");
  EXPECT_STREQ(output.child("rtept").child("time").child_value(), "2026-09-26T12:25:34Z");
  const auto finish = output.child("rtept").next_sibling("rtept");
  EXPECT_STREQ(finish.child("time").child_value(), "2026-09-26T18:25:34Z");
  EXPECT_STREQ(finish.child("extensions").child("opencpn:rte_properties").attribute("etd").value(),
               "2026-09-26T18:25:34Z");
  EXPECT_DOUBLE_EQ(finish.child("extensions").child("opencpn:rte_properties").attribute("planned_speed").as_double(), 5.5);
}
