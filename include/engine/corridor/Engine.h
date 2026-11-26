// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "supercpn/weather_routing/Engine.h"
#include <array>

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
  unsigned additionalMotions{1024};
  // Cheap shoreline guidance may be supplied separately from authoritative
  // motion, chart, depth and final validation providers.
  std::shared_ptr<const wr::LandAndBoundaryProvider> geography;
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
};

SearchResult search(const wr::RoutingRequest&, const wr::RoutingEnvironment&,
                    const Options& = {});
}  // namespace corridor_routing
