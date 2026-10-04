#include <gtest/gtest.h>
#include "DeviceMemoryPolicy.h"
#include "GribTimelineCachePolicy.h"

namespace wr = weather_routing;

TEST(DeviceMemoryPolicy, LowRamShrinksBothCachesWithoutRaisingUserLimits) {
  for (bool quick : {false, true}) {
    const auto admission = wr::EvaluateGribTimelineCacheAdmission(2048, quick, 500, 64);
    EXPECT_EQ(admission.requested_mib, 2048);
    EXPECT_EQ(admission.effective_mib, 62);
    EXPECT_FALSE(admission.approved);
    EXPECT_EQ(wr::EvaluateGribTimelineCacheAdmission(16, quick, 500, 64).effective_mib, 16);
    EXPECT_EQ(wr::EvaluateGribTimelineCacheAdmission(192, quick, 500, 32).effective_mib, 62);
  }
  EXPECT_EQ(wr::PressureCacheLimitMiB(256, 500, 16), 31);
  EXPECT_EQ(wr::PressureCacheLimitMiB(512, 64), 16);
  EXPECT_EQ(wr::PressureCacheLimitMiB(512, 0), 512);
  EXPECT_EQ(wr::PressureCacheLimitMiB(512, 1), 16);
  EXPECT_EQ(wr::EvaluateGribTimelineCacheAdmission(2048, false, 1, 64).effective_mib, 16);
}

TEST(DeviceMemoryPolicy, AdditionalWorkersUseRemainingHeadroom) {
  EXPECT_EQ(wr::MemoryAwareRouteWorkerLimit(8, 0, 500), 1);
  EXPECT_EQ(wr::MemoryAwareRouteWorkerLimit(8, 0, 1024), 3);
  EXPECT_EQ(wr::MemoryAwareRouteWorkerLimit(8, 0, 1024, 512), 1);
  EXPECT_EQ(wr::MemoryAwareRouteWorkerLimit(8, 2, 100), 2);
  EXPECT_EQ(wr::MemoryAwareRouteWorkerLimit(8, 2, 1024), 5);
  EXPECT_EQ(wr::MemoryAwareRouteWorkerLimit(2, 0, 65536), 2);
  EXPECT_EQ(wr::MemoryAwareRouteWorkerLimit(8, 0, 64), 1);
#ifndef __OCPN__ANDROID__
  EXPECT_EQ(wr::MemoryAwareRouteWorkerLimit(8, 0, 0), 8);
#endif
}
