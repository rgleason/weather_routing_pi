// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "supercpn/weather_routing/Engine.h"
#include <array>
#include <functional>

namespace corridor_routing {
namespace wr = supercpn::weather_routing;

struct Options {
  bool diversifyApproaches{true};
  bool refineCoastalGeography{true};
  bool useBothGuides{true};
  bool paceCoastalArrivals{true};
  bool coastalPilot{true};
  bool discoverChannels{true};
  bool exploreWeatherCorridors{};
  bool recoverCoastal{};
  bool recoverPassage{};
  // The host can offer a longer search horizon on a failed ocean passage.
  // It applies only to a separate recovery request.
  std::optional<wr::Duration> oceanRecoveryHorizon;
  unsigned additionalMotions{1024};
  // Cheap shoreline guidance may be supplied separately from authoritative
  // motion, chart, depth and final validation providers.
  std::shared_ptr<const wr::LandAndBoundaryProvider> geography;
  // Optional host provider for a blocked departure. It can widen the
  // default course search while retaining the physical safety checks.
  std::function<std::shared_ptr<const wr::LandAndBoundaryProvider>()> departureRecoveryProvider;
};

struct SearchResult {
  wr::RoutingResult route;
  std::array<std::uint64_t, 3> evaluatedByApproach{};
  std::uint64_t checkedMotions{};
  std::uint64_t acceptedArrivals{};
  std::uint64_t guideQueries{};
  std::uint64_t channelQueries{};
  double channelLengthNm{};
  std::uint64_t arrivalProbes{};
  std::uint64_t deferredArrivals{};
  bool fineGeographicGuide{};
  bool wideGeographicGuide{};
  bool pilotValidated{};
  unsigned coastalRecoveryPasses{};
  bool coastalRecoveryValidated{};
  unsigned weatherGuideSamples{};
  bool weatherGuideAvailable{};
  bool weatherGuideImproved{};
  std::optional<wr::Duration> incumbentPassage;
  std::optional<wr::Duration> estimatedWeatherPassage;
  double weatherGuideSeparationNm{};
  std::optional<double> weatherSearchClosestNm;
  std::optional<double> firstValidSeconds;
  std::optional<wr::Duration> firstPassage;
  std::optional<wr::GeoPoint> closestPosition;
};

SearchResult search(const wr::RoutingRequest&, const wr::RoutingEnvironment&,
                    const Options& = {});
}  // namespace corridor_routing
