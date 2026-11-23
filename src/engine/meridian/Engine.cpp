// SPDX-License-Identifier: GPL-3.0-or-later
#include "engine/meridian/Engine.h"
#include "engine/meridian/StepPolicy.h"
#include "engine/meridian/StateKey.h"
#include "engine/meridian/HierarchicalGuide.h"
#include "MotionKernel.h"
#include "RoutingInternal.h"
#include "RoutingSequenceAudit.h"
#include "supercpn/weather_routing/Preview.h"
#include <algorithm>
#include <cmath>
#include <limits>
#include <map>
#include <set>
#include <sstream>
#include <tuple>

namespace meridian_routing {
namespace {
namespace in = wr::internal;
using Clock = std::chrono::steady_clock;
constexpr unsigned noParent = ~unsigned{};
struct Node {
  in::MotionState state;
  unsigned parent{noParent};
  wr::RouteLeg leg;
};
struct Pending {
  unsigned parent{};
  double heading{}, rank{}, breadth{};
  wr::Duration step{};
  bool wait{};
};
using Entry = std::pair<double, std::uint64_t>;
// All queues share one bounded pool. Removing the worst guided candidate
// erases its breadth entry too, so stale entries cannot grow without bound.
class Queue {
  unsigned maximum_;
  std::uint64_t serial_{};
  std::map<std::uint64_t, Pending> pending_;
  std::set<Entry> guided_, breadth_;
  void erase(std::uint64_t id) {
    const auto p = pending_.at(id);
    guided_.erase({p.rank, id});
    breadth_.erase({p.breadth, id});
    pending_.erase(id);
  }
public:
  explicit Queue(unsigned maximum) : maximum_(maximum) {}
  bool empty() const { return pending_.empty(); }
  void push(Pending p) {
    if (!std::isfinite(p.rank) || !std::isfinite(p.breadth)) return;
    if (pending_.size() >= maximum_) {
      auto worst = std::prev(guided_.end());
      if (worst->first <= p.rank) return;
      erase(worst->second);
    }
    const auto id = serial_++;
    pending_.emplace(id, p);
    guided_.emplace(p.rank, id);
    breadth_.emplace(p.breadth, id);
  }
  Pending pop(bool broad) {
    const auto id = (broad ? breadth_ : guided_).begin()->second;
    auto p = pending_.at(id);
    erase(id);
    return p;
  }
};
bool pointValid(wr::GeoPoint p) {
  return std::isfinite(p.latitude) && std::isfinite(p.longitude) &&
         std::abs(p.latitude) <= 85 && std::abs(p.longitude) <= 180;
}
std::vector<wr::RouteLeg> trace(const std::vector<Node>& nodes, unsigned id) {
  std::vector<wr::RouteLeg> legs;
  for (; nodes[id].parent != noParent; id = nodes[id].parent)
    legs.push_back(nodes[id].leg);
  std::reverse(legs.begin(), legs.end());
  return legs;
}
wr::RoutingResult search(const wr::RoutingRequest& request,
    const wr::RoutingEnvironment& environment, const Options& options,
    wr::Duration coastal, wr::Duration offshore, std::uint64_t allowance,
    std::vector<Node>& seed, std::optional<HierarchicalGuide>& preparedGuide) {
  wr::RoutingResult r;
  auto& d = r.diagnostics;
  const std::vector<Node>* tree = nullptr;
  unsigned closestId = 0;
  const auto stop = [&](wr::RoutingStatus status, const std::string& why) {
    r.status = status;
    r.message = why;
    if (tree) {
      const auto& sampled = (*tree)[closestId].state;
      std::ostringstream point;
      point << "Meridian closest sampled state: " << sampled.position.latitude
            << ", " << sampled.position.longitude << "; epoch "
            << sampled.time.time_since_epoch().count() << "; incoming heading "
            << sampled.incomingHeading << "; manoeuvres " << sampled.manoeuvres
            << "; risk " << sampled.risk << "; fuel used " << sampled.fuel;
      d.stageStopReasons.push_back(point.str());
    }
    if (tree && (status == wr::RoutingStatus::ResourceLimitReached ||
                 status == wr::RoutingStatus::SearchIncomplete)) {
      // Preserve an exact predecessor chain, including time, tack, fuel and
      // mode history. It is only a search seed: the complete joined route
      // must still pass independent chronological and safety validation.
      std::vector<unsigned> indices;
      for (unsigned id = closestId; id != noParent; id = (*tree)[id].parent)
        indices.push_back(id);
      std::reverse(indices.begin(), indices.end());
      std::vector<Node> prefix;
      prefix.reserve(indices.size());
      for (auto id : indices) {
        auto node = (*tree)[id];
        node.parent = prefix.empty() ? noParent : static_cast<unsigned>(prefix.size() - 1);
        prefix.push_back(std::move(node));
      }
      seed = std::move(prefix);
    }
    return r;
  };
  if (request.cancellation.cancelled())
    return stop(wr::RoutingStatus::Cancelled, "Meridian cancelled");
  if (!pointValid(request.start) || !pointValid(request.destination))
    return stop(!pointValid(request.start) ? wr::RoutingStatus::InvalidStart
                                          : wr::RoutingStatus::InvalidDestination,
                "Meridian requires finite endpoints between 85 S and 85 N");
  if (environment.landAndBoundaries &&
      (environment.landAndBoundaries->pointForbidden(request.start) ||
       environment.landAndBoundaries->pointForbidden(request.destination)))
    return stop(environment.landAndBoundaries->pointForbidden(request.start)
                    ? wr::RoutingStatus::InvalidStart : wr::RoutingStatus::InvalidDestination,
                "Forbidden route endpoint");
  r.preflight = wr::RoutingEngine{}.preflight(request, environment);
  r.warnings = r.preflight->warnings;
  if (!r.preflight->canRoute)
    return stop(in::failedPreflightStatus(*r.preflight), "Meridian weather/vessel preflight failed");
  if (environment.xtdCurrent && request.environment.useCurrent &&
      r.preflight->coverage.currentFallbackNeeded)
    return stop(wr::RoutingStatus::CurrentDataRequired,
                "Prepare current-prediction coverage before invoking Meridian");
  wr::PolarPerformanceModel fallback(request.vessel);
  const auto& performance = environment.performance ? *environment.performance : fallback;
  const auto maxNodes = std::min<std::uint64_t>(options.maximumNodes,
                                               request.limits.maximumRetainedStates);
  if (maxNodes < 2 || !allowance)
    return stop(wr::RoutingStatus::ResourceLimitReached, "Meridian has no search allowance");
  StepPolicy schedule(options.geography ? options.geography.get()
                                        : environment.landAndBoundaries.get(),
                      coastal, offshore, request.constraints.landSafetyMarginNm);
  Queue queue(options.maximumPending);
  std::vector<Node> nodes;
  nodes.reserve(maxNodes);
  std::map<StateKey, std::vector<unsigned>> labels;
  if (seed.empty()) {
    Node root;
    root.state.position = request.start;
    root.state.time = request.departure;
    if (environment.landAndBoundaries && request.constraints.landSafetyMarginNm > 0)
      root.state.departureEgressActive =
          environment.landAndBoundaries->distanceToForbiddenNm(request.start) <
          request.constraints.landSafetyMarginNm;
    nodes.push_back(std::move(root));
  } else {
    nodes = seed;
    d.stageStopReasons.push_back("Meridian refinement resumes " +
        std::to_string(nodes.size() - 1) + " physically checked prefix legs; full-route replay remains required");
  }
  const auto first = static_cast<unsigned>(nodes.size() - 1);
  closestId = first;
  tree = &nodes;
  d.closestApproachNm = wr::distanceNm(nodes[first].state.position, request.destination);
  if (request.progress)
    request.progress({wr::RoutingProgressStage::Preflight, 1, 1, 0, nodes.size(), 0,
                       d.closestApproachNm, 100});
  if (!preparedGuide || !seed.empty()) {
    // Rebuild fine local geography at the recovered frontier. The coarse
    // ocean sketch can otherwise underestimate a nearby cape's detour.
    // Only advisory geometry uses this origin; sailing/replay retain the
    // original request, departure and exact predecessor chain.
    auto guideRequest = request;
    guideRequest.start = nodes[first].state.position;
    preparedGuide.emplace(guideRequest, options.geography ? options.geography.get()
                                                        : environment.landAndBoundaries.get());
  }
  const auto& guide = *preparedGuide;
  d.stageStopReasons.push_back("Meridian full-circle heading fan; base separation " +
      std::to_string(360. / options.headings) + " degrees; configured Max Search Angle does not restrict this method");
  d.stageStopReasons.push_back("Meridian advisory geography guide: " +
      std::to_string(guide.path().size()) + " vertices; never accepted as sailing legs");
  wr::RoutingPreviewPublisher preview(request);
  std::uint64_t evaluated = 0;
  unsigned validated = 0;
  const auto horizon = request.departure + request.limits.maximumRouteDuration;
  wr::RoutingStatus lastFailure = wr::RoutingStatus::NoFeasibleRoute;
  double referenceSpeed = 0;
  for (const auto& profile : request.vessel.profiles)
    for (const auto& row : profile.rows)
      for (const auto& point : row.points)
        referenceSpeed = std::max(referenceSpeed, point.boatSpeedKnots);
  referenceSpeed = std::clamp(referenceSpeed * .65, 2., 20.);
  const auto expand = [&](unsigned id) {
    const auto& state = nodes[id].state;
    if (state.time >= horizon || request.cancellation.cancelled()) return;
    auto cap = std::min(schedule(state.position), horizon - state.time);
    // Resolve the final tacks instead of stepping past a small destination.
    const auto destinationDistance = wr::distanceNm(state.position, request.destination);
    if (destinationDistance < 3) cap = std::min(cap, wr::Duration{300});
    if (destinationDistance < 1) cap = std::min(cap, wr::Duration{120});
    const auto resolved = in::resolveEnvironment(request, environment,
                                                state.position, state.time);
    ++d.weatherSamples;
    if (resolved.failureStatus != wr::RoutingStatus::Complete) {
      lastFailure = resolved.failureStatus;
      return;
    }
    const auto& weather = resolved.snapshot;
    const auto tws = wr::vectorMagnitudeKnots(weather.wind.velocity);
    const auto bearing = wr::initialBearingDegrees(state.position, request.destination);
    const double elapsed = double((state.time - request.departure).count());
    // The weather-derived estimate orders work only. No candidate is pruned
    // by an assumed maximum boat/current speed or by an ETA lower bound.
    for (unsigned h = 0; h < options.headings + 2; ++h) {
      const auto heading = h < options.headings ? h * 360. / options.headings
          : h == options.headings ? bearing : state.incomingHeading;
      // This standalone method searches a full heading fan. The legacy
      // search cone is a pruning control, not a sailing/safety constraint.
      // All physical wind angles and every actual motion remain checked.
      const auto choices = performance.candidatesAt(state.position, state.time,
          tws, wr::trueWindAngleDegrees(weather.wind.velocity, heading),
          weather.waves, state.mode, state.modeDuration);
      double speed = 0;
      for (const auto& choice : choices)
        if (choice.valid) speed = std::max(speed, choice.speedThroughWaterKnots);
      if (speed <= .05) continue;
      const auto water = wr::speedDirectionToVector(speed, heading);
      const wr::Vector2 ground{water.eastKnots + weather.current.velocity.eastKnots,
                              water.northKnots + weather.current.velocity.northKnots};
      const auto sog = wr::vectorMagnitudeKnots(ground);
      if (sog <= .05) continue;
      for (const auto step : {cap, std::max(wr::Duration{60}, cap / 2)}) {
        if (step > horizon - state.time || d.generatedStates >= allowance) break;
        ++d.generatedStates;
        const auto point = wr::destinationPoint(state.position,
            wr::vectorDirectionToDegrees(ground), sog * step.count() / 3600.);
        if (!pointValid(point) || wr::distanceNm(request.start, point) >
            request.limits.maximumExplorationDistanceNm) continue;
        const auto remaining = guide.estimate(point);
        // Use a heading-independent vessel estimate. Extrapolating this
        // candidate's instantaneous polar speed over an ocean would wrongly
        // favour a fast heading even while it makes little local progress.
        // The weighted guide favours useful progress; periodic breadth pops
        // retain exploration. It is not an admissible ETA bound.
        const auto estimate = remaining * 3600 / referenceSpeed;
        queue.push({id, heading, elapsed + step.count() + 2 * estimate,
                    elapsed + step.count(), step, false});
      }
    }
    if (request.options.allowWaiting && d.generatedStates < allowance) {
      ++d.generatedStates;
      const auto wait = std::min(wr::Duration{1800}, horizon - state.time);
      queue.push({id, state.incomingHeading, elapsed + wait.count() +
          wr::distanceNm(state.position, request.destination) * 1200,
          elapsed + wait.count(), wait, true});
    }
  };
  expand(first);
  // A closest coastal state can be a lee-shore cul-de-sac. Reopen the last
  // ten sailed miles of the checked prefix so refinement can turn earlier.
  // Every branch retains its original predecessor/time/resource history.
  double reopenedNm = 0;
  for (unsigned id = first; id && reopenedNm < 10;) {
    reopenedNm += wr::distanceNm(nodes[id].state.position, nodes[id - 1].state.position);
    expand(--id);
    if (d.generatedStates >= allowance) break;
  }
  while (!queue.empty() && d.generatedStates <= allowance) {
    if (request.cancellation.cancelled())
      return stop(wr::RoutingStatus::Cancelled, "Meridian cancelled");
    if (nodes.size() >= maxNodes || evaluated >= allowance)
      return stop(wr::RoutingStatus::ResourceLimitReached, "Meridian retained-state or motion allowance reached");
    const auto proposal = queue.pop(++evaluated % 32 == 0);
    const auto parent = nodes[proposal.parent].state;
    std::vector<in::MotionCandidate> motions;
    if (proposal.wait) {
      if (auto wait = in::checkedWait(request, environment, parent,
                                      proposal.step, d))
        motions.push_back(std::move(*wait));
    } else {
      motions = in::checkedMotion(request, environment, performance, parent,
          proposal.heading, proposal.step, wr::Duration{300}, d, &lastFailure);
    }
    for (auto& motion : motions) {
      if (!pointValid(motion.state.position) || motion.state.time > horizon ||
          wr::distanceNm(request.start, motion.state.position) >
          request.limits.maximumExplorationDistanceNm) continue;
      const auto cap = schedule(motion.state.position);
      auto& group = labels[key(motion.state, request.departure, cap <= wr::Duration{1800})];
      // Retain two alternatives, and never treat a resource-worse label as
      // dominating one with more fuel/time or risk allowance remaining.
      bool dominated = false;
      for (auto id : group) {
        const auto& old = nodes[id].state;
        if (old.time == motion.state.time && old.fuel <= motion.state.fuel &&
            old.risk <= motion.state.risk && old.manoeuvres <= motion.state.manoeuvres &&
            old.motorDuration <= motion.state.motorDuration &&
            old.waitDuration <= motion.state.waitDuration &&
            old.modeDuration == motion.state.modeDuration)
          dominated = true;
      }
      if (dominated || group.size() >= 2) continue;
      if (nodes.size() >= maxNodes)
        return stop(wr::RoutingStatus::ResourceLimitReached, "Meridian retained-state allowance reached");
      const auto id = static_cast<unsigned>(nodes.size());
      group.push_back(id);
      nodes.push_back({std::move(motion.state), proposal.parent, std::move(motion.leg)});
      d.retainedStates = nodes.size();
      const auto nearest = wr::distanceNm(nodes[id].state.position, request.destination);
      if (nearest < d.closestApproachNm) {
        d.closestApproachNm = nearest;
        closestId = id;
      }
      if (nearest < 15 && validated < 32) {
        const auto left = horizon - nodes[id].state.time;
        auto finish = in::checkedConnection(request, environment, performance,
            nodes[id].state, std::min(wr::Duration{7200}, left), d);
        if (finish) {
          ++validated;
          auto legs = trace(nodes, id);
          legs.push_back(std::move(finish->leg));
          if (environment.landAndBoundaries)
            environment.landAndBoundaries->prepareValidationRoute(
                legs, request.constraints.landSafetyMarginNm);
          auto validation = wr::RouteValidator{}.validate(request, environment,
                                                          performance, legs, nullptr);
          d.validationSamples += validation.samples;
          if (validation.passed) {
            r.legs = std::move(legs);
            r.validation = std::move(validation);
            r.status = wr::RoutingStatus::Complete;
            d.closestApproachNm = 0;
            in::summariseRoute(r);
            r.message = "Complete (Meridian experimental)";
            r.warnings.push_back({wr::RoutingWarningCode::SearchPruned,
                "Meridian uses bounded lazy search and approximate state buckets; optimality and completeness are not established"});
            return r;
          }
          d.stageStopReasons.push_back("Meridian candidate replay: " + validation.failureReason);
          if (environment.landAndBoundaries &&
              environment.landAndBoundaries->validationFailureRequiresSearchEscalation())
            return stop(wr::RoutingStatus::ValidationFailure, validation.failureReason);
        }
      }
      expand(id);
      if (d.generatedStates >= allowance) break;
    }
    if (evaluated % 128 == 0) {
      if (request.progress)
        request.progress({wr::RoutingProgressStage::ForwardIsochrone, 1, 1,
            d.generatedStates, nodes.size(), d.landChecks, d.closestApproachNm, 100});
      preview.publish([&] {
        wr::RoutingPreview view;
        const auto id = static_cast<unsigned>(nodes.size() - 1);
        view.time = nodes[id].state.time;
        for (unsigned n = id; n != noParent && view.path.size() < 256; n = nodes[n].parent)
          view.path.push_back(nodes[n].state.position);
        std::reverse(view.path.begin(), view.path.end());
        return view;
      });
    }
    if (d.generatedStates >= allowance)
      return stop(wr::RoutingStatus::ResourceLimitReached, "Meridian proposal allowance reached; feasibility is unresolved");
  }
  if (lastFailure != wr::RoutingStatus::Complete &&
      lastFailure != wr::RoutingStatus::NoFeasibleRoute)
    return stop(lastFailure, "Meridian stopped after rejected motion or unavailable weather");
  return stop(wr::RoutingStatus::SearchIncomplete, "Meridian bounded queue exhausted; feasibility is unresolved");
}
}  // namespace

wr::RoutingResult Engine::route(const wr::RoutingRequest& request,
    const wr::RoutingEnvironment& environment, const Options& options) const {
  wr::RoutingResult result;
  if (options.headings < 8 || options.headings > 360 || options.maximumNodes < 2 ||
      options.maximumNodes > 100000 || options.maximumPending < 2 ||
      options.maximumPending > 200000 || request.options.timeStep < wr::Duration{60} ||
      (request.objective.kind != wr::ObjectiveKind::Fastest &&
       request.objective.kind != wr::ObjectiveKind::FastestUnderSafetyLimits)) {
    result.status = wr::RoutingStatus::InvalidVesselConfiguration;
    result.message = "Invalid or unsupported Meridian search options";
    return result;
  }
  const auto total = std::min(options.maximumProposals, request.limits.maximumGeneratedStates);
  std::uint64_t spent = 0;
  double closest = std::numeric_limits<double>::infinity();
  std::vector<Node> seed;
  std::optional<HierarchicalGuide> guide;
  weather_routing::RoutingSequenceAudit audit;
  std::vector<wr::RoutingAttempt> attempts;
  for (unsigned attempt = 0; attempt < 2 && spent < total; ++attempt) {
    const auto allowance = attempt ? total - spent : total - total * 2 / 5;
    const auto offshore = std::clamp(request.options.timeStep, wr::Duration{60},
                                     wr::Duration{attempt ? 3600 : 21600});
    const auto coastal = std::min(offshore, wr::Duration{attempt ? 300 : 900});
    if (options.attemptStarted) options.attemptStarted(attempt + 1, coastal, offshore, allowance);
    const auto started = Clock::now();
    auto bounded = request;
    if (request.progress) bounded.progress = [&, attempt, before = spent](auto progress) {
      progress.generatedStates += before;
      closest = std::min(closest, progress.closestApproachNm);
      progress.closestApproachNm = closest;
      progress.attempt = attempt + 1;
      progress.totalAttempts = 2;
      request.progress(progress);
    };
    try {
      result = search(bounded, environment, options, coastal, offshore, allowance, seed, guide);
    } catch (const std::bad_alloc&) {
      result = {};
      result.status = wr::RoutingStatus::ResourceLimitReached;
      result.message = "Meridian allocation failed";
    } catch (const std::exception& e) {
      result = {};
      result.status = wr::RoutingStatus::InternalError;
      result.message = e.what();
    }
    spent += result.diagnostics.generatedStates;
    closest = std::min(closest, result.diagnostics.closestApproachNm);
    std::ostringstream detail;
    detail << result.message << "\nCoastal step up to " << coastal.count()
           << " s; offshore step up to " << offshore.count()
           << " s; proposal allowance " << allowance
           << "; closest candidate " << result.diagnostics.closestApproachNm
           << " NM remaining\n" << audit.Record(result.diagnostics,
              std::chrono::duration<double>(Clock::now() - started).count());
    attempts.push_back({attempt ? "Meridian refinement" : "Meridian initial search",
                        result.status, detail.str()});
    if (request.cancellation.cancelled() || result.status == wr::RoutingStatus::Cancelled) {
      weather_routing::CancelRoutingResultKeepingDiagnostics(result);
      break;
    }
    if (result.validation.passed && !result.legs.empty()) break;
    if (result.message == "Meridian allocation failed") break;
    if (result.status != wr::RoutingStatus::ResourceLimitReached &&
        result.status != wr::RoutingStatus::SearchIncomplete &&
        result.status != wr::RoutingStatus::NoFeasibleRoute) break;
  }
  if (!total) {
    result.status = wr::RoutingStatus::ResourceLimitReached;
    result.message = "Meridian has no search allowance";
  }
  result.engineIdentity = "meridian";
  result.searchVariant = "meridian-lazy-spacetime-v9";
  result.engineAttempts = std::move(attempts);
  result.diagnostics.cumulativeGeneratedStates = spent;
  result.diagnostics.closestApproachNm = closest;
  audit.Finish(result);
  return result;
}
}  // namespace meridian_routing
