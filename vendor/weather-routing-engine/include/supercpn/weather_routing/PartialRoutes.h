// SPDX-License-Identifier: Apache-2.0
#pragma once
#include "Engine.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <functional>
#include <tuple>

namespace supercpn::weather_routing {
// The independent replay's positional tolerance. This is a comparison scale,
// never a relaxation of land, depth or destination acceptance.
inline constexpr double kReplayEndpointToleranceNm = 0.15;
enum class PartialRoutePhase { Other, Contour, Beam, Sector, Forward, Reverse, Frontier, Graph };

struct PartialRouteSeed {
  GeoPoint endpoint;
  TimePoint time{};
  std::string stage;
  double remainingNm{};
  PartialRoutePhase phase{PartialRoutePhase::Other};
};
struct PartialRouteTrace {
  // Includes the capacity of captured action/leg arrays and their strings.
  std::size_t bytes{};
  std::function<std::vector<RouteLeg>(const RoutingRequest&,
      const RoutingEnvironment&, RoutingDiagnostics&)> replay;
  std::optional<EnvironmentalPolicy> environmentPolicy;
};

// Compact immutable paths only. No arena references, mutable provider caches
// or captured callbacks. Each engine gets its own share of the caller's byte
// budget, so a later Professional tier cannot erase an earlier engine's paths.
class PartialRouteArchive {
 public:
  struct Entry { PartialRouteSeed seed; PartialRouteTrace trace; };
  PartialRouteArchive(GeoPoint start, GeoPoint destination, TimePoint departure,
      Duration horizon, std::size_t bytes, std::size_t maximumEntries = 16)
      : start_(start), destination_(destination), departure_(departure),
        horizon_(horizon), budget_(bytes), slots_(maximumEntries),
        directNm_(distanceNm(start, destination)) {}
  const std::vector<Entry>& entries() const { return entries_; }
  std::size_t bytes() const { return used_; }
  std::size_t byteBudget() const { return budget_; }
  std::size_t peakBytes() const { return peak_; }
  std::size_t captures() const { return captures_; }
  std::size_t rejected() const { return rejected_; }

  // Called before touching a lineage. Global proximity is anchored at the
  // closest endpoint, rather than chained through successively wider bands.
  bool wanted(const PartialRouteSeed& seed) const {
    if (!slots_ || !budget_ || !std::isfinite(seed.remainingNm) ||
        seed.remainingNm < 0 || seed.time <= departure_ ||
        seed.time > departure_ + horizon_ ||
        seed.remainingNm + kReplayEndpointToleranceNm >= directNm_) return false;
    const auto anchor = std::min(nearest_, seed.remainingNm);
    const auto family = phaseIndex(seed.phase);
    const auto worst = anchor == nearest_ ? worst_[family] : worstEligible(seed.phase, anchor);
    const bool newPhase = counts_[family] == 0;
    // A byte-limited archive can be full before its slot limit. Its measured
    // mean snapshot size avoids repeatedly copying uncompetitive long paths.
    const bool tight = entries_.size() >= slots_ || (!entries_.empty() &&
        budget_ - used_ < used_ / entries_.size());
    if (tight && (!worst || (!newPhase &&
        !(rank(seed, anchor) < rank(entries_[*worst].seed, anchor))))) return false;
    for (const auto& old : entries_) {
      // Dominance applies only at the same endpoint/time neighbourhood;
      // opposite sides of an island must not collapse into one path family.
      if (old.seed.phase == seed.phase && distanceNm(old.seed.endpoint, seed.endpoint) <= 1e-8 &&
          old.seed.remainingNm <= seed.remainingNm &&
          old.seed.time <= seed.time) return false;
    }
    return true;
  }
  template<class Factory>
  void offer(GeoPoint endpoint, TimePoint time, std::string stage,
             Factory&& freeze, PartialRoutePhase phase = PartialRoutePhase::Other) noexcept {
    try {
      PartialRouteSeed seed{endpoint, time, std::move(stage),
                            distanceNm(endpoint, destination_), phase};
      if (!wanted(seed)) return;
      auto trace = freeze();
      if (!trace.replay || trace.bytes > budget_) { ++rejected_; return; }
      // Replace dominated local snapshots; preserve time/proximity trade-offs.
      for (std::size_t i = entries_.size(); i-- > 0;) {
        const auto& old = entries_[i].seed;
        if (old.phase == phase && distanceNm(old.endpoint, endpoint) <= 1e-8 &&
            seed.remainingNm <= old.remainingNm && time <= old.time)
          erase(i);
      }
      while (!entries_.empty() && (entries_.size() >= slots_ ||
                                  trace.bytes > budget_ - used_)) {
        const auto anchor = std::min(nearest_, seed.remainingNm);
        const bool newPhase = counts_[phaseIndex(phase)] == 0;
        const auto worst = worstEligible(phase, anchor);
        if (!worst || (!newPhase &&
            !(rank(seed, anchor) < rank(entries_[*worst].seed, anchor)))) {
          ++rejected_; return;
        }
        erase(*worst);
      }
      if (trace.bytes > budget_ - used_) { ++rejected_; return; }
      entries_.push_back({std::move(seed), std::move(trace)});
      used_ += entries_.back().trace.bytes;
      peak_ = std::max(peak_, used_);
      ++captures_;
      refreshRanks();
    } catch (...) {
      // Optional capture must not fail or consume a solver's work allowance.
      ++rejected_;
    }
  }
 private:
  // Quantisation uses the replay error scale, not an arbitrary coastal
  // corridor. Within a band retain earlier arrivals before tiny distance gains.
  static std::tuple<double, TimePoint, double, double, double> rank(const PartialRouteSeed& seed, double anchor) {
    return std::tuple{std::floor((seed.remainingNm - anchor) / kReplayEndpointToleranceNm),
                      seed.time, seed.remainingNm, seed.endpoint.latitude,
                      seed.endpoint.longitude};
  }
  void refreshRanks() {
    nearest_ = std::numeric_limits<double>::infinity();
    for (const auto& entry : entries_) nearest_ = std::min(nearest_, entry.seed.remainingNm);
    counts_.fill(0);
    for (const auto& entry : entries_) ++counts_[phaseIndex(entry.seed.phase)];
    for (std::size_t i = 0; i < worst_.size(); ++i)
      worst_[i] = worstEligible(static_cast<PartialRoutePhase>(i), nearest_);
  }
  static std::size_t phaseIndex(PartialRoutePhase phase) {
    const auto i = static_cast<std::size_t>(phase);
    return i < 8 ? i : 0;
  }
  std::optional<std::size_t> worstEligible(PartialRoutePhase phase, double anchor) const {
    std::optional<std::size_t> worst;
    for (std::size_t i = 0; i < entries_.size(); ++i) {
      const auto family = entries_[i].seed.phase;
      // The incoming path may replace its own phase's last representative.
      if (family != phase && counts_[phaseIndex(family)] == 1) continue;
      if (!worst || rank(entries_[*worst].seed, anchor) < rank(entries_[i].seed, anchor)) worst = i;
    }
    return worst;
  }
  void erase(std::size_t i) {
    used_ -= entries_[i].trace.bytes;
    entries_.erase(entries_.begin() + i);
    refreshRanks();
  }
  GeoPoint start_, destination_;
  TimePoint departure_;
  Duration horizon_;
  std::size_t budget_, slots_, used_{}, peak_{}, captures_{}, rejected_{};
  double directNm_, nearest_{std::numeric_limits<double>::infinity()};
  std::array<unsigned, 8> counts_{};
  std::array<std::optional<std::size_t>, 8> worst_{};
  std::vector<Entry> entries_;
};

inline std::size_t PartialLegBytes(const std::vector<RouteLeg>& legs) {
  std::size_t bytes = sizeof(PartialRouteTrace) + legs.capacity() * sizeof(RouteLeg);
  for (const auto& leg : legs) {
    bytes += leg.profileIdentity.capacity() + leg.windSource.datasetIdentity.capacity() +
        leg.currentSource.datasetIdentity.capacity() + leg.waveSource.datasetIdentity.capacity();
    for (const auto* source : {&leg.windSource, &leg.currentSource, &leg.waveSource,
                              &leg.waves.metadata})
      bytes += source->modelIdentity.capacity() + source->fallbackReason.capacity() +
               (source == &leg.waves.metadata ? source->datasetIdentity.capacity() : 0);
  }
  return bytes;
}
inline PartialRouteTrace FreezePartialLegs(std::vector<RouteLeg> legs) {
  const auto bytes = PartialLegBytes(legs);
  return {bytes, [legs = std::move(legs)](const RoutingRequest&,
      const RoutingEnvironment&, RoutingDiagnostics&) { return legs; }};
}

// Original request and original destination are mandatory. A partial endpoint
// never receives destination-ingress privileges. The optional host gate checks
// the delivered plotting chords in addition to independent physical replay.
inline RoutingResult ValidatePartialRoute(const RoutingRequest& request,
    const RoutingEnvironment& environment, const PartialRouteTrace& trace,
    const std::function<bool(const RouteLeg&)>& host = {}) {
  RoutingResult result;
  result.status = RoutingStatus::SearchIncomplete;
  result.message = "Incomplete — destination not reached";
  if (!trace.replay || request.cancellation.cancelled()) return result;
  auto replayRequest = request;
  if (trace.environmentPolicy) replayRequest.environment = *trace.environmentPolicy;
  auto legs = trace.replay(replayRequest, environment, result.diagnostics);
  if (request.cancellation.cancelled()) return result;
  if (!legs.empty() && (legs.back().endTime <= request.departure ||
      legs.back().endTime > request.departure + request.limits.maximumRouteDuration)) {
    result.validation.failureReason = "partial route exceeds permitted route horizon";
    return result;
  }
  PolarPerformanceModel fallback(request.vessel);
  const auto& performance = environment.performance ? *environment.performance : fallback;
  result.validation = RouteValidator{}.validatePrefix(replayRequest, environment, performance, legs,
                                                       &result.diagnostics);
  if (!result.validation.passed) return result;
  for (std::size_t i = 0; i < legs.size(); ++i) {
    legs[i].coastalDepartureEgress = i < result.validation.coastalDepartureEgressLegs;
    if (request.cancellation.cancelled() || (host && !host(legs[i]))) {
      result.validation.passed = false;
      result.validation.failureReason = "partial plotting geometry rejected or cancelled";
      return result;
    }
  }
  if (distanceNm(legs.back().end, request.destination) + kReplayEndpointToleranceNm >=
      distanceNm(request.start, request.destination)) {
    result.validation.passed = false;
    result.validation.failureReason = "partial route makes no measurable progress";
    return result;
  }
  result.legs = std::move(legs);
  summariseDeliveredRoute(result);
  result.environment = result.validation.environment;
  result.sourceTransitions = result.validation.sourceTransitions;
  return result;
}
} // namespace supercpn::weather_routing
