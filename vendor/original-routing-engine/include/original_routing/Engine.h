// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <chrono>
#include "supercpn/weather_routing/Engine.h"

namespace original_routing {
namespace wr = supercpn::weather_routing;

struct Options {
  std::chrono::milliseconds maximumRuntime{std::chrono::minutes{15}};
  std::uint64_t maximumGeometryOperations{200000000};
  std::uint64_t maximumWeatherQueries{20000000};
  unsigned maximumValidatedCandidates{64};
  // Search remains the catalogue contour algorithm. Only candidate routes
  // pay for five-minute chronological integration and independent replay.
  wr::Duration replaySlice{std::chrono::minutes{5}};
  // Optional, bounded diagnostic display data; never accepted sailing legs.
  bool captureVisualization{false};
};

// Stateless, reentrant engine. Providers belong to the request; sharing one
// provider across concurrent calls requires that provider's const methods to
// support concurrent reads. No host API, wxWidgets, global binding or TLS.
class Engine {
public:
  wr::RoutingResult route(const wr::RoutingRequest& request,
                          const wr::RoutingEnvironment& environment,
                          const Options& options = {}) const;
};
}  // namespace original_routing
