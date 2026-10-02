/***************************************************************************
 *   Copyright (C) 2024 by OpenCPN development team                        *
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 2 of the License, or     *
 *   (at your option) any later version.                                   *
 *                                                                         *
 *   This program is distributed in the hope that it will be useful,       *
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of        *
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the         *
 *   GNU General Public License for more details.                          *
 *                                                                         *
 *   You should have received a copy of the GNU General Public License     *
 *   along with this program; if not, write to the                         *
 *   Free Software Foundation, Inc.,                                       *
 *   59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.             *
 **************************************************************************/

#include <gtest/gtest.h>
#include <Polar.h>
#include <wx/filename.h>
#include <fstream>
#include <iterator>
#include <vector>
#include <bzlib.h>
#include <zlib.h>

class PolarTest: public ::testing::Test {
protected:
  wxString
    m_testDataDir = TESTDATADIR,
    m_testPolarFileRelativePath = "polars/Hallberg-Rassy_40_test.pol",
    m_testPolarFileName = m_testDataDir + "/" + m_testPolarFileRelativePath,
    m_testFileOpenMessage = "";
    Polar m_polar;

  PolarTest() {
    // You can do set-up work for each test here.
  }

  ~PolarTest() override {
    // You can do clean-up work that doesn't throw exceptions here.
  }

  void SetUp() override {
    // Code here will be called immediately after the constructor (right
    // before each test).
    bool success = m_polar.Open(m_testPolarFileName, m_testFileOpenMessage);
    EXPECT_EQ(success, true) << "Failed to open polar file: " << m_testPolarFileName;
  }

  void TearDown() override {
    // Code here will be called immediately after each test (right
    // before the destructor).
  }
};

TEST_F(PolarTest, OpenFailed) {
  Polar polar;
  wxString filename = "invalid.xml", message = "";
  bool success = polar.Open(filename, message);
  EXPECT_EQ(success, false);
}

TEST_F(PolarTest, OpensCompressedPolarsWithLineReader) {
  std::ifstream source(m_testPolarFileName.ToStdString(), std::ios::binary);
  ASSERT_TRUE(source);
  const std::string contents(std::istreambuf_iterator<char>{source}, {});
  ASSERT_FALSE(contents.empty());

  const wxString base = wxFileName::CreateTempFileName("wr-polar-compressed-");
  ASSERT_FALSE(base.empty());
  wxRemoveFile(base);

  const wxString gzipPath = base + ".pol.gz";
  {
    gzFile gzip = gzopen(gzipPath.utf8_str(), "wb");
    ASSERT_NE(gzip, nullptr);
    ASSERT_EQ(gzwrite(gzip, contents.data(), contents.size()),
              static_cast<int>(contents.size()));
    ASSERT_EQ(gzclose(gzip), Z_OK);
  }

  const wxString bzipPath = base + ".pol.bz2";
  {
    std::vector<char> input(contents.begin(), contents.end());
    unsigned int packedLength = static_cast<unsigned int>(input.size() * 1.01 + 601);
    std::vector<char> packed(packedLength);
    ASSERT_EQ(BZ2_bzBuffToBuffCompress(packed.data(), &packedLength,
                                      input.data(), input.size(), 9, 0, 30),
              BZ_OK);
    std::ofstream out(bzipPath.ToStdString(), std::ios::binary);
    ASSERT_TRUE(out);
    out.write(packed.data(), packedLength);
  }

  for (const wxString& path : {gzipPath, bzipPath}) {
    // Create the fixture using zlib's portable narrow API, then rename it
    // so the reader must handle a Unicode filename on Windows as well.
    const wxString unicodePath = path + wxString::FromUTF8("-\xc3\xa9-\xe8\x88\xb9") +
        (path == gzipPath ? ".gz" : ".bz2");
    SCOPED_TRACE(std::string(unicodePath.utf8_str()));
    ASSERT_TRUE(wxRenameFile(path, unicodePath));
    Polar loaded;
    wxString message;
    ASSERT_TRUE(loaded.Open(unicodePath, message)) << message;
    PolarSpeedStatus status;
    EXPECT_NEAR(loaded.Speed(10, 10, &status, false), 1.3, 1e-6);
    wxRemoveFile(unicodePath);
  }
}

TEST_F(PolarTest, EmptyDimensionsCannotTruncateAnExistingPolar) {
  const wxString file = wxFileName::CreateTempFileName("wr-polar-save-");
  ASSERT_FALSE(file.IsEmpty());
  {
    std::ofstream out(file.ToStdString());
    out << "existing polar must survive";
  }
  for (bool noWind : {false, true}) {
    Polar empty;
    empty.AddDegreeStep(45);
    empty.AddWindSpeed(10);
    if (noWind) empty.RemoveWindSpeed(0);
    else empty.RemoveDegreeStep(0);
    EXPECT_FALSE(empty.Save(file));
    PolarSpeedStatus status;
    EXPECT_TRUE(std::isnan(empty.Speed(45, 10, &status, false)));
    EXPECT_EQ(status, POLAR_SPEED_NO_POLAR_DATA);
    for (float value : empty.GetVMGTrueWind(10).values)
      EXPECT_TRUE(std::isnan(value));
    for (float value : empty.GetVMGApparentWind(10).values)
      EXPECT_TRUE(std::isnan(value));
    std::ifstream in(file.ToStdString());
    EXPECT_EQ(std::string(std::istreambuf_iterator<char>(in), {}),
              "existing polar must survive");
  }
  wxRemoveFile(file);
}

TEST_F(PolarTest, ClosestVWiBasic) {
  int VW1i, VW2i;
  m_polar.ClosestVWi(10, VW1i, VW2i);
  EXPECT_EQ(VW1i, 4);
  EXPECT_EQ(VW2i, 5);
}

TEST_F(PolarTest, SpeedBasic) {
  
  PolarSpeedStatus status;
  
  double speed = m_polar.Speed(10, 10, &status, false);
  EXPECT_NEAR(speed, 1.3, 1e-6);
}

TEST(PolarWindRangeTest, LightWindExampleCannotInventFastStrongWindSpeeds) {
  Polar polar;
  wxString message;
  ASSERT_TRUE(polar.Open(
      wxString(WEATHER_ROUTING_SOURCE_DIR) +
          "/data/polars/Example/Example-0-10.pol", message));
  PolarSpeedStatus status;
  // Previously the 9--10 knot slope produced 13.88 knots at 18.2 knots
  // of wind (333 miles/day), despite this being a light-wind sail table.
  EXPECT_NEAR(polar.Speed(60.0, 18.2, &status, false), 6.5, 1e-6);
  EXPECT_EQ(status, POLAR_SPEED_SUCCESS);
  EXPECT_TRUE(std::isnan(polar.Speed(60.0, 18.2, &status, true)));
  EXPECT_EQ(status, POLAR_SPEED_WIND_TOO_STRONG);
}

TEST(PolarWindRangeTest, PermittedLightWindFallbackTapersToZero) {
  Polar polar;
  wxString message;
  ASSERT_TRUE(polar.Open(
      wxString(WEATHER_ROUTING_SOURCE_DIR) +
          "/data/polars/Example/Example-6-24.pol", message));
  PolarSpeedStatus status;
  EXPECT_NEAR(polar.Speed(60.0, 3.0, &status, false), 2.85, 1e-6);
  EXPECT_NEAR(polar.Speed(60.0, 0.0, &status, false), 0.0, 1e-6);
  EXPECT_NEAR(polar.Speed(20.0, 3.0, nullptr, false, true),
              0.5 * polar.Speed(20.0, 6.0, nullptr, false, true), 1e-6);
  EXPECT_TRUE(std::isnan(polar.Speed(60.0, 3.0, &status, true)));
  EXPECT_EQ(status, POLAR_SPEED_WIND_TOO_LIGHT);
}

TEST(PolarWindRangeTest, ClimatologyExamplesOfferOnlySailsWithinWindRange) {
  const char* names[] = {"Example-0-10.pol", "Example-6-24.pol",
                         "Example-15-30.pol", "Example-24-60.pol"};
  int usable = 0;
  double fastest = 0.0;
  for (const char* name : names) {
    Polar polar;
    wxString message;
    ASSERT_TRUE(polar.Open(wxString(WEATHER_ROUTING_SOURCE_DIR) +
                              "/data/polars/Example/" + name, message));
    const double speed = polar.Speed(60.0, 18.2, nullptr, true, true);
    if (!std::isfinite(speed)) continue;
    ++usable;
    fastest = std::max(fastest, speed);
  }
  EXPECT_EQ(usable, 2);
  EXPECT_NEAR(fastest, 7.912, 1e-6);
}

TEST(PolarWindRangeTest, IntermediateAndEndpointSpeedsRemainUnchanged) {
  Polar polar;
  wxString message;
  ASSERT_TRUE(polar.Open(
      wxString(WEATHER_ROUTING_SOURCE_DIR) +
          "/data/polars/Example/Example-15-30.pol", message));
  EXPECT_NEAR(polar.Speed(60.0, 15.0, nullptr, true), 7.4, 1e-6);
  EXPECT_NEAR(polar.Speed(60.0, 18.2, nullptr, true), 7.912, 1e-6);
  EXPECT_NEAR(polar.Speed(60.0, 30.0, nullptr, true), 8.7, 1e-6);
  EXPECT_NEAR(polar.Speed(60.0, 50.0, nullptr, false), 8.7, 1e-6);
}

TEST(PolarWindRangeTest, ExplicitZeroWindPoweredPolarIsPreserved) {
  Polar polar;
  wxString message;
  ASSERT_TRUE(polar.Open(wxString(TESTDATADIR) +
                            "/polars/ZeroWindPowered_test.pol", message));
  EXPECT_NEAR(polar.Speed(90.0, 0.0, nullptr, true), 5.5, 1e-6);
  EXPECT_NEAR(polar.Speed(90.0, 0.0, nullptr, false), 5.5, 1e-6);
}

TEST_F(PolarTest, SpeedAtApparentWindDirectionBasic) {
  double twa;
  double speed = m_polar.SpeedAtApparentWindDirection(10, 10, &twa);
  EXPECT_NEAR(speed, 1.473, 1e-3);
  EXPECT_NEAR(twa, 11.444, 1e-3);
}

TEST_F(PolarTest, SpeedAtApparentWindBasic) {
  double TWA;
  double speed = m_polar.SpeedAtApparentWind(90, 10, &TWA);
  EXPECT_NEAR(speed, 7.669, 1e-3);
  EXPECT_NEAR(TWA, 127.498, 1e-3);
}

TEST_F(PolarTest, GetVMGTrueWindBasic) {
  SailingVMG vmg = m_polar.GetVMGTrueWind(10);
  EXPECT_NEAR(vmg.values[SailingVMG::STARBOARD_UPWIND], 44.998, 1e-3);
  EXPECT_NEAR(vmg.values[SailingVMG::PORT_UPWIND], 315.002, 1e-3);
  EXPECT_NEAR(vmg.values[SailingVMG::STARBOARD_DOWNWIND], 152.498, 1e-3);
  EXPECT_NEAR(vmg.values[SailingVMG::PORT_DOWNWIND], 207.501, 1e-3);
}

TEST_F(PolarTest, GetVMGApparentWindBasic) {
  SailingVMG vmg = m_polar.GetVMGApparentWind(10);
  EXPECT_NEAR(vmg.values[SailingVMG::STARBOARD_UPWIND], 45.873, 1e-3);
  EXPECT_NEAR(vmg.values[SailingVMG::PORT_UPWIND], 314.127, 1e-3);
  EXPECT_NEAR(vmg.values[SailingVMG::STARBOARD_DOWNWIND], 169.359, 1e-3);
  EXPECT_NEAR(vmg.values[SailingVMG::PORT_DOWNWIND], 190.640, 1e-3);
}

TEST_F(PolarTest, TrueWindSpeedBasic) {
  double speed = m_polar.TrueWindSpeed(5, 90, 80);
  EXPECT_NEAR(speed, 4.428, 1e-3);
}

TEST_F(PolarTest, InterpolateSpeedsBasic) {
  bool success = m_polar.InterpolateSpeeds();
  EXPECT_EQ(success, false); // @todo: The call fails. Figure out why, and fix this test.
}

TEST_F(PolarTest, UpdateSpeedsBasic) {
  m_polar.UpdateSpeeds(); // @todo: The call succeeded, but did it do the right thing?  Test that.
}

TEST_F(PolarTest, UpdateDegreeStepLookupBasic) {
  m_polar.UpdateDegreeStepLookup(); // @todo: The call succeeded, but did it do the right thing?  Test that.
}

TEST_F(PolarTest, InsideCrossOverContourBasic) {
  bool isInside = m_polar.InsideCrossOverContour(10, 10, true);
  EXPECT_EQ(isInside, false); // @todo: I think we need to load more polars to test this properly.
}

TEST_F(PolarTest, GenerateBasic) {
  m_polar.Generate(std::list<PolarMeasurement>()); // @todo: The call succeeded, but did it do the right thing?  Test that.
}

TEST_F(PolarTest, AddDegreeStepBasic) {
  m_polar.AddDegreeStep(10); // @todo: The call succeeded, but did it do the right thing?  Test that.
}

TEST_F(PolarTest, RemoveDegreeStepBasic) {
  m_polar.RemoveDegreeStep(10); // @todo: The call succeeded, but did it do the right thing?  Test that.
}

TEST_F(PolarTest, OptimizeTackingSpeedBasic) {
  // m_polar.OptimizeTackingSpeed(); // @todo: This method is not defined yet.
}

TEST_F(PolarTest, AddWindSpeedBasic) {
  m_polar.AddWindSpeed(10); // @todo: The call succeeded, but did it do the right thing?  Test that.
}

TEST_F(PolarTest, RemoveWindSpeedBasic) {
  m_polar.RemoveWindSpeed(10); // @todo: The call succeeded, but did it do the right thing?  Test that.
}

TEST_F(PolarTest, VelocityApparentWindBasic) {
  double speed = Polar::VelocityApparentWind(10, 10, 10);
  EXPECT_NEAR(speed, 19.923, 1e-3);
}

TEST_F(PolarTest, VelocityTrueWindBasic) {
  // Dead upwind
  double speed = Polar::VelocityTrueWind(5, 10, 0);
  EXPECT_NEAR(speed, -5.000, 1e-3);
  // Beating
  speed = Polar::VelocityTrueWind(8, 5, 45);
  EXPECT_NEAR(speed, 3.640, 1e-3);
  // Beam reach
  speed = Polar::VelocityTrueWind(6, 5, 90);
  EXPECT_NEAR(speed, 3.316, 1e-3);
  // Broad reach
  speed = Polar::VelocityTrueWind(5, 5, 120);
  EXPECT_NEAR(speed, 5.000, 1e-3);
  // Dead downwind
  speed = Polar::VelocityTrueWind(-5, 10, 180);
  EXPECT_NEAR(speed, 15.000, 1e-3);
}
 
TEST_F(PolarTest, DirectionApparentWindBasic) {
  // Returns 0 if aws is 0 (apparent wind direction undefined)
  double direction = Polar::DirectionApparentWind(0, 10, 10, 10);
  EXPECT_NEAR(direction, 0.000, 1e-3);

  // Returns W if stw is 0 (no boat motion, apparent = true wind)
  direction = Polar::DirectionApparentWind(10, 0, 10, 10);
  EXPECT_NEAR(direction, 10.000, 1e-3);

  // Otherwise returns calculated angle accounting for both vectors
  direction = Polar::DirectionApparentWind(10, 5, 90, 10);
  EXPECT_NEAR(direction, 75.522, 1e-3);
}

TEST_F(PolarTest, DirectionApparentWind2Basic) {
  double direction = Polar::DirectionApparentWind(5, 90, 10);
  EXPECT_NEAR(direction, 63.434, 1e-3);
}
