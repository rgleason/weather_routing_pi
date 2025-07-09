#include <gtest/gtest.h>
#include <RoutePoint.h>

class RoutePointTest: public ::testing::Test {
public:
    RoutePointTest() {
        // You can do set-up work for each test here.
    }

    virtual ~RoutePointTest() override {
        // You can do clean-up work that doesn't throw exceptions here.
    }

    virtual void SetUp() override {
        // Code here will be called immediately after the constructor (right
        // before each test).
    }

    virtual void TearDown() override {
        // Code here will be called immediately after each test (right
        // before the destructor).
    }

    // Position m_position;
    double m_latitude=10.0, m_longitude=20.0;
    int m_polar_idx = -1, m_tack_count = 0, m_jibe_count = 0, 
        m_sail_plan_change_count = 0;
    DataMask m_data_mask = DataMask::NONE;
    bool m_data_deficient = false;
    RoutePoint m_routePoint{m_latitude, m_longitude, 
                          m_polar_idx, 
                          m_tack_count, m_jibe_count, 
                          m_sail_plan_change_count, m_data_mask, 
                          m_data_deficient};
};

