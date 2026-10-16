// SPDX-License-Identifier: GPL-3.0-or-later
#include "atlas_routing/Engine.h"
#include "MotionKernel.h"
#include "RoutingInternal.h"
#include "supercpn/weather_routing/Preview.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <deque>
#include <map>
#include <memory_resource>
#include <numbers>
#include <queue>
#include <stdexcept>
#include <tuple>

namespace atlas_routing {
namespace {
namespace in = wr::internal;
using Clock = std::chrono::steady_clock;
constexpr auto none = ~std::uint32_t{};
double milliseconds(Clock::time_point t) {
  return std::chrono::duration<double, std::milli>(Clock::now() - t).count();
}
struct Stop {
  wr::RoutingStatus status;
  const char* reason;
};

class Memory final : public std::pmr::memory_resource {
public:
  explicit Memory(std::uint64_t cap) : cap_(cap) {}
  std::uint64_t peak{};

private:
  void* do_allocate(std::size_t n, std::size_t alignment) override {
    if (n > cap_ - used_) throw std::bad_alloc();
    auto* p = std::pmr::new_delete_resource()->allocate(n, alignment);
    used_ += n;
    peak = std::max(peak, used_);
    return p;
  }
  void do_deallocate(void* p, std::size_t n, std::size_t alignment) override {
    std::pmr::new_delete_resource()->deallocate(p, n, alignment);
    used_ -= n;
  }
  bool do_is_equal(const std::pmr::memory_resource& b) const noexcept override {
    return &b == this;
  }
  std::uint64_t cap_, used_{};
};

struct Work {
  const wr::RoutingRequest& request;
  const Options& options;
  Diagnostics& d;
  bool validating{};
  void check() const {
    if (request.cancellation.cancelled())
      throw Stop{wr::RoutingStatus::Cancelled, "Atlas cancelled"};
  }
  void weather() {
    check();
    if (++d.weatherQueries > options.maximumWeatherQueries)
      throw Stop{wr::RoutingStatus::ResourceLimitReached,
                 "Atlas weather query allowance"};
  }
  void geometry() {
    check();
    if (++d.geometryQueries > options.maximumGeometryQueries)
      throw Stop{wr::RoutingStatus::ResourceLimitReached,
                 "Atlas geometry query allowance"};
  }
  void edge() {
    check();
    if (d.edgeAttempts >= std::min(options.maximumEdgeAttempts,
                                   request.limits.maximumGeneratedStates))
      throw Stop{wr::RoutingStatus::ResourceLimitReached,
                 "Atlas edge solve allowance"};
    ++d.edgeAttempts;
  }
};

class Weather final : public wr::WeatherProvider {
public:
  Weather(std::shared_ptr<const wr::WeatherProvider> p, Work& w)
      : p_(std::move(p)), w_(w) {}
  wr::ParameterCoverage windCoverage() const override {
    return p_->windCoverage();
  }
  wr::ParameterCoverage currentCoverage() const override {
    return p_->currentCoverage();
  }
  wr::ParameterCoverage waveCoverage() const override {
    return p_->waveCoverage();
  }
  wr::WindSample wind(wr::GeoPoint p, wr::TimePoint t) const override {
    w_.weather();
    return p_->wind(p, t);
  }
  wr::CurrentSample current(wr::GeoPoint p, wr::TimePoint t) const override {
    w_.weather();
    return p_->current(p, t);
  }
  wr::WaveSample waves(wr::GeoPoint p, wr::TimePoint t) const override {
    w_.weather();
    return p_->waves(p, t);
  }
  std::string identity() const override { return p_->identity(); }

private:
  std::shared_ptr<const wr::WeatherProvider> p_;
  Work& w_;
};
class Climatology final : public wr::ClimatologyProvider {
public:
  Climatology(std::shared_ptr<const wr::ClimatologyProvider> p, Work& w)
      : p_(std::move(p)), w_(w) {}
  wr::ParameterCoverage coverage() const override { return p_->coverage(); }
  wr::WindSample wind(wr::GeoPoint p, wr::TimePoint t) const override {
    w_.weather();
    return p_->wind(p, t);
  }
  std::string identity() const override { return p_->identity(); }

private:
  std::shared_ptr<const wr::ClimatologyProvider> p_;
  Work& w_;
};
class Current final : public wr::CurrentPredictionProvider {
public:
  Current(std::shared_ptr<wr::CurrentPredictionProvider> p, Work& w)
      : p_(std::move(p)), w_(w) {}
  wr::ParameterCoverage coverage() const override { return p_->coverage(); }
  wr::CoverageResult ensureCoverage(const wr::GeoEnvelope& a, wr::TimePoint s,
                                    wr::TimePoint e, wr::Duration d,
                                    const wr::CancellationToken& c) override {
    w_.check();
    return p_->ensureCoverage(a, s, e, d, c);
  }
  wr::CurrentSample sample(wr::GeoPoint p, wr::TimePoint t) const override {
    w_.weather();
    return p_->sample(p, t);
  }
  std::string identity() const override { return p_->identity(); }
  std::uint64_t sampleCount() const override { return p_->sampleCount(); }
  std::uint64_t expansionCount() const override { return p_->expansionCount(); }

private:
  std::shared_ptr<wr::CurrentPredictionProvider> p_;
  Work& w_;
};
class Performance final : public wr::VesselPerformanceModel {
public:
  Performance(const wr::VesselPerformanceModel& p, Work& w) : p_(p), w_(w) {}
  bool valid(std::string* reason) const override { return p_.valid(reason); }
  std::optional<double> bestSailingSpeedAt(wr::GeoPoint p, wr::TimePoint t,
                                           double s, const wr::WaveSample& v,
                                           double a, double b) const override {
    w_.check();
    ++w_.d.polarQueries;
    return p_.bestSailingSpeedAt(p, t, s, v, a, b);
  }
  std::vector<wr::PerformanceCandidate> candidates(
      double s, double a, const wr::WaveSample& v, wr::PropulsionMode m,
      wr::Duration d) const override {
    w_.check();
    ++w_.d.polarQueries;
    return p_.candidates(s, a, v, m, d);
  }
  std::vector<wr::PerformanceCandidate> candidatesAt(
      wr::GeoPoint p, wr::TimePoint t, double s, double a,
      const wr::WaveSample& v, wr::PropulsionMode m,
      wr::Duration d) const override {
    w_.check();
    ++w_.d.polarQueries;
    return p_.candidatesAt(p, t, s, a, v, m, d);
  }
  wr::PerformanceCandidate evaluate(wr::PropulsionMode m, wr::ProfileRole r,
                                    const std::string& id, double s, double a,
                                    const wr::WaveSample& v) const override {
    w_.check();
    ++w_.d.polarQueries;
    return p_.evaluate(m, r, id, s, a, v);
  }
  wr::PerformanceCandidate evaluateAt(wr::GeoPoint p, wr::TimePoint t,
                                      wr::PropulsionMode m, wr::ProfileRole r,
                                      const std::string& id, double s, double a,
                                      const wr::WaveSample& v) const override {
    w_.check();
    ++w_.d.polarQueries;
    return p_.evaluateAt(p, t, m, r, id, s, a, v);
  }

private:
  const wr::VesselPerformanceModel& p_;
  Work& w_;
};
class Land final : public wr::LandAndBoundaryProvider {
public:
  Land(std::shared_ptr<const wr::LandAndBoundaryProvider> p, Work& w,
       bool hint = false)
      : p_(std::move(p)), w_(w), hint_(hint) {}
  bool pointForbidden(wr::GeoPoint a) const override {
    geometry();
    return p_->pointForbidden(a);
  }
  bool segmentForbidden(wr::GeoPoint a, wr::GeoPoint b,
                        double m) const override {
    geometry();
    return p_->segmentForbidden(a, b, m);
  }
  bool segmentForbiddenAt(wr::GeoPoint a, wr::GeoPoint b, wr::TimePoint t,
                          double m) const override {
    geometry();
    return p_->segmentForbiddenAt(a, b, t, m);
  }
  bool segmentFromKnownSafeForbidden(wr::GeoPoint a, wr::GeoPoint b,
                                     double m) const override {
    geometry();
    return p_->segmentFromKnownSafeForbidden(a, b, m);
  }
  bool segmentFromKnownSafeForbiddenAt(wr::GeoPoint a, wr::GeoPoint b,
                                       wr::TimePoint t,
                                       double m) const override {
    geometry();
    return p_->segmentFromKnownSafeForbiddenAt(a, b, t, m);
  }
  bool validationSegmentFromKnownSafeForbiddenAt(wr::GeoPoint a, wr::GeoPoint b,
                                                 wr::TimePoint t,
                                                 double m) const override {
    geometry();
    return p_->validationSegmentFromKnownSafeForbiddenAt(a, b, t, m);
  }
  double distanceToForbiddenNm(wr::GeoPoint a) const override {
    geometry();
    return p_->distanceToForbiddenNm(a);
  }
  std::optional<double> depthMetres(wr::GeoPoint a) const override {
    geometry();
    return p_->depthMetres(a);
  }
  void prepareValidationRoute(std::span<const wr::RouteLeg> a,
                              double m) const override {
    w_.check();
    p_->prepareValidationRoute(a, m);
  }
  bool validationFailureRequiresSearchEscalation() const override {
    return p_->validationFailureRequiresSearchEscalation();
  }
  bool supportsLandRejectionGuidance() const override {
    return p_->supportsLandRejectionGuidance();
  }
  std::string identity() const override { return p_->identity(); }

private:
  std::shared_ptr<const wr::LandAndBoundaryProvider> p_;
  Work& w_;
  bool hint_{};
  void geometry() const {
    w_.geometry();
    if (hint_) ++w_.d.guidanceGeometryQueries;
  }
};

// A coarse map must never delete a physically admissible vertex. Its segment
// and clearance hints influence scheduling only; the true provider still
// admits points and every physical edge is checked outside this adapter.
class GuidanceLand final : public wr::LandAndBoundaryProvider {
public:
  GuidanceLand(std::shared_ptr<const wr::LandAndBoundaryProvider> actual,
               std::shared_ptr<const wr::LandAndBoundaryProvider> hint)
      : actual_(std::move(actual)), hint_(std::move(hint)) {}
  bool pointForbidden(wr::GeoPoint p) const override {
    return actual_ && actual_->pointForbidden(p);
  }
  bool segmentForbidden(wr::GeoPoint a, wr::GeoPoint b,
                        double m) const override {
    return hint_->segmentForbidden(a, b, m);
  }
  bool segmentFromKnownSafeForbiddenAt(wr::GeoPoint a, wr::GeoPoint b,
                                       wr::TimePoint t,
                                       double m) const override {
    return hint_->segmentFromKnownSafeForbiddenAt(a, b, t, m);
  }
  double distanceToForbiddenNm(wr::GeoPoint p) const override {
    return hint_->distanceToForbiddenNm(p);
  }
  std::string identity() const override {
    return "Atlas geometry scheduling hints";
  }

private:
  std::shared_ptr<const wr::LandAndBoundaryProvider> actual_, hint_;
};

// Azimuthal equidistant coordinates, oriented along the great-circle departure
// bearing. All real motion and distances use the shared spherical geometry.
struct Plane {
  wr::GeoPoint origin;
  double bearing, length;
  Plane(wr::GeoPoint a, wr::GeoPoint b)
      : origin(a),
        bearing(wr::initialBearingDegrees(a, b)),
        length(wr::distanceNm(a, b)) {}
  wr::GeoPoint point(double x, double y) const {
    return wr::destinationPoint(
        origin, bearing + std::atan2(y, x) * 180 / std::numbers::pi,
        std::hypot(x, y));
  }
  std::pair<double, double> xy(wr::GeoPoint p) const {
    const auto d = wr::distanceNm(origin, p);
    const auto a = wr::angularDifferenceDegrees(
                       wr::initialBearingDegrees(origin, p), bearing) *
                   std::numbers::pi / 180;
    return {d * std::cos(a), d * std::sin(a)};
  }
};
struct Vertex {
  wr::GeoPoint point;
  double x{}, y{}, clearance{};
  bool anchor{};
};
struct Arc {
  std::uint32_t to{};
  double length{}, guide{};
};
struct Graph {
  std::pmr::vector<Vertex> vertices;
  std::pmr::vector<std::pmr::vector<Arc>> links;
  std::pmr::vector<double> guide;
  std::pmr::map<std::pair<std::int64_t, std::int64_t>, std::uint32_t> unique;
  Graph(std::pmr::memory_resource* m)
      : vertices(m), links(m), guide(m), unique(m) {}
};

double cross(wr::Vector2 a, wr::Vector2 b) {
  return a.eastKnots * b.northKnots - a.northKnots * b.eastKnots;
}
wr::Vector2 minus(wr::Vector2 a, wr::Vector2 b) {
  return {a.eastKnots - b.eastKnots, a.northKnots - b.northKnots};
}
std::vector<wr::Vector2> velocityHull(std::vector<wr::Vector2> v) {
  if (v.size() < 3) return v;
  std::sort(v.begin(), v.end(), [](auto a, auto b) {
    return std::tie(a.eastKnots, a.northKnots) <
           std::tie(b.eastKnots, b.northKnots);
  });
  std::vector<wr::Vector2> hull;
  for (auto p : v) {
    while (hull.size() > 1 && cross(minus(hull.back(), hull[hull.size() - 2]),
                                    minus(p, hull.back())) <= 0)
      hull.pop_back();
    hull.push_back(p);
  }
  auto lower = hull.size();
  for (auto i = v.rbegin() + 1; i != v.rend(); ++i) {
    while (hull.size() > lower &&
           cross(minus(hull.back(), hull[hull.size() - 2]),
                 minus(*i, hull.back())) <= 0)
      hull.pop_back();
    hull.push_back(*i);
  }
  hull.pop_back();
  return hull;
}
double radialSpeed(const std::vector<wr::Vector2>& hull, double bearing) {
  auto u = wr::speedDirectionToVector(1, bearing);
  double best = 0;
  for (std::size_t i = 0; i < hull.size(); ++i) {
    auto a = hull[i], delta = minus(hull[(i + 1) % hull.size()], a);
    auto det = cross(u, delta);
    if (std::abs(det) < 1e-10) continue;
    auto t = cross(a, delta) / det, s = cross(a, u) / det;
    if (s >= 0 && s <= 1) best = std::max(best, t);
  }
  return best;
}

bool finitePoint(wr::GeoPoint p) {
  return std::isfinite(p.latitude) && std::isfinite(p.longitude) &&
         std::abs(p.latitude) <= 85 && std::abs(p.longitude) <= 180;
}

void addPoint(Graph& g, const Plane& plane, double x, double y,
              const wr::RoutingRequest& r, const wr::RoutingEnvironment& env,
              const Options& opt) {
  if (g.vertices.size() >= opt.maximumWaypoints) return;
  const auto key = std::pair{std::llround(x * 10000), std::llround(y * 10000)};
  if (g.unique.contains(key)) return;
  auto p = plane.point(x, y);
  if (!finitePoint(p) ||
      std::abs(p.latitude) > r.constraints.maximumLatitudeDegrees ||
      wr::distanceNm(r.start, p) > r.limits.maximumExplorationDistanceNm)
    return;
  if (env.landAndBoundaries && env.landAndBoundaries->pointForbidden(p)) return;
  double clearance = env.landAndBoundaries
                         ? env.landAndBoundaries->distanceToForbiddenNm(p)
                         : INFINITY;
  if (std::isnan(clearance)) clearance = 0;
  g.unique.emplace(key, static_cast<std::uint32_t>(g.vertices.size()));
  g.vertices.push_back({p, x, y, clearance});
}

// In a homogeneous planar field with zero switching cost, the best mean
// velocity lies on an exposed edge of the convex hull of feasible velocities.
// A boundary point of a planar convex hull needs at most two controls. This
// supplies spatial hypotheses for explicit tacks/gybes, not fictitious VMG
// motion through an island. All hypotheses are subsequently sailed/replayed.
std::vector<wr::GeoPoint> envelopeSeeds(
    const wr::RoutingRequest& r, const wr::RoutingEnvironment& env,
    const wr::VesselPerformanceModel& perf) {
  const auto e = in::resolveEnvironment(r, env, r.start, r.departure);
  if (e.failureStatus != wr::RoutingStatus::Complete) return {};
  std::vector<wr::Vector2> velocities;
  for (double h = 0; h < 360; h += 2) {
    const auto angle = wr::trueWindAngleDegrees(e.snapshot.wind.velocity, h);
    for (const auto& c : perf.candidatesAt(
             r.start, r.departure,
             wr::vectorMagnitudeKnots(e.snapshot.wind.velocity), angle,
             e.snapshot.waves, wr::PropulsionMode::Sail, {})) {
      if (!c.valid || !wr::sailingAngleAllowed(
                          r, perf, r.start, r.departure,
                          wr::vectorMagnitudeKnots(e.snapshot.wind.velocity),
                          angle, e.snapshot.waves, c.mode, c.role))
        continue;
      auto v = wr::speedDirectionToVector(c.speedThroughWaterKnots, h);
      v.eastKnots += e.snapshot.current.velocity.eastKnots;
      v.northKnots += e.snapshot.current.velocity.northKnots;
      velocities.push_back(v);
    }
  }
  const auto displacement = wr::speedDirectionToVector(
      wr::distanceNm(r.start, r.destination),
      wr::initialBearingDegrees(r.start, r.destination));
  std::vector<std::pair<double, wr::GeoPoint>> candidates;
  double best = INFINITY;
  for (auto a : velocities)
    for (auto b : velocities) {
      const double det =
          a.eastKnots * b.northKnots - a.northKnots * b.eastKnots;
      if (std::abs(det) < 1e-8) continue;
      const double ta = (displacement.eastKnots * b.northKnots -
                         displacement.northKnots * b.eastKnots) /
                        det;
      const double tb = (a.eastKnots * displacement.northKnots -
                         a.northKnots * displacement.eastKnots) /
                        det;
      if (ta < .02 || tb < .02 ||
          (ta + tb) * 3600 > r.limits.maximumRouteDuration.count())
        continue;
      if (ta + tb > best * 1.1) continue;
      best = std::min(best, ta + tb);
      candidates.emplace_back(
          ta + tb,
          wr::destinationPoint(r.start, wr::vectorDirectionToDegrees(a),
                               wr::vectorMagnitudeKnots(a) * ta));
    }
  std::stable_sort(
      candidates.begin(), candidates.end(),
      [](const auto& a, const auto& b) { return a.first < b.first; });
  std::vector<wr::GeoPoint> result;
  for (const auto& [cost, p] : candidates) {
    if (result.size() >= 16 || cost > best * 1.05) break;
    if (std::any_of(result.begin(), result.end(), [&](auto q) {
          return wr::distanceNm(p, q) <
                 std::max(.1, wr::distanceNm(r.start, r.destination) * .015);
        }))
      continue;
    result.push_back(p);
  }
  return result;
}

void mesh(Graph& g, const Plane& p, const wr::RoutingRequest& r,
          const wr::RoutingEnvironment& env, const Options& o, Work& work,
          double x, double y, double size, double base, unsigned depth = 0) {
  work.check();
  if (g.vertices.size() + 5 >= o.maximumWaypoints || depth > 18) return;
  const auto centre = p.point(x, y);
  const bool blocked =
      env.landAndBoundaries && env.landAndBoundaries->pointForbidden(centre);
  bool mixed = false;
  if (env.landAndBoundaries)
    for (auto [dx, dy] : std::array<std::pair<double, double>, 4>{
             {{-.5, -.5}, {.5, -.5}, {-.5, .5}, {.5, .5}}})
      mixed = mixed || env.landAndBoundaries->pointForbidden(
                           p.point(x + dx * size, y + dy * size)) != blocked;
  double clearance = env.landAndBoundaries
                         ? env.landAndBoundaries->distanceToForbiddenNm(centre)
                         : INFINITY;
  if (std::isnan(clearance)) clearance = 0;
  const double endpoint =
      std::min(std::hypot(x, y), std::hypot(x - p.length, y));
  const double target =
      endpoint < 3 * size && std::isfinite(clearance)
          ? std::min(base, std::max(o.minimumCellNm, endpoint / 3))
          : std::min(base, std::max(o.coastalCellNm, clearance / 2));
  if (size > base || (mixed && size > o.minimumCellNm) ||
      (!blocked && size > target && clearance < size * 2) ||
      (endpoint < size * 2 && std::isfinite(clearance) && size > target)) {
    for (auto [dx, dy] : std::array<std::pair<double, double>, 4>{
             {{-.25, -.25}, {.25, -.25}, {-.25, .25}, {.25, .25}}})
      mesh(g, p, r, env, o, work, x + dx * size, y + dy * size, size / 2, base,
           depth + 1);
    return;
  }
  addPoint(g, p, x, y, r, env, o);
  for (auto [dx, dy] : std::array<std::pair<double, double>, 4>{
           {{-.5, -.5}, {.5, -.5}, {-.5, .5}, {.5, .5}}})
    addPoint(g, p, x + dx * size, y + dy * size, r, env, o);
}

// Solve one spatial boundary-value problem. Heading AND elapsed time are
// continuous unknowns (duration rounded to the shared model's one second).
// Keep the actual integrated endpoint, never snap intermediate states or
// grant a harbour-ingress exception to a roadmap vertex.
std::vector<in::MotionCandidate> shoot(const wr::RoutingRequest& r,
                                       const wr::RoutingEnvironment& env,
                                       const wr::VesselPerformanceModel& perf,
                                       const in::MotionState& from,
                                       wr::GeoPoint target, const Options& opt,
                                       Work& work,
                                       wr::RoutingDiagnostics& diag) {
  work.edge();
  if (target == r.destination && (r.constraints.landSafetyMarginNm > 0 ||
                                  r.options.destinationToleranceNm < .008)) {
    // The common endpoint policy admits ingress ONLY for an exact terminal
    // connection. Intermediate spatial vertices always retain the original
    // destination and full stand-off. Reuse that operation here rather than
    // widening a tolerance or granting ingress to every waypoint.
    ++work.d.shootingIntegrations;
    auto terminal = in::checkedConnection(r, env, perf, from,
                                          opt.maximumEdgeDuration, diag);
    if (terminal) return {std::move(*terminal)};
    ++work.d.rejectedEdges;
    return {};
  }
  const auto resolved =
      in::resolveEnvironment(r, env, from.position, from.time);
  ++diag.weatherSamples;
  if (resolved.failureStatus != wr::RoutingStatus::Complete) return {};
  const auto& e = resolved.snapshot;
  const double length = wr::distanceNm(from.position, target);
  if (length < .002) return {};
  const auto bearing = wr::initialBearingDegrees(from.position, target);
  auto heading = bearing;
  // A local current-compensated polar fixed point gives a much better Newton
  // seed than the geographical bearing in strong cross-current.
  for (unsigned i = 0; i < 5; ++i) {
    auto c = perf.candidatesAt(
        from.position, from.time, wr::vectorMagnitudeKnots(e.wind.velocity),
        wr::trueWindAngleDegrees(e.wind.velocity, heading), e.waves, from.mode,
        from.modeDuration);
    if (c.empty()) break;
    const auto best =
        std::max_element(c.begin(), c.end(), [](const auto& a, const auto& b) {
          return a.speedThroughWaterKnots < b.speedThroughWaterKnots;
        });
    const auto water =
        wr::speedDirectionToVector(best->speedThroughWaterKnots, heading);
    const auto ground =
        wr::Vector2{water.eastKnots + e.current.velocity.eastKnots,
                    water.northKnots + e.current.velocity.northKnots};
    heading = wr::normalizeHeading(
        heading - std::clamp(wr::angularDifferenceDegrees(
                                 wr::vectorDirectionToDegrees(ground), bearing),
                             -30., 30.));
  }
  auto seeds = perf.candidatesAt(
      from.position, from.time, wr::vectorMagnitudeKnots(e.wind.velocity),
      wr::trueWindAngleDegrees(e.wind.velocity, heading), e.waves, from.mode,
      from.modeDuration);
  std::vector<in::MotionCandidate> result;
  for (const auto& selected : seeds) {
    if (!selected.valid || selected.speedThroughWaterKnots <= .01) continue;
    auto h = heading;
    const auto unit = wr::speedDirectionToVector(1, bearing);
    const double currentAlong = e.current.velocity.eastKnots * unit.eastKnots +
                                e.current.velocity.northKnots * unit.northKnots;
    double seconds =
        length / std::max(.1, selected.speedThroughWaterKnots + currentAlong) *
        3600;
    for (unsigned iteration = 0; iteration < 9; ++iteration) {
      work.check();
      const auto duration = wr::Duration{
          static_cast<std::int64_t>(std::max(1., std::round(seconds)))};
      if (duration > opt.maximumEdgeDuration ||
          from.time + duration > r.departure + r.limits.maximumRouteDuration)
        break;
      wr::RoutingStatus failure = wr::RoutingStatus::Complete;
      ++work.d.shootingIntegrations;
      auto motions = in::checkedMotion(r, env, perf, from, h, duration,
                                       std::chrono::minutes{5}, diag, &failure);
      auto m = std::find_if(motions.begin(), motions.end(), [&](const auto& m) {
        return m.state.mode == selected.mode && m.state.role == selected.role &&
               m.state.profileIdentity == selected.profileIdentity &&
               m.state.sailPlan == selected.sailPlan;
      });
      if (m == motions.end()) break;
      const double miss = wr::distanceNm(m->state.position, target);
      if (miss <= .008) {
        result.push_back(std::move(*m));
        break;
      }
      const auto achieved = wr::distanceNm(from.position, m->state.position);
      if (achieved < .002) break;
      const auto achievedBearing =
          wr::initialBearingDegrees(from.position, m->state.position);
      bool newton = false;
      // The endpoint map couples heading and duration in a spatially varying
      // current. Independent bearing/range correction can oscillate forever
      // near a jet. Differentiate the actual shared flow map in heading; its
      // time derivative is the endpoint velocity. Solve the 2x2 shooting
      // Jacobian, with a trust region around the current feasible trial.
      for (double dh : {.25, -.25}) {
        ++work.d.shootingIntegrations;
        auto perturbed = in::checkedMotion(
            r, env, perf, from, wr::normalizeHeading(h + dh), duration,
            std::chrono::minutes{5}, diag, &failure);
        auto other = std::find_if(
            perturbed.begin(), perturbed.end(), [&](const auto& x) {
              return x.state.mode == selected.mode &&
                     x.state.role == selected.role &&
                     x.state.profileIdentity == selected.profileIdentity &&
                     x.state.sailPlan == selected.sailPlan;
            });
        if (other == perturbed.end()) continue;
        const auto derivative = wr::speedDirectionToVector(
            wr::distanceNm(m->state.position, other->state.position) / dh,
            wr::initialBearingDegrees(m->state.position,
                                      other->state.position));
        const auto end =
            in::resolveEnvironment(r, env, m->state.position, m->state.time);
        if (end.failureStatus != wr::RoutingStatus::Complete) break;
        const auto c = perf.evaluateAt(
            m->state.position, m->state.time, selected.mode, selected.role,
            selected.profileIdentity,
            wr::vectorMagnitudeKnots(end.snapshot.wind.velocity),
            wr::trueWindAngleDegrees(end.snapshot.wind.velocity, h),
            end.snapshot.waves);
        if (!c.valid) break;
        auto velocity = wr::speedDirectionToVector(c.speedThroughWaterKnots, h);
        velocity.eastKnots =
            (velocity.eastKnots + end.snapshot.current.velocity.eastKnots) /
            3600;
        velocity.northKnots =
            (velocity.northKnots + end.snapshot.current.velocity.northKnots) /
            3600;
        const auto error = wr::speedDirectionToVector(
            miss, wr::initialBearingDegrees(m->state.position, target));
        const double det = cross(derivative, velocity);
        if (std::abs(det) < 1e-12) break;
        const double deltaH = cross(error, velocity) / det,
                     deltaT = cross(derivative, error) / det;
        h = wr::normalizeHeading(h + std::clamp(deltaH, -20., 20.));
        seconds = std::clamp(seconds + deltaT, seconds * .5, seconds * 2);
        newton = true;
        break;
      }
      if (newton) continue;
      h = wr::normalizeHeading(
          h - std::clamp(wr::angularDifferenceDegrees(achievedBearing, bearing),
                         -25., 25.));
      // Account for the motion-free portion explicitly; scaling the total
      // duration converges slowly on short tacking/mode-change connections.
      wr::Duration penalty{};
      if (m->leg.tackTransition) penalty += r.vessel.tackPenalty;
      if (m->leg.gybeTransition) penalty += r.vessel.gybePenalty;
      if (m->leg.propulsionTransition)
        penalty += r.vessel.propulsion.modeChangePenalty;
      if (from.sailPlan >= 0 && m->state.sailPlan >= 0 &&
          from.sailPlan != m->state.sailPlan)
        penalty += r.vessel.sailPlanChangePenalty;
      const double updated =
          (duration.count() - penalty.count()) * length / achieved +
          penalty.count();
      seconds = std::clamp(updated, seconds * .4, seconds * 2.5);
    }
  }
  if (result.empty()) ++work.d.rejectedEdges;
  return result;
}

void buildGraph(Graph& g, const Plane& plane, const wr::RoutingRequest& r,
                const wr::RoutingEnvironment& env, const Options& o, Work& work,
                unsigned pass, const std::vector<wr::GeoPoint>& seeds,
                const wr::VesselPerformanceModel& perf, double speedHint) {
  // IDs 0 and 1 are always the exact requested endpoints.
  g.vertices.push_back({r.start, 0, 0, 0, true});
  g.vertices.push_back({r.destination, plane.length, 0, 0, true});
  g.unique.emplace(std::pair{std::int64_t{0}, std::int64_t{0}}, 0);
  g.unique.emplace(
      std::pair{std::llround(plane.length * 10000), std::int64_t{0}}, 1);
  const double base = std::max(.5, plane.length / (12 + 8 * pass));
  const double pad =
      std::max(3., plane.length * o.domainWidthFraction * (1 + pass));
  const double size = std::max(plane.length + 2 * pad, 2 * pad);
  for (auto p : seeds) {
    const auto [x, y] = plane.xy(p);
    const auto before = g.vertices.size();
    addPoint(g, plane, x, y, r, env, o);
    if (g.vertices.size() > before) g.vertices.back().anchor = true;
  }
  // Install a globally spread coarse mesh before spending refinement on any
  // one coastline. A depth-first refinement must never consume the entire
  // waypoint allowance on the first harbour/headland it encounters.
  for (double x = -pad; x <= plane.length + pad; x += base)
    for (double y = -pad; y <= pad; y += base) {
      const auto before = g.vertices.size();
      addPoint(g, plane, x, y, r, env, o);
      if (g.vertices.size() > before) g.vertices.back().anchor = true;
    }
  // Rings are independent of coast orientation and quadtree phase. They
  // resolve short, curved harbour egress/ingress and both tack directions.
  for (auto [x, y] :
       std::array<std::pair<double, double>, 2>{{{0, 0}, {plane.length, 0}}})
    for (double radius = o.minimumCellNm; radius < std::max(4., base * 2);
         radius *= 2)
      for (unsigned k = 0; k < 24; ++k) {
        const auto a = k * 2 * std::numbers::pi / 24;
        addPoint(g, plane, x + radius * std::cos(a), y + radius * std::sin(a),
                 r, env, o);
      }
  std::pmr::vector<std::tuple<double, double, double>> refine(
      g.vertices.get_allocator().resource());
  for (double x = -pad; x <= plane.length + pad; x += base)
    for (double y = -pad; y <= pad; y += base) {
      const auto point = plane.point(x, y);
      if (env.landAndBoundaries && env.landAndBoundaries->distanceToForbiddenNm(
                                       point) < std::min(10., base * 2))
        refine.emplace_back(std::abs(y) + .1 * std::abs(x - plane.length / 2),
                            x, y);
    }
  std::sort(refine.begin(), refine.end());
  for (auto [priority, x, y] : refine) {
    (void)priority;
    mesh(g, plane, r, env, o, work, x, y, base, base);
  }
  g.links.resize(g.vertices.size());
  std::pmr::vector<std::pair<double, std::uint32_t>> near(
      g.vertices.get_allocator().resource());
  for (std::uint32_t i = 0; i < g.vertices.size(); ++i) {
    work.check();
    const auto e = in::resolveEnvironment(
        r, env, g.vertices[i].point,
        r.departure + wr::Duration{static_cast<long>(
                          std::hypot(g.vertices[i].x, g.vertices[i].y) * 3600 /
                          speedHint)});
    std::vector<wr::Vector2> velocities;
    if (e.failureStatus == wr::RoutingStatus::Complete)
      for (unsigned k = 0; k < 48; ++k) {
        const double h = k * 7.5, angle = wr::trueWindAngleDegrees(
                                      e.snapshot.wind.velocity, h);
        for (const auto& c : perf.candidatesAt(
                 g.vertices[i].point, r.departure,
                 wr::vectorMagnitudeKnots(e.snapshot.wind.velocity), angle,
                 e.snapshot.waves, wr::PropulsionMode::Sail, {})) {
          if (!c.valid ||
              !wr::sailingAngleAllowed(
                  r, perf, g.vertices[i].point, r.departure,
                  wr::vectorMagnitudeKnots(e.snapshot.wind.velocity), angle,
                  e.snapshot.waves, c.mode, c.role))
            continue;
          auto v = wr::speedDirectionToVector(c.speedThroughWaterKnots, h);
          v.eastKnots += e.snapshot.current.velocity.eastKnots;
          v.northKnots += e.snapshot.current.velocity.northKnots;
          velocities.push_back(v);
        }
      }
    const auto hull = velocityHull(std::move(velocities));
    near.clear();
    std::array<std::pair<double, std::uint32_t>, 32> sectors;
    sectors.fill({INFINITY, none});
    for (std::uint32_t j = 0; j < g.vertices.size(); ++j) {
      if (i == j) continue;
      const auto dx = g.vertices[j].x - g.vertices[i].x,
                 dy = g.vertices[j].y - g.vertices[i].y;
      const auto length = std::hypot(dx, dy);
      near.emplace_back(length, j);
      const auto sector =
          static_cast<unsigned>((std::atan2(dy, dx) + std::numbers::pi) *
                                sectors.size() / (2 * std::numbers::pi)) %
          sectors.size();
      if (length < sectors[sector].first) sectors[sector] = {length, j};
    }
    const auto n = std::min<std::size_t>(o.neighbours, near.size());
    std::partial_sort(near.begin(), near.begin() + n, near.end());
    near.resize(n);
    for (auto [d, j] : sectors)
      if (j != none) near.emplace_back(d, j);
    if (i == 0)
      for (std::uint32_t j = 2; j < g.vertices.size(); ++j)
        if (g.vertices[j].anchor)
          near.emplace_back(std::hypot(g.vertices[j].x, g.vertices[j].y), j);
    if (i != 1)
      near.emplace_back(
          std::hypot(g.vertices[i].x - plane.length, g.vertices[i].y), 1);
    std::sort(near.begin(), near.end(),
              [](auto a, auto b) { return a.second < b.second; });
    near.erase(std::unique(near.begin(), near.end(),
                           [](auto a, auto b) { return a.second == b.second; }),
               near.end());
    for (auto [d, j] : near) {
      const auto& a = g.vertices[i];
      const auto& b = g.vertices[j];
      const auto speed =
          radialSpeed(hull, wr::initialBearingDegrees(a.point, b.point));
      double guide = d * speedHint / std::max(.2, speed);
      // Static geography is guidance, never a feasibility certificate or a
      // reason to discard a time-dependent edge. Leave all arcs in the graph.
      if (env.landAndBoundaries && (a.clearance < d || b.clearance < d) &&
          env.landAndBoundaries->segmentForbidden(
              a.point, b.point, r.constraints.landSafetyMarginNm))
        guide += size * 8;
      g.links[i].push_back({j, wr::distanceNm(a.point, b.point), guide});
      ++work.d.graphEdges;
    }
  }
  // Reverse Dijkstra of the static surrogate gives island-aware scheduling.
  // It is NOT an admissible time bound in arbitrary wind/current fields.
  std::pmr::vector<std::pmr::vector<Arc>> reverse(
      g.vertices.get_allocator().resource());
  reverse.resize(g.vertices.size());
  for (std::uint32_t i = 0; i < g.links.size(); ++i)
    for (auto a : g.links[i]) reverse[a.to].push_back({i, a.length, a.guide});
  g.guide.assign(g.vertices.size(), INFINITY);
  g.guide[1] = 0;
  using Pair = std::pair<double, std::uint32_t>;
  std::priority_queue<Pair, std::pmr::vector<Pair>, std::greater<Pair>> q(
      std::greater<Pair>{},
      std::pmr::vector<Pair>{g.vertices.get_allocator().resource()});
  q.emplace(0, 1);
  while (!q.empty()) {
    auto [d, i] = q.top();
    q.pop();
    if (d != g.guide[i]) continue;
    for (auto a : reverse[i])
      if (d + a.guide < g.guide[a.to]) {
        g.guide[a.to] = d + a.guide;
        q.emplace(g.guide[a.to], a.to);
      }
  }
  work.d.waypoints =
      std::max<std::uint64_t>(work.d.waypoints, g.vertices.size());
}

struct Label {
  in::MotionState state;
  std::uint32_t parent{none}, vertex{};
  double heading{};
  wr::Duration duration{};
  bool wait{}, direct{};
};
void appendLabel(std::pmr::deque<Label>& labels, Label label, Work& work) {
  if (labels.size() >= std::min(work.options.maximumLabels,
                                work.request.limits.maximumRetainedStates))
    throw Stop{wr::RoutingStatus::ResourceLimitReached,
               "Atlas retained-label allowance"};
  labels.push_back(std::move(label));
  work.d.labelsPeak = std::max<std::uint64_t>(work.d.labelsPeak, labels.size());
}
std::optional<in::MotionCandidate> waitAt(const wr::RoutingRequest& r,
                                          const wr::RoutingEnvironment& env,
                                          const in::MotionState& from,
                                          wr::Duration delay,
                                          wr::RoutingDiagnostics& diagnostics) {
  if (from.time + delay > r.departure + r.limits.maximumRouteDuration)
    return {};
  auto next = in::checkedWait(r, env, from, delay, diagnostics);
  // Chronological validation restarts the propulsion dwell clock following
  // a stationary hold. Keep the search Markov state consistent with replay.
  if (next) next->state.modeDuration = {};
  return next;
}
using Key = std::tuple<std::uint32_t, std::int64_t, int, int, int, std::string,
                       int, int, std::int64_t, bool>;
Key key(const Label& n, const wr::RoutingRequest& r, const Options& o) {
  return {n.vertex,
          (n.state.time - r.departure).count() / o.arrivalBucket.count(),
          static_cast<int>(n.state.tack),
          static_cast<int>(n.state.mode),
          static_cast<int>(n.state.role),
          n.state.profileIdentity,
          n.state.sailPlan,
          static_cast<int>(wr::normalizeHeading(n.state.incomingHeading) / 15),
          std::min(n.state.modeDuration, r.vessel.propulsion.minimumMotorRun)
              .count(),
          n.state.departureEgressActive};
}
bool same(const in::MotionState& a, const in::MotionState& b) {
  return a.time == b.time && a.mode == b.mode && a.role == b.role &&
         a.sailPlan == b.sailPlan && a.profileIdentity == b.profileIdentity &&
         a.tack == b.tack && a.modeDuration == b.modeDuration &&
         a.motorDuration == b.motorDuration &&
         a.waitDuration == b.waitDuration &&
         a.departureEgressActive == b.departureEgressActive &&
         a.manoeuvres == b.manoeuvres && std::abs(a.fuel - b.fuel) < 1e-8 &&
         std::abs(a.risk - b.risk) < 1e-8 &&
         wr::distanceNm(a.position, b.position) < 1e-7 &&
         std::abs(wr::angularDifferenceDegrees(a.incomingHeading,
                                               b.incomingHeading)) < 1e-7;
}

std::vector<wr::RouteLeg> reconstruct(const std::pmr::deque<Label>& labels,
                                      std::uint32_t id,
                                      const wr::RoutingRequest& r,
                                      const wr::RoutingEnvironment& env,
                                      const wr::VesselPerformanceModel& perf,
                                      Work& work,
                                      wr::RoutingDiagnostics& diag) {
  std::vector<std::uint32_t> chain;
  for (auto i = id; labels[i].parent != none; i = labels[i].parent)
    chain.push_back(i);
  std::reverse(chain.begin(), chain.end());
  std::vector<wr::RouteLeg> legs;
  for (auto i : chain) {
    work.check();
    const auto& n = labels[i];
    const auto& from = labels[n.parent].state;
    if (n.wait) {
      auto m = waitAt(r, env, from, n.duration, diag);
      if (!m || !same(m->state, n.state))
        throw std::runtime_error("Atlas wait reconstruction mismatch");
      legs.push_back(std::move(m->leg));
    } else if (n.direct || (n.state.position == r.destination &&
                            (r.constraints.landSafetyMarginNm > 0 ||
                             r.options.destinationToleranceNm < .008))) {
      // The terminal solver's search window influences its initial guesses.
      // Replay the original window, rather than substituting the solved leg
      // duration (which can select a different root of the boundary problem).
      auto m = in::checkedConnection(r, env, perf, from,
                                     work.options.maximumEdgeDuration, diag);
      if (!m || !same(m->state, n.state))
        throw std::runtime_error("Atlas connection reconstruction mismatch");
      legs.push_back(std::move(m->leg));
    } else {
      wr::RoutingStatus failure = wr::RoutingStatus::Complete;
      auto motions =
          in::checkedMotion(r, env, perf, from, n.heading, n.duration,
                            std::chrono::minutes{5}, diag, &failure);
      auto found =
          std::find_if(motions.begin(), motions.end(),
                       [&](const auto& m) { return same(m.state, n.state); });
      if (found == motions.end())
        throw std::runtime_error("Atlas motion reconstruction mismatch");
      legs.push_back(std::move(found->leg));
    }
  }
  return legs;
}

struct Item {
  double priority{};
  std::uint32_t parent{}, target{};
  std::uint64_t serial{};
  bool wait{};
  bool operator>(const Item& b) const {
    return std::tie(priority, serial) > std::tie(b.priority, b.serial);
  }
};

// Coarse, backward semi-Lagrangian Bellman field. This is guidance only: no
// interpolated path or velocity is ever delivered as boat motion. It supplies
// the temporal information missing from a frozen geography/polar potential.
// The fine search still keeps mode, tack, resource and arrival histories.
struct ValueField {
  std::pmr::vector<double> value;
  Plane plane;
  double west{}, south{}, cell{}, step{};
  unsigned nx{}, ny{}, layers{64};
  bool spatiallyVarying{};
  ValueField(std::pmr::memory_resource* m, const Plane& p)
      : value(m), plane(p) {}
  double spatial(unsigned k, double x, double y) const {
    x = (x - west) / cell;
    y = (y - south) / cell;
    if (x < 0 || y < 0 || x > nx - 1 || y > ny - 1) return INFINITY;
    const auto i = std::min(static_cast<unsigned>(x), nx - 2),
               j = std::min(static_cast<unsigned>(y), ny - 2);
    const double a = x - i, b = y - j;
    const auto at = [&](unsigned u, unsigned v) {
      return value[(k * ny + v) * nx + u];
    };
    return (at(i, j) * (1 - a) + at(i + 1, j) * a) * (1 - b) +
           (at(i, j + 1) * (1 - a) + at(i + 1, j + 1) * a) * b;
  }
  double sample(const in::MotionState& s, const wr::RoutingRequest& r) const {
    if (value.empty()) return INFINITY;
    const auto [x, y] = plane.xy(s.position);
    const auto t = (s.time - r.departure).count() / step;
    if (t < 0 || t > layers) return INFINITY;
    const auto k = std::min(static_cast<unsigned>(t), layers - 1);
    const double a = t - k;
    return spatial(k, x, y) * (1 - a) + spatial(k + 1, x, y) * a;
  }
  void build(const wr::RoutingRequest& r, const wr::RoutingEnvironment& env,
             const wr::VesselPerformanceModel& perf, const Options& o,
             Work& work, double speedHint) {
    if (!o.timeDependentGuidance && !o.weatherLaneProbes) return;
    const auto started = Clock::now();
    bool changing = false;
    const auto first = in::resolveEnvironment(r, env, r.start, r.departure);
    if (first.failureStatus != wr::RoutingStatus::Complete) return;
    auto differs = [&](const in::ResolvedEnvironment& sample) {
      if (sample.failureStatus != first.failureStatus) return true;
      const auto& a = sample.snapshot;
      const auto& b = first.snapshot;
      return wr::vectorMagnitudeKnots(minus(a.wind.velocity, b.wind.velocity)) >
                 .5 ||
             wr::vectorMagnitudeKnots(
                 minus(a.current.velocity, b.current.velocity)) > .5 ||
             a.waves.available != b.waves.available ||
             (a.waves.available &&
              (std::abs(a.waves.significantHeightMetres -
                        b.waves.significantHeightMetres) > .25 ||
               std::abs(a.waves.periodSeconds - b.waves.periodSeconds) > 1 ||
               std::abs(wr::angularDifferenceDegrees(
                   a.waves.directionFromDegrees,
                   b.waves.directionFromDegrees)) > 15));
    };
    for (auto offset : {wr::Duration{3600}, wr::Duration{21600}}) {
      const auto future =
          in::resolveEnvironment(r, env, r.start, r.departure + offset);
      if (differs(future)) changing = true;
      if (env.landAndBoundaries &&
          env.landAndBoundaries->segmentFromKnownSafeForbiddenAt(
              r.start, r.destination, r.departure, 0) !=
              env.landAndBoundaries->segmentFromKnownSafeForbiddenAt(
                  r.start, r.destination, r.departure + offset, 0))
        changing = true;
    }
    for (double side : {-.3, -.2, -.1, .1, .2, .3}) {
      const auto sample = in::resolveEnvironment(
          r, env, plane.point(plane.length / 2, side * plane.length),
          r.departure);
      if (differs(sample)) changing = spatiallyVarying = true;
    }
    if (!o.timeDependentGuidance || (!changing && !o.alwaysBuildWeatherField))
      return;
    cell = std::max(.5, plane.length / 14);
    const auto pad = std::max(4., plane.length * .5);
    west = -pad;
    south = -pad;
    nx = static_cast<unsigned>(std::ceil((plane.length + 2 * pad) / cell)) + 1;
    ny = static_cast<unsigned>(std::ceil(2 * pad / cell)) + 1;
    step = std::max(
        60., std::min<double>(
                 r.limits.maximumRouteDuration.count(),
                 plane.length / std::max(1., speedHint * .5) * 3600 + 21600) /
                 layers);
    value.resize(static_cast<std::size_t>(layers + 1) * nx * ny);
    std::pmr::vector<wr::GeoPoint> points(value.get_allocator().resource());
    for (unsigned j = 0; j < ny; ++j)
      for (unsigned i = 0; i < nx; ++i)
        points.push_back(plane.point(west + i * cell, south + j * cell));
    for (unsigned j = 0; j < ny; ++j)
      for (unsigned i = 0; i < nx; ++i)
        value[(layers * ny + j) * nx + i] =
            wr::distanceNm(points[j * nx + i], r.destination) * 3600 /
            speedHint;
    for (unsigned k = layers; k-- > 0;) {
      const auto time = r.departure + wr::Duration{static_cast<long>(k * step)};
      std::pmr::vector<in::ResolvedEnvironment> samples(
          value.get_allocator().resource());
      samples.reserve(points.size());
      for (auto point : points)
        samples.push_back(in::resolveEnvironment(r, env, point, time));
      for (unsigned j = 0; j < ny; ++j)
        for (unsigned i = 0; i < nx; ++i) {
          work.check();
          const auto index = (k * ny + j) * nx + i;
          const auto point = points[j * nx + i];
          if (wr::distanceNm(point, r.destination) < cell * .75) {
            value[index] = 0;
            continue;
          }
          double best = 1e9;
          auto e = samples[j * nx + i];
          if (o.averageGuidanceTiles && i > 0 && j > 0 && i + 1 < nx &&
              j + 1 < ny) {
            // Box-filter coarse samples only. Detailed physics and acceptance
            // continue sampling the original fields, with no averaging.
            auto wind = e.snapshot.wind.velocity,
                 current = e.snapshot.current.velocity;
            unsigned count = 1;
            for (auto neighbour : {j * nx + i - 1, j * nx + i + 1,
                                   (j - 1) * nx + i, (j + 1) * nx + i}) {
              const auto& s = samples[neighbour];
              if (s.failureStatus != wr::RoutingStatus::Complete) continue;
              wind.eastKnots += s.snapshot.wind.velocity.eastKnots;
              wind.northKnots += s.snapshot.wind.velocity.northKnots;
              current.eastKnots += s.snapshot.current.velocity.eastKnots;
              current.northKnots += s.snapshot.current.velocity.northKnots;
              ++count;
            }
            e.snapshot.wind.velocity = {wind.eastKnots / count,
                                        wind.northKnots / count};
            e.snapshot.current.velocity = {current.eastKnots / count,
                                           current.northKnots / count};
          }
          if (e.failureStatus != wr::RoutingStatus::Complete ||
              (env.landAndBoundaries &&
               env.landAndBoundaries->pointForbidden(point))) {
            value[index] = best;
            continue;
          }
          if (r.options.allowWaiting)
            best = step + value[((k + 1) * ny + j) * nx + i];
          for (unsigned a = 0; a < 24; ++a) {
            const double h = a * 15, angle = wr::trueWindAngleDegrees(
                                         e.snapshot.wind.velocity, h);
            for (const auto& c : perf.candidatesAt(
                     point, time,
                     wr::vectorMagnitudeKnots(e.snapshot.wind.velocity), angle,
                     e.snapshot.waves, wr::PropulsionMode::Sail, {})) {
              ++work.d.valueFieldControls;
              if (!c.valid ||
                  !wr::sailingAngleAllowed(
                      r, perf, point, time,
                      wr::vectorMagnitudeKnots(e.snapshot.wind.velocity), angle,
                      e.snapshot.waves, c.mode, c.role))
                continue;
              auto velocity =
                  wr::speedDirectionToVector(c.speedThroughWaterKnots, h);
              velocity.eastKnots += e.snapshot.current.velocity.eastKnots;
              velocity.northKnots += e.snapshot.current.velocity.northKnots;
              const auto next = wr::destinationPoint(
                  point, wr::vectorDirectionToDegrees(velocity),
                  wr::vectorMagnitudeKnots(velocity) * step / 3600);
              if (env.landAndBoundaries &&
                  env.landAndBoundaries->segmentFromKnownSafeForbiddenAt(
                      point, next, time, r.constraints.landSafetyMarginNm))
                continue;
              const auto [x, y] = plane.xy(next);
              best = std::min(best, step + spatial(k + 1, x, y));
            }
          }
          value[index] = best;
        }
    }
    work.d.valueFieldCells += value.size();
    work.d.valueFieldMs += milliseconds(started);
  }
};

}  // namespace

Result Engine::route(const wr::RoutingRequest& r,
                     const wr::RoutingEnvironment& provided,
                     const Options& o) const {
  Result out;
  auto& result = out.route;
  auto& d = out.atlas;
  result.engineIdentity = kEngineIdentity;
  result.searchVariant = "adaptive-spatial-boundary-value";
  result.status = wr::RoutingStatus::SearchIncomplete;
  const auto started = Clock::now();
  Memory memory(o.memoryBudgetBytes);
  Work work{r, o, d};
  wr::PolarPerformanceModel fallback(r.vessel);
  Performance performance(
      provided.performance ? *provided.performance : fallback, work);
  auto env = provided;
  if (provided.grib) env.grib = std::make_shared<Weather>(provided.grib, work);
  if (provided.climatology)
    env.climatology = std::make_shared<Climatology>(provided.climatology, work);
  if (provided.xtdCurrent)
    env.xtdCurrent = std::make_shared<Current>(provided.xtdCurrent, work);
  if (provided.landAndBoundaries)
    env.landAndBoundaries =
        std::make_shared<Land>(provided.landAndBoundaries, work);
  auto guiding = env;
  if (o.geographyGuidance) {
    auto hint = std::make_shared<Land>(o.geographyGuidance, work, true);
    guiding.landAndBoundaries =
        std::make_shared<GuidanceLand>(env.landAndBoundaries, hint);
  }
  wr::RoutingPreviewPublisher preview(r);
  const auto finish = [&]() {
    d.totalMs = milliseconds(started);
    d.memoryPeakBytes = memory.peak;
    d.searchMs = std::max(0., d.totalMs - d.preprocessingMs -
                                  d.reconstructionMs - d.validationMs);
    result.diagnostics.generatedStates = d.edgeAttempts;
    result.diagnostics.retainedStates = d.labelsPeak;
    result.diagnostics.pruned.cellLabelCap = d.bucketPruned;
    return std::move(out);
  };
  if (o.maximumWaypoints < 32 || o.maximumWaypoints > 20000 ||
      o.neighbours < 4 || o.neighbours > 64 || o.refinementPasses == 0 ||
      o.refinementPasses > 8 || o.labelsPerBucket == 0 ||
      o.labelsPerBucket > 16 || !std::isfinite(o.minimumCellNm) ||
      o.minimumCellNm < .01 || o.minimumCellNm > 20 ||
      !std::isfinite(o.coastalCellNm) || o.coastalCellNm < o.minimumCellNm ||
      o.coastalCellNm > 100 || !std::isfinite(o.domainWidthFraction) ||
      o.domainWidthFraction <= 0 || o.domainWidthFraction > 4 ||
      o.arrivalBucket < wr::Duration{1} || o.waitStep < wr::Duration{1} ||
      o.maximumEdgeDuration < wr::Duration{1} || o.maximumEdgeAttempts == 0 ||
      o.maximumLabels < 2 || o.maximumQueueEntries < 2 ||
      o.memoryBudgetBytes < 4096 || r.limits.maximumGeneratedStates == 0 ||
      r.limits.maximumRetainedStates < 2 ||
      r.limits.maximumRouteDuration <= wr::Duration{} ||
      !std::isfinite(r.options.destinationToleranceNm) ||
      r.options.destinationToleranceNm <= 0 ||
      !std::isfinite(r.constraints.landSafetyMarginNm) ||
      r.constraints.landSafetyMarginNm < 0 ||
      !std::isfinite(r.limits.maximumExplorationDistanceNm) ||
      r.limits.maximumExplorationDistanceNm <= 0) {
    result.status = wr::RoutingStatus::InvalidVesselConfiguration;
    result.message = "Invalid Atlas options or resource limits";
    return finish();
  }
  if (r.objective.kind != wr::ObjectiveKind::Fastest &&
      r.objective.kind != wr::ObjectiveKind::FastestUnderSafetyLimits) {
    result.status = wr::RoutingStatus::InvalidVesselConfiguration;
    result.message =
        "Atlas currently supports deterministic minimum-time objectives";
    return finish();
  }
  try {
    work.check();
    if (!finitePoint(r.start) || !finitePoint(r.destination)) {
      result.status = !finitePoint(r.start)
                          ? wr::RoutingStatus::InvalidStart
                          : wr::RoutingStatus::InvalidDestination;
      result.message = "Atlas supports finite endpoints between 85 S and 85 N";
      return finish();
    }
    if (env.landAndBoundaries) {
      if (env.landAndBoundaries->pointForbidden(r.start)) {
        result.status = wr::RoutingStatus::InvalidStart;
        return finish();
      }
      if (env.landAndBoundaries->pointForbidden(r.destination)) {
        result.status = wr::RoutingStatus::InvalidDestination;
        return finish();
      }
    }
    result.preflight = wr::RoutingEngine{}.preflight(r, env);
    result.warnings = result.preflight->warnings;
    if (!result.preflight->canRoute) {
      result.status = in::failedPreflightStatus(*result.preflight);
      return finish();
    }
    const Plane plane(r.start, r.destination);
    if (env.xtdCurrent && r.environment.useCurrent &&
        result.preflight->coverage.currentFallbackNeeded) {
      const double margin = std::min(
          30., std::max(1., plane.length * o.domainWidthFraction * 2 / 60));
      const auto endLongitude =
          r.start.longitude +
          wr::normalizeLongitude(r.destination.longitude - r.start.longitude);
      wr::GeoEnvelope area{
          wr::normalizeLongitude(std::min(r.start.longitude, endLongitude) -
                                 margin),
          std::max(-85.,
                   std::min(r.start.latitude, r.destination.latitude) - margin),
          wr::normalizeLongitude(std::max(r.start.longitude, endLongitude) +
                                 margin),
          std::min(85., std::max(r.start.latitude, r.destination.latitude) +
                            margin)};
      const double width = area.west <= area.east ? area.east - area.west
                                                  : 360 - area.west + area.east;
      if (width * (area.north - area.south) >
              r.limits.maximumPredictionAreaSquareDegrees ||
          r.limits.maximumCurrentCacheSlices < 2)
        throw Stop{wr::RoutingStatus::ResourceLimitReached,
                   "Atlas initial current coverage allowance"};
      const auto horizon = std::min(r.limits.maximumRouteDuration,
                                    wr::Duration{std::chrono::hours{48}});
      const auto step = std::max(
          wr::Duration{std::chrono::hours{1}},
          wr::Duration{static_cast<std::int64_t>(
              horizon.count() / (r.limits.maximumCurrentCacheSlices - 1) + 1)});
      const auto coverage = env.xtdCurrent->ensureCoverage(
          area, r.departure, r.departure + horizon, step, r.cancellation);
      work.check();
      if (coverage.status != wr::CoverageStatus::Ready)
        throw Stop{coverage.status == wr::CoverageStatus::Cancelled
                       ? wr::RoutingStatus::Cancelled
                   : coverage.status == wr::CoverageStatus::ResourceLimit
                       ? wr::RoutingStatus::ResourceLimitReached
                       : wr::RoutingStatus::WeatherCoverageInsufficient,
                   "Atlas current prediction coverage unavailable"};
    }
    if (plane.length < 1e-9) {
      result.message = "Start and destination coincide";
      return finish();
    }
    const auto maxLabels =
        std::min(o.maximumLabels, r.limits.maximumRetainedStates);
    double speedHint = 6;
    const auto weather = in::resolveEnvironment(r, env, r.start, r.departure);
    if (weather.failureStatus != wr::RoutingStatus::Complete) {
      result.status = weather.failureStatus;
      result.message = weather.failureReason;
      return finish();
    }
    for (double a = 0; a <= 180; a += 10)
      for (const auto& c : performance.candidatesAt(
               r.start, r.departure,
               wr::vectorMagnitudeKnots(weather.snapshot.wind.velocity), a,
               weather.snapshot.waves, wr::PropulsionMode::Sail, {}))
        if (c.valid)
          speedHint = std::max(
              speedHint,
              c.speedThroughWaterKnots +
                  wr::vectorMagnitudeKnots(weather.snapshot.current.velocity));
    const auto accept = [&](const std::pmr::deque<Label>& labels,
                            std::uint32_t id) {
      auto before = Clock::now();
      auto legs = reconstruct(labels, id, r, env, performance, work,
                              result.diagnostics);
      d.reconstructionMs += milliseconds(before);
      before = Clock::now();
      ++d.validationCandidates;
      auto valid = wr::RouteValidator{}.validate(r, env, performance, legs,
                                                 &result.diagnostics);
      result.diagnostics.validationSamples += valid.samples;
      d.validationMs += milliseconds(before);
      if (!valid.passed) {
        ++d.validationFailures;
        if (result.diagnostics.stageStopReasons.size() < 32)
          result.diagnostics.stageStopReasons.push_back("Atlas candidate: " +
                                                        valid.failureReason);
        if (env.landAndBoundaries &&
            env.landAndBoundaries->validationFailureRequiresSearchEscalation())
          throw Stop{
              wr::RoutingStatus::ValidationFailure,
              "Atlas candidate requires authoritative search escalation"};
        return false;
      }
      const auto elapsed = (legs.back().endTime - r.departure).count();
      if (result.validation.passed && elapsed >= result.metrics.elapsed.count())
        return true;
      if (!result.validation.passed) d.firstValidMs = milliseconds(started);
      result.legs = std::move(legs);
      result.validation = std::move(valid);
      result.status = wr::RoutingStatus::Complete;
      result.message = "Complete (Atlas experimental)";
      result.metrics = {};
      result.environment = {};
      result.sourceTransitions.clear();
      result.warnings = result.preflight->warnings;
      result.warnings.push_back(
          {wr::RoutingWarningCode::SearchPruned,
           "Experimental Atlas uses a finite adaptive roadmap and time/heading "
           "label caps; completeness and global optimality are not "
           "established"});
      in::summariseRoute(result);
      d.incumbentSeconds.push_back(elapsed);
      if (o.onIncumbent) {
        try {
          o.onIncumbent(result);
        } catch (...) {
          // A consumer observes a validated snapshot; it is not an acceptance
          // gate and cannot invalidate routing by throwing an exception.
        }
      }
      return true;
    };
    in::MotionState root;
    root.position = r.start;
    root.time = r.departure;
    if (env.landAndBoundaries && r.constraints.landSafetyMarginNm > 0)
      root.departureEgressActive =
          env.landAndBoundaries->distanceToForbiddenNm(r.start) <
          r.constraints.landSafetyMarginNm;
    // A direct boundary-value solve is a candidate, not an optimality proof.
    // Keep searching geography/weather alternatives even when it succeeds.
    std::vector<wr::GeoPoint> seeds;
    {
      std::pmr::deque<Label> labels(&memory);
      appendLabel(labels, {root}, work);
      for (auto& m : shoot(r, env, performance, root, r.destination, o, work,
                           result.diagnostics)) {
        if (wr::distanceNm(m.state.position, r.destination) >
            r.options.destinationToleranceNm)
          continue;
        appendLabel(labels,
                    {m.state, 0, 1, m.leg.courseThroughWaterDegrees,
                     m.state.time - root.time},
                    work);
        d.labelsPeak = std::max<std::uint64_t>(d.labelsPeak, labels.size());
        accept(labels, static_cast<std::uint32_t>(labels.size() - 1));
      }
      if (result.validation.passed && o.improvementAttempts == 0)
        return finish();
      // Time is a decision variable even when movement is feasible. Probe a
      // bounded departure schedule independently of the spatial graph. This
      // captures non-FIFO gates without forcing many tiny spatial loops.
      if (r.options.allowWaiting &&
          (!result.validation.passed || r.environment.useCurrent) &&
          (!env.landAndBoundaries || !env.landAndBoundaries->segmentForbidden(
                                         r.start, r.destination, 0))) {
        const auto quantum = std::max(
            o.waitStep, wr::Duration{r.options.maximumWait.count() / 32});
        for (auto delay = quantum; delay <= r.options.maximumWait;
             delay += quantum) {
          if (result.validation.passed && delay >= result.metrics.elapsed)
            break;
          work.edge();
          auto wait = waitAt(r, env, root, delay, result.diagnostics);
          if (!wait) continue;
          appendLabel(labels, {wait->state, 0, 0, 0, delay, true}, work);
          const auto parent = static_cast<std::uint32_t>(labels.size() - 1);
          for (auto& m : shoot(r, env, performance, wait->state, r.destination,
                               o, work, result.diagnostics)) {
            appendLabel(labels,
                        {m.state, parent, 1, m.leg.courseThroughWaterDegrees,
                         m.state.time - wait->state.time},
                        work);
            d.labelsPeak = std::max<std::uint64_t>(d.labelsPeak, labels.size());
            accept(labels, static_cast<std::uint32_t>(labels.size() - 1));
          }
          if (result.validation.passed && o.improvementAttempts == 0)
            return finish();
        }
      }
      // A long plotting chord is checked at its departure instant. For a
      // timed gate it cannot be accepted merely because the boat would cross
      // the gate later. Move an established departure hold forward to a safe
      // spatial event, then optimise the remaining hold there. This also
      // avoids representing useful waiting by hundreds of sailing loops.
      if ((result.validation.passed && !result.legs.empty() &&
           result.legs.front().stationaryWait) ||
          (!result.validation.passed && env.landAndBoundaries &&
           !env.landAndBoundaries->segmentForbidden(r.start, r.destination,
                                                    0))) {
        double lo = 0, hi = 1;
        std::optional<in::MotionCandidate> prefix;
        for (unsigned i = 0; i < 12; ++i) {
          const auto fraction = (lo + hi) / 2;
          auto trial = shoot(r, env, performance, root,
                             plane.point(plane.length * fraction, 0), o, work,
                             result.diagnostics);
          if (trial.empty())
            hi = fraction;
          else {
            lo = fraction;
            prefix = std::move(trial.front());
          }
        }
        if (prefix && lo > .001) {
          appendLabel(
              labels,
              {prefix->state, 0, 2, prefix->leg.courseThroughWaterDegrees,
               prefix->state.time - root.time},
              work);
          const auto prefixId = static_cast<std::uint32_t>(labels.size() - 1);
          if (!r.options.allowWaiting) {
            // Stationary waiting and sailing a loiter are different controls.
            // Try a continuous out-and-back loop before the blocked event;
            // refine a successful loop radius and replay every manoeuvre.
            auto tryLoop = [&](double radius) {
              const auto point = plane.point(plane.length * lo - radius, 0);
              auto away = shoot(r, env, performance, prefix->state, point, o,
                                work, result.diagnostics);
              if (away.empty()) return false;
              auto back =
                  shoot(r, env, performance, away.front().state,
                        prefix->state.position, o, work, result.diagnostics);
              if (back.empty()) return false;
              auto last = shoot(r, env, performance, back.front().state,
                                r.destination, o, work, result.diagnostics);
              if (last.empty()) return false;
              const auto a = static_cast<std::uint32_t>(labels.size());
              appendLabel(labels,
                          {away.front().state, prefixId, 2,
                           away.front().leg.courseThroughWaterDegrees,
                           away.front().state.time - prefix->state.time},
                          work);
              const auto b = static_cast<std::uint32_t>(labels.size());
              appendLabel(labels,
                          {back.front().state, a, 2,
                           back.front().leg.courseThroughWaterDegrees,
                           back.front().state.time - away.front().state.time},
                          work);
              appendLabel(labels,
                          {last.front().state, b, 1,
                           last.front().leg.courseThroughWaterDegrees,
                           last.front().state.time - back.front().state.time},
                          work);
              d.labelsPeak =
                  std::max<std::uint64_t>(d.labelsPeak, labels.size());
              return accept(labels,
                            static_cast<std::uint32_t>(labels.size() - 1));
            };
            double low = 0;
            for (double radius = .5; radius <= std::max(4., plane.length);
                 radius *= 2) {
              if (tryLoop(radius)) {
                double high = radius;
                for (unsigned i = 0; i < 8; ++i) {
                  auto mid = (low + high) / 2;
                  if (tryLoop(mid))
                    high = mid;
                  else
                    low = mid;
                }
                break;
              }
              low = radius;
            }
          }
          const auto quantum = std::max(
              o.waitStep, wr::Duration{r.options.maximumWait.count() / 32});
          const auto first = wr::Duration{
              ((prefix->state.time - r.departure).count() / quantum.count() +
               1) *
              quantum.count()};
          auto failedUntil = prefix->state.time - r.departure;
          auto tryHold = [&](wr::Duration until) {
            const auto delay = r.departure + until - prefix->state.time;
            if (delay <= wr::Duration{} || delay > r.options.maximumWait)
              return false;
            work.edge();
            auto wait =
                waitAt(r, env, prefix->state, delay, result.diagnostics);
            if (!wait) return false;
            appendLabel(labels, {wait->state, prefixId, 2, 0, delay, true},
                        work);
            const auto waitId = static_cast<std::uint32_t>(labels.size() - 1);
            auto last = shoot(r, env, performance, wait->state, r.destination,
                              o, work, result.diagnostics);
            bool accepted = false;
            for (auto& m : last) {
              appendLabel(labels,
                          {m.state, waitId, 1, m.leg.courseThroughWaterDegrees,
                           m.state.time - wait->state.time},
                          work);
              d.labelsPeak =
                  std::max<std::uint64_t>(d.labelsPeak, labels.size());
              accepted =
                  accept(labels,
                         static_cast<std::uint32_t>(labels.size() - 1)) ||
                  accepted;
            }
            return accepted;
          };
          for (auto until = first; r.options.allowWaiting &&
                                   until <= prefix->state.time - r.departure +
                                                r.options.maximumWait;
               until += quantum) {
            if (result.validation.passed && until >= result.metrics.elapsed)
              break;
            if (!tryHold(until)) {
              failedUntil = until;
              continue;
            }
            // Refine an observed infeasible/feasible time interval. This is
            // local candidate improvement, not an assumption that arbitrary
            // time-dependent feasibility is globally monotone.
            auto feasibleUntil = until;
            for (unsigned i = 0;
                 i < 10 && feasibleUntil - failedUntil > wr::Duration{1}; ++i) {
              auto mid = failedUntil + (feasibleUntil - failedUntil) / 2;
              if (tryHold(mid))
                feasibleUntil = mid;
              else
                failedUntil = mid;
            }
            break;
          }
        }
      }
      seeds = envelopeSeeds(r, env, performance);
      std::optional<wr::GeoPoint> bestVia;
      auto tryVia = [&](wr::GeoPoint point) {
        const auto previous = result.validation.passed
                                  ? result.metrics.elapsed.count()
                                  : std::numeric_limits<std::int64_t>::max();
        for (auto& m : shoot(r, env, performance, root, point, o, work,
                             result.diagnostics)) {
          appendLabel(labels,
                      {m.state, 0, 2, m.leg.courseThroughWaterDegrees,
                       m.state.time - root.time},
                      work);
          const auto parent = static_cast<std::uint32_t>(labels.size() - 1);
          for (auto& last : shoot(r, env, performance, m.state, r.destination,
                                  o, work, result.diagnostics)) {
            appendLabel(
                labels,
                {last.state, parent, 1, last.leg.courseThroughWaterDegrees,
                 last.state.time - m.state.time},
                work);
            d.labelsPeak = std::max<std::uint64_t>(d.labelsPeak, labels.size());
            accept(labels, static_cast<std::uint32_t>(labels.size() - 1));
          }
        }
        if (result.validation.passed &&
            result.metrics.elapsed.count() < previous) {
          bestVia = point;
          return true;
        }
        return false;
      };
      for (auto point : seeds) {
        tryVia(point);
        if (result.validation.passed && o.improvementAttempts == 0)
          return finish();
      }
      if (!result.validation.passed) {
        // A locally optimal homogeneous polar envelope cannot discover a
        // detached weather basin or a route around an isolated moving hazard.
        // Admit broad spatial decisions independently of that envelope, then
        // optimise a feasible two-leg shape with real time-dependent solves.
        for (double fraction : {.25, .5, .75})
          for (double side : {-.05, .05, -.1, .1, -.2, .2, -.4, .4}) {
            auto point =
                plane.point(plane.length * fraction, plane.length * side);
            seeds.push_back(point);
            tryVia(point);
            if (result.validation.passed && o.improvementAttempts == 0)
              return finish();
          }
        if (bestVia && o.improvementAttempts > 0) {
          double step = std::max(.05, plane.length * .05);
          for (unsigned iteration = 0; iteration < 6; ++iteration) {
            const auto [x, y] = plane.xy(*bestVia);
            bool improved = false;
            for (auto [dx, dy] :
                 std::array<std::pair<double, double>, 8>{{{-1, -1},
                                                           {-1, 0},
                                                           {-1, 1},
                                                           {0, -1},
                                                           {0, 1},
                                                           {1, -1},
                                                           {1, 0},
                                                           {1, 1}}})
              improved =
                  tryVia(plane.point(x + dx * step, y + dy * step)) || improved;
            if (!improved) step *= .5;
          }
          seeds.push_back(*bestVia);
        }
      }
    }
    ValueField field(&memory, plane);
    {
      const auto prep = Clock::now();
      field.build(r, guiding, performance, o, work, speedHint);
      d.preprocessingMs += milliseconds(prep);
    }
    if (o.weatherLaneProbes && field.spatiallyVarying) {
      // A fast weather band may justify two separate spatial turns even when
      // a direct route exists. Optimise a three-leg family in continuous
      // entry, exit and lateral displacement; the field only motivates the
      // family. Every evaluation is original-provider physical motion.
      std::pmr::deque<Label> lanes(&memory);
      appendLabel(lanes, {root}, work);
      std::optional<std::array<double, 3>> bestLane;
      auto tryLane = [&](std::array<double, 3> shape) {
        ++d.laneProbes;
        if (shape[0] < .03 * plane.length || shape[1] > .97 * plane.length ||
            shape[1] - shape[0] < .05 * plane.length)
          return false;
        const auto previous = result.validation.passed
                                  ? result.metrics.elapsed.count()
                                  : std::numeric_limits<std::int64_t>::max();
        auto first =
            shoot(r, env, performance, root, plane.point(shape[0], shape[2]), o,
                  work, result.diagnostics);
        for (auto& a : first) {
          appendLabel(lanes,
                      {a.state, 0, 2, a.leg.courseThroughWaterDegrees,
                       a.state.time - root.time},
                      work);
          const auto parent = static_cast<std::uint32_t>(lanes.size() - 1);
          auto middle = shoot(r, env, performance, a.state,
                              plane.point(shape[1], shape[2]), o, work,
                              result.diagnostics);
          for (auto& b : middle) {
            appendLabel(lanes,
                        {b.state, parent, 2, b.leg.courseThroughWaterDegrees,
                         b.state.time - a.state.time},
                        work);
            const auto second = static_cast<std::uint32_t>(lanes.size() - 1);
            auto last = shoot(r, env, performance, b.state, r.destination, o,
                              work, result.diagnostics);
            for (auto& c : last) {
              appendLabel(lanes,
                          {c.state, second, 1, c.leg.courseThroughWaterDegrees,
                           c.state.time - b.state.time},
                          work);
              accept(lanes, static_cast<std::uint32_t>(lanes.size() - 1));
            }
          }
        }
        if (result.validation.passed &&
            result.metrics.elapsed.count() < previous) {
          bestLane = shape;
          return true;
        }
        return false;
      };
      for (double fraction : {.1, .25, .4})
        for (double side : {-.1, .1, -.2, .2, -.3, .3, -.4, .4}) {
          tryLane({plane.length * fraction, plane.length * (1 - fraction),
                   plane.length * side});
          if (result.validation.passed && o.improvementAttempts == 0)
            return finish();
        }
      if (bestLane && o.improvementAttempts > 0) {
        double step = std::max(.05, plane.length * .05);
        for (unsigned iteration = 0;
             iteration < 24 && step > std::max(.025, plane.length * .001);
             ++iteration) {
          bool improved = false;
          for (unsigned coordinate = 0; coordinate < 3; ++coordinate) {
            const auto shape = *bestLane;
            for (double direction : {-1., 1.}) {
              auto next = shape;
              next[coordinate] += direction * step;
              improved = tryLane(next) || improved;
            }
          }
          if (!improved) step *= .5;
        }
        // Keep the subsequent roadmap identical to the no-lane search.
        // A new incumbent should not displace its finite set of graph nodes.
      }
    }
    if (o.weatherFirstPilot && !field.value.empty()) {
      // Feedback extraction is a different candidate architecture from label
      // search: follow a coarse weather value function with a single physical
      // lineage. It can provide an incumbent before any detailed roadmap is
      // constructed, and useful spatial anchors even if final replay fails.
      for (double scale : {.5, .25}) {
        std::pmr::deque<Label> pilot(&memory);
        appendLabel(pilot, {root}, work);
        for (unsigned iteration = 0; iteration < 512; ++iteration) {
          work.check();
          const auto parent = static_cast<std::uint32_t>(pilot.size() - 1);
          const auto from = pilot.back().state;
          if (from.time >= r.departure + r.limits.maximumRouteDuration) break;
          const auto remaining = wr::distanceNm(from.position, r.destination);
          if (remaining < std::max(20., field.cell * 2)) {
            auto last = shoot(r, env, performance, from, r.destination, o, work,
                              result.diagnostics);
            bool accepted = false;
            for (auto& m : last) {
              if (pilot.size() >= maxLabels)
                throw Stop{wr::RoutingStatus::ResourceLimitReached,
                           "Atlas pilot label allowance"};
              appendLabel(pilot,
                          {m.state, parent, 1, m.leg.courseThroughWaterDegrees,
                           m.state.time - from.time},
                          work);
              d.labelsPeak =
                  std::max<std::uint64_t>(d.labelsPeak, pilot.size());
              accepted =
                  accept(pilot, static_cast<std::uint32_t>(pilot.size() - 1)) ||
                  accepted;
            }
            if (accepted) break;
            pilot.resize(parent + 1);
          }
          const auto e =
              in::resolveEnvironment(r, env, from.position, from.time);
          if (e.failureStatus != wr::RoutingStatus::Complete) break;
          auto step = wr::Duration{
              static_cast<long>(std::clamp(field.step * scale, 120., 3600.))};
          step = std::min(
              step, r.departure + r.limits.maximumRouteDuration - from.time);
          struct Control {
            double score{}, heading{};
          };
          std::vector<Control> controls;
          for (unsigned i = 0; i < 48; ++i) {
            const double h = i * 7.5, angle = wr::trueWindAngleDegrees(
                                          e.snapshot.wind.velocity, h);
            for (const auto& c : performance.candidatesAt(
                     from.position, from.time,
                     wr::vectorMagnitudeKnots(e.snapshot.wind.velocity), angle,
                     e.snapshot.waves, from.mode, from.modeDuration)) {
              if (!c.valid ||
                  !wr::sailingAngleAllowed(
                      r, performance, from.position, from.time,
                      wr::vectorMagnitudeKnots(e.snapshot.wind.velocity), angle,
                      e.snapshot.waves, c.mode, c.role))
                continue;
              auto v = wr::speedDirectionToVector(c.speedThroughWaterKnots, h);
              v.eastKnots += e.snapshot.current.velocity.eastKnots;
              v.northKnots += e.snapshot.current.velocity.northKnots;
              auto predicted = from;
              predicted.time += step;
              predicted.position = wr::destinationPoint(
                  from.position, wr::vectorDirectionToDegrees(v),
                  wr::vectorMagnitudeKnots(v) * step.count() / 3600);
              auto score = field.sample(predicted, r);
              if (!std::isfinite(score) || score > 1e8) continue;
              controls.push_back(
                  {score +
                       .01 * wr::distanceNm(predicted.position, r.destination) *
                           3600 / speedHint,
                   h});
            }
          }
          std::stable_sort(controls.begin(), controls.end(),
                           [](auto a, auto b) { return a.score < b.score; });
          std::optional<in::MotionCandidate> best;
          double bestScore = INFINITY;
          for (unsigned reduction = 0; reduction < 4 && !best; ++reduction) {
            for (std::size_t i = 0;
                 i < std::min<std::size_t>(4, controls.size()); ++i) {
              work.edge();
              ++d.shootingIntegrations;
              wr::RoutingStatus failure = wr::RoutingStatus::Complete;
              auto moves = in::checkedMotion(
                  r, env, performance, from, controls[i].heading, step,
                  std::chrono::minutes{5}, result.diagnostics, &failure);
              for (auto& m : moves) {
                const auto score =
                    field.sample(m.state, r) +
                    .01 * wr::distanceNm(m.state.position, r.destination) *
                        3600 / speedHint;
                if (score < bestScore) {
                  bestScore = score;
                  best = std::move(m);
                }
              }
            }
            if (!best)
              step = wr::Duration{std::max<std::int64_t>(30, step.count() / 2)};
          }
          if (!best) break;
          if (pilot.size() >= maxLabels)
            throw Stop{wr::RoutingStatus::ResourceLimitReached,
                       "Atlas pilot label allowance"};
          const auto elapsed = best->state.time - from.time;
          appendLabel(pilot,
                      {best->state, parent, 2,
                       best->leg.courseThroughWaterDegrees, elapsed},
                      work);
          ++d.pilotLegs;
          d.labelsPeak = std::max<std::uint64_t>(d.labelsPeak, pilot.size());
          if (iteration % 8 == 0 && seeds.size() < 80)
            seeds.push_back(best->state.position);
        }
      }
      if (result.validation.passed && o.improvementAttempts == 0)
        return finish();
    }
    for (unsigned pass = 0; pass < o.refinementPasses; ++pass) {
      work.check();
      ++d.passes;
      Options policy = o;
      policy.minimumCellNm =
          std::max(.01, o.minimumCellNm / std::pow(2., pass));
      policy.coastalCellNm =
          std::max(policy.minimumCellNm, o.coastalCellNm / std::pow(2., pass));
      const auto prep = Clock::now();
      Graph g(&memory);
      const bool authoritativePass =
          o.geographyGuidance && pass > 0 && !result.validation.passed;
      buildGraph(g, plane, r, authoritativePass ? env : guiding, policy, work,
                 pass, seeds, performance, speedHint);
      d.preprocessingMs += milliseconds(prep);
      const auto search = Clock::now();
      const auto totalAllowance =
          std::min(o.maximumEdgeAttempts, r.limits.maximumGeneratedStates);
      const auto passEnd =
          d.edgeAttempts +
          (totalAllowance - std::min(totalAllowance, d.edgeAttempts)) /
              (o.refinementPasses - pass);
      std::pmr::deque<Label> labels(&memory);
      appendLabel(labels, {root}, work);
      std::pmr::map<Key, std::pmr::vector<std::uint32_t>> buckets(&memory);
      std::priority_queue<Item, std::pmr::vector<Item>, std::greater<Item>>
          queue(std::greater<Item>{}, std::pmr::vector<Item>{&memory});
      std::uint64_t serial = 0;
      std::optional<std::uint64_t> improveEnd;
      if (result.validation.passed)
        improveEnd = d.edgeAttempts + o.improvementAttempts;
      const char* retryReason = nullptr;
      const auto canRetryGuidance = [&] {
        return o.geographyGuidance && !authoritativePass &&
               !result.validation.passed && pass + 1 < o.refinementPasses;
      };
      auto enqueue = [&](std::uint32_t id) {
        const auto& n = labels[id];
        const double elapsed = (n.state.time - r.departure).count();
        auto push = [&](std::uint32_t to, double length, bool wait) {
          if (queue.size() >= o.maximumQueueEntries) {
            if (canRetryGuidance()) {
              retryReason =
                  "Atlas guided queue allowance; retrying original geography";
              return;
            }
            throw Stop{wr::RoutingStatus::ResourceLimitReached,
                       "Atlas queue allowance"};
          }
          double h = g.guide[to];
          if (!std::isfinite(h))
            h = wr::distanceNm(g.vertices[to].point, r.destination);
          auto heuristic = h * 3600 / speedHint;
          if (!field.value.empty() && !authoritativePass) {
            auto predicted = n.state;
            predicted.position = g.vertices[to].point;
            predicted.time +=
                wr::Duration{static_cast<long>(length * 3600 / speedHint)};
            const auto temporal = field.sample(predicted, r);
            if (std::isfinite(temporal) && temporal < 1e8) heuristic = temporal;
          }
          queue.push({elapsed + length * 3600 / speedHint + 1.08 * heuristic,
                      id, to, serial++, wait});
        };
        for (auto arc : g.links[n.vertex])
          if (arc.to != 0) push(arc.to, arc.guide, false);
        if (r.options.allowWaiting &&
            n.state.waitDuration + o.waitStep <= r.options.maximumWait)
          push(n.vertex, o.waitStep.count() * speedHint / 3600, true);
        d.queuePeak = std::max<std::uint64_t>(d.queuePeak, queue.size());
      };
      enqueue(0);
      while (!queue.empty()) {
        if (retryReason) break;
        work.check();
        if (d.edgeAttempts >= passEnd) break;
        if (improveEnd && d.edgeAttempts >= *improveEnd) break;
        auto item = queue.top();
        queue.pop();
        // Copy the parent: deque insertion preserves references, but this
        // also prevents accidental invalidation during later representations.
        const auto parent = labels[item.parent];
        if (result.validation.passed &&
            parent.state.time >= r.departure + result.metrics.elapsed)
          continue;
        ++d.expandedLabels;
        std::vector<in::MotionCandidate> motions;
        if (item.wait) {
          work.edge();
          if (auto m =
                  waitAt(r, env, parent.state, o.waitStep, result.diagnostics))
            motions.push_back(std::move(*m));
        } else {
          motions =
              shoot(r, env, performance, parent.state,
                    g.vertices[item.target].point, o, work, result.diagnostics);
        }
        for (auto& m : motions) {
          if (m.state.time > r.departure + r.limits.maximumRouteDuration)
            continue;
          if (result.validation.passed &&
              m.state.time >= r.departure + result.metrics.elapsed)
            continue;
          const auto remaining =
              wr::distanceNm(m.state.position, r.destination);
          result.diagnostics.closestApproachNm =
              std::min(result.diagnostics.closestApproachNm, remaining);
          Label n{m.state,
                  item.parent,
                  item.target,
                  m.leg.courseThroughWaterDegrees,
                  m.state.time - parent.state.time,
                  item.wait};
          if (labels.size() >= maxLabels) {
            if (canRetryGuidance()) {
              retryReason =
                  "Atlas guided label allowance; retrying original geography";
              break;
            }
            throw Stop{wr::RoutingStatus::ResourceLimitReached,
                       "Atlas retained-label allowance"};
          }
          if (item.target == 1 &&
              remaining <= r.options.destinationToleranceNm) {
            appendLabel(labels, n, work);
            const auto id = static_cast<std::uint32_t>(labels.size() - 1);
            d.labelsPeak = std::max<std::uint64_t>(d.labelsPeak, labels.size());
            if (accept(labels, id)) {
              if (!improveEnd)
                improveEnd = d.edgeAttempts + o.improvementAttempts;
              if (o.improvementAttempts == 0) {
                d.searchMs += milliseconds(search);
                return finish();
              }
            }
            continue;
          }
          auto& bucket = buckets[key(n, r, o)];
          // This is an explicitly approximate label cap, not an assertion of
          // FIFO dominance. Different time buckets always remain distinct.
          if (bucket.size() >= o.labelsPerBucket) {
            ++d.bucketPruned;
            continue;
          }
          appendLabel(labels, std::move(n), work);
          auto id = static_cast<std::uint32_t>(labels.size() - 1);
          bucket.push_back(id);
          d.labelsPeak = std::max<std::uint64_t>(d.labelsPeak, labels.size());
          enqueue(id);
        }
        if ((d.edgeAttempts & 127) == 0) {
          if (r.progress) try {
              r.progress({wr::RoutingProgressStage::GraphFallback, pass + 1,
                          o.refinementPasses, d.edgeAttempts, labels.size(),
                          result.diagnostics.landChecks,
                          result.diagnostics.closestApproachNm, 100});
            } catch (...) {
            }
          preview.publish([&] {
            wr::RoutingPreview v;
            v.stage = wr::RoutingProgressStage::GraphFallback;
            v.time = parent.state.time;
            v.frontier.push_back(parent.state.position);
            for (auto i = item.parent;
                 i != none &&
                 v.path.size() < wr::RoutingPreviewPublisher::maximumPathPoints;
                 i = labels[i].parent)
              v.path.push_back(labels[i].state.position);
            std::reverse(v.path.begin(), v.path.end());
            return v;
          });
        }
      }
      d.searchMs += milliseconds(search);
      if (retryReason)
        result.diagnostics.stageStopReasons.push_back(retryReason);
    }
    if (!result.validation.passed) {
      result.status = wr::RoutingStatus::SearchIncomplete;
      result.message =
          "Atlas finite roadmap search exhausted; feasibility is unresolved";
    }
  } catch (const Stop& s) {
    if (s.status == wr::RoutingStatus::Cancelled) {
      result.legs.clear();
      result.validation = {};
      result.metrics = {};
      result.status = s.status;
      result.message = s.reason;
    } else if (!result.validation.passed) {
      result.status = s.status;
      result.message = s.reason;
    }
    result.diagnostics.stageStopReasons.push_back(s.reason);
  } catch (const std::bad_alloc&) {
    if (!result.validation.passed) {
      result.status = wr::RoutingStatus::ResourceLimitReached;
      result.message = "Atlas search memory allowance";
    }
    result.diagnostics.resourceLimitEvents.push_back(
        "Atlas search memory allowance");
  } catch (const std::exception& e) {
    // Provider/reconstruction failures must never be silently converted to a
    // successful candidate. A previously independently validated incumbent
    // can survive exhausted search resources, not changed provider semantics.
    result.legs.clear();
    result.validation = {};
    result.metrics = {};
    result.status = wr::RoutingStatus::InternalError;
    result.message = e.what();
  }
  return finish();
}
}  // namespace atlas_routing
