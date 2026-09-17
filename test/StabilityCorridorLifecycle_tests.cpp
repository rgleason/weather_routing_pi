#include <gtest/gtest.h>

#include <cstdint>

#include "StabilityCorridorLifecycle.h"

namespace {

const void* Token(int value) {
  return reinterpret_cast<const void*>(static_cast<uintptr_t>(value));
}

TEST(StabilityCorridorLifecycle, UnpinnedCorridorClosesWithResults) {
  StabilityCorridorLifecycle lifecycle;
  lifecycle.Show(2, {Token(1)});

  EXPECT_TRUE(lifecycle.ResultsClosed());
  EXPECT_FALSE(lifecycle.IsVisible());
  EXPECT_EQ(-1, lifecycle.FamilyId());
}

TEST(StabilityCorridorLifecycle, PinnedCorridorSurvivesResultsClose) {
  StabilityCorridorLifecycle lifecycle;
  lifecycle.Show(3, {Token(1), Token(2)});
  lifecycle.SetPinned(true);

  EXPECT_FALSE(lifecycle.ResultsClosed());
  EXPECT_TRUE(lifecycle.IsVisible());
  EXPECT_TRUE(lifecycle.IsPinned());
  EXPECT_EQ(3, lifecycle.FamilyId());
}

TEST(StabilityCorridorLifecycle, RouteOrderDoesNotInvalidateMultiLegDisplay) {
  StabilityCorridorLifecycle lifecycle;
  lifecycle.Show(1, {Token(1), Token(2)});
  lifecycle.SetPinned(true);

  EXPECT_FALSE(lifecycle.DisplayedRoutesChanged({Token(2), Token(1)}));
  EXPECT_TRUE(lifecycle.IsVisible());
}

TEST(StabilityCorridorLifecycle, DifferentSelectionHidesPinnedCorridor) {
  StabilityCorridorLifecycle lifecycle;
  lifecycle.Show(1, {Token(1), Token(2)});
  lifecycle.SetPinned(true);

  EXPECT_TRUE(lifecycle.DisplayedRoutesChanged({Token(3)}));
  EXPECT_FALSE(lifecycle.IsVisible());
  EXPECT_FALSE(lifecycle.IsPinned());
}

TEST(StabilityCorridorLifecycle, ManualHideClearsPresentationIdentity) {
  StabilityCorridorLifecycle lifecycle;
  lifecycle.Show(4, {Token(7)});
  lifecycle.SetPinned(true);

  EXPECT_TRUE(lifecycle.Contains(Token(7)));
  EXPECT_TRUE(lifecycle.Hide());
  EXPECT_FALSE(lifecycle.Contains(Token(7)));
  EXPECT_EQ(-1, lifecycle.FamilyId());
}

}  // namespace
