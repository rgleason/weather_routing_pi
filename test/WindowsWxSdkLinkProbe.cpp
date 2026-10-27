// SPDX-License-Identifier: GPL-3.0-or-later
// Link a small DLL against the real host/wx SDK, without OpenCPN API mocks.
// In particular an executable unit-test target does not expose MSVC's
// imported STL base-class behaviour across different translation units.
#include "TimeZoneDisplay.h"
#include "ocpn_plugin.h"
#include <wx/choice.h>

extern "C" __declspec(dllexport) size_t WrWxSdkLinkProbe(wxWindow* parent) {
  const auto zones = marine_time::AvailableTimeZones();
  const auto guids = GetWaypointGUIDArray();
  wxArrayString choices;
  for (size_t i = 0; i < zones.GetCount(); ++i) choices.Add(zones[i]);
  wxChoice choice(parent, wxID_ANY, wxDefaultPosition, wxDefaultSize, choices);
  Plugin_WaypointList waypoints;
  Plugin_WaypointExV2List extendedWaypoints;
  waypoints.DeleteContents(true);
  extendedWaypoints.DeleteContents(true);
  waypoints.Append(new PlugIn_Waypoint);
  extendedWaypoints.Append(new PlugIn_Waypoint_ExV2);
  return choices.GetCount() + guids.GetCount();
}
