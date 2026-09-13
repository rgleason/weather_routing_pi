/***************************************************************************
 *   Copyright (C) 2026 by OpenCPN Weather Routing contributors             *
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 3 of the License, or     *
 *   (at your option) any later version.                                   *
 ***************************************************************************/

#include "RouteWaypointExtractor.h"

#include <memory>

#include <wx/log.h>

#include "ocpn_plugin.h"

bool ExtractOpenCPNRouteWaypoints(const wxString& routeGuid,
                                  std::vector<RouteWaypointInfo>& out,
                                  wxString& error) {
  out.clear();
  error.clear();

  if (routeGuid.IsEmpty()) {
    error = _("No OpenCPN route is selected.");
    return false;
  }

  std::unique_ptr<PlugIn_Route> route = GetRoute_Plugin(routeGuid);
  if (!route) {
    error = wxString::Format(_("OpenCPN route not found: %s"), routeGuid);
    return false;
  }

  if (!route->pWaypointList) {
    error = wxString::Format(_("OpenCPN route has no waypoint list: %s"),
                             routeGuid);
    return false;
  }

  int index = 1;
  for (wxPlugin_WaypointListNode* node = route->pWaypointList->GetFirst(); node;
       node = node->GetNext()) {
    PlugIn_Waypoint* waypoint = node->GetData();
    if (!waypoint) {
      wxLogMessage(
          "WeatherRouting multi-leg route extraction: route=%s skipped null "
          "waypoint at index=%d",
          routeGuid, index);
      ++index;
      continue;
    }

    RouteWaypointInfo info;
    info.guid = waypoint->m_GUID;
    info.name = waypoint->m_MarkName;
    info.lat = waypoint->m_lat;
    info.lon = waypoint->m_lon;
    info.index = index;
    out.push_back(info);
    ++index;
  }

  if (out.size() < 2) {
    error = wxString::Format(
        _("OpenCPN route must contain at least two usable waypoints: %s"),
        routeGuid);
    out.clear();
    return false;
  }

  return true;
}
