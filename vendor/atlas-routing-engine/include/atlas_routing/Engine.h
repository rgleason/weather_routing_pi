// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "supercpn/weather_routing/Engine.h"
#include <functional>

namespace atlas_routing {
namespace wr = supercpn::weather_routing;
inline constexpr char kEngineIdentity[] = "atlas";

// Spatial resolution and temporal label resolution are separate. There is no
// global propagation timestep or heading fan. Increasing these allowances
// admits more spatial choices, arrival histories and refinement passes.
struct Options {
  unsigned maximumWaypoints{2400};
  unsigned neighbours{24};
  unsigned refinementPasses{2};
  unsigned labelsPerBucket{2};
  double minimumCellNm{0.12};
  double coastalCellNm{0.6};
  double domainWidthFraction{0.4};
  wr::Duration arrivalBucket{std::chrono::minutes{15}};
  wr::Duration waitStep{std::chrono::minutes{15}};
  wr::Duration maximumEdgeDuration{std::chrono::hours{36}};
  std::uint64_t maximumEdgeAttempts{30000};
  std::uint64_t maximumWeatherQueries{8000000};
  std::uint64_t maximumGeometryQueries{2000000};
  std::uint64_t maximumLabels{60000};
  std::uint64_t maximumQueueEntries{200000};
  // Reserved for search structures; provider/validator allocations are
  // external.
  std::uint64_t memoryBudgetBytes{96ULL * 1024 * 1024};
  // After the first valid route, continue each pass for this many edge solves.
  // Zero returns the first independently validated candidate.
  unsigned improvementAttempts{200};
  bool timeDependentGuidance{true};
  bool weatherFirstPilot{true};
  bool weatherLaneProbes{true};
  bool averageGuidanceTiles{false};
  bool alwaysBuildWeatherField{false};
  // Optional inexpensive geometry for scheduling/refinement only. Physical
  // edges, vertex admission and validation retain the original provider. An
  // unsuccessful guided pass falls back to original geography.
  std::shared_ptr<const wr::LandAndBoundaryProvider> geographyGuidance;
  // Called synchronously for each strictly improving, independently validated
  // incumbent. Copy the result to retain it after the callback. Exceptions are
  // isolated, like the shared progress/preview observers.
  std::function<void(const wr::RoutingResult&)> onIncumbent;
};

struct Diagnostics {
  std::uint64_t waypoints{}, graphEdges{}, expandedLabels{}, edgeAttempts{},
      shootingIntegrations{}, rejectedEdges{}, bucketPruned{}, queuePeak{},
      labelsPeak{}, weatherQueries{}, polarQueries{}, geometryQueries{},
      validationCandidates{}, validationFailures{}, memoryPeakBytes{};
  std::uint64_t guidanceGeometryQueries{};
  unsigned passes{};
  double preprocessingMs{}, searchMs{}, reconstructionMs{}, validationMs{},
      firstValidMs{}, totalMs{};
  double valueFieldMs{};
  std::uint64_t valueFieldCells{}, valueFieldControls{};
  std::uint64_t pilotLegs{};
  std::uint64_t laneProbes{};
  std::vector<std::int64_t> incumbentSeconds;
};

struct Result {
  wr::RoutingResult route;
  Diagnostics atlas;
};

class Engine {
public:
  [[nodiscard]] Result route(const wr::RoutingRequest&,
                             const wr::RoutingEnvironment&,
                             const Options& = {}) const;
};
}  // namespace atlas_routing
