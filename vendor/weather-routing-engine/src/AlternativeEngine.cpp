// SPDX-License-Identifier: Apache-2.0
#include "supercpn/weather_routing/AlternativeEngine.h"
#include "MotionKernel.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <memory>
#include <optional>
#include <vector>

namespace supercpn::weather_routing {
namespace {
struct Node {
  internal::MotionState state;
  RouteLeg incoming;
  std::shared_ptr<const Node> parent;
};

struct Candidate {
  internal::MotionCandidate motion;
  std::shared_ptr<const Node> parent;
  double progress{};
  double remaining{};
  double destinationPriority{};
  unsigned sector{};
};

std::vector<RouteLeg> lineage(const std::shared_ptr<const Node>& parent) {
  std::vector<RouteLeg> legs;
  for (auto node = parent; node && node->parent; node = node->parent)
    legs.push_back(node->incoming);
  std::reverse(legs.begin(), legs.end());
  return legs;
}

bool passesNearDestination(GeoPoint from, GeoPoint to, GeoPoint goal,
                           double radiusNm) {
  const double length = distanceNm(from, to);
  if (length <= 0) return false;
  return std::abs(crossTrackDistanceNm(from, to, goal)) <= radiusNm &&
         distanceNm(from, goal) <= length + radiusNm &&
         distanceNm(to, goal) <= length + radiusNm;
}

bool validPoint(GeoPoint point) {
  return std::isfinite(point.latitude) && std::isfinite(point.longitude) &&
         std::abs(point.latitude) <= 90 && std::abs(point.longitude) <= 180;
}
}  // namespace

AlternativeRoutingResult AlternativeRoutingEngine::route(
    const RoutingRequest& request, const RoutingEnvironment& environment,
    const AlternativeRoutingOptions& requestedOptions) const {
  AlternativeRoutingResult output;
  auto& result = output.route;
  const AlternativeRoutingOptions options = requestedOptions;
  const auto stateLimit = std::min(options.maximumGeneratedStates,
                                   request.limits.maximumGeneratedStates);
  if (options.timeStep < Duration{600} ||
      options.timeStep > Duration{21600} ||
      !std::isfinite(options.headingStepDegrees) ||
      options.headingStepDegrees < 1 || options.headingStepDegrees > 30 ||
      options.sectors < 8 || options.sectors > 360 ||
      !std::isfinite(options.arrivalRadiusNm) ||
      options.arrivalRadiusNm <= 0 || options.arrivalRadiusNm > 20 ||
      !std::isfinite(options.preferredLandMarginNm) ||
      options.preferredLandMarginNm < 0 ||
      !std::isfinite(options.motoringPruneFactor) ||
      options.motoringPruneFactor <= 0 ||
      options.motoringPruneFactor > 1 ||
      !std::isfinite(options.nearLandPruneFactor) ||
      options.nearLandPruneFactor <= 0 ||
      options.nearLandPruneFactor > 1 ||
      stateLimit == 0 || options.maximumWeatherSamples == 0 ||
      options.maximumLineageNodes == 0 ||
      options.maximumValidatedCandidates == 0 ||
      request.limits.maximumRouteDuration <= Duration::zero()) {
    result.status = RoutingStatus::InvalidVesselConfiguration;
    result.message = "Invalid Alternative routing options";
    return output;
  }
  if (request.cancellation.cancelled()) {
    result.status = RoutingStatus::Cancelled;
    result.message = "Alternative route cancelled";
    return output;
  }
  if (!validPoint(request.start) || !validPoint(request.destination)) {
    result.status = !validPoint(request.start) ? RoutingStatus::InvalidStart
                                               : RoutingStatus::InvalidDestination;
    result.message = "Invalid route endpoint";
    return output;
  }
  if (environment.landAndBoundaries &&
      (environment.landAndBoundaries->pointForbidden(request.start) ||
       environment.landAndBoundaries->pointForbidden(request.destination))) {
    result.status = RoutingStatus::InvalidDestination;
    result.message = "Route endpoint is inside a forbidden area";
    return output;
  }
  const auto preflight = RoutingEngine{}.preflight(request, environment);
  result.preflight = preflight;
  result.warnings = preflight.warnings;
  if (!preflight.canRoute) {
    result.status = internal::failedPreflightStatus(preflight);
    result.message = "Alternative routing preflight requires caller action";
    return output;
  }
  if (environment.xtdCurrent && request.environment.useCurrent &&
      preflight.coverage.currentFallbackNeeded) {
    const double margin = std::min(
        30.0, std::max(1.0, request.options.graphCorridorWidthNm / 60.0));
    const double lonDelta = normalizeLongitude(request.destination.longitude -
                                               request.start.longitude);
    const double eastUnwrapped = request.start.longitude + lonDelta;
    GeoEnvelope area;
    area.west = normalizeLongitude(
        std::min(request.start.longitude, eastUnwrapped) - margin);
    area.east = normalizeLongitude(
        std::max(request.start.longitude, eastUnwrapped) + margin);
    area.south = std::max(-89.0, std::min(request.start.latitude,
                                          request.destination.latitude) -
                                    margin);
    area.north = std::min(89.0, std::max(request.start.latitude,
                                        request.destination.latitude) + margin);
    const double areaWidth = area.west <= area.east
                                 ? area.east - area.west
                                 : 360.0 - area.west + area.east;
    if (areaWidth * (area.north - area.south) >
        request.limits.maximumPredictionAreaSquareDegrees) {
      result.status = RoutingStatus::ResourceLimitReached;
      result.message = "initial current-prediction area exceeds resource limit";
      result.diagnostics.resourceLimitEvents.push_back(result.message);
      return output;
    }
    const auto coverage = environment.xtdCurrent->ensureCoverage(
        area, request.departure,
        request.departure + std::min(request.limits.maximumRouteDuration,
                                     Duration{std::chrono::hours{48}}),
        std::chrono::hours{1}, request.cancellation);
    if (coverage.status == CoverageStatus::Cancelled ||
        request.cancellation.cancelled()) {
      result.status = RoutingStatus::Cancelled;
      result.message = coverage.message;
      return output;
    }
    if (coverage.status == CoverageStatus::ResourceLimit) {
      result.status = RoutingStatus::ResourceLimitReached;
      result.message = coverage.message;
      result.diagnostics.resourceLimitEvents.push_back(
          coverage.message.empty() ? "current prediction resource limit"
                                   : coverage.message);
      return output;
    }
    if (coverage.status != CoverageStatus::Ready) {
      result.status = RoutingStatus::WeatherCoverageInsufficient;
      result.message = coverage.message.empty()
                           ? "current prediction coverage preparation failed"
                           : coverage.message;
      return output;
    }
  }

  // Keep the same performance model and checked motion as the other engines.
  // Only frontier selection is intentionally simpler. This is a controlled
  // search comparison, not a claim to reproduce Windy's weather data or API.
  PolarPerformanceModel fallback(request.vessel);
  const auto& performance =
      environment.performance ? *environment.performance : fallback;
  internal::MotionState root;
  root.position = request.start;
  root.time = request.departure;
  if (environment.landAndBoundaries &&
      request.constraints.landSafetyMarginNm > 0)
    root.departureEgressActive =
        environment.landAndBoundaries->distanceToForbiddenNm(request.start) <
        request.constraints.landSafetyMarginNm;
  std::vector<std::shared_ptr<const Node>> frontier{
      std::make_shared<Node>(Node{root, {}, {}})};
  output.alternative.lineageNodesAllocated = 1;
  result.diagnostics.stagesAttempted.push_back(SolverPath::AlternativeSector);
  RoutingStatus dataFailure = RoutingStatus::Complete;

  while (!frontier.empty()) {
    if (request.cancellation.cancelled()) {
      result.status = RoutingStatus::Cancelled;
      result.message = "Alternative route cancelled";
      return output;
    }
    if (result.diagnostics.generatedStates >= stateLimit) {
      result.status = RoutingStatus::ResourceLimitReached;
      result.message = "Alternative generated-state allowance reached";
      result.diagnostics.resourceLimitEvents.push_back(result.message);
      return output;
    }
    if (result.diagnostics.weatherSamples >= options.maximumWeatherSamples) {
      result.status = RoutingStatus::ResourceLimitReached;
      result.message = "Alternative weather-sample allowance reached";
      result.diagnostics.resourceLimitEvents.push_back(result.message);
      return output;
    }
    if (request.progress)
      request.progress({RoutingProgressStage::ForwardIsochrone, 1, 1,
                        result.diagnostics.generatedStates,
                        frontier.size(),
                        result.diagnostics.landChecks,
                        result.diagnostics.closestApproachNm, 100});
    std::vector<std::optional<Candidate>> best(options.sectors);
    std::optional<Candidate> closest;
    bool withinDuration = false;
    for (const auto& parent : frontier) {
      if (parent->state.time + options.timeStep >
          request.departure + request.limits.maximumRouteDuration)
        continue;
      withinDuration = true;
      const double bearing = initialBearingDegrees(parent->state.position,
                                                    request.destination);
      const unsigned headings = static_cast<unsigned>(
          std::ceil(360.0 / options.headingStepDegrees));
      for (unsigned h = 0; h < headings; ++h) {
        if (request.cancellation.cancelled()) {
          result.status = RoutingStatus::Cancelled;
          result.message = "Alternative route cancelled";
          return output;
        }
        const double heading = h * 360.0 / headings;
        if (std::abs(angularDifferenceDegrees(bearing, heading)) >
            request.options.maximumSearchAngleDegrees + 1e-9)
          continue;
        ++output.alternative.attemptedMotions;
        auto motions = internal::checkedMotion(
            request, environment, performance, parent->state, heading,
            options.timeStep, std::min(options.timeStep, Duration{300}),
            result.diagnostics,
            &dataFailure);
        for (auto& motion : motions) {
          ++result.diagnostics.generatedStates;
          if (result.diagnostics.generatedStates > stateLimit) break;
          const auto position = motion.state.position;
          if (distanceNm(request.start, position) >
              request.limits.maximumExplorationDistanceNm)
            continue;
          const double remaining = distanceNm(position, request.destination);
          result.diagnostics.closestApproachNm =
              std::min(result.diagnostics.closestApproachNm, remaining);

          // Windy's near-arrival/overshoot test runs before sector pruning.
          // Here a candidate is accepted only after an exact connection and
          // the independent chronological validator both succeed.
          const bool nearEndpoint = remaining <= options.arrivalRadiusNm;
          const bool crossedEndpoint = !nearEndpoint &&
              passesNearDestination(parent->state.position, position,
                                    request.destination, options.arrivalRadiusNm);
          if ((nearEndpoint || crossedEndpoint) &&
              output.alternative.validatedCandidates <
                  options.maximumValidatedCandidates) {
            const auto& connectFrom =
                crossedEndpoint ? parent->state : motion.state;
            const bool exactCandidate = nearEndpoint && remaining <= 1e-9;
            std::optional<internal::MotionCandidate> terminal;
            if (!exactCandidate)
              terminal = internal::checkedConnection(
                  request, environment, performance, connectFrom,
                  std::max(options.timeStep, Duration{7200}),
                  result.diagnostics);
            if (exactCandidate || terminal) {
              ++output.alternative.validatedCandidates;
              auto legs = lineage(parent);
              if (!crossedEndpoint) legs.push_back(motion.leg);
              if (terminal) legs.push_back(std::move(terminal->leg));
              if (environment.landAndBoundaries)
                environment.landAndBoundaries->prepareValidationRoute(
                    legs, request.constraints.landSafetyMarginNm);
              auto validation = RouteValidator{}.validate(
                  request, environment, performance, legs, nullptr);
              result.diagnostics.validationSamples += validation.samples;
              if (validation.passed) {
                result.legs = std::move(legs);
                result.validation = std::move(validation);
                result.status = RoutingStatus::Complete;
                result.solverPath = SolverPath::AlternativeSector;
                result.message = "Complete — Alternative sector route";
                internal::summariseRoute(result);
                return output;
              }
              result.diagnostics.stageStopReasons.push_back(
                  "Alternative candidate replay: " + validation.failureReason);
            }
          }
          const unsigned sector = std::min<unsigned>(
              options.sectors - 1,
              static_cast<unsigned>(normalizeHeading(
                                        initialBearingDegrees(request.start,
                                                              position) -
                                        initialBearingDegrees(
                                            request.start, request.destination)) /
                                    (360.0 / options.sectors)));
          double penalty = motion.state.mode == PropulsionMode::Sail
                               ? 1.0
                               : options.motoringPruneFactor;
          if (environment.landAndBoundaries &&
              options.preferredLandMarginNm > 0 &&
              environment.landAndBoundaries->distanceToForbiddenNm(position) <
                  options.preferredLandMarginNm)
            penalty *= options.nearLandPruneFactor;
          const double progress = distanceNm(request.start, position) * penalty;
          Candidate candidate{std::move(motion), parent, progress, remaining,
                              remaining / penalty, sector};
          if (!best[sector] || progress > best[sector]->progress + 1e-9)
            best[sector] = candidate;
          if (!closest || candidate.destinationPriority <
                              closest->destinationPriority - 1e-9)
            closest = std::move(candidate);
        }
        if (result.diagnostics.weatherSamples >= options.maximumWeatherSamples)
          break;
        if (result.diagnostics.generatedStates >= stateLimit) break;
      }
      if (result.diagnostics.weatherSamples >= options.maximumWeatherSamples)
        break;
      if (result.diagnostics.generatedStates >= stateLimit) break;
    }
    if (result.diagnostics.weatherSamples >= options.maximumWeatherSamples) {
      result.status = RoutingStatus::ResourceLimitReached;
      result.message = "Alternative weather-sample allowance reached";
      result.diagnostics.resourceLimitEvents.push_back(result.message);
      return output;
    }
    if (!withinDuration) break;
    std::vector<std::shared_ptr<const Node>> next;
    next.reserve(options.sectors + 1);
    bool closestKept = false;
    for (auto& slot : best) {
      if (!slot) continue;
      if (closest && slot->parent == closest->parent &&
          distanceNm(slot->motion.state.position,
                     closest->motion.state.position) < 1e-9)
        closestKept = true;
      next.push_back(std::make_shared<Node>(Node{
          std::move(slot->motion.state), std::move(slot->motion.leg),
          std::move(slot->parent)}));
    }
    if (closest && !closestKept)
      next.push_back(std::make_shared<Node>(Node{
          std::move(closest->motion.state), std::move(closest->motion.leg),
          std::move(closest->parent)}));
    if (next.size() > request.limits.maximumRetainedStates) {
      result.status = RoutingStatus::ResourceLimitReached;
      result.message = "Alternative retained-state allowance reached";
      result.diagnostics.resourceLimitEvents.push_back(result.message);
      return output;
    }
    output.alternative.lineageNodesAllocated += next.size();
    if (output.alternative.lineageNodesAllocated >
        options.maximumLineageNodes) {
      result.status = RoutingStatus::ResourceLimitReached;
      result.message = "Alternative lineage-node allowance reached";
      result.diagnostics.resourceLimitEvents.push_back(result.message);
      return output;
    }
    result.diagnostics.retainedStates =
        std::max<std::uint64_t>(result.diagnostics.retainedStates, next.size());
    frontier = std::move(next);
    ++output.alternative.layers;
  }
  result.status = dataFailure == RoutingStatus::Complete
                      ? RoutingStatus::SearchIncomplete
                      : dataFailure;
  result.message = "Alternative sector search found no validated route";
  return output;
}
}  // namespace supercpn::weather_routing
