// SPDX-License-Identifier: GPL-3.0-or-later
#include <gtest/gtest.h>
#include <iostream>
#include "Quick2Fixtures.h"
#include "RouteComfort.h"
#include "engine/native/RoutingEngineSequence.h"
#include "original_routing/Engine.h"
#include "supercpn/weather_routing/PartialRoutes.h"
#include "supercpn/weather_routing/QuickEngine.h"
#include "supercpn/weather_routing/AlternativeEngine.h"

namespace {
namespace wr = supercpn::weather_routing;
using namespace std::chrono_literals;
wr::RoutingResult Solve(int engine, const quick2_fixtures::Fixture& f) {
  if (engine == 0) {
    original_routing::Options options;
    if (f.request.limits.maximumRouteDuration <= 2h) options.maximumRuntime = 2s;
    return original_routing::Engine{}.route(f.request, f.environment, options);
  }
  if (engine == 1) return quick2_routing::Engine{}.route(f.request, f.environment).route;
  if (engine == 2) return wr::QuickRoutingEngine{}.route(f.request, f.environment).route;
  if (engine == 3) return wr::AlternativeRoutingEngine{}.route(f.request, f.environment).route;
  return wr::ProfessionalEngine{}.route(f.request, f.environment);
}
auto Archive(const wr::RoutingRequest& r, std::size_t bytes = 1024 * 1024) {
  return std::make_shared<wr::PartialRouteArchive>(r.start, r.destination,
      r.departure, r.limits.maximumRouteDuration, bytes);
}
void SameRoute(const wr::RoutingResult& a, const wr::RoutingResult& b) {
  ASSERT_EQ(a.status, b.status);
  ASSERT_EQ(a.legs.size(), b.legs.size());
  EXPECT_EQ(a.diagnostics.generatedStates, b.diagnostics.generatedStates);
  EXPECT_EQ(a.diagnostics.weatherSamples, b.diagnostics.weatherSamples);
  EXPECT_EQ(a.diagnostics.landChecks, b.diagnostics.landChecks);
  for (std::size_t i = 0; i < a.legs.size(); ++i) {
    EXPECT_EQ(a.legs[i].start, b.legs[i].start);
    EXPECT_EQ(a.legs[i].end, b.legs[i].end);
    EXPECT_EQ(a.legs[i].startTime.time_since_epoch().count(), b.legs[i].startTime.time_since_epoch().count());
    EXPECT_EQ(a.legs[i].endTime.time_since_epoch().count(), b.legs[i].endTime.time_since_epoch().count());
    EXPECT_EQ(a.legs[i].profileIdentity, b.legs[i].profileIdentity);
  }
}

TEST(PartialRoutes, CaptureDoesNotChangeSuccessfulEngineSearches) {
  for (int engine = 0; engine < 5; ++engine) {
    SCOPED_TRACE(engine);
    for (const std::string name : {"short", "dateline", "tidal", "island"}) {
      SCOPED_TRACE(name);
      auto f = quick2_fixtures::make(name);
      const auto control = Solve(engine, f);
      ASSERT_TRUE(weather_routing::native::AcceptedRoute(control)) << control.message;
      f.request.partials = Archive(f.request);
      const auto captured = Solve(engine, f);
      SameRoute(control, captured);
      EXPECT_LE(f.request.partials->peakBytes(), 1024U * 1024U);
      EXPECT_LE(f.request.partials->entries().size(), 16U);
    }
  }
}

TEST(PartialRoutes, FailedSearchSnapshotsOutliveAllFiveEngineArenas) {
  for (int engine = 0; engine < 5; ++engine) {
    SCOPED_TRACE(engine);
    auto f = quick2_fixtures::make("open");
    f.request.limits.maximumRouteDuration = 2h;
    f.request.limits.maximumGeneratedStates = 3000;
    f.request.limits.maximumRetainedStates = 10000;
    f.request.limits.maximumGraphLabels = 10000;
    // This fixture exercises prefix lifetime, not the legacy unlimited
    // reverse-bridge defaults. Give every recovery stage an explicit budget.
    f.request.limits.maximumReverseCandidates = 16;
    f.request.limits.maximumReverseBridgeAttempts = 4;
    f.request.partials = Archive(f.request);
    const auto started = std::chrono::steady_clock::now();
    const auto failed = Solve(engine, f);
    std::cerr << "Partial lifetime engine=" << engine << " search_ms=" <<
        std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - started).count() << '\n';
    ASSERT_FALSE(weather_routing::native::AcceptedRoute(failed));
    ASSERT_FALSE(f.request.partials->entries().empty()) << failed.message;
    unsigned passed = 0;
    unsigned replayed = 0;
    for (const auto& entry : f.request.partials->entries()) {
      if (replayed++ == 4) break;
      const auto prefix = wr::ValidatePartialRoute(f.request, f.environment, entry.trace);
      if (!prefix.validation.passed) continue;
      ++passed;
      EXPECT_FALSE(weather_routing::native::AcceptedRoute(prefix));
      ASSERT_FALSE(prefix.legs.empty());
      EXPECT_LE(prefix.metrics.elapsed.count(), f.request.limits.maximumRouteDuration.count());
      EXPECT_LT(wr::distanceNm(prefix.legs.back().end, f.request.destination),
                wr::distanceNm(f.request.start, f.request.destination));
    }
    EXPECT_GT(passed, 0U);
    std::cerr << "Partial lifetime engine=" << engine << " total_ms=" <<
        std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - started).count() << '\n';
  }
}

TEST(PartialRoutes, ArchiveBoundsMemoryAndContainsOptionalCaptureFailures) {
  auto f = quick2_fixtures::make("open");
  auto a = Archive(f.request, 1000);
  const auto end = wr::destinationPoint(f.request.start, 90, 1);
  a->offer(end, f.request.departure + 1h, "throws", []() -> wr::PartialRouteTrace {
    throw std::runtime_error("optional capture failure");
  });
  a->offer(end, f.request.departure + 1h, "oversized", [] {
    return wr::PartialRouteTrace{1001, [](const auto&, const auto&, auto&) { return std::vector<wr::RouteLeg>{}; }};
  });
  EXPECT_TRUE(a->entries().empty());
  EXPECT_EQ(a->rejected(), 2U);
  for (int i = 1; i < 100; ++i)
    a->offer(wr::destinationPoint(f.request.start, 90, i / 3.), f.request.departure + 1h,
        "bounded", [] {
          return wr::PartialRouteTrace{200, [](const auto&, const auto&, auto&) { return std::vector<wr::RouteLeg>{}; }};
        });
  EXPECT_LE(a->bytes(), 1000U); EXPECT_LE(a->peakBytes(), 1000U);
  EXPECT_LE(a->entries().size(), 5U);
}

TEST(PartialRoutes, LaterSequenceFailuresDoNotEraseEarlierArchive) {
  auto f = quick2_fixtures::make("open");
  f.request.limits.maximumRouteDuration = 2h;
  auto archive = Archive(f.request);
  const auto result = weather_routing::native::RunRoutingEngineSequence(
      weather_routing::RoutingEngine::All, f.request.cancellation, [&](auto engine) {
        wr::RoutingResult failed; failed.status = wr::RoutingStatus::SearchIncomplete;
        if (engine == weather_routing::native::SequenceEngine::Quick2) {
          f.request.partials = archive;
          return Solve(1, f);
        }
        return failed;
      });
  EXPECT_FALSE(weather_routing::native::AcceptedRoute(result));
  EXPECT_EQ(result.attemptFailures.size(), 5U);
  ASSERT_FALSE(archive->entries().empty());
  EXPECT_TRUE(wr::ValidatePartialRoute(f.request, f.environment, archive->entries().front().trace).validation.passed);
}

TEST(PartialRoutes, ArchiveRankingMatchesUncachedReferenceThroughEvictions) {
  auto f = quick2_fixtures::make("open");
  wr::PartialRouteArchive archive(f.request.start, f.request.destination, f.request.departure,
      f.request.limits.maximumRouteDuration, 2000);
  std::uint32_t random = 17;
  for (int trial = 0; trial < 2000; ++trial) {
    random = random * 1664525U + 1013904223U;
    const auto endpoint = wr::destinationPoint(f.request.destination, random % 360,
        (random % 6000) / 1000.);
    const auto time = f.request.departure + wr::Duration{1 + random % 100000};
    wr::PartialRouteSeed seed{endpoint, time, "reference", wr::distanceNm(endpoint, f.request.destination)};
    const auto& entries = archive.entries();
    double anchor = seed.remainingNm;
    for (const auto& e : entries) anchor = std::min(anchor, e.seed.remainingNm);
    const auto rank = [&](const auto& s) {
      return std::tuple{std::floor((s.remainingNm - anchor) / wr::kReplayEndpointToleranceNm),
                        s.time, s.remainingNm, s.endpoint.latitude, s.endpoint.longitude};
    };
    bool wanted = seed.remainingNm + wr::kReplayEndpointToleranceNm <
        wr::distanceNm(f.request.start, f.request.destination);
    for (const auto& e : entries)
      if (wr::distanceNm(e.seed.endpoint, endpoint) <= 1e-8 &&
          e.seed.remainingNm <= seed.remainingNm && e.seed.time <= time) wanted = false;
    if (entries.size() >= 16 || (!entries.empty() &&
        archive.byteBudget() - archive.bytes() < archive.bytes() / entries.size())) {
      const auto worst = std::max_element(entries.begin(), entries.end(),
          [&](const auto& a, const auto& b) { return rank(a.seed) < rank(b.seed); });
      wanted = wanted && rank(seed) < rank(worst->seed);
    }
    ASSERT_EQ(archive.wanted(seed), wanted) << trial;
    archive.offer(endpoint, time, "reference", [&] {
      return wr::PartialRouteTrace{100 + random % 400,
          [](const auto&, const auto&, auto&) { return std::vector<wr::RouteLeg>{}; }};
    });
    std::size_t bytes = 0;
    for (const auto& e : archive.entries()) bytes += e.trace.bytes;
    ASSERT_EQ(archive.bytes(), bytes);
    ASSERT_LE(bytes, archive.byteBudget());
  }
}

TEST(PartialRoutes, ArchiveDoesNotTradeHoursForTinyGainAcrossRoundingBoundary) {
  auto f = quick2_fixtures::make("open");
  wr::PartialRouteArchive archive(f.request.start, f.request.destination, f.request.departure,
      f.request.limits.maximumRouteDuration, 2000, 1);
  const auto trace = [] { return wr::PartialRouteTrace{100,
      [](const auto&, const auto&, auto&) { return std::vector<wr::RouteLeg>{}; }}; };
  archive.offer(wr::destinationPoint(f.request.destination, 270, .1501),
                f.request.departure + 1h, "early", trace);
  archive.offer(wr::destinationPoint(f.request.destination, 270, .1499),
                f.request.departure + 25h, "late", trace);
  ASSERT_EQ(archive.entries().size(), 1U);
  EXPECT_EQ(archive.entries().front().seed.stage, "early");
}

TEST(PartialRoutes, LaterInvalidGraphCandidatesPreserveForwardFallback) {
  auto f = quick2_fixtures::make("short");
  const auto solved = Solve(1, f);
  ASSERT_TRUE(solved.validation.passed);
  f.request.destination = wr::destinationPoint(solved.legs.back().end, 90, 10);
  auto archive = Archive(f.request);
  archive->offer(solved.legs.back().end, solved.legs.back().endTime, "forward",
      [&] { return wr::FreezePartialLegs(solved.legs); }, wr::PartialRoutePhase::Forward);
  for (int i = 0; i < 30; ++i) {
    auto broken = solved.legs; broken.back().end = f.request.destination;
    archive->offer(wr::destinationPoint(f.request.destination, 270, .01 + i * .001),
        solved.legs.back().endTime + wr::Duration{i + 1}, "invalid graph",
        [&] { return wr::FreezePartialLegs(broken); }, wr::PartialRoutePhase::Graph);
  }
  unsigned safe = 0, rejected = 0;
  for (const auto& entry : archive->entries()) {
    const auto result = wr::ValidatePartialRoute(f.request, f.environment, entry.trace);
    if (result.validation.passed) { ++safe; EXPECT_EQ(entry.seed.stage, "forward"); }
    else ++rejected;
  }
  EXPECT_EQ(safe, 1U); EXPECT_GT(rejected, 0U);
}

TEST(PartialRoutes, BytePressureDoesNotReconstructEveryUncompetitiveNode) {
  auto f = quick2_fixtures::make("open");
  wr::PartialRouteArchive archive(f.request.start, f.request.destination, f.request.departure,
      f.request.limits.maximumRouteDuration, 2000);
  unsigned copies = 0;
  const auto trace = [&] { ++copies; return wr::PartialRouteTrace{500,
      [](const auto&, const auto&, auto&) { return std::vector<wr::RouteLeg>{}; }}; };
  archive.offer(wr::destinationPoint(f.request.destination, 270, 10), f.request.departure + 1h,
                "contour", trace, wr::PartialRoutePhase::Contour);
  for (int i = 0; i < 3; ++i)
    archive.offer(wr::destinationPoint(f.request.destination, 270, 1 + .01 * i),
        f.request.departure + wr::Duration{7200 + i}, "forward", trace, wr::PartialRoutePhase::Forward);
  const auto before = copies;
  for (int i = 0; i < 10000; ++i)
    archive.offer(wr::destinationPoint(f.request.destination, 270, 5 + i / 100000.),
        f.request.departure + 3h, "uncompetitive", trace, wr::PartialRoutePhase::Forward);
  EXPECT_EQ(copies, before);
}

TEST(PartialRoutes, PrefixGateRejectsHorizonWeatherGeometryAndCancellation) {
  auto f = quick2_fixtures::make("short");
  const auto complete = Solve(1, f);
  ASSERT_TRUE(complete.validation.passed);
  auto legs = complete.legs;
  const auto good = wr::FreezePartialLegs(legs);
  EXPECT_TRUE(wr::ValidatePartialRoute(f.request, f.environment, good).validation.passed);
  auto shortRequest = f.request; shortRequest.limits.maximumRouteDuration = 1s;
  const auto late = wr::ValidatePartialRoute(shortRequest, f.environment, good);
  EXPECT_FALSE(late.validation.passed); EXPECT_TRUE(late.legs.empty());
  EXPECT_NE(late.validation.failureReason.find("horizon"), std::string::npos);
  auto broken = legs; broken.back().end.latitude += 1;
  EXPECT_FALSE(wr::ValidatePartialRoute(f.request, f.environment, wr::FreezePartialLegs(broken)).validation.passed);
  unsigned hostCalls = 0;
  EXPECT_FALSE(wr::ValidatePartialRoute(f.request, f.environment, good, [&](const auto&) {
    ++hostCalls; return false;
  }).validation.passed);
  EXPECT_GT(hostCalls, 0U);
  auto depthRequest = f.request;
  depthRequest.constraints.minimumDepthMetres = 5;
  auto shallow = f.environment;
  shallow.landAndBoundaries = std::make_shared<quick2_fixtures::Shallow>();
  EXPECT_FALSE(wr::ValidatePartialRoute(depthRequest, shallow, good).validation.passed);
  auto windLimit = f.request;
  windLimit.constraints.maximumTrueWindKnots = 1;
  EXPECT_FALSE(wr::ValidatePartialRoute(windLimit, f.environment, good).validation.passed);
  auto missing = quick2_fixtures::make("missing-wind");
  EXPECT_FALSE(wr::ValidatePartialRoute(f.request, missing.environment, good).validation.passed);
  f.request.cancellation.cancel();
  EXPECT_FALSE(wr::ValidatePartialRoute(f.request, f.environment, good).validation.passed);
}

class BufferAtPartialEndpoint final : public wr::LandAndBoundaryProvider {
 public:
  wr::GeoPoint endpoint;
  bool pointForbidden(wr::GeoPoint) const override { return false; }
  bool segmentForbidden(wr::GeoPoint a, wr::GeoPoint b, double margin) const override {
    return margin > 0 && (wr::distanceNm(a, endpoint) < .05 || wr::distanceNm(b, endpoint) < .05);
  }
  double distanceToForbiddenNm(wr::GeoPoint p) const override {
    return wr::distanceNm(p, endpoint) < .05 ? .01 : 100;
  }
  std::string identity() const override { return "partial-endpoint-buffer"; }
};
TEST(PartialRoutes, PartialEndpointNeverGainsDestinationIngressWaiver) {
  auto f = quick2_fixtures::make("short");
  auto solved = Solve(1, f);
  ASSERT_TRUE(solved.validation.passed);
  auto land = std::make_shared<BufferAtPartialEndpoint>();
  land->endpoint = solved.legs.back().end;
  f.environment.landAndBoundaries = land;
  f.request.destination = wr::destinationPoint(land->endpoint, 90, 10);
  f.request.constraints.landSafetyMarginNm = .4;
  EXPECT_FALSE(wr::ValidatePartialRoute(f.request, f.environment,
      wr::FreezePartialLegs(solved.legs)).validation.passed);
}

TEST(PartialRoutes, InternalQuickPrefixRetainsItsStricterCurrentPolicy) {
  auto f = quick2_fixtures::make("short");
  const auto solved = Solve(1, f);
  ASSERT_TRUE(solved.validation.passed);
  wr::UniformWeatherProvider::Configuration weather;
  weather.windTowardKnots = wr::speedDirectionToVector(14, 140);
  f.environment.grib = std::make_shared<wr::UniformWeatherProvider>(weather);
  f.request.environment.useCurrent = true;
  f.request.environment.missingCurrent = wr::MissingCurrentPolicy::AllowAssumedZero;
  f.request.environment.zeroCurrentAcknowledged = true;
  auto trace = wr::FreezePartialLegs(solved.legs);
  ASSERT_TRUE(wr::ValidatePartialRoute(f.request, f.environment, trace).validation.passed);
  trace.environmentPolicy = f.request.environment;
  trace.environmentPolicy->missingCurrent = wr::MissingCurrentPolicy::Disallow;
  EXPECT_FALSE(wr::ValidatePartialRoute(f.request, f.environment, trace).validation.passed);
}

struct ComparisonCandidate {
  weather_routing::RouteComparisonMetric metric;
  weather_routing::RouteComfort windOnlyComfort;
  double remainingNm;
};
ComparisonCandidate Candidate(const char* id, double distance,
                                                int hours, double exposure) {
  ComparisonCandidate c;
  c.metric.id = id; c.remainingNm = distance;
  c.metric.elapsedSeconds = hours * 3600;
  c.metric.comfort.durationSeconds = hours * 3600;
  c.metric.comfort.exposureHours = exposure;
  c.windOnlyComfort = c.metric.comfort;
  return c;
}
std::size_t Select(const std::vector<ComparisonCandidate>& candidates, int preference, bool windOnly) {
  std::vector<weather_routing::RouteComparisonMetric> metrics;
  std::vector<double> remaining;
  for (const auto& c : candidates) {
    auto metric = c.metric; if (windOnly) metric.comfort = c.windOnlyComfort;
    metrics.push_back(metric); remaining.push_back(c.remainingNm);
  }
  return weather_routing::SelectPartialComfortCandidate(metrics, remaining, preference,
      wr::kReplayEndpointToleranceNm).selected;
}
TEST(PartialRoutes, TinyProximityGainDoesNotRewardAnotherDayOfSailing) {
  std::vector candidates{Candidate("wandering", .997, 30, 20), Candidate("early", 1., 5, 3)};
  EXPECT_EQ(Select(candidates, 0, false), 1U);
  EXPECT_EQ(Select(candidates, 100, false), 1U);
}
TEST(PartialRoutes, ComfortSelectionUsesAnchoredProximityAndExplicitCoverage) {
  std::vector candidates{Candidate("near", 1, 5, 10), Candidate("gentle", 1.14, 6, 4),
                        Candidate("too-far", 1.28, 1, 0)};
  EXPECT_EQ(Select(candidates, 0, false), 0U);
  EXPECT_EQ(Select(candidates, 100, false), 1U);
  candidates[1].metric.comfort.categorySeconds[0] = 1;
  EXPECT_EQ(Select(candidates, 100, false), 0U);
  EXPECT_EQ(Select(candidates, 100, true), 1U);
}

TEST(PartialRoutes, RetentionRanksActualReplayEndpointsWhenPredictionsWereWrong) {
  std::vector<weather_routing::PartialRetentionMetric> metrics(33);
  for (std::size_t i = 0; i < 32; ++i)
    metrics[i] = {10 + .01 * i, 3600, {}, {}};
  // This result arrives after a full collection whose contour predictions
  // were closer than their reconstructed endpoints. It must still be kept.
  metrics.back() = {.835, 100000, {}, {}};
  const auto discard = weather_routing::PartialCandidateToDiscard(metrics, wr::kReplayEndpointToleranceNm);
  EXPECT_NE(discard, 32U);
}

TEST(PartialRoutes, RetentionPreservesNearFastAndBothComfortExtremes) {
  using weather_routing::PartialRetentionMetric;
  const auto comfort = [](double exposure) {
    weather_routing::RouteComfort c; c.durationSeconds = 3600; c.exposureHours = exposure; return c;
  };
  std::vector<PartialRetentionMetric> metrics{
      {.835, 180000, comfort(100), comfort(100)},
      {.95, 100000, comfort(20), comfort(20)},
      {.94, 110000, comfort(10), comfort(1)},
      {.94, 120000, comfort(1), comfort(10)},
      {10, 100, comfort(0), comfort(0)}};
  EXPECT_EQ(weather_routing::PartialCandidateToDiscard(metrics, wr::kReplayEndpointToleranceNm), 4U);
}
} // namespace
