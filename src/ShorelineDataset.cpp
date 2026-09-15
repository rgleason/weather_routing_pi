// SPDX-License-Identifier: GPL-3.0-or-later
// Reads the OpenCPN/zyGrib preprocessed GSHHG polygon format. Geometry and
// cache implementation is independent of the host's GSHHS crossing
// implementation.
#include "ShorelineDataset.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <iomanip>
#include <list>
#include <mutex>
#include <sstream>
#include <stdexcept>
#include <unordered_map>
#include <vector>
#include <zlib.h>

namespace weather_routing {
namespace {
constexpr double eps = 1e-10;
struct Point {
  double x, y;
};
struct Edge {
  Point a, b;
};
struct Ring {
  std::vector<Point> points;
  int level;
  bool hole;
};
double Orient(Point a, Point b, Point c) {
  return (b.x - a.x) * (c.y - a.y) - (b.y - a.y) * (c.x - a.x);
}
bool OnEdge(Point p, const Edge& e) {
  return std::abs(Orient(e.a, e.b, p)) <= eps &&
         p.x >= std::min(e.a.x, e.b.x) - eps &&
         p.x <= std::max(e.a.x, e.b.x) + eps &&
         p.y >= std::min(e.a.y, e.b.y) - eps &&
         p.y <= std::max(e.a.y, e.b.y) + eps;
}
bool Intersects(const Edge& a, const Edge& b) {
  const double p = Orient(a.a, a.b, b.a), q = Orient(a.a, a.b, b.b);
  const double r = Orient(b.a, b.b, a.a), s = Orient(b.a, b.b, a.b);
  if (((p > eps && q < -eps) || (p < -eps && q > eps)) &&
      ((r > eps && s < -eps) || (r < -eps && s > eps)))
    return true;
  return OnEdge(a.a, b) || OnEdge(a.b, b) || OnEdge(b.a, a) || OnEdge(b.b, a);
}
bool Contains(const Ring& ring, Point p) {
  bool in = false;
  for (std::size_t i = 0, j = ring.points.size() - 1; i < ring.points.size();
       j = i++) {
    const auto a = ring.points[j], b = ring.points[i];
    if (OnEdge(p, {a, b})) return true;
    if ((a.y > p.y) != (b.y > p.y) &&
        p.x < (b.x - a.x) * (p.y - a.y) / (b.y - a.y) + a.x)
      in = !in;
  }
  return in;
}
bool Land(const std::vector<Ring>& rings, Point p) {
  std::array<int, 5> coverage{};
  for (const auto& r : rings)
    if (Contains(r, p)) coverage[r.level] += r.hole ? -1 : 1;
  bool land = false;
  for (int level = 0; level < 5; level++)
    if (coverage[level] > 0) land = (level % 2) == 0;
  return land;
}
std::uint32_t Read32(std::istream& in) {
  unsigned char b[4];
  if (!in.read(reinterpret_cast<char*>(b), 4))
    throw std::runtime_error("Truncated shoreline data");
  return std::uint32_t(b[0]) | (std::uint32_t(b[1]) << 8) |
         (std::uint32_t(b[2]) << 16) | (std::uint32_t(b[3]) << 24);
}
double ReadDouble(std::istream& in) {
  std::uint64_t bits = Read32(in);
  bits |= std::uint64_t(Read32(in)) << 32;
  double d;
  std::memcpy(&d, &bits, 8);
  return d * 1e-6;
}
struct Tile {
  std::array<std::vector<Edge>, 256> edges;
  std::array<bool, 256> land{};
  std::vector<Ring> rings;
  std::size_t bytes = sizeof(Tile);
};
// SHA-256 (FIPS 180-4), streaming to avoid mapping a full dataset into RAM.
class Sha256 {
  std::array<std::uint32_t, 8> h{{0x6a09e667, 0xbb67ae85, 0x3c6ef372,
                                  0xa54ff53a, 0x510e527f, 0x9b05688c,
                                  0x1f83d9ab, 0x5be0cd19}};
  std::array<unsigned char, 64> block{};
  std::size_t n = 0;
  std::uint64_t bytes = 0;
  static std::uint32_t R(std::uint32_t x, int s) {
    return (x >> s) | (x << (32 - s));
  }
  void Transform() {
    static const std::uint32_t k[64] = {
        0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1,
        0x923f82a4, 0xab1c5ed5, 0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3,
        0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174, 0xe49b69c1, 0xefbe4786,
        0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
        0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147,
        0x06ca6351, 0x14292967, 0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13,
        0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85, 0xa2bfe8a1, 0xa81a664b,
        0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
        0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a,
        0x5b9cca4f, 0x682e6ff3, 0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208,
        0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2};
    std::uint32_t w[64];
    for (int i = 0; i < 16; i++)
      w[i] = (std::uint32_t(block[4 * i]) << 24) |
             (std::uint32_t(block[4 * i + 1]) << 16) |
             (std::uint32_t(block[4 * i + 2]) << 8) | block[4 * i + 3];
    for (int i = 16; i < 64; i++)
      w[i] = w[i - 16] +
             (R(w[i - 15], 7) ^ R(w[i - 15], 18) ^ (w[i - 15] >> 3)) +
             w[i - 7] + (R(w[i - 2], 17) ^ R(w[i - 2], 19) ^ (w[i - 2] >> 10));
    auto a = h[0], b = h[1], c = h[2], d = h[3], e = h[4], f = h[5], g = h[6],
         z = h[7];
    for (int i = 0; i < 64; i++) {
      auto t = z + (R(e, 6) ^ R(e, 11) ^ R(e, 25)) + ((e & f) ^ (~e & g)) +
               k[i] + w[i];
      auto u = (R(a, 2) ^ R(a, 13) ^ R(a, 22)) + ((a & b) ^ (a & c) ^ (b & c));
      z = g;
      g = f;
      f = e;
      e = d + t;
      d = c;
      c = b;
      b = a;
      a = t + u;
    }
    h[0] += a;
    h[1] += b;
    h[2] += c;
    h[3] += d;
    h[4] += e;
    h[5] += f;
    h[6] += g;
    h[7] += z;
  }

public:
  void Add(const unsigned char* data, std::size_t len) {
    bytes += len;
    while (len--) {
      block[n++] = *data++;
      if (n == 64) {
        Transform();
        n = 0;
      }
    }
  }
  std::string Finish() {
    const auto bits = bytes * 8;
    unsigned char pad = 128;
    Add(&pad, 1);
    pad = 0;
    while (n != 56) Add(&pad, 1);
    for (int i = 7; i >= 0; i--) {
      pad = static_cast<unsigned char>(bits >> (8 * i));
      Add(&pad, 1);
    }
    std::ostringstream out;
    out << std::hex << std::setfill('0');
    for (auto x : h) out << std::setw(8) << x;
    return out.str();
  }
};
}  // namespace
struct ShorelineDataset::Impl {
  std::ifstream file;
  std::array<std::uint32_t, 64800> offsets{};
  int version = 0;
  std::size_t size = 0, budget = 0, used = 0;
  mutable std::mutex mutex;
  std::string error;
  std::list<int> lru;
  struct Entry {
    std::shared_ptr<Tile> tile;
    std::list<int>::iterator order;
  };
  std::unordered_map<int, Entry> cache;
  Impl(const std::filesystem::path& path, std::size_t bytes)
      : file(path, std::ios::binary), budget(bytes) {
    if (!file)
      throw std::runtime_error("Shoreline dataset is missing or unreadable");
    size = static_cast<std::size_t>(std::filesystem::file_size(path));
    version = static_cast<int>(Read32(file));
    if (version < 210 || version > 240 || Read32(file) != 1 ||
        Read32(file) != 1 || Read32(file) != 0 ||
        static_cast<std::int32_t>(Read32(file)) != -90 || Read32(file) != 360 ||
        Read32(file) != 90)
      throw std::runtime_error("Unsupported shoreline data header");
    for (int i = 0; i < 5; i++) Read32(file);
    for (auto& o : offsets) {
      o = Read32(file);
      if (o < 48 + 64800 * 4 || o > size || size - o < 16)
        throw std::runtime_error("Invalid shoreline cell offset");
    }
  }
  std::shared_ptr<Tile> Get(int x, int y) {
    const int key = x * 180 + y;
    auto found = cache.find(key);
    if (found != cache.end()) {
      lru.splice(lru.begin(), lru, found->second.order);
      return found->second.tile;
    }
    auto tile = std::make_shared<Tile>();
    auto reserve = [&](auto& values, std::size_t capacity) {
      using Value = typename std::decay_t<decltype(values)>::value_type;
      if (capacity <= values.capacity()) return;
      const auto extra = (capacity - values.capacity()) * sizeof(Value);
      if (tile->bytes > budget || extra > budget - tile->bytes)
        throw ShorelineQueryError(
            "Shoreline tile exceeds cache limit. Increase the shoreline tile "
            "cache in View / Shoreline data.");
      values.reserve(capacity);
      tile->bytes += extra;
    };
    file.clear();
    file.seekg(offsets[key]);
    std::size_t vertices = 0;
    for (int level = 0; level < (version >= 230 ? 4 : 5); level++) {
      const auto count = Read32(file);
      if (count > 100000)
        throw std::runtime_error("Invalid shoreline contour count");
      for (std::uint32_t i = 0; i < count; i++) {
        const auto hole = Read32(file), n = Read32(file);
        if (hole > 1 || n < 3 || n > 2000000 || vertices + n > 2000000)
          throw std::runtime_error("Invalid shoreline polygon");
        vertices += n;
        Ring ring;
        ring.level = level;
        ring.hole = hole != 0;
        reserve(ring.points, n);
        for (std::uint32_t j = 0; j < n; j++) {
          Point p{ReadDouble(file), ReadDouble(file)};
          if (!std::isfinite(p.x) || !std::isfinite(p.y) || p.x < x - 1e-5 ||
              p.x > x + 1 + 1e-5 || p.y < y - 90 - 1e-5 || p.y > y - 89 + 1e-5)
            throw std::runtime_error("Invalid shoreline coordinate");
          ring.points.push_back(p);
        }
        if (tile->rings.size() == tile->rings.capacity())
          reserve(tile->rings,
                  std::max(std::size_t(1), tile->rings.capacity() * 2));
        tile->rings.push_back(std::move(ring));
      }
    }
    for (int by = 0; by < 16; by++)
      for (int bx = 0; bx < 16; bx++)
        tile->land[by * 16 + bx] =
            Land(tile->rings, {x + (bx + .5) / 16., y - 90 + (by + .5) / 16.});
    for (const auto& ring : tile->rings) {
      for (std::size_t i = 0, j = ring.points.size() - 1;
           i < ring.points.size(); j = i++) {
        Edge e{ring.points[j], ring.points[i]};
        // Cell clipping edges are not coastlines. Interior/endpoint land tests
        // still reject a segment traversing a cell wholly covered by land.
        if ((std::abs(e.a.x - e.b.x) < eps &&
             (std::abs(e.a.x - x) < eps || std::abs(e.a.x - x - 1) < eps)) ||
            (std::abs(e.a.y - e.b.y) < eps && (std::abs(e.a.y - y + 90) < eps ||
                                               std::abs(e.a.y - y + 89) < eps)))
          continue;
        int x0 = std::clamp(int(std::floor((std::min(e.a.x, e.b.x) - x) * 16)),
                            0, 15);
        int x1 = std::clamp(int(std::floor((std::max(e.a.x, e.b.x) - x) * 16)),
                            0, 15);
        int y0 = std::clamp(
            int(std::floor((std::min(e.a.y, e.b.y) - y + 90) * 16)), 0, 15);
        int y1 = std::clamp(
            int(std::floor((std::max(e.a.y, e.b.y) - y + 90) * 16)), 0, 15);
        // Include both bins when an edge lies on a subcell boundary.
        if (x0 > 0 && std::abs((std::min(e.a.x, e.b.x) - x) * 16 - x0) < eps)
          --x0;
        if (y0 > 0 &&
            std::abs((std::min(e.a.y, e.b.y) - y + 90) * 16 - y0) < eps)
          --y0;
        for (int by = y0; by <= y1; by++)
          for (int bx = x0; bx <= x1; bx++) {
            auto& bin = tile->edges[by * 16 + bx];
            if (bin.size() == bin.capacity())
              reserve(bin, std::max(std::size_t(1), bin.capacity() * 2));
            bin.push_back(e);
          }
      }
    }
    if (tile->bytes > budget)
      throw std::runtime_error("Shoreline tile exceeds cache budget");
    while (used + tile->bytes > budget && !lru.empty()) {
      int old = lru.back();
      used -= cache.at(old).tile->bytes;
      cache.erase(old);
      lru.pop_back();
    }
    lru.push_front(key);
    cache.emplace(key, Entry{tile, lru.begin()});
    used += tile->bytes;
    return tile;
  }
  bool CheckBin(int bx, int by, Edge segment, Point midpoint) {
    if (by < 0 || by >= 180 * 16) return false;
    bx = (bx % (360 * 16) + 360 * 16) % (360 * 16);
    const int x = bx / 16, y = by / 16, index = (by % 16) * 16 + bx % 16;
    auto tile = Get(x, y);
    const double shift = 360 * std::round((x + .5 - midpoint.x) / 360);
    segment.a.x += shift;
    segment.b.x += shift;
    midpoint.x += shift;
    if (tile->edges[index].empty()) return tile->land[index];
    if (Land(tile->rings, midpoint)) return true;
    for (const auto& e : tile->edges[index])
      if (Intersects(segment, e)) return true;
    return false;
  }
};
ShorelineDataset::ShorelineDataset(const std::filesystem::path& p,
                                   std::size_t b)
    : impl_(new Impl(p, b)) {}
ShorelineDataset::~ShorelineDataset() = default;
int ShorelineDataset::Version() const { return impl_->version; }
std::string ShorelineDataset::Error() const {
  std::lock_guard<std::mutex> lock(impl_->mutex);
  return impl_->error;
}
std::size_t ShorelineDataset::CacheBytes() const {
  std::lock_guard<std::mutex> lock(impl_->mutex);
  return impl_->used;
}
bool ShorelineDataset::CrossesLand(double lat1, double lon1, double lat2,
                                   double lon2) {
  if (!std::isfinite(lat1) || !std::isfinite(lat2) || !std::isfinite(lon1) ||
      !std::isfinite(lon2) || std::abs(lat1) > 90 || std::abs(lat2) > 90)
    throw std::runtime_error("Invalid shoreline query coordinates");
  std::lock_guard<std::mutex> lock(impl_->mutex);
  if (!impl_->error.empty()) throw ShorelineQueryError(impl_->error);
  try {
    lon1 = std::fmod(std::fmod(lon1, 360.) + 360., 360.);
    lon2 = lon1 + std::remainder(lon2 - lon1, 360.);
    Edge segment{{lon1, lat1}, {lon2, lat2}};
    const double x1 = lon1 * 16, x2 = lon2 * 16;
    const double y1 = (lat1 + 90) * 16, y2 = (lat2 + 90) * 16;
    const int bx = int(std::floor(x1)), by = int(std::floor(y1));
    const auto interior = [](double a, double b, int cell) {
      return std::min(a, b) > cell + eps &&
             std::max(a, b) < cell + 1 - eps;
    };
    // Most integration segments stay strictly inside one indexed subcell.
    // Its midpoint containment and complete segment/edge intersections also
    // cover land at either endpoint. Avoid rebuilding a traversal and checking
    // that same subcell three times. Boundary and multi-cell queries retain
    // the full traversal below, including adjacent-cell corner touches.
    if (interior(x1, x2, bx) && interior(y1, y2, by))
      return impl_->CheckBin(bx, by, segment,
                             {(lon1 + lon2) * .5, (lat1 + lat2) * .5});
    std::vector<double> cuts{0, 1};
    auto cut = [&](double a, double b) {
      if (std::abs(b - a) < eps) return;
      for (int k = int(std::floor(std::min(a, b) * 16)) + 1;
           k <= int(std::floor(std::max(a, b) * 16)); k++) {
        double t = (k / 16. - a) / (b - a);
        if (t > eps && t < 1 - eps) cuts.push_back(t);
      }
    };
    cut(lon1, lon2);
    cut(lat1, lat2);
    std::sort(cuts.begin(), cuts.end());
    auto point = [&](double t) {
      return Point{lon1 + (lon2 - lon1) * t, lat1 + (lat2 - lat1) * t};
    };
    // Check endpoints and grid-corner touches in every adjacent cell. A
    // segment can graze a land-cell corner without entering that cell's
    // interior, so midpoint traversal alone cannot establish clearance.
    for (double t : cuts) {
      Point p = point(t);
      int bx = int(std::floor(p.x * 16)), by = int(std::floor((p.y + 90) * 16));
      const bool xb = std::abs(p.x * 16 - bx) < eps;
      const bool yb = std::abs((p.y + 90) * 16 - by) < eps;
      if (t != 0 && t != 1 && !(xb && yb)) continue;
      for (int dx = 0; dx <= (xb ? 1 : 0); dx++)
        for (int dy = 0; dy <= (yb ? 1 : 0); dy++)
          if (impl_->CheckBin(bx - dx, by - dy, {p, p}, p)) return true;
    }
    for (std::size_t i = 1; i < cuts.size(); i++) {
      Point m = point((cuts[i - 1] + cuts[i]) * .5);
      Edge part{point(cuts[i - 1]), point(cuts[i])};
      int bx = int(std::floor(m.x * 16)), by = int(std::floor((m.y + 90) * 16));
      bool xb = std::abs(m.x * 16 - bx) < eps,
           yb = std::abs((m.y + 90) * 16 - by) < eps;
      for (int dx = 0; dx <= (xb ? 1 : 0); dx++)
        for (int dy = 0; dy <= (yb ? 1 : 0); dy++)
          if (impl_->CheckBin(bx - dx, by - dy, part, m)) return true;
    }
    return false;
  } catch (const std::bad_alloc&) {
    throw;
  } catch (const std::exception& e) {
    impl_->error = std::string("Shoreline data error: ") + e.what();
    throw ShorelineQueryError(impl_->error);
  }
}
std::string ShorelineSha256(const std::filesystem::path& path) {
  std::ifstream in(path, std::ios::binary);
  if (!in) throw std::runtime_error("Cannot read shoreline file");
  Sha256 hash;
  std::array<unsigned char, 65536> buf;
  while (in) {
    in.read(reinterpret_cast<char*>(buf.data()), buf.size());
    hash.Add(buf.data(), static_cast<std::size_t>(in.gcount()));
  }
  if (!in.eof()) throw std::runtime_error("Cannot read shoreline file");
  return hash.Finish();
}
void InstallShorelineGzip(const std::filesystem::path& archive,
                          const std::filesystem::path& destination,
                          const std::string& expected_hash,
                          std::size_t expected_bytes) {
  auto temporary = destination;
  temporary += ".partial";
  std::filesystem::create_directories(destination.parent_path());
  try {
    std::ifstream in(archive, std::ios::binary);
    std::ofstream out(temporary, std::ios::binary | std::ios::trunc);
    if (!in || !out)
      throw std::runtime_error(
          "Cannot open shoreline download or installation directory");
    z_stream stream{};
    if (inflateInit2(&stream, 15 + 16) != Z_OK)
      throw std::runtime_error("Cannot initialize shoreline decompression");
    struct End {
      z_stream* s;
      ~End() { inflateEnd(s); }
    } end{&stream};
    std::array<unsigned char, 65536> input, output;
    std::size_t written = 0;
    int status = Z_OK;
    while (status != Z_STREAM_END) {
      if (!stream.avail_in) {
        in.read(reinterpret_cast<char*>(input.data()), input.size());
        stream.avail_in = static_cast<uInt>(in.gcount());
        stream.next_in = input.data();
        if (!stream.avail_in)
          throw std::runtime_error("Incomplete shoreline download");
      }
      stream.avail_out = static_cast<uInt>(output.size());
      stream.next_out = output.data();
      status = inflate(&stream, Z_NO_FLUSH);
      if (status != Z_OK && status != Z_STREAM_END)
        throw std::runtime_error("Invalid shoreline gzip data");
      auto n = output.size() - stream.avail_out;
      written += n;
      if (written > expected_bytes)
        throw std::runtime_error("Shoreline download exceeds approved size");
      out.write(reinterpret_cast<char*>(output.data()), n);
      if (!out)
        throw std::runtime_error(
            "Cannot write shoreline data (check free disk space)");
    }
    if (written != expected_bytes || stream.avail_in || in.peek() != EOF)
      throw std::runtime_error("Unexpected shoreline payload length");
    out.close();
    if (!out || ShorelineSha256(temporary) != expected_hash)
      throw std::runtime_error("Shoreline checksum verification failed");
    {
      ShorelineDataset validation(temporary);
    }
    // Existing immutable approved files are retained; callers select a new
    // generation for repairs so active readers are never overwritten.
    if (std::filesystem::exists(destination))
      throw std::runtime_error("Shoreline destination already exists");
    std::filesystem::rename(temporary, destination);
  } catch (...) {
    std::error_code ec;
    std::filesystem::remove(temporary, ec);
    throw;
  }
}
}  // namespace weather_routing

namespace weather_routing {
std::string DownloadShorelineMirrors(const std::vector<std::string>& sources,
                                     const ShorelineDownload& download,
                                     const std::filesystem::path& archive,
                                     const std::filesystem::path& destination,
                                     const std::string& hash,
                                     std::size_t bytes) {
  struct Cleanup {
    std::filesystem::path p;
    ~Cleanup() {
      std::error_code ec;
      std::filesystem::remove(p, ec);
    }
  } cleanup{archive};
  std::string last;
  for (const auto& source : sources) {
    if (source.rfind("https://", 0) != 0) {
      last = "Shoreline downloads require HTTPS";
      continue;
    }
    std::error_code ec;
    std::filesystem::remove(archive, ec);
    ShorelineDownloadResult result = ShorelineDownloadResult::Failed;
    try {
      result = download(source, archive);
    } catch (const std::exception& e) {
      last = e.what();
      continue;
    }
    if (result == ShorelineDownloadResult::Cancelled)
      throw std::runtime_error(
          "Shoreline download cancelled; existing data retained.");
    if (result != ShorelineDownloadResult::Complete) {
      last = "Download failed or timed out";
      continue;
    }
    try {
      InstallShorelineGzip(archive, destination, hash, bytes);
      return source;
    } catch (const std::exception& e) {
      last = e.what();
    }
  }
  throw std::runtime_error(
      "All shoreline download sources failed; existing data retained. " + last);
}
}  // namespace weather_routing
