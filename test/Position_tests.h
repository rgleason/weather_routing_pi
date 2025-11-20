#include <gtest/gtest.h>
#include <Position.h>
#include "RoutePoint_tests.h"


class PositionTest: public RoutePointTest {
 protected:
    // Default values for Position initialization
    double m_latitude = 10.0;
    double m_longitude = 20.0;
    double m_pheading = 179.0; // Parent heading
    double m_pbearing = 181.0; // Parent bearing
public:
    PositionTest() {
        // You can do set-up work for each test here.
    }

    virtual ~PositionTest() override {
        // You can do clean-up work that doesn't throw exceptions here.
    }

    virtual void SetUp() override {
        // Code here will be called immediately after the constructor (right
        // before each test).
        RoutePointTest::SetUp();
    }

    virtual void TearDown() override {
        // Code here will be called immediately after each test (right
        // before the destructor).
        RoutePointTest::TearDown();
    }

    // Position m_position;
    Position* m_pNext = nullptr;
    int m_polar_idx = -1, m_tack_count = 0, m_jibe_count = 0, 
        m_sail_plan_change_count = 0;
    DataMask m_data_mask = DataMask::NONE;
    bool m_data_deficient = false;
    Position m_position{m_latitude, m_longitude, m_pNext, 
                          m_pheading, m_pbearing, m_polar_idx, 
                          m_tack_count, m_jibe_count, 
                          m_sail_plan_change_count, m_data_mask, 
                          m_data_deficient};
};
