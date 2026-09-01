#include <gtest/gtest.h>

#include "TimeZoneDisplay.h"

namespace {

wxDateTime Utc(int year, wxDateTime::Month month, int day, int hour,
               int minute = 0) {
  const auto conversion = marine_time::FromWallClock(
      year, static_cast<int>(month) + 1, day, hour, minute, 0, "UTC");
  EXPECT_EQ(conversion.status, marine_time::WallClockStatus::Valid);
  return conversion.utc;
}

}  // namespace

TEST(TimeZoneDisplay, LondonUsesGmtInWinterAndBstInSummer) {
  if (!marine_time::IsTimeZoneAvailable("Europe/London")) GTEST_SKIP();

  EXPECT_EQ(marine_time::FormatInTimeZone(
                Utc(2026, wxDateTime::Jan, 15, 12), "%Y-%m-%d %H:%M",
                "Europe/London"),
            "2026-01-15 12:00 GMT");
  EXPECT_EQ(marine_time::FormatInTimeZone(
                Utc(2026, wxDateTime::Jul, 15, 12), "%Y-%m-%d %H:%M",
                "Europe/London"),
            "2026-07-15 13:00 BST");
}

TEST(TimeZoneDisplay, LondonWallClockRoundTripsToUtc) {
  if (!marine_time::IsTimeZoneAvailable("Europe/London")) GTEST_SKIP();

  const auto winter = marine_time::FromWallClock(2026, 1, 15, 12, 0, 0,
                                                  "Europe/London");
  const auto summer = marine_time::FromWallClock(2026, 7, 15, 13, 0, 0,
                                                  "Europe/London");
  ASSERT_EQ(winter.status, marine_time::WallClockStatus::Valid);
  ASSERT_EQ(summer.status, marine_time::WallClockStatus::Valid);
  EXPECT_EQ(winter.utc.GetTicks(),
            Utc(2026, wxDateTime::Jan, 15, 12).GetTicks());
  EXPECT_EQ(summer.utc.GetTicks(),
            Utc(2026, wxDateTime::Jul, 15, 12).GetTicks());
}

TEST(TimeZoneDisplay, RejectsSpringGapAndFlagsAutumnRepeat) {
  if (!marine_time::IsTimeZoneAvailable("Europe/London")) GTEST_SKIP();

  const auto gap = marine_time::FromWallClock(2026, 3, 29, 1, 30, 0,
                                               "Europe/London");
  EXPECT_EQ(gap.status, marine_time::WallClockStatus::Nonexistent);
  EXPECT_FALSE(gap.utc.IsValid());

  const auto repeat = marine_time::FromWallClock(2026, 10, 25, 1, 30, 0,
                                                  "Europe/London");
  ASSERT_EQ(repeat.status, marine_time::WallClockStatus::Ambiguous);
  EXPECT_EQ(repeat.utc.GetTicks(),
            Utc(2026, wxDateTime::Oct, 25, 0, 30).GetTicks());
}

TEST(TimeZoneDisplay, UnknownZoneNeverSilentlyChangesTheInstant) {
  EXPECT_FALSE(marine_time::IsTimeZoneAvailable("Not/A_Real_Zone"));
  EXPECT_FALSE(
      marine_time::ToWallClock(Utc(2026, wxDateTime::Jul, 15, 12),
                               "Not/A_Real_Zone")
          .IsValid());
}
