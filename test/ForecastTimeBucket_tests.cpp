// SPDX-License-Identifier: GPL-3.0-or-later
#include <gtest/gtest.h>
#include "ForecastTimeBucket.h"
using namespace weather_routing;
TEST(ForecastTimeBucket, ForecastEndCannotSupplyRoundedSamplesAfterItsEnd) {
  constexpr std::int64_t end = 1796299200;
  ASSERT_EQ(RoundedWeatherTime(end + 1), end);
  ASSERT_TRUE(ForecastBoundaryInBucket(end, end - 86400, end));
  EXPECT_FALSE(ForecastBoundaryInBucket(end - 900, end - 86400, end));
  EXPECT_FALSE(ForecastBoundaryInBucket(end + 900, end - 86400, end));
  EXPECT_TRUE(ForecastBoundaryInBucket(end, end, end + 86400));
}
TEST(ForecastTimeBucket, MissingFieldsAndDifferentParameterExtentsUseExactTimes) {
  EXPECT_TRUE(ForecastBucketChanges(1, 1, 0));
  EXPECT_TRUE(ForecastBucketChanges(0, 1, 1));
  EXPECT_TRUE(ForecastBucketChanges(3, 3, 1)); // Current ends before wind.
  EXPECT_TRUE(ForecastBucketChanges(5, 1, 1)); // Wave heights end before wind.
  EXPECT_TRUE(ForecastBucketChanges(0, 1, 0)); // Short forecast inside bucket.
  EXPECT_FALSE(ForecastBucketChanges(3, 3, 3));
  EXPECT_FALSE(ForecastBucketChanges(0, 0, 0));
  EXPECT_FALSE(ForecastBoundaryInBucket(10000, -1, -1));
}
