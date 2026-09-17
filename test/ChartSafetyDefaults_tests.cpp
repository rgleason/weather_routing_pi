#include <gtest/gtest.h>

#include "ChartSafetyDefaults.h"

TEST(ChartSafetyDefaults, FreshStandardInstallIsStockSafe) {
  EXPECT_FALSE(weather_routing::chart_safety_defaults::kCheckLoadedCharts);
  EXPECT_FALSE(
      weather_routing::chart_safety_defaults::kRequireChartDepthChecks);
}
