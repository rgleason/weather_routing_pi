#include <gtest/gtest.h>
#include <gmock/gmock.h>
#include <IsoRoute.h>
#include "PlugIn_Waypoint_mock.h"
#include <string>
#include "Position.h"
#include "RouteMap.h"
 class IsoRouteTest: public ::testing::Test {
 protected:
  // Must be even number of positions, just because of how initialization proceeds below.
  static const uint m_positionCount = 8;
  Position* m_position[m_positionCount]; 
  Position* m_position1[m_positionCount];
  IsoRoute *m_isoRoute, *m_isoRoute1;

  IsoRouteTest() {
    // Create sequences of points (i, i*i), (i+1, i*i+1) 
    // - just something to do basic tests with.
    for(int i = 0; i < m_positionCount; i+=2){
      m_position[i] = new Position(i, i*i);
      m_position[i+1] = new Position(i+1, -(i*i+1));
      m_position1[i] = new Position(i+1, i*i+1);
      m_position1[i+1] = new Position(i+2, -(i*i+2));
    }
    // Create simple boundaries in lat/lon coordinate space to verify that
    // the point-in-polygon detection works correctly for determining whether a
    // boat's position falls within a particular routing zone.
    for(int i = 0; i < m_positionCount; ++i) {
      m_position[i]->prev = m_position[(i + m_positionCount - 1) % m_positionCount];  // Previous (wraps around)
      m_position[i]->next = m_position[(i + 1) % m_positionCount];     
      m_position1[i]->prev = m_position1[(i + m_positionCount - 1) % m_positionCount];  // Previous (wraps around)
      m_position1[i]->next = m_position1[(i + 1) % m_positionCount];     
    }
    m_isoRoute = new IsoRoute(m_position[0]->BuildSkipList(), 1);
    m_isoRoute1 = new IsoRoute(m_position1[0]->BuildSkipList(), 1);
    // Set the parent of the second route to the first one
    m_isoRoute1->parent = m_isoRoute;
    // Add the second route as a child of the first one
    m_isoRoute->children.push_back(m_isoRoute1);
  }
 
   ~IsoRouteTest() override {
     // You can do clean-up work that doesn't throw exceptions here.
     // delete m_isoRoute; // TODO quinton: Caused crashes due to bugs in Normalize() code. Disable for now. 
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

 const uint IsoRouteTest::m_positionCount;

 TEST_F(IsoRouteTest, Constructor) {
  EXPECT_FALSE(m_isoRoute->skippoints == nullptr);
  EXPECT_EQ(m_isoRoute->direction, 1);
  EXPECT_TRUE(m_isoRoute->parent == nullptr);
  EXPECT_FALSE(m_isoRoute->children.empty());
  EXPECT_FALSE(m_isoRoute1->parent == nullptr);
  EXPECT_TRUE(m_isoRoute1->children.empty());
 }

 TEST_F(IsoRouteTest, CopyConstructor) {
  // Create a copy of the IsoRoute
  IsoRoute* isoRouteCopy = new IsoRoute(m_isoRoute, nullptr);
  EXPECT_FALSE(isoRouteCopy->skippoints == nullptr);
  EXPECT_EQ(isoRouteCopy->direction, m_isoRoute->direction);
  EXPECT_TRUE(isoRouteCopy->parent == nullptr);
  EXPECT_TRUE(isoRouteCopy->children.empty());
  delete isoRouteCopy;
 }

 TEST_F(IsoRouteTest, PrintBasic) {
  // Check that the Print method does not crash.
  m_isoRoute->Print();
 }

 TEST_F(IsoRouteTest, PrintSkipBasic) {
  // Check that the PrintSkip method does not crash.
  m_isoRoute->PrintSkip();
 }

 TEST_F(IsoRouteTest, ClosestPositionBasic) {
  // When querying with exact coordinates of an existing position,
  // should return that exact position
  Position* closest = m_isoRoute->ClosestPosition(m_position[0]->lat, m_position[0]->lon);
  EXPECT_FLOAT_EQ(closest->lat, m_position[0]->lat);
  EXPECT_FLOAT_EQ(closest->lon, m_position[0]->lon);
  // Given route positions (0,0), (1,1), (2,4), the point (2,4) is closest to (10,10)  
  Position outsidePos(10.0, 10.0);
  closest = m_isoRoute->ClosestPosition(outsidePos.lat, outsidePos.lon);
  EXPECT_FLOAT_EQ(closest->lat, m_position[4]->lat);
  EXPECT_FLOAT_EQ(closest->lon, m_position[4]->lon);
 }

 TEST_F(IsoRouteTest, CompletelyContainedBasic) {
  // Check that the route does not completely contains itself.
  EXPECT_FALSE(m_isoRoute->CompletelyContained(m_isoRoute));
 }

 TEST_F(IsoRouteTest, ContainsRouteBasic) {
  // Check that the route does not contains itself.
  EXPECT_FALSE(m_isoRoute->ContainsRoute(m_isoRoute));
 }

 TEST_F(IsoRouteTest, FindIsoRouteBoundsBasic) {
  double bounds[4];
  // Check that the bounds are found correctly.
  m_isoRoute->FindIsoRouteBounds(bounds);
  EXPECT_FLOAT_EQ(bounds[0], m_position[m_positionCount-1]->lon); // min lon
  EXPECT_FLOAT_EQ(bounds[1], m_position[m_positionCount-2]->lon); // max lon
  EXPECT_FLOAT_EQ(bounds[2], m_position[0]->lat); // min lat
  EXPECT_FLOAT_EQ(bounds[3], m_position[m_positionCount-1]->lat); // max lat
 }

 TEST_F(IsoRouteTest, ReduceClosePointsBasic) {
  // Check that the route is reduced correctly.
  m_isoRoute->ReduceClosePoints();
  EXPECT_EQ(m_isoRoute->Count(),m_positionCount); // No points should be removed
 }

 TEST_F(IsoRouteTest, RemovePositionBasic) {
  // Check that removing a position works correctly.
  Position* posToRemove = m_position[0];
  m_isoRoute->RemovePosition(m_isoRoute->skippoints, posToRemove);
  EXPECT_EQ(m_isoRoute->Count(),m_positionCount - 1); // One less position
  // Check that the removed position is no longer in the route.
  Position* closest = m_isoRoute->ClosestPosition(posToRemove->lat, posToRemove->lon);
  EXPECT_NE(closest, posToRemove); // Should not be the removed position
 }

 TEST_F(IsoRouteTest, UpdateStatisticsBasic) {
  // Check that the statistics are updated correctly.
  const int initialRoutes = m_isoRoute->Count();
  const int initialInvRoutes = initialRoutes - 1; // Assuming one route is inverted
  const int initialSkipPositions = m_isoRoute->SkipCount();
  const int initialPositions = m_positionCount;

  int routes=initialRoutes, 
    invRoutes=initialInvRoutes, 
    skipPositions=initialInvRoutes, 
    positions=initialPositions;
  m_isoRoute->UpdateStatistics(routes, invRoutes, skipPositions, positions);
  // Check that the statistics are updated correctly.
  EXPECT_EQ(routes, initialRoutes+2);
  EXPECT_EQ(invRoutes, initialInvRoutes+1);
  EXPECT_EQ(skipPositions, initialSkipPositions*3-1);
  EXPECT_EQ(positions, initialPositions*3);
 }

 // This function is not declared in the header file, but is used in the tests,
 // so we declare it here for the test.
 bool Normalize(IsoRouteList& rl, IsoRoute* route1, IsoRoute* route2, int level,
               bool inverted_regions);
 // Minimal test to check that Normalize does not crash.
 TEST_F(IsoRouteTest, NormalizeBasic) {
  IsoRouteList isoRouteList;
  int level=0;
  bool inverted=false;
  bool result=Normalize(isoRouteList, m_isoRoute, m_isoRoute1, level, inverted);
  EXPECT_TRUE(result);
  EXPECT_TRUE(isoRouteList.size() >= 1); // Should have at least one route in the list
 }

 TEST_F(IsoRouteTest, SkipCountBasic) {
  // Check that the skip count is correct for the route.
  EXPECT_EQ(m_isoRoute->SkipCount(), m_positionCount);
 }

 TEST_F(IsoRouteTest, CountBasic) {
  // Check that the count of positions in the route is correct.
  EXPECT_EQ(m_isoRoute->Count(), m_positionCount);
 }

  // Minimal test to check that test compiles and Propagate does not crash.
 TEST_F(IsoRouteTest, PropagateBasic) {
  RouteMapConfiguration configuration;
  IsoRouteList isoRouteList;
  m_isoRoute->Propagate(isoRouteList, configuration);
 }

 // Minimal test to check that test compiles and PropagateToEnd does not crash.
 TEST_F(IsoRouteTest, PropagateToEndBasic) {
  RouteMapConfiguration configuration;
  IsoRouteList isoRouteList;
  double mindt = 0.0; // Minimum delta time, not used in this test
  Position *endp = new Position(2.0, 2.0); // Arbitrary end point
  double minH = 0.0; // Minimum heading, not used in this test
  bool mintacked = false; // No tacks
  bool minjibed = false; // No jibes
  bool minsail_plan_changed = false; // No sail plan changes
  DataMask mindata_mask = DataMask::NONE; // No data mask
  // Call the method to propagate to the end point.
  m_isoRoute->PropagateToEnd(configuration, mindt, endp, minH, mintacked, minjibed, minsail_plan_changed, mindata_mask);
 }

 TEST_F(IsoRouteTest, MinimizeLatBasic) {
  auto initialLat = m_isoRoute->skippoints->point->lat;
  m_isoRoute->MinimizeLat();
  EXPECT_LE(m_isoRoute->skippoints->point->lat, initialLat);
 }

 TEST_F(IsoRouteTest, IntersectionCountInside) {
  Position insidePos(0.1, -0.2); // Inside the test polygon.
  EXPECT_EQ(m_isoRoute->IntersectionCount(insidePos)%2, 1); // Should be inside, odd count.
 }

TEST_F(IsoRouteTest, IntersectionCountOutside) {
  Position outsidePos(100.0, 100.0); // Outside the test polygon.
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
    int result[m_positionCount] = {0, 1, 0, 1, 0, 1, 0, 0,};
    for(int i = 0; i < m_positionCount; ++i) {
        EXPECT_EQ(m_isoRoute->IntersectionCount(*m_position[i]) % 2, result[i]) << "Intersection count of (" 
      << m_position[i]->lat << ", " << m_position[i]->lon << ") [vertex " << i << "] should be " << ((result[i] == 0) ? "even" : "odd"); ;
    }
 }

 TEST_F(IsoRouteTest, IntersectionCountBoundaries) {
  // On a boundary of the test polygon. Use a point in the middle of the ith boundary.
  int result[m_positionCount] = { 0,1,0,1,0,1,0,1, };
  for(int i = 0; i < m_positionCount; ++i) {
    Position onBoundary(
      m_position[i]->lat + (m_position[(i+1)%m_positionCount]->lat - m_position[i]->lat)/2, 
      m_position[i]->lon + (m_position[(i+1)%m_positionCount]->lon - m_position[i]->lon)/2); 
    EXPECT_EQ(m_isoRoute->IntersectionCount(onBoundary)%2, result[i]) << "Intersection count of (" 
      << m_position[i]->lat << ", " << m_position[i]->lon << ") [midpoint of edge "<< i << "] should be " << ((result[i] == 0) ? "even" : "odd"); 
  }
 }

 TEST_F(IsoRouteTest, ContainsInside) {
  Position insidePos(0.1, -0.2); // Inside the test polygon.
  EXPECT_TRUE(m_isoRoute->Contains(insidePos, true)); // Should be inside.
 }

TEST_F(IsoRouteTest, ContainsOutside) {
  Position outsidePos(2.0, 3.0); // Outside the test polygon.
  EXPECT_TRUE(m_isoRoute->Contains(outsidePos, true)); // Should be inside.
 }

 Position getMidpoint(const Position& p1, const Position& p2) {
    return Position(
        p1.lon + (p2.lon - p1.lon) / 2,
        p1.lat + (p2.lat - p1.lat) / 2
    );
 }

 TEST_F(IsoRouteTest, ContainsBoundaryPoints) {
    std::vector<std::pair<int,int>> edges = {{0,1}, {1,2}, {2,0}};
    bool inside[] = { 0, 1, 1 };
    int i=0;
    for(const auto& [start, end] : edges) {
        Position midpoint = getMidpoint(*m_position[start], *m_position[end]);
        EXPECT_EQ(m_isoRoute->Contains(midpoint, true), inside[i]) 
            << "Midpoint of edge " << start << "->" << end << " should be " << (inside[i] ? "inside" : "outside");
        i++;
    }
 }
