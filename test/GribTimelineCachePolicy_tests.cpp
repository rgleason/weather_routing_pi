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
  EXPECT_EQ(insufficient.effective_mib, 1024);
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
            wr::kMainGribTimelineCacheFallback64BitMiB);
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
  EXPECT_EQ(wr::NormalizeGribTimelineCacheMiB(0, false, 64), 2048);
  EXPECT_EQ(wr::NormalizeGribTimelineCacheMiB(0, false, 32), 192);
  EXPECT_EQ(wr::NormalizeGribTimelineCacheMiB(0, true, 64), 2048);
  EXPECT_EQ(wr::NormalizeGribTimelineCacheMiB(0, true, 32), 64);
  EXPECT_EQ(wr::NormalizeGribTimelineCacheMiB(99999, false, 64), 8192);
  EXPECT_EQ(wr::NormalizeGribTimelineCacheMiB(99999, false, 32), 192);
}

TEST(GribTimelineCachePolicy, NewDefaultsRemainSubjectToMemoryAdmission) {
  for (bool quick : {false, true}) {
    EXPECT_EQ(wr::EvaluateGribTimelineCacheAdmission(0, quick, 8192, 64).effective_mib, 2048);
    EXPECT_EQ(wr::EvaluateGribTimelineCacheAdmission(0, quick, 5120, 64).effective_mib, 1024);
    EXPECT_EQ(wr::EvaluateGribTimelineCacheAdmission(0, quick, 5119, 64).effective_mib, 512);
    const auto unknown = wr::EvaluateGribTimelineCacheAdmission(0, quick, 0, 64);
    EXPECT_FALSE(unknown.approved);
    EXPECT_EQ(unknown.effective_mib, wr::GribTimelineCacheFallbackMiB(quick, 64));
  }
  EXPECT_EQ(wr::EvaluateGribTimelineCacheAdmission(0, true, 3584, 64).effective_mib, 512);
  EXPECT_EQ(wr::EvaluateGribTimelineCacheAdmission(0, true, 2816, 64).effective_mib, 256);
  EXPECT_EQ(wr::EvaluateGribTimelineCacheAdmission(0, true, 2432, 64).effective_mib, 128);
  EXPECT_EQ(wr::EvaluateGribTimelineCacheAdmission(0, true, 2431, 64).effective_mib, 64);
}

TEST(GribTimelineCachePolicy, ExplicitSmallLimitsAreNeverRaisedToNewDefault) {
  for (bool quick : {false, true}) {
    for (int requested : {16, 64, 128, 192, 256, 512}) {
      const auto admission = wr::EvaluateGribTimelineCacheAdmission(requested, quick, 65536, 64);
      EXPECT_TRUE(admission.approved);
      EXPECT_EQ(admission.requested_mib, requested);
      EXPECT_EQ(admission.effective_mib, requested);
    }
  }
}

TEST(GribTimelineCachePolicy, ReducedAllowancesRespectReserveAndRequest) {
  for (bool quick : {false, true}) {
    for (int requested : {64, 512, 777, 1024, 2048, 2061, 8192}) {
      for (std::uint64_t available : {0U, 1024U, 3000U, 5120U, 8192U, 65536U}) {
        const auto admission = wr::EvaluateGribTimelineCacheAdmission(requested, quick, available, 64);
        EXPECT_LE(admission.effective_mib, admission.requested_mib);
        if (admission.effective_mib > wr::GribTimelineCacheFallbackMiB(quick, 64)) {
          EXPECT_GE(available, wr::kGribTimelineCacheBaseReserveMiB + 3ULL * admission.effective_mib);
        }
      }
    }
  }
}
