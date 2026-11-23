// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "GeographyGuide.h"

namespace meridian_routing {
// Fine endpoint access and a coarser offshore sketch supply a continuous
// ordering potential over a long passage. None of these points is compulsory
// or a substitute for weather-dependent motion and final validation.
class HierarchicalGuide {
  GeographyGuide guide_;
  static std::vector<wr::GeoPoint> path(wr::RoutingRequest request,
      const wr::LandAndBoundaryProvider& geography, bool coarse = false) {
    if (request.cancellation.cancelled()) return {};
    if (!geography.segmentForbidden(request.start, request.destination,
                                      request.constraints.landSafetyMarginNm))
      return {request.start, request.destination};
    return GeographyGuide(request, &geography, INFINITY, coarse).path();
  }
  static std::vector<wr::GeoPoint> access(wr::RoutingRequest request,
      const wr::LandAndBoundaryProvider& geography) {
    const auto bearing = wr::initialBearingDegrees(request.start, request.destination);
    for (double offset : {0., 30., -30., 60., -60., 90., -90.}) {
      if (request.cancellation.cancelled()) break;
      request.destination = wr::destinationPoint(request.start, bearing + offset, 20);
      if (geography.segmentForbidden(request.destination, request.destination,
                                       request.constraints.landSafetyMarginNm)) continue;
      auto points = path(request, geography);
      if (!points.empty()) return points;
    }
    return {};
  }
public:
  HierarchicalGuide(const wr::RoutingRequest& request,
                    const wr::LandAndBoundaryProvider* geography)
      : guide_(request, nullptr) {
    if (!geography) return;
    if (wr::distanceNm(request.start, request.destination) > 160 &&
        geography->segmentForbidden(request.start, request.destination,
                                       request.constraints.landSafetyMarginNm)) {
      auto departure = access(request, *geography);
      auto reverse = request;
      std::swap(reverse.start, reverse.destination);
      auto arrival = access(reverse, *geography);
      if (!departure.empty() && !arrival.empty()) {
        auto middleRequest = request;
        middleRequest.start = departure.back();
        middleRequest.destination = arrival.back();
        auto middle = path(middleRequest, *geography, true);
        if (!middle.empty()) {
          departure.insert(departure.end(), middle.begin() + 1, middle.end());
          std::reverse(arrival.begin(), arrival.end());
          departure.insert(departure.end(), arrival.begin() + 1, arrival.end());
          guide_.adoptPath(std::move(departure));
          return;
        }
      }
    }
    // A medium passage needs guidance to its actual water destination.
    // A point 60 NM along the direct bearing can be inland or inside a shore
    // buffer/estuary that the real passage never needs to enter. Failure to
    // reach that artificial target erased the detour guide and repeatedly
    // favoured moves back toward the blocking land.
    const auto reach = wr::distanceNm(request.start, request.destination) <= 160
        ? double(INFINITY) : 60.;
    guide_ = GeographyGuide(request, geography, reach);
  }
  double estimate(wr::GeoPoint point) const { return guide_.estimate(point); }
  const std::vector<wr::GeoPoint>& path() const { return guide_.path(); }
};
}  // namespace meridian_routing
