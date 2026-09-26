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
