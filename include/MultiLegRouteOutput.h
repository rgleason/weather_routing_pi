/***************************************************************************
 *   Copyright (C) 2026 by OpenCPN development team                       *
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 3 of the License, or     *
 *   (at your option) any later version.                                   *
 ***************************************************************************/

#ifndef _WEATHER_ROUTING_MULTI_LEG_ROUTE_OUTPUT_H_
#define _WEATHER_ROUTING_MULTI_LEG_ROUTE_OUTPUT_H_

#include <vector>

#include <wx/wx.h>

#include "RouteMap.h"

struct MultiLegRouteOutputLeg {
  int index = 0;
  int count = 0;
  std::vector<PlotData> points;
};

struct MultiLegRouteOutputResult {
  bool success = false;
  std::vector<PlotData> points;
  wxString failure_reason;
};

class MultiLegRouteOutput {
public:
  /** Assemble independently computed legs into one ordered passage. */
  static MultiLegRouteOutputResult Assemble(
      const std::vector<MultiLegRouteOutputLeg>& legs,
      double join_tolerance_nm = 0.05);
};

#endif
