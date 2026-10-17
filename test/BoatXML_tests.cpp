#include <gtest/gtest.h>
#include <wx/filename.h>
#include <fstream>

#include "weather_routing_pi.h"
#include "Boat.h"

namespace {
wxString boatTestPath;

class BoatXML : public testing::Test {
protected:
  void SetUp() override {
    boatTestPath = wxFileName::CreateTempFileName("wr-boat-xml-");
    ASSERT_FALSE(boatTestPath.empty());
    ASSERT_TRUE(wxRemoveFile(boatTestPath));
    ASSERT_TRUE(wxFileName::Mkdir(boatTestPath, wxS_DIR_DEFAULT));
    boatTestPath += wxFileName::GetPathSeparator();
  }
  void TearDown() override {
    wxFileName::Rmdir(boatTestPath, wxPATH_RMDIR_RECURSIVE);
    boatTestPath.clear();
  }
  void LoadPolars(Boat& boat) {
    for (const char* name : {"Example-0-10.pol", "Example-15-30.pol"}) {
      Polar polar;
      wxString message;
      ASSERT_TRUE(polar.Open(wxString(WEATHER_ROUTING_SOURCE_DIR) +
          "/data/polars/Example/" + name, message)) << message;
      boat.Polars.push_back(polar);
    }
  }
};
}  // namespace

// Boat.cpp uses this only to resolve its private polar/contour directory.
// Keep the production save/load implementation under test without a host UI.
wxString weather_routing_pi::StandardPath() { return boatTestPath; }

TEST_F(BoatXML, SaveReloadAndReorderKeepEachPolarsSettings) {
  Boat boat;
  ASSERT_NO_FATAL_FAILURE(LoadPolars(boat));
  boat.Polars[0].lowWindPolicy = LowWindPolicy::Strict;
  boat.Polars[0].highWindPolicy = HighWindPolicy::Hold;
  boat.Polars[0].m_crossoverpercentage = .12;
  boat.Polars[1].lowWindPolicy = LowWindPolicy::Taper;
  boat.Polars[1].highWindPolicy = HighWindPolicy::Strict;
  boat.Polars[1].m_crossoverpercentage = .27;
  const wxString file = boatTestPath + "boat.xml";
  ASSERT_TRUE(boat.SaveXML(file).empty());
  Boat loaded;
  ASSERT_TRUE(loaded.OpenXML(file, false).empty());
  ASSERT_EQ(loaded.Polars.size(), 2U);
  for (std::size_t index = 0; index < 2; ++index) {
    EXPECT_EQ(loaded.Polars[index].FileName, boat.Polars[index].FileName);
    EXPECT_EQ(loaded.Polars[index].lowWindPolicy, boat.Polars[index].lowWindPolicy);
    EXPECT_EQ(loaded.Polars[index].highWindPolicy, boat.Polars[index].highWindPolicy);
    EXPECT_NEAR(loaded.Polars[index].m_crossoverpercentage,
                boat.Polars[index].m_crossoverpercentage, 1e-8);
  }
  EXPECT_EQ(loaded.FindBestPolarForCondition(-1, 35, 60, 0, false), 0);
  std::swap(loaded.Polars[0], loaded.Polars[1]);
  ASSERT_TRUE(loaded.SaveXML(file).empty());
  Boat reordered;
  ASSERT_TRUE(reordered.OpenXML(file, false).empty());
  ASSERT_EQ(reordered.Polars.size(), 2U);
  EXPECT_EQ(reordered.Polars[0].FileName, boat.Polars[1].FileName);
  EXPECT_EQ(reordered.Polars[0].lowWindPolicy, LowWindPolicy::Taper);
  EXPECT_EQ(reordered.Polars[0].highWindPolicy, HighWindPolicy::Strict);
  EXPECT_EQ(reordered.Polars[1].FileName, boat.Polars[0].FileName);
  EXPECT_EQ(reordered.Polars[1].lowWindPolicy, LowWindPolicy::Strict);
  EXPECT_EQ(reordered.Polars[1].highWindPolicy, HighWindPolicy::Hold);
}

TEST_F(BoatXML, LegacyBoatRetainsMultiplePolarsAndAutomaticDefaults) {
  Boat boat;
  ASSERT_NO_FATAL_FAILURE(LoadPolars(boat));
  const wxString file = boatTestPath + "legacy.xml";
  std::ofstream xml(file.ToStdString());
  xml << "<OpenCPNWeatherRoutingBoat>\n";
  for (const auto& polar : boat.Polars)
    xml << "<Polar FileName=\"" << polar.FileName.ToStdString()
        << "\" CrossOverPercentage=\"20\"/>\n";
  xml << "</OpenCPNWeatherRoutingBoat>\n";
  xml.close();
  Boat loaded;
  ASSERT_TRUE(loaded.OpenXML(file, false).empty());
  ASSERT_EQ(loaded.Polars.size(), 2U);
  for (std::size_t index = 0; index < 2; ++index) {
    EXPECT_EQ(loaded.Polars[index].FileName, boat.Polars[index].FileName);
    EXPECT_EQ(loaded.Polars[index].lowWindPolicy, LowWindPolicy::Automatic);
    EXPECT_EQ(loaded.Polars[index].highWindPolicy, HighWindPolicy::Automatic);
    EXPECT_NEAR(loaded.Polars[index].m_crossoverpercentage, .2, 1e-8);
  }
}

TEST_F(BoatXML, MultiplePolarSelectionRespectsSailWindLimits) {
  Boat boat;
  ASSERT_NO_FATAL_FAILURE(LoadPolars(boat));
  EXPECT_EQ(boat.FindBestPolarForCondition(-1, 5, 60, 0, false), 0);
  EXPECT_EQ(boat.FindBestPolarForCondition(0, 18.2, 60, 0, false), 1);
  EXPECT_EQ(boat.FindBestPolarForCondition(1, 12, 60, 0, false), -1);
  EXPECT_EQ(boat.FindBestPolarForCondition(1, 35, 60, 0, false), -1);
}
