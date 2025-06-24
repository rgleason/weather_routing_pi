#include <gtest/gtest.h>
#include <gmock/gmock.h>
#include <IsoRoute.h>
#include "PlugIn_Waypoint_mock.h"
#include <string>
#include "Position.h"


 class IsoRouteTest: public ::testing::Test {
 protected:
  //  std::string
  //    m_testDataDir = TESTDATADIR,
  //    m_testBoatFileRelativePath = "polars/Hallberg-Rassy_40_test.pol",
  //    m_testBoatFileName = m_testDataDir + "/" + m_testBoatFileRelativePath,
  //    m_testFileOpenMessage = "";

  Position* m_position[3];                        
  IsoRoute *m_isoRoute;

    IsoRouteTest() {
       // You can do set-up work for each test here.
       int i = 0;
       // Create a sequence of points (i, i*i) - just something to do basic tests with.
       for(i = 0; i < sizeof(m_position)/sizeof(Position*); ++i){
         m_position[i] = new Position(i, i*i);
       }
       // Set up the circular linked list
       i = i-1;
       m_position[i]->prev = m_position[i-1];
       m_position[i]->next = m_position[0];
       m_position[0]->prev = m_position[i];
       m_position[0]->next = m_position[1];
       for(int i = 1; i < sizeof(m_position)/sizeof(Position*) -1; ++i) {
         m_position[i]->prev = m_position[i-1];
         m_position[i]->next = m_position[i+1];
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
 TEST_F(IsoRouteTest, IntersectionCountVertex0) {
  // At a vertex of the test polygon.
  EXPECT_EQ(m_isoRoute->IntersectionCount(*m_position[0])%2, 0); // Should be outside, even count.
 }

 TEST_F(IsoRouteTest, IntersectionCountVertex1) {
  // At a vertex of of the test polygon.
  EXPECT_EQ(m_isoRoute->IntersectionCount(*m_position[1])%2, 0); // Should be outside, even count.
 }

 TEST_F(IsoRouteTest, IntersectionCountVertex2) {
  // At a vertex of of the test polygon.
  EXPECT_EQ(m_isoRoute->IntersectionCount(*m_position[2])%2, 0); // Should be outside, even count.
 }

 TEST_F(IsoRouteTest, IntersectionCountBoundary0) {
  // On a boundary of the test polygon. Use a point in the middle of the first boundary.
  Position onBoundary(
    m_position[0]->lon + (m_position[1]->lon - m_position[0]->lon)/2, 
    m_position[0]->lat + (m_position[1]->lat - m_position[0]->lat)/2); 
  EXPECT_EQ(m_isoRoute->IntersectionCount(onBoundary)%2, 0); // Should be outside, even count.
 }

 TEST_F(IsoRouteTest, IntersectionCountBoundary1) {
  // On a boundary of the test polygon. Use a point in the middle of the second boundary.
  Position onBoundary(
    m_position[1]->lon + (m_position[2]->lon - m_position[1]->lon)/2, 
    m_position[1]->lat + (m_position[2]->lat - m_position[1]->lat)/2); 
  EXPECT_EQ(m_isoRoute->IntersectionCount(onBoundary)%2, 0); // Should be outside, even count.
 }

 TEST_F(IsoRouteTest, IntersectionCountBoundary2) {
  // On a boundary of the test polygon. Use a point in the middle of the third boundary.
  Position onBoundary(
    m_position[0]->lon + (m_position[2]->lon - m_position[0]->lon)/2, 
    m_position[0]->lat + (m_position[2]->lat - m_position[0]->lat)/2);   
    EXPECT_EQ(m_isoRoute->IntersectionCount(onBoundary)%2, 0); // Should be outside, even count.
 }

 TEST_F(IsoRouteTest, ContainsInside) {
  Position insidePos(1.0, 1.9); // Inside the test polygon.
  EXPECT_TRUE(m_isoRoute->Contains(insidePos, true)); // Should be inside.
 }

TEST_F(IsoRouteTest, ContainsOutside) {
  Position outsidePos(2.0, 3.0); // Outside the test polygon.
  EXPECT_FALSE(m_isoRoute->Contains(outsidePos, true)); // Should be outside.
 }

TEST_F(IsoRouteTest, ContainsBoundary0) {
  // On a boundary of the test polygon. Use a point in the middle of the first boundary.
  Position onBoundary(
    m_position[0]->lon + (m_position[1]->lon - m_position[0]->lon)/2, 
    m_position[0]->lat + (m_position[1]->lat - m_position[0]->lat)/2); 
  EXPECT_FALSE(m_isoRoute->Contains(onBoundary, true)); // Should be outside.
 }

 TEST_F(IsoRouteTest, ContainsBoundary1) {
  // On a boundary of the test polygon. Use a point in the middle of the second boundary.
    Position onBoundary(
    m_position[1]->lon + (m_position[2]->lon - m_position[1]->lon)/2, 
    m_position[1]->lat + (m_position[2]->lat - m_position[1]->lat)/2); 
  EXPECT_FALSE(m_isoRoute->Contains(onBoundary, true)); // Should be outside.
 }

 TEST_F(IsoRouteTest, ContainsBoundary2) {
  // On a boundary of the test polygon. Use a point in the middle of the third boundary.
  Position onBoundary(
    m_position[0]->lon + (m_position[2]->lon - m_position[0]->lon)/2, 
    m_position[1]->lat + (m_position[2]->lat - m_position[0]->lat)/2); 
  EXPECT_FALSE(m_isoRoute->Contains(onBoundary, true)); // Should be outside.
 }