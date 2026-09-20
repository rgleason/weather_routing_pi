// SPDX-License-Identifier: GPL-3.0-or-later
#include "Contour.h"
#include "RoutingInternal.h"
#include <array>
#include <memory>
#include <numeric>
#include <optional>
#include <stdexcept>

namespace original_routing::detail {
void Arena::checkpoint() {
  if (++operations > options.maximumGeometryOperations)
    throw Stop{wr::RoutingStatus::ResourceLimitReached, "geometry work limit"};
  if (request.cancellation.cancelled())
    throw Stop{wr::RoutingStatus::Cancelled, "cancelled"};
  if ((operations == 1 || !(operations & 63)) &&
      std::chrono::steady_clock::now() >= deadline)
    throw Stop{wr::RoutingStatus::ResourceLimitReached, "wall-clock limit"};
}
void Arena::allocation() {
  checkpoint();
  if (positions.count + skips.count + routes.count >
      request.limits.maximumRetainedStates * 4)
    throw Stop{wr::RoutingStatus::ResourceLimitReached,
               "geometry memory limit"};
}
Arena::~Arena() {
  cleaning = true;
  for (auto p : routes.objects)
    if (p) delete p;
  for (auto p : skips.objects)
    if (p) delete p;
  for (auto p : positions.objects)
    if (p) delete p;
  for (auto p : traces.objects)
    if (p) delete p;
}
Trace::Trace(Arena& a, Trace* p, wr::GeoPoint v, wr::TimePoint t)
    : arena(a), parent(p), point(v), time(t) {
  if (a.traces.count >= a.request.limits.maximumRetainedStates)
    throw Stop{wr::RoutingStatus::ResourceLimitReached,
               "retained lineage limit"};
  slot = a.traces.add(this);
  if (parent) parent->retain();
}
Trace::~Trace() { arena.traces.remove(slot); }
void Trace::release() {
  Trace* p = this;
  while (p && !--p->refs) {
    auto next = p->parent;
    delete p;
    p = next;
  }
}
Position::Position(Arena& a, double x, double y, Trace* t)
    : arena(a), lat(x), lon(y), trace(t) {
  a.allocation();
  slot = a.positions.add(this);
  if (trace) trace->retain();
  lat = 2e-11 * std::round(lat / 2e-11);
  lon = 2e-11 * std::round(lon / 2e-11);
}
Position::Position(const Position* p)
    : Position(p->arena, p->lat, p->lon, p->trace) {
  propagated = p->propagated;
  copied = true;
}
Position::~Position() {
  arena.positions.remove(slot);
  if (trace && !arena.cleaning) trace->release();
}

class GuardWeather final : public wr::WeatherProvider {
  std::shared_ptr<const wr::WeatherProvider> source;
  Arena& arena;
  std::uint64_t& calls;
  void check() const {
    arena.checkpoint();
    if (calls >= arena.options.maximumWeatherQueries)
      throw Stop{wr::RoutingStatus::ResourceLimitReached,
                 "weather query limit"};
    ++calls;
  }

public:
  GuardWeather(std::shared_ptr<const wr::WeatherProvider> s, Arena& a,
               std::uint64_t& c)
      : source(std::move(s)), arena(a), calls(c) {}
  wr::ParameterCoverage windCoverage() const override {
    return source->windCoverage();
  }
  wr::ParameterCoverage currentCoverage() const override {
    return source->currentCoverage();
  }
  wr::ParameterCoverage waveCoverage() const override {
    return source->waveCoverage();
  }
  wr::WindSample wind(wr::GeoPoint p, wr::TimePoint t) const override {
    check();
    return source->wind(p, t);
  }
  wr::CurrentSample current(wr::GeoPoint p, wr::TimePoint t) const override {
    check();
    return source->current(p, t);
  }
  wr::WaveSample waves(wr::GeoPoint p, wr::TimePoint t) const override {
    check();
    return source->waves(p, t);
  }
  std::string identity() const override { return source->identity(); }
};
class GuardPerformance final : public wr::VesselPerformanceModel {
  std::shared_ptr<const wr::VesselPerformanceModel> source;
  Arena& arena;

public:
  GuardPerformance(std::shared_ptr<const wr::VesselPerformanceModel> s,
                   Arena& a)
      : source(std::move(s)), arena(a) {}
  bool valid(std::string* p = nullptr) const override {
    return source->valid(p);
  }
  std::vector<wr::PerformanceCandidate> candidates(
      double w, double a, const wr::WaveSample& s, wr::PropulsionMode m,
      wr::Duration d) const override {
    arena.checkpoint();
    return source->candidates(w, a, s, m, d);
  }
  std::vector<wr::PerformanceCandidate> candidatesAt(
      wr::GeoPoint p, wr::TimePoint t, double w, double a,
      const wr::WaveSample& s, wr::PropulsionMode m,
      wr::Duration d) const override {
    arena.checkpoint();
    return source->candidatesAt(p, t, w, a, s, m, d);
  }
  wr::PerformanceCandidate evaluate(wr::PropulsionMode m, wr::ProfileRole r,
                                    const std::string& id, double w, double a,
                                    const wr::WaveSample& s) const override {
    arena.checkpoint();
    return source->evaluate(m, r, id, w, a, s);
  }
  wr::PerformanceCandidate evaluateAt(wr::GeoPoint p, wr::TimePoint t,
                                      wr::PropulsionMode m, wr::ProfileRole r,
                                      const std::string& id, double w, double a,
                                      const wr::WaveSample& s) const override {
    arena.checkpoint();
    return source->evaluateAt(p, t, m, r, id, w, a, s);
  }
};
class GuardClimatology final : public wr::ClimatologyProvider {
  std::shared_ptr<const wr::ClimatologyProvider> source;
  Arena& arena;
  std::uint64_t& calls;

public:
  GuardClimatology(std::shared_ptr<const wr::ClimatologyProvider> s, Arena& a,
                   std::uint64_t& c)
      : source(std::move(s)), arena(a), calls(c) {}
  wr::ParameterCoverage coverage() const override { return source->coverage(); }
  wr::WindSample wind(wr::GeoPoint p, wr::TimePoint t) const override {
    arena.checkpoint();
    if (calls >= arena.options.maximumWeatherQueries)
      throw Stop{wr::RoutingStatus::ResourceLimitReached,
                 "weather query limit"};
    ++calls;
    return source->wind(p, t);
  }
  std::string identity() const override { return source->identity(); }
};
class GuardCurrent final : public wr::CurrentPredictionProvider {
  std::shared_ptr<wr::CurrentPredictionProvider> source;
  Arena& arena;
  std::uint64_t& calls;

public:
  GuardCurrent(std::shared_ptr<wr::CurrentPredictionProvider> s, Arena& a,
               std::uint64_t& c)
      : source(std::move(s)), arena(a), calls(c) {}
  wr::ParameterCoverage coverage() const override { return source->coverage(); }
  wr::CoverageResult ensureCoverage(const wr::GeoEnvelope& e, wr::TimePoint a,
                                    wr::TimePoint b, wr::Duration d,
                                    const wr::CancellationToken& t) override {
    arena.checkpoint();
    return source->ensureCoverage(e, a, b, d, t);
  }
  wr::CurrentSample sample(wr::GeoPoint p, wr::TimePoint t) const override {
    arena.checkpoint();
    if (calls >= arena.options.maximumWeatherQueries)
      throw Stop{wr::RoutingStatus::ResourceLimitReached,
                 "weather query limit"};
    ++calls;
    return source->sample(p, t);
  }
  std::string identity() const override { return source->identity(); }
};

struct Motion {
  wr::RouteLeg leg;
  double angle{};
  unsigned profile{};
  wr::Duration modeDuration{}, motorTime{};
  double fuel{};
};
struct State {
  wr::GeoPoint point;
  wr::TimePoint time;
  double angle{};
  unsigned profile{};
  bool prior{};
  wr::Duration modeDuration{}, motorTime{};
  double fuel{};
};
class Search {
public:
  const wr::RoutingRequest& request;
  const Options& options;
  Arena arena;
  wr::RoutingEnvironment environment;
  wr::RoutingResult result;
  std::vector<wr::PerformanceCandidate> profiles;
  std::vector<double> angles;
  wr::RoutingStatus missingStatus{wr::RoutingStatus::NoFeasibleRoute};
  unsigned candidatesTried{};
  double largestStepNm{2};
  Search(const wr::RoutingRequest& r, const wr::RoutingEnvironment& e,
         const Options& o)
      : request(r), options(o), arena(r, o), environment(e) {
    if (e.grib)
      environment.grib = std::make_shared<GuardWeather>(
          e.grib, arena, result.diagnostics.weatherSamples);
    if (e.climatology)
      environment.climatology = std::make_shared<GuardClimatology>(
          e.climatology, arena, result.diagnostics.weatherSamples);
    if (e.xtdCurrent)
      environment.xtdCurrent = std::make_shared<GuardCurrent>(
          e.xtdCurrent, arena, result.diagnostics.weatherSamples);
    environment.performance =
        std::make_shared<GuardPerformance>(e.performance, arena);
    result.solverPath = wr::SolverPath::AdaptiveIsochrone;
    result.diagnostics.stagesAttempted.push_back(result.solverPath);
    const double lo = r.constraints.minimumTrueWindAngleDegrees,
                 hi = r.constraints.maximumTrueWindAngleDegrees;
    for (double a = lo; a <= hi + 1e-8; a += r.options.headingStepDegrees)
      angles.push_back(a);
    if (angles.empty() || angles.back() < hi - 1e-8) angles.push_back(hi);
    const auto positive = angles;
    for (double a : positive)
      if (a > 0 && a < 180) angles.push_back(-a);
    std::sort(angles.begin(), angles.end());
  }
  double longitude(double x) const {
    return request.start.longitude +
           wr::normalizeLongitude(x - request.start.longitude);
  }
  State state(const Trace* t) const {
    return {t->point,
            t->time,
            t->angle,
            t->profile,
            t->parent != nullptr,
            t->modeDuration,
            t->motorTime,
            t->fuel};
  }
  State after(const Motion& m) const {
    return {m.leg.end, m.leg.endTime,  m.angle,     m.profile,
            true,      m.modeDuration, m.motorTime, m.fuel};
  }
  std::optional<wr::EnvironmentalSnapshot> weather(wr::GeoPoint p,
                                                   wr::TimePoint t) {
    arena.checkpoint();
    auto r = wr::internal::resolveEnvironment(request, environment, p, t);
    for (const auto& warning : r.warnings)
      if (std::none_of(result.warnings.begin(), result.warnings.end(),
                       [&](const auto& w) { return w.code == warning.code; }))
        result.warnings.push_back(warning);
    if (r.failureStatus != wr::RoutingStatus::Complete) {
      missingStatus = r.failureStatus;
      return {};
    }
    const auto& v = r.snapshot;
    if (!std::isfinite(v.wind.velocity.eastKnots) ||
        !std::isfinite(v.wind.velocity.northKnots) ||
        !std::isfinite(v.current.velocity.eastKnots) ||
        !std::isfinite(v.current.velocity.northKnots) ||
        (v.waves.available &&
         (!std::isfinite(v.waves.significantHeightMetres) ||
          v.waves.significantHeightMetres < 0)))
      return {};
    if (std::abs(p.latitude) > request.constraints.maximumLatitudeDegrees)
      return {};
    if (request.constraints.maximumTrueWindKnots &&
        wr::vectorMagnitudeKnots(v.wind.velocity) >
            *request.constraints.maximumTrueWindKnots)
      return {};
    if (request.constraints.maximumWaveHeightMetres && v.waves.available &&
        v.waves.significantHeightMetres >
            *request.constraints.maximumWaveHeightMetres)
      return {};
    return v;
  }
  unsigned profile(const wr::PerformanceCandidate& c) {
    for (unsigned n = 0; n < profiles.size(); ++n) {
      const auto& p = profiles[n];
      if (p.mode == c.mode && p.role == c.role &&
          p.profileIdentity == c.profileIdentity && p.sailPlan == c.sailPlan)
        return n;
    }
    if (profiles.size() >= 256)
      throw Stop{wr::RoutingStatus::ResourceLimitReached,
                 "performance profile limit"};
    profiles.push_back(c);
    return profiles.size() - 1;
  }
  std::optional<wr::PerformanceCandidate> performance(
      wr::GeoPoint p, wr::TimePoint t, double heading,
      const wr::EnvironmentalSnapshot& e, unsigned index) {
    double a = wr::trueWindAngleDegrees(e.wind.velocity, heading);
    if (a + 1e-8 < request.constraints.minimumTrueWindAngleDegrees ||
        a - 1e-8 > request.constraints.maximumTrueWindAngleDegrees)
      return {};
    auto& id = profiles[index];
    auto c = environment.performance->evaluateAt(
        p, t, id.mode, id.role, id.profileIdentity,
        wr::vectorMagnitudeKnots(e.wind.velocity), a, e.waves);
    if (!c.valid || !std::isfinite(c.speedThroughWaterKnots) ||
        c.speedThroughWaterKnots <= .05 ||
        !std::isfinite(c.fuelLitresPerHour) || c.fuelLitresPerHour < 0)
      return {};
    auto water = wr::speedDirectionToVector(c.speedThroughWaterKnots, heading);
    if (request.constraints.maximumApparentWindKnots &&
        wr::vectorMagnitudeKnots(
            {e.wind.velocity.eastKnots - water.eastKnots,
             e.wind.velocity.northKnots - water.northKnots}) >
            *request.constraints.maximumApparentWindKnots)
      return {};
    if (request.constraints.maximumOpposingWindCurrent &&
        -(e.wind.velocity.eastKnots * e.current.velocity.eastKnots +
          e.wind.velocity.northKnots * e.current.velocity.northKnots) >
            *request.constraints.maximumOpposingWindCurrent)
      return {};
    if (request.constraints.minimumDepthMetres) {
      if (!environment.landAndBoundaries) return {};
      auto depth = environment.landAndBoundaries->depthMetres(p);
      if (!depth || std::isnan(*depth) ||
          *depth < *request.constraints.minimumDepthMetres)
        return {};
    }
    return c;
  }
  std::optional<unsigned> select(const State& s, double heading,
                                 const wr::EnvironmentalSnapshot& e) {
    double a = wr::trueWindAngleDegrees(e.wind.velocity, heading);
    if (a + 1e-8 < request.constraints.minimumTrueWindAngleDegrees ||
        a - 1e-8 > request.constraints.maximumTrueWindAngleDegrees)
      return {};
    auto cs = environment.performance->candidatesAt(
        s.point, s.time, wr::vectorMagnitudeKnots(e.wind.velocity), a, e.waves,
        s.prior ? profiles[s.profile].mode : wr::PropulsionMode::Sail,
        s.modeDuration);
    std::optional<wr::PerformanceCandidate> best;
    for (auto& c : cs)
      if (c.valid && std::isfinite(c.speedThroughWaterKnots) &&
          c.speedThroughWaterKnots > .05 &&
          ((c.mode == wr::PropulsionMode::Sail &&
            request.vessel.propulsion.allowSailing) ||
           (c.mode == wr::PropulsionMode::Motor &&
            request.vessel.propulsion.allowMotor) ||
           (c.mode == wr::PropulsionMode::MotorSail &&
            request.vessel.propulsion.allowMotorSailing)))
        if (!best || c.speedThroughWaterKnots > best->speedThroughWaterKnots)
          best = c;
    if (!best) return {};
    return profile(*best);
  }
  bool land(wr::GeoPoint a, wr::GeoPoint b, wr::TimePoint t) {
    arena.checkpoint();
    if (!environment.landAndBoundaries) return false;
    ++result.diagnostics.landChecks;
    const bool forbidden = environment.landAndBoundaries->segmentForbiddenAt(
        a, b, t, request.constraints.landSafetyMarginNm);
    if (forbidden) ++result.diagnostics.landRejections;
    return forbidden;
  }
  // Midpoint integration is performed only for candidate routes. Search uses
  // the catalogue's inexpensive Euler front expansion. The final validator
  // independently repeats these explicitly timed, fixed-heading sailed legs.
  std::optional<Motion> motion(
      const State& s, double heading, wr::Duration duration, unsigned id,
      bool precise, bool checkLand = true,
      const wr::EnvironmentalSnapshot* cached = nullptr) {
    if (duration <= wr::Duration::zero() ||
        s.time + duration >
            request.departure + request.limits.maximumRouteDuration)
      return {};
    std::optional<wr::EnvironmentalSnapshot> sampled;
    if (!cached) {
      sampled = weather(s.point, s.time);
      if (!sampled) return {};
      cached = &*sampled;
    }
    const auto initial = cached;
    Motion m;
    m.profile = id;
    auto& leg = m.leg;
    const auto& c = profiles[id];
    leg.start = s.point;
    leg.startTime = s.time;
    leg.endTime = s.time + duration;
    leg.headingDegrees = leg.courseThroughWaterDegrees =
        wr::normalizeHeading(heading);
    leg.propulsionMode = c.mode;
    leg.profileRole = c.role;
    leg.profileIdentity = c.profileIdentity;
    leg.sailPlan = c.sailPlan;
    double from = wr::normalizeHeading(
        wr::vectorDirectionToDegrees(initial->wind.velocity) + 180);
    m.angle = std::remainder(heading - from, 360.);
    leg.tack = m.angle < 0 ? wr::Tack::Port : wr::Tack::Starboard;
    wr::Duration penalty{};
    if (s.prior) {
      const auto& old = profiles[s.profile];
      if (old.mode != c.mode && old.mode != wr::PropulsionMode::Sail &&
          s.modeDuration < request.vessel.propulsion.minimumMotorRun)
        return {};
      if (old.mode != c.mode) {
        leg.propulsionTransition = true;
        penalty += request.vessel.propulsion.modeChangePenalty;
      }
      if (old.sailPlan >= 0 && c.sailPlan >= 0 && old.sailPlan != c.sailPlan)
        penalty += request.vessel.sailPlanChangePenalty;
      if (c.mode == wr::PropulsionMode::Sail &&
          old.mode == wr::PropulsionMode::Sail && s.angle * m.angle < 0) {
        if (std::abs(s.angle - m.angle) < 180) {
          leg.tackTransition = true;
          penalty += request.vessel.tackPenalty;
        } else {
          leg.gybeTransition = true;
          penalty += request.vessel.gybePenalty;
        }
      }
    }
    if (penalty >= duration) return {};
    m.modeDuration = duration + (s.prior && profiles[s.profile].mode == c.mode
                                     ? s.modeDuration
                                     : wr::Duration{});
    m.motorTime =
        s.motorTime +
        (c.mode != wr::PropulsionMode::Sail ? duration : wr::Duration{});
    if (request.vessel.propulsion.maximumMotorTime &&
        m.motorTime > *request.vessel.propulsion.maximumMotorTime)
      return {};
    wr::GeoPoint p = s.point;
    wr::Duration elapsed{};
    const auto moving = duration - penalty;
    const auto maxSlice = precise ? options.replaySlice : moving;
    while (elapsed < moving) {
      arena.checkpoint();
      const auto slice = std::min(maxSlice, moving - elapsed);
      const auto t = s.time + penalty + elapsed;
      std::optional<wr::EnvironmentalSnapshot> e = *initial;
      if (elapsed != wr::Duration::zero() || penalty != wr::Duration::zero())
        e = weather(p, t);
      if (!e) return {};
      auto v = performance(p, t, heading, *e, id);
      if (!v) return {};
      auto water =
          wr::speedDirectionToVector(v->speedThroughWaterKnots, heading);
      wr::Vector2 ground{water.eastKnots + e->current.velocity.eastKnots,
                         water.northKnots + e->current.velocity.northKnots};
      if (precise) {
        const auto mid = wr::destinationPoint(
            p, wr::vectorDirectionToDegrees(ground),
            wr::vectorMagnitudeKnots(ground) * slice.count() / 7200.);
        e = weather(mid, t + wr::Duration(slice.count() / 2));
        if (!e) return {};
        v = performance(mid, t + wr::Duration(slice.count() / 2), heading, *e,
                        id);
        if (!v) return {};
        water = wr::speedDirectionToVector(v->speedThroughWaterKnots, heading);
        ground = {water.eastKnots + e->current.velocity.eastKnots,
                  water.northKnots + e->current.velocity.northKnots};
      }
      if (wr::vectorMagnitudeKnots(ground) <= .05) return {};
      auto next = wr::destinationPoint(
          p, wr::vectorDirectionToDegrees(ground),
          wr::vectorMagnitudeKnots(ground) * slice.count() / 3600.);
      if (std::abs(next.latitude) > request.constraints.maximumLatitudeDegrees)
        return {};
      if (wr::distanceNm(request.start, next) >
          request.limits.maximumExplorationDistanceNm)
        return {};
      if (checkLand && land(p, next, t)) return {};
      if (!precise && !weather(next, s.time + duration))
        return {};  // never extend missing wind with a long Euler step
      leg.speedThroughWaterKnots = v->speedThroughWaterKnots;
      leg.wind = e->wind.velocity;
      leg.windSource = e->wind.metadata;
      leg.current = e->current.velocity;
      leg.currentSource = e->current.metadata;
      leg.waves = e->waves;
      leg.waveSource = e->waves.metadata;
      leg.trueWindSpeedKnots = wr::vectorMagnitudeKnots(e->wind.velocity);
      leg.trueWindAngleDegrees =
          wr::trueWindAngleDegrees(e->wind.velocity, heading);
      leg.estimatedFuelLitres += v->fuelLitresPerHour * slice.count() / 3600.;
      p = next;
      elapsed += slice;
    }
    if (precise && !weather(p, s.time + duration)) return {};
    if (checkLand && precise && land(s.point, p, s.time))
      return {};  // exported chord as well as integrated path
    leg.end = p;
    leg.courseOverGroundDegrees = wr::initialBearingDegrees(s.point, p);
    leg.speedOverGroundKnots =
        wr::distanceNm(s.point, p) * 3600. / duration.count();
    m.fuel = s.fuel + leg.estimatedFuelLitres;
    if (request.vessel.propulsion.maximumFuelLitres &&
        m.fuel > *request.vessel.propulsion.maximumFuelLitres)
      return {};
    if (precise) leg.integrationMaximumSlice = maxSlice;
    return m;
  }
  static std::array<double, 2> offset(wr::GeoPoint p, wr::GeoPoint target) {
    const auto v = wr::speedDirectionToVector(
        wr::distanceNm(p, target), wr::initialBearingDegrees(p, target));
    return {v.eastKnots, v.northKnots};
  }
  // Solve both heading and travel time. Signed angular errors work identically
  // east/west and across the dateline; every trial uses its updated heading.
  std::optional<Motion> direct(const State& s, wr::GeoPoint target, double hint,
                               wr::Duration allowance) {
    double distance = wr::distanceNm(s.point, target);
    if (distance < .00001) return {};
    auto e = weather(s.point, s.time);
    if (!e) return {};
    double heading = wr::initialBearingDegrees(s.point, target);
    auto id = select(s, heading, *e);
    if (!id) return {};
    auto perf = performance(s.point, s.time, heading, *e, *id);
    if (!perf) return {};
    double seconds =
        hint > 0 ? hint
                 : distance * 3600 / std::max(.1, perf->speedThroughWaterKnots);
    seconds = std::clamp(seconds, 1., double(allowance.count()));
    for (int i = 0; i < 10; ++i) {
      auto m = motion(s, heading, wr::Duration(std::llround(seconds)), *id,
                      true, false);
      if (!m) return {};
      auto err = offset(m->leg.end, target);
      if (std::hypot(err[0], err[1]) < .002) {
        auto checked =
            motion(s, heading, m->leg.endTime - s.time, *id, true, true);
        if (!checked || land(checked->leg.end, target, checked->leg.endTime))
          return {};
        checked->leg.end = target;
        return checked;
      }
      auto mh =
          motion(s, heading + .1, m->leg.endTime - s.time, *id, true, false);
      if (!mh)
        mh = motion(s, heading - .1, m->leg.endTime - s.time, *id, true, false);
      if (!mh) return {};
      double dh =
          std::remainder(mh->leg.courseThroughWaterDegrees - heading, 360.);
      const auto delta = offset(m->leg.end, mh->leg.end);
      // Along-course time derivative from the last integrated ground velocity.
      auto water =
          wr::speedDirectionToVector(m->leg.speedThroughWaterKnots, heading);
      const double tx = (water.eastKnots + m->leg.current.eastKnots) / 3600.,
                   ty = (water.northKnots + m->leg.current.northKnots) / 3600.;
      double hx = delta[0] / dh, hy = delta[1] / dh, det = hx * ty - hy * tx;
      if (std::abs(det) < 1e-12) return {};
      double angleUpdate = (err[0] * ty - err[1] * tx) / det,
             timeUpdate = (hx * err[1] - hy * err[0]) / det;
      heading += std::clamp(angleUpdate, -30., 30.);
      seconds += std::clamp(timeUpdate, -seconds * .5, seconds);
      if (seconds < 1 || seconds > allowance.count()) return {};
    }
    return {};
  }
  // An explicit two-leg tack/gybe, never an optimized-speed straight chord.
  // The durations are solved against actual integrated endpoints, then each
  // sailed segment and its land margin is checked before independent replay.
  std::optional<std::vector<Motion>> dogleg(const State& s, wr::GeoPoint target,
                                            wr::Duration allowance) {
    auto e = weather(s.point, s.time);
    if (!e) return {};
    const double from = wr::normalizeHeading(
                     wr::vectorDirectionToDegrees(e->wind.velocity) + 180),
                 bearing = wr::initialBearingDegrees(s.point, target);
    double a = std::abs(std::remainder(bearing - from, 360.));
    const auto targetOffset = offset(s.point, target);
    for (double padding : {1., 5., 15.}) {
      const double angle =
          a < 90 ? request.constraints.minimumTrueWindAngleDegrees + padding
                 : request.constraints.maximumTrueWindAngleDegrees - padding;
      if (angle <= request.constraints.minimumTrueWindAngleDegrees ||
          angle >= request.constraints.maximumTrueWindAngleDegrees)
        continue;
      for (int side : {1, -1}) {
        const double h1 = from + side * angle, h2 = from - side * angle;
        auto id1 = select(s, h1, *e), id2 = select(s, h2, *e);
        if (!id1 || !id2) continue;
        auto v1 = performance(s.point, s.time, h1, *e, *id1),
             v2 = performance(s.point, s.time, h2, *e, *id2);
        if (!v1 || !v2) continue;
        auto g1 = wr::speedDirectionToVector(v1->speedThroughWaterKnots, h1),
             g2 = wr::speedDirectionToVector(v2->speedThroughWaterKnots, h2);
        g1.eastKnots += e->current.velocity.eastKnots;
        g1.northKnots += e->current.velocity.northKnots;
        g2.eastKnots += e->current.velocity.eastKnots;
        g2.northKnots += e->current.velocity.northKnots;
        double determinant =
            g1.eastKnots * g2.northKnots - g2.eastKnots * g1.northKnots;
        if (std::abs(determinant) < 1e-9) continue;
        double t1 =
            3600 *
            (targetOffset[0] * g2.northKnots - targetOffset[1] * g2.eastKnots) /
            determinant;
        double t2 =
            3600 *
            (g1.eastKnots * targetOffset[1] - g1.northKnots * targetOffset[0]) /
            determinant;
        t2 += a < 90 ? request.vessel.tackPenalty.count()
                     : request.vessel.gybePenalty.count();
        for (int iter = 0; iter < 8; ++iter) {
          if (t1 < 1 || t2 < 1 || t1 + t2 > allowance.count()) break;
          auto m1 =
              motion(s, h1, wr::Duration(std::llround(t1)), *id1, true, false);
          if (!m1) break;
          auto m2 = motion(after(*m1), h2, wr::Duration(std::llround(t2)), *id2,
                           true, false);
          if (!m2) break;
          auto err = offset(m2->leg.end, target);
          if (std::hypot(err[0], err[1]) < .002) {
            m1 = motion(s, h1, m1->leg.endTime - s.time, *id1, true, true);
            if (!m1) break;
            m2 = motion(after(*m1), h2, m2->leg.endTime - m2->leg.startTime,
                        *id2, true, true);
            if (!m2 || land(m2->leg.end, target, m2->leg.endTime)) break;
            m2->leg.end = target;
            return std::vector<Motion>{*m1, *m2};
          }
          // Two numerical columns include position/time-varying wind/current.
          auto u = motion(s, h1, wr::Duration(std::llround(t1) + 10), *id1,
                          true, false);
          if (!u) break;
          auto u2 = motion(after(*u), h2, wr::Duration(std::llround(t2)), *id2,
                           true, false);
          auto v = motion(after(*m1), h2, wr::Duration(std::llround(t2) + 10),
                          *id2, true, false);
          if (!u2 || !v) break;
          auto x = offset(m2->leg.end, u2->leg.end),
               y = offset(m2->leg.end, v->leg.end);
          double det = x[0] * y[1] - y[0] * x[1];
          if (std::abs(det) < 1e-12) break;
          t1 += 10 * (err[0] * y[1] - err[1] * y[0]) / det;
          t2 += 10 * (x[0] * err[1] - x[1] * err[0]) / det;
        }
      }
    }
    return {};
  }
  std::optional<std::vector<Motion>> connect(const State& s,
                                             wr::GeoPoint target, double hint,
                                             wr::Duration allowance) {
    if (auto leg = direct(s, target, hint, allowance))
      return std::vector<Motion>{*leg};
    return dogleg(s, target, allowance);
  }
  bool candidate(Trace* end) {
    if (candidatesTried >= options.maximumValidatedCandidates) return false;
    ++candidatesTried;
    std::vector<Trace*> chain;
    for (auto p = end; p && p->parent; p = p->parent) chain.push_back(p);
    std::reverse(chain.begin(), chain.end());
    State s{request.start, request.departure};
    std::vector<wr::RouteLeg> legs;
    for (auto p : chain) {
      const auto hint = (p->time - p->parent->time).count();
      auto repaired =
          connect(s, p->point, hint,
                  wr::Duration(std::max<std::int64_t>(1800, hint * 2 + 1200)));
      if (!repaired) {
        if (result.diagnostics.stageStopReasons.size() < 8)
          result.diagnostics.stageStopReasons.push_back(
              "prefix repair failed at leg " + std::to_string(legs.size()));
        return false;
      }
      for (auto& m : *repaired) {
        legs.push_back(m.leg);
        s = after(m);
      }
    }
    auto terminal =
        connect(s, request.destination, 0,
                std::max(request.options.timeStep * 2, wr::Duration(1800)));
    if (!terminal) {
      if (result.diagnostics.stageStopReasons.size() < 8)
        result.diagnostics.stageStopReasons.push_back(
            "terminal connection failed");
      return false;
    }
    for (auto& m : *terminal) legs.push_back(m.leg);
    arena.checkpoint();
    auto validation = wr::RouteValidator{}.validate(request, environment,
                                                    *environment.performance,
                                                    legs, &result.diagnostics);
    result.diagnostics.validationSamples += validation.samples;
    if (!validation.passed) {
      if (result.diagnostics.stageStopReasons.size() < 8)
        result.diagnostics.stageStopReasons.push_back(validation.failureReason);
      return false;
    }
    result.legs = std::move(legs);
    result.validation = std::move(validation);
    result.environment = result.validation.environment;
    result.sourceTransitions = result.validation.sourceTransitions;
    result.metrics.elapsed = result.legs.back().endTime - request.departure;
    for (auto& leg : result.legs) {
      result.metrics.distanceNm += wr::distanceNm(leg.start, leg.end);
      result.metrics.tackCount += leg.tackTransition;
      result.metrics.gybeCount += leg.gybeTransition;
      auto duration = leg.endTime - leg.startTime;
      if (leg.propulsionMode == wr::PropulsionMode::Sail)
        result.metrics.sailingTime += duration;
      else if (leg.propulsionMode == wr::PropulsionMode::Motor)
        result.metrics.motorOnlyTime += duration;
      else
        result.metrics.motorSailingTime += duration;
      result.metrics.estimatedFuelLitres += leg.estimatedFuelLitres;
      result.metrics.propulsionTransitions += leg.propulsionTransition;
      result.metrics.maximumWindKnots =
          std::max(result.metrics.maximumWindKnots, leg.trueWindSpeedKnots);
      if (leg.waves.available)
        result.metrics.maximumWaveHeightMetres =
            std::max(result.metrics.maximumWaveHeightMetres,
                     leg.waves.significantHeightMetres);
    }
    result.warnings.clear();
    if (result.environment.climatologyWindDuration > wr::Duration{})
      result.warnings.push_back({wr::RoutingWarningCode::ClimatologyWindUsed,
                                 "Route uses authorised climatological wind"});
    if (request.environment.useCurrent &&
        result.environment.currentAssumedZeroDuration > wr::Duration{})
      result.warnings.push_back(
          {wr::RoutingWarningCode::CurrentAssumedZero,
           "Route uses explicitly permitted zero current"});
    if (request.constraints.maximumWaveHeightMetres &&
        result.environment.missingWaveDuration > wr::Duration{})
      result.warnings.push_back({wr::RoutingWarningCode::WaveDataMissing,
                                 "Wave limit could not be checked where "
                                 "missing wave data was explicitly waived"});
    result.status = wr::RoutingStatus::Complete;
    result.diagnostics.closestApproachNm = 0;
    result.diagnostics.completedEffortPercent = 100;
    result.diagnostics.cumulativeGeneratedStates =
        result.diagnostics.generatedStates;
    result.message =
        "Original contour route with explicit arrival and independent "
        "chronological validation";
    return true;
  }
  bool arrivals(const IsoRouteList& routes) {
    std::vector<std::pair<double, Trace*>> nearest;
    for (auto route : routes) {
      Position* p = route->skippoints->point;
      do {
        arena.checkpoint();
        if (!p->propagated && p->trace) {
          double d = wr::distanceNm(p->trace->point, request.destination);
          result.diagnostics.closestApproachNm =
              std::min(result.diagnostics.closestApproachNm, d);
          if (d <= largestStepNm * 1.25) nearest.emplace_back(d, p->trace);
        }
        p = p->next;
      } while (p != route->skippoints->point);
    }
    std::sort(nearest.begin(), nearest.end(),
              [](auto a, auto b) { return a.first < b.first; });
    for (std::size_t n = 0; n < std::min<std::size_t>(nearest.size(), 8); ++n) {
      if (candidatesTried >= options.maximumValidatedCandidates) break;
      if (candidate(nearest[n].second)) return true;
    }
    return false;
  }
  bool propagate(Position* p, IsoRouteList& output, wr::Duration dt) {
    if (p->propagated) return false;
    p->propagated = true;
    const auto s = state(p->trace);
    auto e = weather(s.point, s.time);
    if (!e) return false;
    const double from = wr::normalizeHeading(
        wr::vectorDirectionToDegrees(e->wind.velocity) + 180);
    Position* points = nullptr;
    unsigned count = 0;
    for (double a : angles) {
      arena.checkpoint();
      double h = wr::normalizeHeading(from + a);
      if (s.prior &&
          std::abs(wr::angularDifferenceDegrees(h, p->trace->heading)) >
              request.options.maximumSearchAngleDegrees)
        continue;
      auto id = select(s, h, *e);
      if (!id) continue;
      if (result.diagnostics.generatedStates >=
          request.limits.maximumGeneratedStates)
        throw Stop{wr::RoutingStatus::ResourceLimitReached,
                   "generated state limit"};
      ++result.diagnostics.generatedStates;
      auto m = motion(s, h, dt, *id, false, true, &*e);
      if (!m) {
        ++result.diagnostics.constraintRejections;
        continue;
      }
      largestStepNm =
          std::max(largestStepNm, wr::distanceNm(s.point, m->leg.end));
      auto trace = new Trace(arena, p->trace, m->leg.end, m->leg.endTime);
      trace->heading = h;
      trace->angle = m->angle;
      trace->profile = *id;
      trace->tack = m->leg.tackTransition;
      trace->gybe = m->leg.gybeTransition;
      trace->modeDuration = m->modeDuration;
      trace->motorTime = m->motorTime;
      trace->fuel = m->fuel;
      auto q = new Position(arena, m->leg.end.latitude,
                            longitude(m->leg.end.longitude), trace);
      trace->release();
      if (points) {
        q->prev = points->prev;
        q->next = points;
        points->prev->next = q;
        points->prev = q;
      } else {
        points = q;
        q->prev = q->next = q;
      }
      ++count;
    }
    if (count < 3) {
      if (points) DeletePoints(points);
      return false;
    }
    output.push_back(new IsoRoute(points->BuildSkipList()));
    return true;
  }
  void merge(IsoRouteList& merged, IsoRouteList& input) {
    IsoRouteList unmerged;
    while (!input.empty()) {
      arena.checkpoint();
      auto first = input.front();
      input.pop_front();
      bool combined = false;
      while (!input.empty()) {
        arena.checkpoint();
        auto second = input.front();
        input.pop_front();
        IsoRouteList joined;
        if (Merge(joined, first, second, 0, false)) {
          input.splice(input.end(), joined);
          combined = true;
          break;
        }
        unmerged.push_back(second);
      }
      if (!combined) merged.push_back(first);
      input.splice(input.end(), unmerged);
    }
  }
  void captureFront(const IsoRouteList& front) {
    if (!options.captureVisualization ||
        result.visualization.isochrones.size() >= 128)
      return;
    wr::IsochroneLayer layer;
    std::size_t remaining = 1024, traces = 4;
    for (const auto* route : front) {
      if (!remaining || !route->skippoints) break;
      wr::IsochroneContour contour;
      const auto* start = route->skippoints->point;
      const auto* p = start;
      do {
        arena.checkpoint();
        if (!remaining) break;
        contour.points.push_back({p->lat, p->lon});
        --remaining;
        if (p->trace) {
          layer.time = std::max(layer.time, p->trace->time);
          if (traces) {
            wr::IsochroneTrace trace;
            trace.endpoint = p->trace->point;
            auto* parent = p->trace;
            while (parent && trace.route.size() < 256) {
              trace.route.push_back(parent->point);
              parent = parent->parent;
            }
            if (!parent) {
              std::reverse(trace.route.begin(), trace.route.end());
              layer.traces.push_back(std::move(trace));
              --traces;
            }
          }
        }
        p = p->next;
      } while (p != start);
      // Never close a contour that was truncated by the display allowance.
      contour.closed = p == start;
      if (contour.closed && contour.points.size() > 1)
        contour.points.push_back(contour.points.front());
      if (contour.points.size() > 1)
        layer.contours.push_back(std::move(contour));
    }
    if (!layer.contours.empty())
      result.visualization.isochrones.push_back(std::move(layer));
  }

  wr::RoutingResult run() {
    arena.checkpoint();
    if (environment.landAndBoundaries &&
        environment.landAndBoundaries->pointForbidden(request.start)) {
      result.status = wr::RoutingStatus::InvalidStart;
      result.message = "departure is on land";
      return result;
    }
    if (environment.landAndBoundaries &&
        environment.landAndBoundaries->pointForbidden(request.destination)) {
      result.status = wr::RoutingStatus::InvalidDestination;
      result.message = "destination is on land";
      return result;
    }
    if (request.constraints.landSafetyMarginNm > 0 &&
        environment.landAndBoundaries) {
      if (land(request.start, request.start, request.departure)) {
        result.status = wr::RoutingStatus::InvalidStart;
        result.message =
            "departure lies inside the configured land safety margin (no "
            "implicit harbour waiver)";
        return result;
      }
      if (land(request.destination, request.destination, request.departure)) {
        result.status = wr::RoutingStatus::InvalidDestination;
        result.message =
            "destination lies inside the configured land safety margin (no "
            "implicit harbour waiver)";
        return result;
      }
    }
    if (!weather(request.start, request.departure)) {
      result.status = missingStatus;
      result.message = "departure environment unavailable or constrained";
      return result;
    }
    auto trace = new Trace(arena, nullptr, request.start, request.departure);
    auto point = new Position(arena, request.start.latitude,
                              request.start.longitude, trace);
    trace->release();
    point->next = point->prev = point;
    IsoRouteList front;
    front.push_back(new IsoRoute(point->BuildSkipList()));
    const auto maxLayers = std::min<std::int64_t>(
        10000, request.limits.maximumRouteDuration.count() / 60 + 1);
    for (std::int64_t layer = 0; layer < maxLayers && !front.empty(); ++layer) {
      if (arrivals(front)) return result;
      if (candidatesTried >= options.maximumValidatedCandidates)
        throw Stop{wr::RoutingStatus::ResourceLimitReached,
                   "candidate validation allowance reached"};
      IsoRouteList grown;
      double closest = INFINITY, furthest = 0;
      for (auto route : front) {
        auto p = route->skippoints->point;
        do {
          closest = std::min(
              closest, wr::distanceNm(p->trace->point, request.destination));
          furthest = std::max(furthest,
                              wr::distanceNm(request.start, p->trace->point));
          p = p->next;
        } while (p != route->skippoints->point);
      }
      const double factor =
          request.options.adaptiveTimeStep
              ? std::min(std::min(1., .1 + .9 * closest / 40.),
                         std::min(1., .1 + .9 * furthest / 40.))
              : 1.;
      auto dt = std::max(wr::Duration(10),
                         wr::Duration(std::llround(
                             request.options.timeStep.count() * factor)));
      for (auto route : front) {
        bool propagated = false;
        auto p = route->skippoints->point;
        do {
          if (propagate(p, grown, dt)) propagated = true;
          // Broad heading fans can fill the transient arena before the
          // retained frontier is large. Compact pending contours while their
          // immutable parents remain alive; never raise the memory ceiling.
          if (grown.size() > 1 &&
              arena.traces.count >
                  request.limits.maximumRetainedStates * 3 / 4) {
            IsoRouteList compacted;
            merge(compacted, grown);
            grown.splice(grown.end(), compacted);
          }
          p = p->next;
        } while (p != route->skippoints->point);
        if (propagated) grown.push_front(new IsoRoute(route));
      }
      for (auto route : front) delete route;
      front.clear();
      if (grown.empty()) break;
      merge(front, grown);
      for (auto route : front) route->ReduceClosePoints();
      captureFront(front);
      result.diagnostics.retainedStates = std::max<std::uint64_t>(
          result.diagnostics.retainedStates, arena.traces.count);
      if (request.progress)
        request.progress({wr::RoutingProgressStage::ForwardIsochrone, 1, 1,
                          result.diagnostics.generatedStates,
                          arena.traces.count, result.diagnostics.landChecks,
                          result.diagnostics.closestApproachNm, 100});
    }
    result.status = missingStatus;
    result.message =
        "no validated endpoint connection within the available environment and "
        "search bounds";
    if (candidatesTried >= options.maximumValidatedCandidates) {
      result.status = wr::RoutingStatus::ResourceLimitReached;
      result.message = "candidate validation allowance reached";
    }
    return result;
  }
};
}  // namespace original_routing::detail

namespace original_routing {
wr::RoutingResult Engine::route(const wr::RoutingRequest& request,
                                const wr::RoutingEnvironment& environment,
                                const Options& options) const {
  wr::RoutingResult invalid;
  std::unique_ptr<detail::Search> search;
  try {
    if (request.cancellation.cancelled()) {
      invalid.status = wr::RoutingStatus::Cancelled;
      invalid.message = "cancelled";
      return invalid;
    }
    const auto validPoint = [](wr::GeoPoint p) {
      return std::isfinite(p.latitude) && std::isfinite(p.longitude) &&
             std::abs(p.latitude) <= 90;
    };
    if (!validPoint(request.start)) {
      invalid.status = wr::RoutingStatus::InvalidStart;
      return invalid;
    }
    if (!validPoint(request.destination)) {
      invalid.status = wr::RoutingStatus::InvalidDestination;
      return invalid;
    }
    if (!environment.performance ||
        !environment.performance->valid(&invalid.message)) {
      invalid.status = wr::RoutingStatus::InvalidVesselConfiguration;
      return invalid;
    }
    const auto bounded = [](double x, double lo, double hi) {
      return std::isfinite(x) && x >= lo && x <= hi;
    };
    const auto optionalLimit = [&](std::optional<double> x) {
      return !x || bounded(*x, 0, 1e9);
    };
    if (!std::isfinite(request.options.headingStepDegrees) ||
        request.options.headingStepDegrees < .5 ||
        request.options.headingStepDegrees > 90 ||
        request.options.timeStep < wr::Duration(60) ||
        request.limits.maximumRouteDuration <= wr::Duration::zero() ||
        request.options.timeStep > std::chrono::hours(24 * 7) ||
        request.limits.maximumRouteDuration > std::chrono::hours(24 * 366) ||
        !bounded(request.constraints.minimumTrueWindAngleDegrees, 0, 180) ||
        !bounded(request.constraints.maximumTrueWindAngleDegrees, 0, 180) ||
        request.constraints.minimumTrueWindAngleDegrees >
            request.constraints.maximumTrueWindAngleDegrees ||
        !bounded(request.constraints.landSafetyMarginNm, 0, 100) ||
        !bounded(request.constraints.maximumLatitudeDegrees, 0, 90) ||
        !bounded(request.options.maximumSearchAngleDegrees, 0, 180) ||
        !bounded(request.options.destinationToleranceNm, 0, 100) ||
        !bounded(request.limits.maximumExplorationDistanceNm, .001,
                 1000000000) ||
        !optionalLimit(request.constraints.maximumTrueWindKnots) ||
        !optionalLimit(request.constraints.maximumApparentWindKnots) ||
        !optionalLimit(request.constraints.maximumWaveHeightMetres) ||
        !optionalLimit(request.constraints.minimumDepthMetres) ||
        !optionalLimit(request.constraints.maximumOpposingWindCurrent) ||
        !optionalLimit(request.vessel.propulsion.maximumFuelLitres) ||
        request.vessel.tackPenalty < wr::Duration{} ||
        request.vessel.gybePenalty < wr::Duration{} ||
        request.vessel.sailPlanChangePenalty < wr::Duration{} ||
        request.vessel.propulsion.modeChangePenalty < wr::Duration{} ||
        request.vessel.propulsion.minimumMotorRun < wr::Duration{} ||
        request.limits.maximumRetainedStates >
            std::numeric_limits<std::size_t>::max() / 8 ||
        options.replaySlice < wr::Duration(1) ||
        options.replaySlice > wr::Duration(300) ||
        options.maximumRuntime.count() <= 0 ||
        options.maximumRuntime > std::chrono::hours(24)) {
      invalid.status = wr::RoutingStatus::InvalidVesselConfiguration;
      invalid.message = "invalid Original routing options";
      return invalid;
    }
    if (request.objective.kind != wr::ObjectiveKind::Fastest &&
        request.objective.kind != wr::ObjectiveKind::FastestUnderSafetyLimits) {
      invalid.status = wr::RoutingStatus::InvalidVesselConfiguration;
      invalid.message =
          "Original supports deterministic fastest-route objectives only";
      return invalid;
    }
    search = std::make_unique<detail::Search>(request, environment, options);
    return search->run();
  } catch (const detail::Stop& stop) {
    if (search) invalid = std::move(search->result);
    invalid.status = stop.status;
    invalid.message = stop.reason;
  } catch (const std::bad_alloc&) {
    invalid.status = wr::RoutingStatus::ResourceLimitReached;
    invalid.message = "memory allocation failed";
  } catch (const std::exception& e) {
    invalid.status = wr::RoutingStatus::InternalError;
    invalid.message = e.what();
  } catch (...) {
    invalid.status = wr::RoutingStatus::InternalError;
    invalid.message = "provider or callback threw an unknown exception";
  }
  invalid.legs.clear();
  invalid.validation = {};
  return invalid;
}
}  // namespace original_routing
