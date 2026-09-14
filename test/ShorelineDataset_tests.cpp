// SPDX-License-Identifier: GPL-3.0-or-later
#include "ShorelineDataset.h"
#include "ShorelineSpec.h"
#include <gtest/gtest.h>
#include <fstream>
#include <random>
#include <future>
#include <array>
#include <cstring>
#include <zlib.h>
using namespace weather_routing;
namespace fs = std::filesystem;
namespace {
struct Files {
  fs::path dir = fs::temp_directory_path() /
                 ("wr-shoreline-" + std::to_string(std::random_device{}()));
  Files() { fs::create_directories(dir); }
  ~Files() {
    std::error_code e;
    fs::remove_all(dir, e);
  }
  fs::path operator/(const char* n) { return dir / n; }
};
void U32(std::ostream& out, std::uint32_t n) {
  for (int i = 0; i < 4; i++) out.put(char(n >> (8 * i)));
}
void D64(std::ostream& out, double n) {
  n *= 1e6;
  std::uint64_t bits;
  std::memcpy(&bits, &n, 8);
  U32(out, std::uint32_t(bits));
  U32(out, std::uint32_t(bits >> 32));
}
// One island in cell 0E,0N; all remaining cells share a water record.
void Fixture(const fs::path& p) {
  std::ofstream out(p, std::ios::binary);
  for (auto n : {237, 1, 1, 0, -90, 360, 90, 1, 2, 3, 4, 5}) U32(out, n);
  const unsigned water = 48 + 64800 * 4, island = water + 16;
  for (int i = 0; i < 64800; i++) U32(out, i == 90 ? island : water);
  for (int i = 0; i < 4; i++) U32(out, 0);
  U32(out, 1);
  U32(out, 0);
  U32(out, 4);
  for (auto v : std::vector<std::pair<double, double>>{
           {.2, .2}, {.8, .2}, {.8, .8}, {.2, .8}}) {
    D64(out, v.first);
    D64(out, v.second);
  }
  for (int i = 0; i < 3; i++) U32(out, 0);
}
void Gzip(const fs::path& in, const fs::path& out) {
  std::ifstream f(in, std::ios::binary);
  std::string data((std::istreambuf_iterator<char>(f)), {});
  auto gz = gzopen(out.string().c_str(), "wb");
  ASSERT_NE(gz, nullptr);
  ASSERT_EQ(gzwrite(gz, data.data(), unsigned(data.size())), int(data.size()));
  ASSERT_EQ(gzclose(gz), Z_OK);
}
}  // namespace
TEST(ShorelineDataset, MissingAndInvalidHeadersFail) {
  Files f;
  EXPECT_THROW(ShorelineDataset(f / "absent"), std::runtime_error);
  std::ofstream(f / "bad") << "not a polygon file";
  EXPECT_THROW(ShorelineDataset(f / "bad"), std::runtime_error);
}
TEST(ShorelineDataset, LandContainmentAndWater) {
  Files f;
  Fixture(f / "data");
  ShorelineDataset d(f / "data");
  EXPECT_TRUE(d.CrossesLand(.4, .4, .6, .6));
  EXPECT_TRUE(d.CrossesLand(.5, 0, .5, 1));
  EXPECT_FALSE(d.CrossesLand(.1, .1, .1, .9));
  EXPECT_FALSE(d.CrossesLand(40, -40, 40.1, -39.9));
}
TEST(ShorelineDataset, ShortSegmentsRetainEndpointAndCornerProtection) {
  Files f;
  Fixture(f / "data");
  ShorelineDataset d(f / "data");
  // Each query lies strictly inside one 1/16-degree index bin. A midpoint
  // alone would miss the first crossing and the island-corner touch.
  EXPECT_TRUE(d.CrossesLand(.45, .21, .45, .189));
  EXPECT_TRUE(d.CrossesLand(.45, .189, .45, .21));
  EXPECT_TRUE(d.CrossesLand(.45, .2, .45, .189));
  EXPECT_TRUE(d.CrossesLand(.789, .811, .811, .789));
  EXPECT_FALSE(d.CrossesLand(.79, .811, .811, .79));
  EXPECT_TRUE(d.CrossesLand(.45, .21, .45, .21));
  EXPECT_FALSE(d.CrossesLand(.45, .19, .45, .19));
}
TEST(ShorelineDataset, GridBoundariesAndZeroLength) {
  Files f;
  Fixture(f / "data");
  ShorelineDataset d(f / "data");
  EXPECT_TRUE(d.CrossesLand(.5, 0, .5, 1));
  EXPECT_TRUE(d.CrossesLand(0, .5, 1, .5));
  EXPECT_TRUE(d.CrossesLand(.5, .5, .5, .5));
  EXPECT_FALSE(d.CrossesLand(.1, .1, .1, .1));
  EXPECT_TRUE(d.CrossesLand(.2, .2, .2, .2));
}
TEST(ShorelineDataset, DatelineAndPolesAreValid) {
  Files f;
  Fixture(f / "data");
  ShorelineDataset d(f / "data");
  EXPECT_FALSE(d.CrossesLand(0, 179, 0, -179));
  EXPECT_FALSE(d.CrossesLand(90, 0, 90, 2));
  EXPECT_FALSE(d.CrossesLand(-90, 0, -89, 2));
  EXPECT_THROW(d.CrossesLand(91, 0, 0, 0), std::runtime_error);
  EXPECT_THROW(d.CrossesLand(0, NAN, 0, 0), std::runtime_error);
}
TEST(ShorelineDataset, DirectionSymmetry) {
  Files f;
  Fixture(f / "data");
  ShorelineDataset d(f / "data");
  std::mt19937 rng(1176);
  std::uniform_real_distribution<double> u(-.1, 1.1);
  for (int i = 0; i < 1000; i++) {
    double a = u(rng), b = u(rng), c = u(rng), e = u(rng);
    EXPECT_EQ(d.CrossesLand(a, b, c, e), d.CrossesLand(c, e, a, b));
  }
}
TEST(ShorelineDataset, CacheEvictionPreservesAnswers) {
  Files f;
  Fixture(f / "data");
  ShorelineDataset d(f / "data", 64 * 1024);
  EXPECT_TRUE(d.CrossesLand(.4, .4, .6, .6));
  for (int i = 2; i < 80; i++) EXPECT_FALSE(d.CrossesLand(30, i, 30.1, i + .1));
  EXPECT_LE(d.CacheBytes(), 64u * 1024);
  EXPECT_TRUE(d.CrossesLand(.4, .4, .6, .6));
}
TEST(ShorelineDataset, Sha256KnownVectors) {
  Files f;
  std::ofstream(f / "empty");
  std::ofstream(f / "abc") << "abc";
  EXPECT_EQ(ShorelineSha256(f / "empty"),
            "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
  EXPECT_EQ(ShorelineSha256(f / "abc"),
            "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
}
TEST(ShorelineDataset, VerifiedInstallationAndNoOverwrite) {
  Files f;
  Fixture(f / "data");
  Gzip(f / "data", f / "gz");
  auto hash = ShorelineSha256(f / "data");
  InstallShorelineGzip(f / "gz", f / "installed", hash,
                       fs::file_size(f / "data"));
  EXPECT_EQ(ShorelineSha256(f / "installed"), hash);
  EXPECT_THROW(InstallShorelineGzip(f / "gz", f / "installed", hash,
                                    fs::file_size(f / "data")),
               std::runtime_error);
  EXPECT_EQ(ShorelineSha256(f / "installed"), hash);
}
TEST(ShorelineDataset, CorruptOrOversizedPayloadCannotActivate) {
  Files f;
  Fixture(f / "data");
  Gzip(f / "data", f / "gz");
  EXPECT_THROW(InstallShorelineGzip(f / "gz", f / "installed", "wrong",
                                    fs::file_size(f / "data")),
               std::runtime_error);
  EXPECT_FALSE(fs::exists(f / "installed"));
  EXPECT_FALSE(fs::exists(f / "installed.partial"));
  EXPECT_THROW(InstallShorelineGzip(f / "gz", f / "installed",
                                    ShorelineSha256(f / "data"), 10),
               std::runtime_error);
  fs::resize_file(f / "gz", 12);
  EXPECT_THROW(InstallShorelineGzip(f / "gz", f / "installed",
                                    ShorelineSha256(f / "data"),
                                    fs::file_size(f / "data")),
               std::runtime_error);
}
TEST(ShorelineDataset, MirrorFailureAndCorruptionThenSuccess) {
  Files f;
  Fixture(f / "data");
  Gzip(f / "data", f / "good");
  int calls = 0;
  auto used = DownloadShorelineMirrors(
      {"https://one", "https://two", "https://three"},
      [&](const std::string&, const fs::path& out) {
        ++calls;
        if (calls == 1) return ShorelineDownloadResult::Failed;
        if (calls == 2)
          std::ofstream(out) << "corrupt";
        else
          fs::copy_file(f / "good", out);
        return ShorelineDownloadResult::Complete;
      },
      f / "download", f / "installed", ShorelineSha256(f / "data"),
      fs::file_size(f / "data"));
  EXPECT_EQ(calls, 3);
  EXPECT_EQ(used, "https://three");
  EXPECT_TRUE(fs::exists(f / "installed"));
  EXPECT_FALSE(fs::exists(f / "download"));
}
TEST(ShorelineDataset, CancellationDoesNotTryAnotherMirror) {
  Files f;
  int calls = 0;
  EXPECT_THROW(DownloadShorelineMirrors(
                   {"https://one", "https://two"},
                   [&](const std::string&, const fs::path&) {
                     ++calls;
                     return ShorelineDownloadResult::Cancelled;
                   },
                   f / "download", f / "installed", "x", 1),
               std::runtime_error);
  EXPECT_EQ(calls, 1);
  EXPECT_FALSE(fs::exists(f / "installed"));
}
TEST(ShorelineDataset, InsecureMirrorIsNotContacted) {
  Files f;
  int calls = 0;
  EXPECT_THROW(DownloadShorelineMirrors(
                   {"http://one"},
                   [&](const std::string&, const fs::path&) {
                     ++calls;
                     return ShorelineDownloadResult::Complete;
                   },
                   f / "download", f / "installed", "x", 1),
               std::runtime_error);
  EXPECT_EQ(calls, 0);
}

TEST(ShorelineDataset, ReadFailureInvalidatesSnapshot) {
  Files f;
  Fixture(f / "data");
  // Keep the index and minimum cell record valid, but truncate the polygon
  // before opening the reader. Truncating an already-open tiny fixture may
  // leave its complete valid bytes in the C++ stream's read-ahead buffer
  // (notably on Windows), which does not exercise a failed read at all.
  fs::resize_file(f / "data", 48 + 64800 * 4 + 16 + 16);
  ShorelineDataset d(f / "data");
  EXPECT_THROW(d.CrossesLand(.4, .4, .6, .6), ShorelineQueryError);
  EXPECT_FALSE(d.Error().empty());
  // Restoring the same filename must not revive a failed in-flight snapshot.
  Fixture(f / "data");
  EXPECT_THROW(d.CrossesLand(.4, .4, .6, .6), ShorelineQueryError);
  ShorelineDataset repaired(f / "data");
  EXPECT_TRUE(repaired.CrossesLand(.4, .4, .6, .6));
}
TEST(ShorelineDataset, InsufficientCacheIsExplicit) {
  Files f;
  Fixture(f / "data");
  ShorelineDataset d(f / "data", 1);
  EXPECT_THROW(d.CrossesLand(.5, .5, .5, .5), ShorelineQueryError);
  EXPECT_NE(d.Error().find("cache limit"), std::string::npos);
  EXPECT_EQ(d.CacheBytes(), 0u);
}
TEST(ShorelineDataset, SharedReaderConcurrentQueries) {
  Files f;
  Fixture(f / "data");
  ShorelineDataset d(f / "data", 64 * 1024);
  std::vector<std::future<bool>> jobs;
  for (int i = 0; i < 4; i++)
    jobs.push_back(std::async(std::launch::async, [&] {
      for (int j = 0; j < 100; j++)
        if (!d.CrossesLand(.5, 0, .5, 1) || d.CrossesLand(40, j, 40.1, j + .1))
          return false;
      return true;
    }));
  for (auto& job : jobs) EXPECT_TRUE(job.get());
  EXPECT_LE(d.CacheBytes(), 64u * 1024);
}
TEST(ShorelineDataset, LakesIslandsAndClippingBoundaries) {
  Files f;
  auto path = f / "data";
  Fixture(path);
  std::ofstream out(path, std::ios::binary | std::ios::in);
  out.seekp(48 + 64800 * 4 + 16);
  // Whole-cell land, lake, island within lake, and pond within island.
  // These are the four GSHHG nesting levels, not four independent land lists.
  for (double inset : {0., .2, .4, .45}) {
    U32(out, 1);
    U32(out, 0);
    U32(out, 4);
    for (auto v : std::vector<std::pair<double, double>>{{inset, inset},
                                                         {1 - inset, inset},
                                                         {1 - inset, 1 - inset},
                                                         {inset, 1 - inset}}) {
      D64(out, v.first);
      D64(out, v.second);
    }
  }
  out.close();
  ShorelineDataset d(path);
  EXPECT_TRUE(d.CrossesLand(.1, .1, .1, .9));
  EXPECT_FALSE(d.CrossesLand(.3, .3, .3, .7));
  EXPECT_TRUE(d.CrossesLand(.42, .42, .42, .58));
  EXPECT_FALSE(d.CrossesLand(.5, .5, .5, .5));
  EXPECT_TRUE(d.CrossesLand(.5, 0, .5, 1));
  // Exactly along a whole-cell clipped boundary must not be mistaken for water.
  EXPECT_TRUE(d.CrossesLand(0, .1, 0, .9));
  EXPECT_TRUE(d.CrossesLand(.1, 0, .9, 0));
  EXPECT_TRUE(d.CrossesLand(-.1, .1, .1, -.1));  // Grazes land-cell corner.
  EXPECT_TRUE(d.CrossesLand(.1, -.1, -.1, .1));
  EXPECT_TRUE(d.CrossesLand(-.1, -.1, 0, 0));  // Endpoint on corner.
}
TEST(ShorelineDataset, VerifiedBundledFullResolutionRealWorld) {
  Files f;
  const auto archive =
      fs::u8path(WR_TEST_SHORELINE_DATA) / "poly-f-2.3.7.dat.gz";
  ASSERT_EQ(ShorelineSha256(archive),
            "a36cb8c4fda7d56cfd851d92ed70a315d0a2ff81e10e99e8a0141ce9dc4e6d60");
  const auto installed = f.dir / fs::u8path("coast with spaces-é.dat");
  InstallShorelineGzip(
      archive, installed,
      "8d4d73897c82dd0e8df63f33e4dab9dd3aea7a26459b923bf404cb9299f1cf04",
      171582632);
  ShorelineDataset d(installed, 16 * 1024 * 1024);
  ASSERT_EQ(d.Version(), 237);
  const std::vector<std::array<double, 4>> crossings = {
      {48, -5, 48, 5},           // Brittany, exact latitude cell boundary
      {47, 0, 49, 0},            // France, exact longitude cell boundary
      {53.3, -4.8, 53.3, -4.5},  // Holy Island
      {-18.7260670668956, -174.13594408099112, -18.616207410317653,
       -174.0080853580806},
      {48.85, 2.35, 48.86, 2.36},  // Entire segment on land
      {0, 179.99, 0, -179.99}      // Dateline open-water control below
  };
  for (std::size_t i = 0; i < crossings.size(); i++) {
    auto c = crossings[i];
    const bool expected = i + 1 < crossings.size();
    EXPECT_EQ(d.CrossesLand(c[0], c[1], c[2], c[3]), expected) << i;
    EXPECT_EQ(d.CrossesLand(c[2], c[3], c[0], c[1]), expected) << i;
  }
  EXPECT_FALSE(d.CrossesLand(45, -40, 46, -39));
  EXPECT_FALSE(d.CrossesLand(54, -4.8, 54, -4.5));
  EXPECT_LE(d.CacheBytes(), 16u * 1024 * 1024);
}

class BundledShoreline : public testing::TestWithParam<int> {};
TEST_P(BundledShoreline, OfflineInstallVerificationAndGlobalTileCoverage) {
  Files files;
  const auto& spec = ShorelineSpecFor(GetParam());
  const auto archive = fs::path(WEATHER_ROUTING_SOURCE_DIR) / "data" / "shoreline" /
      (std::string("poly-") + spec.code + "-2.3.7.dat.gz");
  InstallShorelineGzip(archive, files / "shoreline.dat", spec.hash, spec.bytes);
  EXPECT_EQ(ShorelineSha256(files / "shoreline.dat"), spec.hash);
  ShorelineDataset data(files / "shoreline.dat", 16u * 1024 * 1024);
  EXPECT_TRUE(data.CrossesLand(48.85, 2.35, 48.85, 2.35));
  EXPECT_FALSE(data.CrossesLand(0, -30, 0, -29));
  EXPECT_TRUE(data.CrossesLand(50, -6, 51, -1));
  // Exercise every tile with a small cache, including eviction and reloads.
  for (int lon = 0; lon < 360; ++lon) for (int lat = -90; lat < 90; ++lat)
    EXPECT_NO_THROW(data.CrossesLand(lat + .5, lon + .5, lat + .5, lon + .5));
  EXPECT_LE(data.CacheBytes(), 16u * 1024 * 1024);
  EXPECT_TRUE(data.Error().empty());
}
INSTANTIATE_TEST_SUITE_P(AllFiveResolutions, BundledShoreline, testing::Range(0, 5));
TEST(ShorelineDataset, InvalidResolutionRejected) {
  EXPECT_THROW(ShorelineSpecFor(-1), std::invalid_argument);
  EXPECT_THROW(ShorelineSpecFor(5), std::invalid_argument);
}
