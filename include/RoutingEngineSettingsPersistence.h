// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <algorithm>
#include <cmath>
#include <wx/config.h>
#include <tinyxml.h>
#include "RoutingEngineSettings.h"

namespace weather_routing {
// Both XML routes and last-used defaults use the same keys and migration.
// The reader returns false for an absent key, which preserves initialized new
// fields. Missing engine selection in an existing profile means legacy Main.
template <typename Reader>
RoutingEngineSettings ReadRoutingEngineSettingsWith(Reader read) {
  RoutingEngineSettings settings;
  wxString value;
  if (read("RoutingEngine", value)) settings.SetEngineId(value.ToStdString());
  else if (read("QuickRoute", value) && (value == "1" || value == "true"))
    settings.engine = RoutingEngine::Quick;
  const auto integer = [&](const char* key, int& destination) {
    long parsed;
    if (read(key, value) && value.ToLong(&parsed))
      destination = static_cast<int>(std::clamp(parsed, 0L, 1000000L));
  };
  if (read("MainSearchPreset", value)) settings.mainPreset.id = value.ToStdString();
  integer("MainSearchPresetRevision", settings.mainPreset.revision);
  if (read("QuickSearchPreset", value)) settings.quick.preset.id = value.ToStdString();
  integer("QuickSearchPresetRevision", settings.quick.preset.revision);
  integer("QuickMemoryBudgetMiB", settings.quick.memoryBudgetMiB);
  integer("QuickOffshoreStepMinutes", settings.quick.offshoreStepMinutes);
  integer("QuickMaximumSearchAngle", settings.quick.maximumSearchAngle);
  if (read("QuickHeadingStepDegrees", value)) {
    double parsed;
    if (value.ToCDouble(&parsed) && std::isfinite(parsed))
      settings.quick.headingStepDegrees = parsed;
  }
  settings.quick.memoryBudgetMiB = std::clamp(settings.quick.memoryBudgetMiB, 1, 4096);
  settings.quick.offshoreStepMinutes = std::clamp(settings.quick.offshoreStepMinutes, 10, 360);
  settings.quick.headingStepDegrees = std::clamp(settings.quick.headingStepDegrees, 5.0, 30.0);
  settings.quick.maximumSearchAngle = std::clamp(settings.quick.maximumSearchAngle, 0, 180);
  // A profile without preset metadata is custom even if its values happen to
  // equal today's defaults. The metadata never drives numerical configuration.
  if (!read("QuickSearchPreset", value) &&
      (read("QuickOffshoreStepMinutes", value) || read("QuickHeadingStepDegrees", value) || read("QuickMaximumSearchAngle", value)))
    settings.quick.preset = {};
  integer("OriginalMemoryBudgetMiB", settings.original.memoryBudgetMiB);
  integer("OriginalOffshoreStepMinutes", settings.original.offshoreStepMinutes);
  integer("OriginalMaximumSearchAngle", settings.original.maximumSearchAngle);
  if (read("OriginalShorelineResolution", value)) {
    long parsed;
    settings.originalShorelineResolution = value.ToLong(&parsed) && parsed >= 0 && parsed <= 4
        ? static_cast<int>(parsed) : 4;
  }
  integer("OriginalGribTimelineCacheMiB", settings.originalGribTimelineCacheMiB);
  if (read("OriginalHeadingStepDegrees", value)) {
    double parsed;
    if (value.ToCDouble(&parsed) && std::isfinite(parsed))
      settings.original.headingStepDegrees = parsed;
  }
  if (read("OriginalSearchPreset", value)) settings.original.preset.id = value.ToStdString();
  else if (read("OriginalOffshoreStepMinutes", value) ||
           read("OriginalHeadingStepDegrees", value) || read("OriginalMaximumSearchAngle", value))
    settings.original.preset = {};
  integer("OriginalSearchPresetRevision", settings.original.preset.revision);
  settings.original.memoryBudgetMiB = std::clamp(settings.original.memoryBudgetMiB, 1, 4096);
  settings.original.offshoreStepMinutes = std::clamp(settings.original.offshoreStepMinutes, 10, 360);
  settings.original.headingStepDegrees = std::clamp(settings.original.headingStepDegrees, 5.0, 30.0);
  settings.original.maximumSearchAngle = std::clamp(settings.original.maximumSearchAngle, 0, 180);
  settings.originalShorelineResolution = std::clamp(settings.originalShorelineResolution, 0, 4);
  settings.originalGribTimelineCacheMiB = NormalizeGribTimelineCacheMiB(settings.originalGribTimelineCacheMiB, true);
  return settings;
}

template <typename Writer>
void WriteRoutingEngineSettingsWith(const RoutingEngineSettings& settings, Writer write) {
  write("RoutingEngine", wxString::FromUTF8(settings.EngineId()));
  write("MainSearchPreset", wxString::FromUTF8(settings.mainPreset.id));
  write("MainSearchPresetRevision", wxString::Format("%d", settings.mainPreset.revision));
  write("QuickSearchPreset", wxString::FromUTF8(settings.quick.preset.id));
  write("QuickSearchPresetRevision", wxString::Format("%d", settings.quick.preset.revision));
  write("QuickMemoryBudgetMiB", wxString::Format("%d", settings.quick.memoryBudgetMiB));
  write("QuickMaximumSearchAngle", wxString::Format("%d", settings.quick.maximumSearchAngle));
  write("QuickOffshoreStepMinutes", wxString::Format("%d", settings.quick.offshoreStepMinutes));
  write("QuickHeadingStepDegrees", wxString::FromCDouble(settings.quick.headingStepDegrees, 10));
  write("OriginalSearchPreset", wxString::FromUTF8(settings.original.preset.id));
  write("OriginalSearchPresetRevision", wxString::Format("%d", settings.original.preset.revision));
  write("OriginalMemoryBudgetMiB", wxString::Format("%d", settings.original.memoryBudgetMiB));
  write("OriginalMaximumSearchAngle", wxString::Format("%d", settings.original.maximumSearchAngle));
  write("OriginalOffshoreStepMinutes", wxString::Format("%d", settings.original.offshoreStepMinutes));
  write("OriginalHeadingStepDegrees", wxString::FromCDouble(settings.original.headingStepDegrees, 10));
  write("OriginalShorelineResolution", wxString::Format("%d", settings.originalShorelineResolution));
  write("OriginalGribTimelineCacheMiB", wxString::Format("%d", settings.originalGribTimelineCacheMiB));
}

inline RoutingEngineSettings ReadRoutingEngineSettings(const TiXmlElement& element) {
  return ReadRoutingEngineSettingsWith([&](const char* key, wxString& value) {
    const char* attribute = element.Attribute(key);
    if (!attribute) return false;
    value = wxString::FromUTF8(attribute);
    return true;
  });
}
inline RoutingEngineSettings ReadRoutingEngineSettings(wxConfigBase& config) {
  return ReadRoutingEngineSettingsWith([&](const char* key, wxString& value) {
    return config.Read(wxString::FromUTF8(key), &value);
  });
}
inline void WriteRoutingEngineSettings(const RoutingEngineSettings& settings,
                                       TiXmlElement& element) {
  WriteRoutingEngineSettingsWith(settings, [&](const char* key, const wxString& value) {
    element.SetAttribute(key, value.ToUTF8());
  });
}
inline void WriteRoutingEngineSettings(const RoutingEngineSettings& settings,
                                       wxConfigBase& config) {
  WriteRoutingEngineSettingsWith(settings, [&](const char* key, const wxString& value) {
    config.Write(wxString::FromUTF8(key), value);
  });
}
}  // namespace weather_routing
