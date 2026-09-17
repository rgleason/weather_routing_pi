// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <filesystem>
#include <memory>
#include <string>
#include <cstddef>
#include <stdexcept>

namespace weather_routing {
class ShorelineQueryError : public std::runtime_error {
public:
  using std::runtime_error::runtime_error;
};

// Immutable file identity, thread-safe bounded tile cache. Coordinates are
// WGS84 degrees; segments follow the shortest longitude interval, as in the
// host API.
class ShorelineDataset {
public:
  explicit ShorelineDataset(const std::filesystem::path& file,
                            std::size_t cache_bytes = 64u * 1024 * 1024);
  ~ShorelineDataset();
  ShorelineDataset(const ShorelineDataset&) = delete;
  ShorelineDataset& operator=(const ShorelineDataset&) = delete;
  bool CrossesLand(double lat1, double lon1, double lat2, double lon2);
  std::size_t CacheBytes() const;
  int Version() const;
  // A failed read invalidates this snapshot until the manager prepares it
  // again.
  std::string Error() const;

private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};
std::string ShorelineSha256(const std::filesystem::path& path);
// Hash the uncompressed payload, enforce its exact length, and atomically
// install a new destination only after complete validation. Never extracts
// filenames.
void InstallShorelineGzip(const std::filesystem::path& archive,
                          const std::filesystem::path& destination,
                          const std::string& expected_hash,
                          std::size_t expected_bytes);
}  // namespace weather_routing

#include <functional>
#include <vector>
namespace weather_routing {
enum class ShorelineDownloadResult { Complete, Failed, Cancelled };
using ShorelineDownload = std::function<ShorelineDownloadResult(
    const std::string&, const std::filesystem::path&)>;
std::string DownloadShorelineMirrors(const std::vector<std::string>& sources,
                                     const ShorelineDownload& download,
                                     const std::filesystem::path& archive,
                                     const std::filesystem::path& destination,
                                     const std::string& hash,
                                     std::size_t bytes);
}  // namespace weather_routing
