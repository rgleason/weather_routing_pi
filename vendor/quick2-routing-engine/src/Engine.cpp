// SPDX-License-Identifier: GPL-3.0-or-later
#include "quick2_routing/Engine.h"
#include "MotionKernel.h"
#include "RoutingInternal.h"
#include "supercpn/weather_routing/Comfort.h"
#include "supercpn/weather_routing/Preview.h"
#include "supercpn/weather_routing/PartialRoutes.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <map>
#include <stdexcept>
#include <tuple>

namespace quick2_routing {
namespace {
namespace in = wr::internal;
using Clock = std::chrono::steady_clock;
constexpr std::uint32_t noParent = ~std::uint32_t{};
double ms(Clock::time_point t) {
  return std::chrono::duration<double, std::milli>(Clock::now() - t).count();
}
bool pointValid(wr::GeoPoint p) {
  return std::isfinite(p.latitude) && std::isfinite(p.longitude) &&
         std::abs(p.latitude) <= 85 && std::abs(p.longitude) <= 180;
}
// Same severity as Standard and the existing Fastest/Comfort selector.
// A baseline unit of elapsed time prevents comfort search ignoring passage
// time.
double rate(const wr::EnvironmentalSnapshot& e, double heading, bool windOnly) {
  if (!windOnly &&
      (!e.waves.available || !std::isfinite(e.waves.significantHeightMetres)))
    return std::numeric_limits<double>::quiet_NaN();
  const double from = wr::vectorDirectionToDegrees(e.wind.velocity) + 180;
  return 1 + wr::comfortSeverity(
                 wr::vectorMagnitudeKnots(e.wind.velocity), heading - from,
                 windOnly ? 0 : e.waves.significantHeightMetres);
}
struct Node {
  in::MotionState state;
  std::uint32_t parent{noParent};
  double heading{}, cost{};
  wr::Duration duration{};
  bool wait{};
};
struct Proposal {
  std::uint32_t parent{};
  double heading{}, rank{}, rate{};
  wr::Duration duration{};
  wr::GeoPoint position;
  bool attempted{};
};
// Local unwrapped Mercator grid. Geographic resolution is independent of the
// obstacle provider's representation. Time/heading/mode/lineage alternatives
// remain possible; there is deliberately no global "ever visited" mask.
struct Grid {
  double west{}, north{}, scale{}, cell{};
  explicit Grid(wr::GeoPoint start, double nm)
      : west(start.longitude),
        north(mercator(start.latitude)),
        scale(60 *
              std::max(.05, std::cos(start.latitude * std::acos(-1) / 180))),
        cell(nm) {}
  static double mercator(double lat) {
    return std::asinh(std::tan(lat * std::acos(-1) / 180)) * 180 /
           std::acos(-1);
  }
  std::pair<int, int> key(wr::GeoPoint p) const {
    return {static_cast<int>(std::floor(
                wr::normalizeLongitude(p.longitude - west) * scale / cell)),
            static_cast<int>(
                std::floor((mercator(p.latitude) - north) * scale / cell))};
  }
};
using Cell = std::tuple<int, int, int, int, int, int>;
Cell cell(const Grid& g, const in::MotionState& s) {
  const auto [x, y] = g.key(s.position);
  return {x,
          y,
          static_cast<int>(s.tack),
          static_cast<int>(s.mode),
          s.sailPlan,
          static_cast<int>(s.departureEgressActive)};
}
bool same(const in::MotionState& a, const in::MotionState& b) {
  return a.time == b.time && a.mode == b.mode && a.role == b.role &&
         a.profileIdentity == b.profileIdentity && a.tack == b.tack &&
         a.modeDuration == b.modeDuration &&
         a.motorDuration == b.motorDuration &&
         a.waitDuration == b.waitDuration &&
         a.departureEgressActive == b.departureEgressActive &&
         a.manoeuvres == b.manoeuvres && std::abs(a.risk - b.risk) < 1e-8 &&
         std::abs(wr::angularDifferenceDegrees(a.incomingHeading,
                                               b.incomingHeading)) < 1e-8 &&
         a.sailPlan == b.sailPlan &&
         wr::distanceNm(a.position, b.position) < 1e-8 &&
         std::abs(a.fuel - b.fuel) < 1e-8;
}
std::vector<wr::RouteLeg> traceback(
    const wr::RoutingRequest& request,
    const wr::RoutingEnvironment& environment,
    const wr::VesselPerformanceModel& performance,
    const std::vector<Node>& history, std::uint32_t id,
    wr::RoutingDiagnostics& diagnostics) {
  std::vector<std::uint32_t> path;
  for (auto i = id; history[i].parent != noParent; i = history[i].parent)
    path.push_back(i);
  std::reverse(path.begin(), path.end());
  std::vector<wr::RouteLeg> result;
  for (auto i : path) {
    const auto& n = history[i];
    const auto& from = history[n.parent].state;
    if (n.wait) {
      auto m =
          in::checkedWait(request, environment, from, n.duration, diagnostics);
      if (!m || !same(m->state, n.state))
        throw std::runtime_error("Wait replay mismatch");
      result.push_back(std::move(m->leg));
    } else {
      wr::RoutingStatus failure = wr::RoutingStatus::Complete;
      auto motions = in::checkedMotion(
          request, environment, performance, from, n.heading, n.duration,
          std::chrono::minutes{5}, diagnostics, &failure);
      const auto found =
          std::find_if(motions.begin(), motions.end(),
                       [&](const auto& m) { return same(m.state, n.state); });
      if (found == motions.end())
        throw std::runtime_error("Motion replay mismatch");
      result.push_back(found->leg);
    }
  }
  return result;
}
}  // namespace

Result Engine::route(const wr::RoutingRequest& request,
                     const wr::RoutingEnvironment& environment,
                     const Options& options) const {
  Result output;
  auto& r = output.route;
  auto& d = output.frontier;
  const auto completeOutput = [&]() {
    r.diagnostics.generatedStates = d.proposals;
    r.diagnostics.retainedStates = d.lineageNodes;
    return std::move(output);
  };
  wr::RoutingPreviewPublisher preview(request);
  r.engineIdentity = kEngineIdentity;
  r.searchVariant =
      options.objective == Objective::Fastest ? "" : "comfort-shared-v1";
  if (options.frontierLimit < 8 || options.frontierLimit > 8192 ||
      options.headings < 8 || options.headings > 360 ||
      !std::isfinite(options.cellNm) || options.cellNm < .01 ||
      options.cellNm > 60 || options.step < wr::Duration{60} ||
      options.step > std::chrono::hours{6} || options.maximumLayers == 0 ||
      options.maximumLayers > 4096 || options.labelsPerCell == 0 ||
      options.labelsPerCell > 8 ||
      options.maximumLineageNodes < options.frontierLimit + 1 ||
      options.maximumLineageNodes > 1000000 || options.maximumProposals == 0 ||
      options.maximumValidatedCandidates == 0 ||
      request.limits.maximumRouteDuration <= wr::Duration::zero()) {
    r.status = wr::RoutingStatus::InvalidVesselConfiguration;
    r.message = "Invalid frontier policy";
    return completeOutput();
  }
  if (request.cancellation.cancelled()) {
    r.status = wr::RoutingStatus::Cancelled;
    return completeOutput();
  }
  if (request.objective.kind != wr::ObjectiveKind::Fastest &&
      request.objective.kind != wr::ObjectiveKind::FastestUnderSafetyLimits) {
    r.status = wr::RoutingStatus::InvalidVesselConfiguration;
    r.message = "Quick2 supports deterministic time objectives";
    return completeOutput();
  }
  if (!pointValid(request.start) || !pointValid(request.destination)) {
    r.status = !pointValid(request.start)
                   ? wr::RoutingStatus::InvalidStart
                   : wr::RoutingStatus::InvalidDestination;
    r.message = "Quick2 supports finite endpoints between 85 S and 85 N";
    return completeOutput();
  }
  if (environment.landAndBoundaries) {
    const bool startForbidden =
        environment.landAndBoundaries->pointForbidden(request.start);
    if (startForbidden ||
        environment.landAndBoundaries->pointForbidden(request.destination)) {
      r.status = startForbidden ? wr::RoutingStatus::InvalidStart
                                : wr::RoutingStatus::InvalidDestination;
      r.message = "Forbidden route endpoint";
      return completeOutput();
    }
  }
  const auto preflight = wr::RoutingEngine{}.preflight(request, environment);
  r.preflight = preflight;
  r.warnings = preflight.warnings;
  if (!preflight.canRoute) {
    r.status = in::failedPreflightStatus(preflight);
    return completeOutput();
  }
  if (environment.xtdCurrent && request.environment.useCurrent &&
      preflight.coverage.currentFallbackNeeded) {
    r.status = wr::RoutingStatus::CurrentDataRequired;
    r.message = "Prepare current-prediction coverage before invoking Quick2";
    return completeOutput();
  }
  wr::PolarPerformanceModel fallback(request.vessel);
  const auto& performance =
      environment.performance ? *environment.performance : fallback;
  const std::size_t maxNodes = static_cast<std::size_t>(std::min<std::uint64_t>(
      options.maximumLineageNodes, request.limits.maximumRetainedStates));
  const auto generatedLimit =
      std::min(options.maximumProposals, request.limits.maximumGeneratedStates);
  if (maxNodes < 2 || generatedLimit == 0) {
    r.status = wr::RoutingStatus::ResourceLimitReached;
    return completeOutput();
  }
  std::vector<Node> history;
  std::vector<std::uint32_t> frontier, next;
  std::vector<Proposal> proposals;
  try {
    history.reserve(maxNodes);
    frontier.reserve(options.frontierLimit);
    next.reserve(options.frontierLimit);
    proposals.reserve(options.frontierLimit * (options.headings + 2));
    d.searchArrayBytes = maxNodes * sizeof(Node) +
                         2 * options.frontierLimit * sizeof(std::uint32_t) +
                         proposals.capacity() * sizeof(Proposal);
    Node root;
    root.state.position = request.start;
    root.state.time = request.departure;
    if (environment.landAndBoundaries &&
        request.constraints.landSafetyMarginNm > 0)
      root.state.departureEgressActive =
          environment.landAndBoundaries->distanceToForbiddenNm(request.start) <
          request.constraints.landSafetyMarginNm;
    history.push_back(root);
    frontier.push_back(0);
    d.lineageNodes = history.size();
    const Grid grid(request.start, options.cellNm);
    wr::RoutingStatus lastFailure = wr::RoutingStatus::Complete;
    for (unsigned layer = 0; layer < options.maximumLayers && !frontier.empty();
         ++layer) {
      if (request.cancellation.cancelled()) {
        r.status = wr::RoutingStatus::Cancelled;
        return completeOutput();
      }
      ++d.layers;
      d.peakFrontier =
          std::max(d.peakFrontier, static_cast<unsigned>(frontier.size()));
      std::sort(frontier.begin(), frontier.end(), [&](auto a, auto b) {
        return std::tuple{
                   history[a].cost + wr::distanceNm(history[a].state.position,
                                                    request.destination) *
                                         600,
                   a} < std::tuple{history[b].cost +
                                       wr::distanceNm(history[b].state.position,
                                                      request.destination) *
                                           600,
                                   b};
      });
      preview.publish([&] {
        wr::RoutingPreview view;
        const auto stride =
            std::max<std::size_t>(1, (frontier.size() + 255) / 256);
        const auto representative = frontier.front();
        view.time = history[representative].state.time;
        for (std::size_t i = 0; i < frontier.size(); i += stride)
          view.frontier.push_back(history[frontier[i]].state.position);
        for (auto id = representative; id != noParent && view.path.size() < 256;
             id = history[id].parent)
          view.path.push_back(history[id].state.position);
        std::reverse(view.path.begin(), view.path.end());
        return view;
      });
      double nearest = std::numeric_limits<double>::infinity();
      for (auto i : frontier)
        nearest = std::min(nearest, wr::distanceNm(history[i].state.position,
                                                   request.destination));
      r.diagnostics.closestApproachNm =
          std::min(r.diagnostics.closestApproachNm, nearest);
      if (request.partials) {
        for (auto id : frontier) {
          const auto& state = history[id].state;
          request.partials->offer(state.position, state.time, "Quick2 frontier", [&] {
            std::vector<Node> path;
            for (auto i = id; i != noParent; i = history[i].parent) {
              if ((path.size() + 1) * sizeof(Node) > request.partials->byteBudget()) return wr::PartialRouteTrace{};
              path.push_back(history[i]);
            }
            std::reverse(path.begin(), path.end());
            std::size_t bytes = sizeof(wr::PartialRouteTrace) + path.capacity() * sizeof(Node);
            for (std::size_t i = 0; i < path.size(); ++i) {
              path[i].parent = i ? static_cast<std::uint32_t>(i - 1) : noParent;
              bytes += path[i].state.profileIdentity.capacity();
            }
            return wr::PartialRouteTrace{bytes, [path = std::move(path)](
                const wr::RoutingRequest& replayRequest,
                const wr::RoutingEnvironment& replayEnvironment,
                wr::RoutingDiagnostics& diagnostics) {
              wr::PolarPerformanceModel fallback(replayRequest.vessel);
              const auto& performance = replayEnvironment.performance
                  ? *replayEnvironment.performance : fallback;
              return traceback(replayRequest, replayEnvironment, performance,
                  path, static_cast<std::uint32_t>(path.size() - 1), diagnostics);
            }};
          }, wr::PartialRoutePhase::Beam);
        }
      }
      if (request.progress)
        request.progress({wr::RoutingProgressStage::ForwardIsochrone, 1, 1,
                          d.proposals, history.size(), r.diagnostics.landChecks,
                          nearest, 100});
      const auto validationStart = Clock::now();
      unsigned finishTries = 0;
      for (auto id : frontier) {
        const auto& node = history[id];
        const auto left = request.departure +
                          request.limits.maximumRouteDuration - node.state.time;
        if (wr::distanceNm(node.state.position, request.destination) > 15 ||
            left <= wr::Duration::zero())
          continue;
        if (++finishTries > 4 ||
            d.validatedCandidates >= options.maximumValidatedCandidates)
          break;
        auto finish = in::checkedConnection(
            request, environment, performance, node.state,
            std::min(wr::Duration{7200}, left), r.diagnostics);
        if (!finish) continue;
        ++d.validatedCandidates;
        auto legs = traceback(request, environment, performance, history, id,
                              r.diagnostics);
        legs.push_back(std::move(finish->leg));
        if (environment.landAndBoundaries)
          environment.landAndBoundaries->prepareValidationRoute(
              legs, request.constraints.landSafetyMarginNm);
        auto validation = wr::RouteValidator{}.validate(
            request, environment, performance, legs, nullptr);
        r.diagnostics.validationSamples += validation.samples;
        if (validation.passed) {
          r.legs = std::move(legs);
          r.validation = std::move(validation);
          r.status = wr::RoutingStatus::Complete;
          in::summariseRoute(r);
          r.message = "Complete (Quick2)";
          r.warnings.push_back({wr::RoutingWarningCode::SearchPruned,
                                "Quick2 uses bounded frontier pruning; global "
                                "optimality is not established"});
          output.discomfortHours =
              (r.metrics.elapsed.count() +
               wr::routeDiscomfortSeconds(r.legs, options.comfortWindOnly)) /
              3600.0;
          d.validationMs += ms(validationStart);
          d.lineageNodes = history.size();
          r.diagnostics.generatedStates = d.proposals;
          r.diagnostics.retainedStates = history.size();
          return completeOutput();
        }
        r.diagnostics.stageStopReasons.push_back("Candidate replay: " +
                                                 validation.failureReason);
        if (environment.landAndBoundaries &&
            environment.landAndBoundaries
                ->validationFailureRequiresSearchEscalation()) {
          r.status = wr::RoutingStatus::ValidationFailure;
          d.validationMs += ms(validationStart);
          return completeOutput();
        }
      }
      d.validationMs += ms(validationStart);
      proposals.clear();
      next.clear();
      const auto proposalStart = Clock::now();
      wr::Duration base = options.step;
      if (layer < 2 || nearest < 15) base = std::min(base, wr::Duration{900});
      for (auto id : frontier) {
        const auto& node = history[id];
        const auto& s = node.state;
        const auto resolved =
            in::resolveEnvironment(request, environment, s.position, s.time);
        ++r.diagnostics.weatherSamples;
        if (resolved.failureStatus != wr::RoutingStatus::Complete) {
          lastFailure = resolved.failureStatus;
          continue;
        }
        const auto& e = resolved.snapshot;
        const double tws = wr::vectorMagnitudeKnots(e.wind.velocity);
        if ((request.constraints.maximumTrueWindKnots &&
             tws > *request.constraints.maximumTrueWindKnots) ||
            (request.constraints.maximumWaveHeightMetres && e.waves.available &&
             e.waves.significantHeightMetres >
                 *request.constraints.maximumWaveHeightMetres))
          continue;
        const double bearing =
            wr::initialBearingDegrees(s.position, request.destination);
        for (unsigned h = 0; h < options.headings + 2; ++h) {
          if (request.cancellation.cancelled()) {
            r.status = wr::RoutingStatus::Cancelled;
            return completeOutput();
          }
          if (++d.proposals > generatedLimit) {
            r.status = wr::RoutingStatus::ResourceLimitReached;
            r.message = "Quick2 search allowance exhausted before finding a validated route";
            return completeOutput();
          }
          const double heading = h < options.headings
                                     ? h * 360. / options.headings
                                 : h == options.headings ? bearing
                                                         : s.incomingHeading;
          if (std::abs(wr::angularDifferenceDegrees(heading, bearing)) >
              request.options.maximumSearchAngleDegrees)
            continue;
          const auto choices = performance.candidatesAt(
              s.position, s.time, tws,
              wr::trueWindAngleDegrees(e.wind.velocity, heading), e.waves,
              s.mode, s.modeDuration);
          double best = 0;
          for (const auto& choice : choices)
            if (choice.valid)
              best = std::max(best, choice.speedThroughWaterKnots);
          if (best <= .05) continue;
          const auto water = wr::speedDirectionToVector(best, heading);
          const wr::Vector2 ground{
              water.eastKnots + e.current.velocity.eastKnots,
              water.northKnots + e.current.velocity.northKnots};
          const double sog = wr::vectorMagnitudeKnots(ground);
          if (sog <= .05) continue;
          const double exposure = rate(e, heading, options.comfortWindOnly);
          if (options.objective == Objective::Comfortable &&
              !std::isfinite(exposure))
            continue;
          const double costRate =
              options.objective == Objective::Comfortable ? exposure : 1;
          const auto step = wr::Duration{std::max<std::int64_t>(
              60, static_cast<std::int64_t>(base.count() / costRate))};
          if (s.time + step >
              request.departure + request.limits.maximumRouteDuration)
            continue;
          const auto position = wr::destinationPoint(
              s.position, wr::vectorDirectionToDegrees(ground),
              sog * step.count() / 3600.);
          if (!pointValid(position) ||
              wr::distanceNm(request.start, position) >
                  request.limits.maximumExplorationDistanceNm)
            continue;
          proposals.push_back(
              {id, heading,
               node.cost + step.count() * costRate +
                   wr::distanceNm(position, request.destination) * 600,
               costRate, step, position});
        }
      }
      std::sort(proposals.begin(), proposals.end(),
                [](const auto& a, const auto& b) {
                  return std::tuple{a.rank, a.parent, a.heading} <
                         std::tuple{b.rank, b.parent, b.heading};
                });
      d.proposalMs += ms(proposalStart);
      const auto materializeStart = Clock::now();
      std::map<std::pair<int, int>, unsigned> proposalCells;
      std::map<Cell, std::vector<std::uint32_t>> exactCells;
      std::array<unsigned, 24> sectors{};
      const auto admit = [&](in::MotionCandidate motion, const Proposal& p,
                             bool wait) {
        auto& occupants = exactCells[cell(grid, motion.state)];
        if (occupants.size() >= options.labelsPerCell) {
          ++d.duplicateCells;
          return false;
        }
        for (auto id : occupants)
          if (history[id].parent == p.parent &&
              std::abs(wr::angularDifferenceDegrees(history[id].heading,
                                                    p.heading)) < 20)
            return false;
        if (history.size() >= maxNodes) return false;
        const auto id = static_cast<std::uint32_t>(history.size());
        history.push_back({std::move(motion.state), p.parent, p.heading,
                           history[p.parent].cost + p.duration.count() * p.rate,
                           p.duration, wait});
        occupants.push_back(id);
        next.push_back(id);
        d.lineageNodes = history.size();
        return true;
      };
      // Two passes reserve angular diversity before filling remaining cells.
      for (unsigned pass = 0; pass < 2 && next.size() < options.frontierLimit;
           ++pass) {
        for (auto& p : proposals) {
          if (request.cancellation.cancelled()) {
            r.status = wr::RoutingStatus::Cancelled;
            return completeOutput();
          }
          if (next.size() >= options.frontierLimit) break;
          if (p.attempted) continue;
          const unsigned sector =
              static_cast<unsigned>(
                  wr::normalizeHeading(
                      wr::initialBearingDegrees(request.start, p.position)) /
                  15) %
              24;
          if (pass == 0 &&
              sectors[sector] >= std::max(1u, options.frontierLimit / 24))
            continue;
          auto& attempts = proposalCells[grid.key(p.position)];
          if (attempts >= 4) continue;
          p.attempted = true;
          ++attempts;
          ++d.exactMotions;
          auto motions = in::checkedMotion(request, environment, performance,
                                           history[p.parent].state, p.heading,
                                           p.duration, std::chrono::minutes{5},
                                           r.diagnostics, &lastFailure);
          for (auto& motion : motions) {
            if (next.size() >= options.frontierLimit) break;
            if (admit(std::move(motion), p, false)) ++sectors[sector];
          }
        }
      }
      if (next.empty() && request.options.allowWaiting)
        for (auto id : frontier) {
          auto wait = in::checkedWait(request, environment, history[id].state,
                                      base, r.diagnostics);
          if (wait &&
              wait->state.time <=
                  request.departure + request.limits.maximumRouteDuration) {
            Proposal p{id,   history[id].heading,       0, 1,
                       base, history[id].state.position};
            if (admit(std::move(*wait), p, true)) ++r.diagnostics.waitStates;
          }
          if (next.size() >= options.frontierLimit) break;
        }
      if (next.size() >= options.frontierLimit) ++d.frontierTruncations;
      d.materializeMs += ms(materializeStart);
      frontier.swap(next);
      if (history.size() >= maxNodes) {
        r.status = wr::RoutingStatus::ResourceLimitReached;
        r.message = "Quick2 retained-state allowance exhausted before finding a validated route";
        return completeOutput();
      }
    }
    r.status = lastFailure == wr::RoutingStatus::Complete
                   ? wr::RoutingStatus::SearchIncomplete
                   : lastFailure;
    r.message = "Quick2 search did not find a validated route within its allowance";
  } catch (const std::bad_alloc&) {
    r.status = wr::RoutingStatus::ResourceLimitReached;
    r.message = "Frontier allocation failed";
  } catch (const std::exception& e) {
    r.status = wr::RoutingStatus::ValidationFailure;
    r.message = e.what();
  }
  d.lineageNodes = history.size();
  r.diagnostics.generatedStates = d.proposals;
  r.diagnostics.retainedStates = history.size();
  return completeOutput();
}
}  // namespace quick2_routing
