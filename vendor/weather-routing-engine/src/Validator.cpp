#include "supercpn/weather_routing/Engine.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <sstream>

#include "RoutingInternal.h"

namespace supercpn::weather_routing {
namespace {
RouteValidationResult fail(RouteValidationResult result, std::string reason) {
  result.passed = false;
  result.failureReason = std::move(reason);
  return result;
}

bool closePoint(GeoPoint a, GeoPoint b) { return distanceNm(a, b) <= 0.002; }

void addUsage(EnvironmentalSourceUsage& usage,
              const EnvironmentalSnapshot& environment, Duration duration) {
  if (duration <= Duration::zero()) return;
  if (environment.wind.metadata.source == EnvironmentalSource::GribForecast)
    usage.gribWindDuration += duration;
  else if (environment.wind.metadata.source == EnvironmentalSource::Climatology)
    usage.climatologyWindDuration += duration;
  if (environment.current.metadata.source == EnvironmentalSource::GribForecast)
    usage.gribCurrentDuration += duration;
  else if (environment.current.metadata.source ==
           EnvironmentalSource::XtdCurrentPrediction)
    usage.xtdCurrentDuration += duration;
  else if (environment.current.metadata.source ==
           EnvironmentalSource::NoDataAssumedZero)
    usage.currentAssumedZeroDuration += duration;
  if (environment.waves.available &&
      environment.waves.metadata.source == EnvironmentalSource::GribForecast)
    usage.gribWaveDuration += duration;
  else
    usage.missingWaveDuration += duration;
}
}  // namespace

RouteValidationResult validateRoute(const RoutingRequest& request,
                                    const RoutingEnvironment& environment,
                                    const VesselPerformanceModel& performance,
                                    std::span<const RouteLeg> legs,
                                    RoutingDiagnostics* diagnostics,
                                    bool requireDestination) {
  RouteValidationResult result;
  if (legs.empty()) return fail(std::move(result), "route contains no legs");
  if (environment.landAndBoundaries)
    environment.landAndBoundaries->prepareValidationRoute(
        legs, request.constraints.landSafetyMarginNm);
  if (!closePoint(legs.front().start, request.start))
    return fail(std::move(result),
                "route does not start at the requested position");
  if (requireDestination && distanceNm(legs.back().end, request.destination) >
                                request.options.destinationToleranceNm + 1e-9)
    return fail(std::move(result),
                "route ends outside the configured destination tolerance");
  if (legs.front().startTime != request.departure)
    return fail(std::move(result),
                "route departure timestamp differs from request");
  if (environment.landAndBoundaries &&
      environment.landAndBoundaries->pointForbidden(request.start))
    return fail(std::move(result),
                "route starts inside land or an exclusion zone");

  PropulsionMode previousMode = legs.front().propulsionMode;
  Duration previousModeDuration{};
  Duration totalMotor{};
  Duration totalWait{};
  double totalFuel{};
  std::optional<EnvironmentalSnapshot> previousEnvironment;
  bool departureEgress = false;
  double departureClearanceNm = std::numeric_limits<double>::infinity();
  if (environment.landAndBoundaries &&
      request.constraints.landSafetyMarginNm > 0.0) {
    departureClearanceNm =
        environment.landAndBoundaries->distanceToForbiddenNm(request.start);
    departureEgress =
        departureClearanceNm < request.constraints.landSafetyMarginNm;
  }

  for (std::size_t legIndex = 0; legIndex < legs.size(); ++legIndex) {
    const RouteLeg& leg = legs[legIndex];
    if (leg.endTime <= leg.startTime)
      return fail(std::move(result),
                  "route timestamps are not strictly increasing");
    if (legIndex > 0) {
      const RouteLeg& prior = legs[legIndex - 1];
      if (!closePoint(prior.end, leg.start) || prior.endTime != leg.startTime)
        return fail(std::move(result),
                    "route legs are not chronologically continuous");
      if (leg.propulsionMode != previousMode &&
          previousMode != PropulsionMode::Sail &&
          previousModeDuration < request.vessel.propulsion.minimumMotorRun)
        return fail(std::move(result),
                    "propulsion transition violates minimum motor runtime");
      if (leg.propulsionMode != previousMode) previousModeDuration = Duration{};
      previousMode = leg.propulsionMode;
    }
    const Duration duration =
        std::chrono::duration_cast<Duration>(leg.endTime - leg.startTime);
    if (leg.stationaryWait) {
      if (!closePoint(leg.start, leg.end))
        return fail(std::move(result),
                    "stationary wait leg changes geographic position");
      totalWait += duration;
      if (!request.options.allowWaiting ||
          totalWait > request.options.maximumWait)
        return fail(std::move(result),
                    "route exceeds the bounded stationary-wait allowance");
      const unsigned samples = std::max<unsigned>(
          2, static_cast<unsigned>(std::ceil(
                 std::chrono::duration<double>(duration).count() / 300.0)));
      if (result.samples + samples > request.limits.maximumValidationSamples)
        return fail(std::move(result),
                    "validation sample resource limit reached");
      for (unsigned sampleIndex = 0; sampleIndex < samples; ++sampleIndex) {
        const Duration offset{static_cast<std::int64_t>(
            duration.count() * (sampleIndex + 1) / samples)};
        const auto resolved = internal::resolveEnvironment(
            request, environment, leg.start, leg.startTime + offset);
        ++result.samples;
        if (resolved.failureStatus != RoutingStatus::Complete)
          return fail(std::move(result),
                      "environmental validation failed during stationary "
                      "wait: " +
                          resolved.failureReason);
        if (request.constraints.maximumTrueWindKnots &&
            vectorMagnitudeKnots(resolved.snapshot.wind.velocity) >
                *request.constraints.maximumTrueWindKnots + 1e-9)
          return fail(std::move(result),
                      "stationary wait exceeds hard true-wind limit");
        if (request.constraints.maximumWaveHeightMetres &&
            resolved.snapshot.waves.available &&
            resolved.snapshot.waves.significantHeightMetres >
                *request.constraints.maximumWaveHeightMetres + 1e-9)
          return fail(std::move(result),
                      "stationary wait exceeds hard wave-height limit");
        if (request.constraints.minimumDepthMetres) {
          if (!environment.landAndBoundaries)
            return fail(std::move(result),
                        "minimum depth configured without depth provider");
          const auto depth =
              environment.landAndBoundaries->depthMetres(leg.start);
          if (!depth || *depth < *request.constraints.minimumDepthMetres)
            return fail(std::move(result),
                        "stationary wait violates hard minimum-depth limit");
        }
        addUsage(
            result.environment, resolved.snapshot,
            Duration{duration.count() / static_cast<std::int64_t>(samples)});
        internal::appendTransitions(
            result.sourceTransitions,
            previousEnvironment ? &*previousEnvironment : nullptr,
            resolved.snapshot, leg.start, leg.startTime + offset,
            request.environment);
        previousEnvironment = resolved.snapshot;
      }
      previousModeDuration = Duration{};
      ++result.acceptedPrefixLegs;
      continue;
    }
    if (leg.propulsionMode != PropulsionMode::Sail) totalMotor += duration;
    previousModeDuration += duration;
    if (request.vessel.propulsion.maximumMotorTime &&
        totalMotor > *request.vessel.propulsion.maximumMotorTime)
      return fail(std::move(result), "route exceeds maximum motor time");
    if (environment.landAndBoundaries) {
      if (diagnostics) ++diagnostics->landChecks;
      const bool destinationIngressLeg =
          requireDestination && request.constraints.landSafetyMarginNm > 0.0 &&
          legIndex + 1 == legs.size();
      const double destinationIngressRadiusNm =
          std::max(0.5, request.constraints.landSafetyMarginNm * 1.5);
      const double chordSafetyMarginNm =
          departureEgress ? 0.0 : request.constraints.landSafetyMarginNm;
      bool chordForbidden =
          environment.landAndBoundaries
              ->validationSegmentFromKnownSafeForbiddenAt(
                  leg.start, leg.end, leg.startTime, chordSafetyMarginNm);
      if (chordForbidden && destinationIngressLeg && !departureEgress) {
        const double chordDistance = distanceNm(leg.start, leg.end);
        const double bearing = initialBearingDegrees(leg.start, leg.end);
        const GeoPoint ingressStart =
            chordDistance <= destinationIngressRadiusNm
                ? leg.start
                : destinationPoint(leg.start, bearing,
                                   chordDistance - destinationIngressRadiusNm);
        chordForbidden = (chordDistance > destinationIngressRadiusNm &&
                          environment.landAndBoundaries
                              ->validationSegmentFromKnownSafeForbiddenAt(
                                  leg.start, ingressStart, leg.startTime,
                                  chordSafetyMarginNm)) ||
                         environment.landAndBoundaries
                             ->validationSegmentFromKnownSafeForbiddenAt(
                                 ingressStart, leg.end, leg.startTime, 0.0);
      }
      if (chordForbidden)
        return fail(
            std::move(result),
            "delivered route geometry intersects land or an exclusion zone");
    }

    const double legDistance = distanceNm(leg.start, leg.end);
    const double seconds = std::chrono::duration<double>(duration).count();
    if (!std::isfinite(legDistance) || seconds <= 0.0)
      return fail(std::move(result),
                  "route leg has invalid geometry or duration");
    unsigned samples = std::max<unsigned>(
        2, static_cast<unsigned>(std::ceil(seconds / 300.0)));
    samples = std::max<unsigned>(
        samples, static_cast<unsigned>(std::ceil(legDistance / 2.0)));
    if (leg.propulsionTransition || leg.tackTransition || leg.gybeTransition)
      samples = std::max<unsigned>(
          samples, static_cast<unsigned>(std::ceil(seconds / 300.0)));
    if (environment.landAndBoundaries &&
        environment.landAndBoundaries->distanceToForbiddenNm(leg.start) < 5.0)
      samples = std::max<unsigned>(
          samples,
          static_cast<unsigned>(std::ceil(std::max(1.0, legDistance) / 0.25)));
    Duration transition{};
    if (leg.propulsionTransition)
      transition += request.vessel.propulsion.modeChangePenalty;
    if (leg.tackTransition) transition += request.vessel.tackPenalty;
    if (leg.gybeTransition) transition += request.vessel.gybePenalty;
    if (legIndex > 0) {
      const RouteLeg& prior = legs[legIndex - 1];
      if (prior.sailPlan >= 0 && leg.sailPlan >= 0 &&
          prior.sailPlan != leg.sailPlan)
        transition += request.vessel.sailPlanChangePenalty;
    }
    if (transition >= duration)
      return fail(std::move(result),
                  "route leg leaves no time for forward motion after "
                  "transition penalties");
    const Duration moving = duration - transition;
    const bool replaySearchCadence =
        leg.integrationMaximumSlice > Duration::zero();
    const Duration replaySlice = replaySearchCadence
                                     ? std::clamp(leg.integrationMaximumSlice,
                                                  Duration{1},
                                                  Duration{std::chrono::minutes{5}})
                                     : Duration{};
    if (replaySearchCadence)
      samples = static_cast<unsigned>(
          (moving.count() + replaySlice.count() - 1) / replaySlice.count());
    if (result.samples + static_cast<std::uint64_t>(samples) * 2U + 1U >
        request.limits.maximumValidationSamples)
      return fail(std::move(result),
                  "validation sample resource limit reached");

    GeoPoint replayPoint = leg.start;
    Duration elapsed{};
    double replayFuel{};
    bool destinationIngress = false;
    double priorDestinationDistance =
        distanceNm(replayPoint, request.destination);
    for (unsigned sampleIndex = 0; sampleIndex < samples; ++sampleIndex) {
      const Duration slice =
          replaySearchCadence
              ? std::min(replaySlice, moving - elapsed)
              : (sampleIndex + 1 == samples
                     ? moving - elapsed
                     : Duration{moving.count() /
                                static_cast<std::int64_t>(samples)});
      if (slice <= Duration::zero()) continue;
      const TimePoint sliceStartTime = leg.startTime + transition + elapsed;
      const auto predictorEnvironment = internal::resolveEnvironment(
          request, environment, replayPoint, sliceStartTime);
      ++result.samples;
      if (predictorEnvironment.failureStatus != RoutingStatus::Complete) {
        std::ostringstream message;
        message << "environmental validation failed on leg " << legIndex << ": "
                << predictorEnvironment.failureReason;
        return fail(std::move(result), message.str());
      }
      const double predictorTws =
          vectorMagnitudeKnots(predictorEnvironment.snapshot.wind.velocity);
      const double predictorTwa =
          trueWindAngleDegrees(predictorEnvironment.snapshot.wind.velocity,
                               leg.courseThroughWaterDegrees);
      const auto predictorPerformance = performance.evaluateAt(
          replayPoint, sliceStartTime, leg.propulsionMode, leg.profileRole,
          leg.profileIdentity, predictorTws, predictorTwa,
          predictorEnvironment.snapshot.waves);
      if (!predictorPerformance.valid)
        return fail(std::move(result),
                    "selected propulsion/profile is unavailable during replay");
      const Vector2 predictorWater =
          speedDirectionToVector(predictorPerformance.speedThroughWaterKnots,
                                 leg.courseThroughWaterDegrees);
      const Vector2 predictorGround{
          predictorWater.eastKnots +
              predictorEnvironment.snapshot.current.velocity.eastKnots,
          predictorWater.northKnots +
              predictorEnvironment.snapshot.current.velocity.northKnots};
      const double predictorSog = vectorMagnitudeKnots(predictorGround);
      if (!std::isfinite(predictorSog) || predictorSog <= 0.05)
        return fail(std::move(result),
                    "route replay predictor has no positive speed over ground");
      const GeoPoint midpoint = destinationPoint(
          replayPoint, vectorDirectionToDegrees(predictorGround),
          predictorSog * std::chrono::duration<double>(slice).count() / 7200.0);
      const TimePoint time = sliceStartTime + Duration{slice.count() / 2};
      const auto resolved =
          internal::resolveEnvironment(request, environment, midpoint, time);
      ++result.samples;
      if (resolved.failureStatus != RoutingStatus::Complete) {
        std::ostringstream message;
        message << "environmental validation failed on leg " << legIndex << ": "
                << resolved.failureReason;
        return fail(std::move(result), message.str());
      }
      addUsage(result.environment, resolved.snapshot, slice);
      internal::appendTransitions(
          result.sourceTransitions,
          previousEnvironment ? &*previousEnvironment : nullptr,
          resolved.snapshot, midpoint, time, request.environment);
      previousEnvironment = resolved.snapshot;

      const double tws = vectorMagnitudeKnots(resolved.snapshot.wind.velocity);
      const double twa = trueWindAngleDegrees(resolved.snapshot.wind.velocity,
                                              leg.courseThroughWaterDegrees);
      if (twa + 1e-9 < request.constraints.minimumTrueWindAngleDegrees ||
          twa - 1e-9 > request.constraints.maximumTrueWindAngleDegrees)
        return fail(std::move(result),
                    "route violates hard true-wind-angle bounds");
      const auto achievable = performance.evaluateAt(
          midpoint, time, leg.propulsionMode, leg.profileRole,
          leg.profileIdentity, tws, twa, resolved.snapshot.waves);
      if (!achievable.valid) {
        std::ostringstream message;
        message << "selected propulsion/profile is unavailable on leg "
                << legIndex;
        return fail(std::move(result), message.str());
      }
      if (request.constraints.maximumTrueWindKnots &&
          tws > *request.constraints.maximumTrueWindKnots + 1e-9)
        return fail(std::move(result), "route exceeds hard true-wind limit");
      if (request.constraints.maximumApparentWindKnots) {
        const Vector2 water = speedDirectionToVector(
            achievable.speedThroughWaterKnots, leg.courseThroughWaterDegrees);
        const Vector2 apparent{
            resolved.snapshot.wind.velocity.eastKnots - water.eastKnots,
            resolved.snapshot.wind.velocity.northKnots - water.northKnots};
        if (vectorMagnitudeKnots(apparent) >
            *request.constraints.maximumApparentWindKnots + 1e-9)
          return fail(std::move(result),
                      "route exceeds hard apparent-wind limit");
      }
      if (request.constraints.maximumOpposingWindCurrent) {
        const double opposing =
            -(resolved.snapshot.wind.velocity.eastKnots *
                  resolved.snapshot.current.velocity.eastKnots +
              resolved.snapshot.wind.velocity.northKnots *
                  resolved.snapshot.current.velocity.northKnots);
        if (opposing > *request.constraints.maximumOpposingWindCurrent + 1e-9)
          return fail(std::move(result),
                      "route exceeds hard wind-against-current limit");
      }
      if (request.constraints.maximumWaveHeightMetres &&
          resolved.snapshot.waves.available &&
          resolved.snapshot.waves.significantHeightMetres >
              *request.constraints.maximumWaveHeightMetres + 1e-9)
        return fail(std::move(result), "route exceeds hard wave-height limit");
      if (!std::isfinite(achievable.speedThroughWaterKnots) ||
          !std::isfinite(tws) || !std::isfinite(twa))
        return fail(std::move(result),
                    "route replay produced a non-finite value");
      if (request.constraints.minimumDepthMetres) {
        if (!environment.landAndBoundaries)
          return fail(std::move(result),
                      "minimum depth configured without depth provider");
        const auto depth = environment.landAndBoundaries->depthMetres(midpoint);
        if (!depth)
          return fail(std::move(result),
                      "depth data missing during validation");
        if (*depth < *request.constraints.minimumDepthMetres)
          return fail(std::move(result),
                      "route violates hard minimum-depth limit");
      }

      const Vector2 water = speedDirectionToVector(
          achievable.speedThroughWaterKnots, leg.courseThroughWaterDegrees);
      const Vector2 ground{
          water.eastKnots + resolved.snapshot.current.velocity.eastKnots,
          water.northKnots + resolved.snapshot.current.velocity.northKnots};
      const double sog = vectorMagnitudeKnots(ground);
      if (!std::isfinite(sog) || sog <= 0.05)
        return fail(std::move(result),
                    "route replay has no positive speed over ground");
      const GeoPoint next = destinationPoint(
          replayPoint, vectorDirectionToDegrees(ground),
          sog * std::chrono::duration<double>(slice).count() / 3600.0);
      if (environment.landAndBoundaries) {
        if (diagnostics) ++diagnostics->landChecks;
        if (departureEgress) {
          if (environment.landAndBoundaries
                  ->validationSegmentFromKnownSafeForbiddenAt(
                      replayPoint, next, leg.startTime + elapsed, 0.0))
            return fail(std::move(result),
                        "forward replay intersects land or an exclusion zone");
          const double nextClearance =
              environment.landAndBoundaries->distanceToForbiddenNm(next);
          if (nextClearance + 1e-6 < departureClearanceNm)
            return fail(
                std::move(result),
                "coastal departure moves deeper into the safety buffer");
          departureClearanceNm = nextClearance;
          departureEgress =
              nextClearance + 1e-6 < request.constraints.landSafetyMarginNm;
        } else {
          bool forbidden =
              environment.landAndBoundaries
                  ->validationSegmentFromKnownSafeForbiddenAt(
                      replayPoint, next, leg.startTime + elapsed,
                      destinationIngress
                          ? 0.0
                          : request.constraints.landSafetyMarginNm);
          const bool finalIngressLeg =
              requireDestination &&
              request.constraints.landSafetyMarginNm > 0.0 &&
              legIndex + 1 == legs.size();
          const double destinationIngressRadiusNm =
              std::max(0.5, request.constraints.landSafetyMarginNm * 1.5);
          const double nextDestinationDistance =
              distanceNm(next, request.destination);
          if (forbidden && finalIngressLeg &&
              nextDestinationDistance <= destinationIngressRadiusNm + 1e-6 &&
              !destinationIngress &&
              nextDestinationDistance + 1e-6 < priorDestinationDistance &&
              !environment.landAndBoundaries
                   ->validationSegmentFromKnownSafeForbiddenAt(
                       replayPoint, next, leg.startTime + elapsed, 0.0)) {
            destinationIngress = true;
            forbidden = false;
          }
          if (forbidden)
            return fail(std::move(result),
                        "forward replay intersects land or an exclusion zone");
          if (destinationIngress &&
              nextDestinationDistance > priorDestinationDistance + 1e-6)
            return fail(
                std::move(result),
                "coastal destination ingress moves away from the destination "
                "inside the safety buffer");
          priorDestinationDistance = nextDestinationDistance;
        }
      }
      replayPoint = next;
      replayFuel += achievable.fuelLitresPerHour *
                    std::chrono::duration<double>(slice).count() / 3600.0;
      elapsed += slice;
    }
    // Exploratory legs are integrated in 15-minute slices while authoritative
    // replay uses at least five-minute slices (and one-minute destination
    // connections).  A very short percentage-based tolerance can therefore
    // reject an otherwise fully feasible and chart-safe leg for the expected
    // numerical difference between those integrations, then trigger millions
    // of unnecessary recovery states.  Keep this tightly bounded at 0.15 NM
    // and explicitly chart-check the small reconciliation below.
    constexpr double endpointToleranceNm = 0.15;
    const double endpointErrorNm = distanceNm(replayPoint, leg.end);
    if (endpointErrorNm > endpointToleranceNm) {
      std::ostringstream message;
      message << "leg " << legIndex
              << " requires a trajectory that forward replay misses by "
              << endpointErrorNm << " NM (tolerance " << endpointToleranceNm
              << " NM)";
      return fail(std::move(result), message.str());
    }
    if (endpointErrorNm > 0.002 && environment.landAndBoundaries) {
      const double reconciliationMarginNm =
          departureEgress || destinationIngress
              ? 0.0
              : request.constraints.landSafetyMarginNm;
      if (environment.landAndBoundaries
              ->validationSegmentFromKnownSafeForbiddenAt(
                  replayPoint, leg.end, leg.endTime, reconciliationMarginNm))
        return fail(
            std::move(result),
            "forward replay endpoint reconciliation intersects land or an "
            "exclusion zone");
    }
    if (leg.estimatedFuelLitres + 1e-6 < replayFuel)
      return fail(std::move(result),
                  "route leg understates independently replayed fuel use");
    totalFuel += replayFuel;
    if (request.vessel.propulsion.maximumFuelLitres &&
        totalFuel > *request.vessel.propulsion.maximumFuelLitres + 1e-6)
      return fail(std::move(result), "route exceeds fuel limit");
    ++result.acceptedPrefixLegs;
  }
  result.passed = true;
  return result;
}

RouteValidationResult RouteValidator::validate(
    const RoutingRequest& request, const RoutingEnvironment& environment,
    const VesselPerformanceModel& performance, std::span<const RouteLeg> legs,
    RoutingDiagnostics* diagnostics) const {
  return validateRoute(request, environment, performance, legs, diagnostics,
                       true);
}

RouteValidationResult RouteValidator::validatePrefix(
    const RoutingRequest& request, const RoutingEnvironment& environment,
    const VesselPerformanceModel& performance, std::span<const RouteLeg> legs,
    RoutingDiagnostics* diagnostics) const {
  return validateRoute(request, environment, performance, legs, diagnostics,
                       false);
}

}  // namespace supercpn::weather_routing
