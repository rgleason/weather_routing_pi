#include <gtest/gtest.h>

#include "GribTimelineCachePolicy.h"

namespace wr = weather_routing;

TEST(GribTimelineCachePolicy, PreservesHistoricalStandardLimits) {
  auto main = wr::EvaluateGribTimelineCacheAdmission(512, false, 1024, 64);
  EXPECT_TRUE(main.approved);
  EXPECT_EQ(main.effective_mib, 512);
  auto quick = wr::EvaluateGribTimelineCacheAdmission(64, true, 256, 64);
  EXPECT_TRUE(quick.approved);
  EXPECT_EQ(quick.effective_mib, 64);
}

TEST(GribTimelineCachePolicy, TwoGiBCacheRequiresEightGiBAvailable) {
  auto insufficient =
      wr::EvaluateGribTimelineCacheAdmission(2048, false, 8191, 64);
  EXPECT_FALSE(insufficient.approved);
  EXPECT_EQ(insufficient.effective_mib,
            wr::kMainGribTimelineCacheDefaultMiB);
  EXPECT_EQ(insufficient.required_reserve_mib, 6144U);
  EXPECT_EQ(insufficient.required_before_mib, 8192U);

  auto admitted =
      wr::EvaluateGribTimelineCacheAdmission(2048, false, 8192, 64);
  EXPECT_TRUE(admitted.approved);
  EXPECT_EQ(admitted.effective_mib, 2048);
}

TEST(GribTimelineCachePolicy, UnknownMemoryNeverEnablesLargeCache) {
  auto admission =
      wr::EvaluateGribTimelineCacheAdmission(4096, false, 0, 64);
  EXPECT_FALSE(admission.approved);
  EXPECT_FALSE(admission.memory_known);
  EXPECT_EQ(admission.effective_mib,
            wr::kMainGribTimelineCacheDefaultMiB);
}

TEST(GribTimelineCachePolicy, ThirtyTwoBitProcessesNeverEnableLargeCache) {
  auto admission =
      wr::EvaluateGribTimelineCacheAdmission(2048, false, 65536, 32);
  EXPECT_EQ(admission.requested_mib,
            wr::kGribTimelineCacheMaximum32BitMiB);
  EXPECT_EQ(admission.effective_mib,
            wr::kGribTimelineCacheMaximum32BitMiB);
}

TEST(GribTimelineCachePolicy, ValuesAreNormalizedPerEngine) {
  EXPECT_EQ(wr::NormalizeGribTimelineCacheMiB(0, false, 64), 512);
  EXPECT_EQ(wr::NormalizeGribTimelineCacheMiB(0, true, 64), 64);
  EXPECT_EQ(wr::NormalizeGribTimelineCacheMiB(99999, false, 64), 8192);
  EXPECT_EQ(wr::NormalizeGribTimelineCacheMiB(99999, false, 32), 192);
}
