/***************************************************************************
 *   Copyright (C) 2026 by OpenCPN development team                       *
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 3 of the License, or     *
 *   (at your option) any later version.                                   *
 ***************************************************************************/

#ifndef _WEATHER_ROUTING_ROUTE_SIMPLIFIER_H_
#define _WEATHER_ROUTING_ROUTE_SIMPLIFIER_H_

#include <functional>
#include <vector>

#include <wx/string.h>

#include "RouteMap.h"

struct RouteSimplificationOptions {
  double max_cross_track_error_nm;
  double max_eta_penalty_minutes;
  double heading_change_degrees;
  double wind_direction_change_degrees;
  double wind_speed_change_knots;
  double current_direction_change_degrees;
  double current_speed_change_knots;

  RouteSimplificationOptions();
};

struct RouteSimplificationResult {
  bool success;
  std::vector<PlotData> points;
  int original_points;
  int simplified_points;
  double max_deviation_nm;
  double estimated_eta_change_seconds;
  int safety_checks;
  int feasibility_checks;
  wxString failure_reason;

  RouteSimplificationResult();
};

class RouteSimplifier {
public:
  typedef std::function<bool(const PlotData&, const PlotData&, wxString*)>
      SafetyCheck;
  typedef std::function<bool(const PlotData&, const PlotData&, double, double*,
                             wxString*)>
      FeasibilityCheck;

  static RouteSimplificationResult Simplify(
      const std::vector<PlotData>& points,
      const RouteSimplificationOptions& options,
      const SafetyCheck& safety_check,
      const FeasibilityCheck& feasibility_check);

  static double SegmentMaximumDeviationNm(const std::vector<PlotData>& points,
                                          size_t first, size_t last);
};

#endif
