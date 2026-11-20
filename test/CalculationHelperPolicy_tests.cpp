#include <gtest/gtest.h>
#include <wx/fileconf.h>
#include <wx/sstream.h>
#include "CalculationHelperPolicy.h"
TEST(CalculationHelpers, FirstRunIsConservativeAndChoicesSurviveReopen) {
  wxStringInputStream empty(""); wxFileConfig config(empty);
  config.SetPath("/PlugIns/WeatherRouting");
  auto value=weather_routing::ReadCalculationHelperPreferences(config);
  EXPECT_FALSE(value.additional_helpers); EXPECT_FALSE(value.parallel_methods);
  weather_routing::WriteCalculationHelperPreferences(config,{true,true});
  wxStringOutputStream saved; ASSERT_TRUE(config.Save(saved));
  wxStringInputStream input(saved.GetString()); wxFileConfig reopened(input);
  reopened.SetPath("/PlugIns/WeatherRouting");
  value=weather_routing::ReadCalculationHelperPreferences(reopened);
  EXPECT_TRUE(value.additional_helpers); EXPECT_TRUE(value.parallel_methods);
  weather_routing::WriteCalculationHelperPreferences(reopened,{false,true});
  value=weather_routing::ReadCalculationHelperPreferences(reopened);
  EXPECT_FALSE(value.additional_helpers); EXPECT_TRUE(value.parallel_methods);
  reopened.SetPath("/PlugIns/xWeatherRouting2");
  value=weather_routing::ReadCalculationHelperPreferences(reopened);
  EXPECT_FALSE(value.additional_helpers); EXPECT_FALSE(value.parallel_methods);
}
TEST(CalculationHelpers, PreparedWeatherCacheIsSmallWithoutOptInOrAvailableMemory) {
  using weather_routing::PreparedWeatherSampleLimit;
  EXPECT_EQ(PreparedWeatherSampleLimit(false,false,32768),32768u);
  EXPECT_EQ(PreparedWeatherSampleLimit(true,false,32768),500000u);
  EXPECT_EQ(PreparedWeatherSampleLimit(true,true,32768),32768u);
  EXPECT_EQ(PreparedWeatherSampleLimit(true,false,0),32768u);
  EXPECT_LE(PreparedWeatherSampleLimit(true,false,128),8192u);
}

TEST(CalculationHelpers, CacheMaintenanceNeverDeletesAnUnboundedFractionUnderLock) {
  using weather_routing::WeatherSampleEvictionBatch;
  EXPECT_EQ(WeatherSampleEvictionBatch(500000),256u);
  EXPECT_EQ(WeatherSampleEvictionBatch(32768),256u);
  EXPECT_EQ(WeatherSampleEvictionBatch(10),1u);
  EXPECT_EQ(WeatherSampleEvictionBatch(1),1u);
}
