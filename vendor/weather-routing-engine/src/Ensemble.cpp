#include "supercpn/weather_routing/Engine.h"

#include <algorithm>
#include <cmath>
#include <numeric>

#include "RoutingInternal.h"

namespace supercpn::weather_routing {
namespace {
bool complete(RoutingStatus status) {
  return status == RoutingStatus::Complete ||
         status == RoutingStatus::CompleteUsingReverseRecovery ||
         status == RoutingStatus::CompleteUsingGraphFallback;
}

RoutingResult replayGeometry(const RoutingRequest& request,
                             const RoutingEnvironment& environment,
                             const RoutingResult& candidate) {
  RoutingResult result;
  result.solverPath = candidate.solverPath;
  PolarPerformanceModel fallbackPerformance(request.vessel);
  const VesselPerformanceModel& performance =
      environment.performance ? *environment.performance : fallbackPerformance;
  TimePoint time = request.departure;
  PropulsionMode previousMode = candidate.legs.empty()
                                    ? PropulsionMode::Sail
                                    : candidate.legs.front().propulsionMode;
  for (std::size_t i = 0; i < candidate.legs.size(); ++i) {
    const auto& geometry = candidate.legs[i];
    const double distance = distanceNm(geometry.start, geometry.end);
    const double groundCourse =
        initialBearingDegrees(geometry.start, geometry.end);
    const auto resolved = internal::resolveEnvironment(request, environment,
                                                       geometry.start, time);
    if (resolved.failureStatus != RoutingStatus::Complete) {
      result.status = resolved.failureStatus;
      result.message = resolved.failureReason;
      return result;
    }
    double heading = geometry.courseThroughWaterDegrees;
    PerformanceCandidate available;
    const Vector2 along = speedDirectionToVector(1.0, groundCourse);
    const Vector2 right{along.northKnots, -along.eastKnots};
    const auto current = resolved.snapshot.current.velocity;
    const double currentAlong = current.eastKnots * along.eastKnots +
                                current.northKnots * along.northKnots;
    const double currentCross = current.eastKnots * right.eastKnots +
                                current.northKnots * right.northKnots;
    double correctedGroundSpeed{};
    for (int iteration = 0; iteration < 12; ++iteration) {
      available = performance.evaluateAt(
          request.start, request.departure, geometry.propulsionMode,
          geometry.profileRole, geometry.profileIdentity,
          vectorMagnitudeKnots(resolved.snapshot.wind.velocity),
          trueWindAngleDegrees(resolved.snapshot.wind.velocity, heading),
          resolved.snapshot.waves);
      if (!available.valid ||
          available.speedThroughWaterKnots <= std::abs(currentCross)) {
        result.status = RoutingStatus::NoFeasibleRoute;
        result.message = "ensemble member cannot reproduce candidate heading";
        return result;
      }
      const double waterAlong = std::sqrt(available.speedThroughWaterKnots *
                                              available.speedThroughWaterKnots -
                                          currentCross * currentCross);
      correctedGroundSpeed = waterAlong + currentAlong;
      if (correctedGroundSpeed <= 0.05) break;
      const Vector2 waterVelocity{
          waterAlong * along.eastKnots - currentCross * right.eastKnots,
          waterAlong * along.northKnots - currentCross * right.northKnots};
      const double correctedHeading = vectorDirectionToDegrees(waterVelocity);
      if (std::abs(angularDifferenceDegrees(correctedHeading, heading)) <
          1e-6) {
        heading = correctedHeading;
        break;
      }
      heading = correctedHeading;
    }
    if (correctedGroundSpeed <= 0.05) {
      result.status = RoutingStatus::NoFeasibleRoute;
      result.message = "ensemble replay has non-positive ground speed";
      return result;
    }
    Duration penalty{};
    if (i > 0 && geometry.propulsionMode != previousMode)
      penalty += request.vessel.propulsion.modeChangePenalty;
    if (geometry.tackTransition) penalty += request.vessel.tackPenalty;
    if (geometry.gybeTransition) penalty += request.vessel.gybePenalty;
    const Duration moving{static_cast<std::int64_t>(
        std::ceil(distance / correctedGroundSpeed * 3600.0))};
    RouteLeg leg = geometry;
    leg.startTime = time;
    leg.endTime = time + moving + penalty;
    leg.headingDegrees = heading;
    leg.courseThroughWaterDegrees = heading;
    leg.courseOverGroundDegrees = groundCourse;
    leg.speedThroughWaterKnots = available.speedThroughWaterKnots;
    leg.speedOverGroundKnots = correctedGroundSpeed;
    leg.trueWindSpeedKnots =
        vectorMagnitudeKnots(resolved.snapshot.wind.velocity);
    leg.trueWindAngleDegrees =
        trueWindAngleDegrees(resolved.snapshot.wind.velocity, heading);
    leg.windSource = resolved.snapshot.wind.metadata;
    leg.current = current;
    leg.currentSource = resolved.snapshot.current.metadata;
    leg.waves = resolved.snapshot.waves;
    leg.waveSource = resolved.snapshot.waves.metadata;
    leg.estimatedFuelLitres = available.fuelLitresPerHour *
                              static_cast<double>(moving.count()) / 3600.0;
    result.legs.push_back(std::move(leg));
    time = result.legs.back().endTime;
    previousMode = geometry.propulsionMode;
  }
  RouteValidator validator;
  result.validation = validator.validate(request, environment, performance,
                                         result.legs, &result.diagnostics);
  result.diagnostics.validationSamples = result.validation.samples;
  if (!result.validation.passed) {
    result.status = RoutingStatus::ValidationFailure;
    result.message = result.validation.failureReason;
    return result;
  }
  result.status = RoutingStatus::Complete;
  result.sourceTransitions = result.validation.sourceTransitions;
  result.environment = result.validation.environment;
  result.metrics.elapsed = std::chrono::duration_cast<Duration>(
      result.legs.back().endTime - result.legs.front().startTime);
  for (const auto& leg : result.legs) {
    const Duration duration =
        std::chrono::duration_cast<Duration>(leg.endTime - leg.startTime);
    result.metrics.distanceNm += distanceNm(leg.start, leg.end);
    result.metrics.estimatedFuelLitres += leg.estimatedFuelLitres;
    result.metrics.maximumWindKnots =
        std::max(result.metrics.maximumWindKnots, leg.trueWindSpeedKnots);
    if (leg.waves.available)
      result.metrics.maximumWaveHeightMetres =
          std::max(result.metrics.maximumWaveHeightMetres,
                   leg.waves.significantHeightMetres);
    if (leg.propulsionMode == PropulsionMode::Sail)
      result.metrics.sailingTime += duration;
    else if (leg.propulsionMode == PropulsionMode::MotorSail)
      result.metrics.motorSailingTime += duration;
    else
      result.metrics.motorOnlyTime += duration;
    result.metrics.propulsionTransitions += leg.propulsionTransition;
    result.metrics.tackCount += leg.tackTransition;
    result.metrics.gybeCount += leg.gybeTransition;
  }
  return result;
}

GeoPoint routeMidpoint(const RoutingResult& result) {
  if (result.legs.empty()) return {};
  const double half = result.metrics.distanceNm / 2.0;
  double travelled = 0.0;
  for (const auto& leg : result.legs) {
    const double length = distanceNm(leg.start, leg.end);
    if (travelled + length >= half && length > 0.0)
      return destinationPoint(leg.start,
                              initialBearingDegrees(leg.start, leg.end),
                              half - travelled);
    travelled += length;
  }
  return result.legs.back().end;
}

template <typename T>
T quantile(std::vector<T> values, double probability) {
  if (values.empty()) return T{};
  std::sort(values.begin(), values.end());
  const double position = probability * (values.size() - 1);
  const std::size_t index = static_cast<std::size_t>(std::floor(position));
  if constexpr (std::is_same_v<T, Duration>) {
    if (index + 1 >= values.size()) return values[index];
    const double fraction = position - static_cast<double>(index);
    return Duration{static_cast<std::int64_t>(std::llround(
        values[index].count() +
        fraction * (values[index + 1].count() - values[index].count())))};
  } else {
    if (index + 1 >= values.size()) return values[index];
    return values[index] + (position - static_cast<double>(index)) *
                               (values[index + 1] - values[index]);
  }
}
}  // namespace

RoutingResult RoutingEngine::routeEnsemble(
    const RoutingRequest& request,
    std::span<const RoutingEnvironment> members) const {
  if (members.empty()) {
    RoutingResult result;
    result.status = RoutingStatus::WeatherCoverageInsufficient;
    result.message = "ensemble contains no forecast members";
    return result;
  }
  std::vector<RoutingResult> routes;
  routes.reserve(members.size());
  for (const auto& member : members)
    // Apply the same cumulative effort tiers as a deterministic member route.
    // A higher ensemble effort must not discard a member solution already
    // found at a lower tier merely because wider pruning changes its lineage.
    routes.push_back(route(request, member));

  std::vector<std::vector<RoutingResult>> evaluations(
      routes.size(), std::vector<RoutingResult>(members.size()));
  for (std::size_t candidate = 0; candidate < routes.size(); ++candidate) {
    if (!complete(routes[candidate].status)) continue;
    for (std::size_t member = 0; member < members.size(); ++member)
      evaluations[candidate][member] =
          replayGeometry(request, members[member], routes[candidate]);
  }

  std::vector<GeoPoint> midpoints;
  std::vector<unsigned> candidateFamilies(routes.size());
  for (std::size_t i = 0; i < routes.size(); ++i) {
    if (!complete(routes[i].status)) continue;
    const GeoPoint midpoint = routeMidpoint(routes[i]);
    unsigned family = 0;
    for (; family < midpoints.size(); ++family)
      if (distanceNm(midpoints[family], midpoint) <=
          std::max(5.0, request.options.graphCorridorWidthNm * 0.2))
        break;
    if (family == midpoints.size()) midpoints.push_back(midpoint);
    candidateFamilies[i] = family;
  }
  std::optional<std::size_t> selectedCandidate;
  std::tuple<unsigned, Duration, std::size_t> best{
      std::numeric_limits<unsigned>::max(), Duration::max(),
      std::numeric_limits<std::size_t>::max()};
  for (std::size_t candidate = 0; candidate < routes.size(); ++candidate) {
    std::vector<Duration> candidateEtas;
    for (const auto& evaluation : evaluations[candidate])
      if (complete(evaluation.status) && evaluation.validation.passed)
        candidateEtas.push_back(evaluation.metrics.elapsed);
    if (candidateEtas.empty()) continue;
    const auto rank = std::tuple{
        static_cast<unsigned>(members.size() - candidateEtas.size()),
        quantile(candidateEtas, request.objective.percentile), candidate};
    if (!selectedCandidate || rank < best) {
      best = rank;
      selectedCandidate = candidate;
    }
  }
  if (!selectedCandidate) {
    RoutingResult result = routes.front();
    result.message = "no ensemble member produced a validated route";
    return result;
  }

  EnsembleMetrics metrics;
  metrics.memberCount = members.size();
  metrics.routeFamilyCount = midpoints.size();
  std::vector<Duration> etas;
  std::vector<double> fuels;
  for (std::size_t memberIndex = 0; memberIndex < members.size();
       ++memberIndex) {
    const auto& evaluation = evaluations[*selectedCandidate][memberIndex];
    EnsembleMemberResult member;
    member.memberIdentity = members[memberIndex].memberIdentity;
    member.feasible =
        complete(evaluation.status) && evaluation.validation.passed;
    member.failureReason = member.feasible ? std::string{} : evaluation.message;
    member.routeFamily = candidateFamilies[memberIndex];
    if (member.feasible) {
      ++metrics.feasibleMemberCount;
      member.eta = evaluation.metrics.elapsed;
      member.maximumWindKnots = evaluation.metrics.maximumWindKnots;
      member.maximumWaveMetres = evaluation.metrics.maximumWaveHeightMetres;
      member.fuelLitres = evaluation.metrics.estimatedFuelLitres;
      etas.push_back(member.eta);
      fuels.push_back(member.fuelLitres);
      metrics.worstCredibleWindKnots =
          std::max(metrics.worstCredibleWindKnots, member.maximumWindKnots);
      metrics.worstCredibleWaveMetres =
          std::max(metrics.worstCredibleWaveMetres, member.maximumWaveMetres);
    }
    metrics.members.push_back(std::move(member));
  }
  metrics.feasibilityPercentage =
      100.0 * metrics.feasibleMemberCount / std::max(1U, metrics.memberCount);
  metrics.medianEta = quantile(etas, 0.5);
  metrics.lowerEtaQuantile = quantile(etas, 0.1);
  metrics.upperEtaQuantile = quantile(etas, 0.9);
  metrics.medianFuelLitres = quantile(fuels, 0.5);
  metrics.upperFuelLitres = quantile(fuels, 0.9);
  for (std::size_t i = 0; i < midpoints.size(); ++i)
    for (std::size_t j = i + 1; j < midpoints.size(); ++j)
      metrics.routeSpreadNm = std::max(metrics.routeSpreadNm,
                                       distanceNm(midpoints[i], midpoints[j]));
  const double medianHours =
      static_cast<double>(metrics.medianEta.count()) / 3600.0;
  const double upperHours =
      static_cast<double>(metrics.upperEtaQuantile.count()) / 3600.0;
  metrics.robustRankingScore =
      medianHours + std::max(0.0, upperHours - medianHours) * 0.5 +
      (1.0 - metrics.feasibilityPercentage / 100.0) * 1000.0;

  auto representative = std::find_if(
      evaluations[*selectedCandidate].begin(),
      evaluations[*selectedCandidate].end(), [](const auto& value) {
        return complete(value.status) && value.validation.passed;
      });
  RoutingResult result = std::move(*representative);
  result.ensemble = std::move(metrics);
  if (result.ensemble.feasibilityPercentage + 1e-9 <
      request.objective.minimumFeasibilityProbability * 100.0) {
    result.warnings.push_back(
        {RoutingWarningCode::EnsembleMemberFailed,
         "validated member feasibility is below the configured probability"});
  }
  return result;
}

}  // namespace supercpn::weather_routing
