#include <gtest/gtest.h>
#include <cmath>
#include <cstring>
#include <limits>
#include "RouteSafetyCheck.h"
#include "ChartLongitude.h"

using namespace weather_routing;
namespace {
std::vector<SafetyCheckWaypoint> Route() {
  return {{"a", "Départ", 53, -5}, {"b", "Arrival", 53, -4.98}};
}
PlugInSegmentSafetyOptions Options(double depth = 0, double margin = 0) {
  PlugInSegmentSafetyOptions result{};
  result.minimum_depth_m = depth; result.safety_margin_nm = margin;
  return result;
}
auto Answer(int status, int source = PI_SEGMENT_SAFETY_SOURCE_PLUGIN_VECTOR) {
  return [=](double, double, double, double, const PlugInSegmentSafetyOptions*,
             PlugInSegmentSafetyResult* result) {
    result->status = status; result->source = source; return true;
  };
}
void Finish(RouteSafetyCheck& check) {
  for (int i = 0; i < 10000 && check.State() == RouteCheckState::Running; ++i) check.Advance();
}

TEST(RouteSafetyCheck, ChecksEveryWholeSegmentWithoutChangingRoute) {
  const auto input = Route();
  std::size_t calls = 0;
  double previous_lon = input.front().lon;
  RouteSafetyCheck check(input, Options(3, .1),
      [&](double a, double b, double c, double d, const auto* options, auto* result) {
    ++calls;
    EXPECT_NEAR(a, 53, 1e-10); EXPECT_NEAR(c, 53, 1e-10);
    EXPECT_NEAR(b, previous_lon, 1e-10); EXPECT_GT(d, b); previous_lon = d;
    EXPECT_EQ(options->struct_size, sizeof(*options));
    EXPECT_EQ(options->check_land, 1); EXPECT_EQ(options->check_depth, 1);
    EXPECT_EQ(options->allow_gshhs_fallback, 0);
    EXPECT_EQ(options->force_authoritative_fine_validation, 1);
    EXPECT_DOUBLE_EQ(options->minimum_depth_m, 3);
    EXPECT_DOUBLE_EQ(options->safety_margin_nm, .1);
    result->status = PI_SEGMENT_SAFETY_SAFE;
    result->source = PI_SEGMENT_SAFETY_SOURCE_PLUGIN_VECTOR;
    return true;
  });
  EXPECT_FALSE(check.Clear());
  Finish(check);
  EXPECT_TRUE(check.Clear()); EXPECT_EQ(calls, check.Total());
  EXPECT_NEAR(previous_lon, input.back().lon, 1e-10);
  EXPECT_EQ(input, Route()); EXPECT_TRUE(check.Matches(input));
}

TEST(RouteSafetyCheck, DepthZeroExplicitlyDisablesDepth) {
  RouteSafetyCheck check(Route(), Options(), [](double, double, double, double, const auto* o, auto* r) {
    EXPECT_FALSE(o->check_depth); r->status = PI_SEGMENT_SAFETY_SAFE;
    r->source = PI_SEGMENT_SAFETY_SOURCE_VECTOR_CHART; return true;
  });
  Finish(check); EXPECT_TRUE(check.Clear());
}

TEST(RouteSafetyCheck, InteriorLandHitIsFoundEvenWhenEndpointsAreClear) {
  RouteSafetyCheck check(Route(), Options(), [](double, double lon1, double, double lon2, const auto*, auto* r) {
    r->status = lon1 <= -4.99 && lon2 >= -4.99 ? PI_SEGMENT_SAFETY_CROSSES_LAND : PI_SEGMENT_SAFETY_SAFE;
    r->source = PI_SEGMENT_SAFETY_SOURCE_CM93; return true;
  });
  Finish(check); EXPECT_FALSE(check.Clear()); EXPECT_GE(check.HazardChunks(), 1);
  ASSERT_EQ(check.Findings().size(), 1); EXPECT_EQ(check.Findings()[0].leg, 1);
}

class RouteCheckStatus : public testing::TestWithParam<int> {};
TEST_P(RouteCheckStatus, HazardAndUnknownStatusesNeverPass) {
  RouteSafetyCheck check(Route(), Options(3), Answer(GetParam()));
  Finish(check); EXPECT_EQ(check.State(), RouteCheckState::Complete);
  EXPECT_FALSE(check.Clear()); ASSERT_FALSE(check.Findings().empty());
  EXPECT_EQ(check.Findings()[0].status, GetParam());
  if (RouteSafetyCheck::IsUnverified(GetParam())) EXPECT_EQ(check.UnverifiedChunks(), check.Total());
  else EXPECT_EQ(check.HazardChunks(), check.Total());
}
INSTANTIATE_TEST_SUITE_P(AllReasons, RouteCheckStatus, testing::Values(
    PI_SEGMENT_SAFETY_CROSSES_LAND, PI_SEGMENT_SAFETY_WITHIN_LAND_MARGIN,
    PI_SEGMENT_SAFETY_UNSAFE_AREA, PI_SEGMENT_SAFETY_DRYING_AREA,
    PI_SEGMENT_SAFETY_TOO_SHALLOW, PI_SEGMENT_SAFETY_UNKNOWN_DEPTH,
    PI_SEGMENT_SAFETY_NO_DATA, PI_SEGMENT_SAFETY_ERROR));

TEST(RouteSafetyCheck, FailedAndUnrecognizedQueriesBecomeUnverified) {
  for (int mode : {0, 1}) {
    RouteSafetyCheck check(Route(), Options(), [=](double, double, double, double, const auto*, auto* r) {
      r->status = mode ? 9999 : PI_SEGMENT_SAFETY_SAFE;
      return mode != 0;
    });
    Finish(check); EXPECT_FALSE(check.Clear()); EXPECT_EQ(check.UnverifiedChunks(), check.Total());
    EXPECT_EQ(check.Findings()[0].status, PI_SEGMENT_SAFETY_ERROR);
  }
}

TEST(RouteSafetyCheck, FallbackAndAbsentSourcesCannotCertifyChartCoverage) {
  for (int source : {PI_SEGMENT_SAFETY_SOURCE_GSHHS_FALLBACK, PI_SEGMENT_SAFETY_SOURCE_NONE}) {
    RouteSafetyCheck check(Route(), Options(), Answer(PI_SEGMENT_SAFETY_SAFE, source));
    Finish(check); EXPECT_FALSE(check.Clear()); EXPECT_EQ(check.UnverifiedChunks(), check.Total());
  }
  RouteSafetyCheck check(Route(), Options(), [](double, double, double, double, const auto*, auto* r) {
    r->status = PI_SEGMENT_SAFETY_SAFE; r->source = PI_SEGMENT_SAFETY_SOURCE_VECTOR_CHART;
    r->used_fallback = 1; return true;
  });
  Finish(check); EXPECT_FALSE(check.Clear());
}

TEST(RouteSafetyCheck, PendingChartsAreRetriedBeforeAnyClaim) {
  int queries = 0;
  RouteSafetyCheck check(Route(), Options(), [&](double, double, double, double, const auto*, auto* r) {
    r->status = ++queries < 3 ? PI_SEGMENT_SAFETY_PENDING_DATA : PI_SEGMENT_SAFETY_SAFE;
    r->source = PI_SEGMENT_SAFETY_SOURCE_VECTOR_CHART; return true;
  });
  check.Advance(); EXPECT_EQ(check.Checked(), 0); EXPECT_FALSE(check.Clear());
  check.Advance(); EXPECT_EQ(check.Checked(), 0);
  Finish(check); EXPECT_TRUE(check.Clear()); EXPECT_EQ(queries, check.Total() + 2);
}

TEST(RouteSafetyCheck, PermanentlyPendingChartsEventuallyProduceUnverifiedReport) {
  RouteSafetyCheck check(Route(), Options(), Answer(PI_SEGMENT_SAFETY_PENDING_DATA));
  Finish(check); EXPECT_EQ(check.State(), RouteCheckState::Complete); EXPECT_FALSE(check.Clear());
  EXPECT_EQ(check.UnverifiedChunks(), check.Total());
}

TEST(RouteSafetyCheck, PendingDiagnosticCannotCertifyAnOtherwiseSafeSection) {
  RouteSafetyCheck check(Route(), Options(), [](double, double, double, double, const auto*, auto* r) {
    r->status = PI_SEGMENT_SAFETY_SAFE;
    r->source = PI_SEGMENT_SAFETY_SOURCE_PLUGIN_VECTOR;
    r->diagnostic_reason = PI_SEGMENT_SAFETY_DIAG_PENDING_DATA;
    return true;
  });
  Finish(check); EXPECT_EQ(check.State(), RouteCheckState::Complete);
  EXPECT_FALSE(check.Clear()); EXPECT_EQ(check.UnverifiedChunks(), check.Total());
}

TEST(RouteSafetyCheck, HighLatitudeRhumbChunksRespectDistanceLimit) {
  RouteSafetyCheck check({{"a", "Start", 60, 0}, {"b", "End", 85, 30}}, Options(),
      [](double lat1, double lon1, double lat2, double lon2, const auto*, auto* r) {
    constexpr double pi = 3.14159265358979323846;
    const double dphi = (lat2 - lat1) * pi / 180;
    const double dpsi = std::log(std::tan(pi / 4 + lat2 * pi / 360) /
                                 std::tan(pi / 4 + lat1 * pi / 360));
    const double q = std::abs(dpsi) > 1e-12 ? dphi / dpsi : std::cos(lat1 * pi / 180);
    const double distance = std::hypot(dphi, q * (lon2 - lon1) * pi / 180) * 3440.065;
    EXPECT_LE(distance, .25000001);
    r->status = PI_SEGMENT_SAFETY_SAFE;
    r->source = PI_SEGMENT_SAFETY_SOURCE_VECTOR_CHART; return true;
  });
  while (check.State() == RouteCheckState::Running) check.Advance();
  EXPECT_TRUE(check.Clear());
}

TEST(RouteSafetyCheck, CancelLeavesPartialReportAndDoesNotRunAnotherQuery) {
  int queries = 0;
  RouteSafetyCheck check(Route(), Options(), [&](double, double, double, double, const auto*, auto* r) {
    ++queries; r->status = PI_SEGMENT_SAFETY_SAFE; r->source = PI_SEGMENT_SAFETY_SOURCE_VECTOR_CHART; return true;
  });
  check.Advance(); check.Cancel(); check.Advance();
  EXPECT_EQ(check.State(), RouteCheckState::Cancelled); EXPECT_EQ(queries, 1);
  EXPECT_FALSE(check.Clear()); EXPECT_LT(check.Checked(), check.Total());
}

TEST(RouteSafetyCheck, EditedReorderedOrRemovedWaypointsInvalidateSnapshot) {
  RouteSafetyCheck check(Route(), Options(), Answer(PI_SEGMENT_SAFETY_SAFE));
  Finish(check); ASSERT_TRUE(check.Clear());
  for (int mode = 0; mode < 5; ++mode) {
    auto points = Route();
    if (mode == 0) points[0].lon += .001;
    if (mode == 1) points[0].guid = "new";
    if (mode == 2) points[0].name = "Edited";
    if (mode == 3) std::swap(points[0], points[1]);
    if (mode == 4) points.pop_back();
    EXPECT_FALSE(check.Matches(points));
  }
  check.Invalidate("Chart group changed"); EXPECT_FALSE(check.Clear());
  EXPECT_EQ(check.State(), RouteCheckState::Outdated);
}

TEST(RouteSafetyCheck, AdjacentFindingsMergeButClearGapsAndLegsDoNot) {
  auto points = Route(); points.push_back({"c", "End", 53, -4.96});
  int n = 0;
  RouteSafetyCheck check(points, Options(), [&](double, double, double, double, const auto*, auto* r) {
    r->status = ++n == 2 ? PI_SEGMENT_SAFETY_SAFE : PI_SEGMENT_SAFETY_CROSSES_LAND;
    r->source = PI_SEGMENT_SAFETY_SOURCE_VECTOR_CHART; return true;
  });
  Finish(check); ASSERT_EQ(check.Findings().size(), 3);
  EXPECT_EQ(check.Findings()[0].leg, 1); EXPECT_EQ(check.Findings()[1].leg, 1);
  EXPECT_EQ(check.Findings()[2].leg, 2);
  EXPECT_LT(check.Findings()[0].to_fraction, check.Findings()[1].from_fraction);
}

TEST(RouteSafetyCheck, KnownDepthUsesMinimumAndUnknownDepthIsNotInvented) {
  double depth = 3;
  RouteSafetyCheck check(Route(), Options(5), [&](double, double, double, double, const auto*, auto* r) {
    r->status = PI_SEGMENT_SAFETY_TOO_SHALLOW; r->source = PI_SEGMENT_SAFETY_SOURCE_PLUGIN_VECTOR;
    r->has_depth = 1; r->min_depth_m = depth; depth -= .1; return true;
  });
  Finish(check); ASSERT_EQ(check.Findings().size(), 1);
  EXPECT_NEAR(check.Findings()[0].minimum_depth_m, 3 - (check.Total() - 1) * .1, 1e-10);
  RouteSafetyCheck unknown(Route(), Options(5), Answer(PI_SEGMENT_SAFETY_UNKNOWN_DEPTH));
  Finish(unknown); EXPECT_FALSE(unknown.Findings()[0].has_depth);
}

TEST(RouteSafetyCheck, UsesReportedHitAndFallsBackToCheckedSectionPosition) {
  RouteSafetyCheck check(Route(), Options(), [](double, double, double, double, const auto*, auto* r) {
    r->status = PI_SEGMENT_SAFETY_CROSSES_LAND; r->source = PI_SEGMENT_SAFETY_SOURCE_CM93;
    r->hit_sample_count = 1; r->hit_sample_lat = 53.001; r->hit_sample_lon = 355.01;
    return true;
  });
  Finish(check); EXPECT_DOUBLE_EQ(check.Findings()[0].lat, 53.001);
  EXPECT_NEAR(check.Findings()[0].lon, -4.99, 1e-10);
  RouteSafetyCheck unknown(Route(), Options(), Answer(PI_SEGMENT_SAFETY_NO_DATA));
  Finish(unknown); EXPECT_NEAR(unknown.Findings()[0].lat, 53, 1e-10);
  EXPECT_GT(unknown.Findings()[0].lon, -5); EXPECT_LT(unknown.Findings()[0].lon, -4.98);
}

TEST(RouteSafetyCheck, DatelineCrossingTakesShortLocalPath) {
  std::size_t calls = 0;
  RouteSafetyCheck check({{"a", "West", 10, 179.99}, {"b", "East", 10, -179.99}}, Options(),
      [&](double, double lon1, double, double lon2, const auto*, auto* r) {
    ++calls;
    EXPECT_LT(std::abs(CanonicalChartLongitude(lon2 - lon1)), .01);
    r->status = PI_SEGMENT_SAFETY_SAFE; r->source = PI_SEGMENT_SAFETY_SOURCE_VECTOR_CHART; return true;
  });
  Finish(check); EXPECT_TRUE(check.Clear()); EXPECT_LT(calls, 10);
}

TEST(RouteSafetyCheck, ZeroLengthLegStillChecksItsWaypoint) {
  auto points = Route(); points[1].lat = points[0].lat; points[1].lon = points[0].lon;
  RouteSafetyCheck check(points, Options(), Answer(PI_SEGMENT_SAFETY_CROSSES_LAND));
  EXPECT_EQ(check.Total(), 1); Finish(check); EXPECT_FALSE(check.Clear());
}

TEST(RouteSafetyCheck, InvalidCoordinatesAndThresholdsNeverCallHost) {
  int calls = 0;
  const auto query = [&](double, double, double, double, const auto*, auto*) { ++calls; return true; };
  for (double bad : {90., -90., std::numeric_limits<double>::quiet_NaN()}) {
    auto points = Route(); points[0].lat = bad;
    RouteSafetyCheck check(points, Options(), query); Finish(check);
    EXPECT_EQ(check.State(), RouteCheckState::Invalid); EXPECT_FALSE(check.Clear());
  }
  for (auto options : {Options(-1), Options(0, -1), Options(std::numeric_limits<double>::infinity())}) {
    RouteSafetyCheck check(Route(), options, query); Finish(check);
    EXPECT_EQ(check.State(), RouteCheckState::Invalid);
  }
  RouteSafetyCheck empty({}, Options(), query); Finish(empty);
  EXPECT_EQ(calls, 0);
}

TEST(RouteSafetyCheck, OversizedRouteStopsBeforeAllocatingUnboundedWork) {
  std::vector<SafetyCheckWaypoint> points;
  for (int i = 0; i < 20; ++i) points.push_back({"id", "Point", 0, i % 2 ? 179. : 0.});
  RouteSafetyCheck check(points, Options(), Answer(PI_SEGMENT_SAFETY_SAFE));
  EXPECT_EQ(check.State(), RouteCheckState::Invalid); EXPECT_FALSE(check.Error().empty());
}
}  // namespace
