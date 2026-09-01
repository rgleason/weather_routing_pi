#include <gtest/gtest.h>

#include "ChartSafetyCache.h"
#include "ChartSafetyHost.h"

namespace {

TEST(ChartSafetyHost, StockHostWithoutOptionalSymbolsRemainsUsable) {
  weather_routing::ChartSafetyCache cache;

  ASSERT_FALSE(weather_routing::chart_safety_host::Initialize(&cache));
  EXPECT_FALSE(weather_routing::chart_safety_host::Available());
  EXPECT_EQ(weather_routing::chart_safety_host::Status(),
            "Enhanced chart-safety host capability is unavailable.");

  PlugInSegmentSafetyOptions options = {};
  options.struct_size = sizeof(options);
  options.check_land = 1;
  PlugInSegmentSafetyResult result = {};
  result.struct_size = sizeof(result);
  EXPECT_FALSE(weather_routing::chart_safety_host::CheckSegment(
      53.3, -4.6, 53.4, -4.7, &options, &result));

  weather_routing::chart_safety_host::Shutdown();
}

}  // namespace
