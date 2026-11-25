#include <gtest/gtest.h>

#include <limits>
#include <vector>

#include "RoutingEndpointPreflight.h"

namespace {
using namespace weather_routing;

TEST(RoutingEndpointPreflight, LandIsRejectedBeforeAnyBufferedQuery) {
  std::vector<double> margins;
  const auto result = AssessRoutingEndpoint(.4, [&](double margin) {
    margins.push_back(margin);
    return RoutingEndpointProbe{EndpointSurface::Land, "GSHHG Full", "land"};
  });
  EXPECT_TRUE(result.Blocked());
  EXPECT_FALSE(result.WarnAboutMargin());
  EXPECT_EQ(margins, std::vector<double>{0.0});
  EXPECT_EQ(result.Evidence().source, "GSHHG Full");
}

TEST(RoutingEndpointPreflight, WaterInsideMarginWarnsAndRemainsAdmissible) {
  for (const auto hit : {EndpointSurface::Land, EndpointSurface::Drying,
                         EndpointSurface::Unsafe, EndpointSurface::Margin}) {
    std::vector<double> margins;
    const auto result = AssessRoutingEndpoint(.4, [&](double margin) {
      margins.push_back(margin);
      return RoutingEndpointProbe{margin == 0 ? EndpointSurface::Water : hit,
                                  "chart provider", "chart cell"};
    });
    EXPECT_FALSE(result.Blocked());
    EXPECT_TRUE(result.WarnAboutMargin());
    EXPECT_EQ(margins, (std::vector<double>{0.0, .4}));
    EXPECT_EQ(result.point.surface, EndpointSurface::Water);
  }
}

TEST(RoutingEndpointPreflight, ClearWaterDoesNotWarn) {
  const auto result = AssessRoutingEndpoint(.4, [](double) {
    return RoutingEndpointProbe{EndpointSurface::Water, "GSHHG Full", ""};
  });
  EXPECT_FALSE(result.Blocked());
  EXPECT_FALSE(result.WarnAboutMargin());
}

TEST(RoutingEndpointPreflight, ZeroMarginDoesNotInventAClearanceWarning) {
  unsigned calls = 0;
  const auto result = AssessRoutingEndpoint(0, [&](double margin) {
    ++calls;
    EXPECT_EQ(margin, 0);
    return RoutingEndpointProbe{EndpointSurface::Water, "chart provider", ""};
  });
  EXPECT_EQ(calls, 1u);
  EXPECT_FALSE(result.Blocked());
  EXPECT_FALSE(result.WarnAboutMargin());
}

TEST(RoutingEndpointPreflight, MissingEvidenceIsNotReportedAsLandOrClearWater) {
  for (bool missingPoint : {true, false}) {
    const auto result = AssessRoutingEndpoint(.4, [&](double margin) {
      return RoutingEndpointProbe{
          missingPoint || margin > 0 ? EndpointSurface::Unavailable
                                     : EndpointSurface::Water,
          "chart provider", "coverage unavailable"};
    });
    EXPECT_TRUE(result.Blocked());
    EXPECT_FALSE(result.WarnAboutMargin());
    EXPECT_EQ(result.Evidence().surface, EndpointSurface::Unavailable);
    EXPECT_EQ(result.Evidence().detail, "coverage unavailable");
  }
}

TEST(RoutingEndpointPreflight, DryingAndUnsafePointsRetainTheirOwnReason) {
  for (const auto surface : {EndpointSurface::Drying, EndpointSurface::Unsafe}) {
    const auto result = AssessRoutingEndpoint(.4, [=](double) {
      return RoutingEndpointProbe{surface, "ENC chart", "charted restriction"};
    });
    EXPECT_TRUE(result.Blocked());
    EXPECT_EQ(result.Evidence().surface, surface);
  }
}

TEST(RoutingEndpointPreflight, InvalidMarginCannotCertifyAnEndpoint) {
  for (double margin : {-1.0, std::numeric_limits<double>::quiet_NaN(),
                         std::numeric_limits<double>::infinity()}) {
    unsigned calls = 0;
    const auto result = AssessRoutingEndpoint(margin, [&](double) {
      ++calls;
      return RoutingEndpointProbe{EndpointSurface::Water, "", ""};
    });
    EXPECT_EQ(calls, 0u);
    EXPECT_TRUE(result.Blocked());
  }
}
}  // namespace
