#include "OffshoreHazards.h"

#include <gtest/gtest.h>

#include <stdexcept>
#include <string>

namespace {

using weather_routing::OffshoreHazards;

std::string Document(const std::string& records) {
  return R"({"schemaVersion":1,"dataset":"Offshore test","revision":"1","hazards":[)" +
         records + "]}";
}

constexpr auto kReef =
    R"({"id":"reef","source":"test","lat":0.02,"lon":0,"radiusNm":1,"uncertaintyNm":0.1})";

TEST(OffshoreHazards, UsesExtentAndClearanceOutsideTheChartPoint) {
  const auto dataset = OffshoreHazards::Parse(Document(kReef));
  EXPECT_EQ(dataset->Size(), 1U);
  EXPECT_FALSE(dataset->Intersect(0, -0.2, 0, 0.2, 0));
  const auto* hit = dataset->Intersect(0, -0.2, 0, 0.2, 0.2).hazard;
  ASSERT_NE(hit, nullptr);
  EXPECT_EQ(hit->id, "reef");
  EXPECT_FALSE(dataset->Intersect(0, -0.2, 0, -0.1, 0.2));
  EXPECT_TRUE(dataset->Intersect(0.02, 0, 0.02, 0, 0));
}

TEST(OffshoreHazards, ChecksAntimeridianInEitherDirection) {
  const auto dataset = OffshoreHazards::Parse(Document(
      R"({"id":"shoal","source":"test","lat":0,"lon":180,"radiusNm":0.1,"uncertaintyNm":0})"));
  EXPECT_TRUE(dataset->Intersect(0, 179.9, 0, -179.9, 0));
  EXPECT_TRUE(dataset->Intersect(0, -179.9, 0, 179.9, 0));
  EXPECT_TRUE(dataset->Intersect(0, 179.9, 0, 180.1, 0));
  EXPECT_FALSE(dataset->Intersect(0, -0.1, 0, 0.1, 0));
}

TEST(OffshoreHazards, ChecksGreatCircleThatBowsOutsideEndpointLatitudes) {
  const auto dataset = OffshoreHazards::Parse(Document(
      R"({"id":"polar","source":"test","lat":90,"lon":0,"radiusNm":0.1,"uncertaintyNm":0})"));
  EXPECT_TRUE(dataset->Intersect(80, 0, 80, 180, 0));
  EXPECT_FALSE(dataset->Intersect(80, 0, 80, 170, 0));
}

TEST(OffshoreHazards, RejectsIncompleteOrAmbiguousInputAsAUnit) {
  EXPECT_THROW(OffshoreHazards::Parse(Document(std::string(kReef) + "," + kReef)),
               std::runtime_error);
  EXPECT_THROW(OffshoreHazards::Parse(Document(kReef) + " garbage"), std::runtime_error);
  EXPECT_THROW(OffshoreHazards::Parse(Document(kReef) + " garbage }"), std::runtime_error);
  EXPECT_THROW(OffshoreHazards::Parse(Document("{}")), std::runtime_error);
  EXPECT_THROW(OffshoreHazards::Parse(Document("")), std::runtime_error);
  EXPECT_THROW(OffshoreHazards::Parse(Document(
      R"({"id":"reef","source":"test","lat":0,"lon":0,"radiusNm":0,"uncertaintyNm":0})")),
               std::runtime_error);
  const auto dataset = OffshoreHazards::Parse(Document(kReef));
  EXPECT_TRUE(dataset->Intersect(0, 0, 0, 180, 0).indeterminate);
}

}  // namespace
