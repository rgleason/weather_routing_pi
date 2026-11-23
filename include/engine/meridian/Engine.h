// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "supercpn/weather_routing/Engine.h"
#include <functional>
#include <memory>

namespace meridian_routing {
namespace wr = supercpn::weather_routing;
struct Options {
  std::shared_ptr<const wr::LandAndBoundaryProvider> geography;
  std::uint64_t maximumProposals{2000000};
  unsigned maximumNodes{30000}, maximumPending{40000}, headings{48};
  std::function<void(unsigned, wr::Duration, wr::Duration, std::uint64_t)>
      attemptStarted;
};
// Independent, bounded lazy best-first search over continuous sailing states.
// Existing route-search implementations are neither called nor modified.
class Engine {
public:
  wr::RoutingResult route(const wr::RoutingRequest&,
                         const wr::RoutingEnvironment&,
                         const Options& = {}) const;
};
}  // namespace meridian_routing
