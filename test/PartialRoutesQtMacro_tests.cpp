// SPDX-License-Identifier: GPL-3.0-or-later
#include <gtest/gtest.h>
#include "supercpn/weather_routing/Types.h"

// Qt headers define this before the adapter includes PartialRoutes.h. Keep
// this a separate executable so all definitions see the same macro context.
#define slots
#include "supercpn/weather_routing/PartialRoutes.h"
#undef slots

TEST(PartialRoutesQtMacro, ArchiveRetainsCapacityWithQtKeywordEnabled) {
  namespace wr = supercpn::weather_routing;
  const wr::GeoPoint start{22, -160}, end{23.5, -160}, point{22.5, -160};
  const wr::TimePoint departure{wr::Duration{1792224000}};
  const auto time = departure + std::chrono::hours{1};
  wr::PartialRouteArchive archive(start, end, departure,
      std::chrono::hours{48}, 1024 * 1024);
  ASSERT_TRUE(archive.wanted({point, time, "Qt adapter", wr::distanceNm(point, end)}));
  archive.offer(point, time, "Qt adapter", [&] {
    wr::RouteLeg leg;
    leg.start = start; leg.end = point;
    leg.startTime = departure; leg.endTime = time;
    return wr::FreezePartialLegs({leg});
  });
  ASSERT_EQ(archive.entries().size(), 1U);
  EXPECT_GT(archive.bytes(), 0U);
}
