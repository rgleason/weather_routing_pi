#include "Position_tests.h"

TEST_F(PositionTest, ConstructorBasic) {
    // Check that the position is initialized correctly
    EXPECT_DOUBLE_EQ(m_position.lat, m_latitude);
    EXPECT_DOUBLE_EQ(m_position.lon, m_longitude);
    EXPECT_DOUBLE_EQ(m_position.parent_heading, m_pheading);
    EXPECT_DOUBLE_EQ(m_position.parent_bearing, m_pbearing);
    EXPECT_EQ(m_position.polar, m_polar_idx);
    EXPECT_EQ(m_position.tacks, m_tack_count);
    EXPECT_EQ(m_position.jibes, m_jibe_count);
    EXPECT_EQ(m_position.sail_plan_changes, m_sail_plan_change_count);
    EXPECT_EQ(m_position.data_mask, m_data_mask);
    EXPECT_EQ(m_position.grib_is_data_deficient, m_data_deficient);
}

TEST_F(PositionTest, toJsonBasic) {
    // Check that the toJson method works correctly
    Json::Value json;
    m_position.toJson(json);
    EXPECT_DOUBLE_EQ(json["parent_heading"].asDouble(), m_pheading);
    EXPECT_DOUBLE_EQ(json["parent_bearing"].asDouble(), m_pbearing);
}

TEST_F(PositionTest, fromJsonBasic) {
    // Check that the toJson method works correctly
    Json::Value json;
    m_position.toJson(json);

    Position position(json);
    EXPECT_DOUBLE_EQ(m_position.lat, position.lat);
    EXPECT_DOUBLE_EQ(m_position.lon, position.lon);
    EXPECT_DOUBLE_EQ(m_position.parent_heading, position.parent_heading);
    EXPECT_DOUBLE_EQ(m_position.parent_bearing, position.parent_bearing);
    EXPECT_EQ(m_position.polar, position.polar);
    EXPECT_EQ(m_position.tacks, position.tacks);
    EXPECT_EQ(m_position.jibes, position.jibes);
    EXPECT_EQ(m_position.sail_plan_changes, position.sail_plan_changes);
    EXPECT_EQ(m_position.data_mask, position.data_mask);
    EXPECT_EQ(m_position.grib_is_data_deficient, position.grib_is_data_deficient);
}
