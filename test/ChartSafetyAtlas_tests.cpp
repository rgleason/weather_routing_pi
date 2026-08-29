#include <gtest/gtest.h>

#include "ChartSafetyAtlas.h"

namespace {

using weather_routing::ChartSafetyAtlasChart;
using weather_routing::ChartSafetyAtlasTiles;
using weather_routing::ChartSafetyAtlasIdentity;
using weather_routing::EstimateChartSafetyAtlas;
using weather_routing::kChartSafetyAtlasEstimatedBytesPerTile;

ChartSafetyAtlasChart Chart(std::string path, double min_lat, double min_lon,
                            double max_lat, double max_lon) {
  ChartSafetyAtlasChart chart;
  chart.path = std::move(path);
  chart.min_lat = min_lat;
  chart.min_lon = min_lon;
  chart.max_lat = max_lat;
  chart.max_lon = max_lon;
  return chart;
}

TEST(ChartSafetyAtlas, OverlappingChartBoundsAreCountedOnce) {
  const std::vector<ChartSafetyAtlasChart> charts = {
      Chart("a", 53.0, -5.0, 53.1, -4.9),
      Chart("b", 53.0, -5.0, 53.1, -4.9)};
  const auto estimate = EstimateChartSafetyAtlas(charts);

  EXPECT_EQ(estimate.available_charts, 2u);
  EXPECT_EQ(estimate.selected_charts, 2u);
  EXPECT_EQ(estimate.upper_bound_tiles, 4u);
  EXPECT_EQ(estimate.compact_bytes,
            4u * kChartSafetyAtlasEstimatedBytesPerTile);
  EXPECT_TRUE(estimate.complete);
}

TEST(ChartSafetyAtlas, SelectionControlsPrebuildEstimate) {
  const std::vector<ChartSafetyAtlasChart> charts = {
      Chart("west", 53.0, -5.0, 53.05, -4.95),
      Chart("east", 53.0, -4.0, 53.05, -3.95)};
  const auto estimate = EstimateChartSafetyAtlas(charts, {"west"}, false);

  EXPECT_EQ(estimate.selected_charts, 1u);
  EXPECT_EQ(estimate.upper_bound_tiles, 1u);
}

TEST(ChartSafetyAtlas, DatelineCrossingUsesTwoNarrowRanges) {
  const std::vector<ChartSafetyAtlasChart> charts = {
      Chart("date-line", 10.0, 179.95, 10.05, -179.95)};
  const auto tiles = ChartSafetyAtlasTiles(charts);

  // Floating-point representation puts the exact 10.05 northern edge just
  // inside a second latitude row.  The conservative estimator must never
  // under-count, so the two narrow longitude ranges span two rows here.
  EXPECT_EQ(tiles.size(), 4u);
}

TEST(ChartSafetyAtlas, EstimatorReportsItsSafetyCap) {
  const std::vector<ChartSafetyAtlasChart> charts = {
      Chart("large", 50.0, -10.0, 60.0, 2.0)};
  const auto estimate = EstimateChartSafetyAtlas(charts, {}, true, 100);

  EXPECT_FALSE(estimate.complete);
  EXPECT_EQ(estimate.upper_bound_tiles, 101u);
}

TEST(ChartSafetyAtlas, IdentityChangesOnlyForSelectedChartMetadata) {
  auto selected = Chart("selected", 53.0, -5.0, 53.1, -4.9);
  auto unrelated = Chart("unrelated", 50.0, -2.0, 50.1, -1.9);
  const std::vector<ChartSafetyAtlasChart> original = {selected, unrelated};
  const std::string identity =
      ChartSafetyAtlasIdentity(original, {"selected"}, false);

  unrelated.edition_time = 42;
  EXPECT_EQ(identity,
            ChartSafetyAtlasIdentity({selected, unrelated}, {"selected"},
                                     false));
  selected.edition_time = 42;
  EXPECT_NE(identity,
            ChartSafetyAtlasIdentity({selected, unrelated}, {"selected"},
                                     false));
}

}  // namespace
