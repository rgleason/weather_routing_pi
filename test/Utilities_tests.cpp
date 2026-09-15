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
 #include <Utilities.h>

  TEST(UtilitiesTests, deg2radBasic) {
    EXPECT_DOUBLE_EQ(deg2rad(180), M_PI);
    EXPECT_DOUBLE_EQ(deg2rad(90), M_PI/2);
    EXPECT_DOUBLE_EQ(deg2rad(0), 0);
    EXPECT_DOUBLE_EQ(deg2rad(360), 2*M_PI);
  }

  TEST(UtilitiesTests, rad2degBasic) {
    EXPECT_DOUBLE_EQ(rad2deg(M_PI), 180);
    EXPECT_DOUBLE_EQ(rad2deg(M_PI/2), 90);
    EXPECT_DOUBLE_EQ(rad2deg(0), 0);
    EXPECT_DOUBLE_EQ(rad2deg(2*M_PI), 360);
  }

  TEST(UtilitiesTests, heading_resolveBasic) {
    EXPECT_DOUBLE_EQ(heading_resolve(180), -180);
    EXPECT_DOUBLE_EQ(heading_resolve(-180), -180);
    EXPECT_DOUBLE_EQ(heading_resolve(0), 0);
    EXPECT_DOUBLE_EQ(heading_resolve(360), 0);
    EXPECT_DOUBLE_EQ(heading_resolve(-360), 0);
    EXPECT_DOUBLE_EQ(heading_resolve(540), -180);
    EXPECT_DOUBLE_EQ(heading_resolve(-540), -180);
  }

  TEST(UtilitiesTests, positive_degreesBasic) {
    EXPECT_DOUBLE_EQ(positive_degrees(180), 180);
    EXPECT_DOUBLE_EQ(positive_degrees(-180), 180);
    EXPECT_DOUBLE_EQ(positive_degrees(0), 0);
    EXPECT_DOUBLE_EQ(positive_degrees(360), 0);
    EXPECT_DOUBLE_EQ(positive_degrees(-360), 0);
    EXPECT_DOUBLE_EQ(positive_degrees(540), 180);
    EXPECT_DOUBLE_EQ(positive_degrees(-540), 180);
  }

  TEST(UtilitiesTests, rad2posdegBasic) {
    EXPECT_DOUBLE_EQ(rad2posdeg(M_PI), 180);
    EXPECT_DOUBLE_EQ(rad2posdeg(M_PI/2), 90);
    EXPECT_DOUBLE_EQ(rad2posdeg(0), 0);
    EXPECT_DOUBLE_EQ(rad2posdeg(2*M_PI), 0);
  }

  TEST(UtilitiesTests, squareBasic) {
    EXPECT_DOUBLE_EQ(square(0), 0);
    EXPECT_DOUBLE_EQ(square(1), 1);
    EXPECT_DOUBLE_EQ(square(-1), 1);
    EXPECT_DOUBLE_EQ(square(2), 4);
    EXPECT_DOUBLE_EQ(square(-2), 4);
  }

  TEST(UtilitiesTests, cubeBasic) {
    EXPECT_DOUBLE_EQ(cube(0), 0);
    EXPECT_DOUBLE_EQ(cube(1), 1);
    EXPECT_DOUBLE_EQ(cube(-1), -1);
    EXPECT_DOUBLE_EQ(cube(2), 8);
    EXPECT_DOUBLE_EQ(cube(-2), -8);
  }

  TEST(UtilitiesTests, average_longitudeBasic) {
    EXPECT_DOUBLE_EQ(average_longitude(0, 0), 0);
    EXPECT_DOUBLE_EQ(average_longitude(0, 180), 90);
    EXPECT_DOUBLE_EQ(average_longitude(180, 0), 90);
    EXPECT_NEAR(0, average_longitude(0, 360), 8e-15); // The algorithm is nor perfect
    EXPECT_NEAR(0, average_longitude(360, 0), 8e-15); // The algorithm is nor perfect
    EXPECT_DOUBLE_EQ(average_longitude(180, 180), 180);
    EXPECT_DOUBLE_EQ(average_longitude(180, 360), -90);
    EXPECT_DOUBLE_EQ(average_longitude(360, 180), -90);
  }

  #include <tinyxml.h>
  TEST(UtilitiesTests, AttributeDoubleBasic) {
    TiXmlElement e("test");
    e.SetAttribute("test", "1.0");
    EXPECT_DOUBLE_EQ(AttributeDouble(&e, "test", 0), 1.0);
    e.SetAttribute("test", "2.0");
    EXPECT_DOUBLE_EQ(AttributeDouble(&e, "test", 0), 2.0);
    e.SetAttribute("test", "3.0");
    EXPECT_DOUBLE_EQ(AttributeDouble(&e, "test", 0), 3.0);
    e.SetAttribute("test", "1.0");
    EXPECT_DOUBLE_EQ(AttributeDouble(&e, "test", 0), 1.0);
  }

  TEST(UtilitiesTests, AttributeIntBasic) {
    TiXmlElement e("test");
    e.SetAttribute("test", "1");
    EXPECT_EQ(AttributeInt(&e, "test", 0), 1);
    e.SetAttribute("test", "2");
    EXPECT_EQ(AttributeInt(&e, "test", 0), 2);
    e.SetAttribute("test", "3");
    EXPECT_EQ(AttributeInt(&e, "test", 0), 3);
    e.SetAttribute("test", "1");
    EXPECT_EQ(AttributeInt(&e, "test", 0), 1);
  }

  TEST(UtilitiesTests, AttributeBoolBasic) {
    TiXmlElement e("test");
    e.SetAttribute("test", true);
    EXPECT_EQ(AttributeBool(&e, "test", false), true);
    e.SetAttribute("test", false);
    EXPECT_EQ(AttributeBool(&e, "test", true), false);
    e.SetAttribute("test", true);
    EXPECT_EQ(AttributeBool(&e, "test", false), true);
  }

#ifdef __MSVC__
  TEST(UtilitiesTests, truncBasic) {
    EXPECT_DOUBLE_EQ(trunc(0.0), 0.0);
    EXPECT_DOUBLE_EQ(trunc(1.0), 1.0);
    EXPECT_DOUBLE_EQ(trunc(-1.0), -1.0);
    EXPECT_DOUBLE_EQ(trunc(1.1), 1.0);
    EXPECT_DOUBLE_EQ(trunc(-1.1), -1.0);
    EXPECT_DOUBLE_EQ(trunc(1.9), 1.0);
    EXPECT_DOUBLE_EQ(trunc(-1.9), -1.0);
  }

  TEST(UtilitiesTests, roundBasic) {
    EXPECT_DOUBLE_EQ(round(0.0), 0.0);
    EXPECT_DOUBLE_EQ(round(1.0), 1.0);
    EXPECT_DOUBLE_EQ(round(-1.0), -1.0);
    EXPECT_DOUBLE_EQ(round(1.1), 1.0);
    EXPECT_DOUBLE_EQ(round(-1.1), -1.0);
    EXPECT_DOUBLE_EQ(round(1.9), 2.0);
    EXPECT_DOUBLE_EQ(round(-1.9), -2.0);
  }
#endif

TEST(UtilitiesTests, ft2mBasic) {
    EXPECT_DOUBLE_EQ(ft2m(0), 0);
    EXPECT_DOUBLE_EQ(ft2m(1), 0.3048);
    EXPECT_DOUBLE_EQ(ft2m(-1), -0.3048);
    EXPECT_DOUBLE_EQ(ft2m(1.1), 0.33528);
    EXPECT_DOUBLE_EQ(ft2m(-1.1), -0.33528);
    EXPECT_DOUBLE_EQ(ft2m(1.9), 0.57912);
    EXPECT_DOUBLE_EQ(ft2m(-1.9), -0.57912);
}

TEST(UtilitiesTests, m2ftBasic) {
    EXPECT_NEAR(m2ft(0.0), 0.0, 0.0001);
    EXPECT_NEAR(m2ft(0.3048), 1.0, 0.0001);
    EXPECT_NEAR(m2ft(-0.3048), -1.0, 0.0001);
    EXPECT_NEAR(m2ft(0.33528), 1.1, 0.0001);
    EXPECT_NEAR(m2ft(-0.33528), -1.1, 0.0001);
    EXPECT_NEAR(m2ft(0.57912), 1.9, 0.0001);
    EXPECT_NEAR(m2ft(-0.57912), -1.9, 0.0001);
}

TEST(UtilitiesTests, m_s2knotsBasic) {
    EXPECT_NEAR(m_s2knots(0.0), 0.0, 0.01);
    EXPECT_NEAR(m_s2knots(0.514444), 1.0, 0.01);
    EXPECT_NEAR(m_s2knots(-0.514444), -1.0, 0.01);
    EXPECT_NEAR(m_s2knots(0.564976), 1.1, 0.01);
    EXPECT_NEAR(m_s2knots(-0.564976), -1.1, 0.01);
    EXPECT_NEAR(m_s2knots(1.852), 3.5984, 0.01);
    EXPECT_NEAR(m_s2knots(-1.852), -3.5984, 0.01);
}

TEST(UtilitiesTests, knots2m_sBasic) {
    EXPECT_NEAR(knots2m_s(0.0), 0.0, 0.01);
    EXPECT_NEAR(knots2m_s(1.0), 0.514444, 0.01);
    EXPECT_NEAR(knots2m_s(-1.0), -0.514444, 0.01);
    EXPECT_NEAR(knots2m_s(1.1), 0.564976, 0.01);
    EXPECT_NEAR(knots2m_s(-1.1), -0.564976, 0.01);
    EXPECT_NEAR(knots2m_s(3.5984), 1.852, 0.01);
    EXPECT_NEAR(knots2m_s(-3.5984), -1.852, 0.01);
}

TEST(UtilitiesTests, CalculateTimeDeltaBasic) {
     wxDateTime dt1 = wxDateTime::Now();
     wxDateTime dt2 = dt1;
     dt2.Add(wxTimeSpan(0, 0, 0, 1));
     EXPECT_EQ(calculateTimeDelta(dt1, dt2).ToStdString(), "00 00");
     dt2.Add(wxTimeSpan(0, 0, 1, 0));
     EXPECT_EQ(calculateTimeDelta(dt1, dt2).ToStdString(), "00 01");
     dt2.Add(wxTimeSpan(0, 1, 0, 0));
     EXPECT_EQ(calculateTimeDelta(dt1, dt2).ToStdString(), "01 01");
     dt2.Add(wxTimeSpan(1, 0, 0, 0));
     EXPECT_EQ(calculateTimeDelta(dt1, dt2).ToStdString(), "01:01");
}
#include "RouteExportFileName.h"

TEST(RouteExportFileName, KeepsPositionNamesOutOfFilesystemSyntax) {
  const wxString label = "WXRoute_Provincetown [3h / 10deg]_Lizard:Point\\End?";
  EXPECT_EQ(weather_routing::RouteExportFileStem(label),
            "WXRoute_Provincetown [3h _ 10deg]_Lizard_Point_End_");
  EXPECT_TRUE(label.Contains("3h / 10deg"));
  EXPECT_EQ(weather_routing::RouteExportFileStem("WXRoute_A<>B|C*D\"E\n"),
            "WXRoute_A__B_C_D_E_");
  EXPECT_EQ(weather_routing::RouteExportFileStem("WXRoute_End. "), "WXRoute_End");
  const wxString unicode_name = wxString::FromUTF8("WXRoute_Vava‘u_Lizard Point");
  EXPECT_EQ(weather_routing::RouteExportFileStem(unicode_name), unicode_name);
}
