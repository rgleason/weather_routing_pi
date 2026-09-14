// SPDX-License-Identifier: GPL-3.0-or-later
#include <gtest/gtest.h>
#include <wx/fileconf.h>
#include <wx/sstream.h>
#include "RoutingEngineSettingsPersistence.h"
#include "ShorelineSettings.h"

namespace wr = weather_routing;
namespace {
// Search actions accept the same fields as RouteMapConfiguration without
// coupling persistence tests to the OpenCPN host and routing worker lifecycle.
struct Configuration {
  wr::RoutingEngineSettings EngineSettings;
  int ShorelineResolution{3};
  int QuickShorelineResolution{0};
  int ChartShorelineResolution{0};
  int EffectiveShorelineResolution() const { return IsQuick() ? QuickShorelineResolution : ShorelineResolution; }
  bool DetectLand{true};
  double DeltaTime{10800}, ByDegrees{10}, MaxSearchAngle{95};
  int RoutingEffortPercent{200};
  bool UseReverseReachabilityRecovery{true};
  double MinimumDepthMeters{12}, SafetyMarginLand{2}, UpwindEfficiency{0.83};
  bool UseMotor{true}, AllowDataDeficient{false};
  bool IsQuick() const { return EngineSettings.engine == wr::RoutingEngine::Quick; }
};
}

TEST(RoutingEngineSettings, LegacyXmlSelectsMainWithoutResettingSavedValues) {
  TiXmlDocument document;
  document.Parse(R"(<Configuration dt="10800" ByDegrees="10" RoutingEffortPercent="200" MinimumDepthMeters="12" SafetyMarginLand="2" />)");
  auto& xml = *document.RootElement();
  auto settings = wr::ReadRoutingEngineSettings(xml);
  EXPECT_EQ(settings.engine, wr::RoutingEngine::Main);
  EXPECT_EQ(settings.mainPreset.id, "custom");
  wr::WriteRoutingEngineSettings(settings, xml);
  EXPECT_STREQ(xml.Attribute("dt"), "10800");
  EXPECT_STREQ(xml.Attribute("ByDegrees"), "10");
  EXPECT_STREQ(xml.Attribute("RoutingEffortPercent"), "200");
  EXPECT_STREQ(xml.Attribute("MinimumDepthMeters"), "12");
  EXPECT_STREQ(xml.Attribute("SafetyMarginLand"), "2");
}

TEST(RoutingEngineSettings, PrototypeQuickMigratesAndExplicitEngineTakesPrecedence) {
  TiXmlElement xml("Configuration");
  xml.SetAttribute("QuickRoute", 1);
  xml.SetAttribute("QuickMemoryBudgetMiB", 512);
  auto settings = wr::ReadRoutingEngineSettings(xml);
  EXPECT_EQ(settings.engine, wr::RoutingEngine::Quick);
  EXPECT_EQ(settings.quick.memoryBudgetMiB, 512);
  xml.SetAttribute("RoutingEngine", "main");
  EXPECT_EQ(wr::ReadRoutingEngineSettings(xml).engine, wr::RoutingEngine::Main);
}

TEST(RoutingEngineSettings, UnknownEngineIsPreservedForExplicitRejection) {
  TiXmlElement xml("Configuration");
  xml.SetAttribute("RoutingEngine", "future-engine");
  auto settings = wr::ReadRoutingEngineSettings(xml);
  EXPECT_EQ(settings.engine, wr::RoutingEngine::Unsupported);
  wr::WriteRoutingEngineSettings(settings, xml);
  EXPECT_STREQ(xml.Attribute("RoutingEngine"), "future-engine");
}

TEST(RoutingEngineSettings, XmlRoundTripPreservesBothEnginesAndPresetRevisions) {
  wr::RoutingEngineSettings settings;
  settings.engine = wr::RoutingEngine::Quick;
  settings.mainPreset = {"balanced", 7};
  settings.quick = {768, 240, 12.5, 85, {"balanced", 19}};
  TiXmlElement xml("Configuration");
  wr::WriteRoutingEngineSettings(settings, xml);
  // Metadata does not replace resolved settings with current factory values.
  EXPECT_EQ(wr::ReadRoutingEngineSettings(xml), settings);
}

TEST(RoutingEngineSettings, LastUsedDefaultsRoundTripAndReinstallKeepConfiguration) {
  wxStringInputStream input("[PlugIns/WeatherRouting/LastUsedConfiguration]\nDeltaTime=21600\nByDegrees=20\nQuickRoute=1\nQuickMemoryBudgetMiB=1024\n");
  wxFileConfig config(input);
  config.SetPath("/PlugIns/WeatherRouting/LastUsedConfiguration");
  auto settings = wr::ReadRoutingEngineSettings(config);
  EXPECT_EQ(settings.engine, wr::RoutingEngine::Quick);
  EXPECT_EQ(settings.quick.memoryBudgetMiB, 1024);
  wr::WriteRoutingEngineSettings(settings, config);
  wxStringOutputStream saved;
  ASSERT_TRUE(config.Save(saved));
  wxStringInputStream restoredInput(saved.GetString());
  wxFileConfig restored(restoredInput);
  restored.SetPath("/PlugIns/WeatherRouting/LastUsedConfiguration");
  EXPECT_EQ(wr::ReadRoutingEngineSettings(restored), settings);
  EXPECT_EQ(restored.ReadLong("DeltaTime", 0), 21600);
  EXPECT_EQ(restored.ReadLong("ByDegrees", 0), 20);
}

TEST(RoutingEngineSettings, SwitchingEnginesDoesNotChangeEitherSearchConfiguration) {
  Configuration c;
  c.EngineSettings.quick = {1024, 240, 15, 85, {"custom", 0}};
  const auto before = c;
  c.EngineSettings.SetEngineId("quick");
  c.EngineSettings.SetEngineId("main");
  EXPECT_EQ(c.EngineSettings, before.EngineSettings);
  EXPECT_EQ(c.DeltaTime, before.DeltaTime);
  EXPECT_EQ(c.ByDegrees, before.ByDegrees);
  EXPECT_EQ(c.RoutingEffortPercent, before.RoutingEffortPercent);
}

TEST(RoutingEngineSettings, ResetMainChangesOnlyItsSearchTuning) {
  Configuration c;
  c.EngineSettings.quick = {1024, 240, 15, 85, {"custom", 0}};
  const auto before = c;
  wr::ResetMainToBalanced(c);
  EXPECT_EQ(c.DeltaTime, 3600);
  EXPECT_EQ(c.ByDegrees, 5);
  EXPECT_EQ(c.RoutingEffortPercent, 100);
  EXPECT_EQ(c.MaxSearchAngle, 120);
  EXPECT_FALSE(c.UseReverseReachabilityRecovery);
  EXPECT_EQ(c.EngineSettings.mainPreset, (wr::SearchPreset{"balanced", 1}));
  EXPECT_EQ(c.EngineSettings.quick, before.EngineSettings.quick);
  EXPECT_EQ(c.EngineSettings.engine, before.EngineSettings.engine);
  EXPECT_EQ(c.MinimumDepthMeters, before.MinimumDepthMeters);
  EXPECT_EQ(c.SafetyMarginLand, before.SafetyMarginLand);
  EXPECT_EQ(c.UpwindEfficiency, before.UpwindEfficiency);
  EXPECT_EQ(c.UseMotor, before.UseMotor);
  EXPECT_EQ(c.AllowDataDeficient, before.AllowDataDeficient);
}

TEST(RoutingEngineSettings, ResetQuickPreservesItsMemoryBudgetAndEveryMainValue) {
  Configuration c;
  c.EngineSettings.quick = {96, 240, 15, 85, {"custom", 0}};
  const auto before = c;
  c.EngineSettings.ResetQuickToBalanced();
  EXPECT_EQ(c.EngineSettings.quick, (wr::QuickSearchSettings{96, 180, 20, 120, {"balanced", 1}}));
  EXPECT_EQ(c.EngineSettings.mainPreset, before.EngineSettings.mainPreset);
  EXPECT_EQ(c.DeltaTime, before.DeltaTime);
  EXPECT_EQ(c.ByDegrees, before.ByDegrees);
  EXPECT_EQ(c.MaxSearchAngle, before.MaxSearchAngle);
  EXPECT_EQ(c.RoutingEffortPercent, before.RoutingEffortPercent);
  EXPECT_EQ(c.MinimumDepthMeters, before.MinimumDepthMeters);
}

TEST(RoutingEngineSettings, ResultProvenanceSurvivesEngineSwitchAndPresetReset) {
  Configuration c;
  const auto main = wr::RoutingSearchSnapshot::Capture(c, true);
  c.EngineSettings.SetEngineId("quick");
  c.EngineSettings.quick.memoryBudgetMiB = 512;
  const auto quick = wr::RoutingSearchSnapshot::Capture(c, true);
  wr::ResetMainToBalanced(c);
  c.EngineSettings.ResetQuickToBalanced();
  c.EngineSettings.SetEngineId("main");
  EXPECT_EQ(main.engine, "main");
  EXPECT_EQ(main.timeStepSeconds, 10800);
  EXPECT_EQ(main.headingStepDegrees, 10);
  EXPECT_EQ(main.effortPercent, 200);
  EXPECT_EQ(quick.engine, "quick");
  EXPECT_EQ(quick.memoryBudgetMiB, 512);
  EXPECT_EQ(quick.timeStepSeconds, 10800);
}

TEST(RoutingEngineSettings, MissingQuickFieldsInitializeOnlyNewSettings) {
  TiXmlElement xml("Configuration");
  xml.SetAttribute("MainSearchPreset", "custom");
  xml.SetAttribute("QuickMemoryBudgetMiB", 96);
  auto settings = wr::ReadRoutingEngineSettings(xml);
  EXPECT_EQ(settings.quick.memoryBudgetMiB, 96);
  EXPECT_EQ(settings.quick.offshoreStepMinutes, 180);
  EXPECT_EQ(settings.quick.headingStepDegrees, 20);
  EXPECT_EQ(settings.mainPreset.id, "custom");
}

TEST(ShorelineSettings, MigrationAndIndependentEngineRoundTrips) {
  TiXmlElement xml("Configuration");
  EXPECT_EQ(wr::ReadShorelineResolution(xml, 3), 3);
  EXPECT_EQ(wr::ReadShorelineResolution(xml, 4), 4);
  EXPECT_EQ(wr::ReadShorelineResolution(xml, 0, "QuickShorelineResolution"), 0);
  for (int main = 0; main < 5; ++main) for (int quick = 0; quick < 5; ++quick) {
    xml.SetAttribute("ShorelineResolution", main);
    xml.SetAttribute("QuickShorelineResolution", quick);
    EXPECT_EQ(wr::ReadShorelineResolution(xml, 4), main);
    EXPECT_EQ(wr::ReadShorelineResolution(xml, 0, "QuickShorelineResolution"), quick);
  }
}
TEST(ShorelineSettings, InvalidSavedValuesNeverSilentlyReduceDetail) {
  for (const char* value : {"-1", "5", "9999999999999999999999999", "bad", "1.5"}) {
    TiXmlElement xml("Configuration");
    xml.SetAttribute("ShorelineResolution", value);
    EXPECT_EQ(wr::ReadShorelineResolution(xml, 0), 4);
  }
}
TEST(ShorelineSettings, LastUsedDefaultsPreserveZeroAndIndependentQuickChoice) {
  wxStringInputStream input("ShorelineResolution=0\nQuickShorelineResolution=3\n");
  wxFileConfig c(input);
  EXPECT_EQ(wr::ReadShorelineResolution(c, 4), 0);
  EXPECT_EQ(wr::ReadShorelineResolution(c, 0, "QuickShorelineResolution"), 3);
}
TEST(ShorelineSettings, EngineSwitchingPresetsAndResultSnapshotPreserveChoices) {
  Configuration c;
  EXPECT_EQ(c.EffectiveShorelineResolution(), 3);
  c.EngineSettings.engine = wr::RoutingEngine::Quick;
  EXPECT_EQ(c.EffectiveShorelineResolution(), 0);
  c.QuickShorelineResolution = 2;
  auto result = wr::RoutingSearchSnapshot::Capture(c, true);
  c.EngineSettings.ResetQuickToBalanced();
  wr::ResetMainToBalanced(c);
  EXPECT_EQ(c.QuickShorelineResolution, 2);
  EXPECT_EQ(c.ShorelineResolution, 3);
  c.QuickShorelineResolution = 4;
  EXPECT_EQ(result.shorelineResolution, 2);
  EXPECT_TRUE(result.detectLand);
  c.EngineSettings.engine = wr::RoutingEngine::Main;
  EXPECT_EQ(c.EffectiveShorelineResolution(), 3);
}

TEST(ShorelineSettings, ChartChoiceMigratesSeparatelyAndSurvivesPresets) {
  TiXmlElement xml("Configuration");
  xml.SetAttribute("ShorelineResolution", 4);
  xml.SetAttribute("QuickShorelineResolution", 2);
  EXPECT_EQ(wr::ReadShorelineResolution(xml, 0, "ChartShorelineResolution"), 0);
  xml.SetAttribute("ChartShorelineResolution", 3);
  EXPECT_EQ(wr::ReadShorelineResolution(xml, 0, "ChartShorelineResolution"), 3);
  Configuration c; c.ChartShorelineResolution = 3;
  wr::ResetMainToBalanced(c); c.EngineSettings.ResetQuickToBalanced();
  EXPECT_EQ(c.ChartShorelineResolution, 3);
}
