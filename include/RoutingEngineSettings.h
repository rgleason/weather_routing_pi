// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <string>
#include <string_view>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <array>
#include "GribTimelineCachePolicy.h"

namespace weather_routing {
constexpr int kBalancedSearchPresetRevision = 2;
constexpr double kDefaultFromDegree = 40.0;
constexpr double kDefaultToDegree = 160.0;
constexpr double kDefaultHeadingStepDegrees = 10.0;
constexpr int kDefaultQuickShorelineResolution = 2;
constexpr int kDefaultMaxDivertedCourse = 120;

// Serialized IDs remain stable: historical "quick" is Standard, while
// "main" selects the current Professional implementation.
enum class RoutingEngine { Main, Quick, Unsupported, Original, Auto, All, Quick2, Alternative, Atlas };

inline constexpr std::array kSequenceEngines{
    RoutingEngine::Original, RoutingEngine::Quick2, RoutingEngine::Quick,
    RoutingEngine::Alternative, RoutingEngine::Main, RoutingEngine::Atlas};
// Execution/display order is independent of the historical saved bit positions.
inline constexpr std::array kRoutingEngineOrder{
    RoutingEngine::Original, RoutingEngine::Quick2, RoutingEngine::Atlas,
    RoutingEngine::Quick, RoutingEngine::Alternative, RoutingEngine::Main};
inline constexpr std::array kEngineChoices{
    RoutingEngine::Auto, RoutingEngine::Original, RoutingEngine::Quick2,
    RoutingEngine::Atlas, RoutingEngine::Quick, RoutingEngine::Alternative,
    RoutingEngine::Main, RoutingEngine::All};
// Historical defaults remain unchanged. Atlas is available only by opt-in.
inline constexpr unsigned kAllEnginesMask = 31;
inline constexpr unsigned kAutoEnginesMask = 23;
inline constexpr unsigned kSelectableEnginesMask = 63;
inline unsigned EngineBit(RoutingEngine engine) {
  for (unsigned i = 0; i < kSequenceEngines.size(); ++i)
    if (kSequenceEngines[i] == engine) return 1U << i;
  return 0;
}

inline int EngineSelection(RoutingEngine engine) {
  const auto found = std::find(kEngineChoices.begin(), kEngineChoices.end(), engine);
  return found == kEngineChoices.end() ? -1 : static_cast<int>(found - kEngineChoices.begin());
}
inline RoutingEngine EngineFromSelection(int selection) {
  return selection >= 0 && static_cast<unsigned>(selection) < kEngineChoices.size()
      ? kEngineChoices[selection] : RoutingEngine::Unsupported;
}
inline bool IsCombinedEngine(RoutingEngine engine) {
  return engine == RoutingEngine::Auto || engine == RoutingEngine::All;
}
inline const char* EngineTitle(RoutingEngine engine) {
  switch (engine) {
    case RoutingEngine::Original: return "Quick";
    case RoutingEngine::Quick: return "Standard";
    case RoutingEngine::Main: return "Professional";
    case RoutingEngine::Quick2: return "Quick2";
    case RoutingEngine::Alternative: return "Alternative";
    case RoutingEngine::Atlas: return "Atlas";
    case RoutingEngine::Auto: return "Auto";
    case RoutingEngine::All: return "All (slow)";
    default: return "Unsupported";
  }
}

struct SearchPreset {
  std::string id{"custom"};
  int revision{0};
  bool operator==(const SearchPreset&) const = default;
};

struct QuickSearchSettings {
  int memoryBudgetMiB{256};
  int offshoreStepMinutes{180};
  double headingStepDegrees{kDefaultHeadingStepDegrees};
  int maximumSearchAngle{120};
  SearchPreset preset{"balanced", kBalancedSearchPresetRevision};
  bool operator==(const QuickSearchSettings&) const = default;
};

// Quick (the hardened contour engine) uses the route's visible nominal
// timestep, then reduces it near its endpoints. The saved Original offshore
// value remains for Professional's separate contour incumbent; visible Quick
// ignores it. Standard retains its own policy. Do not apply Professional's
// 15-minute quality floor here.
inline std::int64_t QuickTimeStepSeconds(double configuredSeconds) {
  return std::max<std::int64_t>(60, std::llround(configuredSeconds));
}

// Main's numerical values retain their original RouteMapConfiguration fields
// and serialization keys. Quick owns a separate block; switching selects only
// the algorithm and never copies or resets numerical values.
struct RoutingEngineSettings {
  RoutingEngine engine{RoutingEngine::Main};
  std::string unsupportedId;
  SearchPreset mainPreset;
  QuickSearchSettings quick;
  QuickSearchSettings original;
  int originalShorelineResolution{kDefaultQuickShorelineResolution};
  int originalGribTimelineCacheMiB{kQuickGribTimelineCacheDefaultMiB};
  // Membership is independent of each engine's settings and resource limits.
  // Explicit empty selections remain empty and are rejected before routing.
  unsigned autoEngines{kAutoEnginesMask};
  unsigned allEngines{kAllEnginesMask};
  unsigned SelectedEngines() const {
    return engine == RoutingEngine::Auto ? autoEngines : allEngines;
  }

  QuickSearchSettings& FastSettings() {
    return engine == RoutingEngine::Original || engine == RoutingEngine::Main
        ? original : quick;
  }
  const QuickSearchSettings& FastSettings() const {
    return engine == RoutingEngine::Original || engine == RoutingEngine::Main
        ? original : quick;
  }

  std::string EngineId() const {
    switch (engine) {
      case RoutingEngine::Main: return "main";
      case RoutingEngine::Quick: return "quick";
      case RoutingEngine::Original: return "original";
      case RoutingEngine::Auto: return "auto";
      case RoutingEngine::All: return "all";
      case RoutingEngine::Quick2: return "quick2";
      case RoutingEngine::Alternative: return "alternative";
      case RoutingEngine::Atlas: return "atlas";
      default: return unsupportedId;
    }
  }
  void SetEngineId(std::string_view id) {
    unsupportedId.clear();
    if (id == "main" || id == "professional2") engine = RoutingEngine::Main;
    else if (id == "quick") engine = RoutingEngine::Quick;
    else if (id == "original") engine = RoutingEngine::Original;
    else if (id == "auto") engine = RoutingEngine::Auto;
    else if (id == "all") engine = RoutingEngine::All;
    else if (id == "quick2") engine = RoutingEngine::Quick2;
    else if (id == "alternative") engine = RoutingEngine::Alternative;
    else if (id == "atlas") engine = RoutingEngine::Atlas;
    else {
      engine = RoutingEngine::Unsupported;
      unsupportedId = id;
    }
  }
  void ResetQuickToBalanced() {
    const int budget = quick.memoryBudgetMiB;
    quick = QuickSearchSettings{};
    quick.memoryBudgetMiB = budget;
  }
  void ResetOriginalToBalanced() {
    const int budget = original.memoryBudgetMiB;
    original = QuickSearchSettings{};
    original.memoryBudgetMiB = budget;
  }
  bool operator==(const RoutingEngineSettings&) const = default;
};

// Only an absent last-used profile receives the new first-install choice.
// Legacy XML and existing profiles without a selector remain Professional.
inline void ApplyFirstUseEngineDefaults(RoutingEngineSettings& settings,
                                        bool hasSavedDefaults) {
  if (hasSavedDefaults) return;
  settings.engine = RoutingEngine::Auto;
  settings.mainPreset = {"balanced", kBalancedSearchPresetRevision};
}

struct RoutingSearchSnapshot {
  bool valid{false};
  std::string engine;
  SearchPreset preset;
  double timeStepSeconds{0};
  double headingStepDegrees{0};
  int memoryBudgetMiB{0};
  int maximumSearchAngle{0};
  int effortPercent{0};
  int shorelineResolution{4};
  bool detectLand{false};
  std::string selectedEngine;

  template <typename Configuration>
  static RoutingSearchSnapshot Capture(const Configuration& c, bool native) {
    const bool quick = c.EngineSettings.engine == RoutingEngine::Quick ||
                       c.EngineSettings.engine == RoutingEngine::Original;
    const auto& fast = c.EngineSettings.FastSettings();
    return {true, native ? c.EngineSettings.EngineId() : "legacy",
            quick ? fast.preset : c.EngineSettings.mainPreset,
            c.EngineSettings.engine == RoutingEngine::Original
                ? static_cast<double>(QuickTimeStepSeconds(c.DeltaTime))
                : quick ? fast.offshoreStepMinutes * 60.0 : c.DeltaTime,
            quick ? fast.headingStepDegrees : c.ByDegrees,
            quick ? fast.memoryBudgetMiB : 0,
            static_cast<int>(quick ? fast.maximumSearchAngle : c.MaxSearchAngle),
            quick ? 100 : c.RoutingEffortPercent, c.EffectiveShorelineResolution(), c.DetectLand};
  }
};

template <typename Configuration>
void ResetOriginalToBalanced(Configuration& configuration) {
  configuration.DeltaTime = 3600;
  configuration.EngineSettings.ResetOriginalToBalanced();
}

// This explicit action must never be called while loading/migrating settings.
// Shared safety, vessel, weather and resource preferences are outside its scope.
template <typename Configuration>
void ResetMainToBalanced(Configuration& configuration) {
  configuration.DeltaTime = 3600;
  configuration.ByDegrees = kDefaultHeadingStepDegrees;
  configuration.RoutingEffortPercent = 100;
  configuration.MaxSearchAngle = 120;
  configuration.UseReverseReachabilityRecovery = false;
  configuration.EngineSettings.mainPreset = {"balanced", kBalancedSearchPresetRevision};
}
}  // namespace weather_routing
