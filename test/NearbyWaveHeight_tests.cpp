#include <gtest/gtest.h>
#include <array>
#include "engine/native/NearbyWaveHeight.h"
namespace {
struct Grid {
  double lon{}, lat{}, di{0.1}, dj{0.1};
  std::array<double, 25> values;
  Grid() { values.fill(-1); }
  int getNi() const { return 5; }
  int getNj() const { return 5; }
  double getDi() const { return di; }
  double getDj() const { return dj; }
  double getX(int i) const { return lon + i * di; }
  double getY(int j) const { return lat + j * dj; }
  bool isDefined(int i, int j) const { return values[j * 5 + i] >= 0; }
  double getValue(int i, int j) const { return values[j * 5 + i]; }
};
auto openWater = [](auto, auto) { return true; };
TEST(NearbyWaveHeight, UsesHighestLocalValueIncludingCalmZero) {
  Grid g; g.values[6] = 0; g.values[8] = 2; g.values[18] = 3;
  auto value = weather_routing::native::NearbyWaveHeight(g, {0.2, 0.2}, openWater);
  ASSERT_TRUE(value); EXPECT_DOUBLE_EQ(*value, 3);
}
TEST(NearbyWaveHeight, DoesNotCrossLandOrBorrowWithoutConnectedWater) {
  Grid g; g.values[6] = 9; g.values[8] = 2;
  auto value = weather_routing::native::NearbyWaveHeight(g, {0.2, 0.2},
      [](auto, auto cell) { return cell.longitude > 0.2; });
  ASSERT_TRUE(value); EXPECT_DOUBLE_EQ(*value, 2);
  EXPECT_FALSE(weather_routing::native::NearbyWaveHeight(g, {0.2, 0.2},
      [](auto, auto) { return false; }));
}
TEST(NearbyWaveHeight, KeepsLargeGapsAndOutsideCoverageUnknown) {
  Grid g; g.values[0] = 4;
  EXPECT_FALSE(weather_routing::native::NearbyWaveHeight(g, {0.2, 0.2}, openWater));
  EXPECT_FALSE(weather_routing::native::NearbyWaveHeight(g, {0.2, -0.01}, openWater));
  EXPECT_FALSE(weather_routing::native::NearbyWaveHeight(g, {0.51, 0.2}, openWater));
  g.di = g.dj = 1; g.values[11] = 7;
  EXPECT_FALSE(weather_routing::native::NearbyWaveHeight(g, {2., 2.}, openWater));
}
TEST(NearbyWaveHeight, HandlesDateLineAndReversedLatitudeScanning) {
  Grid g; g.lon = 179.8; g.lat = 0.4; g.dj = -0.1; g.values[13] = 2.5;
  auto value = weather_routing::native::NearbyWaveHeight(g, {0.2, -180.}, openWater);
  ASSERT_TRUE(value); EXPECT_DOUBLE_EQ(*value, 2.5);
  g.lon = 180.2; g.di = -0.1;
  value = weather_routing::native::NearbyWaveHeight(g, {0.2, -180.}, openWater);
  ASSERT_TRUE(value); EXPECT_DOUBLE_EQ(*value, 2.5);
}
}
