// SPDX-License-Identifier: GPL-3.0-or-later
#include <gtest/gtest.h>
#include "engine/native/RouteGust.h"

namespace wr = supercpn::weather_routing;

TEST(RouteGust, SamplesEachLegAtItsOwnStartPositionAndTime) {
  std::vector<wr::RouteLeg> legs(2);
  legs[0].start = {53.3, -4.6};
  legs[1].start = {53.4, -4.2};
  legs[0].startTime = wr::TimePoint{wr::Duration{1000}};
  legs[1].startTime = wr::TimePoint{wr::Duration{2800}};
  int calls = 0;
  weather_routing::native::PopulateRouteGusts(legs,
      [&](wr::GeoPoint position, wr::TimePoint time) {
        const auto& expected = legs[calls++];
        EXPECT_EQ(position, expected.start);
        EXPECT_EQ(time, expected.startTime);
        return time == legs[0].startTime ? 24.5 : 31.2;
      });
  EXPECT_EQ(calls, 2);
  EXPECT_DOUBLE_EQ(weather_routing::native::RouteGustKnots(legs[0]), 24.5);
  EXPECT_DOUBLE_EQ(weather_routing::native::RouteGustKnots(legs[1]), 31.2);
}

TEST(RouteGust, PreservesZeroAndKeepsMissingOrInvalidValuesUnavailable) {
  std::vector<wr::RouteLeg> legs(5);
  const double values[] = {0.0, NAN, INFINITY, -1.0, 35.0};
  int index = 0;
  weather_routing::native::PopulateRouteGusts(legs,
      [&](auto, auto) { return values[index++]; });
  EXPECT_DOUBLE_EQ(weather_routing::native::RouteGustKnots(legs[0]), 0.0);
  for (int i : {1, 2, 3})
    EXPECT_TRUE(std::isnan(weather_routing::native::RouteGustKnots(legs[i])));
  EXPECT_DOUBLE_EQ(weather_routing::native::RouteGustKnots(legs[4]), 35.0);
}

TEST(RouteGust, UnsampledLegIsUnavailable) {
  EXPECT_TRUE(std::isnan(weather_routing::native::RouteGustKnots(wr::RouteLeg{})));
}
