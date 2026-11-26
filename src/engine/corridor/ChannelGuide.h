// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "supercpn/weather_routing/Providers.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <numbers>
#include <queue>
#include <set>

namespace corridor_routing {
namespace wr = supercpn::weather_routing;

// Discover a water connection only where the previous geographical fields
// could not find one. Cache clearance at vertices, then check the actual
// edges of each proposed connection. The result is search guidance.
class ChannelGuide {
  struct XY {
    double x, y;
  };
  struct Section {
    XY from, to;
    double length, after;
  };
  struct Projection {
    double offset, remaining;
    std::size_t section;
    double fraction;
  };

  wr::GeoPoint origin_;
  double longitudeScale_;
  std::vector<Section> sections_;
  std::uint64_t queries_{};
  double length_{};

  XY project(wr::GeoPoint point) const {
    return {wr::normalizeLongitude(point.longitude - origin_.longitude) * longitudeScale_,
            (point.latitude - origin_.latitude) * 60.};
  }

  wr::GeoPoint position(XY p) const {
    return {origin_.latitude + p.y / 60.,
            wr::normalizeLongitude(origin_.longitude + p.x / longitudeScale_)};
  }

  Projection nearest(wr::GeoPoint point) const {
    const auto p = project(point);
    Projection best{std::numeric_limits<double>::infinity(), 0., 0, 0.};
    for (std::size_t i = 0; i < sections_.size(); ++i) {
      const auto& section = sections_[i];
      const auto dx = section.to.x - section.from.x, dy = section.to.y - section.from.y;
      const auto x = p.x - section.from.x, y = p.y - section.from.y;
      const auto f = std::clamp((x * dx + y * dy) / (dx * dx + dy * dy), 0., 1.);
      const auto offset = std::hypot(x - dx * f, y - dy * f);
      const auto remaining = section.after + (1. - f) * section.length;
      if (offset < best.offset || (offset == best.offset && remaining < best.remaining))
        best = {offset, remaining, i, f};
    }
    return best;
  }

public:
  ChannelGuide(const wr::RoutingRequest& request, const wr::LandAndBoundaryProvider& land)
      : origin_(request.start),
        longitudeScale_(60. * std::cos(
            (request.start.latitude + request.destination.latitude) * std::numbers::pi / 360.)) {
    constexpr double step = .26, padding = 34.;
    constexpr auto infinity = std::numeric_limits<double>::infinity();
    constexpr std::uint64_t queryLimit = 110000;
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds{950};
    const auto cancellation = request.cancellation.bounded(deadline, {});
    const auto end = project(request.destination);
    const int minX = int(std::floor((std::min(0., end.x) - padding) / step));
    const int minY = int(std::floor((std::min(0., end.y) - padding) / step));
    const int nx = int(std::ceil((std::max(0., end.x) + padding) / step)) - minX + 1;
    const int ny = int(std::ceil((std::max(0., end.y) + padding) / step)) - minY + 1;
    const auto index = [&](int x, int y) { return y * nx + x; };
    const auto point = [&](int i) {
      return position({step * (i % nx + minX), step * (i / nx + minY)});
    };
    const int start = index(-minX, -minY);
    const auto coordinate = [&](int i) {
      return XY{step * (i % nx + minX), step * (i / nx + minY)};
    };
    std::vector<double> costs(std::size_t(nx) * ny, infinity);
    std::vector<int> parents(costs.size(), -1);
    std::vector<unsigned char> water(costs.size(), 0);
    std::set<std::pair<int, int>> blocked;
    using Entry = std::tuple<double, double, int>;
    const auto margin = request.constraints.landSafetyMarginNm;
    const auto startMargin = land.segmentForbidden(origin_, origin_, margin) ? 0. : margin;
    const auto targetMargin = land.segmentForbidden(
        request.destination, request.destination, margin) ? 0. : margin;
    water[start] = 1;
    const auto clear = [&](int id) {
      if (!water[id] && queries_ < queryLimit) {
        ++queries_;
        const auto p = point(id);
        water[id] = land.segmentForbidden(p, p, margin) ? 2 : 1;
      }
      return water[id] == 1;
    };
    std::vector<wr::GeoPoint> raw;
    for (unsigned attempt = 0; attempt < 4 && !cancellation.cancelled(); ++attempt) {
      std::fill(costs.begin(), costs.end(), infinity);
      std::fill(parents.begin(), parents.end(), -1);
      std::priority_queue<Entry, std::vector<Entry>, std::greater<>> queue;
      costs[start] = 0.;
      queue.emplace(wr::distanceNm(origin_, request.destination), 0., start);
      int goal = -1;
      while (!queue.empty() && queries_ < queryLimit && !cancellation.cancelled()) {
        const auto [estimate, distance, at] = queue.top();
        queue.pop();
        if (distance > costs[at]) continue;
        const auto from = coordinate(at);
        if (std::hypot(from.x - end.x, from.y - end.y) <= .45) {
          ++queries_;
          if (!land.segmentForbidden(position(from), request.destination, targetMargin)) {
            goal = at;
            break;
          }
        }
        const int x = at % nx, y = at / nx;
        const auto tryNext = [&](int dx, int dy) {
          const int xx = x + dx, yy = y + dy;
          if (xx < 0 || xx >= nx || yy < 0 || yy >= ny || queries_ >= queryLimit)
            return;
          const int next = index(xx, yy);
          const auto nextCost = distance + step * (dx && dy ? std::numbers::sqrt2 : 1.);
          if (nextCost >= costs[next] || blocked.contains(std::minmax(at, next)) || !clear(next))
            return;
          if (dx && dy && (!clear(index(xx, y)) || !clear(index(x, yy)))) return;
          costs[next] = nextCost;
          parents[next] = at;
          const auto to = coordinate(next);
          queue.emplace(nextCost + 1.30 * std::hypot(to.x - end.x, to.y - end.y), nextCost, next);
        };
        for (int dy = -1; dy <= 1; ++dy)
          for (int dx = -1; dx <= 1; ++dx)
            if (dx || dy)
            tryNext(dx, dy);
      }
      if (goal < 0 || cancellation.cancelled()) return;
      // Check the short connectors lazily along the selected path. A narrow
      // strip of land can separate two individually clear vertices.
      raw = {request.destination};
      bool valid = true;
      for (int at = goal; at != -1; at = parents[at]) {
        raw.push_back(point(at));
        const auto parent = parents[at];
        if (parent < 0) continue;
        if (cancellation.cancelled() || queries_ >= queryLimit) return;
        ++queries_;
        const auto clearance = parent == start && wr::distanceNm(point(at), origin_) <= .4
            ? startMargin : margin;
        if (land.segmentForbidden(point(parent), point(at), clearance)) {
          blocked.emplace(std::minmax(parent, at));
          valid = false;
        }
      }
      if (valid) break;
      raw.clear();
    }
    if (raw.empty()) return;
    std::reverse(raw.begin(), raw.end());
    std::vector<wr::GeoPoint> simplified{raw.front()};
    for (std::size_t i = 0; i + 1 < raw.size();) {
      auto next = std::min(i + 32, raw.size() - 1);
      while (next > i + 1 && queries_ < queryLimit && !cancellation.cancelled()) {
        ++queries_;
        if (!land.segmentForbidden(raw[i], raw[next], margin)) break;
        --next;
      }
      if (cancellation.cancelled() || queries_ >= queryLimit) next = i + 1;
      simplified.push_back(raw[next]);
      i = next;
    }
    double after = 0.;
    for (auto i = simplified.size() - 1; i > 0; --i) {
      const auto from = project(simplified[i - 1]), to = project(simplified[i]);
      const auto length = std::hypot(to.x - from.x, to.y - from.y);
      if (length < 1e-8) continue;
      sections_.push_back({from, to, length, after});
      after += length;
    }
    std::reverse(sections_.begin(), sections_.end());
    length_ = after;
  }

  bool available() const { return !sections_.empty(); }
  std::uint64_t queries() const { return queries_; }
  double length() const { return length_; }

  double estimate(wr::GeoPoint point, double direct) const {
    if (!available()) return direct;
    const auto p = nearest(point);
    return std::max(direct, p.remaining + p.offset);
  }

  std::optional<double> heading(wr::GeoPoint point) const {
    if (!available()) return {};
    const auto p = nearest(point);
    if (p.offset > 2.) return {};
    auto section = p.section;
    double along = p.fraction * sections_[section].length + .65;
    while (along > sections_[section].length && section + 1 < sections_.size()) {
      along -= sections_[section].length;
      ++section;
    }
    const auto& segment = sections_[section];
    const auto f = std::min(1., along / segment.length);
    const auto target = position({std::lerp(segment.from.x, segment.to.x, f),
                                  std::lerp(segment.from.y, segment.to.y, f)});
    return wr::initialBearingDegrees(point, target);
  }
};
}  // namespace corridor_routing
