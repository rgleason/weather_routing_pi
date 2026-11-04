// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <limits>
#include <string>
#include <span>
#include <tuple>
#include <vector>
#include "supercpn/weather_routing/Types.h"
#include "supercpn/weather_routing/Comfort.h"

namespace weather_routing {
namespace wr = supercpn::weather_routing;

// Versioned prototype model: preserve 1.21's empirical condition severity and
// displayed categories. Exposure integrates continuous severity over hours;
// category durations and the worst leg remain independent metrics.
inline double ConditionSeverity(double windKnots, double relativeWindDegrees,
                                double waveMetres) {
  return wr::comfortSeverity(windKnots, relativeWindDegrees, waveMetres);
}
inline int ConditionCategory(double severity) {
  if (!std::isfinite(severity)) return 0;
  return severity <= 0.5 ? 1 : severity < 1.0 ? 2 : 3;
}

struct RouteComfort {
  static constexpr int modelRevision = 1;
  std::int64_t durationSeconds{};
  std::array<std::int64_t, 4> categorySeconds{}; // Unknown, Good, Bumpy, Difficult
  std::int64_t waveCoveredSeconds{};
  std::int64_t longestDifficultSeconds{};
  double exposureHours{};
  double averageDiscomfort{};
  bool windOnly{};
  int worstCategory{};
  double worstSeverity{-1};
  std::size_t worstLegIndex{};
  wr::GeoPoint worstLegStart;
  wr::GeoPoint worstLegEnd;
  wr::TimePoint worstLegStartTime{};
  wr::TimePoint worstLegEndTime{};
  bool comparable() const {
    return durationSeconds > 0 && categorySeconds[0] == 0;
  }
};

inline RouteComfort CalculateRouteComfort(const std::vector<wr::RouteLeg>& legs,
                                         bool windOnly = false) {
  RouteComfort result;
  result.windOnly = windOnly;
  std::int64_t difficultSpell = 0;
  wr::TimePoint previousEnd{};
  for (std::size_t index = 0; index < legs.size(); ++index) {
    const auto& leg = legs[index];
    const auto seconds = (leg.endTime - leg.startTime).count();
    if (seconds <= 0) continue;
    result.durationSeconds += seconds;
    const bool wavesKnown = leg.waves.available &&
        std::isfinite(leg.waves.significantHeightMetres) &&
        leg.waves.significantHeightMetres >= 0;
    if (wavesKnown) result.waveCoveredSeconds += seconds;
    const double windFrom = std::atan2(leg.wind.eastKnots, leg.wind.northKnots) *
                               180.0 / 3.14159265358979323846 + 180.0;
    const double severity = ConditionSeverity(
        std::hypot(leg.wind.eastKnots, leg.wind.northKnots),
        leg.courseThroughWaterDegrees - windFrom,
        windOnly ? 0.0 : leg.waves.significantHeightMetres);
    const int category = (windOnly || wavesKnown) ? ConditionCategory(severity) : 0;
    result.categorySeconds[category] += seconds;
    if (category > 0) result.exposureHours += severity * seconds / 3600.0;
    if (category == 3) {
      if (index == 0 || previousEnd != leg.startTime) difficultSpell = 0;
      difficultSpell += seconds;
      result.longestDifficultSeconds =
          std::max(result.longestDifficultSeconds, difficultSpell);
    } else difficultSpell = 0;
    previousEnd = leg.endTime;
    if (category > 0 && severity > result.worstSeverity) {
      result.worstCategory = category;
      result.worstSeverity = severity;
      result.worstLegIndex = index;
      result.worstLegStart = leg.start;
      result.worstLegEnd = leg.end;
      result.worstLegStartTime = leg.startTime;
      result.worstLegEndTime = leg.endTime;
    }
  }
  if (result.durationSeconds > 0)
    result.averageDiscomfort = result.exposureHours * 3600.0 / result.durationSeconds;
  return result;
}

struct RouteComparisonMetric {
  std::string id;
  std::int64_t elapsedSeconds{};
  RouteComfort comfort;
};
struct RouteComparisonSelection {
  std::size_t selected{};
  std::int64_t allowanceSeconds{};
  bool comfortAvailable{};
  bool tradeOffAvailable{};
};
inline RouteComparisonSelection SelectComfortCandidate(
    const std::vector<RouteComparisonMetric>& candidates, int preference) {
  RouteComparisonSelection result;
  if (candidates.empty()) return result;
  const auto faster = [&](std::size_t a, std::size_t b) {
    if (candidates[a].elapsedSeconds != candidates[b].elapsedSeconds)
      return candidates[a].elapsedSeconds < candidates[b].elapsedSeconds;
    const auto& x = candidates[a].comfort;
    const auto& y = candidates[b].comfort;
    if (x.comparable() != y.comparable()) return x.comparable();
    if (x.comparable() && x.exposureHours != y.exposureHours)
      return x.exposureHours < y.exposureHours;
    return candidates[a].id < candidates[b].id;
  };
  std::size_t fastest = 0;
  for (std::size_t i = 1; i < candidates.size(); ++i)
    if (faster(i, fastest)) fastest = i;
  result.selected = fastest;
  std::size_t comfortable = fastest;
  bool haveComfort = false;
  const auto gentler = [&](std::size_t a, std::size_t b) {
    if (candidates[a].comfort.exposureHours != candidates[b].comfort.exposureHours)
      return candidates[a].comfort.exposureHours < candidates[b].comfort.exposureHours;
    return faster(a, b);
  };
  for (std::size_t i = 0; i < candidates.size(); ++i)
    if (candidates[i].comfort.comparable() &&
        (!haveComfort || gentler(i, comfortable))) {
      comfortable = i;
      haveComfort = true;
    }
  result.comfortAvailable = haveComfort;
  if (!haveComfort) return result;
  const auto extra = candidates[comfortable].elapsedSeconds -
                     candidates[fastest].elapsedSeconds;
  result.allowanceSeconds = std::max<std::int64_t>(0, extra) *
                            std::clamp(preference, 0, 100) / 100;
  result.tradeOffAvailable = extra > 0 && comfortable != fastest;
  if (preference <= 0) return result;
  bool haveEligible = false;
  const auto ceiling = candidates[fastest].elapsedSeconds + result.allowanceSeconds;
  for (std::size_t i = 0; i < candidates.size(); ++i)
    if (candidates[i].comfort.comparable() && candidates[i].elapsedSeconds <= ceiling &&
        (!haveEligible || gentler(i, result.selected))) {
      result.selected = i;
      haveEligible = true;
    }
  return result;
}
// Proximity eligibility is anchored at the closest retained endpoint. It must
// not grow transitively through a chain of individually close endpoints.
inline RouteComparisonSelection SelectPartialComfortCandidate(
    const std::vector<RouteComparisonMetric>& candidates,
    const std::vector<double>& remainingNm, int preference, double toleranceNm) {
  RouteComparisonSelection result;
  if (candidates.empty() || candidates.size() != remainingNm.size()) return result;
  double nearest = std::numeric_limits<double>::infinity();
  for (const auto d : remainingNm) if (std::isfinite(d) && d >= 0) nearest = std::min(nearest, d);
  std::vector<RouteComparisonMetric> eligible;
  std::vector<std::size_t> indices;
  for (std::size_t i = 0; i < candidates.size(); ++i)
    if (std::isfinite(remainingNm[i]) && remainingNm[i] >= 0 &&
        remainingNm[i] <= nearest + toleranceNm) {
      eligible.push_back(candidates[i]); indices.push_back(i);
    }
  if (indices.empty()) return result;
  result = SelectComfortCandidate(eligible, preference);
  result.selected = indices[result.selected];
  return result;
}

struct PartialRetentionMetric {
  double remainingNm{};
  std::int64_t elapsedSeconds{};
  RouteComfort full, wind;
};
// Admission uses ACTUAL replay endpoints, not a contour's predicted endpoint.
// Preserve the closest and the near-proximity time/comfort extremes before
// dropping a surplus candidate under the caller's count/byte allowance.
inline std::size_t PartialCandidateToDiscard(std::span<const PartialRetentionMetric> metrics,
                                           double toleranceNm) {
  if (metrics.size() < 2) return 0;
  std::size_t closest = 0;
  for (std::size_t i = 1; i < metrics.size(); ++i)
    if (std::tuple{metrics[i].remainingNm, metrics[i].elapsedSeconds} <
        std::tuple{metrics[closest].remainingNm, metrics[closest].elapsedSeconds}) closest = i;
  const double anchor = metrics[closest].remainingNm;
  std::size_t fast = closest, full = closest, wind = closest;
  bool haveFull = false, haveWind = false;
  for (std::size_t i = 0; i < metrics.size(); ++i) {
    const auto& m = metrics[i];
    if (m.remainingNm > anchor + toleranceNm) continue;
    if (m.elapsedSeconds < metrics[fast].elapsedSeconds) fast = i;
    if (m.full.comparable() && (!haveFull ||
        std::tuple{m.full.exposureHours, m.elapsedSeconds} <
        std::tuple{metrics[full].full.exposureHours, metrics[full].elapsedSeconds})) {
      full = i; haveFull = true;
    }
    if (m.wind.comparable() && (!haveWind ||
        std::tuple{m.wind.exposureHours, m.elapsedSeconds} <
        std::tuple{metrics[wind].wind.exposureHours, metrics[wind].elapsedSeconds})) {
      wind = i; haveWind = true;
    }
  }
  const auto rank = [&](std::size_t i) {
    return std::tuple{std::floor((metrics[i].remainingNm - anchor) / toleranceNm),
                      metrics[i].elapsedSeconds, metrics[i].remainingNm, i};
  };
  std::optional<std::size_t> worst;
  for (std::size_t i = 0; i < metrics.size(); ++i) {
    if (i == closest || i == fast || (haveFull && i == full) || (haveWind && i == wind)) continue;
    if (!worst || rank(*worst) < rank(i)) worst = i;
  }
  if (worst) return *worst;
  // A byte budget may be too small for all four extremes. Keep a closest
  // representative; the caller rejects individually oversized paths first.
  for (std::size_t i = 0; i < metrics.size(); ++i)
    if (i != closest && (!worst || rank(*worst) < rank(i))) worst = i;
  return *worst;
}
} // namespace weather_routing
