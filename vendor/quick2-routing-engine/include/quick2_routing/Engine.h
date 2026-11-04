// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "supercpn/weather_routing/Engine.h"

namespace quick2_routing {
namespace wr = supercpn::weather_routing;
inline constexpr char kEngineName[] = "Quick2";
inline constexpr char kEngineIdentity[] = "quick2";
enum class Objective { Fastest, Comfortable };
struct Options {
  Objective objective{Objective::Fastest};
  bool comfortWindOnly{true};
  wr::Duration step{std::chrono::hours{1}};
  unsigned headings{96};
  unsigned frontierLimit{128};
  unsigned labelsPerCell{2};
  double cellNm{1};
  unsigned maximumLayers{672};
  std::uint64_t maximumProposals{2000000};
  std::uint64_t maximumLineageNodes{50000};
  unsigned maximumValidatedCandidates{8};
};
struct Diagnostics {
  std::uint64_t proposals{}, exactMotions{}, duplicateCells{},
      frontierTruncations{};
  std::uint64_t lineageNodes{}, searchArrayBytes{};
  unsigned layers{}, peakFrontier{}, validatedCandidates{};
  double proposalMs{}, materializeMs{}, validationMs{};
};
struct Result {
  wr::RoutingResult route;
  Diagnostics frontier;
  double discomfortHours{};
};
// Independent CPU search, informed by the documented algorithmic principles.
// Approximate proposals never become accepted motion without shared-kernel
// integration and independent chronological validation. No GPU/host API.
class Engine {
public:
  Result route(const wr::RoutingRequest&, const wr::RoutingEnvironment&,
               const Options& = {}) const;
};
using Quick2Engine = Engine;
}  // namespace quick2_routing
