// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "ChannelGuide.h"
#include "supercpn/weather_routing/Providers.h"
#include <algorithm>
#include <cmath>
#include <limits>
#include <numbers>
#include <queue>

namespace corridor_routing {
namespace wr = supercpn::weather_routing;

// A reverse geographic distance field orders checked sailing moves. Its
// vertices do not become waypoints and its extent never limits sailing.
class Geography {
  static constexpr double radians = std::numbers::pi / 180.;
  static constexpr double infinity = std::numeric_limits<double>::infinity();

  struct Grid {
    int length, width, startX, finishX;
    double alongStep, lateralStep;
    std::vector<double> distance;
    bool connected() const { return std::isfinite(distance[index(startX, width / 2)]); }
    int index(int x, int y) const { return y * length + x; }
  };

  wr::GeoPoint start_, finish_;
  double bearing_;
  std::optional<Grid> coarse_, fine_;
  std::optional<ChannelGuide> channel_;
  bool wide_{};
  std::uint64_t queries_{};

  std::vector<double> endpointLinks(const wr::RoutingRequest& request,
      const wr::LandAndBoundaryProvider& land, wr::GeoPoint endpoint,
      const std::vector<wr::GeoPoint>& points) {
    const auto margin = request.constraints.landSafetyMarginNm;
    const auto firstMargin = land.segmentForbidden(endpoint, endpoint, margin) ? 0. : margin;
    std::vector<std::pair<wr::GeoPoint, double>> anchors;
    for (unsigned bearing = 0; bearing < 360 && !request.cancellation.cancelled(); bearing += 15) {
      for (const auto distance : {.25, .5, 1., 1.5}) {
        const auto point = wr::destinationPoint(endpoint, bearing, distance);
        ++queries_;
        if (land.segmentForbidden(endpoint, point, firstMargin)) break;
        anchors.emplace_back(point, distance);
      }
    }
    std::vector<double> links(points.size(), infinity);
    const auto cap = queries_ + 2000;
    for (std::size_t i = 0; i < points.size() && queries_ < cap &&
         !request.cancellation.cancelled(); ++i) {
      const auto point = points[i];
      const auto direct = wr::distanceNm(endpoint, point);
      if (direct > 4.5) continue;
      ++queries_;
      if (!land.segmentForbidden(endpoint, point, firstMargin)) {
        links[i] = direct;
        continue;
      }
      for (const auto& [anchor, firstLeg] : anchors) {
        const auto secondLeg = wr::distanceNm(anchor, point);
        if (firstLeg + secondLeg >= links[i] || secondLeg > 3.5 || queries_ >= cap) continue;
        ++queries_;
        if (!land.segmentForbidden(anchor, point, margin)) links[i] = firstLeg + secondLeg;
      }
    }
    return links;
  }

  Grid build(const wr::RoutingRequest& request, const wr::LandAndBoundaryProvider& land,
             int divisions, int padding, int width, double lateralStep,
             std::uint64_t queryLimit, bool linkEndpoints = false) {
    Grid grid{divisions + 2 * padding + 1, width, padding, padding + divisions,
              wr::distanceNm(start_, finish_) / divisions, lateralStep, {}};
    grid.distance.assign(grid.length * grid.width, infinity);
    std::vector<wr::GeoPoint> points(grid.distance.size());
    for (int x = 0; x < grid.length; ++x) {
      const auto along = wr::destinationPoint(start_, bearing_, (x - padding) * grid.alongStep);
      const auto forward = x < grid.finishX ? wr::initialBearingDegrees(along, finish_) :
          wr::initialBearingDegrees(along, start_) + 180.;
      for (int y = 0; y < width; ++y)
        points[grid.index(x, y)] = wr::destinationPoint(along, forward + 90.,
                                                       (y - width / 2) * lateralStep);
    }
    const auto source = grid.index(grid.startX, width / 2);
    const auto target = grid.index(grid.finishX, width / 2);
    points[source] = start_;
    points[target] = finish_;
    grid.distance[target] = 0.;
    using Entry = std::pair<double, int>;
    std::priority_queue<Entry, std::vector<Entry>, std::greater<>> queue;
    queue.emplace(0., target);
    std::vector<double> sourceLinks;
    if (linkEndpoints) {
      const auto destinationLinks = endpointLinks(request, land, finish_, points);
      for (std::size_t i = 0; i < points.size(); ++i) {
        if (destinationLinks[i] >= grid.distance[i]) continue;
        grid.distance[i] = destinationLinks[i];
        queue.emplace(destinationLinks[i], i);
      }
      sourceLinks = endpointLinks(request, land, start_, points);
    }
    const auto margin = request.constraints.landSafetyMarginNm;
    const bool bufferedTarget = margin > 0 && land.segmentForbidden(finish_, finish_, margin);
    const bool bufferedSource = margin > 0 && land.segmentForbidden(start_, start_, margin);
    const auto stopAfter = queries_ + queryLimit;
    while (!queue.empty() && queries_ < stopAfter && !request.cancellation.cancelled()) {
      const auto [cost, current] = queue.top();
      queue.pop();
      if (cost > grid.distance[current]) continue;
      if (linkEndpoints && cost + sourceLinks[current] < grid.distance[source]) {
        grid.distance[source] = cost + sourceLinks[current];
        queue.emplace(grid.distance[source], source);
      }
      const auto x = current % grid.length, y = current / grid.length;
      for (int nextY = std::max(y - 1, 0); nextY <= std::min(y + 1, width - 1); ++nextY) {
        for (int nextX = std::max(x - 1, 0); nextX <= std::min(x + 1, grid.length - 1); ++nextX) {
          const auto next = grid.index(nextX, nextY);
          const auto leg = wr::distanceNm(points[current], points[next]);
          const auto candidate = cost + leg;
          if (candidate >= grid.distance[next]) continue;
          const bool endpoint = (bufferedTarget && (current == target || next == target)) ||
              (bufferedSource && (current == source || next == source));
          ++queries_;
          if (land.segmentForbidden(points[current], points[next], endpoint ? 0. : margin))
            continue;
          grid.distance[next] = candidate;
          queue.emplace(candidate, next);
        }
      }
    }
    return grid;
  }

public:
  Geography(const wr::RoutingRequest& request, const wr::LandAndBoundaryProvider* land,
            bool refine, bool discoverChannels)
      : start_(request.start), finish_(request.destination),
        bearing_(wr::initialBearingDegrees(start_, finish_)) {
    const auto direct = wr::distanceNm(start_, finish_);
    const auto margin = request.constraints.landSafetyMarginNm;
    if (!land || direct < 0.2 || request.cancellation.cancelled() ||
        !land->segmentForbidden(start_, finish_, margin))
      return;

    const auto across = std::clamp(direct * 0.75 / 20., 0.25, 20.);
    auto coarse = build(request, *land, 20, 5, 25, across, 5000);
    if (coarse.connected()) coarse_ = std::move(coarse);

    // Narrow channels can vanish completely in the coarse field. Use the
    // same region at finer spacing, with a separate, bounded query allowance.
    if (refine && direct <= 105. && !request.cancellation.cancelled()) {
      const auto divisions = std::clamp(int(std::ceil(direct / 0.55)), 36, 100);
      const auto acrossStep = std::max(across / 3., 0.14);
      const auto width = std::clamp(int(std::ceil(24 * across / acrossStep)) | 1, 27, 73);
      auto fine = build(request, *land, divisions, divisions / 4, width, acrossStep, 24000);
      if (fine.connected()) fine_ = std::move(fine);
    }
    // Nearby endpoints can lie across a whole island or peninsula. Widen
    // the geographic suggestion once if neither local mesh connects them.
    if (refine && !coarse_ && !fine_ && direct <= 105. && !request.cancellation.cancelled()) {
      const int divisions = std::clamp(int(std::ceil(direct / 1.5)), 16, 50);
      const auto padding = std::clamp(int(std::ceil(27. * divisions / direct)), 6, 44);
      auto wide = build(request, *land, divisions, padding, 41, 2.5, 16000);
      if (!wide.connected() && !request.cancellation.cancelled())
        wide = build(request, *land, divisions, padding, 41, 2.5, 16000, true);
      if (wide.connected()) {
        fine_ = std::move(wide);
        wide_ = true;
      }
    }
    if (refine && discoverChannels && !coarse_ && !fine_ && direct <= 105. &&
        !request.cancellation.cancelled())
      channel_.emplace(request, *land);
  }

  double estimate(wr::GeoPoint point, double direct, bool preferFine = false) const {
    if (preferFine && channelAvailable()) return channel_->estimate(point, direct);
    const auto& chosen = preferFine && fine_ ? fine_ : coarse_;
    if (!chosen) return direct;
    const auto& grid = *chosen;
    const auto angle = (wr::initialBearingDegrees(start_, point) - bearing_) * radians;
    const auto distance = wr::distanceNm(start_, point);
    const auto x = grid.startX + distance * std::cos(angle) / grid.alongStep;
    const auto y = grid.width / 2 + distance * std::sin(angle) / grid.lateralStep;
    const auto ix = static_cast<int>(std::floor(x)), iy = static_cast<int>(std::floor(y));
    if (ix < 0 || ix >= grid.length - 1 || iy < 0 || iy >= grid.width - 1)
      return direct + 2 * grid.alongStep;
    double estimate = infinity;
    for (int row = iy; row <= iy + 1; ++row)
      for (int column = ix; column <= ix + 1; ++column)
        estimate = std::min(estimate, grid.distance[grid.index(column, row)] +
            std::hypot((x - column) * grid.alongStep, (y - row) * grid.lateralStep));
    return std::isfinite(estimate) ? std::max(direct, estimate) : direct + 4 * grid.alongStep;
  }

  bool available() const { return bool(coarse_) || refined(); }
  bool refined() const { return bool(fine_) || channelAvailable(); }
  bool wide() const { return wide_; }
  bool channelAvailable() const { return channel_ && channel_->available(); }
  std::uint64_t channelQueries() const { return channel_ ? channel_->queries() : 0; }
  double channelLength() const { return channelAvailable() ? channel_->length() : 0.; }
  std::optional<double> channelHeading(wr::GeoPoint point) const {
    return channelAvailable() ? channel_->heading(point) : std::nullopt;
  }
  std::uint64_t queries() const { return queries_ + channelQueries(); }
};
}  // namespace corridor_routing
