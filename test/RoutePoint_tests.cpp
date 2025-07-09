#include "RoutePoint_tests.h"
#include <json/json.h>

TEST_F(RoutePointTest, ConstructorBasic) {
    // Check that the position is initialized correctly
    EXPECT_DOUBLE_EQ(m_routePoint.lat, m_latitude);
    EXPECT_DOUBLE_EQ(m_routePoint.lon, m_longitude);
    EXPECT_EQ(m_routePoint.polar, m_polar_idx);
    EXPECT_EQ(m_routePoint.tacks, m_tack_count);
    EXPECT_EQ(m_routePoint.jibes, m_jibe_count);
    EXPECT_EQ(m_routePoint.sail_plan_changes, m_sail_plan_change_count);
    EXPECT_EQ(m_routePoint.data_mask, m_data_mask);
    EXPECT_EQ(m_routePoint.grib_is_data_deficient, m_data_deficient);
}

TEST_F(RoutePointTest, toJsonBasic) {
    // Check that the toJson method works correctly
    Json::Value json;
    m_routePoint.toJson(json);
    EXPECT_DOUBLE_EQ(json["lat"].asDouble(), m_latitude);
    EXPECT_DOUBLE_EQ(json["lon"].asDouble(), m_longitude);
    EXPECT_EQ(json["polar"].asInt(), m_polar_idx);
    EXPECT_EQ(json["tacks"].asInt(), m_tack_count);
    EXPECT_EQ(json["jibes"].asInt(), m_jibe_count);
    EXPECT_EQ(json["sail_plan_changes"].asInt(), m_sail_plan_change_count);
    EXPECT_EQ(json["data_mask"].asUInt(), static_cast<unsigned int>(m_data_mask));
    EXPECT_EQ(json["grib_is_data_deficient"].asBool(), m_data_deficient);
}

TEST_F(RoutePointTest, fromJsonBasic) {
    // Check that the toJson method works correctly
    Json::Value json;
    m_routePoint.toJson(json);

    RoutePoint routePoint(json);
    EXPECT_DOUBLE_EQ(m_routePoint.lat, routePoint.lat);
    EXPECT_DOUBLE_EQ(m_routePoint.lon, routePoint.lon);
    EXPECT_EQ(m_routePoint.polar, routePoint.polar);
    EXPECT_EQ(m_routePoint.tacks, routePoint.tacks);
    EXPECT_EQ(m_routePoint.jibes, routePoint.jibes);
    EXPECT_EQ(m_routePoint.sail_plan_changes, routePoint.sail_plan_changes);
    EXPECT_EQ(m_routePoint.data_mask, routePoint.data_mask);
    EXPECT_EQ(m_routePoint.grib_is_data_deficient, routePoint.grib_is_data_deficient);
}
