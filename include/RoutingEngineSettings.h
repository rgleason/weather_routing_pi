// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <string>
#include <string_view>
#include "GribTimelineCachePolicy.h"

namespace weather_routing {
constexpr int kBalancedSearchPresetRevision = 2;
constexpr double kDefaultFromDegree = 40.0;
constexpr double kDefaultToDegree = 160.0;
constexpr double kDefaultHeadingStepDegrees = 10.0;
constexpr int kDefaultQuickShorelineResolution = 2;
constexpr int kDefaultMaxDivertedCourse = 120;

// Serialized IDs are permanent: historical "quick" is now Standard, and
// "main" is Professional. Never reinterpret an existing user's selection.
enum class RoutingEngine { Main, Quick, Unsupported, Original };

inline int EngineSelection(RoutingEngine engine) {
  switch (engine) {
    case RoutingEngine::Original: return 0;
    case RoutingEngine::Quick: return 1;
    case RoutingEngine::Main: return 2;
    default: return -1;
  }
}
inline RoutingEngine EngineFromSelection(int selection) {
  return selection == 0 ? RoutingEngine::Original : selection == 1
      ? RoutingEngine::Quick : selection == 2 ? RoutingEngine::Main
      : RoutingEngine::Unsupported;
}
inline const char* EngineTitle(RoutingEngine engine) {
  switch (engine) {
    case RoutingEngine::Original: return "Quick";
    case RoutingEngine::Quick: return "Standard";
    case RoutingEngine::Main: return "Professional";
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

  QuickSearchSettings& FastSettings() {
    return engine == RoutingEngine::Original ? original : quick;
  }
  const QuickSearchSettings& FastSettings() const {
    return engine == RoutingEngine::Original ? original : quick;
  }

  std::string EngineId() const {
    switch (engine) {
      case RoutingEngine::Main: return "main";
      case RoutingEngine::Quick: return "quick";
      case RoutingEngine::Original: return "original";
      default: return unsupportedId;
    }
  }
  void SetEngineId(std::string_view id) {
    unsupportedId.clear();
    if (id == "main") engine = RoutingEngine::Main;
    else if (id == "quick") engine = RoutingEngine::Quick;
    else if (id == "original") engine = RoutingEngine::Original;
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
  settings.engine = RoutingEngine::Original;
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

  template <typename Configuration>
  static RoutingSearchSnapshot Capture(const Configuration& c, bool native) {
    const bool quick = c.EngineSettings.engine == RoutingEngine::Quick ||
                       c.EngineSettings.engine == RoutingEngine::Original;
    const auto& fast = c.EngineSettings.FastSettings();
    return {true, native ? c.EngineSettings.EngineId() : "legacy",
            quick ? fast.preset : c.EngineSettings.mainPreset,
            quick ? fast.offshoreStepMinutes * 60.0 : c.DeltaTime,
            quick ? fast.headingStepDegrees : c.ByDegrees,
            quick ? fast.memoryBudgetMiB : 0,
            static_cast<int>(quick ? fast.maximumSearchAngle : c.MaxSearchAngle),
            quick ? 100 : c.RoutingEffortPercent, c.EffectiveShorelineResolution(), c.DetectLand};
  }
};

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
