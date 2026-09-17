#include <gtest/gtest.h>

#include "engine/native/ConstraintTime.h"

TEST(ConstraintTime, PreservesAValidSegmentTime) {
  const wxDateTime departure(static_cast<time_t>(100));
  const wxDateTime segment(static_cast<time_t>(200));

  const auto resolved =
      weather_routing::native::ResolveConstraintTime(segment, departure);

  ASSERT_TRUE(resolved.has_value());
  EXPECT_EQ(resolved->GetTicks(), segment.GetTicks());
}

TEST(ConstraintTime, UsesDepartureForAnUntimedRecoveryProbe) {
  const wxDateTime departure(static_cast<time_t>(100));

  const auto resolved = weather_routing::native::ResolveConstraintTime(
      wxInvalidDateTime, departure);

  ASSERT_TRUE(resolved.has_value());
  EXPECT_EQ(resolved->GetTicks(), departure.GetTicks());
}

TEST(ConstraintTime, RejectsAProbeWhenNoValidTimeExists) {
  const auto resolved = weather_routing::native::ResolveConstraintTime(
      wxInvalidDateTime, wxInvalidDateTime);

  EXPECT_FALSE(resolved.has_value());
}
