// SPDX-License-Identifier: GPL-3.0-or-later
#include "QuickCompletion.h"
#include "ValidatedRouteReporting.h"
#include "RoutingSequenceAudit.h"
#include "MotionKernel.h"
#include <algorithm>
#include <cmath>
#include <limits>
#include <map>
#include <set>
#include <tuple>

namespace weather_routing {
namespace {
namespace motion = wr::internal;
using Clock = std::chrono::steady_clock;
constexpr std::size_t noParent = std::numeric_limits<std::size_t>::max();
motion::MotionState EndState(std::span<const wr::RouteLeg> prefix,
                             const wr::RoutingRequest& request) {
  motion::MotionState s;
  // Match the kernel's moving-time ledger. Manoeuvres and stationary waits
  // must not count towards minimum motor runs or motor-time allowances.
  for (const auto& leg : prefix) {
    const auto duration = leg.endTime-leg.startTime;
    s.position=leg.end; s.time=leg.endTime;
    if (leg.stationaryWait) { s.waitDuration += duration; continue; }
    wr::Duration penalty{};
    if (leg.propulsionTransition) penalty += request.vessel.propulsion.modeChangePenalty;
    if (leg.tackTransition) penalty += request.vessel.tackPenalty;
    if (leg.gybeTransition) penalty += request.vessel.gybePenalty;
    if (s.sailPlan >= 0 && leg.sailPlan >= 0 && s.sailPlan != leg.sailPlan)
      penalty += request.vessel.sailPlanChangePenalty;
    const auto moving=std::max(wr::Duration{},duration-penalty);
    s.modeDuration=leg.propulsionMode==s.mode ? s.modeDuration+moving : moving;
    if (leg.propulsionMode != wr::PropulsionMode::Sail) s.motorDuration += moving;
    s.fuel += leg.estimatedFuelLitres;
    s.manoeuvres += leg.tackTransition+leg.gybeTransition+leg.propulsionTransition;
    s.incomingHeading=leg.courseThroughWaterDegrees;
    s.tack=leg.tack; s.mode=leg.propulsionMode;
    s.role=leg.profileRole; s.profileIdentity=leg.profileIdentity;
    s.sailPlan=leg.sailPlan; s.departureEgressActive=leg.coastalDepartureEgress;
  }
  return s;
}

struct Node {
  motion::MotionState state;
  wr::RouteLeg leg;
  std::size_t parent{noParent};
};
}

QuickCompletionResult CompleteQuickPrefixes(const wr::RoutingRequest& original,
    const wr::RoutingEnvironment& environment,
    std::span<const wr::PartialRouteArchive::Entry* const> traces,
    const QuickCompletionOptions& options) {
  QuickCompletionResult out;
  out.route.status = wr::RoutingStatus::SearchIncomplete;
  out.route.engineIdentity = "quick3";
  out.route.message = "Quick3 found no validated destination connection";
  const auto began = Clock::now();
  auto request = original;
  request.cancellation = original.cancellation.bounded(began + options.maximumTime, {});
  request.partials.reset();
  const auto done = [&] {
    const bool stopped = request.cancellation.cancelled() ||
        out.motionCalls >= options.maximumMotionCalls;
    out.allowanceReached |= stopped && !original.cancellation.cancelled();
    return stopped;
  };
  wr::PolarPerformanceModel fallback(request.vessel);
  const auto& performance = environment.performance ? *environment.performance : fallback;
  auto& diagnostics = out.route.diagnostics;
  const auto accept = [&](std::vector<wr::RouteLeg> legs) {
    if (done() || legs.empty()) return false;
    ++out.candidatesReplayed;
    auto validation = wr::RouteValidator{}.validate(request, environment,
        performance, legs, &diagnostics);
    diagnostics.validationSamples += validation.samples;
    if (!validation.passed || done()) return false;
    if (options.deliveredLegAllowed)
      for (const auto& leg : legs)
        if (done() || !options.deliveredLegAllowed(leg)) return false;
    out.route.legs = std::move(legs);
    out.route.validation = std::move(validation);
    out.route.status = wr::RoutingStatus::CompleteUsingReverseRecovery;
    out.route.solverPath = wr::SolverPath::ReverseRecovery;
    out.route.message = "Complete (Quick3 bounded destination recovery)";
    wr::summariseDeliveredRoute(out.route);
    UseValidatedSourceReporting(out.route);
    return true;
  };
  std::vector<const wr::PartialRouteArchive::Entry*> ordered(traces.begin(), traces.end());
  std::erase_if(ordered,[&](const auto* entry) {
    return !entry || !std::isfinite(entry->seed.remainingNm) ||
        entry->seed.remainingNm > options.maximumRemainingNm;
  });
  std::stable_sort(ordered.begin(), ordered.end(), [](const auto* a, const auto* b) {
    return std::tuple{a->seed.remainingNm, a->seed.time} <
           std::tuple{b->seed.remainingNm, b->seed.time};
  });
  // Preserve approach-side/time alternatives instead of admitting only the
  // single geographically nearest endpoint. Actual prefix position/time is
  // established by replay below; a proposed contour endpoint is insufficient.
  std::set<std::tuple<int, std::int64_t>> seedFamilies;
  for (const auto* entry : ordered) {
    if (done() || out.seedsTried >= options.maximumSeeds) break;
    ++out.tracesExamined;
    std::vector<wr::RouteLeg> proven;
    try {
      auto prefix = wr::ValidatePartialRoute(request, environment, entry->trace);
      diagnostics.validationSamples += prefix.validation.samples;
      if (!prefix.validation.passed || prefix.legs.empty()) {
        ++out.prefixesRejected; continue;
      }
      proven = std::move(prefix.legs);
    } catch (const std::bad_alloc&) { throw; }
      catch (const std::exception&) { ++out.prefixesRejected; continue; }
    for (std::size_t back : {0U, 2U, 5U}) {
      if (done() || out.seedsTried >= options.maximumSeeds || proven.size() <= back) break;
      const auto count = proven.size() - back;
      auto prefix = std::vector<wr::RouteLeg>(proven.begin(), proven.begin() + count);
      auto start = EndState(prefix,request);
      const auto remaining = wr::distanceNm(start.position, request.destination);
      if (remaining > options.maximumRemainingNm) continue;
      const auto family = std::tuple{
          int(wr::initialBearingDegrees(request.destination, start.position) / 30),
          start.time.time_since_epoch().count() / 1800};
      if (!seedFamilies.insert(family).second) continue;
      ++out.seedsTried;
      if (request.progress)
        request.progress({wr::RoutingProgressStage::ReverseRecovery, out.seedsTried,
            unsigned(options.maximumSeeds), out.motionCalls, 0, diagnostics.landChecks,
            remaining, 100});
      const auto latest = std::min(start.time + options.maximumTailDuration,
          request.departure + request.limits.maximumRouteDuration);
      const auto connect = [&](const motion::MotionState& state, wr::GeoPoint target)
          -> std::optional<motion::MotionCandidate> {
        if (done() || state.time >= latest) return {};
        ++out.motionCalls;
        auto targetRequest = request;
        targetRequest.destination = target;
        return motion::checkedConnection(targetRequest, environment, performance,
            state, std::min(wr::Duration{7200}, latest-state.time), diagnostics);
      };
      // Cheap destination-guided two-leg bridges. All joins are subsequently
      // replayed with the ORIGINAL destination, clearance and vessel policies.
      if (auto finish = connect(start, request.destination)) {
        auto joined = prefix; joined.push_back(finish->leg);
        if (accept(std::move(joined))) goto finished;
      }
      for (double radius : {2., 5., 10.}) {
        for (unsigned bearing = 0; bearing < 360 && !done(); bearing += 30) {
          auto point = wr::destinationPoint(request.destination, bearing, radius);
          if (auto first = connect(start, point))
            if (auto second = connect(first->state, request.destination)) {
              auto joined = prefix; joined.push_back(first->leg); joined.push_back(second->leg);
              if (accept(std::move(joined))) goto finished;
            }
        }
      }
      // Small local forward frontier complements the destination guidance
      // where a single intermediate point cannot provide a sailing approach.
      // This is a bounded completion prototype, not a backwards-time integrator.
      std::vector<Node> nodes;
      nodes.reserve(options.maximumRetainedStates);
      nodes.push_back({start, {}, noParent});
      out.peakRetainedStates=std::max(out.peakRetainedStates,nodes.size());
      std::vector<std::size_t> frontier{0};
      for (unsigned layer = 0; layer < 72 && !frontier.empty() && !done(); ++layer) {
        std::vector<std::size_t> next;
        std::set<std::tuple<int,int,int,int,std::int64_t>> occupied;
        for (auto id : frontier) {
          const auto state = nodes[id].state;
          if (state.time >= latest || done()) continue;
          if (auto finish = connect(state, request.destination)) {
            std::vector<wr::RouteLeg> tail{finish->leg};
            for (auto n = id; nodes[n].parent != noParent; n = nodes[n].parent)
              tail.push_back(nodes[n].leg);
            std::reverse(tail.begin(), tail.end());
            auto joined = prefix; joined.insert(joined.end(), tail.begin(), tail.end());
            if (accept(std::move(joined))) goto finished;
          }
          const auto step = std::min(wr::Duration{600}, latest-state.time);
          for (unsigned heading = 0; heading < 360 && !done(); heading += 15) {
            ++out.motionCalls;
            wr::RoutingStatus failure{};
            auto moves = motion::checkedMotion(request, environment, performance,
                state, heading, step, wr::Duration{60}, diagnostics, &failure);
            for (auto& move : moves) {
              if (nodes.size() >= options.maximumRetainedStates) break;
              if (wr::distanceNm(move.state.position, request.destination) >
                  options.maximumRemainingNm * 1.5) continue;
              const auto p = move.state.position;
              const auto key = std::tuple{int(std::floor(p.latitude*120)),
                  int(std::floor(wr::normalizeLongitude(p.longitude)*120)),
                  int(move.state.tack),int(move.state.mode),
                  move.state.time.time_since_epoch().count()/900};
              if (!occupied.insert(key).second) continue;
              nodes.push_back({std::move(move.state), std::move(move.leg), id});
              out.peakRetainedStates=std::max(out.peakRetainedStates,nodes.size());
              next.push_back(nodes.size()-1);
            }
          }
        }
        std::stable_sort(next.begin(), next.end(), [&](auto a, auto b) {
          const auto rank = [&](auto id) {
            return double((nodes[id].state.time-start.time).count()) +
                wr::distanceNm(nodes[id].state.position,request.destination)*600;
          };
          return rank(a) < rank(b);
        });
        if (next.size() > options.beamWidth) next.resize(options.beamWidth);
        frontier = std::move(next);
        if (nodes.size() >= options.maximumRetainedStates) break;
      }
    }
  }
finished:
  out.route.diagnostics.generatedStates=out.motionCalls;
  out.route.diagnostics.retainedStates=out.peakRetainedStates;
  out.route.message += "\nQuick3 motion attempts: " + std::to_string(out.motionCalls) +
      "; prefixes examined: " + std::to_string(out.tracesExamined) +
      "; full candidate replays: " + std::to_string(out.candidatesReplayed);
  if (original.cancellation.cancelled()) {
    CancelRoutingResultKeepingDiagnostics(out.route);
  }
  out.milliseconds = std::chrono::duration<double,std::milli>(Clock::now()-began).count();
  return out;
}
}  // namespace weather_routing
