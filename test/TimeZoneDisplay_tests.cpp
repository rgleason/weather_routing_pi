#include <gtest/gtest.h>

#include "TimeZoneDisplay.h"
#include "RoutingTimePersistence.h"

namespace {

wxDateTime Utc(int year, wxDateTime::Month month, int day, int hour,
               int minute = 0) {
  const auto conversion = marine_time::FromWallClock(
      year, static_cast<int>(month) + 1, day, hour, minute, 0, "UTC");
  EXPECT_EQ(conversion.status, marine_time::WallClockStatus::Valid);
  return conversion.utc;
}

}  // namespace

TEST(RoutingTimePersistence, DepartureAndArrivalRetainInstantsAfterXmlRoundTrip) {
  TiXmlElement route("Configuration");
  const auto departure = Utc(2026, wxDateTime::Sep, 26, 11, 25);
  const auto arrival = Utc(2026, wxDateTime::Sep, 26, 17, 45);
  weather_routing::WriteRoutingTime(route, departure,
      "StartDate", "StartTime", "StartTimeUnixSeconds");
  weather_routing::WriteRoutingTime(route, arrival,
      "PlannedArrivalDate", "PlannedArrivalTime", "PlannedArrivalUnixSeconds");
  TiXmlPrinter printer;
  route.Accept(&printer);
  TiXmlDocument reopened;
  reopened.Parse(printer.CStr());
  ASSERT_FALSE(reopened.Error());
  ASSERT_NE(reopened.RootElement(), nullptr);
  EXPECT_EQ(weather_routing::ReadRoutingTime(*reopened.RootElement(),
      "StartDate", "StartTime", "StartTimeUnixSeconds", wxDateTime()).GetTicks(),
      departure.GetTicks());
  EXPECT_EQ(weather_routing::ReadRoutingTime(*reopened.RootElement(),
      "PlannedArrivalDate", "PlannedArrivalTime", "PlannedArrivalUnixSeconds",
      wxDateTime()).GetTicks(), arrival.GetTicks());
}

TEST(RoutingTimePersistence, ExplicitInstantSurvivesDifferentLocalClockFields) {
  TiXmlElement route("Configuration");
  const auto departure = Utc(2026, wxDateTime::Sep, 26, 11, 25);
  weather_routing::WriteRoutingTime(route, departure,
      "StartDate", "StartTime", "StartTimeUnixSeconds");
  // A file may move between computers with different system timezones.
  route.SetAttribute("StartTime", "03:25:00");
  EXPECT_EQ(weather_routing::ReadRoutingTime(route,
      "StartDate", "StartTime", "StartTimeUnixSeconds", wxDateTime()).GetTicks(),
      departure.GetTicks());
}

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
