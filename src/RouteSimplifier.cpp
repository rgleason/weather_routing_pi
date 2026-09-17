/***************************************************************************
 *   Copyright (C) 2026 by OpenCPN development team                       *
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 3 of the License, or     *
 *   (at your option) any later version.                                   *
 ***************************************************************************/

#include <wx/wx.h>

#include "RouteSimplifier.h"

#include <algorithm>
#include <cmath>

namespace {

double AngleDifference(double a, double b) {
  if (!std::isfinite(a) || !std::isfinite(b)) return 0.0;
  double difference = std::fmod(std::fabs(a - b), 360.0);
  return difference > 180.0 ? 360.0 - difference : difference;
}

bool Changed(double a, double b, double threshold) {
  return std::isfinite(a) && std::isfinite(b) && std::fabs(a - b) >= threshold;
}

double ToRadians(double degrees) { return degrees * M_PI / 180.0; }

double AngularDistance(double lat1, double lon1, double lat2, double lon2) {
  const double dlat = ToRadians(lat2 - lat1);
  const double dlon = ToRadians(lon2 - lon1);
  const double a = std::sin(dlat / 2.0) * std::sin(dlat / 2.0) +
                   std::cos(ToRadians(lat1)) * std::cos(ToRadians(lat2)) *
                       std::sin(dlon / 2.0) * std::sin(dlon / 2.0);
  return 2.0 * std::atan2(std::sqrt(a), std::sqrt(std::max(0.0, 1.0 - a)));
}

double InitialBearing(double lat1, double lon1, double lat2, double lon2) {
  const double phi1 = ToRadians(lat1);
  const double phi2 = ToRadians(lat2);
  const double dlon = ToRadians(lon2 - lon1);
  return std::atan2(std::sin(dlon) * std::cos(phi2),
                    std::cos(phi1) * std::sin(phi2) -
                        std::sin(phi1) * std::cos(phi2) * std::cos(dlon));
}

double GreatCircleSegmentDistanceNm(const PlotData& start, const PlotData& end,
                                    const PlotData& point) {
  static const double earth_radius_nm = 3440.065;
  const double segment_distance =
      AngularDistance(start.lat, start.lon, end.lat, end.lon);
  if (segment_distance < 1e-12)
    return earth_radius_nm *
           AngularDistance(start.lat, start.lon, point.lat, point.lon);

  const double point_distance =
      AngularDistance(start.lat, start.lon, point.lat, point.lon);
  const double bearing_to_point =
      InitialBearing(start.lat, start.lon, point.lat, point.lon);
  const double segment_bearing =
      InitialBearing(start.lat, start.lon, end.lat, end.lon);
  const double cross_track = std::asin(std::max(
      -1.0, std::min(1.0, std::sin(point_distance) *
                              std::sin(bearing_to_point - segment_bearing))));
  const double cosine_cross_track = std::cos(cross_track);
  double along_track = 0.0;
  if (std::fabs(cosine_cross_track) > 1e-12) {
    const double ratio = std::cos(point_distance) / cosine_cross_track;
    along_track = std::acos(std::max(-1.0, std::min(1.0, ratio)));
  }
  if (std::cos(bearing_to_point - segment_bearing) < 0.0)
    along_track = -along_track;

  if (along_track < 0.0) return earth_radius_nm * point_distance;
  if (along_track > segment_distance)
    return earth_radius_nm *
           AngularDistance(end.lat, end.lon, point.lat, point.lon);
  return earth_radius_nm * std::fabs(cross_track);
}

std::vector<bool> MandatoryPoints(const std::vector<PlotData>& points,
                                  const RouteSimplificationOptions& options) {
  std::vector<bool> mandatory(points.size(), false);
  if (points.empty()) return mandatory;
  mandatory.front() = true;
  mandatory.back() = true;

  for (size_t i = 1; i + 1 < points.size(); ++i) {
    const PlotData& previous = points[i - 1];
    const PlotData& point = points[i];
    const PlotData& next = points[i + 1];

    const bool manoeuvre =
        point.tacks != previous.tacks || point.jibes != previous.jibes ||
        point.sail_plan_changes != previous.sail_plan_changes ||
        point.polar != previous.polar;
    const bool heading_transition =
        AngleDifference(point.cog, next.cog) >= options.heading_change_degrees;
    const bool wind_transition =
        AngleDifference(previous.twdOverWater, point.twdOverWater) >=
            options.wind_direction_change_degrees ||
        Changed(previous.twsOverWater, point.twsOverWater,
                options.wind_speed_change_knots);
    const bool current_transition =
        AngleDifference(previous.currentDir, point.currentDir) >=
            options.current_direction_change_degrees ||
        Changed(previous.currentSpeed, point.currentSpeed,
                options.current_speed_change_knots);
    const bool deficient_transition =
        point.grib_is_data_deficient != previous.grib_is_data_deficient;

    mandatory[i] = manoeuvre || heading_transition || wind_transition ||
                   current_transition || deficient_transition;
  }
  return mandatory;
}

bool SkipsMandatoryPoint(const std::vector<bool>& mandatory, size_t first,
                         size_t last) {
  for (size_t i = first + 1; i < last; ++i)
    if (mandatory[i]) return true;
  return false;
}

double OriginalElapsedSeconds(const PlotData& first, const PlotData& last) {
  if (!first.time.IsValid() || !last.time.IsValid() || last.time <= first.time)
    return 0.0;
  return (last.time - first.time).GetSeconds().ToDouble();
}

}  // namespace

RouteSimplificationOptions::RouteSimplificationOptions()
    : max_cross_track_error_nm(0.10),
      max_eta_penalty_minutes(5.0),
      heading_change_degrees(25.0),
      wind_direction_change_degrees(20.0),
      wind_speed_change_knots(3.0),
      current_direction_change_degrees(30.0),
      current_speed_change_knots(0.5) {}

RouteSimplificationResult::RouteSimplificationResult()
    : success(false),
      original_points(0),
      simplified_points(0),
      max_deviation_nm(0.0),
      estimated_eta_change_seconds(0.0),
      safety_checks(0),
      feasibility_checks(0) {}

double RouteSimplifier::SegmentMaximumDeviationNm(
    const std::vector<PlotData>& points, size_t first, size_t last) {
  if (last <= first + 1 || last >= points.size()) return 0.0;

  double maximum = 0.0;

  for (size_t i = first + 1; i < last; ++i)
    maximum = std::max(maximum, GreatCircleSegmentDistanceNm(
                                    points[first], points[last], points[i]));
  return maximum;
}

RouteSimplificationResult RouteSimplifier::Simplify(
    const std::vector<PlotData>& points,
    const RouteSimplificationOptions& options, const SafetyCheck& safety_check,
    const FeasibilityCheck& feasibility_check) {
  RouteSimplificationResult result;
  result.original_points = static_cast<int>(points.size());
  if (points.size() < 2) {
    result.failure_reason = _("Route has too few points to simplify");
    return result;
  }

  const std::vector<bool> mandatory = MandatoryPoints(points, options);
  const double total_seconds =
      std::max(1.0, OriginalElapsedSeconds(points.front(), points.back()));
  const double total_penalty_seconds =
      std::max(0.0, options.max_eta_penalty_minutes * 60.0);

  result.points.push_back(points.front());
  size_t first = 0;
  while (first + 1 < points.size()) {
    size_t limit = points.size() - 1;
    for (size_t i = first + 1; i < points.size() - 1; ++i) {
      if (mandatory[i]) {
        limit = i;
        break;
      }
    }

    size_t accepted = first + 1;
    double accepted_deviation = 0.0;
    double accepted_eta_change = 0.0;
    for (size_t candidate = limit; candidate > first; --candidate) {
      if (candidate == first + 1) {
        accepted = candidate;
        break;
      }
      if (SkipsMandatoryPoint(mandatory, first, candidate)) continue;

      const double deviation =
          SegmentMaximumDeviationNm(points, first, candidate);
      if (deviation > options.max_cross_track_error_nm) continue;

      wxString reason;
      ++result.safety_checks;
      if (safety_check &&
          !safety_check(points[first], points[candidate], &reason))
        continue;

      const double original_seconds =
          OriginalElapsedSeconds(points[first], points[candidate]);
      const double allocated_penalty =
          total_penalty_seconds * original_seconds / total_seconds;
      double shortcut_eta_change = 0.0;
      ++result.feasibility_checks;
      if (feasibility_check &&
          !feasibility_check(points[first], points[candidate],
                             allocated_penalty, &shortcut_eta_change, &reason))
        continue;

      accepted = candidate;
      accepted_deviation = deviation;
      accepted_eta_change = shortcut_eta_change;
      break;
    }

    result.points.push_back(points[accepted]);
    result.max_deviation_nm =
        std::max(result.max_deviation_nm, accepted_deviation);
    result.estimated_eta_change_seconds += accepted_eta_change;
    first = accepted;
  }

  result.success = true;
  result.simplified_points = static_cast<int>(result.points.size());
  return result;
}
