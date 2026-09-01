#include "supercpn/weather_routing/Engine.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <iterator>
#include <map>
#include <numbers>
#include <queue>
#include <set>
#include <sstream>
#include <tuple>
#include <unordered_map>

#include "RoutingInternal.h"

namespace supercpn::weather_routing {
namespace {
using internal::ResolvedEnvironment;

struct TimedMotionSegment {
  GeoPoint start;
  GeoPoint end;
  TimePoint startTime{};
};

struct Node {
  GeoPoint position;
  TimePoint time{};
  double incomingHeading{};
  Tack tack{Tack::Unknown};
  PropulsionMode mode{PropulsionMode::Sail};
  ProfileRole role{ProfileRole::SailOnly};
  std::string profileIdentity;
  int sailPlan{-1};
  Duration modeDuration{};
  // True only until this lineage first clears the configured stand-off at
  // the departure. It must never be inferred again from a later coastal
  // position: doing so would incorrectly grant departure-egress semantics
  // elsewhere on the route.
  bool departureEgressActive{};
  Duration motorDuration{};
  Duration waitDuration{};
  double fuel{};
  double risk{};
  unsigned manoeuvres{};
  std::size_t predecessor{std::numeric_limits<std::size_t>::max()};
  RouteLeg incomingLeg;
  std::vector<TimedMotionSegment> incomingMotionSegments;
};

struct SearchArtifacts {
  std::vector<Node> nodes;
  std::vector<std::size_t> retained;
  std::vector<std::vector<std::size_t>> recoveryFrontiers;
  std::vector<IsochroneLayer> isochrones;
  std::optional<std::size_t> solution;
  std::vector<std::size_t> alternativeSolutions;
  std::vector<std::size_t> preferredRecoverySeeds;
  RoutingStatus failure{RoutingStatus::NoFeasibleRoute};
  std::string reason;
  bool resourceLimited{};
  bool convergenceStalled{};
};

void reportProgress(const RoutingRequest& request, RoutingProgressStage stage,
                    const RoutingDiagnostics& diagnostics, unsigned attempt = 0,
                    unsigned totalAttempts = 0) {
  if (!request.progress) return;
  request.progress({stage, attempt, totalAttempts, diagnostics.generatedStates,
                    diagnostics.retainedStates, diagnostics.landChecks,
                    diagnostics.closestApproachNm});
}

bool finitePoint(GeoPoint point) {
  return std::isfinite(point.latitude) && std::isfinite(point.longitude) &&
         point.latitude >= -90.0 && point.latitude <= 90.0 &&
         point.longitude >= -180.0 && point.longitude <= 180.0;
}

double searchLandMarginNm(const RoutingRequest& request) {
  const double configured = request.constraints.landSafetyMarginNm;
  return configured > 0.0 ? configured + 0.01 : 0.0;
}

Tack tackFor(double course, Vector2 wind) {
  const double from = normalizeHeading(vectorDirectionToDegrees(wind) + 180.0);
  return angularDifferenceDegrees(course, from) >= 0.0 ? Tack::Starboard
                                                       : Tack::Port;
}

bool isMotor(PropulsionMode mode) { return mode != PropulsionMode::Sail; }

bool hardEnvironmentConstraints(const RoutingRequest& request,
                                const EnvironmentalSnapshot& environment);

std::optional<Node> waitInPlace(const RoutingRequest& request,
                                const RoutingEnvironment& environment,
                                const Node& from, Duration duration,
                                RoutingDiagnostics& diagnostics) {
  if (!request.options.allowWaiting || duration <= Duration::zero() ||
      from.waitDuration + duration > request.options.maximumWait)
    return {};
  if (environment.landAndBoundaries &&
      environment.landAndBoundaries->pointForbidden(from.position))
    return {};
  const TimePoint midpoint = from.time + Duration{duration.count() / 2};
  const auto resolved = internal::resolveEnvironment(request, environment,
                                                     from.position, midpoint);
  ++diagnostics.weatherSamples;
  if (resolved.failureStatus != RoutingStatus::Complete ||
      !hardEnvironmentConstraints(request, resolved.snapshot))
    return {};
  if (request.constraints.minimumDepthMetres) {
    if (!environment.landAndBoundaries) return {};
    const auto depth =
        environment.landAndBoundaries->depthMetres(from.position);
    if (!depth || *depth < *request.constraints.minimumDepthMetres) return {};
  }

  Node node = from;
  node.time += duration;
  node.waitDuration += duration;
  node.incomingLeg = {};
  node.incomingLeg.start = from.position;
  node.incomingLeg.end = from.position;
  node.incomingLeg.startTime = from.time;
  node.incomingLeg.endTime = node.time;
  node.incomingLeg.headingDegrees = from.incomingHeading;
  node.incomingLeg.courseThroughWaterDegrees = from.incomingHeading;
  node.incomingLeg.courseOverGroundDegrees = from.incomingHeading;
  node.incomingLeg.wind = resolved.snapshot.wind.velocity;
  node.incomingLeg.windSource = resolved.snapshot.wind.metadata;
  node.incomingLeg.current = resolved.snapshot.current.velocity;
  node.incomingLeg.currentSource = resolved.snapshot.current.metadata;
  node.incomingLeg.waves = resolved.snapshot.waves;
  node.incomingLeg.waveSource = resolved.snapshot.waves.metadata;
  node.incomingLeg.propulsionMode = from.mode;
  node.incomingLeg.profileRole = from.role;
  node.incomingLeg.profileIdentity = from.profileIdentity;
  node.incomingLeg.sailPlan = from.sailPlan;
  node.incomingLeg.tack = from.tack;
  node.incomingLeg.stationaryWait = true;
  for (const auto& warning : resolved.warnings)
    node.incomingLeg.warnings.push_back(warning);
  ++diagnostics.waitStates;
  return node;
}

double stateCost(const RoutingRequest& request, const Node& node) {
  const double elapsed =
      std::chrono::duration<double>(node.time - request.departure).count();
  switch (request.objective.kind) {
    case ObjectiveKind::FuelAware:
      return elapsed * request.objective.timeWeight +
             node.fuel * 3600.0 * request.objective.fuelWeight;
    case ObjectiveKind::WeightedTimeRiskComfort:
      return elapsed * request.objective.timeWeight +
             node.risk * request.objective.riskWeight;
    default:
      return elapsed;
  }
}

std::vector<double> headings(double step, double bearing, bool adaptive,
                             double refinedStep, double maximumSearchAngle) {
  std::set<int> values;
  auto add = [&](double value) {
    values.insert(
        static_cast<int>(std::llround(normalizeHeading(value) * 1000.0)));
  };
  const auto headingCount = static_cast<unsigned>(std::ceil(360.0 / step));
  for (unsigned i = 0; i < headingCount; ++i) {
    const double heading = i * step;
    if (heading < 360.0 - 1e-9 &&
        std::abs(angularDifferenceDegrees(bearing, heading)) <=
            maximumSearchAngle + 1e-9)
      add(heading);
  }
  add(bearing);
  if (adaptive) {
    for (int i = -4; i <= 4; ++i) add(bearing + i * refinedStep);
  }
  std::vector<double> result;
  result.reserve(values.size());
  for (int value : values) result.push_back(value / 1000.0);
  std::stable_sort(result.begin(), result.end(), [&](double a, double b) {
    return std::tuple{std::abs(angularDifferenceDegrees(bearing, a)), a} <
           std::tuple{std::abs(angularDifferenceDegrees(bearing, b)), b};
  });
  return result;
}

std::vector<double> graphCorridorSchedule(const RoutingOptions& options) {
  const double initial = std::max(0.0, options.graphCorridorWidthNm);
  if (!std::isfinite(initial))
    return {std::numeric_limits<double>::infinity()};

  const double requestedMaximum = options.maximumGraphCorridorWidthNm;
  const double maximum =
      std::isfinite(requestedMaximum)
          ? std::max(initial, requestedMaximum)
          : std::numeric_limits<double>::infinity();
  std::vector<double> widths{initial};
  if (maximum <= initial + 1e-9) return widths;

  // Two intermediate doublings retain the fast, focused graph behaviour for
  // ordinary passages without jumping directly from a local coastal search
  // to an unbounded ocean-wide search. The final stage still covers the
  // complete configured envelope (or becomes unbounded when an external
  // geometric constraint, such as OpenCPN's MaxDivertedCourse, is
  // authoritative).
  double intermediate = std::max(initial + 1.0, initial * 2.0);
  for (int stage = 0; stage < 2 && intermediate < maximum; ++stage) {
    widths.push_back(intermediate);
    intermediate = std::max(intermediate + 1.0, intermediate * 2.0);
  }
  widths.push_back(maximum);
  return widths;
}

std::string graphCorridorDescription(double widthNm) {
  if (!std::isfinite(widthNm)) return "unbounded";
  std::ostringstream stream;
  stream.setf(std::ios::fixed);
  stream.precision(1);
  stream << widthNm << " NM";
  return stream.str();
}

bool hardEnvironmentConstraints(const RoutingRequest& request,
                                const EnvironmentalSnapshot& environment) {
  if (request.constraints.maximumTrueWindKnots &&
      vectorMagnitudeKnots(environment.wind.velocity) >
          *request.constraints.maximumTrueWindKnots)
    return false;
  if (request.constraints.maximumWaveHeightMetres &&
      environment.waves.available &&
      environment.waves.significantHeightMetres >
          *request.constraints.maximumWaveHeightMetres)
    return false;
  if (request.constraints.maximumOpposingWindCurrent) {
    const double opposing = -(environment.wind.velocity.eastKnots *
                                  environment.current.velocity.eastKnots +
                              environment.wind.velocity.northKnots *
                                  environment.current.velocity.northKnots);
    if (opposing > *request.constraints.maximumOpposingWindCurrent)
      return false;
  }
  return true;
}

bool hardMotionConstraints(const RoutingRequest& request,
                           const EnvironmentalSnapshot& environment,
                           double heading, double speedThroughWater) {
  const double twa = trueWindAngleDegrees(environment.wind.velocity, heading);
  if (twa + 1e-9 < request.constraints.minimumTrueWindAngleDegrees ||
      twa - 1e-9 > request.constraints.maximumTrueWindAngleDegrees)
    return false;
  if (request.constraints.maximumApparentWindKnots) {
    const Vector2 boat = speedDirectionToVector(speedThroughWater, heading);
    const Vector2 apparent{
        environment.wind.velocity.eastKnots - boat.eastKnots,
        environment.wind.velocity.northKnots - boat.northKnots};
    if (vectorMagnitudeKnots(apparent) >
        *request.constraints.maximumApparentWindKnots)
      return false;
  }
  return true;
}

Duration transitionPenalty(const RoutingRequest& request, const Node& from,
                           PropulsionMode nextMode, Tack nextTack,
                           int nextSailPlan, double twa) {
  Duration penalty{};
  if (nextMode != from.mode)
    penalty += request.vessel.propulsion.modeChangePenalty;
  if (from.tack != Tack::Unknown && nextTack != from.tack) {
    penalty +=
        twa <= 90.0 ? request.vessel.tackPenalty : request.vessel.gybePenalty;
  }
  if (from.sailPlan >= 0 && nextSailPlan >= 0 && from.sailPlan != nextSailPlan)
    penalty += request.vessel.sailPlanChangePenalty;
  return penalty;
}

RouteLeg buildLeg(const RoutingRequest& request, const Node& from,
                  const ResolvedEnvironment& resolved,
                  const PerformanceCandidate& performance, double heading,
                  Vector2 groundVelocity, GeoPoint end, Duration duration,
                  Tack nextTack, Duration movingDuration,
                  Duration integrationMaximumSlice) {
  RouteLeg leg;
  leg.start = from.position;
  leg.end = end;
  leg.startTime = from.time;
  leg.endTime = from.time + duration;
  leg.headingDegrees = heading;
  leg.courseThroughWaterDegrees = heading;
  leg.courseOverGroundDegrees = vectorDirectionToDegrees(groundVelocity);
  leg.speedThroughWaterKnots = performance.speedThroughWaterKnots;
  leg.speedOverGroundKnots = vectorMagnitudeKnots(groundVelocity);
  leg.trueWindSpeedKnots =
      vectorMagnitudeKnots(resolved.snapshot.wind.velocity);
  leg.trueWindAngleDegrees =
      trueWindAngleDegrees(resolved.snapshot.wind.velocity, heading);
  leg.wind = resolved.snapshot.wind.velocity;
  leg.windSource = resolved.snapshot.wind.metadata;
  leg.current = resolved.snapshot.current.velocity;
  leg.currentSource = resolved.snapshot.current.metadata;
  leg.waves = resolved.snapshot.waves;
  leg.waveSource = resolved.snapshot.waves.metadata;
  leg.propulsionMode = performance.mode;
  leg.profileRole = performance.role;
  leg.profileIdentity = performance.profileIdentity;
  leg.sailPlan = performance.sailPlan;
  leg.tack = nextTack;
  leg.coastalDepartureEgress = from.departureEgressActive;
  leg.propulsionTransition = performance.mode != from.mode;
  leg.tackTransition = from.tack != Tack::Unknown && nextTack != from.tack &&
                       leg.trueWindAngleDegrees <= 90.0;
  leg.gybeTransition = from.tack != Tack::Unknown && nextTack != from.tack &&
                       leg.trueWindAngleDegrees > 90.0;
  leg.integrationMaximumSlice = std::clamp(
      integrationMaximumSlice, Duration{1}, Duration{std::chrono::minutes{5}});
  leg.estimatedFuelLitres =
      performance.fuelLitresPerHour *
      std::chrono::duration<double>(movingDuration).count() / 3600.0;
  if (request.constraints.maximumTrueWindKnots)
    leg.margins.windKnots =
        *request.constraints.maximumTrueWindKnots - leg.trueWindSpeedKnots;
  if (request.constraints.maximumWaveHeightMetres && leg.waves.available)
    leg.margins.waveMetres = *request.constraints.maximumWaveHeightMetres -
                             leg.waves.significantHeightMetres;
  for (const auto& warning : resolved.warnings) leg.warnings.push_back(warning);
  return leg;
}

bool withinPropulsionLimits(const RoutingRequest& request, const Node& node,
                            Duration additionalMotor, double additionalFuel) {
  if (request.vessel.propulsion.maximumMotorTime &&
      node.motorDuration + additionalMotor >
          *request.vessel.propulsion.maximumMotorTime)
    return false;
  if (request.vessel.propulsion.maximumFuelLitres &&
      node.fuel + additionalFuel > *request.vessel.propulsion.maximumFuelLitres)
    return false;
  return true;
}

struct MotionReplay {
  GeoPoint end;
  PerformanceCandidate finalPerformance;
  double averageSpeedThroughWaterKnots{};
  double fuelLitres{};
  double risk{};
  std::vector<TimedMotionSegment> segments;
};

// Integrate one fixed through-water course with the selected
// propulsion/profile. Resolve the environment at every slice boundary as
// well as its midpoint. Reusing the preceding midpoint at the next boundary
// gives the search and the independent validator different trajectories in
// spatially or temporally varying weather and current.
std::optional<MotionReplay> integrateMotion(
    const RoutingRequest& request, const RoutingEnvironment& environment,
    const VesselPerformanceModel& performanceModel, const Node& from,
    double heading, const PerformanceCandidate& selected, TimePoint movingStart,
    Duration moving, Duration maximumSlice, RoutingDiagnostics& diagnostics,
    RoutingStatus* dataFailure) {
  if (moving <= Duration::zero()) return {};
  MotionReplay replay;
  replay.end = from.position;
  replay.finalPerformance = selected;
  maximumSlice =
      std::clamp(maximumSlice, Duration{1}, Duration{std::chrono::minutes{5}});
  Duration elapsed{};
  double speedSeconds = 0.0;
  auto predictorResolved = internal::resolveEnvironment(
      request, environment, replay.end, movingStart);
  ++diagnostics.weatherSamples;
  if (predictorResolved.failureStatus != RoutingStatus::Complete) {
    if (dataFailure) *dataFailure = predictorResolved.failureStatus;
    return {};
  }
  while (elapsed < moving) {
    const Duration slice = std::min(maximumSlice, moving - elapsed);
    const double startTws =
        vectorMagnitudeKnots(predictorResolved.snapshot.wind.velocity);
    const double startTwa =
        trueWindAngleDegrees(predictorResolved.snapshot.wind.velocity, heading);
    const auto startPerformance = performanceModel.evaluateAt(
        replay.end, movingStart + elapsed, selected.mode, selected.role,
        selected.profileIdentity, startTws, startTwa,
        predictorResolved.snapshot.waves);
    if (!startPerformance.valid ||
        !hardEnvironmentConstraints(request, predictorResolved.snapshot) ||
        !hardMotionConstraints(request, predictorResolved.snapshot, heading,
                               startPerformance.speedThroughWaterKnots))
      return {};

    const Vector2 startWater = speedDirectionToVector(
        startPerformance.speedThroughWaterKnots, heading);
    const Vector2 startGround{
        startWater.eastKnots +
            predictorResolved.snapshot.current.velocity.eastKnots,
        startWater.northKnots +
            predictorResolved.snapshot.current.velocity.northKnots};
    const double startSog = vectorMagnitudeKnots(startGround);
    if (!std::isfinite(startSog) || startSog <= 0.05) return {};
    const auto midpointFor = [&](Duration interval) {
      const double halfHours =
          std::chrono::duration<double>(interval).count() / 7200.0;
      return destinationPoint(replay.end, vectorDirectionToDegrees(startGround),
                              startSog * halfHours);
    };
    GeoPoint midpoint = midpointFor(slice);
    auto midpointResolved = internal::resolveEnvironment(
        request, environment, midpoint,
        movingStart + elapsed + Duration{slice.count() / 2});
    ++diagnostics.weatherSamples;
    if (midpointResolved.failureStatus != RoutingStatus::Complete) {
      if (dataFailure) *dataFailure = midpointResolved.failureStatus;
      return {};
    }
    const double midpointTws =
        vectorMagnitudeKnots(midpointResolved.snapshot.wind.velocity);
    const double midpointTwa =
        trueWindAngleDegrees(midpointResolved.snapshot.wind.velocity, heading);
    auto midpointPerformance = performanceModel.evaluateAt(
        midpoint, movingStart + elapsed + Duration{slice.count() / 2},
        selected.mode, selected.role, selected.profileIdentity, midpointTws,
        midpointTwa, midpointResolved.snapshot.waves);
    if (!midpointPerformance.valid ||
        !hardEnvironmentConstraints(request, midpointResolved.snapshot) ||
        !hardMotionConstraints(request, midpointResolved.snapshot, heading,
                               midpointPerformance.speedThroughWaterKnots))
      return {};

    const Vector2 water = speedDirectionToVector(
        midpointPerformance.speedThroughWaterKnots, heading);
    const Vector2 ground{
        water.eastKnots + midpointResolved.snapshot.current.velocity.eastKnots,
        water.northKnots +
            midpointResolved.snapshot.current.velocity.northKnots};
    const double sog = vectorMagnitudeKnots(ground);
    if (!std::isfinite(sog) || sog <= 0.05) return {};
    const double sliceSeconds = std::chrono::duration<double>(slice).count();
    const GeoPoint next =
        destinationPoint(replay.end, vectorDirectionToDegrees(ground),
                         sog * sliceSeconds / 3600.0);
    replay.segments.push_back({replay.end, next, movingStart + elapsed});
    replay.end = next;
    replay.finalPerformance = std::move(midpointPerformance);
    speedSeconds +=
        replay.finalPerformance.speedThroughWaterKnots * sliceSeconds;
    replay.fuelLitres +=
        replay.finalPerformance.fuelLitresPerHour * sliceSeconds / 3600.0;
    if (midpointResolved.snapshot.waves.available)
      replay.risk += midpointResolved.snapshot.waves.significantHeightMetres *
                     sliceSeconds / 3600.0;
    elapsed += slice;
    if (elapsed < moving) {
      predictorResolved = internal::resolveEnvironment(
          request, environment, replay.end, movingStart + elapsed);
      ++diagnostics.weatherSamples;
      if (predictorResolved.failureStatus != RoutingStatus::Complete) {
        if (dataFailure) *dataFailure = predictorResolved.failureStatus;
        return {};
      }
    }
  }
  replay.averageSpeedThroughWaterKnots =
      speedSeconds / std::chrono::duration<double>(moving).count();
  return replay;
}

bool nodeMotionForbidden(const RoutingRequest& request,
                         const RoutingEnvironment& environment,
                         Node& node, RoutingDiagnostics& diagnostics) {
  if (!environment.landAndBoundaries) return false;
  double departureClearanceNm = std::numeric_limits<double>::infinity();
  bool departureEgress = node.departureEgressActive;
  if (departureEgress && request.constraints.landSafetyMarginNm > 0.0) {
    departureClearanceNm =
        environment.landAndBoundaries->distanceToForbiddenNm(
            node.incomingLeg.start);
  }
  const bool chordStartsInDepartureEgress = departureEgress;
  const bool destinationIngress =
      distanceNm(node.position, request.destination) <= 1e-6 &&
      request.constraints.landSafetyMarginNm > 0.0;
  const double destinationIngressRadiusNm =
      std::max(0.5, request.constraints.landSafetyMarginNm * 1.5);
  const double margin = searchLandMarginNm(request);
  bool ingressStarted = false;
  double priorDestinationDistance =
      distanceNm(node.incomingLeg.start, request.destination);
  for (const auto& segment : node.incomingMotionSegments) {
    ++diagnostics.landChecks;
    bool forbidden =
        environment.landAndBoundaries->segmentFromKnownSafeForbiddenAt(
            segment.start, segment.end, segment.startTime,
            departureEgress || ingressStarted ? 0.0 : margin);
    if (forbidden && destinationIngress && !departureEgress &&
        !ingressStarted) {
      const double nextDistance = distanceNm(segment.end, request.destination);
      if (nextDistance <= destinationIngressRadiusNm + 1e-6 &&
          nextDistance + 1e-6 < priorDestinationDistance &&
          !environment.landAndBoundaries->segmentFromKnownSafeForbiddenAt(
              segment.start, segment.end, segment.startTime, 0.0)) {
        ingressStarted = true;
        forbidden = false;
      }
    }
    if (forbidden) {
      ++diagnostics.landRejections;
      return true;
    }
    if (departureEgress) {
      const double nextClearanceNm =
          environment.landAndBoundaries->distanceToForbiddenNm(segment.end);
      if (nextClearanceNm + 1e-6 < departureClearanceNm) {
        ++diagnostics.landRejections;
        return true;
      }
      departureClearanceNm = nextClearanceNm;
      departureEgress = nextClearanceNm + 1e-6 <
                        request.constraints.landSafetyMarginNm;
    } else if (ingressStarted) {
      const double nextDistance = distanceNm(segment.end, request.destination);
      if (nextDistance > priorDestinationDistance + 1e-6) {
        ++diagnostics.landRejections;
        return true;
      }
      priorDestinationDistance = nextDistance;
    } else {
      priorDestinationDistance = distanceNm(segment.end, request.destination);
    }
    if (request.constraints.minimumDepthMetres) {
      const auto depth =
          environment.landAndBoundaries->depthMetres(segment.end);
      if (!depth || *depth < *request.constraints.minimumDepthMetres) {
        ++diagnostics.constraintRejections;
        return true;
      }
    }
  }
  // The public route and exported GPX represent this state by its endpoint
  // chord, while chronological replay follows the integrated slices above.
  // Both geometries must therefore be safe before the state is admitted.
  ++diagnostics.landChecks;
  bool chordForbidden =
      environment.landAndBoundaries->segmentFromKnownSafeForbiddenAt(
          node.incomingLeg.start, node.incomingLeg.end,
          node.incomingLeg.startTime,
          chordStartsInDepartureEgress ? 0.0 : margin);
  if (chordForbidden && destinationIngress &&
      !chordStartsInDepartureEgress) {
    const double chordDistance =
        distanceNm(node.incomingLeg.start, node.incomingLeg.end);
    const double bearing =
        initialBearingDegrees(node.incomingLeg.start, node.incomingLeg.end);
    const GeoPoint ingressStart =
        chordDistance <= destinationIngressRadiusNm
            ? node.incomingLeg.start
            : destinationPoint(node.incomingLeg.start, bearing,
                               chordDistance - destinationIngressRadiusNm);
    chordForbidden =
        (chordDistance > destinationIngressRadiusNm &&
         environment.landAndBoundaries->segmentFromKnownSafeForbiddenAt(
             node.incomingLeg.start, ingressStart, node.incomingLeg.startTime,
             margin)) ||
        environment.landAndBoundaries->segmentFromKnownSafeForbiddenAt(
            ingressStart, node.incomingLeg.end, node.incomingLeg.startTime,
            0.0);
  }
  if (chordForbidden) {
    ++diagnostics.landRejections;
    return true;
  }
  if (request.constraints.minimumDepthMetres) {
    const auto depth =
        environment.landAndBoundaries->depthMetres(node.incomingLeg.end);
    if (!depth || *depth < *request.constraints.minimumDepthMetres) {
      ++diagnostics.constraintRejections;
      return true;
    }
  }
  node.departureEgressActive = departureEgress;
  return false;
}

std::optional<Node> directConnection(
    const RoutingRequest& request, const RoutingEnvironment& environment,
    const VesselPerformanceModel& performanceModel, const Node& from,
    Duration maximumDuration, RoutingDiagnostics& diagnostics) {
  const double distance = distanceNm(from.position, request.destination);
  if (distance <= 1e-9) return {};
  const double groundCourse =
      initialBearingDegrees(from.position, request.destination);
  const auto resolved = internal::resolveEnvironment(request, environment,
                                                     from.position, from.time);
  ++diagnostics.weatherSamples;
  if (resolved.failureStatus != RoutingStatus::Complete ||
      !hardEnvironmentConstraints(request, resolved.snapshot))
    return {};
  const Vector2 targetUnit = speedDirectionToVector(1.0, groundCourse);
  const double currentAlong =
      resolved.snapshot.current.velocity.eastKnots * targetUnit.eastKnots +
      resolved.snapshot.current.velocity.northKnots * targetUnit.northKnots;
  std::optional<Node> best;
  for (int seed = -2; seed <= 2; ++seed) {
    const double assumedHeading = normalizeHeading(groundCourse + seed * 5.0);
    const double twa =
        trueWindAngleDegrees(resolved.snapshot.wind.velocity, assumedHeading);
    const auto options = performanceModel.candidatesAt(
        from.position, from.time,
        resolved.snapshot.wind.available
            ? vectorMagnitudeKnots(resolved.snapshot.wind.velocity)
            : 0.0,
        twa, resolved.snapshot.waves, from.mode, from.modeDuration);
    for (const auto& candidate : options) {
      if (!candidate.valid) continue;
      double trialHeading = assumedHeading;
      const double initialGroundSpeed =
          std::max(0.1, candidate.speedThroughWaterKnots + currentAlong);
      Duration trialMoving{static_cast<std::int64_t>(
          std::ceil(distance / initialGroundSpeed * 3600.0))};
      std::optional<MotionReplay> accepted;
      Duration acceptedPenalty{};
      Tack acceptedTack{Tack::Unknown};
      for (int iteration = 0; iteration < 10; ++iteration) {
        const double trialTwa =
            trueWindAngleDegrees(resolved.snapshot.wind.velocity, trialHeading);
        const Tack nextTack =
            tackFor(trialHeading, resolved.snapshot.wind.velocity);
        const Duration penalty =
            transitionPenalty(request, from, candidate.mode, nextTack,
                              candidate.sailPlan, trialTwa);
        if (trialMoving <= Duration::zero() ||
            trialMoving + penalty > maximumDuration)
          break;
        RoutingStatus ignoredFailure = RoutingStatus::Complete;
        auto replay = integrateMotion(
            request, environment, performanceModel, from, trialHeading,
            candidate, from.time + penalty, trialMoving,
            // Destination connections are sparse and are the first complete
            // routes offered to the independent one-minute validator. Match
            // that cadence here so a five-minute sample cannot conceal a
            // short hard wind-angle or weather-limit violation and force an
            // otherwise avoidable recovery search.
            Duration{std::chrono::minutes{1}}, diagnostics, &ignoredFailure);
        if (!replay) break;
        const double miss = distanceNm(replay->end, request.destination);
        if (miss <= 0.01) {
          accepted = std::move(replay);
          acceptedPenalty = penalty;
          acceptedTack = nextTack;
          break;
        }
        const double achievedDistance = distanceNm(from.position, replay->end);
        if (!std::isfinite(achievedDistance) || achievedDistance <= 0.01) break;
        const double achievedBearing =
            initialBearingDegrees(from.position, replay->end);
        trialHeading =
            normalizeHeading(trialHeading - angularDifferenceDegrees(
                                                achievedBearing, groundCourse));
        const double scaledSeconds = static_cast<double>(trialMoving.count()) *
                                     distance / achievedDistance;
        trialMoving = Duration{static_cast<std::int64_t>(
            std::max(1.0, std::round(scaledSeconds)))};
      }
      if (!accepted) continue;
      const Duration total = trialMoving + acceptedPenalty;
      const double fuel = accepted->fuelLitres;
      const Duration addedMotor =
          isMotor(candidate.mode) ? trialMoving : Duration{};
      if (!withinPropulsionLimits(request, from, addedMotor, fuel)) continue;
      const double groundSpeed =
          distance /
          (std::chrono::duration<double>(trialMoving).count() / 3600.0);
      const Vector2 groundVelocity =
          speedDirectionToVector(groundSpeed, groundCourse);
      Node node = from;
      node.position = request.destination;
      node.time = from.time + total;
      node.incomingHeading = trialHeading;
      node.tack = acceptedTack;
      node.mode = candidate.mode;
      node.role = candidate.role;
      node.profileIdentity = candidate.profileIdentity;
      node.sailPlan = candidate.sailPlan;
      node.modeDuration = candidate.mode == from.mode
                              ? from.modeDuration + trialMoving
                              : trialMoving;
      node.motorDuration += addedMotor;
      node.fuel += fuel;
      node.risk += accepted->risk;
      node.manoeuvres +=
          (candidate.mode != from.mode) +
          (from.tack != Tack::Unknown && from.tack != acceptedTack);
      node.incomingLeg = buildLeg(
          request, from, resolved, candidate, trialHeading, groundVelocity,
          request.destination, total, acceptedTack, trialMoving,
          Duration{std::chrono::minutes{1}});
      node.incomingLeg.speedThroughWaterKnots =
          accepted->averageSpeedThroughWaterKnots;
      node.incomingLeg.estimatedFuelLitres = fuel;
      node.incomingMotionSegments = accepted->segments;
      if (nodeMotionForbidden(request, environment, node, diagnostics))
        continue;
      if (!best || stateCost(request, node) < stateCost(request, *best))
        best = std::move(node);
    }
  }
  return best;
}

Duration directConnectionWindow(const RoutingRequest& request, const Node& from,
                                Duration stageMinimum) {
  const auto routeDeadline =
      request.departure + request.limits.maximumRouteDuration;
  if (from.time >= routeDeadline) return Duration::zero();

  // A direct approach is attempted only inside the caller's bounded arrival
  // envelope. Do not constrain it to two or three *refined* search steps: a
  // slow point of sail, an adverse current, or a manoeuvre penalty can make a
  // valid final 2--3 NM leg take longer. Two hours is nevertheless a strict
  // cap so an infeasible straight approach does not churn through many GRIB
  // frames when the correct answer is another tack in the graph.
  return std::min(
      std::chrono::duration_cast<Duration>(routeDeadline - from.time),
      std::max(stageMinimum, Duration{std::chrono::hours{2}}));
}

std::vector<Node> propagate(const RoutingRequest& request,
                            const RoutingEnvironment& environment,
                            const VesselPerformanceModel& performanceModel,
                            const Node& from, double heading, Duration step,
                            RoutingDiagnostics& diagnostics,
                            Duration maximumIntegrationSlice,
                            RoutingStatus* dataFailure) {
  const auto resolved = internal::resolveEnvironment(request, environment,
                                                     from.position, from.time);
  ++diagnostics.weatherSamples;
  if (resolved.failureStatus != RoutingStatus::Complete) {
    if (dataFailure) *dataFailure = resolved.failureStatus;
    return {};
  }
  if (!hardEnvironmentConstraints(request, resolved.snapshot)) {
    ++diagnostics.constraintRejections;
    return {};
  }
  const double tws = vectorMagnitudeKnots(resolved.snapshot.wind.velocity);
  const double twa =
      trueWindAngleDegrees(resolved.snapshot.wind.velocity, heading);
  const auto performances = performanceModel.candidatesAt(
      from.position, from.time, tws, twa, resolved.snapshot.waves, from.mode,
      from.modeDuration);
  std::vector<Node> result;
  for (const auto& performance : performances) {
    if (!hardMotionConstraints(request, resolved.snapshot, heading,
                               performance.speedThroughWaterKnots)) {
      ++diagnostics.constraintRejections;
      continue;
    }
    const Tack nextTack = tackFor(heading, resolved.snapshot.wind.velocity);
    const Duration penalty = transitionPenalty(
        request, from, performance.mode, nextTack, performance.sailPlan, twa);
    if (penalty >= step) continue;
    const Duration moving = step - penalty;
    auto replay =
        integrateMotion(request, environment, performanceModel, from, heading,
                        performance, from.time + penalty, moving,
                        maximumIntegrationSlice, diagnostics, dataFailure);
    if (!replay) continue;
    const GeoPoint end = replay->end;
    if (std::abs(end.latitude) > request.constraints.maximumLatitudeDegrees)
      continue;
    if (distanceNm(request.start, end) >
        request.limits.maximumExplorationDistanceNm)
      continue;
    const double fuel = replay->fuelLitres;
    const Duration addedMotor = isMotor(performance.mode) ? moving : Duration{};
    if (!withinPropulsionLimits(request, from, addedMotor, fuel)) {
      ++diagnostics.propulsionRejections;
      continue;
    }
    Node node = from;
    node.position = end;
    node.time = from.time + step;
    node.incomingHeading = heading;
    node.tack = nextTack;
    node.mode = performance.mode;
    node.role = performance.role;
    node.profileIdentity = performance.profileIdentity;
    node.sailPlan = performance.sailPlan;
    node.modeDuration =
        performance.mode == from.mode ? from.modeDuration + moving : moving;
    node.motorDuration += addedMotor;
    node.fuel += fuel;
    node.risk += replay->risk;
    node.manoeuvres += (performance.mode != from.mode) +
                       (from.tack != Tack::Unknown && from.tack != nextTack);
    const double groundSpeed =
        distanceNm(from.position, end) /
        (std::chrono::duration<double>(moving).count() / 3600.0);
    const Vector2 ground = speedDirectionToVector(
        groundSpeed, initialBearingDegrees(from.position, end));
    node.incomingLeg = buildLeg(request, from, resolved, performance, heading,
                                ground, end, step, nextTack, moving,
                                maximumIntegrationSlice);
    node.incomingLeg.speedThroughWaterKnots =
        replay->averageSpeedThroughWaterKnots;
    node.incomingLeg.estimatedFuelLitres = fuel;
    node.incomingMotionSegments = replay->segments;
    result.push_back(std::move(node));
  }
  return result;
}

using CellKey = std::tuple<int, int, int, int, int>;

struct CellKeyHash {
  std::size_t operator()(const CellKey& key) const noexcept {
    std::size_t value = 0;
    std::apply(
        [&](const auto... fields) {
          ((value ^= std::hash<int>{}(fields) + 0x9e3779b9U + (value << 6U) +
                     (value >> 2U)),
           ...);
        },
        key);
    return value;
  }
};

CellKey stateCell(const RoutingRequest&, const Node& node, double cellNm,
                  double headingTolerance) {
  const double latScale = 60.0 / std::max(0.1, cellNm);
  const double lonScale = 60.0 *
                          std::max(0.1, std::cos(node.position.latitude *
                                                 std::numbers::pi / 180.0)) /
                          std::max(0.1, cellNm);
  return {static_cast<int>(std::floor(node.position.latitude * latScale)),
          static_cast<int>(std::floor(
              normalizeLongitude(node.position.longitude) * lonScale)),
          static_cast<int>(node.tack), static_cast<int>(node.mode),
          static_cast<int>(std::floor(node.incomingHeading /
                                      std::max(1.0, headingTolerance)))};
}

std::vector<Node> pruneLayer(const RoutingRequest& request,
                             const RoutingEnvironment& environment,
                             std::vector<Node> candidates, unsigned labelCap,
                             RoutingDiagnostics& diagnostics) {
  std::unordered_map<CellKey, std::vector<Node>, CellKeyHash> cells;
  for (auto& candidate : candidates)
    cells[stateCell(request, candidate, request.options.spatialCellNm,
                    request.options.dominanceHeadingToleranceDegrees)]
        .push_back(std::move(candidate));
  std::vector<Node> retained;
  for (auto& [key, labels] : cells) {
    (void)key;
    std::stable_sort(
        labels.begin(), labels.end(), [&](const Node& a, const Node& b) {
          const auto score = [&](const Node& node) {
            return std::tuple{stateCost(request, node),
                              node.fuel,
                              node.risk,
                              node.motorDuration.count(),
                              node.manoeuvres,
                              distanceNm(node.position, request.destination),
                              node.position.latitude,
                              node.position.longitude};
          };
          return score(a) < score(b);
        });
    std::vector<Node> pareto;
    std::vector<Node> lineageAlternatives;
    std::set<std::size_t> deferredPredecessors;
    for (auto& label : labels) {
      const bool dominated =
          std::any_of(pareto.begin(), pareto.end(), [&](const Node& other) {
            return other.fuel <=
                       label.fuel + request.options.epsilonDominance &&
                   other.risk <=
                       label.risk + request.options.epsilonDominance &&
                   other.motorDuration <= label.motorDuration &&
                   stateCost(request, other) <=
                       stateCost(request, label) +
                           request.options.epsilonDominance;
          });
      if (dominated) {
        ++diagnostics.pruned.dominated;
        // A locally cheaper state can become a dead end after a later wind,
        // tide, tack or coastal constraint. Preserve one bounded alternative
        // from a different predecessor lineage so temporary optimality cannot
        // collapse the entire cell to a single ancestry.
        if (lineageAlternatives.size() < labelCap &&
            deferredPredecessors.insert(label.predecessor).second)
          lineageAlternatives.push_back(std::move(label));
        continue;
      }
      // Semantic chart queries are materially more expensive than weather and
      // polar integration.  Apply them only after a candidate has survived
      // deterministic spatial/Pareto pruning, but before it can enter a
      // frontier.  Existing safe labels are the only labels allowed to
      // dominate here, so an unsafe candidate can never hide a safe one.
      if (nodeMotionForbidden(request, environment, label, diagnostics))
        continue;
      pareto.push_back(std::move(label));
    }
    if (pareto.size() < labelCap && !lineageAlternatives.empty()) {
      std::set<std::size_t> representedPredecessors;
      for (const auto& label : pareto)
        representedPredecessors.insert(label.predecessor);
      for (auto& alternative : lineageAlternatives) {
        if (pareto.size() >= labelCap) break;
        if (representedPredecessors.contains(alternative.predecessor))
          continue;
        if (nodeMotionForbidden(request, environment, alternative, diagnostics))
          continue;
        representedPredecessors.insert(alternative.predecessor);
        pareto.push_back(std::move(alternative));
      }
    }
    if (pareto.size() > labelCap) {
      diagnostics.pruned.cellLabelCap += pareto.size() - labelCap;
      pareto.resize(labelCap);
    }
    for (auto& label : pareto) retained.push_back(std::move(label));
  }
  std::stable_sort(
      retained.begin(), retained.end(), [&](const Node& a, const Node& b) {
        return std::tuple{distanceNm(a.position, request.destination),
                          stateCost(request, a), a.position.latitude,
                          a.position.longitude, a.incomingHeading} <
               std::tuple{distanceNm(b.position, request.destination),
                          stateCost(request, b), b.position.latitude,
                          b.position.longitude, b.incomingHeading};
      });
  // An isochrone is an outer reachability frontier, not the complete interior
  // of every heading-expanded cell. Retain a deterministic, sector-balanced
  // subset while keeping tack and propulsion labels in separate buckets.
  const std::size_t nominalFrontierCap =
      std::max<std::size_t>(128, static_cast<std::size_t>(labelCap) * 64U);
  // A short coastal passage does not need ocean-scale interior density. Keep
  // at least two deterministic representatives across the roughly 120
  // three-degree sectors/tack/mode buckets, while avoiding 600+ parents each
  // spawning a full heading fan on every hourly layer.
  const std::size_t frontierCap =
      distanceNm(request.start, request.destination) <= 200.0
          ? std::min<std::size_t>(nominalFrontierCap, 256U)
          : nominalFrontierCap;
  if (retained.size() > frontierCap) {
    using Sector = std::tuple<int, int, int>;
    std::map<Sector, std::vector<Node>> sectors;
    const double sectorDegrees =
        std::max(3.0, request.options.headingStepDegrees / 2.0);
    for (auto& node : retained) {
      const int sector = static_cast<int>(std::floor(
          initialBearingDegrees(request.start, node.position) / sectorDegrees));
      sectors[{sector, static_cast<int>(node.tack),
               static_cast<int>(node.mode)}]
          .push_back(std::move(node));
    }
    std::vector<Node> balanced;
    balanced.reserve(frontierCap);
    for (std::size_t rank = 0; balanced.size() < frontierCap; ++rank) {
      bool added = false;
      for (auto& [key, labels] : sectors) {
        (void)key;
        if (rank < labels.size()) {
          balanced.push_back(std::move(labels[rank]));
          added = true;
          if (balanced.size() == frontierCap) break;
        }
      }
      if (!added) break;
    }
    diagnostics.pruned.cellLabelCap += retained.size() - balanced.size();
    retained = std::move(balanced);
    std::stable_sort(
        retained.begin(), retained.end(), [&](const Node& a, const Node& b) {
          return std::tuple{distanceNm(a.position, request.destination),
                            stateCost(request, a), a.position.latitude,
                            a.position.longitude, a.incomingHeading} <
                 std::tuple{distanceNm(b.position, request.destination),
                            stateCost(request, b), b.position.latitude,
                            b.position.longitude, b.incomingHeading};
        });
  }
  return retained;
}

std::vector<GeoPoint> inspectionLineage(const std::vector<Node>& nodes,
                                        std::size_t index) {
  std::vector<GeoPoint> reversed;
  reversed.reserve(64);
  while (index != std::numeric_limits<std::size_t>::max()) {
    reversed.push_back(nodes[index].position);
    index = nodes[index].predecessor;
  }
  std::reverse(reversed.begin(), reversed.end());
  return reversed;
}

IsochroneLayer buildIsochroneLayer(const RoutingRequest& request,
                                   const RoutingEnvironment& environment,
                                   const std::vector<Node>& nodes,
                                   const std::vector<std::size_t>& retained,
                                   double headingStep) {
  IsochroneLayer visual;
  if (retained.empty()) return visual;
  visual.time = nodes[retained.front()].time;

  struct Representative {
    double bearing{};
    double range{};
    std::size_t node{};
  };
  const double sectorDegrees = std::clamp(headingStep / 2.0, 2.0, 10.0);
  std::map<int, Representative> outer;
  for (const std::size_t index : retained) {
    const GeoPoint point = nodes[index].position;
    const double bearing = initialBearingDegrees(request.start, point);
    const double range = distanceNm(request.start, point);
    const int sector = static_cast<int>(std::floor(bearing / sectorDegrees));
    auto [entry, inserted] =
        outer.try_emplace(sector, Representative{bearing, range, index});
    if (!inserted &&
        std::tie(range, bearing, point.latitude, point.longitude, index) >
            std::tie(entry->second.range, entry->second.bearing,
                     nodes[entry->second.node].position.latitude,
                     nodes[entry->second.node].position.longitude,
                     entry->second.node))
      entry->second = {bearing, range, index};
  }
  std::vector<Representative> representatives;
  representatives.reserve(outer.size());
  for (const auto& [sector, representative] : outer) {
    (void)sector;
    representatives.push_back(representative);
  }
  std::stable_sort(
      representatives.begin(), representatives.end(),
      [&](const Representative& a, const Representative& b) {
        const GeoPoint pa = nodes[a.node].position;
        const GeoPoint pb = nodes[b.node].position;
        return std::tie(a.bearing, a.range, pa.latitude, pa.longitude, a.node) <
               std::tie(b.bearing, b.range, pb.latitude, pb.longitude, b.node);
      });
  // Exact lineages are useful for route-to-cursor inspection, but retaining
  // one full predecessor chain for every fine contour sector grows
  // quadratically with route duration. Forty-eight evenly spaced traces per
  // layer preserve an inspection resolution finer than the UI cursor while
  // bounding memory on long ocean passages and small navigation computers.
  constexpr std::size_t kMaximumInspectionTracesPerLayer = 48;
  const std::size_t traceStride = std::max<std::size_t>(
      1, (representatives.size() + kMaximumInspectionTracesPerLayer - 1) /
             kMaximumInspectionTracesPerLayer);
  for (std::size_t index = 0; index < representatives.size();
       index += traceStride) {
    const auto& representative = representatives[index];
    const GeoPoint endpoint = nodes[representative.node].position;
    visual.traces.push_back(
        {endpoint, inspectionLineage(nodes, representative.node)});
  }
  if (representatives.size() < 2) return visual;

  const auto edgeIsDefensible = [&](std::size_t from, std::size_t to) {
    const Representative& a = representatives[from];
    const Representative& b = representatives[to];
    double gap = b.bearing - a.bearing;
    if (gap < 0.0) gap += 360.0;
    if (gap > std::max(25.0, sectorDegrees * 3.25)) return false;
    const GeoPoint start = nodes[a.node].position;
    const GeoPoint end = nodes[b.node].position;
    const double expectedArc =
        std::max(a.range, b.range) * gap * std::numbers::pi / 180.0;
    const double maximumChord =
        std::max(request.options.spatialCellNm * 4.0,
                 expectedArc * 3.0 + request.options.spatialCellNm * 2.0);
    if (distanceNm(start, end) > maximumChord) return false;
    return !environment.landAndBoundaries ||
           !environment.landAndBoundaries->visualizationSegmentForbidden(
               start, end, request.constraints.landSafetyMarginNm);
  };

  std::vector<bool> connected(representatives.size());
  bool allConnected = true;
  std::size_t firstBreak = 0;
  for (std::size_t i = 0; i < representatives.size(); ++i) {
    connected[i] = edgeIsDefensible(i, (i + 1) % representatives.size());
    if (!connected[i]) {
      allConnected = false;
      firstBreak = i;
    }
  }
  if (allConnected) {
    IsochroneContour contour;
    contour.closed = true;
    for (const auto& representative : representatives)
      contour.points.push_back(nodes[representative.node].position);
    visual.contours.push_back(std::move(contour));
  } else {
    IsochroneContour contour;
    std::size_t current = (firstBreak + 1) % representatives.size();
    for (std::size_t count = 0; count < representatives.size(); ++count) {
      contour.points.push_back(nodes[representatives[current].node].position);
      if (count + 1 < representatives.size() && !connected[current]) {
        if (contour.points.size() >= 2)
          visual.contours.push_back(std::move(contour));
        contour = {};
      }
      current = (current + 1) % representatives.size();
    }
    if (contour.points.size() >= 2)
      visual.contours.push_back(std::move(contour));
  }
  if (!visual.contours.empty()) {
    const auto largest = std::max_element(
        visual.contours.begin(), visual.contours.end(),
        [](const IsochroneContour& a, const IsochroneContour& b) {
          return a.points.size() < b.points.size();
        });
    visual.frontier = largest->points;
  }
  return visual;
}

SearchArtifacts forwardSearch(const RoutingRequest& request,
                              const RoutingEnvironment& environment,
                              const VesselPerformanceModel& performance,
                              double headingStep, Duration step,
                              unsigned labelCap, bool allowCompletion,
                              unsigned attempt, unsigned totalAttempts,
                              std::uint64_t endpointGeneratedStateCeiling,
                              std::uint64_t forwardGeneratedStateCeiling,
                              RoutingDiagnostics& diagnostics) {
  SearchArtifacts result;
  Node initial;
  initial.position = request.start;
  initial.time = request.departure;
  if (environment.landAndBoundaries &&
      request.constraints.landSafetyMarginNm > 0.0) {
    initial.departureEgressActive =
        environment.landAndBoundaries->distanceToForbiddenNm(request.start) +
            1e-6 <
        request.constraints.landSafetyMarginNm;
  }
  result.nodes.push_back(std::move(initial));
  result.retained.push_back(0);
  const auto deadline = request.departure + request.limits.maximumRouteDuration;
  unsigned layer = 0;
  unsigned stalledApproachLayers = 0;
  double lastApproachProgressNm = std::numeric_limits<double>::infinity();
  RoutingStatus dataFailure = RoutingStatus::Complete;
  const double focusedCorridorCoreWidthNm =
      request.options.graphCorridorWidthNm;
  // Preserve a sparse fringe for routes which initially need to turn away
  // from the destination around a coastal obstruction.  Keeping the fringe
  // sparse is important: every survivor otherwise incurs an authoritative
  // chart/depth query before ordinary layer pruning can reject it.
  const double focusedCorridorWidthNm =
      focusedCorridorCoreWidthNm * 1.5;
  constexpr std::size_t kDetourFringeCandidatesPerSide = 128;
  const bool focusForwardBeforeGraphRecovery =
      request.options.useGraphFallback &&
      std::isfinite(focusedCorridorWidthNm) && focusedCorridorWidthNm > 0.0;
  if (focusForwardBeforeGraphRecovery) {
    diagnostics.stageStopReasons.push_back(
        "forward isochrone focused corridor: full core " +
        graphCorridorDescription(focusedCorridorCoreWidthNm) +
        ", sparse detour fringe to " +
        graphCorridorDescription(focusedCorridorWidthNm) +
        "; wider states deferred to progressive graph recovery");
  }
  while (!result.retained.empty() &&
         result.nodes[result.retained.front()].time < deadline) {
    if (request.cancellation.cancelled()) {
      result.failure = RoutingStatus::Cancelled;
      result.reason = "routing cancelled";
      return result;
    }
    std::vector<Node> candidates;
    std::vector<Node> directCandidates;
    constexpr std::size_t kMaximumDirectCandidates = 8;
    // A chart-authorised departure can legitimately start inside the configured
    // shoreline stand-off (for example at a harbour entrance). Use the finest
    // configured angular resolution and a locally shortened first step so a
    // narrow, curved safe egress is not lost merely because the normal offshore
    // fan straddles or over-shoots it. Subsequent layers return to the
    // attempt's normal resolution; refining an entire long passage grows the
    // frontier needlessly.
    bool coastalDepartureEgress = false;
    if (environment.landAndBoundaries &&
        request.constraints.landSafetyMarginNm > 0.0) {
      coastalDepartureEgress = std::any_of(
          result.retained.begin(), result.retained.end(),
          [&](std::size_t index) {
            return result.nodes[index].departureEgressActive;
          });
    }
    const double layerHeadingStep =
        coastalDepartureEgress ||
                (layer == 0 && environment.landAndBoundaries)
            ? std::min(headingStep, request.options.refinedHeadingStepDegrees)
            : headingStep;
    Duration layerStep = step;
    if (coastalDepartureEgress) {
      // This safety-critical endpoint cadence is not an optional offshore
      // search refinement. Use the authoritative minimum even when ordinary
      // adaptive time stepping is disabled.
      layerStep = request.options.minimumTimeStep;
    } else if (request.options.adaptiveTimeStep) {
      if (layer < 3 && environment.landAndBoundaries) {
        // Preserve a fine, curved departure fan for long configured offshore
        // steps. One shortened layer can still be followed immediately by an
        // hour-long chord across a nearby headland; three half-hour-or-finer
        // layers clear the local coastal topology before returning to the
        // user's efficient offshore cadence.
        layerStep =
            std::max(request.options.minimumTimeStep,
                     std::min(step / 2, Duration{std::chrono::minutes{30}}));
      } else {
        const auto nearest = std::min_element(
            result.retained.begin(), result.retained.end(),
            [&](std::size_t a, std::size_t b) {
              return distanceNm(result.nodes[a].position, request.destination) <
                     distanceNm(result.nodes[b].position, request.destination);
            });
        const double nearestNm =
            distanceNm(result.nodes[*nearest].position, request.destination);
        const double approachRadiusNm = std::max(
            request.options.destinationToleranceNm * 4.0,
            std::chrono::duration<double>(step).count() / 3600.0 * 10.0);
        if (nearestNm <= approachRadiusNm)
          layerStep = request.options.minimumTimeStep;
      }
    }
    for (const std::size_t index : result.retained) {
      const Node& from = result.nodes[index];
      const std::size_t candidatesBefore = candidates.size();
      const double remaining = distanceNm(from.position, request.destination);
      diagnostics.closestApproachNm =
          std::min(diagnostics.closestApproachNm, remaining);
      if (allowCompletion &&
          remaining <=
              std::max(request.options.destinationToleranceNm,
                       static_cast<double>(step.count()) / 3600.0 * 20.0)) {
        if (auto direct = directConnection(
                request, environment, performance, from,
                directConnectionWindow(request, from, layerStep * 2),
                diagnostics)) {
          direct->predecessor = index;
          directCandidates.push_back(std::move(*direct));
          if (directCandidates.size() >= kMaximumDirectCandidates) break;
          // A complete connection from this state is always preferable to
          // propagating another incomplete state from the same predecessor.
          // Continue inspecting the retained layer so independent replay has
          // bounded alternatives if the first connection is rejected.
          continue;
        }
      }
      const double bearing =
          initialBearingDegrees(from.position, request.destination);
      for (double heading :
           headings(layerHeadingStep, bearing, request.options.adaptiveHeadings,
                    request.options.refinedHeadingStepDegrees,
                    request.options.maximumSearchAngleDegrees)) {
        // Exploratory propagation is independently replayed at a dense
        // cadence before any route can be returned. Match the adapter's
        // canonical 15-minute weather buckets here: five-minute integration
        // multiplied provider traffic by roughly three without adding
        // forecast detail, while final replay remains unchanged.
        const Duration integrationSlice{
            coastalDepartureEgress || attempt > 1
                ? std::chrono::minutes{5}
                : std::chrono::minutes{15}};
        for (auto& next : propagate(request, environment, performance, from,
                                    heading, layerStep, diagnostics,
                                    integrationSlice, &dataFailure)) {
          const std::uint64_t ordinaryForwardGenerated =
              diagnostics.generatedStates -
              diagnostics.coastalEndpointGeneratedStates;
          if ((coastalDepartureEgress &&
               diagnostics.coastalEndpointGeneratedStates >=
                   endpointGeneratedStateCeiling) ||
              (!coastalDepartureEgress &&
               ordinaryForwardGenerated >= forwardGeneratedStateCeiling)) {
            result.failure = RoutingStatus::ResourceLimitReached;
            result.reason = coastalDepartureEgress
                                ? "coastal endpoint generated-state budget "
                                  "reached"
                                : "forward-stage generated-state budget "
                                  "reached; preserving recovery capacity";
            result.resourceLimited = true;
            diagnostics.resourceLimitEvents.push_back(result.reason);
            return result;
          }
          ++diagnostics.generatedStates;
          if (coastalDepartureEgress)
            ++diagnostics.coastalEndpointGeneratedStates;
          if ((diagnostics.generatedStates & 8191U) == 0U)
            reportProgress(request, RoutingProgressStage::ForwardIsochrone,
                           diagnostics, attempt, totalAttempts);
          next.predecessor = index;
          candidates.push_back(std::move(next));
        }
      }
      if (candidates.size() == candidatesBefore) {
        if (auto waiting = waitInPlace(request, environment, from, layerStep,
                                       diagnostics)) {
          const std::uint64_t ordinaryForwardGenerated =
              diagnostics.generatedStates -
              diagnostics.coastalEndpointGeneratedStates;
          if ((coastalDepartureEgress &&
               diagnostics.coastalEndpointGeneratedStates >=
                   endpointGeneratedStateCeiling) ||
              (!coastalDepartureEgress &&
               ordinaryForwardGenerated >= forwardGeneratedStateCeiling)) {
            result.failure = RoutingStatus::ResourceLimitReached;
            result.reason = coastalDepartureEgress
                                ? "coastal endpoint generated-state budget "
                                  "reached"
                                : "forward-stage generated-state budget "
                                  "reached; preserving recovery capacity";
            result.resourceLimited = true;
            diagnostics.resourceLimitEvents.push_back(result.reason);
            return result;
          }
          ++diagnostics.generatedStates;
          if (coastalDepartureEgress)
            ++diagnostics.coastalEndpointGeneratedStates;
          waiting->predecessor = index;
          candidates.push_back(std::move(*waiting));
        }
      }
    }
    if (!directCandidates.empty()) {
      std::stable_sort(
          directCandidates.begin(), directCandidates.end(),
          [&](const Node& a, const Node& b) {
            return std::tuple{stateCost(request, a), a.time, a.predecessor} <
                   std::tuple{stateCost(request, b), b.time, b.predecessor};
          });
      result.alternativeSolutions.reserve(directCandidates.size());
      for (auto& direct : directCandidates) {
        result.nodes.push_back(std::move(direct));
        result.alternativeSolutions.push_back(result.nodes.size() - 1);
      }
      result.solution = result.alternativeSolutions.front();
      return result;
    }
    if (focusForwardBeforeGraphRecovery) {
      std::vector<Node> coreCandidates;
      std::vector<Node> portFringeCandidates;
      std::vector<Node> starboardFringeCandidates;
      coreCandidates.reserve(candidates.size());
      for (auto& candidate : candidates) {
        const double crossTrack = crossTrackDistanceNm(
            request.start, request.destination, candidate.position);
        if (std::abs(crossTrack) <= focusedCorridorCoreWidthNm) {
          coreCandidates.push_back(std::move(candidate));
        } else if (std::abs(crossTrack) <= focusedCorridorWidthNm) {
          (crossTrack < 0.0 ? portFringeCandidates
                            : starboardFringeCandidates)
              .push_back(std::move(candidate));
        }
      }
      const auto retainBestFringe = [&](std::vector<Node>& fringe) {
        std::stable_sort(fringe.begin(), fringe.end(),
                         [&](const Node& a, const Node& b) {
                           return std::tuple{
                                      distanceNm(a.position,
                                                 request.destination),
                                      stateCost(request, a), a.predecessor} <
                                  std::tuple{
                                      distanceNm(b.position,
                                                 request.destination),
                                      stateCost(request, b), b.predecessor};
                         });
        if (fringe.size() > kDetourFringeCandidatesPerSide)
          fringe.resize(kDetourFringeCandidatesPerSide);
      };
      retainBestFringe(portFringeCandidates);
      retainBestFringe(starboardFringeCandidates);
      const std::size_t before = candidates.size();
      candidates.clear();
      candidates.reserve(coreCandidates.size() + portFringeCandidates.size() +
                         starboardFringeCandidates.size());
      std::move(coreCandidates.begin(), coreCandidates.end(),
                std::back_inserter(candidates));
      std::move(portFringeCandidates.begin(), portFringeCandidates.end(),
                std::back_inserter(candidates));
      std::move(starboardFringeCandidates.begin(),
                starboardFringeCandidates.end(),
                std::back_inserter(candidates));
      diagnostics.pruned.outsideCorridor += before - candidates.size();
    }
    auto retainedNodes = pruneLayer(request, environment, std::move(candidates),
                                    labelCap, diagnostics);
    if (coastalDepartureEgress && environment.landAndBoundaries) {
      const auto clearedStandOff = [&](const Node& candidate) {
        return !candidate.departureEgressActive;
      };
      // Once at least one safe family has cleared the coastal stand-off, do
      // not let slower lineages which remain inside it hold the entire route
      // at endpoint cadence. Preserve every cleared family and hand those
      // states to the ordinary offshore isochrone together.
      if (std::any_of(retainedNodes.begin(), retainedNodes.end(),
                      clearedStandOff))
        std::erase_if(retainedNodes,
                      [&](const Node& candidate) {
                        return !clearedStandOff(candidate);
                      });
    }
    result.retained.clear();
    for (auto& node : retainedNodes) {
      if (result.nodes.size() >= request.limits.maximumRetainedStates) {
        result.failure = RoutingStatus::ResourceLimitReached;
        result.reason = "maximum retained states reached";
        result.resourceLimited = true;
        diagnostics.resourceLimitEvents.push_back(result.reason);
        return result;
      }
      result.nodes.push_back(std::move(node));
      result.retained.push_back(result.nodes.size() - 1);
      ++diagnostics.retainedStates;
    }
    if (!result.retained.empty())
      result.isochrones.push_back(buildIsochroneLayer(
          request, environment, result.nodes, result.retained, headingStep));
    if (!result.retained.empty()) {
      constexpr std::size_t kRecoveryFrontierHistory = 8;
      result.recoveryFrontiers.push_back(result.retained);
      if (result.recoveryFrontiers.size() > kRecoveryFrontierHistory)
        result.recoveryFrontiers.erase(result.recoveryFrontiers.begin());
    }
    reportProgress(request, RoutingProgressStage::ForwardIsochrone, diagnostics,
                   attempt, totalAttempts);
    if (!result.retained.empty() && request.options.adaptiveTimeStep &&
        (request.options.useReverseRecovery ||
         request.options.useGraphFallback)) {
      double layerClosestNm = std::numeric_limits<double>::infinity();
      for (const std::size_t index : result.retained)
        layerClosestNm = std::min(
            layerClosestNm,
            distanceNm(result.nodes[index].position, request.destination));
      // A difficult coastal route can become topologically stalled well
      // outside the small final-connection radius. Continuing the same
      // forward fan at ever finer resolution then repeats millions of
      // equivalent weather and chart checks before recovery is allowed to
      // start. Once the frontier is in the final 30% of the passage, eight
      // consecutive layers without meaningful progress are enough useful
      // forward context for bounded recovery. This does not reject the
      // frontier: it hands retained chart-safe states to reverse recovery and
      // then graph fallback.
      const double approachRadiusNm =
          std::max({request.options.destinationToleranceNm * 4.0,
                    std::chrono::duration<double>(step).count() / 3600.0 * 10.0,
                    distanceNm(request.start, request.destination) * 0.30});
      if (layerClosestNm <= approachRadiusNm) {
        const double meaningfulProgressNm =
            std::max(0.5, request.options.destinationToleranceNm * 0.5);
        if (layerClosestNm < lastApproachProgressNm - meaningfulProgressNm) {
          lastApproachProgressNm = layerClosestNm;
          stalledApproachLayers = 0;
        } else {
          ++stalledApproachLayers;
        }
        if (stalledApproachLayers >= 8) {
          result.failure = RoutingStatus::SearchIncomplete;
          result.reason =
              "destination convergence stalled; escalating to recovery";
          result.convergenceStalled = true;
          return result;
        }
      }
    }
    ++layer;
    if (!allowCompletion && (diagnostics.closestApproachNm <=
                                 request.options.destinationToleranceNm * 3.0 ||
                             layer >= 48)) {
      result.reason =
          "forward completion deliberately withheld for recovery test";
      return result;
    }
  }
  if (dataFailure != RoutingStatus::Complete) {
    result.failure = dataFailure;
    result.reason = "environmental coverage ended during search";
  } else {
    result.failure = RoutingStatus::NoFeasibleRoute;
    result.reason = result.retained.empty() ? "forward frontier collapsed"
                                            : "maximum route duration reached";
  }
  return result;
}

std::vector<RouteLeg> reconstruct(const SearchArtifacts& search,
                                  std::size_t solution) {
  std::vector<RouteLeg> reversed;
  for (std::size_t index = solution;
       index != std::numeric_limits<std::size_t>::max();) {
    const Node& node = search.nodes[index];
    if (node.predecessor == std::numeric_limits<std::size_t>::max()) break;
    reversed.push_back(node.incomingLeg);
    index = node.predecessor;
  }
  std::reverse(reversed.begin(), reversed.end());
  return reversed;
}

std::vector<std::size_t> reconstructNodeIndices(const SearchArtifacts& search,
                                                std::size_t solution) {
  std::vector<std::size_t> reversed;
  for (std::size_t index = solution;
       index != std::numeric_limits<std::size_t>::max();) {
    const Node& node = search.nodes[index];
    if (node.predecessor == std::numeric_limits<std::size_t>::max()) break;
    reversed.push_back(index);
    index = node.predecessor;
  }
  std::reverse(reversed.begin(), reversed.end());
  return reversed;
}

std::vector<std::size_t> diverseFrontierRecoveryCandidates(
    const RoutingRequest& request, const SearchArtifacts& search,
    std::size_t maximumCandidates);

std::optional<std::pair<SearchArtifacts, std::size_t>> reverseRecovery(
    const RoutingRequest& request, const RoutingEnvironment& environment,
    const VesselPerformanceModel& performance, SearchArtifacts search,
    RoutingDiagnostics& diagnostics) {
  diagnostics.stagesAttempted.push_back(SolverPath::ReverseRecovery);
  ++diagnostics.reverseLayers;
  const std::uint64_t configuredCandidateLimit =
      request.limits.maximumReverseCandidates;
  const std::uint64_t legacyCandidateLimit =
      static_cast<std::uint64_t>(request.options.reverseLayers) * 64U;
  const std::uint64_t boundedCandidateLimit =
      configuredCandidateLimit > 0 ? configuredCandidateLimit
                                   : legacyCandidateLimit;
  const std::size_t reverseCandidateLimit = static_cast<std::size_t>(
      std::min<std::uint64_t>(boundedCandidateLimit,
                              std::numeric_limits<std::size_t>::max()));
  std::vector<std::size_t> nearestCandidates;
  nearestCandidates.reserve(search.nodes.size());
  for (std::size_t i = 1; i < search.nodes.size(); ++i)
    nearestCandidates.push_back(i);
  std::stable_sort(nearestCandidates.begin(), nearestCandidates.end(),
                   [&](std::size_t a, std::size_t b) {
                     return std::tuple{
                                distanceNm(search.nodes[a].position,
                                           request.destination),
                                -search.nodes[a]
                                     .time.time_since_epoch()
                                     .count(),
                                a} <
                            std::tuple{
                                distanceNm(search.nodes[b].position,
                                           request.destination),
                                -search.nodes[b]
                                     .time.time_since_epoch()
                                     .count(),
                                b};
                   });
  // Monotonic extension of the established reverse recovery: never displace
  // the complete legacy nearest-state quota when a higher resource policy
  // asks for lineage diversity. Additional candidates are drawn from distinct
  // recent-frontier sectors and ancestries.
  const std::size_t nearestQuota = static_cast<std::size_t>(
      std::min<std::uint64_t>(legacyCandidateLimit, reverseCandidateLimit));
  if (nearestCandidates.size() > nearestQuota)
    nearestCandidates.resize(nearestQuota);
  std::vector<std::size_t> candidates = std::move(nearestCandidates);
  std::set<std::size_t> admitted(candidates.begin(), candidates.end());
  for (const std::size_t diverse : diverseFrontierRecoveryCandidates(
           request, search, reverseCandidateLimit)) {
    if (candidates.size() >= reverseCandidateLimit) break;
    if (admitted.insert(diverse).second) candidates.push_back(diverse);
  }
  bool bridgeBudgetReported = false;
  const auto bridgeBudgetExhausted = [&]() {
    if (request.limits.maximumReverseBridgeAttempts == 0 ||
        diagnostics.reverseCandidateBridges <
            request.limits.maximumReverseBridgeAttempts)
      return false;
    if (!bridgeBudgetReported) {
      diagnostics.stageStopReasons.push_back(
          "reverse bridge integration budget exhausted after " +
          std::to_string(diagnostics.reverseCandidateBridges) +
          " chronological attempts");
      bridgeBudgetReported = true;
    }
    return true;
  };

  std::vector<GeoPoint> approaches;
  const std::array<double, 3> approachRadii{
      std::max(2.0, request.options.destinationToleranceNm * 2.0), 5.0, 10.0};
  for (const double radiusNm : approachRadii) {
    ++diagnostics.reverseLayers;
    for (int bearing = 0; bearing < 360; bearing += 10) {
      const GeoPoint point = destinationPoint(
          request.destination, static_cast<double>(bearing), radiusNm);
      if (std::abs(point.latitude) > request.constraints.maximumLatitudeDegrees)
        continue;
      if (environment.landAndBoundaries &&
          (environment.landAndBoundaries->pointForbidden(point) ||
           environment.landAndBoundaries->segmentFromKnownSafeForbidden(
               point, request.destination, searchLandMarginNm(request))))
        continue;
      approaches.push_back(point);
    }
  }
  for (std::size_t index : candidates) {
    if (request.cancellation.cancelled()) return {};
    if (bridgeBudgetExhausted()) return {};
    ++diagnostics.reverseNodes;
    if ((diagnostics.reverseNodes & 63U) == 0U)
      reportProgress(request, RoutingProgressStage::ReverseRecovery,
                     diagnostics);
    ++diagnostics.reverseCandidateBridges;
    if (auto bridge = directConnection(
            request, environment, performance, search.nodes[index],
            request.options.reverseHorizon, diagnostics)) {
      bridge->predecessor = index;
      search.nodes.push_back(std::move(*bridge));
      const std::size_t solution = search.nodes.size() - 1;
      return std::pair{std::move(search), solution};
    }
    ++diagnostics.reverseRejectedBridges;
    diagnostics.reverseRejectionReasons.push_back(
        "candidate could not be replayed forward under dynamics or "
        "constraints");

    std::vector<GeoPoint> orderedApproaches = approaches;
    std::stable_sort(
        orderedApproaches.begin(), orderedApproaches.end(),
        [&](GeoPoint a, GeoPoint b) {
          return std::tuple{distanceNm(search.nodes[index].position, a) +
                                distanceNm(a, request.destination),
                            a.latitude, a.longitude} <
                 std::tuple{distanceNm(search.nodes[index].position, b) +
                                distanceNm(b, request.destination),
                            b.latitude, b.longitude};
        });
    for (const GeoPoint approach : orderedApproaches) {
      if (request.cancellation.cancelled()) return {};
      if (bridgeBudgetExhausted()) return {};
      ++diagnostics.reverseCandidateBridges;
      if (environment.landAndBoundaries &&
          environment.landAndBoundaries->segmentFromKnownSafeForbiddenAt(
              search.nodes[index].position, approach, search.nodes[index].time,
              searchLandMarginNm(request))) {
        ++diagnostics.reverseRejectedBridges;
        continue;
      }
      RoutingRequest approachRequest = request;
      approachRequest.destination = approach;
      auto first = directConnection(
          approachRequest, environment, performance, search.nodes[index],
          request.options.reverseHorizon, diagnostics);
      if (!first) {
        ++diagnostics.reverseRejectedBridges;
        continue;
      }
      auto second =
          directConnection(request, environment, performance, *first,
                           request.options.reverseHorizon, diagnostics);
      if (!second) {
        ++diagnostics.reverseRejectedBridges;
        continue;
      }
      first->predecessor = index;
      const std::size_t firstIndex = search.nodes.size();
      second->predecessor = firstIndex;
      search.nodes.push_back(std::move(*first));
      search.nodes.push_back(std::move(*second));
      const std::size_t solution = search.nodes.size() - 1;
      return std::pair{std::move(search), solution};
    }
  }
  diagnostics.stageStopReasons.push_back(
      "reverse reachability found no reproducible bridge");
  return {};
}

std::size_t ancestorStepsBack(const SearchArtifacts& search, std::size_t index,
                              unsigned steps) {
  while (steps-- > 0 && index < search.nodes.size()) {
    const std::size_t predecessor = search.nodes[index].predecessor;
    if (predecessor == std::numeric_limits<std::size_t>::max()) break;
    index = predecessor;
  }
  return index;
}

std::vector<std::size_t> diverseFrontierRecoveryCandidates(
    const RoutingRequest& request, const SearchArtifacts& search,
    std::size_t maximumCandidates) {
  if (maximumCandidates == 0) return {};
  std::vector<std::vector<std::size_t>> frontiers =
      search.recoveryFrontiers;
  if (frontiers.empty() && !search.retained.empty())
    frontiers.push_back(search.retained);
  std::reverse(frontiers.begin(), frontiers.end());

  std::vector<std::vector<std::size_t>> orderedFrontiers;
  orderedFrontiers.reserve(frontiers.size());
  for (auto& frontier : frontiers) {
    using ArrivalSector = std::tuple<int, int, int>;
    std::map<ArrivalSector, std::vector<std::size_t>> sectors;
    for (const std::size_t index : frontier) {
      if (index == 0 || index >= search.nodes.size()) continue;
      const Node& node = search.nodes[index];
      const int sector = static_cast<int>(std::floor(
          initialBearingDegrees(request.destination, node.position) / 10.0));
      sectors[{sector, static_cast<int>(node.tack),
               static_cast<int>(node.mode)}]
          .push_back(index);
    }
    for (auto& [key, candidates] : sectors) {
      (void)key;
      std::stable_sort(
          candidates.begin(), candidates.end(),
          [&](std::size_t a, std::size_t b) {
            const Node& left = search.nodes[a];
            const Node& right = search.nodes[b];
            return std::tuple{
                       distanceNm(left.position, request.destination),
                       stateCost(request, left), left.time,
                       left.position.latitude, left.position.longitude, a} <
                   std::tuple{
                       distanceNm(right.position, request.destination),
                       stateCost(request, right), right.time,
                       right.position.latitude, right.position.longitude, b};
          });
    }
    std::vector<std::size_t> ordered;
    for (std::size_t rank = 0;; ++rank) {
      bool added = false;
      for (auto& [key, candidates] : sectors) {
        (void)key;
        if (rank < candidates.size()) {
          ordered.push_back(candidates[rank]);
          added = true;
        }
      }
      if (!added) break;
    }
    orderedFrontiers.push_back(std::move(ordered));
  }

  std::vector<std::size_t> selected;
  selected.reserve(maximumCandidates);
  std::set<std::size_t> seen;
  std::map<std::size_t, unsigned> ancestryUse;
  const auto admit = [&](std::size_t index,
                         std::vector<std::size_t>& destination) {
    if (!seen.insert(index).second) return;
    const std::size_t ancestry = ancestorStepsBack(search, index, 4);
    unsigned& use = ancestryUse[ancestry];
    if (use >= 2) return;
    ++use;
    destination.push_back(index);
  };

  for (const std::size_t preferred : search.preferredRecoverySeeds) {
    if (preferred > 0 && preferred < search.nodes.size())
      admit(preferred, selected);
    if (selected.size() >= maximumCandidates) return selected;
  }
  for (std::size_t rank = 0; selected.size() < maximumCandidates; ++rank) {
    bool added = false;
    for (const auto& frontier : orderedFrontiers) {
      if (rank >= frontier.size()) continue;
      const std::size_t before = selected.size();
      admit(frontier[rank], selected);
      added = added || selected.size() != before;
      if (selected.size() >= maximumCandidates) break;
    }
    if (!added) break;
  }
  return selected;
}

struct QueueEntry {
  double priority{};
  double cost{};
  double destinationDistanceNm{};
  std::uint64_t serial{};
  std::size_t node{};
};
struct QueueLater {
  bool operator()(const QueueEntry& a, const QueueEntry& b) const {
    // Distance is only a tie-breaker after the complete objective cost.  This
    // keeps Dijkstra/A* optimality intact while avoiding insertion-order
    // expansion of thousands of equal-time labels before a destination-side
    // label from that same cost layer.
    return std::tie(a.priority, a.cost, a.destinationDistanceNm, a.serial) >
           std::tie(b.priority, b.cost, b.destinationDistanceNm, b.serial);
  }
};

enum class GraphSearchKind { FrontierRecovery, GlobalFallback };

using GraphLabelKey = std::tuple<CellKey, std::int64_t, int>;

struct GraphLabelKeyHash {
  std::size_t operator()(const GraphLabelKey& key) const noexcept {
    std::size_t value = CellKeyHash{}(std::get<0>(key));
    value ^= std::hash<std::int64_t>{}(std::get<1>(key)) + 0x9e3779b9U +
             (value << 6U) + (value >> 2U);
    value ^= std::hash<int>{}(std::get<2>(key)) + 0x9e3779b9U + (value << 6U) +
             (value >> 2U);
    return value;
  }
};

SearchArtifacts graphSearch(const RoutingRequest& request,
                            const RoutingEnvironment& environment,
                            const VesselPerformanceModel& performance,
                            const SearchArtifacts* seed,
                            std::uint64_t generatedStateBudget,
                            std::uint64_t graphLabelBudget,
                            GraphSearchKind kind,
                            RoutingDiagnostics& diagnostics) {
  SearchArtifacts result;
  const bool frontierRecovery = kind == GraphSearchKind::FrontierRecovery;
  const std::string stageName =
      frontierRecovery ? "frontier recovery" : "graph fallback";
  const RoutingProgressStage progressStage =
      frontierRecovery ? RoutingProgressStage::FrontierRecovery
                       : RoutingProgressStage::GraphFallback;
  std::priority_queue<QueueEntry, std::vector<QueueEntry>, QueueLater> open;
  std::unordered_map<GraphLabelKey, std::vector<std::size_t>, GraphLabelKeyHash>
      labels;
  std::unordered_map<GraphLabelKey, std::vector<Node>, GraphLabelKeyHash>
      deferredLabels;
  std::size_t deferredLabelCount{};
  std::uint64_t serial = 1;
  std::uint64_t graphLabelsThisSearch{};
  const Duration step = std::max(
      request.options.minimumTimeStep,
      std::min(request.options.timeStep, Duration{std::chrono::minutes{30}}));
  const std::vector<double> corridorWidths =
      graphCorridorSchedule(request.options);
  std::size_t corridorStage{};
  double activeCorridorWidthNm = corridorWidths.front();
  const std::uint64_t graphGeneratedStatesAtStart =
      diagnostics.generatedStates;
  const std::uint64_t graphGeneratedStateLimit =
      generatedStateBudget >
              std::numeric_limits<std::uint64_t>::max() -
                  graphGeneratedStatesAtStart
          ? std::numeric_limits<std::uint64_t>::max()
          : graphGeneratedStatesAtStart + generatedStateBudget;
  const auto stageBudgetNumerator = [&]() {
    if (corridorStage + 1 == corridorWidths.size()) return std::uint64_t{2};
    return static_cast<std::uint64_t>(corridorStage + 1);
  };
  const auto stageBudgetDenominator = [&]() {
    if (corridorStage + 1 == corridorWidths.size()) return std::uint64_t{2};
    return static_cast<std::uint64_t>(
        std::max<std::size_t>(2, 2 * (corridorWidths.size() - 1)));
  };
  const auto stageGeneratedStateCeiling = [&]() {
    return graphGeneratedStatesAtStart +
           generatedStateBudget * stageBudgetNumerator() /
               stageBudgetDenominator();
  };
  const auto stageGraphLabelCeiling = [&]() {
    return graphLabelBudget * stageBudgetNumerator() /
           stageBudgetDenominator();
  };
  diagnostics.graphCorridorWidthsNm.push_back(activeCorridorWidthNm);
  diagnostics.stageStopReasons.push_back(
      stageName + " corridor stage 1/" +
      std::to_string(corridorWidths.size()) + ": " +
      graphCorridorDescription(activeCorridorWidthNm));
  std::optional<double> localDestinationRadiusNm;
  std::optional<double> initialLocalDestinationRadiusNm;
  std::optional<TimePoint> localGraphDeadline;
  if (seed && seed->nodes.size() > 1) {
    result.nodes = seed->nodes;
    result.isochrones = seed->isochrones;
    // Only independently replayed candidate prefixes may seed graph recovery.
    // If no candidate leg survived replay, restart the graph at the known-safe
    // departure.  Certifying thousands of arbitrary retained forward lineages
    // here would rasterise the failed search fan before recovery even begins.
    std::vector<std::size_t> seedIndices;
    if (!seed->preferredRecoverySeeds.empty()) {
      seedIndices = seed->preferredRecoverySeeds;
      seedIndices.erase(std::remove_if(seedIndices.begin(), seedIndices.end(),
                                       [&](std::size_t index) {
                                         return index == 0 ||
                                                index >= result.nodes.size();
                                       }),
                        seedIndices.end());
      std::sort(seedIndices.begin(), seedIndices.end());
      seedIndices.erase(std::unique(seedIndices.begin(), seedIndices.end()),
                        seedIndices.end());
    } else {
      seedIndices.push_back(0);
    }
    std::stable_sort(
        seedIndices.begin(), seedIndices.end(),
        [&](std::size_t a, std::size_t b) {
          return std::tuple{
                     distanceNm(result.nodes[a].position, request.destination),
                     stateCost(request, result.nodes[a]), a} <
                 std::tuple{
                     distanceNm(result.nodes[b].position, request.destination),
                     stateCost(request, result.nodes[b]), b};
        });
    constexpr std::size_t kMaximumAcceptedSeeds = 256;
    if (seedIndices.size() > kMaximumAcceptedSeeds)
      seedIndices.resize(kMaximumAcceptedSeeds);
    const std::vector<std::size_t>& acceptedSeedIndices = seedIndices;
    diagnostics.stageStopReasons.push_back(
        seed->preferredRecoverySeeds.empty()
            ? stageName + " restarted from known-safe departure"
            : stageName + " admitted independently validated candidate "
              "prefixes only");
    const double closestSeedNm =
        distanceNm(result.nodes[acceptedSeedIndices.front()].position,
                   request.destination);
    localDestinationRadiusNm = std::max(20.0, closestSeedNm * 3.0);
    TimePoint latestSeedTime{};
    double closestAcceptedSeedNm = std::numeric_limits<double>::infinity();
    for (const std::size_t index : acceptedSeedIndices) {
      const Node& node = result.nodes[index];
      latestSeedTime = std::max(latestSeedTime, node.time);
      closestAcceptedSeedNm =
          std::min(closestAcceptedSeedNm,
                   distanceNm(node.position, request.destination));
      const double cost = stateCost(request, node);
      double heuristic = 0.0;
      if (request.options.heuristicMaximumSpeedKnots > 0.0)
        heuristic = distanceNm(node.position, request.destination) /
                    request.options.heuristicMaximumSpeedKnots * 3600.0;
      open.push({cost + heuristic, cost,
                 distanceNm(node.position, request.destination), serial++,
                 index});
      const auto elapsedBucket =
          std::chrono::duration_cast<Duration>(node.time - request.departure)
              .count() /
          std::max<std::int64_t>(1, step.count());
      labels[{stateCell(request, node, request.options.spatialCellNm / 2.0,
                        request.options.dominanceHeadingToleranceDegrees),
              elapsedBucket, static_cast<int>(node.role)}]
          .push_back(index);
      ++diagnostics.graphLabels;
      ++graphLabelsThisSearch;
    }
    localGraphDeadline = latestSeedTime + request.options.reverseHorizon;
    initialLocalDestinationRadiusNm = localDestinationRadiusNm;
    diagnostics.stageStopReasons.push_back(
        stageName + " seeded from " +
        std::to_string(acceptedSeedIndices.size()) +
        " safe chronological prefixes; closest " +
        std::to_string(closestAcceptedSeedNm) + " NM");
  } else {
    Node initial;
    initial.position = request.start;
    initial.time = request.departure;
    if (environment.landAndBoundaries &&
        request.constraints.landSafetyMarginNm > 0.0) {
      initial.departureEgressActive =
          environment.landAndBoundaries->distanceToForbiddenNm(request.start) +
              1e-6 <
          request.constraints.landSafetyMarginNm;
    }
    result.nodes.push_back(std::move(initial));
    open.push({0.0, 0.0, distanceNm(request.start, request.destination), 0, 0});
  }
  RoutingStatus dataFailure = RoutingStatus::Complete;
  bool graphLabelLimitReached = false;
  bool deferredCapacityReached = false;
  const auto labelKey = [&](const Node& node) {
    const auto elapsedBucket =
        std::chrono::duration_cast<Duration>(node.time - request.departure)
            .count() /
        std::max<std::int64_t>(1, step.count());
    return GraphLabelKey{
        stateCell(request, node, request.options.spatialCellNm / 2.0,
                  request.options.dominanceHeadingToleranceDegrees),
        elapsedBucket, static_cast<int>(node.role)};
  };
  const auto outsideActiveCorridor = [&](const Node& node) {
    if (localDestinationRadiusNm &&
        distanceNm(node.position, request.destination) >
            *localDestinationRadiusNm)
      return true;
    return std::isfinite(activeCorridorWidthNm) &&
           std::abs(crossTrackDistanceNm(request.start, request.destination,
                                         node.position)) >
               activeCorridorWidthNm;
  };
  const double maximumCorridorWidthNm = corridorWidths.back();
  const auto outsideMaximumCorridor = [&](const Node& node) {
    return std::isfinite(maximumCorridorWidthNm) &&
           std::abs(crossTrackDistanceNm(request.start, request.destination,
                                         node.position)) >
               maximumCorridorWidthNm;
  };
  const auto defer = [&](Node next) {
    if (outsideMaximumCorridor(next)) {
      ++diagnostics.pruned.outsideCorridor;
      return;
    }
    auto& candidates = deferredLabels[labelKey(next)];
    // Do not let an as-yet unvalidated deferred label dominate another one:
    // the cheaper label might later fail a chart-safety check while the other
    // is the only safe route around the obstruction. Bound all admitted and
    // deferred graph labels together instead.
    if (graphLabelsThisSearch + deferredLabelCount >=
        graphLabelBudget) {
      ++diagnostics.pruned.cellLabelCap;
      deferredCapacityReached = true;
      return;
    }
    candidates.push_back(std::move(next));
    ++deferredLabelCount;
  };
  std::function<void(Node)> admit;
  admit = [&](Node next) {
    if (outsideActiveCorridor(next)) {
      defer(std::move(next));
      return;
    }
    auto& cellLabels = labels[labelKey(next)];
    const bool dominated = std::any_of(
        cellLabels.begin(), cellLabels.end(), [&](std::size_t otherIndex) {
          const Node& other = result.nodes[otherIndex];
          return other.fuel <= next.fuel && other.risk <= next.risk &&
                 other.motorDuration <= next.motorDuration &&
                 stateCost(request, other) <= stateCost(request, next);
        });
    if (dominated) {
      ++diagnostics.pruned.dominated;
      return;
    }
    if (cellLabels.size() >= request.options.labelsPerCell) {
      ++diagnostics.pruned.cellLabelCap;
      return;
    }
    // Deferred labels have not yet incurred any expensive chart query.
    // Perform the complete constraint and chart-safety check only when their
    // corridor stage becomes active.
    if (nodeMotionForbidden(request, environment, next, diagnostics)) return;
    if (graphLabelsThisSearch >= graphLabelBudget) {
      graphLabelLimitReached = true;
      return;
    }
    result.nodes.push_back(std::move(next));
    const std::size_t index = result.nodes.size() - 1;
    cellLabels.push_back(index);
    ++diagnostics.graphLabels;
    ++graphLabelsThisSearch;
    const double cost = stateCost(request, result.nodes[index]);
    double heuristic = 0.0;
    if (request.options.heuristicMaximumSpeedKnots > 0.0)
      heuristic =
          distanceNm(result.nodes[index].position, request.destination) /
          request.options.heuristicMaximumSpeedKnots * 3600.0;
    open.push({cost + heuristic, cost,
               distanceNm(result.nodes[index].position, request.destination),
               serial++, index});
  };
  const auto widenCorridor = [&]() {
    if (corridorStage + 1 >= corridorWidths.size() ||
        deferredLabelCount == 0)
      return false;
    const double previousWidth = activeCorridorWidthNm;
    ++corridorStage;
    activeCorridorWidthNm = corridorWidths[corridorStage];
    const bool finalStage = corridorStage + 1 == corridorWidths.size();
    if (finalStage) {
      // The seed-local radius was only an accelerator. It must not become an
      // undocumented geometric restriction on the final graph stage.
      localDestinationRadiusNm.reset();
    } else if (initialLocalDestinationRadiusNm) {
      const double initialWidth = std::max(1.0, corridorWidths.front());
      localDestinationRadiusNm =
          *initialLocalDestinationRadiusNm *
          std::max(1.0, activeCorridorWidthNm / initialWidth);
    }

    std::vector<Node> ready;
    for (auto it = deferredLabels.begin(); it != deferredLabels.end();) {
      std::vector<Node> remaining;
      for (auto& node : it->second) {
        if (outsideActiveCorridor(node)) {
          remaining.push_back(std::move(node));
        } else {
          ready.push_back(std::move(node));
          --deferredLabelCount;
        }
      }
      if (remaining.empty()) {
        it = deferredLabels.erase(it);
      } else {
        it->second = std::move(remaining);
        ++it;
      }
    }
    std::stable_sort(ready.begin(), ready.end(), [&](const Node& a,
                                                     const Node& b) {
      return std::tuple{a.time, stateCost(request, a), a.position.latitude,
                        a.position.longitude, a.predecessor} <
             std::tuple{b.time, stateCost(request, b), b.position.latitude,
                        b.position.longitude, b.predecessor};
    });
    diagnostics.graphCorridorWidthsNm.push_back(activeCorridorWidthNm);
    diagnostics.stageStopReasons.push_back(
        stageName + " widened corridor after exhaustion from " +
        graphCorridorDescription(previousWidth) + " to " +
        graphCorridorDescription(activeCorridorWidthNm) + "; activating " +
        std::to_string(ready.size()) + " deferred labels");
    for (auto& node : ready) admit(std::move(node));
    reportProgress(request, progressStage, diagnostics,
                   static_cast<unsigned>(corridorStage + 1),
                   static_cast<unsigned>(corridorWidths.size()));
    return true;
  };

  while (true) {
    if (corridorStage + 1 < corridorWidths.size() &&
        deferredLabelCount > 0 &&
        (diagnostics.generatedStates >= stageGeneratedStateCeiling() ||
         graphLabelsThisSearch >= stageGraphLabelCeiling())) {
      if (!widenCorridor()) break;
      if (graphLabelLimitReached) {
        result.failure = RoutingStatus::ResourceLimitReached;
        result.reason = "maximum graph labels reached during corridor widening";
        result.resourceLimited = true;
        diagnostics.resourceLimitEvents.push_back(result.reason);
        return result;
      }
      continue;
    }
    if (open.empty()) {
      if (!widenCorridor()) break;
      if (graphLabelLimitReached) {
        result.failure = RoutingStatus::ResourceLimitReached;
        result.reason = "maximum graph labels reached during corridor widening";
        result.resourceLimited = true;
        diagnostics.resourceLimitEvents.push_back(result.reason);
        return result;
      }
      continue;
    }
    if (request.cancellation.cancelled()) {
      result.failure = RoutingStatus::Cancelled;
      result.reason = "routing cancelled";
      return result;
    }
    const QueueEntry entry = open.top();
    open.pop();
    const Node from = result.nodes[entry.node];
    if (localGraphDeadline && from.time >= *localGraphDeadline) continue;
    const double remaining = distanceNm(from.position, request.destination);
    diagnostics.closestApproachNm =
        std::min(diagnostics.closestApproachNm, remaining);
    const double arrivalCaptureNm =
        std::min(0.25, request.options.destinationToleranceNm);
    if (entry.node != 0 && remaining <= arrivalCaptureNm) {
      const auto validation =
          RouteValidator{}.validate(request, environment, performance,
                                    reconstruct(result, entry.node), nullptr);
      diagnostics.validationSamples += validation.samples;
      if (validation.passed) {
        result.solution = entry.node;
        return result;
      }
      diagnostics.stageStopReasons.push_back(
          "destination-zone graph candidate rejected by independent forward "
          "replay: " +
          validation.failureReason);
      // The rejected route prefix cannot be repaired by expanding away from
      // the destination and returning later: every descendant would inherit
      // the same invalid chronological lineage.  Discard it here instead of
      // allowing one bad prefix to consume the graph budget repeatedly.
      continue;
    }
    if (remaining <=
        std::max(request.options.destinationToleranceNm,
                 static_cast<double>(step.count()) / 3600.0 * 20.0)) {
      if (auto direct = directConnection(
              request, environment, performance, from,
              directConnectionWindow(request, from, step * 3), diagnostics)) {
        if (graphLabelsThisSearch >= graphLabelBudget) {
          result.failure = RoutingStatus::ResourceLimitReached;
          result.reason = "maximum graph labels reached";
          result.resourceLimited = true;
          diagnostics.resourceLimitEvents.push_back(result.reason);
          return result;
        }
        ++diagnostics.graphLabels;
        ++graphLabelsThisSearch;
        direct->predecessor = entry.node;
        result.nodes.push_back(std::move(*direct));
        const std::size_t candidate = result.nodes.size() - 1;
        const auto validation =
            RouteValidator{}.validate(request, environment, performance,
                                      reconstruct(result, candidate), nullptr);
        diagnostics.validationSamples += validation.samples;
        if (validation.passed) {
          result.solution = candidate;
          return result;
        }
        diagnostics.stageStopReasons.push_back(
            "graph candidate rejected by independent forward replay: " +
            validation.failureReason);
      }
    }
    const double bearing =
        initialBearingDegrees(from.position, request.destination);
    const Duration expansionStep =
        request.options.adaptiveTimeStep &&
                remaining <= std::max(10.0, request.options.spatialCellNm * 5.0)
            ? request.options.minimumTimeStep
            : step;
    bool movementGenerated = false;
    for (double heading :
         headings(request.options.graphHeadingStepDegrees, bearing, true,
                  request.options.graphHeadingStepDegrees / 2.0,
                  request.options.maximumSearchAngleDegrees)) {
      if (graphLabelsThisSearch >= graphLabelBudget) {
        result.failure = RoutingStatus::ResourceLimitReached;
        result.reason = "maximum graph labels reached";
        result.resourceLimited = true;
        diagnostics.resourceLimitEvents.push_back(result.reason);
        return result;
      }
      auto propagated = propagate(
          request, environment, performance, from, heading, expansionStep,
          diagnostics, Duration{std::chrono::minutes{15}}, &dataFailure);
      for (auto& next : propagated) {
        if (diagnostics.generatedStates >= graphGeneratedStateLimit) {
          result.failure = RoutingStatus::ResourceLimitReached;
          result.reason =
              "maximum generated states reached during " + stageName;
          result.resourceLimited = true;
          diagnostics.resourceLimitEvents.push_back(result.reason);
          return result;
        }
        ++diagnostics.generatedStates;
        if ((diagnostics.generatedStates & 8191U) == 0U)
          reportProgress(request, progressStage, diagnostics);
        movementGenerated = true;
        next.predecessor = entry.node;
        admit(std::move(next));
        if (graphLabelLimitReached) {
          result.failure = RoutingStatus::ResourceLimitReached;
          result.reason = "maximum graph labels reached";
          result.resourceLimited = true;
          diagnostics.resourceLimitEvents.push_back(result.reason);
          return result;
        }
      }
    }
    if (!movementGenerated &&
        diagnostics.generatedStates < graphGeneratedStateLimit &&
        graphLabelsThisSearch < graphLabelBudget) {
      if (auto waiting = waitInPlace(request, environment, from, expansionStep,
                                     diagnostics)) {
        ++diagnostics.generatedStates;
        waiting->predecessor = entry.node;
        admit(std::move(*waiting));
      }
    }
  }
  result.failure = dataFailure != RoutingStatus::Complete
                       ? dataFailure
                       : RoutingStatus::NoFeasibleRoute;
  result.reason =
      dataFailure != RoutingStatus::Complete
          ? "environmental coverage ended before graph search could complete"
      : deferredCapacityReached
          ? "graph deferred-label resource limit reached before a feasible "
            "route was found"
          : "all configured graph corridor stages exhausted without a "
            "feasible label";
  if (deferredCapacityReached) {
    result.failure = RoutingStatus::ResourceLimitReached;
    result.resourceLimited = true;
    diagnostics.resourceLimitEvents.push_back(result.reason);
  }
  return result;
}

void calculateResultSummaries(RoutingResult& result) {
  if (result.legs.empty()) return;
  result.metrics.elapsed = std::chrono::duration_cast<Duration>(
      result.legs.back().endTime - result.legs.front().startTime);
  std::optional<EnvironmentalSnapshot> previous;
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
    if (leg.stationaryWait)
      result.metrics.waitingTime += duration;
    else if (leg.propulsionMode == PropulsionMode::Sail)
      result.metrics.sailingTime += duration;
    else if (leg.propulsionMode == PropulsionMode::MotorSail)
      result.metrics.motorSailingTime += duration;
    else
      result.metrics.motorOnlyTime += duration;
    result.metrics.propulsionTransitions += leg.propulsionTransition;
    result.metrics.tackCount += leg.tackTransition;
    result.metrics.gybeCount += leg.gybeTransition;
    switch (leg.windSource.source) {
      case EnvironmentalSource::GribForecast:
        result.environment.gribWindDuration += duration;
        break;
      case EnvironmentalSource::Climatology:
        result.environment.climatologyWindDuration += duration;
        break;
      default:
        break;
    }
    switch (leg.currentSource.source) {
      case EnvironmentalSource::GribForecast:
        result.environment.gribCurrentDuration += duration;
        break;
      case EnvironmentalSource::XtdCurrentPrediction:
        result.environment.xtdCurrentDuration += duration;
        break;
      case EnvironmentalSource::NoDataAssumedZero:
        result.environment.currentAssumedZeroDuration += duration;
        break;
      default:
        break;
    }
    if (leg.waves.available &&
        leg.waveSource.source == EnvironmentalSource::GribForecast)
      result.environment.gribWaveDuration += duration;
    else
      result.environment.missingWaveDuration += duration;
    EnvironmentalSnapshot current;
    current.wind = {true, {}, leg.windSource};
    current.current = {true, {}, leg.currentSource};
    current.waves = leg.waves;
    internal::appendTransitions(result.sourceTransitions,
                                previous ? &*previous : nullptr, current,
                                leg.start, leg.startTime, {});
    previous = current;
    for (const auto& warning : leg.warnings) {
      if (std::none_of(result.warnings.begin(), result.warnings.end(),
                       [&](const auto& existing) {
                         return existing.code == warning.code;
                       }))
        result.warnings.push_back(warning);
    }
  }
}

RoutingStatus preflightStatus(const RoutingPreflightResult& preflight) {
  for (auto action : preflight.requiredActions) {
    if (action == RequiredUserAction::LoadSailingPolar ||
        action == RequiredUserAction::ConfigureVesselPerformance)
      return RoutingStatus::InvalidPolar;
    if (action == RequiredUserAction::LoadWeatherGrib ||
        action == RequiredUserAction::GenerateWeatherGrib ||
        action == RequiredUserAction::ConfirmClimatologyWindFallback)
      return RoutingStatus::WindForecastRequired;
    if (action == RequiredUserAction::LoadCurrentPredictionDataset ||
        action == RequiredUserAction::ConfirmRoutingWithoutCurrent)
      return RoutingStatus::CurrentDataRequired;
    if (action == RequiredUserAction::ConfirmRoutingWithoutWaveData)
      return RoutingStatus::WaveDataRequired;
    if (action == RequiredUserAction::LoadDepthData)
      return RoutingStatus::WeatherCoverageInsufficient;
  }
  return RoutingStatus::InvalidVesselConfiguration;
}
}  // namespace

RoutingPreflightResult RoutingEngine::preflight(
    const RoutingRequest& request,
    const RoutingEnvironment& environment) const {
  RoutingPreflightResult result;
  PolarPerformanceModel fallbackPerformance(request.vessel);
  const VesselPerformanceModel& performance =
      environment.performance ? *environment.performance : fallbackPerformance;
  std::string polarReason;
  result.vessel.profileCount = request.vessel.profiles.size();
  result.vessel.validSailingProfile = performance.valid(&polarReason);
  result.vessel.motorSailingConfigured = std::any_of(
      request.vessel.profiles.begin(), request.vessel.profiles.end(),
      [](const auto& p) { return p.role == ProfileRole::MotorSailing; });
  result.vessel.motorConfigured =
      request.vessel.propulsion.configuredMotorSpeedKnots > 0.0 ||
      std::any_of(
          request.vessel.profiles.begin(), request.vessel.profiles.end(),
          [](const auto& p) { return p.role == ProfileRole::MotorOnly; });
  if (!result.vessel.validSailingProfile) {
    result.requiredActions.push_back(RequiredUserAction::LoadSailingPolar);
    result.warnings.push_back(
        {RoutingWarningCode::SearchIncomplete, polarReason});
  }
  for (const auto& profile : request.vessel.profiles)
    if (profile.estimated)
      result.warnings.push_back({RoutingWarningCode::EstimatedPolar,
                                 "vessel performance uses an estimated polar"});

  if (environment.grib) {
    result.coverage.wind = environment.grib->windCoverage();
    result.coverage.current = environment.grib->currentCoverage();
    result.coverage.waves = environment.grib->waveCoverage();
  }
  const TimePoint nominalEnd =
      request.departure +
      std::min(
          request.limits.maximumRouteDuration,
          Duration{static_cast<std::int64_t>(std::ceil(
              distanceNm(request.start, request.destination) / 3.0 * 3600.0))});
  const auto coverageContains = [](const ParameterCoverage& coverage,
                                   GeoPoint point) {
    if (!coverage.available || point.latitude < coverage.area.south ||
        point.latitude > coverage.area.north)
      return false;
    const double longitude = normalizeLongitude(point.longitude);
    return coverage.area.west <= coverage.area.east
               ? longitude >= coverage.area.west &&
                     longitude <= coverage.area.east
               : longitude >= coverage.area.west ||
                     longitude <= coverage.area.east;
  };
  const auto coversRoute = [&](const ParameterCoverage& coverage) {
    return coverageContains(coverage, request.start) &&
           coverageContains(coverage, request.destination) &&
           (!coverage.begins || request.departure >= *coverage.begins) &&
           (!coverage.ends || nominalEnd <= *coverage.ends);
  };
  const bool gribWindCovers = coversRoute(result.coverage.wind);
  result.coverage.windFallbackNeeded = !gribWindCovers;
  if (!gribWindCovers) {
    const bool climatologyAvailable =
        environment.climatology &&
        environment.climatology->coverage().available;
    if (!climatologyAvailable || request.environment.climatology ==
                                     ClimatologyFallbackPolicy::Disallow) {
      result.requiredActions.push_back(RequiredUserAction::LoadWeatherGrib);
    } else if (request.environment.climatology ==
                   ClimatologyFallbackPolicy::RequireExplicitAcknowledgement &&
               !request.environment.climatologyAcknowledged) {
      result.requiredActions.push_back(
          RequiredUserAction::ConfirmClimatologyWindFallback);
    } else {
      result.warnings.push_back({RoutingWarningCode::WindCoverageIncomplete,
                                 "GRIB wind coverage is incomplete; authorised "
                                 "climatology will be used"});
    }
  }

  const bool gribCurrentCovers = coversRoute(result.coverage.current);
  const bool currentAvailable =
      gribCurrentCovers || static_cast<bool>(environment.xtdCurrent);
  result.coverage.currentFallbackNeeded = !gribCurrentCovers;
  if (request.environment.useCurrent && !currentAvailable) {
    if (request.environment.missingCurrent == MissingCurrentPolicy::Disallow)
      result.requiredActions.push_back(
          RequiredUserAction::LoadCurrentPredictionDataset);
    else if (request.environment.missingCurrent ==
                 MissingCurrentPolicy::RequireExplicitAcknowledgement &&
             !request.environment.zeroCurrentAcknowledged)
      result.requiredActions.push_back(
          RequiredUserAction::ConfirmRoutingWithoutCurrent);
    else
      result.warnings.push_back(
          {RoutingWarningCode::CurrentAssumedZero,
           "no current source; explicit zero-current policy applies"});
  } else if (request.environment.useCurrent && !gribCurrentCovers &&
             environment.xtdCurrent) {
    result.warnings.push_back(
        {RoutingWarningCode::XtdCurrentUsed,
         "GRIB current unavailable; XTD current prediction will be used"});
    result.coverage.current = environment.xtdCurrent->coverage();
  }

  if (request.constraints.maximumWaveHeightMetres &&
      !coversRoute(result.coverage.waves)) {
    result.coverage.waveWaiverNeeded = true;
    if (request.environment.missingWaves ==
        MissingWavePolicy::DisallowWhenConstrained)
      result.requiredActions.push_back(
          RequiredUserAction::ConfirmRoutingWithoutWaveData);
    else if (request.environment.missingWaves ==
                 MissingWavePolicy::RequireExplicitAcknowledgement &&
             !request.environment.missingWavesAcknowledged)
      result.requiredActions.push_back(
          RequiredUserAction::ConfirmRoutingWithoutWaveData);
    else
      result.warnings.push_back(
          {RoutingWarningCode::WaveDataMissing,
           "wave constraint data is unavailable and explicitly waived"});
  }
  if (request.constraints.minimumDepthMetres &&
      (!environment.landAndBoundaries ||
       !environment.landAndBoundaries->depthMetres(request.start) ||
       !environment.landAndBoundaries->depthMetres(request.destination)))
    result.requiredActions.push_back(RequiredUserAction::LoadDepthData);
  result.canRoute = result.requiredActions.empty();
  return result;
}

RoutingResult RoutingEngine::route(
    const RoutingRequest& request,
    const RoutingEnvironment& environment) const {
  const auto normalizedEffort = [](unsigned effort) {
    if (effort <= 125) return 100U;
    if (effort <= 175) return 150U;
    if (effort <= 300) return 200U;
    return 400U;
  };
  const unsigned selectedEffort =
      normalizedEffort(request.options.routingEffortPercent);
  const std::array<unsigned, 4> tiers{100U, 150U, 200U, 400U};
  const auto scale = [&](std::uint64_t value, unsigned tier) {
    if (selectedEffort == tier || value == 0) return value;
    const long double scaled =
        static_cast<long double>(value) * tier / selectedEffort;
    return static_cast<std::uint64_t>(std::floor(scaled + 0.5L));
  };
  const auto complete = [](RoutingStatus status) {
    return status == RoutingStatus::Complete ||
           status == RoutingStatus::CompleteUsingReverseRecovery ||
           status == RoutingStatus::CompleteUsingFrontierRecovery ||
           status == RoutingStatus::CompleteUsingGraphFallback;
  };
  const auto retryable = [](RoutingStatus status) {
    return status == RoutingStatus::NoFeasibleRoute ||
           status == RoutingStatus::SearchIncomplete ||
           status == RoutingStatus::ResourceLimitReached ||
           status == RoutingStatus::ValidationFailure;
  };

  std::vector<unsigned> attempted;
  std::vector<std::string> priorFailures;
  std::uint64_t cumulativeGenerated{};
  RoutingResult last;
  for (const unsigned tier : tiers) {
    if (tier > selectedEffort) break;
    RoutingRequest tierRequest = request;
    tierRequest.options.routingEffortPercent = tier;
    tierRequest.limits.maximumGeneratedStates =
        scale(request.limits.maximumGeneratedStates, tier);
    tierRequest.limits.maximumCoastalEndpointGeneratedStates =
        scale(request.limits.maximumCoastalEndpointGeneratedStates, tier);
    tierRequest.limits.maximumForwardGeneratedStates =
        scale(request.limits.maximumForwardGeneratedStates, tier);
    tierRequest.limits.maximumReverseCandidates =
        scale(request.limits.maximumReverseCandidates, tier);
    tierRequest.limits.maximumReverseBridgeAttempts =
        scale(request.limits.maximumReverseBridgeAttempts, tier);
    tierRequest.limits.maximumFrontierRecoveryGeneratedStates =
        scale(request.limits.maximumFrontierRecoveryGeneratedStates, tier);
    tierRequest.limits.maximumFrontierRecoveryLabels =
        scale(request.limits.maximumFrontierRecoveryLabels, tier);
    tierRequest.limits.maximumGraphGeneratedStates =
        scale(request.limits.maximumGraphGeneratedStates, tier);
    tierRequest.limits.maximumRetainedStates =
        scale(request.limits.maximumRetainedStates, tier);
    tierRequest.limits.maximumGraphLabels =
        scale(request.limits.maximumGraphLabels, tier);

    last = routeMember(tierRequest, environment);
    attempted.push_back(tier);
    cumulativeGenerated += last.diagnostics.generatedStates;
    last.diagnostics.effortTiersAttempted = attempted;
    last.diagnostics.cumulativeGeneratedStates = cumulativeGenerated;
    for (const auto& failure : priorFailures)
      last.diagnostics.stageStopReasons.insert(
          last.diagnostics.stageStopReasons.begin(), failure);
    if (complete(last.status)) {
      last.diagnostics.completedEffortPercent = tier;
      return last;
    }
    if (!retryable(last.status)) return last;
    priorFailures.push_back("effort tier " + std::to_string(tier) +
                            "% ended with " + toString(last.status) + ": " +
                            last.message);
  }
  return last;
}

RoutingResult RoutingEngine::routeMember(
    const RoutingRequest& request,
    const RoutingEnvironment& environment) const {
  RoutingResult result;
  reportProgress(request, RoutingProgressStage::Preflight, result.diagnostics);
  if (!finitePoint(request.start)) {
    result.status = RoutingStatus::InvalidStart;
    result.message = "start coordinate is invalid";
    return result;
  }
  if (!finitePoint(request.destination)) {
    result.status = RoutingStatus::InvalidDestination;
    result.message = "destination coordinate is invalid";
    return result;
  }
  if (environment.landAndBoundaries &&
      environment.landAndBoundaries->pointForbidden(request.start)) {
    result.status = RoutingStatus::InvalidStart;
    result.message = "start is inside a forbidden area";
    return result;
  }
  if (environment.landAndBoundaries &&
      environment.landAndBoundaries->pointForbidden(request.destination)) {
    result.status = RoutingStatus::InvalidDestination;
    result.message = "destination is inside a forbidden area";
    return result;
  }
  if (request.constraints.minimumDepthMetres &&
      environment.landAndBoundaries) {
    const double minimumDepth = *request.constraints.minimumDepthMetres;
    const auto startDepth =
        environment.landAndBoundaries->depthMetres(request.start);
    if (startDepth && *startDepth < minimumDepth) {
      std::ostringstream message;
      message << "start depth " << *startDepth
              << " m is shallower than configured minimum " << minimumDepth
              << " m";
      result.status = RoutingStatus::InvalidStart;
      result.message = message.str();
      result.diagnostics.stageStopReasons.push_back(result.message);
      return result;
    }
    const auto destinationDepth =
        environment.landAndBoundaries->depthMetres(request.destination);
    if (destinationDepth && *destinationDepth < minimumDepth) {
      std::ostringstream message;
      message << "destination depth " << *destinationDepth
              << " m is shallower than configured minimum " << minimumDepth
              << " m";
      result.status = RoutingStatus::InvalidDestination;
      result.message = message.str();
      result.diagnostics.stageStopReasons.push_back(result.message);
      return result;
    }
  }
  const auto check = preflight(request, environment);
  result.preflight = check;
  result.warnings = check.warnings;
  if (!check.canRoute) {
    result.status = preflightStatus(check);
    result.message = "routing preflight requires caller action";
    return result;
  }
  PolarPerformanceModel fallbackPerformance(request.vessel);
  const VesselPerformanceModel& performance =
      environment.performance ? *environment.performance : fallbackPerformance;

  const auto xtdExpansionsBefore =
      environment.xtdCurrent ? environment.xtdCurrent->expansionCount() : 0;
  const auto xtdSamplesBefore =
      environment.xtdCurrent ? environment.xtdCurrent->sampleCount() : 0;
  if (environment.xtdCurrent && request.environment.useCurrent &&
      check.coverage.currentFallbackNeeded) {
    reportProgress(request, RoutingProgressStage::CurrentCoverage,
                   result.diagnostics);
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
    area.south = std::max(
        -89.0, std::min(request.start.latitude, request.destination.latitude) -
                   margin);
    area.north = std::min(
        89.0, std::max(request.start.latitude, request.destination.latitude) +
                  margin);
    const double areaWidth = area.west <= area.east
                                 ? area.east - area.west
                                 : 360.0 - area.west + area.east;
    if (areaWidth * (area.north - area.south) >
        request.limits.maximumPredictionAreaSquareDegrees) {
      result.status = RoutingStatus::ResourceLimitReached;
      result.message = "initial current-prediction area exceeds resource limit";
      result.diagnostics.resourceLimitEvents.push_back(result.message);
      return result;
    }
    const auto coverage = environment.xtdCurrent->ensureCoverage(
        area, request.departure,
        request.departure + std::min(request.limits.maximumRouteDuration,
                                     Duration{std::chrono::hours{48}}),
        std::chrono::hours{1}, request.cancellation);
    if (coverage.status == CoverageStatus::Cancelled) {
      result.status = RoutingStatus::Cancelled;
      result.message = coverage.message;
      return result;
    }
    if (coverage.status == CoverageStatus::ResourceLimit) {
      result.status = RoutingStatus::ResourceLimitReached;
      result.message = coverage.message;
      result.diagnostics.resourceLimitEvents.push_back(
          coverage.message.empty() ? "current prediction resource limit"
                                   : coverage.message);
      return result;
    }
    if (coverage.status != CoverageStatus::Ready) {
      result.status = RoutingStatus::WeatherCoverageInsufficient;
      result.message = coverage.message.empty()
                           ? "current prediction coverage preparation failed"
                           : coverage.message;
      return result;
    }
  }

  SearchArtifacts latest;
  bool found = false;
  std::string lastValidationFailure;
  std::vector<std::size_t> rejectedCandidateSafePrefixes;
  bool candidateReplayProducedUsefulPrefix = false;
  bool requiresExternalConstraintEscalation = false;
  RouteValidator candidateValidator;
  const auto acceptCandidate = [&](const SearchArtifacts& search,
                                   std::size_t solution, SolverPath path) {
    auto legs = reconstruct(search, solution);
    const auto validation = candidateValidator.validate(
        request, environment, performance, legs, &result.diagnostics);
    result.diagnostics.validationSamples += validation.samples;
    if (!validation.passed) {
      lastValidationFailure = validation.failureReason;
      const std::vector<std::size_t> lineage =
          reconstructNodeIndices(search, solution);
      if (validation.acceptedPrefixLegs > 0 &&
          validation.acceptedPrefixLegs <= lineage.size()) {
        const std::size_t prefix =
            lineage[validation.acceptedPrefixLegs - 1];
        rejectedCandidateSafePrefixes.push_back(prefix);
        const double routeDistanceNm =
            distanceNm(request.start, request.destination);
        const double remainingNm =
            distanceNm(search.nodes[prefix].position, request.destination);
        // A one-leg replay discrepancy near departure is evidence that the
        // exploratory integration needs refinement, not a useful graph seed.
        // Preserve genuinely progressed prefixes, while allowing an early
        // mismatch to trigger the denser five-minute forward retry.
        candidateReplayProducedUsefulPrefix =
            candidateReplayProducedUsefulPrefix ||
            (validation.acceptedPrefixLegs >= 3 &&
             remainingNm <= routeDistanceNm * 0.75);
      }
      result.diagnostics.stageStopReasons.push_back(
          "candidate rejected by independent forward validation: " +
          validation.failureReason + "; accepted prefix legs " +
          std::to_string(validation.acceptedPrefixLegs));
      return false;
    }
    result.legs = std::move(legs);
    result.solverPath = path;
    return true;
  };
  const unsigned attempts = request.options.forceForwardFailureForTesting
                                ? 1U
                                : std::min(5U, request.options.retryStages);
  const std::uint64_t legacyGraphReserve =
      request.options.useGraphFallback && request.options.retryStages >= 7
          ? request.options.forceForwardFailureForTesting
                ? request.limits.maximumGeneratedStates -
                      std::min<std::uint64_t>(
                          request.limits.maximumGeneratedStates, 1000U)
                : std::max<std::uint64_t>(
                      1, request.limits.maximumGeneratedStates / 4U)
          : 0U;
  const std::uint64_t forwardGeneratedStateCeiling =
      request.limits.maximumForwardGeneratedStates > 0
          ? request.limits.maximumForwardGeneratedStates
          : request.limits.maximumGeneratedStates -
                std::min(request.limits.maximumGeneratedStates,
                         legacyGraphReserve);
  const std::uint64_t endpointGeneratedStateCeiling =
      request.limits.maximumCoastalEndpointGeneratedStates > 0
          ? request.limits.maximumCoastalEndpointGeneratedStates
          : std::max<std::uint64_t>(
                1024U, request.limits.maximumGeneratedStates / 50U);
  const std::uint64_t frontierRecoveryGeneratedStateBudget =
      request.limits.maximumFrontierRecoveryGeneratedStates;
  const std::uint64_t frontierRecoveryLabelBudget =
      request.limits.maximumFrontierRecoveryLabels;
  const std::uint64_t globalGraphGeneratedStateBudget =
      request.limits.maximumGraphGeneratedStates > 0
          ? request.limits.maximumGraphGeneratedStates
          : legacyGraphReserve;
  std::uint64_t previousAttemptGenerated{};
  for (unsigned attempt = 0; attempt < attempts; ++attempt) {
    if (attempt > 0) {
      const std::uint64_t estimatedRefinement = std::max<std::uint64_t>(
          previousAttemptGenerated, previousAttemptGenerated * 3U / 2U);
      const std::uint64_t ordinaryForwardGenerated =
          result.diagnostics.generatedStates -
          result.diagnostics.coastalEndpointGeneratedStates;
      if (ordinaryForwardGenerated >= forwardGeneratedStateCeiling ||
          estimatedRefinement >
              forwardGeneratedStateCeiling -
                  std::min(forwardGeneratedStateCeiling,
                           ordinaryForwardGenerated)) {
        result.diagnostics.stageStopReasons.push_back(
            "next forward refinement skipped to preserve bounded recovery "
            "budget");
        break;
      }
    }
    reportProgress(request, RoutingProgressStage::ForwardIsochrone,
                   result.diagnostics, attempt + 1, attempts);
    result.diagnostics.stagesAttempted.push_back(SolverPath::AdaptiveIsochrone);
    const double headingStep =
        std::max(request.options.refinedHeadingStepDegrees,
                 request.options.headingStepDegrees / std::pow(1.5, attempt));
    const Duration step = std::max(
        request.options.minimumTimeStep,
        Duration{static_cast<std::int64_t>(request.options.timeStep.count() /
                                           (attempt >= 2 ? 2 : 1))});
    const unsigned labels =
        std::min<unsigned>(64, request.options.labelsPerCell * (1 + attempt));
    const std::uint64_t generatedBefore = result.diagnostics.generatedStates;
    const std::uint64_t endpointGeneratedBefore =
        result.diagnostics.coastalEndpointGeneratedStates;
    latest = forwardSearch(
        request, environment, performance, headingStep, step, labels,
        !request.options.forceForwardFailureForTesting, attempt + 1, attempts,
        endpointGeneratedStateCeiling, forwardGeneratedStateCeiling,
        result.diagnostics);
    previousAttemptGenerated =
        (result.diagnostics.generatedStates - generatedBefore) -
        (result.diagnostics.coastalEndpointGeneratedStates -
         endpointGeneratedBefore);
    if (latest.solution) {
      rejectedCandidateSafePrefixes.clear();
      candidateReplayProducedUsefulPrefix = false;
      const std::vector<std::size_t> candidates =
          latest.alternativeSolutions.empty()
              ? std::vector<std::size_t>{*latest.solution}
              : latest.alternativeSolutions;
      for (const std::size_t candidate : candidates) {
        found =
            acceptCandidate(latest, candidate, SolverPath::AdaptiveIsochrone);
        if (found) break;
      }
      if (found) break;
      const std::size_t firstRejectedCandidate = candidates.front();
      latest.solution.reset();
      latest.alternativeSolutions.clear();
      // Forward completions are appended after all admitted frontier nodes.
      // Failed independent replays must never become recovery seeds.
      if (firstRejectedCandidate < latest.nodes.size())
        latest.nodes.resize(firstRejectedCandidate);
      latest.preferredRecoverySeeds = rejectedCandidateSafePrefixes;
      requiresExternalConstraintEscalation =
          environment.landAndBoundaries &&
          environment.landAndBoundaries
              ->validationFailureRequiresSearchEscalation();
      latest.failure = requiresExternalConstraintEscalation
                           ? RoutingStatus::ValidationFailure
                           : RoutingStatus::SearchIncomplete;
      latest.reason =
          requiresExternalConstraintEscalation
              ? "authoritative candidate replay requires detailed-constraint "
                "search escalation: " +
                    lastValidationFailure
              : "forward candidates failed independent validation";
    }
    result.diagnostics.stageStopReasons.push_back(latest.reason);
    if (latest.failure == RoutingStatus::Cancelled || latest.resourceLimited)
      break;
    if (requiresExternalConstraintEscalation) break;
    if (candidateReplayProducedUsefulPrefix) {
      result.diagnostics.stageStopReasons.push_back(
          "forward candidate replay supplied a safe recovery prefix; "
          "skipping whole-route refinement");
      break;
    }
    if (latest.convergenceStalled) break;
  }
  result.diagnostics.forwardGeneratedStates =
      result.diagnostics.generatedStates -
      result.diagnostics.coastalEndpointGeneratedStates;

  // Keep the final forward frontier geometry available to inspection clients
  // even when production recovery subsequently replaces SearchArtifacts with
  // graph labels.  This data is never used for route acceptance.
  const std::vector<IsochroneLayer> forwardIsochrones = latest.isochrones;
  if (!found && !requiresExternalConstraintEscalation &&
      latest.failure != RoutingStatus::Cancelled &&
      request.options.useReverseRecovery && latest.nodes.size() > 1 &&
      request.options.retryStages >= 6 &&
      !request.options.forceReverseFailureForTesting) {
    reportProgress(request, RoutingProgressStage::ReverseRecovery,
                   result.diagnostics);
    const std::size_t forwardNodeCount = latest.nodes.size();
    if (auto recovered = reverseRecovery(request, environment, performance,
                                         latest, result.diagnostics)) {
      latest = std::move(recovered->first);
      found = acceptCandidate(latest, recovered->second,
                              SolverPath::ReverseRecovery);
      if (!found) {
        latest.solution.reset();
        // Reverse recovery appends its bridge nodes.  If the complete bridge
        // fails replay, retain only the independently admitted forward
        // prefixes for graph recovery.
        if (latest.nodes.size() > forwardNodeCount)
          latest.nodes.resize(forwardNodeCount);
        latest.failure = RoutingStatus::SearchIncomplete;
        latest.reason = "reverse candidate failed independent validation";
      }
    }
  }

  if (!found && !requiresExternalConstraintEscalation &&
      latest.failure != RoutingStatus::Cancelled &&
      request.options.useFrontierRecovery &&
      frontierRecoveryGeneratedStateBudget > 0 &&
      frontierRecoveryLabelBudget > 0 && latest.nodes.size() > 1) {
    constexpr std::size_t kMaximumPrefixReplays = 64;
    constexpr std::size_t kMaximumAcceptedPrefixes = 24;
    const std::vector<std::size_t> prefixCandidates =
        diverseFrontierRecoveryCandidates(request, latest,
                                          kMaximumPrefixReplays);
    std::vector<std::size_t> accepted;
    accepted.reserve(kMaximumAcceptedPrefixes);
    for (const std::size_t index : prefixCandidates) {
      if (result.diagnostics.validationSamples >=
          request.limits.maximumValidationSamples)
        break;
      auto legs = reconstruct(latest, index);
      const auto validation = candidateValidator.validatePrefix(
          request, environment, performance, legs, &result.diagnostics);
      result.diagnostics.validationSamples += validation.samples;
      if (validation.passed) accepted.push_back(index);
      if (accepted.size() >= kMaximumAcceptedPrefixes) break;
    }
    latest.preferredRecoverySeeds = std::move(accepted);
    result.diagnostics.stageStopReasons.push_back(
        "diverse frontier-prefix replay accepted " +
        std::to_string(latest.preferredRecoverySeeds.size()) + " of " +
        std::to_string(prefixCandidates.size()) + " candidates");

    if (!latest.preferredRecoverySeeds.empty()) {
      reportProgress(request, RoutingProgressStage::FrontierRecovery,
                     result.diagnostics);
      result.diagnostics.stagesAttempted.push_back(
          SolverPath::FrontierRecovery);
      const std::uint64_t generatedBefore =
          result.diagnostics.generatedStates;
      const std::uint64_t labelsBefore = result.diagnostics.graphLabels;
      latest = graphSearch(
          request, environment, performance, &latest,
          frontierRecoveryGeneratedStateBudget, frontierRecoveryLabelBudget,
          GraphSearchKind::FrontierRecovery, result.diagnostics);
      result.diagnostics.frontierRecoveryGeneratedStates =
          result.diagnostics.generatedStates - generatedBefore;
      result.diagnostics.frontierRecoveryLabels =
          result.diagnostics.graphLabels - labelsBefore;
      if (latest.solution) {
        found = acceptCandidate(latest, *latest.solution,
                                SolverPath::FrontierRecovery);
        if (!found) {
          latest.solution.reset();
          latest.failure = RoutingStatus::ValidationFailure;
          latest.reason =
              "frontier recovery candidate failed independent validation: " +
              lastValidationFailure;
        }
      } else {
        result.diagnostics.stageStopReasons.push_back(latest.reason);
      }
    }
  }

  if (!found && !requiresExternalConstraintEscalation &&
      latest.failure != RoutingStatus::Cancelled &&
      request.options.useGraphFallback && request.options.retryStages >= 7 &&
      globalGraphGeneratedStateBudget > 0) {
    reportProgress(request, RoutingProgressStage::GraphFallback,
                   result.diagnostics);
    result.diagnostics.stagesAttempted.push_back(SolverPath::GraphFallback);
    const std::uint64_t generatedBefore = result.diagnostics.generatedStates;
    latest = graphSearch(
        request, environment, performance, nullptr,
        globalGraphGeneratedStateBudget, request.limits.maximumGraphLabels,
        GraphSearchKind::GlobalFallback, result.diagnostics);
    result.diagnostics.graphGeneratedStates =
        result.diagnostics.generatedStates - generatedBefore;
    if (latest.solution) {
      found =
          acceptCandidate(latest, *latest.solution, SolverPath::GraphFallback);
      if (!found) {
        latest.solution.reset();
        latest.failure = RoutingStatus::ValidationFailure;
        latest.reason = "graph candidate failed independent validation: " +
                        lastValidationFailure;
      }
    } else {
      result.diagnostics.stageStopReasons.push_back(latest.reason);
    }
  }

  result.diagnostics.xtdCoverageExpansions =
      environment.xtdCurrent
          ? environment.xtdCurrent->expansionCount() - xtdExpansionsBefore
          : 0;
  result.diagnostics.xtdSamples =
      environment.xtdCurrent
          ? environment.xtdCurrent->sampleCount() - xtdSamplesBefore
          : 0;
  if (!found) {
    result.visualization.isochrones =
        latest.isochrones.empty() ? forwardIsochrones : latest.isochrones;
    result.status = latest.failure;
    result.message = latest.reason;
    return result;
  }

  result.visualization.isochrones =
      latest.isochrones.empty() ? forwardIsochrones : latest.isochrones;

  calculateResultSummaries(result);
  reportProgress(request, RoutingProgressStage::Validation, result.diagnostics);
  RouteValidator validator;
  result.validation = validator.validate(request, environment, performance,
                                         result.legs, &result.diagnostics);
  result.diagnostics.validationSamples += result.validation.samples;
  if (!result.validation.passed) {
    result.status = RoutingStatus::ValidationFailure;
    result.message = result.validation.failureReason;
    result.legs.clear();
    return result;
  }
  result.sourceTransitions = result.validation.sourceTransitions;
  result.environment = result.validation.environment;
  result.status = result.solverPath == SolverPath::ReverseRecovery
                      ? RoutingStatus::CompleteUsingReverseRecovery
                  : result.solverPath == SolverPath::FrontierRecovery
                      ? RoutingStatus::CompleteUsingFrontierRecovery
                  : result.solverPath == SolverPath::GraphFallback
                      ? RoutingStatus::CompleteUsingGraphFallback
                      : RoutingStatus::Complete;
  result.message = "route passed independent chronological forward validation";
  reportProgress(request, RoutingProgressStage::Complete, result.diagnostics);
  return result;
}

}  // namespace supercpn::weather_routing
