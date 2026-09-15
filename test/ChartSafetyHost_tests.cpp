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

#include "ChartLongitude.h"

TEST(ChartSafetyHostLongitude, KeepsLegacyHostRequestsLocalAcrossDateLine) {
  for (auto endpoints : {std::pair{179.99, -179.99},
                         std::pair{-179.99, 179.99}}) {
    const auto parts = weather_routing::SplitChartSegment(
        -20, endpoints.first, -20.02, endpoints.second);
    ASSERT_EQ(parts.count, 2u);
    for (unsigned i = 0; i < parts.count; ++i) {
      const auto& part = parts.segments[i];
      EXPECT_LT(std::abs(part.lon2 - part.lon1), .011);
      EXPECT_GE(part.lon1, -180);
      EXPECT_LE(part.lon1, 180);
      EXPECT_GE(part.lon2, -180);
      EXPECT_LE(part.lon2, 180);
    }
    EXPECT_NEAR(parts.segments[0].lat2, -20.01, 1e-9);
    EXPECT_DOUBLE_EQ(parts.segments[0].lat2, parts.segments[1].lat1);
  }
}

TEST(ChartSafetyHostLongitude, QuintonFinalLegIsShortAndStaysNearTonga) {
  const auto parts = weather_routing::SplitChartSegment(
      -18.62565120, -173.88944898, -18.62, 186.12);
  ASSERT_EQ(parts.count, 1u);
  EXPECT_NEAR(parts.segments[0].lon2, -173.88, 1e-10);
  EXPECT_LT(std::abs(parts.segments[0].lon2 - parts.segments[0].lon1), .01);
}

TEST(ChartSafetyHost, PreparationDeadlineAndExternalCancellationAreIndependent) {
  using namespace weather_routing::chart_safety_host;
  std::atomic_bool cancel{false};
  SetPrewarmCancellationFlag(&cancel);
  SetPrewarmDeadline(std::chrono::steady_clock::now() + std::chrono::hours(1));
  EXPECT_FALSE(PrewarmCancellationRequested());
  cancel = true;
  EXPECT_TRUE(PrewarmCancellationRequested());
  cancel = false;
  SetPrewarmDeadline(std::chrono::steady_clock::now() - std::chrono::seconds(1));
  EXPECT_TRUE(PrewarmCancellationRequested());
  SetPrewarmDeadline({});
  EXPECT_FALSE(PrewarmCancellationRequested());
  SetPrewarmCancellationFlag(nullptr);
}
