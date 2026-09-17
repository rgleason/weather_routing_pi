// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <string>
#include <string_view>

namespace weather_routing {
enum class RoutingEngine { Main, Quick, Unsupported };

struct SearchPreset {
  std::string id{"custom"};
  int revision{0};
  bool operator==(const SearchPreset&) const = default;
};

struct QuickSearchSettings {
  int memoryBudgetMiB{256};
  int offshoreStepMinutes{180};
  double headingStepDegrees{20.0};
  int maximumSearchAngle{120};
  SearchPreset preset{"balanced", 1};
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

  std::string EngineId() const {
    switch (engine) {
      case RoutingEngine::Main: return "main";
      case RoutingEngine::Quick: return "quick";
      default: return unsupportedId;
    }
  }
  void SetEngineId(std::string_view id) {
    unsupportedId.clear();
    if (id == "main") engine = RoutingEngine::Main;
    else if (id == "quick") engine = RoutingEngine::Quick;
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
  bool operator==(const RoutingEngineSettings&) const = default;
};

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
    const bool quick = c.IsQuick();
    return {true, native ? c.EngineSettings.EngineId() : "legacy",
            quick ? c.EngineSettings.quick.preset : c.EngineSettings.mainPreset,
            quick ? c.EngineSettings.quick.offshoreStepMinutes * 60.0 : c.DeltaTime,
            quick ? c.EngineSettings.quick.headingStepDegrees : c.ByDegrees,
            quick ? c.EngineSettings.quick.memoryBudgetMiB : 0,
            static_cast<int>(quick ? c.EngineSettings.quick.maximumSearchAngle : c.MaxSearchAngle),
            quick ? 100 : c.RoutingEffortPercent, c.EffectiveShorelineResolution(), c.DetectLand};
  }
};

// This explicit action must never be called while loading/migrating settings.
// Shared safety, vessel, weather and resource preferences are outside its scope.
template <typename Configuration>
void ResetMainToBalanced(Configuration& configuration) {
  configuration.DeltaTime = 3600;
  configuration.ByDegrees = 5.0;
  configuration.RoutingEffortPercent = 100;
  configuration.MaxSearchAngle = 120;
  configuration.UseReverseReachabilityRecovery = false;
  configuration.EngineSettings.mainPreset = {"balanced", 1};
}
}  // namespace weather_routing
