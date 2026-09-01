/***************************************************************************
 *   Copyright (C) 2026 by OpenCPN Weather Routing contributors             *
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 3 of the License, or     *
 *   (at your option) any later version.                                   *
 ***************************************************************************/

#ifndef _ROUTE_WAYPOINT_EXTRACTOR_H_
#define _ROUTE_WAYPOINT_EXTRACTOR_H_

#include <vector>

#include <wx/string.h>

struct RouteWaypointInfo {
  wxString guid;
  wxString name;
  double lat;
  double lon;
  int index;
};

bool ExtractOpenCPNRouteWaypoints(const wxString& routeGuid,
                                  std::vector<RouteWaypointInfo>& out,
                                  wxString& error);

#endif
