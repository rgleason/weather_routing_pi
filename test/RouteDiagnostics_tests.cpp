// SPDX-License-Identifier: GPL-3.0-or-later
#include <gtest/gtest.h>
#include "RouteDiagnostics.h"

namespace {
namespace wr = supercpn::weather_routing;
using weather_routing::DiagnosticSeverity;
using weather_routing::RouteDiagnosticReport;
const weather_routing::RouteDiagnostic* Find(
    const RouteDiagnosticReport& report, const std::string& code) {
  for (const auto& item : report.items) if (item.code == code) return &item;
  return nullptr;
}
wr::RoutingResult Completed() {
  wr::RoutingResult result;
  result.status = wr::RoutingStatus::Complete;
  result.validation.passed = true;
  result.metrics.elapsed = wr::Duration{10800};
  for (int n = 0; n < 3; ++n) {
    wr::RouteLeg leg;
    leg.start = {48.0 + n * .1, -125};
    leg.end = {48.0 + (n + 1) * .1, -125};
    leg.startTime = wr::TimePoint{wr::Duration{1782367200 + n * 3600}};
    leg.endTime = leg.startTime + wr::Duration{3600};
    leg.windSource.source = wr::EnvironmentalSource::GribForecast;
    leg.currentSource.source = wr::EnvironmentalSource::NoDataAssumedZero;
    leg.currentSource.datasetIdentity = "current-disabled";
    leg.waves.available = true;
    result.legs.push_back(leg);
  }
  return result;
}
wr::ParameterCoverage Forecast() {
  return {true, {}, {}, {-158, 21, -126, 49}, "Pacific forecast"};
}
}  // namespace

TEST(RouteDiagnostics, RickGetsSelectionAndProviderCauseRatherThanGenericWind) {
  const auto report = weather_routing::CheckWindSourceSelection(false, true, false);
  ASSERT_TRUE(report.HasErrors());
  const auto* cause = Find(report, "setup.wind_source");
  ASSERT_NE(cause, nullptr);
  EXPECT_NE(cause->detail.Find("GRIB is not selected"), wxNOT_FOUND);
  EXPECT_NE(cause->detail.Find("provider is unavailable"), wxNOT_FOUND);
  EXPECT_NE(cause->action.Find("Data Source"), wxNOT_FOUND);
}
TEST(RouteDiagnostics, DeliberatelyDisabledSourcesAndAvailableClimatologyDiffer) {
  EXPECT_TRUE(weather_routing::CheckWindSourceSelection(false, false, false).HasErrors());
  EXPECT_FALSE(weather_routing::CheckWindSourceSelection(false, true, true).HasErrors());
  EXPECT_FALSE(weather_routing::CheckWindSourceSelection(true, false, false).HasErrors());
  const auto report = weather_routing::CheckWindSourceSelection(true, true, false);
  EXPECT_FALSE(report.HasErrors());
  EXPECT_EQ(report.Count(DiagnosticSeverity::Warning), 1U);
}
TEST(RouteDiagnostics, PierreDepartureCoverageIsSeparateFromDestinationProblem) {
  const auto report = weather_routing::CheckForecastWindCoverage(
      Forecast(), {48.433333, -124.75}, {21.3, -157.866667},
      wr::TimePoint{wr::Duration{1782367200}}, false);
  EXPECT_TRUE(report.HasErrors());
  const auto* cause = Find(report, "coverage.start_outside_wind");
  ASSERT_NE(cause, nullptr);
  EXPECT_NE(cause->detail.Find("-124.75000"), wxNOT_FOUND);
  EXPECT_NE(cause->detail.Find("-126.00000"), wxNOT_FOUND);
  EXPECT_EQ(Find(report, "coverage.destination_outside_wind"), nullptr);
}
TEST(RouteDiagnostics, AvailablePermittedFallbackMakesCoverageGapWarning) {
  const auto report = weather_routing::CheckForecastWindCoverage(
      Forecast(), {48.4, -124.75}, {21.3, -157.8}, {}, true);
  EXPECT_FALSE(report.HasErrors());
  EXPECT_EQ(report.Count(DiagnosticSeverity::Warning), 1U);
  ASSERT_NE(Find(report, "coverage.start_outside_wind"), nullptr);
}
TEST(RouteDiagnostics, UnknownForecastRangeNeverBecomesInventedEndTime) {
  const auto report = weather_routing::CheckForecastWindCoverage(
      Forecast(), {48, -127}, {22, -157}, {}, false);
  EXPECT_FALSE(report.HasErrors());
  ASSERT_NE(Find(report, "coverage.time_unknown"), nullptr);
  EXPECT_EQ(Find(report, "coverage.forecast_end"), nullptr);
}
TEST(RouteDiagnostics, ForecastMetadataChecksDepartureAndReportsKnownEnd) {
  auto coverage = Forecast();
  coverage.begins = wr::TimePoint{wr::Duration{1000}};
  coverage.ends = wr::TimePoint{wr::Duration{2000}};
  const auto report = weather_routing::CheckForecastWindCoverage(
      coverage, {48, -127}, {22, -157}, wr::TimePoint{wr::Duration{2100}}, false);
  EXPECT_NE(Find(report, "coverage.departure_time"), nullptr);
  EXPECT_NE(Find(report, "coverage.forecast_end"), nullptr);
  EXPECT_EQ(Find(report, "coverage.time_unknown"), nullptr);
}
TEST(RouteDiagnostics, CoverageCrossingDatelineNormalizesLongitudes) {
  auto coverage = Forecast();
  coverage.area = {170, -30, -170, 30};
  EXPECT_FALSE(weather_routing::CheckForecastWindCoverage(
      coverage, {0, 175}, {0, 185}, {}, false).HasErrors());
  EXPECT_TRUE(weather_routing::CheckForecastWindCoverage(
      coverage, {0, 175}, {0, -150}, {}, false).HasErrors());
}
TEST(RouteDiagnostics, MissingFrameDoesNotInventMissingParameterOrFileCause) {
  const auto report = weather_routing::CheckForecastWindCoverage({}, {1,2}, {3,4}, {}, false);
  const auto* cause = Find(report, "coverage.wind_unavailable");
  ASSERT_NE(cause, nullptr);
  EXPECT_NE(cause->detail.Find("does not identify"), wxNOT_FOUND);
}
TEST(RouteDiagnostics, CompleteForecastRouteHasNoFalseFallbackOrZeroCurrentWarning) {
  const auto report = weather_routing::DescribeRoutingResult(Completed());
  EXPECT_TRUE(report.complete);
  EXPECT_EQ(report.Count(DiagnosticSeverity::Warning), 0U);
  EXPECT_EQ(Find(report, "usage.climatology_wind"), nullptr);
  EXPECT_EQ(Find(report, "usage.zero_current"), nullptr);
}
TEST(RouteDiagnostics, ActualClimatologyIntervalsKeepDurationAndDoNotBridgeGaps) {
  auto result = Completed();
  result.legs[0].windSource.source = wr::EnvironmentalSource::Climatology;
  result.legs[2].windSource.source = wr::EnvironmentalSource::Climatology;
  const auto report = weather_routing::DescribeRoutingResult(result);
  const auto* warning = Find(report, "usage.climatology_wind");
  ASSERT_NE(warning, nullptr);
  EXPECT_NE(warning->detail.Find("2 h 00 min"), wxNOT_FOUND);
  EXPECT_NE(warning->detail.Find("66.7%"), wxNOT_FOUND);
  EXPECT_NE(warning->detail.Find("06:00:00"), wxNOT_FOUND);
  EXPECT_NE(warning->detail.Find("08:00:00"), wxNOT_FOUND);
  EXPECT_NE(warning->detail.Find("09:00:00"), wxNOT_FOUND);
  EXPECT_NE(warning->action.Find("historical"), wxNOT_FOUND);
}
TEST(RouteDiagnostics, MissingCurrentIsDifferentFromDisabledOrModelledZero) {
  auto result = Completed();
  result.legs[1].currentSource.datasetIdentity = "explicit-zero-current";
  result.legs[2].currentSource.source = wr::EnvironmentalSource::GribForecast;
  const auto report = weather_routing::DescribeRoutingResult(result);
  ASSERT_NE(Find(report, "usage.zero_current"), nullptr);
  EXPECT_NE(Find(report, "usage.zero_current")->detail.Find("1 h 00 min"), wxNOT_FOUND);
}
TEST(RouteDiagnostics, MissingWavesAreUnknownAndLimitNotChecked) {
  auto result = Completed();
  result.legs[0].waves.available = false;
  const auto report = weather_routing::DescribeRoutingResult(result);
  const auto* warning = Find(report, "usage.missing_waves");
  ASSERT_NE(warning, nullptr);
  EXPECT_NE(warning->action.Find("could not be checked"), wxNOT_FOUND);
  EXPECT_NE(warning->detail.Find("33.3%"), wxNOT_FOUND);
}
TEST(RouteDiagnostics, EstimatesAndWaitingRemainWarningsOnSuccessfulRoute) {
  auto result = Completed();
  result.legs[1].waves.nearbyHeightEstimate = true;
  result.legs[2].stationaryWait = true;
  result.warnings.push_back({wr::RoutingWarningCode::EstimatedPolar, "estimated profile"});
  result.warnings.push_back({wr::RoutingWarningCode::CoastalEndpointLeeway, "destination"});
  const auto report = weather_routing::DescribeRoutingResult(result);
  EXPECT_TRUE(report.complete);
  EXPECT_NE(Find(report, "usage.estimated_waves"), nullptr);
  EXPECT_NE(Find(report, "usage.waiting"), nullptr);
  EXPECT_EQ(report.Count(DiagnosticSeverity::Warning), 4U);
}
TEST(RouteDiagnostics, SuccessfulSearchPruningIsNoticeRatherThanFailedRoute) {
  auto result = Completed();
  result.warnings.push_back({wr::RoutingWarningCode::SearchPruned, "bounded"});
  const auto report = weather_routing::DescribeRoutingResult(result);
  EXPECT_TRUE(report.complete);
  EXPECT_FALSE(report.HasErrors());
  EXPECT_EQ(report.Count(DiagnosticSeverity::Notice), 1U);
}
TEST(RouteDiagnostics, EveryRequiredActionSurvivesPreflightWithDepthNamedCorrectly) {
  wr::RoutingResult result;
  result.status = wr::RoutingStatus::InvalidPolar;
  result.preflight.emplace();
  result.preflight->requiredActions = {wr::RequiredUserAction::LoadSailingPolar,
      wr::RequiredUserAction::LoadWeatherGrib, wr::RequiredUserAction::LoadDepthData};
  const auto report = weather_routing::DescribeRoutingResult(result);
  EXPECT_EQ(report.Count(DiagnosticSeverity::Error), 3U);
  EXPECT_NE(report.Text().Find("charted depth"), wxNOT_FOUND);
  EXPECT_NE(report.Text().Find("polar"), wxNOT_FOUND);
}
TEST(RouteDiagnostics, FailedAttemptsStayVisibleAlongsideSuccessfulWinner) {
  auto result = Completed();
  result.attemptFailures.push_back({"Quick", wr::RoutingStatus::CurrentDataRequired, "current unavailable"});
  result.engineAttempts = result.attemptFailures;
  result.engineAttempts.push_back({"Standard", wr::RoutingStatus::Complete, "validated"});
  const auto report = weather_routing::DescribeRoutingResult(result);
  EXPECT_TRUE(report.complete);
  EXPECT_FALSE(report.HasErrors());
  EXPECT_NE(Find(report, "sequence.incomplete"), nullptr);
  EXPECT_NE(report.Text().Find("Standard"), wxNOT_FOUND);
  EXPECT_NE(report.Text().Find("current unavailable"), wxNOT_FOUND);
}
TEST(RouteDiagnostics, CancellationIsNotAnErrorAndRetainsUserAction) {
  wr::RoutingResult result;
  result.status = wr::RoutingStatus::Cancelled;
  const auto report = weather_routing::DescribeRoutingResult(result);
  EXPECT_TRUE(report.cancelled);
  EXPECT_FALSE(report.HasErrors());
  EXPECT_EQ(report.Count(DiagnosticSeverity::Warning), 0U);
  EXPECT_NE(report.Summary().Find("Stopped"), wxNOT_FOUND);
}
TEST(RouteDiagnostics, ResourceAndReplayFailuresRetainUnderlyingEvidence) {
  wr::RoutingResult result;
  result.status = wr::RoutingStatus::ResourceLimitReached;
  result.diagnostics.resourceLimitEvents.push_back("generated limit 1200");
  result.diagnostics.stageStopReasons.push_back("forward allowance exhausted");
  const auto resource = weather_routing::DescribeRoutingResult(result);
  EXPECT_NE(resource.Text().Find("generated limit 1200"), wxNOT_FOUND);
  EXPECT_NE(resource.Text().Find("does not establish"), wxNOT_FOUND);
  result.status = wr::RoutingStatus::ValidationFailure;
  result.validation.failureReason = "leg 2 trajectory missed by 0.3 NM";
  EXPECT_NE(weather_routing::DescribeRoutingResult(result).Text().Find("0.3 NM"), wxNOT_FOUND);
}
TEST(RouteDiagnostics, DiagnosticsDeduplicateIdenticalFactsButKeepDistinctScopes) {
  RouteDiagnosticReport report;
  const weather_routing::RouteDiagnostic issue{"coverage", DiagnosticSeverity::Warning, "missing", "point A", {}, "Quick"};
  report.Add(issue); report.Add(issue);
  auto other = issue; other.engine = "Atlas"; report.Add(other);
  other.detail = "point B"; report.Add(other);
  EXPECT_EQ(report.items.size(), 3U);
}
TEST(RouteDiagnostics, UTCFormattingDoesNotDependOnDisplayTimeZone) {
  EXPECT_EQ(weather_routing::DiagnosticTimeUtc(
      wr::TimePoint{wr::Duration{1782367200}}), "2026-06-25 06:00:00");
}
TEST(RouteDiagnostics, FragmentedUsageSummaryIsBounded) {
  auto result = Completed();
  result.legs.clear();
  result.metrics.elapsed = wr::Duration{60 * 3600};
  for (int n = 0; n < 60; ++n) {
    auto leg = Completed().legs[0];
    leg.startTime += wr::Duration{n * 3600};
    leg.endTime += wr::Duration{n * 3600};
    leg.windSource.source = n % 2 ? wr::EnvironmentalSource::GribForecast : wr::EnvironmentalSource::Climatology;
    result.legs.push_back(leg);
  }
  const auto report = weather_routing::DescribeRoutingResult(result);
  ASSERT_NE(Find(report, "usage.climatology_wind"), nullptr);
  EXPECT_NE(Find(report, "usage.climatology_wind")->detail.Find("omitted"), wxNOT_FOUND);
  EXPECT_LT(report.Text().size(), 10000U);
}

TEST(RouteDiagnostics, MissingWavesDoNotClaimDisabledCeilingWasEnforced) {
  auto result = Completed();
  result.legs[0].waves.available = false;
  const auto report = weather_routing::DescribeRoutingResult(result, false);
  ASSERT_NE(Find(report, "usage.missing_waves"), nullptr);
  EXPECT_EQ(Find(report, "usage.missing_waves")->action.Find("limit"), wxNOT_FOUND);
}
TEST(RouteDiagnostics, UnvalidatedCompleteStatusCannotBecomeSuccess) {
  auto result = Completed();
  result.validation.passed = false;
  const auto report = weather_routing::DescribeRoutingResult(result);
  EXPECT_FALSE(report.complete);
  EXPECT_TRUE(report.HasErrors());
  EXPECT_EQ(report.status, wr::RoutingStatus::ValidationFailure);
  EXPECT_NE(report.Summary().Find("final check"), wxNOT_FOUND);
}
TEST(RouteDiagnostics, ActualClimatologyCurrentIsNamedSeparatelyFromOtherPredictions) {
  auto result = Completed();
  result.legs[0].currentSource.source = wr::EnvironmentalSource::XtdCurrentPrediction;
  result.legs[0].currentSource.modelIdentity = "OpenCPN climatology current";
  result.legs[1].currentSource.source = wr::EnvironmentalSource::XtdCurrentPrediction;
  result.legs[1].currentSource.modelIdentity = "separate prediction model";
  const auto report = weather_routing::DescribeRoutingResult(result);
  EXPECT_NE(Find(report, "usage.climatology_current"), nullptr);
  EXPECT_NE(Find(report, "usage.alternative_current"), nullptr);
}

TEST(RouteDiagnostics, MixedEngineFailuresDoNotMakeLastEnginesCauseTheSharedCause) {
  wr::RoutingResult result;
  result.status = wr::RoutingStatus::WindForecastRequired;
  result.attemptFailures = {{"Quick", wr::RoutingStatus::InvalidDestination, "destination is on land"},
      {"Standard", wr::RoutingStatus::WindForecastRequired, "wind missing"}};
  const auto report = weather_routing::DescribeRoutingResult(result);
  EXPECT_EQ(report.status, wr::RoutingStatus::NoFeasibleRoute);
  EXPECT_NE(report.Text().Find("destination is on land"), wxNOT_FOUND);
  EXPECT_NE(report.Text().Find("wind missing"), wxNOT_FOUND);
}
TEST(RouteDiagnostics, SharedMissingWindCauseRemainsSpecificAcrossEngines) {
  wr::RoutingResult result;
  result.status = wr::RoutingStatus::WindForecastRequired;
  result.attemptFailures = {{"Quick", wr::RoutingStatus::WindForecastRequired, "wind missing"},
      {"Standard", wr::RoutingStatus::WindForecastRequired, "wind missing"}};
  EXPECT_EQ(weather_routing::DescribeRoutingResult(result).status,
            wr::RoutingStatus::WindForecastRequired);
}

TEST(RouteDiagnostics, LaterSafetyFailureCannotLeaveACompleteDiagnostic) {
  auto report = weather_routing::DescribeRoutingResult(Completed());
  report = weather_routing::ReconcileRouteDiagnostics(report, {}, "Chart check rejected leg 2", {}, false);
  EXPECT_FALSE(report.complete);
  EXPECT_EQ(report.status, wr::RoutingStatus::ValidationFailure);
  EXPECT_NE(report.Text().Find("Chart check rejected leg 2"), wxNOT_FOUND);
}
TEST(RouteDiagnostics, NativeFailureSummaryIsNotDuplicatedAsAnotherError) {
  wr::RoutingResult result;
  result.status = wr::RoutingStatus::WindForecastRequired;
  const auto original = weather_routing::DescribeRoutingResult(result);
  const auto report = weather_routing::ReconcileRouteDiagnostics(original, {}, original.Summary(), {}, false);
  EXPECT_EQ(report.Count(DiagnosticSeverity::Error), original.Count(DiagnosticSeverity::Error));
}
TEST(RouteDiagnostics, LegacyPreflightFailureAndWeatherErrorAllSurvive) {
  const auto report = weather_routing::ReconcileRouteDiagnostics({}, "Bad boat", "No feasible heading", "Wind unavailable at UTC time", false);
  EXPECT_EQ(report.Count(DiagnosticSeverity::Error), 3U);
  EXPECT_NE(report.Text().Find("Bad boat"), wxNOT_FOUND);
  EXPECT_NE(report.Text().Find("No feasible heading"), wxNOT_FOUND);
  EXPECT_NE(report.Text().Find("Wind unavailable"), wxNOT_FOUND);
}
TEST(RouteDiagnostics, AllocationCancellationIsReportedAsMemoryRatherThanUserStop) {
  wr::RoutingResult result;
  result.status = wr::RoutingStatus::Cancelled;
  const auto report = weather_routing::ReconcileRouteDiagnostics(
      weather_routing::DescribeRoutingResult(result), {}, "Allocation failed during caching", {}, true);
  EXPECT_FALSE(report.cancelled);
  EXPECT_EQ(report.status, wr::RoutingStatus::ResourceLimitReached);
  EXPECT_EQ(report.Summary(), "Memory allocation failed");
}
