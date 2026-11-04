// SPDX-License-Identifier: GPL-3.0-or-later
#include <gtest/gtest.h>
#include <wx/fileconf.h>
#include <wx/sstream.h>
#include "ProfessionalQuickPolicy.h"
#include "BundledDataUpgradePolicy.h"
#include "RoutingEngineSettingsPersistence.h"
#include "ShorelineSettings.h"

namespace wr = weather_routing;
TEST(RoutingEngineSettings, ProfessionalQuickAdmissionKeepsMemoryReserve) {
  const auto spacious = wr::SelectProfessionalQuickAdmission(256, 50000, 64);
  EXPECT_EQ(spacious.effectiveMiB, 1024U);
  EXPECT_EQ(spacious.requiredBeforeMiB, 6144U);
  const auto constrained = wr::SelectProfessionalQuickAdmission(256, 4096, 64);
  EXPECT_EQ(constrained.effectiveMiB, 512U);
  EXPECT_LE(constrained.requiredBeforeMiB, constrained.availableMiB);
  EXPECT_EQ(wr::SelectProfessionalQuickAdmission(256, 2200, 64).effectiveMiB,
            0U);
  EXPECT_EQ(wr::SelectProfessionalQuickAdmission(256, 0, 64).effectiveMiB,
            256U);
  EXPECT_EQ(wr::SelectProfessionalQuickAdmission(256, 4096, 32).effectiveMiB,
            192U);
}
TEST(RoutingEngineSettings, ProfessionalUsesItsIndependentQuickBlock) {
  wr::RoutingEngineSettings settings;
  settings.quick.memoryBudgetMiB = 128;
  settings.original.memoryBudgetMiB = 512;
  settings.engine = wr::RoutingEngine::Main;
  EXPECT_EQ(settings.FastSettings().memoryBudgetMiB, 512);
  settings.FastSettings().memoryBudgetMiB = 768;
  EXPECT_EQ(settings.original.memoryBudgetMiB, 768);
  EXPECT_EQ(settings.quick.memoryBudgetMiB, 128);
}
TEST(RoutingEngineSettings, ExperimentalProfessionalIdMigratesToMain) {
  wr::RoutingEngineSettings settings;
  settings.SetEngineId("professional2");
  EXPECT_EQ(settings.engine, wr::RoutingEngine::Main);
  EXPECT_EQ(settings.EngineId(), "main");
}
TEST(RoutingEngineSettings, EightAdvancedTitlesKeepStableSavedIds) {
  wr::RoutingEngineSettings settings;
  const char* titles[] = {"Auto", "Quick", "Quick2", "Atlas", "Standard", "Alternative", "Professional", "All (slow)"};
  const char* ids[] = {"auto", "original", "quick2", "atlas", "quick", "alternative", "main", "all"};
  for (int i = 0; i < 8; ++i) {
    settings.engine = wr::EngineFromSelection(i);
    EXPECT_EQ(wr::EngineSelection(settings.engine), i);
    EXPECT_STREQ(wr::EngineTitle(settings.engine), titles[i]);
    EXPECT_EQ(settings.EngineId(), ids[i]);
  }
  EXPECT_EQ(wr::EngineFromSelection(-1), wr::RoutingEngine::Unsupported);
  EXPECT_EQ(wr::EngineFromSelection(8), wr::RoutingEngine::Unsupported);
  settings.SetEngineId("alternative");
  EXPECT_EQ(settings.engine, wr::RoutingEngine::Alternative);
}
TEST(RoutingEngineSettings, FirstInstallAutoButUpgradePreservesEveryExistingSelection) {
  wr::RoutingEngineSettings fresh;
  wr::ApplyFirstUseEngineDefaults(fresh, false);
  EXPECT_EQ(fresh.engine, wr::RoutingEngine::Auto);
  for (const char* id : {"original", "quick", "main", "auto", "all", "unknown"}) {
    TiXmlElement xml("Configuration");
    xml.SetAttribute("RoutingEngine", id);
    auto saved = wr::ReadRoutingEngineSettings(xml);
    const auto before = saved;
    wr::ApplyFirstUseEngineDefaults(saved, true);
    EXPECT_EQ(saved, before);
  }
  TiXmlElement legacy("Configuration");
  auto existing = wr::ReadRoutingEngineSettings(legacy);
  wr::ApplyFirstUseEngineDefaults(existing, true);
  EXPECT_EQ(existing.engine, wr::RoutingEngine::Main);
}

TEST(RoutingEngineSettings, BundledFirstInstallExamplesSelectAuto) {
  TiXmlDocument document(WEATHER_ROUTING_SOURCE_DIR "/data/WeatherRoutingConfiguration.xml");
  ASSERT_TRUE(document.LoadFile());
  auto* root = document.RootElement();
  ASSERT_NE(root, nullptr);
  int configurations = 0;
  for (auto* element = root->FirstChildElement("Configuration"); element;
       element = element->NextSiblingElement("Configuration")) {
    EXPECT_EQ(wr::ReadRoutingEngineSettings(*element).engine, wr::RoutingEngine::Auto);
    ++configurations;
  }
  EXPECT_GT(configurations, 0);
}
TEST(RoutingEngineSettings, AllThreeBlocksSurviveSwitchSaveReinstallAndIndependentReset) {
  wr::RoutingEngineSettings settings;
  settings.quick = {512, 240, 15, 90, {"custom", 0}};
  settings.original = {128, 120, 12.5, 100, {"custom", 0}};
  settings.mainPreset = {"custom", 7};
  settings.originalShorelineResolution = 4;
  // Persistence stores the effective value supported by this process. The
  // Windows catalogue target is 32-bit and deliberately caps this at 192 MiB.
  const int originalCacheMiB =
      wr::NormalizeGribTimelineCacheMiB(1024, true);
  settings.originalGribTimelineCacheMiB = originalCacheMiB;
  for (int i = 0; i < 8; ++i) {
    settings.engine = wr::EngineFromSelection(i);
    TiXmlElement xml("Configuration");
    wr::WriteRoutingEngineSettings(settings, xml);
    EXPECT_EQ(wr::ReadRoutingEngineSettings(xml), settings);
    wxStringInputStream empty("");
    wxFileConfig profile(empty);
    wr::WriteRoutingEngineSettings(settings, profile);
    wxStringOutputStream output;
    ASSERT_TRUE(profile.Save(output));
    wxStringInputStream restoredInput(output.GetString());
    wxFileConfig restored(restoredInput);
    EXPECT_EQ(wr::ReadRoutingEngineSettings(restored), settings);
  }
  const auto before = settings;
  settings.ResetOriginalToBalanced();
  EXPECT_EQ(settings.quick, before.quick);
  EXPECT_EQ(settings.mainPreset, before.mainPreset);
  EXPECT_EQ(settings.original.memoryBudgetMiB, 128);
  EXPECT_EQ(settings.originalShorelineResolution, 4);
  EXPECT_EQ(settings.originalGribTimelineCacheMiB, originalCacheMiB);
  EXPECT_EQ(settings.original.offshoreStepMinutes, 180);
}
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
  EXPECT_EQ(c.ByDegrees, 10);
  EXPECT_EQ(c.RoutingEffortPercent, 100);
  EXPECT_EQ(c.MaxSearchAngle, 120);
  EXPECT_FALSE(c.UseReverseReachabilityRecovery);
  EXPECT_EQ(c.EngineSettings.mainPreset, (wr::SearchPreset{"balanced", 2}));
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
  EXPECT_EQ(c.EngineSettings.quick, (wr::QuickSearchSettings{96, 180, 10, 120, {"balanced", 2}}));
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

TEST(RoutingEngineSettings, QuickUsesConfiguredNominalStepDespiteSavedOffshoreValue) {
  TiXmlElement xml("Configuration");
  xml.SetAttribute("RoutingEngine", "original");
  xml.SetAttribute("OriginalOffshoreStepMinutes", 180);
  xml.SetAttribute("QuickOffshoreStepMinutes", 240);
  Configuration c;
  c.EngineSettings = wr::ReadRoutingEngineSettings(xml);
  for (const int step : {600, 3600, 7200}) {
    c.DeltaTime = step;
    const auto snapshot = wr::RoutingSearchSnapshot::Capture(c, true);
    EXPECT_EQ(snapshot.timeStepSeconds, step);
    EXPECT_EQ(wr::QuickTimeStepSeconds(c.DeltaTime), step);
  }
  c.EngineSettings.SetEngineId("quick");
  EXPECT_EQ(wr::RoutingSearchSnapshot::Capture(c, true).timeStepSeconds, 14400);
}

TEST(RoutingEngineSettings, QuickPresetResetsNominalStepAndPreservesStandardAndSafety) {
  Configuration c;
  c.EngineSettings.original.memoryBudgetMiB = 128;
  c.EngineSettings.quick = {512, 240, 15, 90, {"custom", 0}};
  const auto before = c;
  wr::ResetOriginalToBalanced(c);
  EXPECT_EQ(c.DeltaTime, 3600);
  EXPECT_EQ(c.EngineSettings.original.memoryBudgetMiB, 128);
  EXPECT_EQ(c.EngineSettings.quick, before.EngineSettings.quick);
  EXPECT_EQ(c.RoutingEffortPercent, before.RoutingEffortPercent);
  EXPECT_EQ(c.MinimumDepthMeters, before.MinimumDepthMeters);
  EXPECT_EQ(c.SafetyMarginLand, before.SafetyMarginLand);
  EXPECT_EQ(c.UseMotor, before.UseMotor);
}

TEST(RoutingEngineSettings, MissingQuickFieldsInitializeOnlyNewSettings) {
  TiXmlElement xml("Configuration");
  xml.SetAttribute("MainSearchPreset", "custom");
  xml.SetAttribute("QuickMemoryBudgetMiB", 96);
  auto settings = wr::ReadRoutingEngineSettings(xml);
  EXPECT_EQ(settings.quick.memoryBudgetMiB, 96);
  EXPECT_EQ(settings.quick.offshoreStepMinutes, 180);
  EXPECT_EQ(settings.quick.headingStepDegrees, 10);
  EXPECT_EQ(settings.mainPreset.id, "custom");
}

TEST(RoutingEngineSettings, OldBalancedPresetKeepsItsResolvedSampling) {
  TiXmlDocument document;
  document.Parse(R"(<Configuration RoutingEngine="quick" QuickSearchPreset="balanced" QuickSearchPresetRevision="1" QuickOffshoreStepMinutes="180" QuickHeadingStepDegrees="20" QuickMaximumSearchAngle="120" QuickShorelineResolution="0" QuickGribTimelineCacheMiB="64" />)");
  auto& xml = *document.RootElement();
  const auto settings = wr::ReadRoutingEngineSettings(xml);
  EXPECT_EQ(settings.quick.headingStepDegrees, 20);
  EXPECT_EQ(settings.quick.preset, (wr::SearchPreset{"balanced", 1}));
  EXPECT_EQ(wr::ReadShorelineResolution(xml, wr::kDefaultQuickShorelineResolution, "QuickShorelineResolution"), 0);
  wr::WriteRoutingEngineSettings(settings, xml);
  EXPECT_EQ(wr::ReadRoutingEngineSettings(xml), settings);
  EXPECT_STREQ(xml.Attribute("QuickGribTimelineCacheMiB"), "64");
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

TEST(RoutingEngineSettings, IndependentMembershipDefaultsAndPersistence) {
  wr::RoutingEngineSettings settings;
  EXPECT_EQ(settings.autoEngines, 23U);
  EXPECT_EQ(settings.allEngines, 31U);
  EXPECT_FALSE(settings.autoEngines & wr::EngineBit(wr::RoutingEngine::Alternative));
  settings.autoEngines = wr::EngineBit(wr::RoutingEngine::Quick2) | wr::EngineBit(wr::RoutingEngine::Quick);
  settings.allEngines = wr::EngineBit(wr::RoutingEngine::Alternative);
  TiXmlElement xml("Configuration");
  wr::WriteRoutingEngineSettings(settings, xml);
  EXPECT_EQ(wr::ReadRoutingEngineSettings(xml), settings);
  wxStringInputStream empty("");
  wxFileConfig profile(empty);
  wr::WriteRoutingEngineSettings(settings, profile);
  EXPECT_EQ(wr::ReadRoutingEngineSettings(profile), settings);
  xml.SetAttribute("AutoEngineMask", "0");
  EXPECT_EQ(wr::ReadRoutingEngineSettings(xml).autoEngines, 0U);
  xml.SetAttribute("AllEngineMask", "999");
  EXPECT_EQ(wr::ReadRoutingEngineSettings(xml).allEngines, 0U);
}
TEST(RoutingEngineSettings, ExperimentalAtlasMembershipRoundTripsWithoutChangingDefaults) {
  wr::RoutingEngineSettings settings;
  settings.autoEngines |= wr::EngineBit(wr::RoutingEngine::Atlas);
  settings.allEngines |= wr::EngineBit(wr::RoutingEngine::Atlas);
  TiXmlElement xml("Configuration");
  wr::WriteRoutingEngineSettings(settings, xml);
  EXPECT_EQ(wr::ReadRoutingEngineSettings(xml), settings);
  wxStringInputStream empty("");
  wxFileConfig profile(empty);
  wr::WriteRoutingEngineSettings(settings, profile);
  EXPECT_EQ(wr::ReadRoutingEngineSettings(profile), settings);
  EXPECT_EQ(wr::RoutingEngineSettings{}.autoEngines, 23U);
  EXPECT_EQ(wr::RoutingEngineSettings{}.allEngines, 31U);
}

TEST(RoutingEngineSettings, FeatureUpgradeDoesNotOfferToOverwritePersonalBoatsOrRoutes) {
  for (int version=124; version<=131; ++version)
    EXPECT_TRUE(wr::CanSkipBundledDataUpdate(version, 1, 32));
  EXPECT_FALSE(wr::CanSkipBundledDataUpdate(0, 1, 32));
  EXPECT_FALSE(wr::CanSkipBundledDataUpdate(123, 1, 32));
  EXPECT_FALSE(wr::CanSkipBundledDataUpdate(133, 1, 32));
  EXPECT_FALSE(wr::CanSkipBundledDataUpdate(131, 2, 0));
  EXPECT_FALSE(wr::CanSkipBundledDataUpdate(132, 1, 34));
  for (int version = 124; version <= 132; ++version)
    EXPECT_TRUE(wr::CanSkipBundledDataUpdate(version, 1, 33));
}

TEST(RoutingEngineSettings, ToolkitUpgradeRetains134DataWithoutSkippingOlderDataUpdates) {
  EXPECT_TRUE(wr::CanSkipBundledDataUpdate(134, 1, 35));
  EXPECT_FALSE(wr::CanSkipBundledDataUpdate(0, 1, 35));
  EXPECT_FALSE(wr::CanSkipBundledDataUpdate(133, 1, 35));
  EXPECT_FALSE(wr::CanSkipBundledDataUpdate(134, 1, 34));
  EXPECT_FALSE(wr::CanSkipBundledDataUpdate(134, 1, 36));
  EXPECT_FALSE(wr::CanSkipBundledDataUpdate(134, 2, 0));
}
