#include <gtest/gtest.h>

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "AppendOnlyCache.h"

namespace {

class AppendOnlyCacheTest : public ::testing::Test {
protected:
  void SetUp() override {
    directory_ = std::filesystem::temp_directory_path() /
                 ("opencpn-cache-test-" +
                  std::to_string(reinterpret_cast<std::uintptr_t>(this)));
    std::filesystem::create_directories(directory_);
    path_ = directory_ / "cache.bin";
  }

  void TearDown() override { std::filesystem::remove_all(directory_); }

  static weather_routing::AppendOnlyCacheRecord Record(
      const std::string& key, const std::string& value) {
    return {key, std::vector<unsigned char>(value.begin(), value.end())};
  }

  std::filesystem::path directory_;
  std::filesystem::path path_;
};

TEST_F(AppendOnlyCacheTest, PersistsOnlyAppendedRecordsAcrossReopen) {
  weather_routing::AppendOnlyCache cache;
  std::string error;
  ASSERT_TRUE(cache.Open(path_.string(), "charts-v1", 100, &error)) << error;
  ASSERT_TRUE(cache.PutBatch({Record("a", "alpha"), Record("b", "beta")},
                             &error))
      << error;
  const std::uint64_t first_size = cache.FileBytes();
  ASSERT_TRUE(cache.PutBatch({Record("c", "gamma")}, &error)) << error;
  EXPECT_GT(cache.FileBytes(), first_size);
  EXPECT_LT(cache.FileBytes() - first_size, first_size);

  weather_routing::AppendOnlyCache reopened;
  ASSERT_TRUE(reopened.Open(path_.string(), "charts-v1", 100, &error))
      << error;
  std::vector<unsigned char> value;
  ASSERT_TRUE(reopened.Get("b", &value, &error)) << error;
  EXPECT_EQ(std::string(value.begin(), value.end()), "beta");
  EXPECT_EQ(reopened.EntryCount(), 3U);
}

TEST_F(AppendOnlyCacheTest, LatestRecordWinsWithoutSnapshotRewrite) {
  weather_routing::AppendOnlyCache cache;
  std::string error;
  ASSERT_TRUE(cache.Open(path_.string(), "charts-v1", 100, &error)) << error;
  ASSERT_TRUE(cache.PutBatch({Record("tile", "old")}, &error)) << error;
  ASSERT_TRUE(cache.PutBatch({Record("tile", "new")}, &error)) << error;

  weather_routing::AppendOnlyCache reopened;
  ASSERT_TRUE(reopened.Open(path_.string(), "charts-v1", 100, &error))
      << error;
  std::vector<unsigned char> value;
  ASSERT_TRUE(reopened.Get("tile", &value, &error)) << error;
  EXPECT_EQ(std::string(value.begin(), value.end()), "new");
  EXPECT_EQ(reopened.EntryCount(), 1U);
}

TEST_F(AppendOnlyCacheTest, ContainsTracksOnlyLiveValidatedRecords) {
  weather_routing::AppendOnlyCache cache;
  std::string error;
  ASSERT_TRUE(cache.Open(path_.string(), "charts-v1", 100, &error)) << error;
  ASSERT_TRUE(cache.PutBatch({Record("tile", "safe")}, &error)) << error;
  EXPECT_TRUE(cache.Contains("tile"));
  EXPECT_FALSE(cache.Contains("missing"));
  ASSERT_TRUE(cache.Erase("tile", &error)) << error;
  EXPECT_FALSE(cache.Contains("tile"));

  weather_routing::AppendOnlyCache reopened;
  ASSERT_TRUE(reopened.Open(path_.string(), "charts-v1", 100, &error))
      << error;
  EXPECT_FALSE(reopened.Contains("tile"));
}

TEST_F(AppendOnlyCacheTest, IgnoresTruncatedTailAndKeepsEarlierRecords) {
  weather_routing::AppendOnlyCache cache;
  std::string error;
  ASSERT_TRUE(cache.Open(path_.string(), "charts-v1", 100, &error)) << error;
  ASSERT_TRUE(cache.PutBatch({Record("safe", "complete")}, &error)) << error;
  {
    std::ofstream output(path_, std::ios::binary | std::ios::app);
    const char partial[] = {'W', 'C', 'R', '1', 20, 0};
    output.write(partial, sizeof(partial));
  }

  weather_routing::AppendOnlyCache reopened;
  ASSERT_TRUE(reopened.Open(path_.string(), "charts-v1", 100, &error))
      << error;
  std::vector<unsigned char> value;
  ASSERT_TRUE(reopened.Get("safe", &value, &error)) << error;
  EXPECT_EQ(std::string(value.begin(), value.end()), "complete");
  EXPECT_GT(reopened.RecoveredTailRecords(), 0U);

  ASSERT_TRUE(reopened.PutBatch({Record("after-recovery", "durable")},
                                &error))
      << error;
  weather_routing::AppendOnlyCache after_append;
  ASSERT_TRUE(after_append.Open(path_.string(), "charts-v1", 100, &error))
      << error;
  ASSERT_TRUE(after_append.Get("after-recovery", &value, &error)) << error;
  EXPECT_EQ(std::string(value.begin(), value.end()), "durable");
}

TEST_F(AppendOnlyCacheTest, IdentityMismatchInvalidatesAllEntries) {
  weather_routing::AppendOnlyCache cache;
  std::string error;
  ASSERT_TRUE(cache.Open(path_.string(), "old-charts", 100, &error)) << error;
  ASSERT_TRUE(cache.PutBatch({Record("tile", "safe")}, &error)) << error;

  weather_routing::AppendOnlyCache changed;
  ASSERT_TRUE(changed.Open(path_.string(), "new-charts", 100, &error))
      << error;
  EXPECT_TRUE(changed.IdentityWasReset());
  EXPECT_EQ(changed.EntryCount(), 0U);
  EXPECT_TRUE(std::filesystem::exists(path_.string() + ".incompatible"));
  std::vector<unsigned char> value;
  EXPECT_FALSE(changed.Get("tile", &value, &error));
}

TEST_F(AppendOnlyCacheTest, EntryLimitSurvivesReopenUsingTombstones) {
  weather_routing::AppendOnlyCache cache;
  std::string error;
  ASSERT_TRUE(cache.Open(path_.string(), "charts-v1", 2, &error)) << error;
  ASSERT_TRUE(cache.PutBatch(
      {Record("a", "1"), Record("b", "2"), Record("c", "3")}, &error))
      << error;
  EXPECT_EQ(cache.EntryCount(), 2U);

  weather_routing::AppendOnlyCache reopened;
  ASSERT_TRUE(reopened.Open(path_.string(), "charts-v1", 2, &error))
      << error;
  EXPECT_EQ(reopened.EntryCount(), 2U);
  std::vector<unsigned char> value;
  EXPECT_FALSE(reopened.Get("a", &value, &error));
  EXPECT_TRUE(reopened.Get("b", &value, &error));
  EXPECT_TRUE(reopened.Get("c", &value, &error));
}

TEST_F(AppendOnlyCacheTest, CompactionPreservesLiveValues) {
  weather_routing::AppendOnlyCache cache;
  std::string error;
  ASSERT_TRUE(cache.Open(path_.string(), "charts-v1", 100, &error)) << error;
  ASSERT_TRUE(cache.PutBatch({Record("tile", "old")}, &error)) << error;
  ASSERT_TRUE(cache.PutBatch({Record("tile", "new"), Record("other", "x")},
                             &error))
      << error;
  const std::uint64_t before = cache.FileBytes();
  ASSERT_TRUE(cache.Compact(&error)) << error;
  EXPECT_LT(cache.FileBytes(), before);

  weather_routing::AppendOnlyCache reopened;
  ASSERT_TRUE(reopened.Open(path_.string(), "charts-v1", 100, &error))
      << error;
  std::vector<unsigned char> value;
  ASSERT_TRUE(reopened.Get("tile", &value, &error)) << error;
  EXPECT_EQ(std::string(value.begin(), value.end()), "new");
}

}  // namespace
