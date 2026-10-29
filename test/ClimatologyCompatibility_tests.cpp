#include <gtest/gtest.h>
#include <wx/datetime.h>

#include <cstdint>
#include <sstream>
#include <json/json.h>

#include "ClimatologyCompatibility.h"

namespace {
bool Data(int setting, const wxDateTime&, double lat, double lon,
          double& direction, double& speed) {
  direction = 270; speed = 12.5;
  return setting == 0 && lat == 53.4 && lon == -5.5;
}
bool Atlas(const wxDateTime&, double, double, int& count, double* frequencies,
           double* speeds, double& gale, double& calm) {
  if (count != 8) return false;
  frequencies[0] = .5; speeds[0] = 12.5; gale = .1; calm = .2;
  return true;
}
int Cyclones(double, double, double, double, const wxDateTime&, int days) {
  return days == 30 ? 2 : -1;
}
template <typename T> std::string Pointer(T function, bool prefix = true) {
  std::ostringstream value;
  if (prefix) value << "0x";
  value << std::hex << reinterpret_cast<std::uintptr_t>(function);
  return value.str();
}
Json::Value Message(int major, int minor) {
  Json::Value value;
  value["ClimatologyVersionMajor"] = major;
  value["ClimatologyVersionMinor"] = minor;
  value["ClimatologyDataPtr"] = Pointer(&Data);
  value["ClimatologyWindAtlasDataPtr"] = Pointer(&Atlas);
  value["ClimatologyCycloneTrackCrossingsPtr"] = Pointer(&Cyclones);
  return value;
}
weather_routing::ClimatologyMessage Read(const Json::Value& value) {
  return weather_routing::ReadClimatologyMessage(Json::FastWriter().write(value));
}
}  // namespace

TEST(ClimatologyCompatibility, Catalogue16And2026Data20RetainTheSameCallableABI) {
  for (int major : {1, 2}) {
    auto value = Message(major, major == 1 ? 6 : 0);
    if (major == 2) {
      value["ClimatologyDatasetVersion"] = "ocpn-climatology-2026.2";
      value["ClimatologyQueryAPIVersion"] = 1;
      value["ClimatologyQueryV1Ptr"] = "0x123"; // Additive API is not invoked.
    }
    const auto api = Read(value);
    ASSERT_TRUE(api.valid); ASSERT_TRUE(api.supported);
    ASSERT_EQ(api.data, &Data); ASSERT_EQ(api.wind_atlas, &Atlas);
    ASSERT_EQ(api.cyclones, &Cyclones);
    wxDateTime date(24, wxDateTime::Oct, 2026);
    double direction = 0, speed = 0;
    EXPECT_TRUE(api.data(0, date, 53.4, -5.5, direction, speed));
    EXPECT_EQ(direction, 270); EXPECT_EQ(speed, 12.5);
    int count = 8; double frequencies[8]{}, speeds[8]{}, gale = 0, calm = 0;
    EXPECT_TRUE(api.wind_atlas(date, 53.4, -5.5, count, frequencies, speeds, gale, calm));
    EXPECT_EQ(frequencies[0], .5); EXPECT_EQ(speeds[0], 12.5);
    EXPECT_EQ(gale, .1); EXPECT_EQ(calm, .2);
    EXPECT_EQ(api.cyclones(53.4, -5.5, 53.4, -5.3, date, 30), 2);
    EXPECT_EQ(api.dataset, major == 2 ? "ocpn-climatology-2026.2" : "legacy/unversioned");
  }
}

TEST(ClimatologyCompatibility, WindowsPointerTextAndOriginal010AreSupported) {
  auto value = Message(0, 10);
  value["ClimatologyDataPtr"] = Pointer(&Data, false);
  const auto api = Read(value);
  ASSERT_TRUE(api.valid); ASSERT_TRUE(api.supported); EXPECT_EQ(api.data, &Data);
}

TEST(ClimatologyCompatibility, UnloadClearsAllThreeCallbacks) {
  auto value = Message(2, 0);
  value["ClimatologyDataPtr"] = "(nil)";
  value["ClimatologyWindAtlasDataPtr"] = "0x0";
  value["ClimatologyCycloneTrackCrossingsPtr"] = "0000000000000000";
  const auto api = Read(value);
  EXPECT_TRUE(api.valid); EXPECT_TRUE(api.supported);
  EXPECT_EQ(api.data, nullptr); EXPECT_EQ(api.wind_atlas, nullptr);
  EXPECT_EQ(api.cyclones, nullptr);
}

TEST(ClimatologyCompatibility, EveryAdvertisementIsValidatedAfterAnAcceptedMessage) {
  auto api = Read(Message(1, 6)); ASSERT_NE(api.data, nullptr);
  for (const auto& version : {std::pair<int,int>{0,9}, {1,7}, {2,1}, {3,0}, {-1,6}}) {
    api = Read(Message(version.first, version.second));
    EXPECT_FALSE(api.supported); EXPECT_EQ(api.data, nullptr);
    EXPECT_EQ(api.wind_atlas, nullptr); EXPECT_EQ(api.cyclones, nullptr);
  }
}

TEST(ClimatologyCompatibility, MalformedMessagesNeverRetainCallbacks) {
  EXPECT_FALSE(weather_routing::ReadClimatologyMessage("{bad json").valid);
  for (const char* pointer : {"", "-1", " -1", "0x123junk", "0x10000000000000000"}) {
    auto value = Message(2, 0); value["ClimatologyWindAtlasDataPtr"] = pointer;
    const auto api = Read(value);
    EXPECT_FALSE(api.valid); EXPECT_EQ(api.data, nullptr);
    EXPECT_EQ(api.wind_atlas, nullptr); EXPECT_EQ(api.cyclones, nullptr);
  }
  auto value = Message(2, 0); value["ClimatologyVersionMajor"] = "2";
  EXPECT_FALSE(Read(value).valid);
}
