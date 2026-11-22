// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cmath>
#include <optional>
#include <string>

namespace weather_routing {

enum class EndpointSurface { Water, Land, Drying, Unsafe, Margin, Unavailable };

struct RoutingEndpointProbe {
  EndpointSurface surface{EndpointSurface::Unavailable};
  std::string source;
  std::string detail;
};

struct RoutingEndpointAssessment {
  RoutingEndpointProbe point;
  std::optional<RoutingEndpointProbe> buffered;

  bool Blocked() const {
    return point.surface != EndpointSurface::Water ||
           (buffered && buffered->surface == EndpointSurface::Unavailable);
  }
  bool WarnAboutMargin() const {
    return !Blocked() && buffered &&
           buffered->surface != EndpointSurface::Water;
  }
  const RoutingEndpointProbe& Evidence() const {
    return buffered && buffered->surface != EndpointSurface::Water
               ? *buffered : point;
  }
};

// The point and its clearance buffer answer different questions. Never reject
// a water endpoint as land merely because the buffered query touches land.
// This is independent of routing mode and does not change coastal entry/exit
// allowances or the search/final-validation predicates.
template<class Query>
RoutingEndpointAssessment AssessRoutingEndpoint(double margin_nm, Query query) {
  RoutingEndpointAssessment result;
  if (!std::isfinite(margin_nm) || margin_nm < 0.0) {
    result.point.detail = "Invalid shore margin";
    return result;
  }
  result.point = query(0.0);
  if (result.point.surface == EndpointSurface::Water && margin_nm > 0.0)
    result.buffered = query(margin_nm);
  return result;
}

}  // namespace weather_routing
