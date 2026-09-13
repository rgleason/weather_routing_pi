#include <gtest/gtest.h>

#include <cmath>

#include "SunCalculator.h"

TEST(SunCalculatorTests, EquinoxNoonAtGreenwichIsHigh) {
  const wxDateTime noonUtc(static_cast<time_t>(1774008000));
  const double elevation = SunCalculator::GetSunElevation(0.0, 0.0, noonUtc);
  EXPECT_GT(elevation, 87.0);
  EXPECT_LT(elevation, 90.1);
}

TEST(SunCalculatorTests, EquinoxMidnightAtGreenwichIsBelowHorizon) {
  const wxDateTime midnightUtc(static_cast<time_t>(1773964800));
  EXPECT_LT(SunCalculator::GetSunElevation(0.0, 0.0, midnightUtc), -87.0);
}

TEST(SunCalculatorTests, InvalidTimeReturnsNaN) {
  EXPECT_TRUE(std::isnan(
      SunCalculator::GetSunElevation(50.0, -1.0, wxDateTime())));
}
