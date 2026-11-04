// SPDX-License-Identifier: GPL-3.0-or-later
#include "ocpn_plugin.h"

#if defined(__WXMSW__) && wxUSE_STD_CONTAINERS
// With STL-backed wx lists, inline Clear/destruction references these helpers.
// The API declares the list types without exporting their helpers from the
// Windows host, so plugin-owned lists need their definitions in this DLL.
#include <wx/listimpl.cpp>
WX_DEFINE_LIST(Plugin_WaypointList)
WX_DEFINE_LIST(Plugin_WaypointExV2List)
#endif
