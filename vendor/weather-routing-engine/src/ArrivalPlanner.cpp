#include "supercpn/weather_routing/ArrivalPlanner.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <map>
#include <set>
#include <tuple>

namespace supercpn::weather_routing {
namespace {

bool completeStatus(RoutingStatus status) {
  return status == RoutingStatus::Complete ||
         status == RoutingStatus::CompleteUsingReverseRecovery ||
         status == RoutingStatus::CompleteUsingFrontierRecovery ||
         status == RoutingStatus::CompleteUsingGraphFallback;
}

std::optional<TimePoint> routeArrival(const RoutingResult& route,
                                      TimePoint departure) {
  if (!completeStatus(route.status) || !route.validation.passed) return {};
  if (!route.legs.empty()) return route.legs.back().endTime;
  if (route.metrics.elapsed >= Duration::zero())
    return departure + route.metrics.elapsed;
  return {};
}

TimePoint clampTime(TimePoint value, TimePoint minimum, TimePoint maximum) {
  return std::max(minimum, std::min(maximum, value));
}

std::int64_t key(TimePoint value) { return value.time_since_epoch().count(); }

}  // namespace

ArrivalPlanningResult ArrivalPlanner::plan(
    const RoutingRequest& request, const ArrivalPlanningOptions& options,
    const ArrivalRouteEvaluator& evaluate) const {
  ArrivalPlanningResult output;
  output.diagnostics.effectiveDeadline =
      options.plannedArrival - options.safetyMargin;
  output.diagnostics.earliestDeparture =
      output.diagnostics.effectiveDeadline - options.searchHorizon;
  if (options.earliestAllowedDeparture)
    output.diagnostics.earliestDeparture =
        std::max(output.diagnostics.earliestDeparture,
                 *options.earliestAllowedDeparture);

  if (!evaluate || options.plannedArrival.time_since_epoch().count() <= 0 ||
      options.safetyMargin < Duration::zero() ||
      options.searchHorizon <= Duration::zero() ||
      options.initialSearchStep <= Duration::zero() ||
      options.refinementStep <= Duration::zero() ||
      options.arrivalTolerance < Duration::zero() ||
      options.refinementStep > options.initialSearchStep ||
      options.maximumRouteEvaluations == 0 ||
      !std::isfinite(options.nominalPassageSpeedKnots) ||
      options.nominalPassageSpeedKnots <= 0.0 ||
      output.diagnostics.earliestDeparture >=
          output.diagnostics.effectiveDeadline) {
    output.message = "invalid arrival-planning options";
    return output;
  }

  const double directDistance = distanceNm(request.start, request.destination);
  const auto estimatedDuration =
      Duration{static_cast<std::int64_t>(std::llround(
          directDistance / options.nominalPassageSpeedKnots * 3600.0))};
  const Duration boundedEstimate = std::clamp(
      estimatedDuration, options.initialSearchStep, options.searchHorizon);
  const TimePoint earliest = output.diagnostics.earliestDeparture;
  const TimePoint deadline = output.diagnostics.effectiveDeadline;
  const TimePoint projected =
      clampTime(deadline - boundedEstimate, earliest, deadline);
  output.diagnostics.reverseProjectedDeparture = projected;

  std::vector<TimePoint> pending;
  std::set<std::int64_t> scheduled;
  std::set<std::int64_t> evaluated;
  const auto schedule = [&](TimePoint departure, bool front = false) {
    departure = clampTime(departure, earliest, deadline);
    const std::int64_t candidateKey = key(departure);
    if (evaluated.contains(candidateKey) ||
        !scheduled.insert(candidateKey).second)
      return;
    if (front)
      pending.insert(pending.begin(), departure);
    else
      pending.push_back(departure);
  };

  schedule(projected);
  schedule(projected - options.initialSearchStep);
  schedule(projected + options.initialSearchStep);
  schedule(earliest);
  schedule(deadline - options.initialSearchStep);

  std::map<std::int64_t, std::pair<ArrivalCandidate, RoutingResult>> results;
  unsigned fallbackIndex = 0;
  while (output.diagnostics.forwardRoutesEvaluated <
         options.maximumRouteEvaluations) {
    if (request.cancellation.cancelled()) {
      output.status = ArrivalPlanningStatus::Cancelled;
      output.message = "arrival planning cancelled";
      return output;
    }

    if (pending.empty()) {
      const std::int64_t direction = fallbackIndex % 2 == 0 ? -1 : 1;
      const std::int64_t magnitude =
          static_cast<std::int64_t>(fallbackIndex / 2 + 2);
      ++fallbackIndex;
      schedule(projected + options.initialSearchStep * (direction * magnitude));
      if (pending.empty()) break;
    }

    const TimePoint departure = pending.front();
    pending.erase(pending.begin());
    scheduled.erase(key(departure));
    if (!evaluated.insert(key(departure)).second) continue;

    RoutingResult route = evaluate(departure);
    ++output.diagnostics.forwardRoutesEvaluated;
    output.diagnostics.evaluatedDepartures.push_back(departure);

    ArrivalCandidate candidate;
    candidate.departure = departure;
    candidate.routingStatus = route.status;
    candidate.complete = completeStatus(route.status);
    candidate.forwardValidated = candidate.complete && route.validation.passed;
    candidate.message = route.message;
    candidate.arrival = routeArrival(route, departure);
    if (candidate.arrival) {
      ++output.diagnostics.completeRoutes;
      candidate.deadlineError =
          std::chrono::duration_cast<Duration>(*candidate.arrival - deadline);
      if (candidate.deadlineError <= Duration::zero())
        ++output.diagnostics.feasibleRoutes;

      // Project the observed forward ETA error back onto the departure axis.
      // This is the arrival-anchored reverse timing step. It never supplies a
      // route: the projected departure is always evaluated chronologically.
      const TimePoint correction =
          clampTime(departure - candidate.deadlineError, earliest, deadline);
      schedule(correction, true);
      if (std::abs(candidate.deadlineError.count()) >
          options.arrivalTolerance.count()) {
        schedule(correction - options.refinementStep, true);
        schedule(correction + options.refinementStep, true);
      } else {
        // A route already within tolerance still needs one later, proven
        // forward solve so that the selected departure is not unnecessarily
        // conservative.
        schedule(departure + options.refinementStep, true);
      }
      ++output.diagnostics.reverseProjections;
    }
    results.emplace(key(departure), std::pair{candidate, std::move(route)});

    // When two forward-valid ETAs straddle the arrival deadline, interpolate
    // a departure and then prove it with another complete forward solve.
    std::optional<std::pair<TimePoint, Duration>> previous;
    for (const auto& [unused, entry] : results) {
      (void)unused;
      if (!entry.first.arrival) continue;
      const auto current =
          std::pair{entry.first.departure, entry.first.deadlineError};
      if (previous && ((previous->second <= Duration::zero() &&
                        current.second > Duration::zero()) ||
                       (previous->second > Duration::zero() &&
                        current.second <= Duration::zero()))) {
        const double firstError = previous->second.count();
        const double secondError = current.second.count();
        const double denominator = secondError - firstError;
        if (std::abs(denominator) > 0.5) {
          const double fraction =
              std::clamp(-firstError / denominator, 0.0, 1.0);
          const auto span = current.first - previous->first;
          const TimePoint interpolated =
              previous->first + Duration{static_cast<std::int64_t>(
                                    std::llround(span.count() * fraction))};
          const std::size_t oldSize = pending.size();
          schedule(interpolated, true);
          if (pending.size() != oldSize)
            ++output.diagnostics.bracketRefinements;
        }
      }
      previous = current;
    }
  }

  output.diagnostics.evaluationBudgetExhausted =
      output.diagnostics.forwardRoutesEvaluated >=
          options.maximumRouteEvaluations &&
      !pending.empty();
  for (auto& [unused, entry] : results) {
    (void)unused;
    output.candidates.push_back(entry.first);
  }

  auto best = results.end();
  for (auto iterator = results.begin(); iterator != results.end(); ++iterator) {
    const ArrivalCandidate& candidate = iterator->second.first;
    if (!candidate.arrival || candidate.deadlineError > Duration::zero() ||
        !candidate.forwardValidated)
      continue;
    if (best == results.end() ||
        std::tuple{candidate.departure, -candidate.deadlineError.count()} >
            std::tuple{best->second.first.departure,
                       -best->second.first.deadlineError.count()})
      best = iterator;
  }
  if (best == results.end()) {
    output.status = ArrivalPlanningStatus::NoFeasibleSchedule;
    output.message =
        "no forward-validated route met the planned arrival deadline";
    return output;
  }

  output.status = ArrivalPlanningStatus::Complete;
  output.departure = best->second.first.departure;
  output.arrival = best->second.first.arrival;
  output.scheduleMargin = -best->second.first.deadlineError;
  output.route = std::move(best->second.second);
  output.message = "forward-validated arrival schedule complete";
  return output;
}

std::string toString(ArrivalPlanningStatus status) {
  switch (status) {
    case ArrivalPlanningStatus::Complete:
      return "complete";
    case ArrivalPlanningStatus::NoFeasibleSchedule:
      return "no feasible schedule";
    case ArrivalPlanningStatus::InvalidRequest:
      return "invalid request";
    case ArrivalPlanningStatus::Cancelled:
      return "cancelled";
  }
  return "invalid request";
}

}  // namespace supercpn::weather_routing
