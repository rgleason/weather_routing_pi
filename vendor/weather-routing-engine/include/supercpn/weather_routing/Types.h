#pragma once

#include <atomic>
#include <chrono>
#include <cstdint>
#include <functional>
#include <limits>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace supercpn::weather_routing {

using TimePoint = std::chrono::sys_seconds;
using Duration = std::chrono::seconds;

struct GeoPoint {
  double latitude{};
  double longitude{};
  bool operator==(const GeoPoint&) const = default;
};

struct GeoEnvelope {
  double west{};
  double south{};
  double east{};
  double north{};
};

struct Vector2 {
  double eastKnots{};
  double northKnots{};
};

enum class EnvironmentalVariable { Wind, Current, Waves };
enum class EnvironmentalSource {
  GribForecast,
  Climatology,
  XtdCurrentPrediction,
  NoDataAssumedZero,
  SyntheticTestField,
  Missing
};

struct EnvironmentalSourceMetadata {
  EnvironmentalSource source{EnvironmentalSource::Missing};
  std::string datasetIdentity;
  std::string modelIdentity;
  std::optional<TimePoint> modelRunTime;
  std::optional<TimePoint> sourceTimestamp;
  std::optional<double> spatialResolutionDegrees;
  std::string fallbackReason;
  bool operator==(const EnvironmentalSourceMetadata&) const = default;
};

struct WindSample {
  bool available{};
  // Meteorological wind vector: components point toward where the air moves.
  Vector2 velocity;
  EnvironmentalSourceMetadata metadata;
};

struct CurrentSample {
  bool available{};
  // Oceanographic current vector: components point toward where water moves.
  Vector2 velocity;
  EnvironmentalSourceMetadata metadata;
};

struct WaveSample {
  bool available{};
  double significantHeightMetres{};
  double directionFromDegrees{};
  double periodSeconds{};
  EnvironmentalSourceMetadata metadata;
};

struct EnvironmentalSnapshot {
  WindSample wind;
  CurrentSample current;
  WaveSample waves;
};

enum class TransitionReason {
  InitialSource,
  ParameterUnavailable,
  GeographicCoverageEnded,
  TemporalCoverageEnded,
  MissingCell,
  HigherPrioritySourceRestored,
  ExplicitZeroAssumption
};

struct EnvironmentalSourceTransition {
  EnvironmentalVariable variable{EnvironmentalVariable::Wind};
  EnvironmentalSource previousSource{EnvironmentalSource::Missing};
  EnvironmentalSource newSource{EnvironmentalSource::Missing};
  GeoPoint position;
  TimePoint time{};
  TransitionReason reason{TransitionReason::InitialSource};
  bool requiredAcknowledgement{};
};

struct EnvironmentalSourceUsage {
  Duration gribWindDuration{};
  Duration climatologyWindDuration{};
  Duration gribCurrentDuration{};
  Duration xtdCurrentDuration{};
  Duration currentAssumedZeroDuration{};
  Duration gribWaveDuration{};
  Duration missingWaveDuration{};
};

enum class PropulsionMode { Sail, MotorSail, Motor };
enum class ProfileRole { SailOnly, MotorSailing, MotorOnly };
enum class Tack { Unknown, Port, Starboard };

struct PolarPoint {
  double trueWindAngleDegrees{};
  double boatSpeedKnots{};
};

struct PolarRow {
  double trueWindSpeedKnots{};
  std::vector<PolarPoint> points;
};

struct WavePerformancePoint {
  double significantHeightMetres{};
  double periodSeconds{};
  double speedFactor{1.0};
};

struct PerformanceProfile {
  ProfileRole role{ProfileRole::SailOnly};
  std::string identity;
  bool estimated{};
  std::string assumptions;
  std::vector<PolarRow> rows;
  // Optional measured/estimated speed multiplier surface. When present this
  // takes precedence over the vessel-wide linear wave penalty.
  std::vector<WavePerformancePoint> wavePerformance;
  double efficiency{1.0};
};

struct PropulsionPolicy {
  bool allowSailing{true};
  bool allowMotorSailing{};
  bool allowMotor{};
  double motorBelowSailingSpeedKnots{};
  double configuredMotorSpeedKnots{};
  double motorSailingBoostKnots{};
  Duration minimumMotorRun{};
  Duration modeChangePenalty{};
  double crossoverHysteresisKnots{0.2};
  std::optional<Duration> maximumMotorTime;
  std::optional<double> maximumFuelLitres;
  std::optional<double> fuelConsumptionLitresPerHour;
};

struct VesselConfiguration {
  std::vector<PerformanceProfile> profiles;
  PropulsionPolicy propulsion;
  double upwindEfficiency{1.0};
  double downwindEfficiency{1.0};
  double wavePenaltyPerMetre{};
  Duration tackPenalty{};
  Duration gybePenalty{};
  Duration sailPlanChangePenalty{};
};

enum class ClimatologyFallbackPolicy {
  Disallow,
  AllowWithWarning,
  RequireExplicitAcknowledgement
};
enum class MissingCurrentPolicy {
  Disallow,
  AllowAssumedZero,
  RequireExplicitAcknowledgement
};
enum class MissingWavePolicy {
  DisallowWhenConstrained,
  AllowWithWarning,
  RequireExplicitAcknowledgement
};

struct EnvironmentalPolicy {
  bool useWind{true};
  bool useCurrent{true};
  bool useWaves{true};
  ClimatologyFallbackPolicy climatology{ClimatologyFallbackPolicy::Disallow};
  MissingCurrentPolicy missingCurrent{MissingCurrentPolicy::Disallow};
  MissingWavePolicy missingWaves{MissingWavePolicy::AllowWithWarning};
  bool climatologyAcknowledged{};
  bool zeroCurrentAcknowledged{};
  bool missingWavesAcknowledged{};
};

enum class ObjectiveKind {
  Fastest,
  FastestUnderSafetyLimits,
  RobustFastest,
  WeightedTimeRiskComfort,
  FuelAware
};
enum class EnsembleRiskMeasure { Expected, Percentile, ConditionalValueAtRisk };

struct RoutingObjective {
  ObjectiveKind kind{ObjectiveKind::FastestUnderSafetyLimits};
  double timeWeight{1.0};
  double riskWeight{};
  double comfortWeight{};
  double fuelWeight{};
  EnsembleRiskMeasure riskMeasure{EnsembleRiskMeasure::Expected};
  double percentile{0.9};
  double minimumFeasibilityProbability{1.0};
};

struct RoutingConstraints {
  std::optional<double> maximumTrueWindKnots;
  std::optional<double> maximumApparentWindKnots;
  std::optional<double> maximumWaveHeightMetres;
  std::optional<double> minimumDepthMetres;
  std::optional<double> maximumOpposingWindCurrent;
  double minimumTrueWindAngleDegrees{};
  double maximumTrueWindAngleDegrees{180.0};
  double landSafetyMarginNm{};
  double maximumLatitudeDegrees{89.0};
};

struct RoutingOptions {
  Duration timeStep{std::chrono::hours{1}};
  Duration minimumTimeStep{std::chrono::minutes{10}};
  double headingStepDegrees{15.0};
  double refinedHeadingStepDegrees{5.0};
  // Maximum angular departure on either side of the instantaneous bearing to
  // destination. 180 degrees deliberately enables full-circle exploration.
  double maximumSearchAngleDegrees{120.0};
  double destinationToleranceNm{1.0};
  double spatialCellNm{5.0};
  unsigned labelsPerCell{8};
  bool adaptiveTimeStep{true};
  bool adaptiveHeadings{true};
  bool adaptiveFrontierDensity{true};
  bool useReverseRecovery{true};
  bool useFrontierRecovery{true};
  bool useGraphFallback{true};
  // Permit a bounded stationary hold only when a retained state cannot make
  // any feasible progress. This is a recovery action for tidal/weather gates,
  // not an unbounded departure-time optimiser.
  bool allowWaiting{true};
  Duration maximumWait{std::chrono::hours{6}};
  unsigned retryStages{7};
  unsigned reverseLayers{8};
  Duration reverseHorizon{std::chrono::hours{24}};
  // The graph starts inside the fast corridor and, only if that search
  // exhausts, progressively admits deferred labels out to the maximum.
  // Setting the maximum to infinity removes the graph-only geometric bound;
  // normal route constraints and resource limits still apply.
  double graphCorridorWidthNm{120.0};
  double maximumGraphCorridorWidthNm{120.0};
  double graphHeadingStepDegrees{5.0};
  double heuristicMaximumSpeedKnots{};
  double dominancePositionToleranceNm{2.0};
  double dominanceHeadingToleranceDegrees{15.0};
  double epsilonDominance{0.01};
  bool preserveRouteFamilies{true};
  // Effort levels are executed cumulatively (100, 150, 200, 400). A route
  // found at a lower level is therefore retained instead of being lost when
  // a larger search changes pruning decisions.
  unsigned routingEffortPercent{100};
  bool forceForwardFailureForTesting{};
  bool forceReverseFailureForTesting{};
};

struct ResourceLimits {
  Duration maximumRouteDuration{std::chrono::hours{24 * 30}};
  // Stage-specific limits are preferred by production callers. Zero retains
  // the legacy split of maximumGeneratedStates for API compatibility.
  std::uint64_t maximumCoastalEndpointGeneratedStates{};
  std::uint64_t maximumForwardGeneratedStates{};
  // Reverse bridge recovery does not generate ordinary isochrone states, so
  // bound both the number of retained lineages inspected and the more
  // expensive chronological bridge integrations independently.
  std::uint64_t maximumReverseCandidates{};
  std::uint64_t maximumReverseBridgeAttempts{};
  std::uint64_t maximumFrontierRecoveryGeneratedStates{};
  std::uint64_t maximumFrontierRecoveryLabels{};
  std::uint64_t maximumGraphGeneratedStates{};
  std::uint64_t maximumGeneratedStates{2000000};
  std::uint64_t maximumRetainedStates{100000};
  std::uint64_t maximumGraphLabels{250000};
  std::uint64_t maximumValidationSamples{100000};
  std::uint64_t maximumCurrentCacheSlices{512};
  double maximumExplorationDistanceNm{5000.0};
  double maximumPredictionAreaSquareDegrees{25000.0};
};

class CancellationToken {
public:
  CancellationToken() : flag_(std::make_shared<std::atomic_bool>(false)) {}
  explicit CancellationToken(std::shared_ptr<std::atomic_bool> flag)
      : flag_(std::move(flag)) {}
  void cancel() const { flag_->store(true, std::memory_order_relaxed); }
  [[nodiscard]] bool cancelled() const {
    return flag_->load(std::memory_order_relaxed);
  }

private:
  std::shared_ptr<std::atomic_bool> flag_;
};

enum class RoutingProgressStage {
  Preflight,
  CurrentCoverage,
  ForwardIsochrone,
  ReverseRecovery,
  FrontierRecovery,
  GraphFallback,
  Validation,
  Complete
};

struct RoutingProgressUpdate {
  RoutingProgressStage stage{RoutingProgressStage::Preflight};
  unsigned attempt{};
  unsigned totalAttempts{};
  std::uint64_t generatedStates{};
  std::uint64_t retainedStates{};
  std::uint64_t landChecks{};
  double closestApproachNm{std::numeric_limits<double>::infinity()};
};

using RoutingProgressCallback =
    std::function<void(const RoutingProgressUpdate&)>;

struct RoutingRequest {
  GeoPoint start;
  GeoPoint destination;
  TimePoint departure{};
  VesselConfiguration vessel;
  EnvironmentalPolicy environment;
  RoutingObjective objective;
  RoutingConstraints constraints;
  RoutingOptions options;
  ResourceLimits limits;
  CancellationToken cancellation;
  // Runtime observer only; configuration serialization intentionally omits it.
  RoutingProgressCallback progress;
};

enum class RequiredUserAction {
  LoadSailingPolar,
  ConfigureVesselPerformance,
  LoadWeatherGrib,
  GenerateWeatherGrib,
  ConfirmClimatologyWindFallback,
  LoadCurrentPredictionDataset,
  ConfirmRoutingWithoutCurrent,
  ConfirmRoutingWithoutWaveData,
  LoadDepthData
};

enum class RoutingWarningCode {
  ClimatologyWindUsed,
  WindCoverageIncomplete,
  XtdCurrentUsed,
  CurrentAssumedZero,
  CurrentCoverageIncomplete,
  WaveDataMissing,
  WaveCoverageIncomplete,
  EstimatedPolar,
  SearchPruned,
  SearchIncomplete,
  EnsembleMemberFailed
};

struct RoutingWarning {
  RoutingWarningCode code{RoutingWarningCode::SearchIncomplete};
  std::string message;
};

struct ParameterCoverage {
  bool available{};
  std::optional<TimePoint> begins;
  std::optional<TimePoint> ends;
  GeoEnvelope area;
  std::string datasetIdentity;
};

struct EnvironmentalCoverageSummary {
  ParameterCoverage wind;
  ParameterCoverage current;
  ParameterCoverage waves;
  bool windFallbackNeeded{};
  bool currentFallbackNeeded{};
  bool waveWaiverNeeded{};
};

struct VesselConfigurationSummary {
  bool validSailingProfile{};
  bool motorSailingConfigured{};
  bool motorConfigured{};
  unsigned profileCount{};
};

struct RoutingPreflightResult {
  bool canRoute{};
  std::vector<RequiredUserAction> requiredActions;
  std::vector<RoutingWarning> warnings;
  EnvironmentalCoverageSummary coverage;
  VesselConfigurationSummary vessel;
};

enum class RoutingStatus {
  Complete,
  CompleteUsingReverseRecovery,
  CompleteUsingFrontierRecovery,
  CompleteUsingGraphFallback,
  NoFeasibleRoute,
  SearchIncomplete,
  ResourceLimitReached,
  WindForecastRequired,
  CurrentDataRequired,
  WaveDataRequired,
  WeatherCoverageInsufficient,
  InvalidPolar,
  InvalidVesselConfiguration,
  InvalidStart,
  InvalidDestination,
  ValidationFailure,
  Cancelled,
  InternalError
};

enum class SolverPath {
  None,
  AdaptiveIsochrone,
  ReverseRecovery,
  FrontierRecovery,
  GraphFallback
};

struct ConstraintMargins {
  double windKnots{std::numeric_limits<double>::infinity()};
  double waveMetres{std::numeric_limits<double>::infinity()};
  double fuelLitres{std::numeric_limits<double>::infinity()};
  double motorSeconds{std::numeric_limits<double>::infinity()};
};

struct RouteLeg {
  GeoPoint start;
  GeoPoint end;
  TimePoint startTime{};
  TimePoint endTime{};
  double headingDegrees{};
  double courseThroughWaterDegrees{};
  double courseOverGroundDegrees{};
  double speedThroughWaterKnots{};
  double speedOverGroundKnots{};
  double trueWindSpeedKnots{};
  double trueWindAngleDegrees{};
  // Meteorological vector components, pointing toward where the air moves.
  // Retained on the delivered leg so chart clients can draw route-time wind
  // barbs without reusing solver state or resampling a different forecast.
  Vector2 wind;
  EnvironmentalSourceMetadata windSource;
  Vector2 current;
  EnvironmentalSourceMetadata currentSource;
  WaveSample waves;
  EnvironmentalSourceMetadata waveSource;
  PropulsionMode propulsionMode{PropulsionMode::Sail};
  ProfileRole profileRole{ProfileRole::SailOnly};
  std::string profileIdentity;
  int sailPlan{-1};
  Tack tack{Tack::Unknown};
  bool tackTransition{};
  bool gybeTransition{};
  bool propulsionTransition{};
  bool stationaryWait{};
  // True when this leg was admitted under the one-way coastal departure
  // egress rule. Consumers which independently replay the delivered route
  // must use the zero-margin (actual-hazard) check for this exact leg, then
  // resume the configured stand-off as soon as this flag becomes false.
  bool coastalDepartureEgress{};
  // Maximum motion-integration slice used by the solver for this leg. The
  // independent validator repeats the same numerical integration partition
  // so a time- or position-varying current cannot create a false endpoint
  // mismatch merely by choosing a different cadence.
  Duration integrationMaximumSlice{};
  double estimatedFuelLitres{};
  ConstraintMargins margins;
  std::vector<RoutingWarning> warnings;
};

// Bounded, deterministic solver geometry intended only for inspection.  Route
// acceptance never depends on this data; the delivered legs are independently
// replayed by RouteValidator.
struct IsochroneContour {
  std::vector<GeoPoint> points;
  bool closed{};
};

// Exact predecessor lineage for one bounded contour point. Chart clients use
// the nearest trace for the optional route-to-cursor inspection overlay; they
// must never interpret it as an accepted route.
struct IsochroneTrace {
  GeoPoint endpoint;
  std::vector<GeoPoint> route;
};

struct IsochroneLayer {
  TimePoint time{};
  // Compatibility view of the first contour for older result consumers.
  // New clients should render contours so disconnected fronts are never
  // joined across land or an unsupported angular gap.
  std::vector<GeoPoint> frontier;
  bool reverse{};
  std::vector<IsochroneContour> contours;
  std::vector<IsochroneTrace> traces;
};

struct RoutingVisualization {
  std::vector<IsochroneLayer> isochrones;
};

struct RouteMetrics {
  Duration elapsed{};
  Duration sailingTime{};
  Duration motorSailingTime{};
  Duration motorOnlyTime{};
  Duration waitingTime{};
  double distanceNm{};
  double estimatedFuelLitres{};
  unsigned propulsionTransitions{};
  unsigned tackCount{};
  unsigned gybeCount{};
  double maximumWindKnots{};
  double maximumWaveHeightMetres{};
};

struct RouteValidationResult {
  bool passed{};
  std::string failureReason;
  std::size_t acceptedPrefixLegs{};
  std::uint64_t samples{};
  EnvironmentalSourceUsage environment;
  std::vector<EnvironmentalSourceTransition> sourceTransitions;
};

struct PruningDiagnostics {
  std::uint64_t dominated{};
  std::uint64_t cellLabelCap{};
  std::uint64_t outsideCorridor{};
  std::uint64_t duplicate{};
};

struct RoutingDiagnostics {
  std::vector<SolverPath> stagesAttempted;
  std::vector<std::string> stageStopReasons;
  std::uint64_t generatedStates{};
  std::uint64_t coastalEndpointGeneratedStates{};
  std::uint64_t forwardGeneratedStates{};
  std::uint64_t frontierRecoveryGeneratedStates{};
  std::uint64_t graphGeneratedStates{};
  std::uint64_t retainedStates{};
  PruningDiagnostics pruned;
  std::uint64_t weatherSamples{};
  std::uint64_t xtdSamples{};
  std::uint64_t xtdCoverageExpansions{};
  std::uint64_t landChecks{};
  std::uint64_t landRejections{};
  std::uint64_t constraintRejections{};
  std::uint64_t propulsionRejections{};
  std::uint64_t reverseLayers{};
  std::uint64_t reverseNodes{};
  std::uint64_t reverseCandidateBridges{};
  std::uint64_t reverseRejectedBridges{};
  std::vector<std::string> reverseRejectionReasons;
  std::uint64_t graphLabels{};
  std::uint64_t frontierRecoveryLabels{};
  std::vector<double> graphCorridorWidthsNm;
  std::uint64_t waitStates{};
  std::uint64_t validationSamples{};
  double closestApproachNm{std::numeric_limits<double>::infinity()};
  std::vector<std::string> resourceLimitEvents;
  std::vector<unsigned> effortTiersAttempted;
  unsigned completedEffortPercent{};
  std::uint64_t cumulativeGeneratedStates{};
};

struct EnsembleMemberResult {
  std::string memberIdentity;
  bool feasible{};
  Duration eta{};
  double maximumWindKnots{};
  double maximumWaveMetres{};
  double fuelLitres{};
  std::string failureReason;
  unsigned routeFamily{};
};

struct EnsembleMetrics {
  unsigned memberCount{};
  unsigned feasibleMemberCount{};
  double feasibilityPercentage{};
  Duration medianEta{};
  Duration lowerEtaQuantile{};
  Duration upperEtaQuantile{};
  double worstCredibleWindKnots{};
  double worstCredibleWaveMetres{};
  double medianFuelLitres{};
  double upperFuelLitres{};
  double routeSpreadNm{};
  unsigned routeFamilyCount{};
  double robustRankingScore{};
  std::vector<EnsembleMemberResult> members;
};

struct RoutingResult {
  RoutingStatus status{RoutingStatus::InternalError};
  std::vector<RouteLeg> legs;
  RouteMetrics metrics;
  RouteValidationResult validation;
  EnvironmentalSourceUsage environment;
  RoutingDiagnostics diagnostics;
  SolverPath solverPath{SolverPath::None};
  std::vector<RoutingWarning> warnings;
  std::vector<EnvironmentalSourceTransition> sourceTransitions;
  EnsembleMetrics ensemble;
  RoutingVisualization visualization;
  std::optional<RoutingPreflightResult> preflight;
  std::string message;
};

}  // namespace supercpn::weather_routing
