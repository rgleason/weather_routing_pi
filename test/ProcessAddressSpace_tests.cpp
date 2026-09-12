#include <gtest/gtest.h>

#include "ProcessAddressSpace.h"

namespace {
constexpr std::uint64_t GiB = 1024ULL * 1024 * 1024;

TEST(ProcessAddressSpace, UsesHostLimitRatherThanFixedTwoGiB) {
  for (const auto total : {2 * GiB, 4 * GiB, 128 * 1024 * GiB}) {
    const auto usage = weather_routing::MakeProcessAddressSpace(total, total / 4);
    ASSERT_TRUE(usage);
    EXPECT_EQ(usage->UsedBytes(), total * 3 / 4);
    EXPECT_DOUBLE_EQ(usage->UsedPercent(), 75.0);
  }
}

TEST(ProcessAddressSpace, RejectsUnknownOrInconsistentMeasurements) {
  EXPECT_FALSE(weather_routing::MakeProcessAddressSpace(0, 0));
  EXPECT_FALSE(weather_routing::MakeProcessAddressSpace(2 * GiB, 4 * GiB));
  const auto exhausted = weather_routing::MakeProcessAddressSpace(2 * GiB, 0);
  ASSERT_TRUE(exhausted);
  EXPECT_DOUBLE_EQ(exhausted->UsedPercent(), 100.0);
}

#ifdef _WIN32
TEST(ProcessAddressSpace, WindowsReservationConsumesAddressSpaceWithoutCommit) {
  const auto before = weather_routing::QueryProcessAddressSpace();
  ASSERT_TRUE(before);
  constexpr SIZE_T reservationBytes = 64 * 1024 * 1024;
  void* reservation = VirtualAlloc(nullptr, reservationBytes, MEM_RESERVE,
                                   PAGE_NOACCESS);
  ASSERT_NE(reservation, nullptr);
  const auto reserved = weather_routing::QueryProcessAddressSpace();
  const BOOL released = VirtualFree(reservation, 0, MEM_RELEASE);
  ASSERT_TRUE(released);
  ASSERT_TRUE(reserved);
  EXPECT_EQ(reserved->total, before->total);
  EXPECT_GE(reserved->UsedBytes(), before->UsedBytes() + reservationBytes);
}
#endif
}  // namespace
