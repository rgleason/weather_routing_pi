// SPDX-License-Identifier: Apache-2.0
#pragma once
#include "Types.h"
#include <algorithm>
#include <chrono>
#include <utility>

namespace supercpn::weather_routing {
// Check demand/cadence BEFORE touching geometry. Only the latest immutable
// snapshot is needed by the UI. Observer failures cannot fail a calculation.
class RoutingPreviewPublisher {
 public:
  static constexpr std::size_t maximumFrontPoints = 256;
  static constexpr std::size_t maximumPathPoints = 256;
  explicit RoutingPreviewPublisher(const RoutingRequest& request,
      std::chrono::milliseconds interval = std::chrono::milliseconds{500})
      : request_(request), interval_(interval) {}
  template<class Factory> void publish(Factory&& factory) noexcept {
    if (!request_.preview) return;
    try {
      if (request_.previewWanted && !request_.previewWanted()) return;
      const auto now = std::chrono::steady_clock::now();
      if (published_ && now - last_ < interval_) return;
      last_ = now;
      published_ = true;
      auto value = std::make_shared<RoutingPreview>(factory());
      value->phase = request_.previewPhase;
      // Factories are bounded too; enforce the transport contract defensively.
      std::size_t remaining = maximumFrontPoints;
      for (auto& contour : value->contours) {
        if (contour.points.size() > remaining) contour.closed = false;
        contour.points.resize(std::min(contour.points.size(), remaining));
        remaining -= contour.points.size();
      }
      std::erase_if(value->contours, [](const auto& c) { return c.points.empty(); });
      value->frontier.resize(std::min(value->frontier.size(), remaining));
      if (value->path.size() > maximumPathPoints)
        value->path.erase(value->path.begin(), value->path.end() - maximumPathPoints);
      request_.preview(std::move(value));
    } catch (...) {
      // Rendering is optional, including under memory pressure.
    }
  }
 private:
  const RoutingRequest& request_;
  std::chrono::milliseconds interval_;
  bool published_{};
  std::chrono::steady_clock::time_point last_{};
};
inline RoutingPreview PreviewFromLayer(const IsochroneLayer& layer) {
  RoutingPreview preview;
  preview.time = layer.time;
  std::size_t remaining = RoutingPreviewPublisher::maximumFrontPoints;
  for (const auto& source : layer.contours) {
    if (!remaining) break;
    IsochroneContour contour;
    const auto count = std::min(remaining, source.points.size());
    contour.points.assign(source.points.begin(), source.points.begin() + count);
    contour.closed = source.closed && count == source.points.size();
    remaining -= count;
    preview.contours.push_back(std::move(contour));
  }
  if (!layer.traces.empty()) {
    const auto& path = layer.traces.front().route;
    const auto count = std::min(path.size(), RoutingPreviewPublisher::maximumPathPoints);
    preview.path.assign(path.end() - count, path.end());
  }
  return preview;
}
}  // namespace supercpn::weather_routing
