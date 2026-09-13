// SPDX-License-Identifier: Apache-2.0
#include "supercpn/weather_routing/QuickEngine.h"
#include "MotionKernel.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <deque>
#include <memory_resource>
#include <limits>
#include <numeric>
#include <stdexcept>
#include <tuple>

namespace supercpn::weather_routing {
namespace {
constexpr std::uint32_t none = ~std::uint32_t{0};
struct WorkLimit : std::runtime_error {
  using std::runtime_error::runtime_error;
};
struct Cancelled {};

class BudgetResource final : public std::pmr::memory_resource {
public:
  explicit BudgetResource(std::size_t limit) : limit_(limit) {}
  std::uint64_t peak{};

private:
  void* do_allocate(std::size_t bytes, std::size_t alignment) override {
    if (bytes > limit_ - used_) throw std::bad_alloc();
    void* p = std::pmr::new_delete_resource()->allocate(bytes, alignment);
    used_ += bytes;
    peak = std::max<std::uint64_t>(peak, used_);
    return p;
  }
  void do_deallocate(void* p, std::size_t n, std::size_t a) override {
    std::pmr::new_delete_resource()->deallocate(p, n, a);
    used_ -= n;
  }
  bool do_is_equal(
      const std::pmr::memory_resource& other) const noexcept override {
    return this == &other;
  }
  std::size_t limit_, used_{};
};

struct Work {
  const RoutingRequest& request;
  const QuickRoutingOptions& options;
  QuickRoutingDiagnostics& diagnostics;
  void check() const {
    if (request.cancellation.cancelled()) throw Cancelled{};
  }
  void weather() {
    check();
    if (++diagnostics.weatherCalls > options.maximumWeatherCalls)
      throw WorkLimit("Quick weather-work allowance reached");
  }
};

class CountedWeather final : public WeatherProvider {
public:
  CountedWeather(std::shared_ptr<const WeatherProvider> provider, Work& work)
      : provider_(std::move(provider)), work_(work) {}
  ParameterCoverage windCoverage() const override {
    return provider_->windCoverage();
  }
  ParameterCoverage currentCoverage() const override {
    return provider_->currentCoverage();
  }
  ParameterCoverage waveCoverage() const override {
    return provider_->waveCoverage();
  }
  WindSample wind(GeoPoint p, TimePoint t) const override {
    work_.weather();
    return provider_->wind(p, t);
  }
  CurrentSample current(GeoPoint p, TimePoint t) const override {
    work_.weather();
    return provider_->current(p, t);
  }
  WaveSample waves(GeoPoint p, TimePoint t) const override {
    work_.weather();
    return provider_->waves(p, t);
  }
  std::string identity() const override { return provider_->identity(); }

private:
  std::shared_ptr<const WeatherProvider> provider_;
  Work& work_;
};
class CountedClimatology final : public ClimatologyProvider {
public:
  CountedClimatology(std::shared_ptr<const ClimatologyProvider> p, Work& w)
      : provider_(std::move(p)), work_(w) {}
  ParameterCoverage coverage() const override { return provider_->coverage(); }
  WindSample wind(GeoPoint p, TimePoint t) const override {
    work_.weather();
    return provider_->wind(p, t);
  }
  std::string identity() const override { return provider_->identity(); }

private:
  std::shared_ptr<const ClimatologyProvider> provider_;
  Work& work_;
};

// No RouteLeg, strings or motion vectors per retained state. The original
// physical state and action are sufficient to reconstruct a selected lineage.
struct Label {
  GeoPoint position;
  double heading{}, fuel{}, risk{}, score{};
  std::int64_t time{}, modeSeconds{}, motorSeconds{}, waitSeconds{};
  std::uint32_t parent{none}, references{}, profile{}, manoeuvres{}, duration{};
  int sailPlan{-1};
  std::uint16_t slice{}, depth{};
  std::uint8_t tack{}, mode{}, role{}, flags{};
};
static_assert(sizeof(Label) <= 144);

class Pool {
public:
  explicit Pool(std::pmr::memory_resource* resource, QuickRoutingDiagnostics& d)
      : labels(resource), free(resource), profiles(resource), diagnostics(d) {}
  std::pmr::deque<Label> labels;
  std::pmr::vector<std::uint32_t> free;
  std::pmr::vector<std::pmr::string> profiles;
  QuickRoutingDiagnostics& diagnostics;
  std::uint32_t intern(const std::string& value) {
    for (std::uint32_t i = 0; i < profiles.size(); ++i)
      if (std::string_view(profiles[i]) == value) return i;
    profiles.emplace_back(value);
    return static_cast<std::uint32_t>(profiles.size() - 1);
  }
  Label pack(const internal::MotionState& s) {
    Label n;
    n.position = s.position;
    n.heading = s.incomingHeading;
    n.time = s.time.time_since_epoch().count();
    n.fuel = s.fuel;
    n.risk = s.risk;
    n.modeSeconds = s.modeDuration.count();
    n.motorSeconds = s.motorDuration.count();
    n.waitSeconds = s.waitDuration.count();
    n.profile = intern(s.profileIdentity);
    n.sailPlan = s.sailPlan;
    n.manoeuvres = s.manoeuvres;
    n.tack = static_cast<std::uint8_t>(s.tack);
    n.mode = static_cast<std::uint8_t>(s.mode);
    n.role = static_cast<std::uint8_t>(s.role);
    n.flags = s.departureEgressActive ? 1 : 0;
    return n;
  }
  internal::MotionState unpack(const Label& n) const {
    internal::MotionState s;
    s.position = n.position;
    s.time = TimePoint{Duration{n.time}};
    s.incomingHeading = n.heading;
    s.fuel = n.fuel;
    s.risk = n.risk;
    s.modeDuration = Duration{n.modeSeconds};
    s.motorDuration = Duration{n.motorSeconds};
    s.waitDuration = Duration{n.waitSeconds};
    s.profileIdentity = profiles.at(n.profile);
    s.sailPlan = n.sailPlan;
    s.manoeuvres = n.manoeuvres;
    s.tack = static_cast<Tack>(n.tack);
    s.mode = static_cast<PropulsionMode>(n.mode);
    s.role = static_cast<ProfileRole>(n.role);
    s.departureEgressActive = (n.flags & 1) != 0;
    return s;
  }
  std::uint32_t add(Label n) {
    n.references = 1;
    std::uint32_t id;
    if (free.empty()) {
      id = static_cast<std::uint32_t>(labels.size());
      labels.push_back(n);
    } else {
      id = free.back();
      free.pop_back();
      labels[id] = n;
    }
    if (n.parent != none) ++labels[n.parent].references;
    return id;
  }
  void release(std::uint32_t id) {
    while (id != none && --labels[id].references == 0) {
      const auto parent = labels[id].parent;
      free.push_back(id);
      ++diagnostics.reclaimedLabels;
      id = parent;
    }
  }
};

auto rank(const Label& n) {
  return std::tuple{n.score,
                    n.fuel,
                    n.risk,
                    n.motorSeconds,
                    n.manoeuvres,
                    n.position.latitude,
                    n.position.longitude,
                    n.heading,
                    n.tack,
                    n.mode,
                    n.profile,
                    n.sailPlan,
                    n.parent};
}

double objectiveCost(const RoutingRequest& r, const Label& n) {
  const double seconds =
      static_cast<double>(n.time - r.departure.time_since_epoch().count());
  if (r.objective.kind == ObjectiveKind::FuelAware)
    return seconds * r.objective.timeWeight +
           n.fuel * 3600 * r.objective.fuelWeight;
  if (r.objective.kind == ObjectiveKind::WeightedTimeRiskComfort)
    return seconds * r.objective.timeWeight + n.risk * r.objective.riskWeight;
  return seconds;
}

// Geographical cells thin alternatives; this is deliberately heuristic
// pruning, not a claim that nearby positions are equivalent physical states.
auto cell(const RoutingRequest& r, const Label& n) {
  const double lat = std::floor(n.position.latitude * 60 / 2.0);
  const double lon = std::floor(
      normalizeLongitude(n.position.longitude - r.start.longitude) * 60 *
      std::max(.05, std::cos(n.position.latitude * 3.141592653589793 / 180)) /
      2.0);
  return std::tuple{lat,
                    lon,
                    n.tack,
                    n.mode,
                    n.profile,
                    n.sailPlan,
                    static_cast<int>(std::floor(n.heading / 20)),
                    n.flags & 1};
}

std::vector<RouteLeg> reconstruct(const RoutingRequest& request,
                                  const RoutingEnvironment& environment,
                                  const VesselPerformanceModel& performance,
                                  const Pool& pool, std::uint32_t id,
                                  RoutingDiagnostics& diagnostics,
                                  std::pmr::memory_resource* resource) {
  std::pmr::vector<std::uint32_t> path(resource);
  for (auto p = id; pool.labels[p].parent != none; p = pool.labels[p].parent)
    path.push_back(p);
  std::reverse(path.begin(), path.end());
  std::vector<RouteLeg> legs;
  legs.reserve(path.size() + 1);
  for (auto p : path) {
    const Label& n = pool.labels[p];
    const auto from = pool.unpack(pool.labels[n.parent]);
    std::vector<internal::MotionCandidate> choices;
    RoutingStatus failure = RoutingStatus::Complete;
    if (n.flags & 2) {
      auto waiting = internal::checkedWait(request, environment, from,
                                           Duration{n.duration}, diagnostics);
      if (waiting) choices.push_back(std::move(*waiting));
    } else {
      choices = internal::checkedMotion(
          request, environment, performance, from, n.heading,
          Duration{n.duration}, Duration{n.slice}, diagnostics, &failure);
    }
    auto chosen =
        std::find_if(choices.begin(), choices.end(), [&](const auto& c) {
          return c.state.time.time_since_epoch().count() == n.time &&
                 c.state.sailPlan == n.sailPlan &&
                 static_cast<unsigned>(c.state.mode) == n.mode &&
                 c.state.profileIdentity ==
                     std::string_view(pool.profiles[n.profile]) &&
                 distanceNm(c.state.position, n.position) < 1e-8 &&
                 std::abs(c.state.fuel - n.fuel) < 1e-8 &&
                 std::abs(c.state.risk - n.risk) < 1e-8;
        });
    if (chosen == choices.end())
      throw WorkLimit(
          "Quick lineage reconstruction did not reproduce the stored motion");
    legs.push_back(std::move(chosen->leg));
  }
  return legs;
}
}  // namespace

QuickRoutingResult QuickRoutingEngine::route(
    const RoutingRequest& request, const RoutingEnvironment& source,
    const QuickRoutingOptions& requestedOptions) const {
  QuickRoutingOptions options = requestedOptions;
  options.maximumGeneratedStates = std::min(
      options.maximumGeneratedStates, request.limits.maximumGeneratedStates);
  QuickRoutingResult output;
  auto& result = output.route;
  output.quick.memoryBudgetMiB = options.memoryBudgetMiB;
  if (options.memoryBudgetMiB < 1 || options.memoryBudgetMiB > 4096 ||
      options.memoryBudgetMiB >
          std::numeric_limits<std::size_t>::max() / (1024U * 1024U) ||
      options.beamWidth < 8 || options.beamWidth > 512 ||
      options.recoveryBeamWidth < options.beamWidth ||
      options.recoveryBeamWidth > 512 || options.offshoreStep < Duration{600} ||
      options.offshoreStep > Duration{21600} ||
      !std::isfinite(options.headingStepDegrees) ||
      options.headingStepDegrees < 5 || options.headingStepDegrees > 30 ||
      options.maximumGeneratedStates == 0 ||
      options.maximumGeneratedStates > 10000000 ||
      options.maximumWeatherCalls == 0 ||
      options.maximumValidatedCandidates == 0 ||
      options.maximumValidatedCandidates > 8 ||
      request.limits.maximumRouteDuration <= Duration::zero()) {
    result.status = RoutingStatus::InvalidVesselConfiguration;
    result.message = "Invalid Quick routing options";
    return output;
  }
  BudgetResource memory(static_cast<std::size_t>(options.memoryBudgetMiB) *
                        1024 * 1024);
  Work work{request, options, output.quick};
  try {
    work.check();
    const auto valid = [](GeoPoint p) {
      return std::isfinite(p.latitude) && std::isfinite(p.longitude) &&
             std::abs(p.latitude) <= 90 && std::abs(p.longitude) <= 180;
    };
    if (!valid(request.start) || !valid(request.destination)) {
      result.status = !valid(request.start) ? RoutingStatus::InvalidStart
                                            : RoutingStatus::InvalidDestination;
      result.message = "Invalid route endpoint";
      return output;
    }
    RoutingEnvironment environment = source;
    if (source.grib)
      environment.grib = std::make_shared<CountedWeather>(source.grib, work);
    if (source.climatology)
      environment.climatology =
          std::make_shared<CountedClimatology>(source.climatology, work);
    if (environment.landAndBoundaries) {
      if (environment.landAndBoundaries->pointForbidden(request.start) ||
          environment.landAndBoundaries->pointForbidden(request.destination)) {
        result.status = RoutingStatus::InvalidDestination;
        result.message = "Route endpoint is inside a forbidden area";
        return output;
      }
      if (request.constraints.minimumDepthMetres) {
        for (const auto p : {request.start, request.destination}) {
          const auto d = environment.landAndBoundaries->depthMetres(p);
          if (d && *d < *request.constraints.minimumDepthMetres) {
            result.status = RoutingStatus::InvalidDestination;
            result.message =
                "Endpoint is shallower than configured minimum depth";
            return output;
          }
        }
      }
    }
    const auto check = RoutingEngine{}.preflight(request, environment);
    result.preflight = check;
    result.warnings = check.warnings;
    if (!check.canRoute) {
      result.status = internal::failedPreflightStatus(check);
      result.message = "Quick routing preflight requires caller action";
      return output;
    }
    if (environment.xtdCurrent && request.environment.useCurrent &&
        check.coverage.currentFallbackNeeded) {
      work.check();
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
      area.north = std::min(
          89.0, std::max(request.start.latitude, request.destination.latitude) +
                    margin);
      const double areaWidth = area.west <= area.east
                                   ? area.east - area.west
                                   : 360.0 - area.west + area.east;
      if (areaWidth * (area.north - area.south) >
          request.limits.maximumPredictionAreaSquareDegrees) {
        result.status = RoutingStatus::ResourceLimitReached;
        result.message =
            "initial current-prediction area exceeds resource limit";
        result.diagnostics.resourceLimitEvents.push_back(result.message);
        return output;
      }
      const auto coverage = environment.xtdCurrent->ensureCoverage(
          area, request.departure,
          request.departure + std::min(request.limits.maximumRouteDuration,
                                       Duration{std::chrono::hours{48}}),
          std::chrono::hours{1}, request.cancellation);
      if (coverage.status == CoverageStatus::Cancelled) {
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
    PolarPerformanceModel fallback(request.vessel);
    const auto& performance =
        environment.performance ? *environment.performance : fallback;
    const double totalDistance = distanceNm(request.start, request.destination);
    RoutingStatus dataFailure = RoutingStatus::Complete;
    for (unsigned attempt = 0; attempt < 2; ++attempt) {
      ++output.quick.attempts;
      Pool pool(&memory, output.quick);
      std::pmr::vector<std::uint32_t> frontier(&memory), nextFrontier(&memory);
      std::pmr::vector<Label> candidates(&memory), selected(&memory);
      internal::MotionState root;
      root.position = request.start;
      root.time = request.departure;
      if (environment.landAndBoundaries &&
          request.constraints.landSafetyMarginNm > 0)
        root.departureEgressActive =
            environment.landAndBoundaries->distanceToForbiddenNm(
                request.start) < request.constraints.landSafetyMarginNm;
      frontier.push_back(pool.add(pool.pack(root)));
      const unsigned beam =
          attempt ? options.recoveryBeamWidth : options.beamWidth;
      const auto stageLimit = attempt ? options.maximumGeneratedStates
                                      : options.maximumGeneratedStates * 2 / 3;
      unsigned layer = 0;
      while (!frontier.empty() &&
             result.diagnostics.generatedStates < stageLimit) {
        result.diagnostics.retainedStates =
            std::max<std::uint64_t>(result.diagnostics.retainedStates,
                                    pool.labels.size() - pool.free.size());
        work.check();
        if (request.progress)
          request.progress({RoutingProgressStage::ForwardIsochrone, attempt + 1,
                            2, result.diagnostics.generatedStates,
                            static_cast<std::uint64_t>(pool.labels.size() -
                                                       pool.free.size()),
                            result.diagnostics.landChecks,
                            result.diagnostics.closestApproachNm, 100});
        std::sort(frontier.begin(), frontier.end(), [&](auto a, auto b) {
          return rank(pool.labels[a]) < rank(pool.labels[b]);
        });
        double nearest = std::numeric_limits<double>::infinity();
        for (auto id : frontier)
          nearest = std::min(nearest, distanceNm(pool.labels[id].position,
                                                 request.destination));
        result.diagnostics.closestApproachNm =
            std::min(result.diagnostics.closestApproachNm, nearest);
        // Sparse direct approaches. Each is integrated at the same minute
        // cadence as Main; failed connections cannot become accepted routes.
        if (nearest < 20.0) {
          unsigned tries = 0;
          for (auto id : frontier) {
            if (++tries > 4 || output.quick.validatedCandidates >=
                                   options.maximumValidatedCandidates)
              break;
            if (distanceNm(pool.labels[id].position, request.destination) > 20)
              continue;
            auto terminal = internal::checkedConnection(
                request, environment, performance, pool.unpack(pool.labels[id]),
                Duration{7200}, result.diagnostics);
            if (!terminal) continue;
            ++output.quick.validatedCandidates;
            auto legs = reconstruct(request, environment, performance, pool, id,
                                    result.diagnostics, &memory);
            legs.push_back(std::move(terminal->leg));
            if (environment.landAndBoundaries)
              environment.landAndBoundaries->prepareValidationRoute(
                  legs, request.constraints.landSafetyMarginNm);
            // Acceptance uses the original providers so the search-work cap
            // cannot interrupt and thereby weaken independent validation.
            auto validation = RouteValidator{}.validate(
                request, source, performance, legs, nullptr);
            result.diagnostics.validationSamples += validation.samples;
            if (validation.passed) {
              result.legs = std::move(legs);
              result.validation = std::move(validation);
              result.status = RoutingStatus::Complete;
              result.solverPath = SolverPath::QuickBeam;
              result.message = "Complete — Quick Route";
              internal::summariseRoute(result);
              output.quick.peakSearchBytes = memory.peak;
              return output;
            }
            result.diagnostics.stageStopReasons.push_back(
                "Quick candidate replay: " + validation.failureReason);
          }
        }
        candidates.clear();
        selected.clear();
        nextFrontier.clear();
        Duration step = options.offshoreStep;
        if (totalDistance < 120) step = std::min(step, Duration{3600});
        if (layer < 3) step = Duration{1800};
        if (nearest < 15) step = Duration{600};
        for (auto id : frontier)
          if (pool.labels[id].flags & 1) step = Duration{600};
        for (auto id : frontier) {
          const Label parent = pool.labels[id];
          const auto from = pool.unpack(parent);
          if (from.time + step >
              request.departure + request.limits.maximumRouteDuration)
            continue;
          std::array<double, 80> angles{};
          std::size_t count = 0;
          const double bearing =
              initialBearingDegrees(from.position, request.destination);
          auto addAngle = [&](double h) {
            h = normalizeHeading(h);
            if (std::abs(angularDifferenceDegrees(bearing, h)) >
                request.options.maximumSearchAngleDegrees + 1e-9)
              return;
            for (std::size_t i = 0; i < count; ++i)
              if (std::abs(angularDifferenceDegrees(h, angles[i])) < .01)
                return;
            if (count < angles.size()) angles[count++] = h;
          };
          const double increment =
              (parent.flags & 1)
                  ? 5.0
                  : (attempt ? std::max(10.0, options.headingStepDegrees / 2)
                             : options.headingStepDegrees);
          for (double h = 0; h < 360; h += increment) addAngle(h);
          addAngle(bearing);
          addAngle(parent.heading);
          for (double off : {-10., -5., 5., 10.}) addAngle(bearing + off);
          bool movement = false;
          for (std::size_t a = 0;
               a < count && result.diagnostics.generatedStates < stageLimit;
               ++a) {
            work.check();
            ++output.quick.attemptedMotions;
            if (output.quick.attemptedMotions >
                options.maximumGeneratedStates * 4)
              throw WorkLimit("Quick attempted-motion allowance reached");
            const Duration slice =
                (parent.flags & 1) ? Duration{300} : Duration{900};
            auto motions = internal::checkedMotion(
                request, environment, performance, from, angles[a], step, slice,
                result.diagnostics, &dataFailure);
            for (const auto& motion : motions) {
              if (result.diagnostics.generatedStates >= stageLimit) break;
              ++result.diagnostics.generatedStates;
              movement = true;
              if (distanceNm(request.start, motion.state.position) >
                  request.limits.maximumExplorationDistanceNm)
                continue;
              Label n = pool.pack(motion.state);
              n.parent = id;
              n.depth = parent.depth + 1;
              n.duration = static_cast<std::uint32_t>(step.count());
              n.slice = static_cast<std::uint16_t>(slice.count());
              const double remaining =
                  distanceNm(n.position, request.destination);
              // Common positive speed keeps the score a ranking hint only.
              n.score = objectiveCost(request, n) + remaining / 6.0 * 3600;
              candidates.push_back(n);
            }
            if (candidates.size() > 4096) {
              std::sort(candidates.begin(), candidates.end(),
                        [](const auto& a, const auto& b) {
                          return rank(a) < rank(b);
                        });
              candidates.resize(2048);
            }
          }
          if (!movement && result.diagnostics.generatedStates < stageLimit) {
            auto wait = internal::checkedWait(request, environment, from, step,
                                              result.diagnostics);
            if (wait) {
              ++result.diagnostics.generatedStates;
              Label n = pool.pack(wait->state);
              n.parent = id;
              n.depth = parent.depth + 1;
              n.duration = static_cast<std::uint32_t>(step.count());
              n.flags |= 2;
              n.score = objectiveCost(request, n) +
                        distanceNm(n.position, request.destination) / 6 * 3600;
              candidates.push_back(n);
            }
          }
        }
        std::sort(
            candidates.begin(), candidates.end(),
            [](const auto& a, const auto& b) { return rank(a) < rank(b); });
        // Preserve multiple approach directions before filling with the best
        // remaining eligible spatial/heading families.
        std::array<unsigned, 72> sectorCount{};
        auto admit = [&](const Label& n) {
          unsigned duplicates = 0;
          for (const auto& s : selected)
            if (cell(request, n) == cell(request, s)) ++duplicates;
          if (duplicates >= 2) return false;
          selected.push_back(n);
          return true;
        };
        for (const auto& n : candidates) {
          const unsigned sector =
              static_cast<unsigned>(normalizeHeading(initialBearingDegrees(
                                        request.start, n.position)) /
                                    30) *
                  6 +
              n.tack * 2 + (n.mode != 0);
          if (sectorCount[std::min<unsigned>(sector, 71)] >=
              std::max(1U, beam / 24))
            continue;
          if (admit(n)) ++sectorCount[std::min<unsigned>(sector, 71)];
          if (selected.size() >= beam) break;
        }
        for (const auto& n : candidates) {
          if (selected.size() >= beam) break;
          if (std::any_of(selected.begin(), selected.end(),
                          [&](const auto& s) { return rank(s) == rank(n); }))
            continue;
          admit(n);
        }
        for (const auto& n : selected) nextFrontier.push_back(pool.add(n));
        for (auto id : frontier) pool.release(id);
        frontier.swap(nextFrontier);
        ++layer;
      }
    }
    result.status = dataFailure == RoutingStatus::Complete
                        ? RoutingStatus::SearchIncomplete
                        : dataFailure;
    result.message =
        "Quick search did not find a validated route within its allowance; try "
        "the main engine";
  } catch (const Cancelled&) {
    result.status = RoutingStatus::Cancelled;
    result.message = "Quick route cancelled";
  } catch (const std::bad_alloc&) {
    result.status = RoutingStatus::ResourceLimitReached;
    result.message =
        "Quick allocation failed: search budget or process memory limit "
        "reached";
  } catch (const WorkLimit& error) {
    result.status = RoutingStatus::ResourceLimitReached;
    result.message = error.what();
  }
  output.quick.peakSearchBytes = memory.peak;
  return output;
}
}  // namespace supercpn::weather_routing
