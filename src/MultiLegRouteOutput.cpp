/***************************************************************************
 *   Copyright (C) 2026 by OpenCPN development team                       *
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 3 of the License, or     *
 *   (at your option) any later version.                                   *
 ***************************************************************************/

#include "MultiLegRouteOutput.h"

#include <algorithm>
#include <cmath>

#include <wx/intl.h>

namespace {

constexpr double kEarthRadiusNm = 3440.065;
constexpr double kPi = 3.14159265358979323846;

double DegreesToRadians(double value) { return value * kPi / 180.0; }

double NormalizeLongitudeDelta(double value) {
  while (value > 180.0) value -= 360.0;
  while (value < -180.0) value += 360.0;
  return value;
}

double DistanceNm(const PlotData& first, const PlotData& second) {
  const double lat1 = DegreesToRadians(first.lat);
  const double lat2 = DegreesToRadians(second.lat);
  const double dlat = lat2 - lat1;
  const double dlon =
      DegreesToRadians(NormalizeLongitudeDelta(second.lon - first.lon));
  const double sin_lat = std::sin(dlat / 2.0);
  const double sin_lon = std::sin(dlon / 2.0);
  const double a =
      sin_lat * sin_lat + std::cos(lat1) * std::cos(lat2) * sin_lon * sin_lon;
  return 2.0 * kEarthRadiusNm *
         std::asin(std::sqrt(std::min(1.0, std::max(0.0, a))));
}

bool SameWaypoint(const PlotData& first, const PlotData& second) {
  return std::fabs(first.lat - second.lat) < 1e-8 &&
         std::fabs(NormalizeLongitudeDelta(first.lon - second.lon)) < 1e-8;
}

}  // namespace

MultiLegRouteOutputResult MultiLegRouteOutput::Assemble(
    const std::vector<MultiLegRouteOutputLeg>& input,
    double join_tolerance_nm) {
  MultiLegRouteOutputResult result;
  if (input.empty()) {
    result.failure_reason = _("The multi-waypoint passage has no route legs.");
    return result;
  }
  if (!std::isfinite(join_tolerance_nm) || join_tolerance_nm < 0.0) {
    result.failure_reason = _("The route-leg join tolerance is invalid.");
    return result;
  }

  std::vector<MultiLegRouteOutputLeg> legs = input;
  std::sort(legs.begin(), legs.end(),
            [](const MultiLegRouteOutputLeg& first,
               const MultiLegRouteOutputLeg& second) {
              return first.index < second.index;
            });

  const int expected_count = legs.front().count;
  if (expected_count <= 0 || static_cast<int>(legs.size()) != expected_count) {
    result.failure_reason = wxString::Format(
        _("The multi-waypoint passage is incomplete: expected %d legs but "
          "found %lu."),
        expected_count, static_cast<unsigned long>(legs.size()));
    return result;
  }

  for (size_t i = 0; i < legs.size(); ++i) {
    const int expected_index = static_cast<int>(i + 1);
    if (legs[i].count != expected_count || legs[i].index != expected_index) {
      result.failure_reason = wxString::Format(
          _("The multi-waypoint passage has missing, duplicate, or "
            "inconsistent leg numbering near leg %d."),
          expected_index);
      return result;
    }
    if (legs[i].points.size() < 2) {
      result.failure_reason = wxString::Format(
          _("Leg %d of the multi-waypoint passage has no complete route to "
            "export."),
          expected_index);
      return result;
    }

    size_t first_point = 0;
    if (!result.points.empty()) {
      const double join_distance =
          DistanceNm(result.points.back(), legs[i].points.front());
      if (join_distance > join_tolerance_nm) {
        result.failure_reason = wxString::Format(
            _("Legs %d and %d do not meet at the same waypoint (gap %.3f "
              "NM)."),
            expected_index - 1, expected_index, join_distance);
        result.points.clear();
        return result;
      }
      if (SameWaypoint(result.points.back(), legs[i].points.front()))
        first_point = 1;
    }
    result.points.insert(result.points.end(),
                         legs[i].points.begin() + first_point,
                         legs[i].points.end());
  }

  result.success = result.points.size() >= 2;
  if (!result.success)
    result.failure_reason =
        _("The multi-waypoint passage produced too few route points.");
  return result;
}
