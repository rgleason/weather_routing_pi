#pragma once

#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <unordered_map>
#include <vector>

#include "supercpn/weather_routing/Types.h"

namespace supercpn::weather_routing {

class WeatherProvider {
public:
  virtual ~WeatherProvider() = default;
  [[nodiscard]] virtual ParameterCoverage windCoverage() const = 0;
  [[nodiscard]] virtual ParameterCoverage currentCoverage() const = 0;
  [[nodiscard]] virtual ParameterCoverage waveCoverage() const = 0;
  [[nodiscard]] virtual WindSample wind(GeoPoint position,
                                        TimePoint time) const = 0;
  [[nodiscard]] virtual CurrentSample current(GeoPoint position,
                                              TimePoint time) const = 0;
  [[nodiscard]] virtual WaveSample waves(GeoPoint position,
                                         TimePoint time) const = 0;
  [[nodiscard]] virtual std::string identity() const = 0;
};

class ClimatologyProvider {
public:
  virtual ~ClimatologyProvider() = default;
  [[nodiscard]] virtual ParameterCoverage coverage() const = 0;
  [[nodiscard]] virtual WindSample wind(GeoPoint position,
                                        TimePoint time) const = 0;
  [[nodiscard]] virtual std::string identity() const = 0;
};

enum class CoverageStatus {
  Ready,
  UnsupportedArea,
  ResourceLimit,
  Cancelled,
  Error
};
struct CoverageResult {
  CoverageStatus status{CoverageStatus::Error};
  std::string message;
  std::uint64_t generatedTiles{};
};

class CurrentPredictionProvider {
public:
  virtual ~CurrentPredictionProvider() = default;
  [[nodiscard]] virtual ParameterCoverage coverage() const = 0;
  virtual CoverageResult ensureCoverage(
      const GeoEnvelope& area, TimePoint start, TimePoint end,
      Duration outputStep, const CancellationToken& cancellation) = 0;
  [[nodiscard]] virtual CurrentSample sample(GeoPoint position,
                                             TimePoint time) const = 0;
  [[nodiscard]] virtual std::string identity() const = 0;
  [[nodiscard]] virtual std::uint64_t sampleCount() const { return 0; }
  [[nodiscard]] virtual std::uint64_t expansionCount() const { return 0; }
};

class LandAndBoundaryProvider {
public:
  virtual ~LandAndBoundaryProvider() = default;
  [[nodiscard]] virtual bool pointForbidden(GeoPoint point) const = 0;
  [[nodiscard]] virtual bool segmentForbidden(GeoPoint start, GeoPoint end,
                                              double safetyMarginNm) const = 0;
  // Time-aware variants allow host adapters to preserve dynamic constraints
  // such as historical cyclone-track avoidance. Geometry-only providers need
  // not override these methods.
  [[nodiscard]] virtual bool segmentForbiddenAt(GeoPoint start, GeoPoint end,
                                                TimePoint time,
                                                double safetyMarginNm) const {
    (void)time;
    return segmentForbidden(start, end, safetyMarginNm);
  }
  // Search and validation chains establish their first point independently.
  // The known-safe point may be inside a non-zero stand-off buffer (such as a
  // harbour departure); implementations may omit the clearance test at that
  // exact point, but must still reject polygon crossings and enforce the full
  // margin on the segment interior and endpoint. This permits egress, never a
  // near-shore approach or a retained state inside the buffer.
  [[nodiscard]] virtual bool segmentFromKnownSafeForbidden(
      GeoPoint start, GeoPoint end, double safetyMarginNm) const {
    return segmentForbidden(start, end, safetyMarginNm);
  }
  [[nodiscard]] virtual bool segmentFromKnownSafeForbiddenAt(
      GeoPoint start, GeoPoint end, TimePoint time,
      double safetyMarginNm) const {
    (void)time;
    return segmentFromKnownSafeForbidden(start, end, safetyMarginNm);
  }
  // Independent chronological replay may need a stricter provider than the
  // exploratory search. Hosts with expensive authoritative chart semantics
  // can search against a conservative fast shoreline and override this hook
  // to chart-check only complete route candidates. The default preserves
  // identical search and validation semantics.
  [[nodiscard]] virtual bool validationSegmentFromKnownSafeForbiddenAt(
      GeoPoint start, GeoPoint end, TimePoint time,
      double safetyMarginNm) const {
    return segmentFromKnownSafeForbiddenAt(start, end, time, safetyMarginNm);
  }
  // Hosts whose authoritative safety data is serviced asynchronously may
  // batch cache preparation for a complete candidate before chronological
  // replay. This is a scheduling hook only; validation still checks every
  // segment independently. The default is deliberately a no-op.
  virtual void prepareValidationRoute(std::span<const RouteLeg>, double) const {
  }
  // A host may deliberately search with a cheaper shoreline and validate with
  // a more detailed chart. If every bounded candidate fails that stricter
  // replay, the host can request immediate escalation to its authoritative
  // search mode instead of spending the remaining reverse/graph budget on the
  // same approximate constraint model.
  [[nodiscard]] virtual bool validationFailureRequiresSearchEscalation() const {
    return false;
  }
  // Isochrone contours are an inspection aid, never accepted vessel motion.
  // Hosts may provide a cheaper shoreline-only test here so drawing fronts
  // cannot consume a bounded semantic chart-safety budget needed by routing.
  [[nodiscard]] virtual bool visualizationSegmentForbidden(
      GeoPoint start, GeoPoint end, double safetyMarginNm) const {
    return segmentFromKnownSafeForbidden(start, end, safetyMarginNm);
  }
  [[nodiscard]] virtual double distanceToForbiddenNm(GeoPoint point) const = 0;
  [[nodiscard]] virtual std::optional<double> depthMetres(GeoPoint) const {
    return {};
  }
  [[nodiscard]] virtual std::string identity() const = 0;
};

struct PerformanceCandidate {
  bool valid{};
  PropulsionMode mode{PropulsionMode::Sail};
  ProfileRole role{ProfileRole::SailOnly};
  std::string profileIdentity;
  int sailPlan{-1};
  double speedThroughWaterKnots{};
  double fuelLitresPerHour{};
};

class VesselPerformanceModel {
public:
  virtual ~VesselPerformanceModel() = default;
  [[nodiscard]] virtual bool valid(std::string* reason = nullptr) const = 0;
  [[nodiscard]] virtual std::vector<PerformanceCandidate> candidates(
      double trueWindSpeedKnots, double trueWindAngleDegrees,
      const WaveSample& waves, PropulsionMode previousMode,
      Duration previousModeDuration) const = 0;
  [[nodiscard]] virtual PerformanceCandidate evaluate(
      PropulsionMode mode, ProfileRole role, const std::string& profileIdentity,
      double trueWindSpeedKnots, double trueWindAngleDegrees,
      const WaveSample& waves) const = 0;
  [[nodiscard]] virtual std::vector<PerformanceCandidate> candidatesAt(
      GeoPoint, TimePoint, double trueWindSpeedKnots,
      double trueWindAngleDegrees, const WaveSample& waves,
      PropulsionMode previousMode, Duration previousModeDuration) const {
    return candidates(trueWindSpeedKnots, trueWindAngleDegrees, waves,
                      previousMode, previousModeDuration);
  }
  [[nodiscard]] virtual PerformanceCandidate evaluateAt(
      GeoPoint, TimePoint, PropulsionMode mode, ProfileRole role,
      const std::string& profileIdentity, double trueWindSpeedKnots,
      double trueWindAngleDegrees, const WaveSample& waves) const {
    return evaluate(mode, role, profileIdentity, trueWindSpeedKnots,
                    trueWindAngleDegrees, waves);
  }
};

struct RoutingEnvironment {
  std::shared_ptr<const WeatherProvider> grib;
  std::shared_ptr<const ClimatologyProvider> climatology;
  std::shared_ptr<CurrentPredictionProvider> xtdCurrent;
  std::shared_ptr<const LandAndBoundaryProvider> landAndBoundaries;
  std::shared_ptr<const VesselPerformanceModel> performance;
  std::string memberIdentity{"deterministic"};
};

class OpenWaterProvider final : public LandAndBoundaryProvider {
public:
  bool pointForbidden(GeoPoint) const override { return false; }
  bool segmentForbidden(GeoPoint, GeoPoint, double) const override {
    return false;
  }
  double distanceToForbiddenNm(GeoPoint) const override {
    return std::numeric_limits<double>::infinity();
  }
  std::optional<double> depthMetres(GeoPoint) const override {
    return std::numeric_limits<double>::infinity();
  }
  std::string identity() const override { return "open-water"; }
};

class UniformWeatherProvider final : public WeatherProvider,
                                     public ClimatologyProvider {
public:
  struct Configuration {
    std::string identity{"synthetic-uniform"};
    std::optional<Vector2> windTowardKnots;
    std::optional<Vector2> currentTowardKnots;
    std::optional<WaveSample> wave;
    std::optional<TimePoint> begins;
    std::optional<TimePoint> ends;
    GeoEnvelope area{-180.0, -90.0, 180.0, 90.0};
    EnvironmentalSource source{EnvironmentalSource::SyntheticTestField};
  };

  explicit UniformWeatherProvider(Configuration configuration);
  ParameterCoverage windCoverage() const override;
  ParameterCoverage currentCoverage() const override;
  ParameterCoverage waveCoverage() const override;
  ParameterCoverage coverage() const override { return windCoverage(); }
  WindSample wind(GeoPoint position, TimePoint time) const override;
  CurrentSample current(GeoPoint position, TimePoint time) const override;
  WaveSample waves(GeoPoint position, TimePoint time) const override;
  std::string identity() const override { return configuration_.identity; }

private:
  bool covered(GeoPoint position, TimePoint time) const;
  Configuration configuration_;
};

class PolygonBoundaryProvider final : public LandAndBoundaryProvider {
public:
  explicit PolygonBoundaryProvider(std::vector<std::vector<GeoPoint>> polygons,
                                   std::string identity = "scenario-obstacles");
  bool pointForbidden(GeoPoint point) const override;
  bool segmentForbidden(GeoPoint start, GeoPoint end,
                        double safetyMarginNm) const override;
  bool segmentFromKnownSafeForbidden(GeoPoint start, GeoPoint end,
                                     double safetyMarginNm) const override;
  double distanceToForbiddenNm(GeoPoint point) const override;
  std::string identity() const override { return identity_; }

private:
  struct BoundaryEdge {
    GeoPoint start;
    GeoPoint end;
    std::size_t polygon{};
  };

  [[nodiscard]] std::vector<std::size_t> edgeCandidatesForArea(
      double south, double north, double westUnwrapped,
      double eastUnwrapped) const;
  std::vector<std::vector<GeoPoint>> polygons_;
  std::vector<BoundaryEdge> edges_;
  std::vector<std::vector<std::size_t>> edgeLatitudeBands_;
  std::unordered_map<std::uint32_t, std::vector<std::size_t>> edgeCells_;
  std::string identity_;
};

class PolarPerformanceModel final : public VesselPerformanceModel {
public:
  explicit PolarPerformanceModel(VesselConfiguration configuration);
  bool valid(std::string* reason = nullptr) const override;
  std::vector<PerformanceCandidate> candidates(
      double trueWindSpeedKnots, double trueWindAngleDegrees,
      const WaveSample& waves, PropulsionMode previousMode,
      Duration previousModeDuration) const override;
  PerformanceCandidate evaluate(PropulsionMode mode, ProfileRole role,
                                const std::string& profileIdentity,
                                double trueWindSpeedKnots,
                                double trueWindAngleDegrees,
                                const WaveSample& waves) const override;
  [[nodiscard]] const VesselConfiguration& configuration() const {
    return configuration_;
  }
  static bool validateProfile(const PerformanceProfile& profile,
                              std::string* reason = nullptr);
  static double interpolate(const PerformanceProfile& profile,
                            double trueWindSpeedKnots,
                            double trueWindAngleDegrees);

private:
  const PerformanceProfile* profile(ProfileRole role,
                                    const std::string& identity = {}) const;
  PerformanceCandidate evaluateProfile(const PerformanceProfile& profile,
                                       double tws, double twa,
                                       const WaveSample& waves) const;
  VesselConfiguration configuration_;
};

class XtdCurrentProvider final : public CurrentPredictionProvider {
public:
  struct Options {
    std::size_t readerTileCapacity{48};
    std::uint64_t readerCacheBytes{96ULL * 1024ULL * 1024ULL};
    std::size_t maximumSlices{512};
    double tileDegrees{2.0};
    double sampleSpacingDegrees{0.25};
    bool includeExpectedSeasonalCirculation{true};
  };

  explicit XtdCurrentProvider(std::string path);
  XtdCurrentProvider(std::string path, Options options);
  ~XtdCurrentProvider() override;
  XtdCurrentProvider(const XtdCurrentProvider&) = delete;
  XtdCurrentProvider& operator=(const XtdCurrentProvider&) = delete;
  ParameterCoverage coverage() const override;
  CoverageResult ensureCoverage(const GeoEnvelope& area, TimePoint start,
                                TimePoint end, Duration outputStep,
                                const CancellationToken& cancellation) override;
  CurrentSample sample(GeoPoint position, TimePoint time) const override;
  std::string identity() const override;
  std::uint64_t sampleCount() const override;
  std::uint64_t expansionCount() const override;

private:
  class Impl;
  std::unique_ptr<Impl> impl_;
};

}  // namespace supercpn::weather_routing
