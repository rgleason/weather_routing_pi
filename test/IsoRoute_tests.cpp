#include <gtest/gtest.h>
#include <gmock/gmock.h>
#include <IsoRoute.h>
#include "PlugIn_Waypoint_mock.h"
#include <string>
#include "Position.h"


 class IsoRouteTest: public ::testing::Test {
 protected:
  Position* m_position[3];                        
  IsoRoute *m_isoRoute;

    IsoRouteTest() {
      // You can do set-up work for each test here.
      const int size = sizeof(m_position)/sizeof(Position*);
      // Create a sequence of points (i, i*i) - just something to do basic tests with.
      for(int i = 0; i < size; ++i){
        m_position[i] = new Position(i, i*i);
      }
      // Create a simple triangular boundary in lat/lon coordinate space to verify that
      // the point-in-polygon detection works correctly for determining whether a
      // boat's position falls within a particular routing zone.
      for(int i = 0; i < size; ++i) {
        m_position[i]->prev = m_position[(i + size - 1) % size];  // Previous (wraps around)
        m_position[i]->next = m_position[(i + 1) % size];     
      }
      m_isoRoute = new IsoRoute(m_position[0]->BuildSkipList(), 1);
    }
 
   ~IsoRouteTest() override {
     // You can do clean-up work that doesn't throw exceptions here.
     delete m_isoRoute;
   }
 
   void SetUp() override {
     // Code here will be called immediately after the constructor (right
     // before each test).
   }
 
   void TearDown() override {
     // Code here will be called immediately after each test (right
     // before the destructor).
   }
 };

 TEST_F(IsoRouteTest, Constructor) {
  EXPECT_FALSE(m_isoRoute->skippoints == nullptr);
  EXPECT_EQ(m_isoRoute->direction, 1);
  EXPECT_TRUE(m_isoRoute->parent == nullptr);
  EXPECT_TRUE(m_isoRoute->children.empty());
 }

// Find and set the polygon's starting point to the position with the minimum latitude.
 TEST_F(IsoRouteTest, MinimizeLatBasic) {
  auto initialLat = m_isoRoute->skippoints->point->lat;
  m_isoRoute->MinimizeLat();
  EXPECT_LE(m_isoRoute->skippoints->point->lat, initialLat);
 }

 TEST_F(IsoRouteTest, IntersectionCountInside) {
  Position insidePos(1.0, 1.9); // Inside the test polygon.
  EXPECT_EQ(m_isoRoute->IntersectionCount(insidePos), 1); // Should be inside, odd count.
 }

TEST_F(IsoRouteTest, IntersectionCountOutside) {
  Position outsidePos(2.0, 3.0); // Outside the test polygon.
  EXPECT_EQ(m_isoRoute->IntersectionCount(outsidePos)%2, 0); // Should be outside, even count.
 }

 // Note some complexities for edges and vertices in 
 // https://en.wikipedia.org/wiki/Point_in_polygon#Limited_precision
 // For example, to prevent a point being considered inside both of 
 // two adjacent polygons (or indeed inside neither of them), some rules 
 // are used to assign points to one or the other polygon, but not both 
 // or neither.  The tests below are empirical base on the current behavior 
 // of the ray casting algorithm used, and  may need to be adjusted if the algorithm
 // changes in the future.
 TEST_F(IsoRouteTest, IntersectionCountVertices) {
    for(int i = 0; i < 3; ++i) {
        EXPECT_EQ(m_isoRoute->IntersectionCount(*m_position[i]) % 2, 0);
    }
 }

 TEST_F(IsoRouteTest, IntersectionCountBoundaries) {
  // On a boundary of the test polygon. Use a point in the middle of the ith boundary.
  const int size = 3;
  for(int i = 0; i < size; ++i) {
    Position onBoundary(
      m_position[i]->lon + (m_position[(i+1)%size]->lon - m_position[i]->lon)/2, 
      m_position[i]->lat + (m_position[(i+1)%size]->lat - m_position[i]->lat)/2); 
    EXPECT_EQ(m_isoRoute->IntersectionCount(onBoundary)%2, 0); // Should be outside, even count.
  }
 }

 TEST_F(IsoRouteTest, ContainsInside) {
  Position insidePos(1.0, 1.9); // Inside the test polygon.
  EXPECT_TRUE(m_isoRoute->Contains(insidePos, true)); // Should be inside.
 }

TEST_F(IsoRouteTest, ContainsOutside) {
  Position outsidePos(2.0, 3.0); // Outside the test polygon.
  EXPECT_FALSE(m_isoRoute->Contains(outsidePos, true)); // Should be outside.
 }

 Position getMidpoint(const Position& p1, const Position& p2) {
    return Position(
        p1.lon + (p2.lon - p1.lon) / 2,
        p1.lat + (p2.lat - p1.lat) / 2
    );
 }

 TEST_F(IsoRouteTest, ContainsBoundaryPoints) {
    std::vector<std::pair<int,int>> edges = {{0,1}, {1,2}, {2,0}};
    
    for(const auto& [start, end] : edges) {
        Position midpoint = getMidpoint(*m_position[start], *m_position[end]);
        EXPECT_FALSE(m_isoRoute->Contains(midpoint, true)) 
            << "Midpoint of edge " << start << "->" << end << " should be outside";
    }
 }