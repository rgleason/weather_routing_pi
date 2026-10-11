// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <algorithm>
#include <chrono>
#include <cmath>
#include <functional>
#include <memory>
#include <vector>

#include "engine/native/RoutingEngineSequence.h"
#include "supercpn/weather_routing/Comfort.h"
#include "supercpn/weather_routing/Engine.h"

namespace weather_routing::native {
namespace wr = supercpn::weather_routing;
struct ComfortSearchOptions {
  int additionalPercent{200};
  int maximumSeconds{20};
  std::optional<std::chrono::steady_clock::time_point> deadline;
  bool windOnly{true};
  bool nativeComfortLanes{false};
  unsigned maximumAlternatives{4};
  std::uint64_t maximumGeneratedStates{2000000};
  std::uint64_t maximumWeatherCalls{12000000};
};
inline std::chrono::milliseconds ComfortAllowance(std::chrono::milliseconds initial,
                                                  const ComfortSearchOptions& options) {
  if (initial.count() <= 0 || options.additionalPercent <= 0 || options.maximumSeconds <= 0)
    return {};
  return std::chrono::milliseconds{static_cast<long long>(std::min(
      static_cast<double>(initial.count()) * std::clamp(options.additionalPercent, 0, 400) / 100.0,
      static_cast<double>(std::clamp(options.maximumSeconds, 0, 3600)) * 1000))};
}
struct ComfortSearchReport {
  std::vector<wr::RoutingResult> alternatives;
  std::chrono::milliseconds allowance{}, elapsed{};
  unsigned attempts{}, validated{}, rejected{}, duplicates{}, missing{};
  std::uint64_t generated{}, weatherCalls{};
  bool stopped{}, cancelled{}, exhausted{};
};
class ComfortWeatherBudget final : public wr::WeatherProvider {
 public:
  ComfortWeatherBudget(std::shared_ptr<const wr::WeatherProvider> source,
                       const wr::CancellationToken& cancel, std::shared_ptr<std::atomic_bool> stop,
                       std::uint64_t limit)
      : source_(std::move(source)), cancel_(cancel), stop_(std::move(stop)), limit_(limit) {}
  wr::ParameterCoverage windCoverage() const override { return source_->windCoverage(); }
  wr::ParameterCoverage currentCoverage() const override { return source_->currentCoverage(); }
  wr::ParameterCoverage waveCoverage() const override { return source_->waveCoverage(); }
  wr::WindSample wind(wr::GeoPoint p, wr::TimePoint t) const override {
    return admit() ? source_->wind(p, t) : wr::WindSample{};
  }
  wr::CurrentSample current(wr::GeoPoint p, wr::TimePoint t) const override {
    return admit() ? source_->current(p, t) : wr::CurrentSample{};
  }
  wr::WaveSample waves(wr::GeoPoint p, wr::TimePoint t) const override {
    return admit() ? source_->waves(p, t) : wr::WaveSample{};
  }
  std::string identity() const override { return source_->identity(); }
  void beginAttempt(const wr::CancellationToken& token, std::uint64_t limit,
                    std::shared_ptr<std::atomic_bool> stop) {
    cancel_ = token;
    attemptStart_ = calls;
    attemptLimit_ = limit;
    attemptStop_ = std::move(stop);
  }
  void endAttempt(const wr::CancellationToken& token) {
    cancel_ = token;
    attemptStop_.reset();
  }
  mutable std::uint64_t calls{};

 private:
  bool admit() const {
    if (cancel_.cancelled()) return false;
    if (calls >= limit_) {
      stop_->store(true);
      return false;
    }
    if (attemptStop_ && calls - attemptStart_ >= attemptLimit_) {
      attemptStop_->store(true);
      return false;
    }
    ++calls;
    return true;
  }
  std::shared_ptr<const wr::WeatherProvider> source_;
  wr::CancellationToken cancel_;
  std::shared_ptr<std::atomic_bool> stop_;
  std::uint64_t limit_;
  std::uint64_t attemptStart_{}, attemptLimit_{};
  std::shared_ptr<std::atomic_bool> attemptStop_;
};
// The callback always runs the selected engine. `directed` enables Standard's
// native comfort lanes; other engines diversify via refined searches and
// weather-ranked intermediate lanes. No motion/performance model is altered.
using ComfortSolve = std::function<std::vector<wr::RoutingResult>(
    const wr::RoutingRequest&, const wr::RoutingEnvironment&, bool directed)>;
inline ComfortSearchReport ExploreComfortAlternatives(
    const wr::RoutingRequest& original, const wr::RoutingEnvironment& environment,
    const wr::RoutingResult& baseline, std::chrono::milliseconds initial,
    const ComfortSearchOptions& options, const ComfortSolve& solve,
    const std::function<bool(const wr::RoutingResult&)>& hostAccept,
    const std::shared_ptr<std::atomic_bool>& stop) {
  ComfortSearchReport report;
  report.allowance = ComfortAllowance(initial, options);
  if (!AcceptedRoute(baseline) || report.allowance.count() == 0 || !environment.grib ||
      !environment.performance || !options.maximumGeneratedStates || !options.maximumWeatherCalls)
    return report;
  const auto started = std::chrono::steady_clock::now();
  const auto deadline = options.deadline ? std::min(started + report.allowance, *options.deadline)
                                         : started + report.allowance;
  auto budgetStop = std::make_shared<std::atomic_bool>(false);
  auto request = original;
  request.cancellation =
      original.cancellation.bounded(deadline, stop).bounded(deadline, budgetStop);
  request.limits.maximumRouteDuration = std::min(
      original.limits.maximumRouteDuration,
      baseline.metrics.elapsed + std::min(baseline.metrics.elapsed / 2, wr::Duration{12 * 3600}));
  request.options.routingEffortPercent = 100;
  // Additional work has its own finite ceiling, irrespective of normal effort.
  const auto limited = [&](wr::RoutingRequest next) {
    const auto remaining = options.maximumGeneratedStates > report.generated
                               ? options.maximumGeneratedStates - report.generated
                               : 0;
    next.limits.maximumGeneratedStates = std::min(next.limits.maximumGeneratedStates, remaining);
    next.limits.maximumForwardGeneratedStates =
        std::min(next.limits.maximumForwardGeneratedStates, remaining / 2);
    next.limits.maximumForwardArrivalGeneratedStates = 0;
    next.limits.maximumGraphGeneratedStates =
        std::min(next.limits.maximumGraphGeneratedStates, remaining / 4);
    next.limits.maximumFrontierRecoveryGeneratedStates =
        std::min(next.limits.maximumFrontierRecoveryGeneratedStates, remaining / 4);
    next.limits.maximumRetainedStates =
        std::min<std::uint64_t>(next.limits.maximumRetainedStates, 32768);
    next.limits.maximumGraphLabels = std::min<std::uint64_t>(next.limits.maximumGraphLabels, 32768);
    return next;
  };
  auto weather = std::make_shared<ComfortWeatherBudget>(environment.grib, request.cancellation,
                                                        budgetStop, options.maximumWeatherCalls);
  auto boundedEnvironment = environment;
  boundedEnvironment.grib = weather;
  const auto identical = [](const wr::RoutingResult& a, const wr::RoutingResult& b) {
    if (a.legs.size() != b.legs.size()) return false;
    for (std::size_t i = 0; i < a.legs.size(); ++i) {
      const auto& x = a.legs[i];
      const auto& y = b.legs[i];
      if (x.startTime != y.startTime || x.endTime != y.endTime ||
          wr::distanceNm(x.start, y.start) > 0.001 || wr::distanceNm(x.end, y.end) > 0.001)
        return false;
    }
    return true;
  };
  const auto dominates = [&](const wr::RoutingResult& a, const wr::RoutingResult& b) {
    const auto x = wr::routeDiscomfortSeconds(a.legs, options.windOnly);
    const auto y = wr::routeDiscomfortSeconds(b.legs, options.windOnly);
    return std::isfinite(x) && std::isfinite(y) && a.metrics.elapsed <= b.metrics.elapsed &&
           x <= y + 1e-8;
  };
  const auto retain = [&](wr::RoutingResult candidate) {
    if (!AcceptedRoute(candidate) || candidate.legs.front().startTime != original.departure ||
        candidate.metrics.elapsed > request.limits.maximumRouteDuration ||
        wr::distanceNm(candidate.legs.front().start, original.start) > 0.05 ||
        wr::distanceNm(candidate.legs.back().end, original.destination) > 0.05) {
      ++report.rejected;
      return;
    }
    if (!std::isfinite(wr::routeDiscomfortSeconds(candidate.legs, options.windOnly))) {
      ++report.missing;
      return;
    }
    if (identical(baseline, candidate) ||
        std::any_of(report.alternatives.begin(), report.alternatives.end(),
                    [&](const auto& r) { return identical(r, candidate); })) {
      ++report.duplicates;
      return;
    }
    if (request.cancellation.cancelled()) return;
    if (boundedEnvironment.landAndBoundaries)
      boundedEnvironment.landAndBoundaries->prepareValidationRoute(
          candidate.legs, request.constraints.landSafetyMarginNm);
    auto validation = wr::RouteValidator{}.validate(request, boundedEnvironment,
                                                    *environment.performance, candidate.legs);
    if (!validation.passed || request.cancellation.cancelled() || !hostAccept(candidate)) {
      ++report.rejected;
      return;
    }
    ++report.validated;
    if (dominates(baseline, candidate) ||
        std::any_of(report.alternatives.begin(), report.alternatives.end(),
                    [&](const auto& r) { return dominates(r, candidate); }))
      return;
    candidate.validation = std::move(validation);
    std::erase_if(report.alternatives, [&](const auto& r) { return dominates(candidate, r); });
    report.alternatives.push_back(std::move(candidate));
    std::stable_sort(
        report.alternatives.begin(), report.alternatives.end(),
        [](const auto& a, const auto& b) { return a.metrics.elapsed < b.metrics.elapsed; });
    const auto cap = std::clamp(options.maximumAlternatives, 1U, 4U);
    if (report.alternatives.size() > cap)
      report.alternatives.erase(report.alternatives.begin() +
                                (cap == 1 ? 1 : report.alternatives.size() / 2));
  };
  const auto attempt = [&](wr::RoutingRequest next, bool directed) {
    if (request.cancellation.cancelled() || report.attempts >= 12 ||
        report.generated >= options.maximumGeneratedStates)
      return std::vector<wr::RoutingResult>{};
    const auto now = std::chrono::steady_clock::now();
    // Reserve time for the other search plans instead of letting one refined
    // attempt consume the complete allowance before gentler lanes are tried.
    const auto slice =
        std::max(std::chrono::milliseconds{1},
                 std::chrono::duration_cast<std::chrono::milliseconds>(deadline - now) /
                     std::max(1U, 6U - std::min(5U, report.attempts)));
    auto attemptStop = std::make_shared<std::atomic_bool>(false);
    next.cancellation = request.cancellation.bounded(std::min(deadline, now + slice), attemptStop);
    const auto remainingAttempts = std::max(1U, 6U - std::min(5U, report.attempts));
    weather->beginAttempt(
        next.cancellation,
        std::max<std::uint64_t>(1, (options.maximumWeatherCalls - weather->calls) / remainingAttempts),
        attemptStop);
    ++report.attempts;
    std::vector<wr::RoutingResult> results;
    try {
      results = solve(limited(next), boundedEnvironment, directed);
    } catch (const std::exception&) {
      ++report.rejected;
      // A throwing solver cannot report its spent states. Charge the
      // remaining quota conservatively and preserve secured results.
      report.generated = options.maximumGeneratedStates;
      budgetStop->store(true);
    }
    weather->endAttempt(request.cancellation);
    // A solver may return several candidates with shared diagnostics. Charge
    // its work once, using the largest reported cumulative/generated count.
    std::uint64_t generated{};
    for (const auto& r : results)
      generated = std::max(generated, std::max(r.diagnostics.generatedStates,
                                               r.diagnostics.cumulativeGeneratedStates));
    report.generated += generated;
    if (report.generated >= options.maximumGeneratedStates) budgetStop->store(true);
    return results;
  };
  // Preserve candidates already validated if an optional attempt fails.
  try {
    auto refined = request;
    refined.options.timeStep =
        std::max(refined.options.minimumTimeStep, original.options.timeStep / 2);
    refined.options.headingStepDegrees = std::max(2.5, original.options.headingStepDegrees / 2);
    refined.options.refinedHeadingStepDegrees =
        std::min(refined.options.refinedHeadingStepDegrees, refined.options.headingStepDegrees);
    for (auto& r : attempt(refined, false)) retain(std::move(r));
    if (options.nativeComfortLanes)
      for (auto& r : attempt(request, true)) retain(std::move(r));
    struct Lane {
      wr::GeoPoint point;
      double score;
    };
    std::vector<Lane> lanes;
    const double passage = wr::distanceNm(original.start, original.destination);
    const double bearing = wr::initialBearingDegrees(original.start, original.destination);
    for (double fraction : {0.35, 0.65}) {
      for (double side : {-1.0, 1.0}) {
        for (double width : {0.05, 0.15}) {
          if (request.cancellation.cancelled()) break;
          const auto center = wr::destinationPoint(original.start, bearing, passage * fraction);
          const auto point = wr::destinationPoint(center, bearing + side * 90,
                                                  std::clamp(passage * width, 0.5, 30.0));
          if (environment.landAndBoundaries &&
              (environment.landAndBoundaries->pointForbidden(point) ||
               environment.landAndBoundaries->distanceToForbiddenNm(point) <
                   request.constraints.landSafetyMarginNm))
            continue;
          const auto t =
              original.departure +
              wr::Duration{static_cast<long long>(baseline.metrics.elapsed.count() * fraction)};
          auto wind = weather->wind(point, t);
          if (!wind.available) continue;
          auto wave = options.windOnly ? wr::WaveSample{} : weather->waves(point, t);
          if (!options.windOnly &&
              (!wave.available || !std::isfinite(wave.significantHeightMetres)))
            continue;
          const double from = wr::vectorDirectionToDegrees(wind.velocity) + 180;
          const double severity =
              wr::comfortSeverity(wr::vectorMagnitudeKnots(wind.velocity),
                                  wr::initialBearingDegrees(point, original.destination) - from,
                                  options.windOnly ? 0 : wave.significantHeightMetres);
          if (std::isfinite(severity)) lanes.push_back({point, severity});
        }
      }
    }
    std::stable_sort(lanes.begin(), lanes.end(),
                     [](const auto& a, const auto& b) { return a.score < b.score; });
    for (const auto& lane : lanes) {
      if (request.cancellation.cancelled() || report.attempts >= 12) break;
      auto first = request;
      first.destination = lane.point;
      auto prefixes = attempt(first, false);
      if (prefixes.empty() || !AcceptedRoute(prefixes.front())) continue;
      auto prefix = std::move(prefixes.front());
      auto second = request;
      second.start = prefix.legs.back().end;
      second.departure = prefix.legs.back().endTime;
      second.limits.maximumRouteDuration -= prefix.metrics.elapsed;
      if (second.limits.maximumRouteDuration <= wr::Duration{}) continue;
      auto suffixes = attempt(second, false);
      if (suffixes.empty() || !AcceptedRoute(suffixes.front())) continue;
      auto joined = std::move(suffixes.front());
      // An intermediate lane is not a new coastal departure. Full replay
      // enforces the original buffer; host checks must use it as well.
      for (auto& leg : joined.legs) leg.coastalDepartureEgress = false;
      joined.legs.insert(joined.legs.begin(), prefix.legs.begin(), prefix.legs.end());
      wr::summariseDeliveredRoute(joined);
      retain(std::move(joined));
    }
  } catch (const std::exception&) {
    ++report.rejected;
  }
  report.weatherCalls = weather->calls;
  report.elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
      std::chrono::steady_clock::now() - started);
  report.cancelled = original.cancellation.cancelled();
  report.stopped = stop->load();
  report.exhausted = budgetStop->load() || std::chrono::steady_clock::now() >= deadline;
  if (report.cancelled) report.alternatives.clear();
  return report;
}
}  // namespace weather_routing::native
