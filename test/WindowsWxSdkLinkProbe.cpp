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
  // The SDK owns construction of its legacy list types. Construct the same
  // host route/track objects used by output code, rather than a new local
  // wx 3.2 list whose non-exported virtual node helpers belong to the host.
  PlugIn_Track track;
  PlugIn_Route_ExV2 route;
  track.pWaypointList->Append(new PlugIn_Waypoint);
  route.pWaypointList->Append(new PlugIn_Waypoint_ExV2);
  track.pWaypointList->DeleteContents(true);
  track.pWaypointList->Clear();
  route.pWaypointList->DeleteContents(true);
  route.pWaypointList->Clear();
  return choices.GetCount() + guids.GetCount();
}
