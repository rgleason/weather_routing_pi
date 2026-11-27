// SPDX-License-Identifier: GPL-3.0-or-later
#include "engine/corridor/Engine.h"
#include "ArrivalGuide.h"
#include "ConnectedWaterGuide.h"
#include "Geography.h"
#include "MotionKernel.h"
#include "RoutingInternal.h"
#include "WeatherGuide.h"
#include "engine/meridian/StateKey.h"
#include "engine/meridian/StepPolicy.h"
#include "supercpn/weather_routing/Preview.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <map>
#include <numbers>
#include <set>

namespace corridor_routing {
namespace {
namespace in = wr::internal;
using Clock = std::chrono::steady_clock;
constexpr auto noParent = std::numeric_limits<std::size_t>::max();
constexpr auto radians = std::numbers::pi / 180.;

struct Node {
  in::MotionState state;
  std::size_t parent{noParent};
  wr::RouteLeg leg;
};

struct CheckedPrefix {
  std::vector<wr::RouteLeg> legs;
  in::MotionState arrival;
};

struct Move {
  double priority;
  double remaining;
  double finePriority;
  std::uint64_t order;
  std::size_t parent;
  double heading;
  wr::Duration duration;
  bool wait;
  unsigned side;
  bool operator<(const Move& other) const {
    return std::tie(priority, order) < std::tie(other.priority, other.order);
  }
};

struct ProgressOrder {
  bool operator()(const Move& a, const Move& b) const {
    return std::tie(a.remaining, a.priority, a.order) <
           std::tie(b.remaining, b.priority, b.order);
  }
};

struct FineGuideOrder {
  bool operator()(const Move& a, const Move& b) const {
    return std::tie(a.finePriority, a.order) < std::tie(b.finePriority, b.order);
  }
};

class Approaches {
  static constexpr unsigned maximumPending = 36000;
  std::array<std::set<Move>, 3> pending_;
  std::set<Move, ProgressOrder> progress_;
  std::set<Move, FineGuideOrder> alternate_;
  unsigned queueLimit_;
  bool useBothGuides_;
  unsigned cursor_{};
  unsigned pops_{};

public:
  Approaches(bool diverse, bool useBothGuides)
      : queueLimit_(diverse ? maximumPending / pending_.size() : maximumPending),
        useBothGuides_(useBothGuides) {}

  void add(Move move) {
    auto& queue = pending_[move.side];
    queue.insert(move);
    progress_.insert(move);
    if (useBothGuides_) alternate_.insert(move);
    if (queue.size() > queueLimit_) {
      auto worst = std::prev(queue.end());
      progress_.erase(*worst);
      if (useBothGuides_) alternate_.erase(*worst);
      queue.erase(worst);
    }
  }

  std::optional<Move> take() {
    if (++pops_ % 5 == 0 && !progress_.empty()) {
      auto selected = progress_.extract(progress_.begin());
      const auto& move = selected.value();
      pending_[move.side].erase(move);
      if (useBothGuides_) alternate_.erase(move);
      return std::move(selected.value());
    }
    if (useBothGuides_ && pops_ % 3 == 0 && !alternate_.empty()) {
      auto selected = alternate_.extract(alternate_.begin());
      const auto& move = selected.value();
      pending_[move.side].erase(move);
      progress_.erase(move);
      return std::move(selected.value());
    }
    // Give both sides recurring turns even when the centre has cheaper moves.
    constexpr std::array order{0u, 1u, 0u, 2u, 0u};
    for (unsigned choice = 0; choice < order.size(); ++choice) {
      const auto side = order[cursor_++ % order.size()];
      auto& queue = pending_[side];
      if (!queue.empty()) {
        auto selected = queue.extract(queue.begin());
        progress_.erase(selected.value());
        if (useBothGuides_) alternate_.erase(selected.value());
        return std::move(selected.value());
      }
    }
    return {};
  }
};

bool valid(wr::GeoPoint point) {
  return std::isfinite(point.latitude) && std::abs(point.latitude) <= 85 &&
         std::isfinite(point.longitude) && std::abs(point.longitude) <= 180;
}

unsigned approach(wr::GeoPoint start, double lineBearing, wr::GeoPoint point,
                  double width) {
  const auto distance = wr::distanceNm(start, point);
  const auto bearing = wr::initialBearingDegrees(start, point);
  const auto crossTrack = distance * std::sin((bearing - lineBearing) * radians);
  if (crossTrack < -width) return 1;
  if (crossTrack > width) return 2;
  return 0;
}

std::vector<wr::RouteLeg> path(const std::vector<Node>& nodes,
                              std::size_t parent, wr::RouteLeg finalLeg) {
  std::vector<wr::RouteLeg> legs;
  legs.push_back(std::move(finalLeg));
  for (auto at = parent; nodes[at].parent != noParent; at = nodes[at].parent)
    legs.push_back(nodes[at].leg);
  std::reverse(legs.begin(), legs.end());
  return legs;
}

bool lessResources(const in::MotionState& previous, const in::MotionState& next) {
  // Times in different windows never share a bucket. Within a window, require
  // the same time and nearly the same point before dropping a duplicate.
  return previous.time == next.time &&
         wr::distanceNm(previous.position, next.position) < 0.005 &&
         previous.modeDuration == next.modeDuration &&
         previous.motorDuration <= next.motorDuration &&
         previous.waitDuration <= next.waitDuration &&
         previous.fuel <= next.fuel && previous.risk <= next.risk &&
         previous.manoeuvres <= next.manoeuvres;
}

SearchResult searchPass(const wr::RoutingRequest& request,
                        const wr::RoutingEnvironment& environment,
                        const Options& options,
                        const WeatherGuide* weatherGuide = nullptr,
                        const ArrivalGuide* arrivalGuide = nullptr,
                        const ConnectedWaterGuide* waterGuide = nullptr,
                        const CheckedPrefix* prefix = nullptr,
                        in::MotionState* arrivalState = nullptr) {
  SearchResult result;
  auto& route = result.route;
  auto& diagnostics = route.diagnostics;
  const auto started = Clock::now();
  std::size_t waterStage = 0;
  auto checkpoints = waterGuide ? waterGuide->route() : std::vector<wr::GeoPoint>{};
  unsigned checkpointsReached = 0;
  wr::Duration closestElapsed{};
  const auto finish = [&](wr::RoutingStatus status, const char* message) {
    if (waterGuide)
      diagnostics.stageStopReasons.emplace_back("Connected-water recovery reached " +
          std::to_string(checkpointsReached) + " checkpoints. Closest approach at sailing hour " +
          std::to_string(std::chrono::duration_cast<std::chrono::hours>(closestElapsed).count()));
    if (route.legs.empty()) {
      route.status = status;
      route.message = message;
    } else {
      diagnostics.closestApproachNm = 0.;
      result.closestPosition = request.destination;
      in::summariseRoute(route);
    }
    return std::move(result);
  };

  if (request.cancellation.cancelled())
    return finish(wr::RoutingStatus::Cancelled, "Search cancelled");
  const auto* geography = environment.landAndBoundaries.get();
  const auto* hints = options.geography ? options.geography.get() : geography;
  if (!valid(request.start) || (geography && geography->pointForbidden(request.start)))
    return finish(wr::RoutingStatus::InvalidStart, "Start is forbidden or outside search bounds");
  if (!valid(request.destination) ||
      (geography && geography->pointForbidden(request.destination)))
    return finish(wr::RoutingStatus::InvalidDestination, "Destination is forbidden or outside search bounds");
  if (request.objective.kind != wr::ObjectiveKind::Fastest &&
      request.objective.kind != wr::ObjectiveKind::FastestUnderSafetyLimits)
    return finish(wr::RoutingStatus::InvalidVesselConfiguration,
                  "Experiment supports the fastest-arrival objective");

  route.preflight = wr::RoutingEngine{}.preflight(request, environment);
  route.warnings = route.preflight->warnings;
  if (!route.preflight->canRoute)
    return finish(in::failedPreflightStatus(*route.preflight), "Weather or vessel preflight failed");
  if (environment.xtdCurrent && request.environment.useCurrent &&
      route.preflight->coverage.currentFallbackNeeded)
    return finish(wr::RoutingStatus::CurrentDataRequired, "Prepare current prediction coverage");

  const auto budget = request.limits.maximumGeneratedStates;
  const auto nodeLimit = std::min<std::uint64_t>(40000, request.limits.maximumRetainedStates);
  if (!budget || nodeLimit < 2 || request.limits.maximumRouteDuration <= wr::Duration{})
    return finish(wr::RoutingStatus::ResourceLimitReached, "No search allowance");

  wr::PolarPerformanceModel fallback(request.vessel);
  const auto& performance = environment.performance ? *environment.performance : fallback;
  const auto horizon = request.departure + request.limits.maximumRouteDuration;
  const auto nominal = std::clamp(request.options.timeStep, wr::Duration{60}, wr::Duration{21600});
  const auto direct = wr::distanceNm(request.start, request.destination);
  const auto offshore = waterGuide && direct > 900.
      ? std::max(nominal, wr::Duration{10800}) : nominal;
  meridian_routing::StepPolicy steps(hints, std::min(nominal, wr::Duration{900}),
                                    offshore, request.constraints.landSafetyMarginNm);
  const auto lineBearing = wr::initialBearingDegrees(request.start, request.destination);
  const auto spread = std::clamp(direct * 0.025, 0.12, 4.);
  const Geography guide(request, hints, options.refineCoastalGeography, options.discoverChannels);
  result.guideQueries = guide.queries() + (arrivalGuide ? arrivalGuide->queries() : 0) +
                        (waterGuide ? waterGuide->queries() : 0);
  result.fineGeographicGuide = guide.refined();
  result.wideGeographicGuide = guide.wide();
  result.channelQueries = guide.channelQueries();
  result.channelLengthNm = guide.channelLength();
  if (guide.available())
    diagnostics.stageStopReasons.emplace_back("Advisory geographic detour field available");
  double referenceSpeed = 0.;
  for (const auto& profile : request.vessel.profiles)
    for (const auto& row : profile.rows)
      for (const auto& point : row.points)
        referenceSpeed = std::max(referenceSpeed, point.boatSpeedKnots);
  referenceSpeed = std::clamp(referenceSpeed * 0.8, 2., 30.);

  const bool useBothGuides = options.useBothGuides && guide.refined();
  Approaches open(options.diversifyApproaches, useBothGuides);
  std::vector<Node> nodes;
  nodes.reserve(nodeLimit);
  std::map<meridian_routing::StateKey, std::vector<std::size_t>> seen;
  in::MotionState start;
  start.position = request.start;
  start.time = request.departure;
  start.departureEgressActive = geography && request.constraints.landSafetyMarginNm > 0 &&
      geography->distanceToForbiddenNm(start.position) < request.constraints.landSafetyMarginNm;
  nodes.push_back({start, noParent, {}});
  if (prefix) {
    for (const auto& leg : prefix->legs) {
      auto state = in::MotionState{};
      state.position = leg.end;
      state.time = leg.endTime;
      nodes.push_back({std::move(state), nodes.size() - 1, leg});
    }
    nodes.back().state = prefix->arrival;
  }
  const auto seed = nodes.size() - 1;
  diagnostics.retainedStates = nodes.size();
  diagnostics.closestApproachNm = wr::distanceNm(nodes.back().state.position, request.destination);
  result.closestPosition = nodes.back().state.position;
  auto lastFailure = wr::RoutingStatus::SearchIncomplete;
  std::uint64_t stopAfter = budget;
  auto bestPassage = request.limits.maximumRouteDuration;
  std::size_t closest = seed;
  std::string validationEscalation;
  wr::RoutingPreviewPublisher preview(request);
  const auto margin = request.constraints.landSafetyMarginNm;
  const auto checkpoint = [&]() -> std::optional<wr::GeoPoint> {
    if (waterStage + 1 < checkpoints.size()) return checkpoints[waterStage];
    return {};
  };
  std::optional<ArrivalGuide> checkpointGuide;
  const auto prepareCheckpoint = [&](wr::GeoPoint from) {
    if (!waterGuide) return;
    checkpointGuide.reset();
    const auto gate = checkpoint();
    if (!gate || !hints->segmentForbidden(from, *gate, request.constraints.landSafetyMarginNm))
      return;
    auto local = request;
    local.destination = *gate;
    const auto spacing = std::clamp(wr::distanceNm(from, *gate) / 25., .5, 3.);
    checkpointGuide.emplace(local, *hints, spacing);
    result.guideQueries += checkpointGuide->queries();
  };
  prepareCheckpoint(nodes.back().state.position);
  const auto advanceCheckpoint = [&](wr::GeoPoint position) {
    ++checkpointsReached;
    ++waterStage;
    prepareCheckpoint(position);
  };

  const auto enqueue = [&](std::size_t node, double heading, wr::Duration duration,
                           wr::GeoPoint estimate, bool wait) {
    const auto& state = nodes[node].state;
    const auto remaining = wr::distanceNm(estimate, request.destination);
    const auto elapsed = state.time - request.departure + duration;
    if (!std::isfinite(remaining) || diagnostics.generatedStates >= budget ||
        elapsed > bestPassage) return;
    const auto side = options.diversifyApproaches
        ? approach(request.start, lineBearing, estimate, spread) : 0u;
    const auto estimateDetour = [&](bool preferFine) {
      const auto offshore = guide.estimate(estimate, remaining, preferFine);
      if (arrivalGuide) return arrivalGuide->estimate(estimate, remaining, offshore);
      return waterGuide ? waterGuide->estimate(estimate, remaining, offshore) : offshore;
    };
    const auto detour = estimateDetour(!useBothGuides);
    const auto fineDetour = useBothGuides ? estimateDetour(true) : detour;
    const auto progressWeight = waterGuide ? 3.5 : 1.75;
    const auto gate = checkpoint();
    double progress = gate ? wr::distanceNm(estimate, *gate)
                           : (guide.channelAvailable() ? fineDetour : detour);
    if (checkpointGuide && checkpointGuide->available())
      progress = checkpointGuide->estimate(estimate, progress, progress);
    double ordering = progressWeight * (gate ? progress : detour) * 3600. / referenceSpeed;
    if (weatherGuide && (!guide.available() ||
        (remaining > 40. && wr::distanceNm(request.start, estimate) > 40.))) {
      const auto [weatherSeconds, lateralNm] = weatherGuide->costs(estimate);
      // Keep enough geometric progress to reach the destination within the
      // refinement window, even if the coarse transit forecast is optimistic.
      ordering = .45 * weatherSeconds + (2.8 * detour + 1.65 * lateralNm) * 3600. / referenceSpeed;
      progress = detour + .7 * lateralNm;
    }
    const auto rank = elapsed.count() + ordering;
    const auto finePriority = elapsed.count() + progressWeight * fineDetour * 3600. / referenceSpeed;
    open.add({rank, progress,
              finePriority, diagnostics.generatedStates++,
              node, heading, duration, wait, side});
  };

  const auto expand = [&](std::size_t node) {
    const auto& state = nodes[node].state;
    if (state.time >= horizon || request.cancellation.cancelled()) return;
    auto duration = std::min(steps(state.position), horizon - state.time);
    const auto gate = checkpoint();
    if (gate && wr::distanceNm(state.position, *gate) < 22.)
      duration = std::min(duration, wr::Duration{3600});
    const auto remaining = wr::distanceNm(state.position, request.destination);
    if (remaining < 3.) duration = std::min(duration, wr::Duration{300});
    if (remaining < 1.) duration = std::min(duration, wr::Duration{120});
    const auto weather = in::resolveEnvironment(request, environment, state.position, state.time);
    ++diagnostics.weatherSamples;
    if (weather.failureStatus != wr::RoutingStatus::Complete) {
      lastFailure = weather.failureStatus;
      return;
    }
    const auto& wind = weather.snapshot.wind.velocity;
    const auto& current = weather.snapshot.current.velocity;
    const auto destination = wr::initialBearingDegrees(
        state.position, gate.value_or(request.destination));
    const auto tws = wr::vectorMagnitudeKnots(wind);
    const auto channelHeading = guide.channelHeading(state.position);
    const auto headings = channelHeading ? 51u : 50u;
    for (unsigned headingIndex = 0; headingIndex < headings; ++headingIndex) {
      if (diagnostics.generatedStates >= budget) break;
      const auto heading = headingIndex < 48 ? headingIndex * 7.5
          : headingIndex == 48 ? destination : headingIndex == 49 ? state.incomingHeading
                                                                 : *channelHeading;
      const auto twa = wr::trueWindAngleDegrees(wind, heading);
      const auto candidates = performance.candidatesAt(state.position, state.time, tws,
          twa, weather.snapshot.waves, state.mode, state.modeDuration);
      double speed = 0.;
      for (const auto& candidate : candidates)
        if (candidate.valid) speed = std::max(speed, candidate.speedThroughWaterKnots);
      if (speed <= 0.05) continue;
      const auto water = wr::speedDirectionToVector(speed, heading);
      const wr::Vector2 ground{water.eastKnots + current.eastKnots,
                               water.northKnots + current.northKnots};
      const auto trial = headingIndex == 50 ? std::min(duration, wr::Duration{420}) : duration;
      const auto estimate = wr::destinationPoint(state.position, wr::vectorDirectionToDegrees(ground),
          wr::vectorMagnitudeKnots(ground) * trial.count() / 3600.);
      enqueue(node, heading, trial, estimate, false);
    }
    if (request.options.allowWaiting) {
      const auto delay = std::min(wr::Duration{900}, horizon - state.time);
      enqueue(node, state.incomingHeading, delay, state.position, true);
    }
  };

  const auto tryArrival = [&](std::size_t at) {
    const auto& from = nodes[at].state;
    const auto remaining = wr::distanceNm(from.position, request.destination);
    if (remaining > 15.) return;
    // A direct connection across a headland normally cannot work. Keep
    // occasional probes there and try all close arrivals. When currents
    // are enabled the connector can curve, so retain its usual cadence.
    if (options.paceCoastalArrivals && result.guideQueries && remaining > 2.5 &&
        !request.environment.useCurrent &&
        hints->segmentForbidden(from.position, request.destination, 0.) &&
        ++result.deferredArrivals % 16 != 0)
      return;
    const auto window = std::min(wr::Duration{14400}, horizon - from.time);
    if (window <= wr::Duration{} || diagnostics.validationSamples >= request.limits.maximumValidationSamples)
      return;
    ++result.arrivalProbes;
    auto connection = in::checkedConnection(request, environment, performance, from, window, diagnostics);
    if (!connection || wr::distanceNm(connection->state.position, request.destination) > 0.001)
      return;
    const auto time = connection->state.time - request.departure;
    if (time > horizon - request.departure || (!route.legs.empty() && time >= bestPassage))
      return;
    auto legs = path(nodes, at, std::move(connection->leg));
    if (geography) geography->prepareValidationRoute(legs, request.constraints.landSafetyMarginNm);
    auto replay = request;
    replay.limits.maximumValidationSamples -= diagnostics.validationSamples;
    auto validation = wr::RouteValidator{}.validate(replay, environment, performance, legs);
    diagnostics.validationSamples += validation.samples;
    if (!validation.passed) {
      if (geography && geography->validationFailureRequiresSearchEscalation())
        validationEscalation = validation.failureReason;
      return;
    }
    route.legs = std::move(legs);
    if (arrivalState) *arrivalState = connection->state;
    route.validation = std::move(validation);
    route.status = wr::RoutingStatus::Complete;
    route.message = "Independently validated arrival";
    route.engineIdentity = "corridor";
    bestPassage = time;
    ++result.acceptedArrivals;
    if (!result.firstValidSeconds) {
      result.firstValidSeconds = std::chrono::duration<double>(Clock::now() - started).count();
      result.firstPassage = time;
      stopAfter = result.checkedMotions + std::min<std::uint64_t>(
          options.additionalMotions, budget - result.checkedMotions);
    }
  };

  tryArrival(seed);
  if (!validationEscalation.empty())
    return finish(wr::RoutingStatus::ValidationFailure, validationEscalation.c_str());
  if (result.checkedMotions < stopAfter) expand(seed);
  while (result.checkedMotions < stopAfter && nodes.size() < nodeLimit) {
    if (request.cancellation.cancelled())
      return finish(wr::RoutingStatus::Cancelled, "Search reached its deadline or was cancelled");
    const auto selected = open.take();
    if (!selected) break;
    const auto& move = *selected;
    const auto& parent = nodes[move.parent].state;
    if (parent.time + move.duration - request.departure > bestPassage) continue;
    ++result.checkedMotions;
    ++result.evaluatedByApproach[move.side];
    std::vector<in::MotionCandidate> motions;
    if (move.wait) {
      auto waiting = in::checkedWait(request, environment, parent, move.duration, diagnostics);
      if (waiting) {
        // Chronological replay restarts the propulsion dwell clock after a hold.
        waiting->state.modeDuration = {};
        motions.push_back(std::move(*waiting));
      }
    } else {
      motions = in::checkedMotion(request, environment, performance, parent, move.heading,
                                   move.duration, wr::Duration{300}, diagnostics, &lastFailure);
    }
    for (auto& motion : motions) {
      if (nodes.size() >= nodeLimit) break;
      if (!valid(motion.state.position) || motion.state.time >= horizon ||
          wr::distanceNm(request.start, motion.state.position) > request.limits.maximumExplorationDistanceNm)
        continue;
      const auto fine = steps(motion.state.position) <= wr::Duration{1800};
      auto& alternatives = seen[meridian_routing::key(motion.state, request.departure, fine)];
      if (std::any_of(alternatives.begin(), alternatives.end(), [&](auto other) {
            return lessResources(nodes[other].state, motion.state);
          })) {
        ++diagnostics.pruned.duplicate;
        continue;
      }
      if (alternatives.size() >= 2) {
        const auto priorTime = nodes[alternatives.front()].state.time;
        const auto lastTime = nodes[alternatives.back()].state.time;
        // Keep the earliest and latest arrivals in a full bucket. Already
        // queued work stays valid, as do descendants of an earlier label.
        if (motion.state.time < priorTime) alternatives.erase(alternatives.begin());
        else if (motion.state.time > lastTime) alternatives.pop_back();
        else {
          ++diagnostics.pruned.cellLabelCap;
          continue;
        }
      }
      const auto at = nodes.size();
      const auto before = std::lower_bound(alternatives.begin(), alternatives.end(), motion.state.time,
          [&](auto other, auto time) { return nodes[other].state.time < time; });
      alternatives.insert(before, at);
      nodes.push_back({std::move(motion.state), move.parent, std::move(motion.leg)});
      diagnostics.retainedStates = std::max<std::uint64_t>(diagnostics.retainedStates, nodes.size());
      const auto remaining = wr::distanceNm(nodes.back().state.position, request.destination);
      if (remaining < diagnostics.closestApproachNm) {
        diagnostics.closestApproachNm = remaining;
        closest = at;
        result.closestPosition = nodes.back().state.position;
        closestElapsed = nodes.back().state.time - request.departure;
      }
      tryArrival(at);
      if (!validationEscalation.empty())
        return finish(wr::RoutingStatus::ValidationFailure, validationEscalation.c_str());
      if (result.firstValidSeconds && !options.additionalMotions) break;
      if (const auto gate = checkpoint(); gate &&
          wr::distanceNm(nodes[at].state.position, *gate) <= 8. &&
          !hints->segmentForbidden(nodes[at].state.position, *gate,
                                   request.constraints.landSafetyMarginNm)) {
        advanceCheckpoint(nodes[at].state.position);
        std::vector<Node> prefix;
        for (auto parent = at; parent != noParent; parent = nodes[parent].parent)
          prefix.push_back(std::move(nodes[parent]));
        std::reverse(prefix.begin(), prefix.end());
        for (std::size_t i = 1; i < prefix.size(); ++i) prefix[i].parent = i - 1;
        nodes = std::move(prefix);
        nodes.reserve(nodeLimit);
        open = Approaches(options.diversifyApproaches, useBothGuides);
        seen.clear();
        closest = nodes.size() - 1;
        expand(closest);
        break;
      }
      if (result.checkedMotions < stopAfter) expand(at);
    }
    if (result.checkedMotions % 256 == 0) {
      if (request.progress)
        request.progress({wr::RoutingProgressStage::ForwardIsochrone, 1, 1,
            diagnostics.generatedStates, nodes.size(), diagnostics.landChecks,
            diagnostics.closestApproachNm, request.options.routingEffortPercent});
      preview.publish([&] {
        wr::RoutingPreview view;
        view.time = nodes[closest].state.time;
        for (auto n = closest; n != noParent && view.path.size() < 256; n = nodes[n].parent)
          view.path.push_back(nodes[n].state.position);
        std::reverse(view.path.begin(), view.path.end());
        return view;
      });
    }
  }
  if (nodes.size() >= nodeLimit || diagnostics.generatedStates >= budget)
    return finish(wr::RoutingStatus::ResourceLimitReached, "Search allowance exhausted");
  return finish(lastFailure, "Search ended without a validated arrival");
}
void combineWork(SearchResult& selected, const SearchResult& other) {
  selected.fineGeographicGuide |= other.fineGeographicGuide;
  selected.wideGeographicGuide |= other.wideGeographicGuide;
  selected.guideQueries += other.guideQueries;
  selected.channelQueries += other.channelQueries;
  selected.channelLengthNm = std::max(selected.channelLengthNm, other.channelLengthNm);
  selected.arrivalProbes += other.arrivalProbes;
  selected.deferredArrivals += other.deferredArrivals;
  selected.checkedMotions += other.checkedMotions;
  selected.acceptedArrivals += other.acceptedArrivals;
  for (unsigned side = 0; side < selected.evaluatedByApproach.size(); ++side)
    selected.evaluatedByApproach[side] += other.evaluatedByApproach[side];
  auto& total = selected.route.diagnostics;
  const auto& work = other.route.diagnostics;
  total.generatedStates += work.generatedStates;
  total.retainedStates = std::max(total.retainedStates, work.retainedStates);
  if (work.closestApproachNm < total.closestApproachNm) {
    total.closestApproachNm = work.closestApproachNm;
    selected.closestPosition = other.closestPosition;
  }
  total.weatherSamples += work.weatherSamples;
  total.landChecks += work.landChecks;
  total.landRejections += work.landRejections;
  total.validationSamples += work.validationSamples;
  total.pruned.duplicate += work.pruned.duplicate;
  total.stageStopReasons.insert(total.stageStopReasons.end(),
                                work.stageStopReasons.begin(), work.stageStopReasons.end());
}

bool hasRecoveryBudget(const wr::RoutingRequest& request, const SearchResult& result) {
  const auto& work = result.route.diagnostics;
  return !request.cancellation.cancelled() && result.route.preflight &&
         result.route.preflight->canRoute &&
         work.generatedStates < request.limits.maximumGeneratedStates &&
         work.validationSamples < request.limits.maximumValidationSamples;
}

bool recoverableStop(const SearchResult& result) {
  const auto status = result.route.status;
  return !result.route.validation.passed &&
         (status == wr::RoutingStatus::ResourceLimitReached ||
          status == wr::RoutingStatus::SearchIncomplete ||
          status == wr::RoutingStatus::NoFeasibleRoute);
}

void chargeWork(wr::RoutingRequest& request, const SearchResult& result) {
  const auto& work = result.route.diagnostics;
  request.limits.maximumGeneratedStates -=
      std::min(work.generatedStates, request.limits.maximumGeneratedStates);
  request.limits.maximumValidationSamples -=
      std::min(work.validationSamples, request.limits.maximumValidationSamples);
}

bool recoverArrival(const wr::RoutingRequest& request,
                    const wr::RoutingEnvironment& environment,
                    const Options& options, Clock::time_point started,
                    SearchResult& baseline) {
  const bool wider = baseline.route.diagnostics.closestApproachNm > 30.;
  if (!options.recoverCoastal || (wider && !options.recoverPassage) ||
      !recoverableStop(baseline) ||
      !baseline.closestPosition || baseline.route.diagnostics.closestApproachNm > 125. ||
      wr::distanceNm(request.start, request.destination) <= 105. ||
      !hasRecoveryBudget(request, baseline))
    return false;
  const auto* land = options.geography ? options.geography.get()
                                       : environment.landAndBoundaries.get();
  if (!land || !land->segmentForbidden(*baseline.closestPosition, request.destination,
                                       request.constraints.landSafetyMarginNm))
    return false;

  auto retry = request;
  chargeWork(retry, baseline);
  retry.limits.maximumGeneratedStates =
      std::min<std::uint64_t>(retry.limits.maximumGeneratedStates, 200000);
  retry.limits.maximumRetainedStates =
      std::min<std::uint64_t>(retry.limits.maximumRetainedStates, 4096);
  retry.cancellation = request.cancellation.bounded(
      Clock::now() + std::chrono::seconds{4}, {});
  const ArrivalGuide guide(retry, *land, wider ? 4. : 1.5);

  if (guide.available() && !retry.cancellation.cancelled()) {
    // Approach-side alternatives can diverge well before the closest point.
    // Re-run within the remaining budget using the reverse shoreline estimate.
    auto recoveryOptions = options;
    recoveryOptions.additionalMotions = 0;
    if (request.environment.useCurrent) recoveryOptions.refineCoastalGeography = false;
    const auto offset = std::chrono::duration<double>(Clock::now() - started).count();
    auto recovered = searchPass(retry, environment, recoveryOptions, nullptr, &guide);
    recovered.route.diagnostics.stageStopReasons.emplace_back(
        wider ? "Coastal arrival recovery used a wider reverse shoreline field"
              : "Coastal arrival recovery used a reverse shoreline field");
    if (recovered.route.validation.passed && !request.cancellation.cancelled()) {
      if (recovered.firstValidSeconds) *recovered.firstValidSeconds += offset;
      combineWork(recovered, baseline);
      baseline = std::move(recovered);
      return true;
    }
    combineWork(baseline, recovered);
  } else {
    baseline.guideQueries += guide.queries();
  }
  if (request.cancellation.cancelled()) {
    baseline.route.status = wr::RoutingStatus::Cancelled;
    baseline.route.message = "Search cancelled";
  }
  return false;
}

std::optional<CheckedPrefix> findDepartureExit(wr::RoutingRequest& retry,
    const wr::RoutingEnvironment& environment, const Options& options,
    const wr::LandAndBoundaryProvider& land, SearchResult& baseline) {
  const auto margin = retry.constraints.landSafetyMarginNm;
  std::vector<wr::GeoPoint> exits;
  for (int direction = 0; direction < 360 && !retry.cancellation.cancelled(); direction += 15) {
    const auto exit = wr::destinationPoint(retry.start, direction, 22.);
    if (!land.segmentForbidden(exit, exit, margin + 8.) &&
        !land.segmentForbidden(retry.start, exit, margin))
      exits.push_back(exit);
  }
  if (exits.empty()) return {};

  auto exitOptions = options;
  exitOptions.additionalMotions = 0;
  std::optional<CheckedPrefix> best;
  // Sample different clear departure bearings, including departures away from
  // the destination. Reuse the actual arrival state of the fastest exit.
  const auto count = std::min<std::size_t>(3, exits.size());
  for (std::size_t i = 0; i < count; ++i) {
    if (retry.cancellation.cancelled() || !retry.limits.maximumGeneratedStates ||
        !retry.limits.maximumValidationSamples)
      break;
    auto local = retry;
    const auto exit = count == 1 ? 0 : i * (exits.size() - 1) / (count - 1);
    local.destination = exits[exit];
    local.limits.maximumGeneratedStates = std::min<std::uint64_t>(30000, retry.limits.maximumGeneratedStates);
    local.limits.maximumRetainedStates = std::min<std::uint64_t>(1024, retry.limits.maximumRetainedStates);
    local.limits.maximumValidationSamples = std::min<std::uint64_t>(4096, retry.limits.maximumValidationSamples / 4);
    local.cancellation = retry.cancellation.bounded(Clock::now() + std::chrono::seconds{4}, {});
    in::MotionState arrival;
    auto result = searchPass(local, environment, exitOptions, nullptr, nullptr, nullptr, nullptr, &arrival);
    chargeWork(retry, result);
    if (result.route.validation.passed && (!best || arrival.time < best->arrival.time) &&
        result.route.legs.size() + 1 < retry.limits.maximumRetainedStates)
      best = CheckedPrefix{std::move(result.route.legs), std::move(arrival)};
    // The local destination is not the passage destination.
    result.route.diagnostics.closestApproachNm = std::numeric_limits<double>::infinity();
    result.closestPosition.reset();
    result.acceptedArrivals = 0;
    if (!result.route.validation.passed)
      result.route.diagnostics.stageStopReasons.emplace_back("Could not validate a local departure exit. " + result.route.message);
    combineWork(baseline, result);
  }
  return best;
}

void recoverConnectedWater(const wr::RoutingRequest& request,
                           const wr::RoutingEnvironment& environment,
                           const Options& options, Clock::time_point started,
                           SearchResult& baseline) {
  const auto distance = wr::distanceNm(request.start, request.destination);
  if (!options.recoverPassage || !recoverableStop(baseline) || distance <= 105. ||
      !hasRecoveryBudget(request, baseline))
    return;

  const auto* land = options.geography ? options.geography.get()
                                       : environment.landAndBoundaries.get();
  if (!land || !land->segmentForbidden(request.start, request.destination,
                                       request.constraints.landSafetyMarginNm))
    return;

  auto retry = request;
  chargeWork(retry, baseline);
  retry.limits.maximumGeneratedStates =
      std::min<std::uint64_t>(retry.limits.maximumGeneratedStates, 1800000);
  retry.cancellation = request.cancellation.bounded(
      Clock::now() + std::chrono::seconds{35}, {});
  if (distance > 3000. && options.oceanRecoveryHorizon &&
      request.environment.climatology != wr::ClimatologyFallbackPolicy::Disallow) {
    retry.limits.maximumRouteDuration = std::max(retry.limits.maximumRouteDuration,
                                                *options.oceanRecoveryHorizon);
  }
  const auto bearing = wr::initialBearingDegrees(retry.start, retry.destination);
  const bool departureBlocked = land->segmentForbidden(retry.start,
      wr::destinationPoint(retry.start, bearing, 20.), retry.constraints.landSafetyMarginNm);
  auto recoveryEnvironment = environment;
  bool widerDeparture = false;
  if (departureBlocked && options.departureRecoveryProvider) {
    if (auto provider = options.departureRecoveryProvider()) {
      recoveryEnvironment.landAndBoundaries = std::move(provider);
      widerDeparture = true;
    }
  }
  const auto prefix = departureBlocked
      ? findDepartureExit(retry, recoveryEnvironment, options, *land, baseline) : std::nullopt;
  auto guidance = retry;
  if (prefix) guidance.start = prefix->arrival.position;
  const ConnectedWaterGuide guide(guidance, *land);
  if (!guide.available() || retry.cancellation.cancelled()) {
    baseline.guideQueries += guide.queries();
    if (request.cancellation.cancelled()) {
      baseline.route.status = wr::RoutingStatus::Cancelled;
      baseline.route.message = "Search cancelled";
    }
    return;
  }

  auto recoveryOptions = options;
  recoveryOptions.additionalMotions = 0;
  const auto offset = std::chrono::duration<double>(Clock::now() - started).count();
  auto recovered = searchPass(retry, recoveryEnvironment, recoveryOptions, nullptr, nullptr, &guide,
                              prefix ? &*prefix : nullptr);
  recovered.route.diagnostics.stageStopReasons.emplace_back(
      "Coastal recovery used a connected-water distance field");
  if (prefix)
    recovered.route.diagnostics.stageStopReasons.emplace_back("Connected-water recovery replayed a checked departure exit");
  if (widerDeparture)
    recovered.route.diagnostics.stageStopReasons.emplace_back("Connected-water recovery widened the default diverted course for a blocked departure");
  if (retry.limits.maximumRouteDuration > request.limits.maximumRouteDuration)
    recovered.route.diagnostics.stageStopReasons.emplace_back("Connected-water recovery used an extended ocean search horizon");
  if (recovered.route.validation.passed && !request.cancellation.cancelled()) {
    if (recovered.firstValidSeconds) *recovered.firstValidSeconds += offset;
    combineWork(recovered, baseline);
    baseline = std::move(recovered);
  } else {
    combineWork(baseline, recovered);
  }
  if (request.cancellation.cancelled()) {
    baseline.route.status = wr::RoutingStatus::Cancelled;
    baseline.route.message = "Search cancelled";
  }
}

SearchResult recoverCoastal(const wr::RoutingRequest& request,
                            const wr::RoutingEnvironment& environment,
                            const Options& options, Clock::time_point started,
                            SearchResult result) {
  auto recoveryOptions = options;
  recoveryOptions.useBothGuides = false;
  for (const bool fine : {true, false}) {
    if (result.route.validation.passed || !hasRecoveryBudget(request, result)) break;
    // Give the fine detour field an exclusive turn. The coarse view can
    // suggest a different island-side approach if fine guidance failed.
    recoveryOptions.refineCoastalGeography = fine;
    auto retry = request;
    chargeWork(retry, result);
    if (fine)
      retry.cancellation = request.cancellation.bounded(
          Clock::now() + std::chrono::milliseconds{900}, {});
    const auto offset = std::chrono::duration<double>(Clock::now() - started).count();
    auto next = searchPass(retry, environment, recoveryOptions);
    if (next.firstValidSeconds) *next.firstValidSeconds += offset;
    next.coastalRecoveryPasses = result.coastalRecoveryPasses + 1;
    next.coastalRecoveryValidated = next.route.validation.passed;
    combineWork(next, result);
    next.route.diagnostics.stageStopReasons.emplace_back(
        fine ? "Coastal recovery used fine geographic guidance" :
               "Coastal recovery used coarse geographic guidance");
    result = std::move(next);
  }
  return result;
}

SearchResult searchBase(const wr::RoutingRequest& request,
                        const wr::RoutingEnvironment& environment,
                        const Options& options) {
  // Static geographic refinement changed the work ordering enough to lose
  // a validated strong-current control. Keep the original timed search for
  // current-enabled passages until that combination is qualified separately.
  if (request.environment.useCurrent) {
    auto currentOptions = options;
    currentOptions.refineCoastalGeography = false;
    return searchPass(request, environment, currentOptions);
  }
  const auto direct = wr::distanceNm(request.start, request.destination);
  const auto* land = options.geography ? options.geography.get() : environment.landAndBoundaries.get();
  if (!options.coastalPilot || !options.refineCoastalGeography || !land ||
      request.cancellation.cancelled() || !valid(request.start) ||
      !valid(request.destination) || direct > 105. || direct < .2 ||
      !land->segmentForbidden(request.start, request.destination,
                             request.constraints.landSafetyMarginNm))
    return searchPass(request, environment, options);

  const auto started = Clock::now();
  auto pilotRequest = request;
  pilotRequest.cancellation = request.cancellation.bounded(
      started + std::chrono::milliseconds{350}, {});
  auto pilotOptions = options;
  pilotOptions.refineCoastalGeography = false;
  pilotOptions.paceCoastalArrivals = false;
  pilotOptions.additionalMotions = 0;
  auto pilot = searchPass(pilotRequest, environment, pilotOptions);
  pilot.pilotValidated = pilot.route.validation.passed;
  if ((pilot.pilotValidated && !options.additionalMotions) ||
      request.cancellation.cancelled() ||
      (pilot.route.preflight && !pilot.route.preflight->canRoute))
    return pilot;

  auto followup = request;
  chargeWork(followup, pilot);
  auto followupOptions = options;
  const bool recovery = options.recoverCoastal && !pilot.pilotValidated;
  if (pilot.pilotValidated) {
    followup.cancellation = request.cancellation.bounded(
        Clock::now() + std::chrono::milliseconds{700}, {});
    // The pilot has already explored the coarse view. Give refinement
    // time to test a different approach while the validated route is kept.
    followupOptions.useBothGuides = false;
  } else if (recovery) {
    followup.cancellation = request.cancellation.bounded(
        started + std::chrono::milliseconds{1900}, {});
  }
  const auto offset = std::chrono::duration<double>(Clock::now() - started).count();
  auto alternative = searchPass(followup, environment, followupOptions);
  const bool usePilot = pilot.pilotValidated && (!alternative.route.validation.passed ||
      pilot.route.metrics.elapsed <= alternative.route.metrics.elapsed);
  auto& selected = usePilot ? pilot : alternative;
  const auto& other = usePilot ? alternative : pilot;
  selected.pilotValidated = pilot.pilotValidated;
  combineWork(selected, other);
  if (pilot.pilotValidated) {
    selected.firstValidSeconds = pilot.firstValidSeconds;
    selected.firstPassage = pilot.firstPassage;
  } else if (selected.firstValidSeconds) {
    *selected.firstValidSeconds += offset;
  }
  selected.route.diagnostics.stageStopReasons.emplace_back(pilot.pilotValidated ?
      "Coarse coastal pilot retained a validated arrival" : "Coastal search continued beyond coarse pilot");
  if (recovery && !selected.route.validation.passed)
    return recoverCoastal(request, environment, options, started, std::move(selected));
  return std::move(selected);
}
}  // namespace

SearchResult search(const wr::RoutingRequest& request,
                    const wr::RoutingEnvironment& environment,
                    const Options& options) {
  const auto started = Clock::now();
  auto incumbent = searchBase(request, environment, options);
  if (recoverArrival(request, environment, options, started, incumbent)) return incumbent;
  if (!incumbent.route.validation.passed) {
    recoverConnectedWater(request, environment, options, started, incumbent);
    return incumbent;
  }
  // Keep the existing policy on short passages. This experiment compares
  // additional searching on offshore-scale routes that already have an arrival.
  if (!options.exploreWeatherCorridors || !incumbent.route.validation.passed ||
      request.cancellation.cancelled() ||
      wr::distanceNm(request.start, request.destination) < 180.)
    return incumbent;

  const auto passage = incumbent.route.metrics.elapsed;
  auto followup = request;
  const auto& spent = incumbent.route.diagnostics;
  if (passage <= wr::Duration{1} ||
      spent.generatedStates >= request.limits.maximumGeneratedStates ||
      spent.validationSamples >= request.limits.maximumValidationSamples)
    return incumbent;
  followup.limits.maximumGeneratedStates -= spent.generatedStates;
  followup.limits.maximumValidationSamples -= spent.validationSamples;
  followup.limits.maximumRouteDuration = passage - wr::Duration{1};
  followup.cancellation = request.cancellation.bounded(
      Clock::now() + std::chrono::milliseconds{700}, {});
  const auto* hints = options.geography ? options.geography.get() : environment.landAndBoundaries.get();
  WeatherGuide guide(followup, environment, incumbent.route, hints);
  incumbent.weatherGuideSamples = guide.samples();
  incumbent.incumbentPassage = passage;
  if (!guide.available()) return incumbent;
  auto weatherOptions = options;
  // The guide has already selected a different approach. Spend the
  // remaining allowance on one queue with that priority.
  weatherOptions.diversifyApproaches = false;
  auto alternative = searchPass(followup, environment, weatherOptions, &guide);
  const bool improved = alternative.route.validation.passed &&
                        alternative.route.metrics.elapsed < passage;
  auto& selected = improved ? alternative : incumbent;
  const auto& other = improved ? incumbent : alternative;
  combineWork(selected, other);
  selected.firstValidSeconds = incumbent.firstValidSeconds;
  selected.firstPassage = incumbent.firstPassage;
  selected.incumbentPassage = passage;
  selected.weatherGuideSamples = guide.samples();
  selected.weatherGuideAvailable = true;
  selected.weatherGuideImproved = improved;
  selected.weatherGuideSeparationNm = guide.separationNm();
  selected.estimatedWeatherPassage = guide.estimatedPassage();
  selected.weatherSearchClosestNm = alternative.route.diagnostics.closestApproachNm;
  selected.route.diagnostics.stageStopReasons.emplace_back(improved
      ? "Forecast-guided search returned a faster validated arrival"
      : "Forecast-guided search retained the validated incumbent");
  return std::move(selected);
}
}  // namespace corridor_routing
