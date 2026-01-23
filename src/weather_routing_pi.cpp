/***************************************************************************
 *
 * Project:  OpenCPN Weather Routing plugin
 * Author:   Sean D'Epagnier
 *
 ***************************************************************************
 *   Copyright (C) 2016 by Sean D'Epagnier                                 *
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 3 of the License, or     *
 *   (at your option) any later version.                                   *
 *                                                                         *
 *   This program is distributed in the hope that it will be useful,       *
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of        *
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the         *
 *   GNU General Public License for more details.                          *
 *                                                                         *
 *   You should have received a copy of the GNU General Public License     *
 *   along with this program; if not, write to the                         *
 *   Free Software Foundation, Inc.,                                       *
 *   51 Franklin Street, Fifth Floor, Boston, MA 02110-1301,  USA.         *
 ***************************************************************************
 */

#include <wx/wx.h>
#include <wx/stdpaths.h>
#include <wx/treectrl.h>
#include <wx/fileconf.h>

#include <sstream>

#include "Utilities.h"
#include "Boat.h"
#include "RoutePoint.h"
#include "RouteMap.h"
#include "RouteMapOverlay.h"
#include "WeatherRouting.h"
#include "weather_routing_pi.h"

Json::Value g_ReceivedJSONMsg;
wxString g_ReceivedMessage;

// Define minimum and maximum versions of the grib plugin supported
#define GRIB_MAX_MAJOR 5
#define GRIB_MAX_MINOR 0
#define GRIB_MIN_MAJOR 4
#define GRIB_MIN_MINOR 1

// Define minimum and maximum versions of the climatology plugin supported
#define CLIMATOLOGY_MAX_MAJOR 1
#define CLIMATOLOGY_MAX_MINOR 6
#define CLIMATOLOGY_MIN_MAJOR 0
#define CLIMATOLOGY_MIN_MINOR 10

static Json::Value g_ReceivedODVersionJSONMsg;
static bool ODVersionNewerThan(int major, int minor, int patch) {
  Json::Value jMsg;
  Json::FastWriter writer;
  jMsg["Source"] = "WEATHER_ROUTING_PI";
  jMsg["Type"] = "Request";
  jMsg["Msg"] = "Version";
  jMsg["MsgId"] = "version";
  SendPluginMessage(wxS("OCPN_DRAW_PI"), writer.write(jMsg));

  if (g_ReceivedODVersionJSONMsg.size() <= 0) return false;
  if (g_ReceivedODVersionJSONMsg["Major"].asInt() > major) return true;
  if (g_ReceivedODVersionJSONMsg["Major"].asInt() == major &&
      g_ReceivedODVersionJSONMsg["Minor"].asInt() > minor)
    return true;
  if (g_ReceivedODVersionJSONMsg["Major"].asInt() == major &&
      g_ReceivedODVersionJSONMsg["Minor"].asInt() == minor &&
      g_ReceivedODVersionJSONMsg["Patch"].asInt() >= patch)
    return true;
  return false;
}

extern "C" DECL_EXP opencpn_plugin* create_pi(void* ppimgr) {
  return new weather_routing_pi(ppimgr);
}

extern "C" DECL_EXP void destroy_pi(opencpn_plugin* p) { delete p; }

#include "icons.h"

/**
 * @brief Constructs the Weather Routing plugin instance.
 *
 * @param ppimgr Pointer to the plugin manager interface.
 *
 * @details On Windows, initializes a 5-second timer for continuous address
 * space monitoring. The monitor runs independently of the Settings dialog and
 * alerts users when memory usage exceeds the configured threshold.
 */
weather_routing_pi::weather_routing_pi(void* ppimgr)
    : opencpn_plugin_118(ppimgr) {

#ifdef __WXMSW__
  m_addressSpaceMonitor = std::make_unique<AddressSpaceMonitor>();

  // Force-disable saved settings
  wxConfigBase* c = wxConfig::Get();
  if (c) {
    c->Write("/PlugIns/WeatherRouting/AddressSpaceAutoStopEnabled", false);
    c->Write("/PlugIns/WeatherRouting/AddressSpaceAlertEnabled", false);
    c->Flush();
  }

   // ---- INSERT THIS ----
  m_addressSpaceMonitor->SetAlertEnabled(false);
  m_addressSpaceMonitor->SetAutoStopEnabled(false);
  // ---- END INSERT ----
#endif


  // Create the PlugIn icons
  initialize_images();

  // Create the PlugIn icons  -from shipdriver
  // loads png file for the listing panel icon
  wxFileName fn;
  auto path = GetPluginDataDir("weather_routing_pi");
  fn.SetPath(path);
  fn.AppendDir("data");
  fn.SetFullName("weather_routing_panel.png");

  path = fn.GetFullPath();

  wxInitAllImageHandlers();

  wxLogDebug(wxString("Using icon path: ") + path);
  if (!wxImage::CanRead(path)) {
    wxLogDebug("Initiating image handlers.");
    wxInitAllImageHandlers();
  }
  wxImage panelIcon(path);
  if (panelIcon.IsOk())
    m_panelBitmap = wxBitmap(panelIcon);
  else
    wxLogWarning("Weather_Routing Navigation Panel icon has NOT been loaded");

  b_in_boundary_reply = false;
  m_tCursorLatLon.Connect(
      wxEVT_TIMER, wxTimerEventHandler(weather_routing_pi::OnCursorLatLonTimer),
      NULL, this);
  m_pWeather_Routing = NULL;

#ifdef __WXMSW__
  // ========== Windows-Only: Address Space Monitoring Initialization ==========

  // Start continuous address space monitoring (independent of Settings dialog)
  m_addressSpaceTimer.Bind(wxEVT_TIMER,
                           &weather_routing_pi::OnAddressSpaceTimer, this);
  m_addressSpaceTimer.Start(5000);  // Check every 5 seconds

  wxLogMessage("weather_routing_pi: Address space monitoring started");
#endif
}

/**
 * @brief Destructor - ensures proper cleanup of all resources.
 *
 * @details Shutdown sequence (critical order):
 * 1. Stop and unbind address space timer to prevent further events
 * 2. Shutdown AddressSpaceMonitor (marks invalid, closes alert dialog)
 * 3. Process pending events multiple times to clear event queue
 * 4. Delete plugin resources
 *
 * @note Event processing loop (5 iterations with yields) ensures no orphaned
 * timer events fire after destruction.
 */
weather_routing_pi::~weather_routing_pi() {
  wxLogMessage("weather_routing_pi: Destructor starting");

 #ifdef __WXMSW__
  // ========== Windows-Only: Address Space Monitor Cleanup ==========

  // CRITICAL STEP 1: FIRST Stop and Unbind Timers Before Shutdown
  if (m_addressSpaceTimer.IsRunning()) {
    m_addressSpaceTimer.Stop();
    wxLogMessage("weather_routing_pi: Stopped address space timer");  }
    // CRITICAL STEP 2: Unbind the event handler to prevent queued events
    m_addressSpaceTimer.Unbind(wxEVT_TIMER, &weather_routing_pi::OnAddressSpaceTimer, this);
  

  // CRITICAL STEP 3: Shutdown the monitor (marks invalid, clears gauge, closes alert)
    wxLogMessage("Before: m_addressSpaceMonitor=%p", static_cast<void*>(m_addressSpaceMonitor.get()));
    if (m_addressSpaceMonitor) {m_addressSpaceMonitor->Shutdown();
    // No need to delete or set to nullptr; unique_ptr handles it.
    wxLogMessage("After: m_addressSpaceMonitor shutdown (unique_ptr will clean up)");
    } else {
    wxLogWarning("m_addressSpaceMonitor is nullptr in destructor");
  }

  // CRITICAL STEP 4: Process pending events multiple times
  // This prevents orphaned timer events from firing after monitor is destroyed
  if (wxTheApp) {
    for (int i = 0; i < 5; i++) {  // 5 iterations ensures all queued events processed
      wxTheApp->ProcessPendingEvents();
      wxYield();  // Allow GUI to process events
      wxMilliSleep(10); // Small delay to let events settle
    }
  }
    wxLogMessage("weather_routing_pi: Address space monitoring cleanup complete");

#endif

    // 2. Close and destroy all dialogs/windows
    if (m_pWeather_Routing) {
      m_pWeather_Routing->Close();
      delete m_pWeather_Routing;
      m_pWeather_Routing = nullptr;
    }

    // 3. Flush the event queue
    if (wxTheApp) {
      wxTheApp->ProcessPendingEvents();
      wxYield();
    }

  delete _img_WeatherRouting;

  wxLogMessage("weather_routing_pi: Destructor complete");
}

#ifdef __WXMSW__
/**
 * @brief Timer callback for continuous address space monitoring (Windows only).
 *
 * @param event Timer event (unused).
 *
 * @details Called every 5 seconds to check memory usage and show alerts if
 * the threshold is exceeded. Validates monitor state before accessing to
 * prevent use-after-destruction crashes.
 *
 * @note This timer runs independently of the Settings dialog and continues even
 * when the dialog is closed.
 */

void weather_routing_pi::OnAddressSpaceTimer(wxTimerEvent& event) {
  // Double-check monitor validity before accessing
  if (!m_addressSpaceMonitor || !m_addressSpaceMonitor->IsValidState()) {
    wxLogWarning(
        "OnAddressSpaceTimer: Monitor is invalid or null, stopping timer");
    if (m_addressSpaceTimer.IsRunning()) {
      m_addressSpaceTimer.Stop();
    }
    return;
  }

  // This will run continuously, even when Settings dialog is closed
  m_addressSpaceMonitor->CheckAndAlert();
}
#endif

int weather_routing_pi::Init() {
  AddLocaleCatalog("opencpn-weather_routing_pi");

  //    Get a pointer to the opencpn configuration object
  m_pconfig = GetOCPNConfigObject();

  // Get a pointer to the opencpn display canvas, to use as a parent for the
  // WEATHER_ROUTING dialog
  m_parent_window = GetOCPNCanvasWindow();

  m_pWeather_Routing = NULL;

  RouteMapConfiguration::s_plugin_instance = this;

#ifdef PLUGIN_USE_SVG
  m_leftclick_tool_id = InsertPlugInToolSVG(
      "WeatherRouting", _svg_weather_routing, _svg_weather_routing_rollover,
      _svg_weather_routing_toggled, wxITEM_CHECK, _("Weather Routing"),
      wxEmptyString, NULL, WEATHER_ROUTING_TOOL_POSITION, 0, this);
#else
  m_leftclick_tool_id =
      InsertPlugInTool(wxEmptyString, _img_WeatherRouting, _img_WeatherRouting,
                       wxITEM_CHECK, _("Weather Routing"), wxEmptyString, NULL,
                       WEATHER_ROUTING_TOOL_POSITION, 0, this);
#endif
  wxMenu dummy_menu;
  m_position_menu_id = AddCanvasContextMenuItem(
      new wxMenuItem(&dummy_menu, -1, _("Weather Route Position")), this);
  SetCanvasMenuItemViz(m_position_menu_id, false);

  m_waypoint_menu_id = AddCanvasMenuItem(
      new wxMenuItem(&dummy_menu, -1, _("Weather Route Position")), this,
      "Waypoint");
  SetCanvasMenuItemViz(m_waypoint_menu_id, false, "Waypoint");

  m_route_menu_id = AddCanvasMenuItem(
      new wxMenuItem(&dummy_menu, -1, _("Weather Route Analysis")), this,
      "Route");
  // SetCanvasMenuItemViz(m_route_menu_id, false, "Route");

  //    And load the configuration items
  LoadConfig();

  return (WANTS_OVERLAY_CALLBACK | WANTS_OPENGL_OVERLAY_CALLBACK |
          WANTS_TOOLBAR_CALLBACK | WANTS_CONFIG | WANTS_CURSOR_LATLON |
          WANTS_NMEA_EVENTS | WANTS_PLUGIN_MESSAGING | USES_AUI_MANAGER);
}

/**
 * @brief Deinitializes the plugin and cleans up all resources.
 *
 * @return true Always returns true to indicate successful cleanup.
 *
 * @details Shutdown sequence (critical order):
 * 1. Stop cursor timer
 * 2. Stop and shutdown AddressSpaceMonitor (Windows only)
 * 3. Process events to clear queued timer events
 * 4. Close WeatherRouting dialog (which includes SettingsDialog)
 * 5. Delete WeatherRouting object
 * 6. Final event processing to handle destruction events
 *
 * @note Monitor MUST be shutdown before closing WeatherRouting to allow
 * SettingsDialog's timer to stop cleanly.
 */
bool weather_routing_pi::DeInit() {
  wxLogMessage("weather_routing_pi::DeInit() - Starting cleanup");

  m_tCursorLatLon.Stop();

#ifdef __WXMSW__
  // CRITICAL: Shutdown monitor BEFORE closing WeatherRouting
  // This ensures the SettingsDialog's timer can safely stop
  // Stop the address space monitoring timer
  if (m_addressSpaceTimer.IsRunning()) {
    m_addressSpaceTimer.Stop();
    wxLogMessage("weather_routing_pi::DeInit() - Stopped address space timer");
  }
  // Now shutdown the monitor
  if (m_addressSpaceMonitor) {
    m_addressSpaceMonitor->Shutdown();
    wxLogMessage("weather_routing_pi::DeInit() - Monitor shutdown complete");
  }

  // Process events to clear any queued timer events
  if (wxTheApp) {
    wxTheApp->ProcessPendingEvents();
    wxYield();
  }
#endif

  // CRITICAL: Close and destroy WeatherRouting (which includes SettingsDialog)
  if (m_pWeather_Routing) {
    wxLogMessage(
        "weather_routing_pi::DeInit() - About to close WeatherRouting");
    m_pWeather_Routing->Close();

    // Force event processing to handle close events
    if (wxTheApp) {
      wxTheApp->ProcessPendingEvents();
      wxYield();
    }
    wxLogMessage(
        "weather_routing_pi::DeInit() - WeatherRouting closed and events "
        "processed");
  }

  WeatherRouting* wr = m_pWeather_Routing;
  m_pWeather_Routing =
      NULL; /* needed first as destructor may call event loop */
  delete wr;

  // Additional event processing after deletion
  if (wxTheApp) {
    wxTheApp->ProcessPendingEvents();
  }

  wxLogMessage("weather_routing_pi::DeInit() - Cleanup complete");
  return true;
}

int weather_routing_pi::GetAPIVersionMajor() { return OCPN_API_VERSION_MAJOR; }

int weather_routing_pi::GetAPIVersionMinor() { return OCPN_API_VERSION_MINOR; }

int weather_routing_pi::GetPlugInVersionMajor() { return PLUGIN_VERSION_MAJOR; }

int weather_routing_pi::GetPlugInVersionMinor() { return PLUGIN_VERSION_MINOR; }

int weather_routing_pi::GetPlugInVersionPatch() { return PLUGIN_VERSION_PATCH; }

int weather_routing_pi::GetPlugInVersionPost() { return PLUGIN_VERSION_TWEAK; }

// wxBitmap *weather_routing_pi::GetPlugInBitmap()
//{
//    return new wxBitmap(_img_WeatherRouting->ConvertToImage().Copy());
//}

// Shipdriver uses the climatology_panel.png file to make the bitmap.
wxBitmap* weather_routing_pi::GetPlugInBitmap() { return &m_panelBitmap; }
// End of shipdriver process

wxString weather_routing_pi::GetCommonName() { return PLUGIN_COMMON_NAME; }

wxString weather_routing_pi::GetShortDescription() {
  return _(PLUGIN_SHORT_DESCRIPTION);
}

wxString weather_routing_pi::GetLongDescription() {
  return _(PLUGIN_LONG_DESCRIPTION);
}

void weather_routing_pi::SetDefaults() {}

int weather_routing_pi::GetToolbarToolCount() { return 1; }

void weather_routing_pi::SetCursorLatLon(double lat, double lon) {
  if (m_pWeather_Routing && m_pWeather_Routing->FirstCurrentRouteMap() &&
      !m_tCursorLatLon.IsRunning())
    m_tCursorLatLon.Start(50, true);

  m_cursor_lat = lat;
  m_cursor_lon = lon;
}

bool weather_routing_pi::WarnAboutPluginVersion(
  const std::string plugin_name,
  int version_major, int version_minor, 
  int min_major, int min_minor, 
  int max_major, int max_minor,    
  const std::string consequence_msg,
  const std::string recommended_version_msg_prefix, 
  const std::string version_msg_suffix)
{
  int version = 1000 * version_major + version_minor,
    version_min = 1000 * min_major + min_minor,
    version_max = 1000 * max_major + max_minor;

  if (version < version_min || version > version_max) {
    std::stringstream msg;
    msg << plugin_name << " " << _("plugin version") << ' '
        << version_major << '.'<< version_minor << ' '
        << _("is not officially supported.") << std::endl << std::endl
        << recommended_version_msg_prefix << ' '
        << min_major << '.' << min_minor << " - " 
        << max_major << '.' << max_minor
        << consequence_msg;      

    wxMessageDialog mdlg(
        m_parent_window,
        msg.str(),
        _("Weather Routing") + " - " + _("Warning"), wxOK | wxICON_WARNING);
    mdlg.ShowModal();
    return true;
  }
  return false;
}

void weather_routing_pi::SetPluginMessage(wxString& message_id,
                                          wxString& message_body) {
  if (message_id == "GRIB_VALUES") {
    Json::Value root;
    Json::Reader reader;
    // wxString    sLogMessage;
    if (reader.parse(static_cast<std::string>(message_body), root)) {
      g_ReceivedJSONMsg = root;
      g_ReceivedMessage = message_body;
    }
  } else if (message_id == "GRIB_TIMELINE") {
    Json::Reader r;
    Json::Value v;
    r.parse(static_cast<std::string>(message_body), v);

    if (v["Day"].asInt() != -1) {
      wxDateTime time;

      time.Set(v["Day"].asInt(), (wxDateTime::Month)v["Month"].asInt(),
               v["Year"].asInt(), v["Hour"].asInt(), v["Minute"].asInt(),
               v["Second"].asInt());

      if (m_pWeather_Routing && time.IsValid()) {
        m_pWeather_Routing->m_ConfigurationDialog.m_GribTimelineTime =
            time.ToUTC();
        //            m_pWeather_Routing->m_ConfigurationDialog.m_cbUseGrib->Enable();
        RequestRefresh(m_parent_window);
      }
    }
  } else if (message_id == "GRIB_TIMELINE_RECORD") {
    Json::Reader r;
    Json::Value v;
    r.parse(static_cast<std::string>(message_body), v);
    static bool shown_warnings;
    if (!shown_warnings) {
      shown_warnings = true;
      WarnAboutPluginVersion(
        _("Grib").ToStdString(),
        v["GribVersionMajor"].asInt(),
        v["GribVersionMinor"].asInt(),
        GRIB_MIN_MAJOR,
        GRIB_MIN_MINOR,
        GRIB_MAX_MAJOR,
        GRIB_MAX_MINOR);
    }

    wxString sptr = v["TimelineSetPtr"].asString();
    wxCharBuffer bptr = sptr.To8BitData();
    const char* ptr = bptr.data();

    GribRecordSet* gptr;
    sscanf(ptr, "%p", &gptr);

    if (m_pWeather_Routing) {
      RouteMapOverlay* routemapoverlay =
          m_pWeather_Routing->m_RouteMapOverlayNeedingGrib;
      if (routemapoverlay) {
        routemapoverlay->Lock();
        routemapoverlay->SetNewGrib(gptr);
        routemapoverlay->Unlock();
      }
    }
  } else if (message_id == "CLIMATOLOGY") {
    if (!m_pWeather_Routing) return; /* not ready */

    Json::Reader r;
    Json::Value v;
    r.parse(static_cast<std::string>(message_body), v);

    static bool shown_warnings;
    if (!shown_warnings) {
      shown_warnings = true;
      if (WarnAboutPluginVersion(
        _("Climatology").ToStdString(),
        v["ClimatologyVersionMajor"].asInt(),
        v["ClimatologyVersionMinor"].asInt(),
        CLIMATOLOGY_MIN_MAJOR,
        CLIMATOLOGY_MIN_MINOR,
        CLIMATOLOGY_MAX_MAJOR,
        CLIMATOLOGY_MAX_MINOR,
        ".\n\n " + _("No climatology data will be available.").ToStdString())
      ) {
        return;
      }
    }

    wxString sptr = v["ClimatologyDataPtr"].asString();
    sscanf(sptr.To8BitData().data(), "%p", &RouteMap::ClimatologyData);

    sptr = v["ClimatologyWindAtlasDataPtr"].asString();
    sscanf(sptr.To8BitData().data(), "%p", &RouteMap::ClimatologyWindAtlasData);

    sptr = v["ClimatologyCycloneTrackCrossingsPtr"].asString();
    sscanf(sptr.To8BitData().data(), "%p",
           &RouteMap::ClimatologyCycloneTrackCrossings);

    if (m_pWeather_Routing) {
      if (RouteMap::ClimatologyData == nullptr) {
        m_pWeather_Routing->m_ConfigurationDialog.m_cClimatologyType->Enable(
            false);
      } else {
        m_pWeather_Routing->m_ConfigurationDialog.m_cClimatologyType->Enable(
            true);
      }
      m_pWeather_Routing->m_ConfigurationDialog.m_cbAvoidCycloneTracks->Enable(
          RouteMap::ClimatologyCycloneTrackCrossings != nullptr);
    }
  } else if (message_id == wxS("OCPN_DRAW_PI_READY_FOR_REQUESTS")) {
    if (message_body == "FALSE") {
      RouteMap::ODFindClosestBoundaryLineCrossing = nullptr;
    } else if (message_body == "TRUE" && m_pWeather_Routing) {
      RequestOcpnDrawSetting();
    }
  } else if (message_id == wxS("WEATHER_ROUTING_PI")) {
    // now read the JSON text and store it in the 'root' structure
    Json::Value root;
    Json::Reader reader;
    // check for errors before retreiving values...
    if (!reader.parse(static_cast<std::string>(message_body), root)) {
      wxLogMessage("weather_routing_pi: Error parsing JSON message - " +
                   reader.getFormattedErrorMessages() + " : " + message_body);
    }

    if (root["Type"].asString() == "Response" &&
        root["Source"].asString() == "OCPN_DRAW_PI") {
      if (root["Msg"].asString() == "Version") {
        if (root["MsgId"].asString() == "version")
          g_ReceivedODVersionJSONMsg = root;
      } else if (root["Msg"].asString() == "GetAPIAddresses") {
        wxString sptr = root["OD_FindClosestBoundaryLineCrossing"].asString();
        sscanf(sptr.To8BitData().data(), "%p",
               &RouteMap::ODFindClosestBoundaryLineCrossing);
      } else if (root["Msg"].asString() == "FindPointInAnyBoundary") {
        if (root["MsgId"].asString() == "exist") {
          b_in_boundary_reply = root["Found"].asBool() == true;
          // if (b_in_boundary_reply) printf("collision with %s\n", (const
          // char*)root[wxS("GUID")].AsString().mb_str());
        }
      }
    }
  }
}

// true if lat lon in any active boundary, aka we can't exit it.
// use JSON msg rather than binary it's not time sensitive.
bool weather_routing_pi::InBoundary(double lat, double lon) {
  Json::Value jMsg;
  Json::FastWriter writer;

  jMsg["Source"] = "WEATHER_ROUTING_PI";
  jMsg["Type"] = "Request";

  jMsg["Msg"] = "FindPointInAnyBoundary";
  jMsg["MsgId"] = "exist";

  jMsg["lat"] = lat;
  jMsg["lon"] = lon;

  jMsg["BoundaryState"] = "Active";
  jMsg["BoundaryType"] = "Exclusion";

  b_in_boundary_reply = false;
  SendPluginMessage("OCPN_DRAW_PI", writer.write(jMsg));

  return b_in_boundary_reply;
}

void weather_routing_pi::SetPositionFixEx(PlugIn_Position_Fix_Ex& pfix) {
  m_boat_lat = pfix.Lat;
  m_boat_lon = pfix.Lon;
}

void weather_routing_pi::ShowPreferencesDialog(wxWindow* parent) {}

void weather_routing_pi::RequestOcpnDrawSetting() {
  if (ODVersionNewerThan(1, 1, 15)) {
    Json::Value jMsg;
    Json::FastWriter writer;

    jMsg["Source"] = "WEATHER_ROUTING_PI";
    jMsg["Type"] = "Request";
    jMsg["Msg"] = "GetAPIAddresses";
    jMsg["MsgId"] = "GetAPIAddresses";
    SendPluginMessage("OCPN_DRAW_PI", writer.write(jMsg));
  }
}

void weather_routing_pi::NewWR() {
  if (m_pWeather_Routing) return;

  m_pWeather_Routing = new WeatherRouting(m_parent_window, *this);

#ifdef __WXMSW__
  wxLogMessage("Before: m_addressSpaceMonitor=%p",
               static_cast<void*>(m_addressSpaceMonitor.get()));
  if (m_addressSpaceMonitor) {
    m_addressSpaceMonitor->SetWeatherRouting(m_pWeather_Routing);
    wxLogMessage("After: m_addressSpaceMonitor=%p (SetWeatherRouting called)",
                 static_cast<void*>(m_addressSpaceMonitor.get()));
  } else {
    wxLogWarning("m_addressSpaceMonitor is nullptr before SetWeatherRouting");
  }

  // ---- INSERT THIS BLOCK ----
  // Disable memory-triggered StopAll() and AutoReset
  m_addressSpaceMonitor->SetAlertEnabled(false);
  m_addressSpaceMonitor->SetAutoStopEnabled(false);
  // ---- END INSERT ----

#endif

  wxPoint p = m_pWeather_Routing->GetPosition();
  m_pWeather_Routing->Move(0,
                           0);  // workaround for gtk autocentre dialog behavior
  m_pWeather_Routing->Move(p);

  SendPluginMessage("GRIB_TIMELINE_REQUEST", "");
  SendPluginMessage("CLIMATOLOGY_REQUEST", "");
  RequestOcpnDrawSetting();
  m_pWeather_Routing->Reset();
}

void weather_routing_pi::OnToolbarToolCallback(int id) {
  if (!m_pWeather_Routing) NewWR();

  m_pWeather_Routing->Show(!m_pWeather_Routing->IsShown());
}

void weather_routing_pi::OnContextMenuItemCallback(int id) {
  if (!m_pWeather_Routing) NewWR();

  if (id == m_position_menu_id) {
    m_pWeather_Routing->AddPosition(m_cursor_lat, m_cursor_lon);
  } else if (id == m_waypoint_menu_id) {
    wxString GUID = GetSelectedWaypointGUID_Plugin();
    if (GUID.IsEmpty()) return;
    std::unique_ptr<PlugIn_Waypoint> w = GetWaypoint_Plugin(GUID);
    PlugIn_Waypoint* wp = w.get();
    if (wp == nullptr) return;
    m_pWeather_Routing->AddPosition(wp->m_lat, wp->m_lon, wp->m_MarkName,
                                    wp->m_GUID);
  } else if (id == m_route_menu_id) {
    wxString GUID = GetSelectedRouteGUID_Plugin();

    m_pWeather_Routing->AddRoute(GUID);
  }
  m_pWeather_Routing->Reset();
}

bool weather_routing_pi::RenderOverlay(wxDC& wxdc, PlugIn_ViewPort* vp) {
  if (m_pWeather_Routing && m_pWeather_Routing->IsShown()) {
    piDC dc(wxdc);
    m_pWeather_Routing->Render(dc, *vp);
    return true;
  }
  return false;
}

bool weather_routing_pi::RenderGLOverlay(wxGLContext* pcontext,
                                         PlugIn_ViewPort* vp) {
  if (m_pWeather_Routing && m_pWeather_Routing->IsShown()) {
    piDC dc;
    dc.SetVP(vp);
    m_pWeather_Routing->Render(dc, *vp);
    return true;
  }
  return false;
}

#ifdef __WXMSW__
void weather_routing_pi::ReassignAddressSpaceMonitor() {
  if (m_addressSpaceTimer.IsRunning()) {
    m_addressSpaceTimer.Stop();
    m_addressSpaceTimer.Unbind(wxEVT_TIMER,
                               &weather_routing_pi::OnAddressSpaceTimer, this);
  }

  if (m_addressSpaceMonitor) {
    m_addressSpaceMonitor->Shutdown();
    m_addressSpaceMonitor.reset();
  }

  m_addressSpaceMonitor = std::make_unique<AddressSpaceMonitor>();

  m_addressSpaceTimer.Bind(wxEVT_TIMER,
                           &weather_routing_pi::OnAddressSpaceTimer, this);
  m_addressSpaceTimer.Start(5000);
}
#endif


void weather_routing_pi::OnCursorLatLonTimer(wxTimerEvent&) {
  if (m_pWeather_Routing == 0) return;

  std::list<RouteMapOverlay*> routemapoverlays =
      m_pWeather_Routing->CurrentRouteMaps();
  bool refresh = false;
  for (std::list<RouteMapOverlay*>::iterator it = routemapoverlays.begin();
       it != routemapoverlays.end(); it++)
    if ((*it)->SetCursorLatLon(m_cursor_lat, m_cursor_lon)) refresh = true;

  m_pWeather_Routing->UpdateCursorPositionDialog();
  m_pWeather_Routing->UpdateRoutePositionDialog();

  if (refresh) {
    RequestRefresh(m_parent_window);
    m_pWeather_Routing->CursorRouteChanged();
  }
}

bool weather_routing_pi::LoadConfig() {
  wxFileConfig* pConf = (wxFileConfig*)m_pconfig;

  if (!pConf) return false;

  pConf->SetPath("/PlugIns/WeatherRouting");
  return true;
}

bool weather_routing_pi::SaveConfig() {
  wxFileConfig* pConf = (wxFileConfig*)m_pconfig;

  if (!pConf) return false;

  pConf->SetPath(_T ( "/PlugIns/WeatherRouting" ));
  return true;
}

void weather_routing_pi::SetColorScheme(PI_ColorScheme cs) {
  DimeWindow(m_pWeather_Routing);
}

wxString weather_routing_pi::StandardPath() {
  wxString s = wxFileName::GetPathSeparator();
  wxString stdPath = *GetpPrivateApplicationDataLocation();

  stdPath += s + "plugins";
  if (!wxDirExists(stdPath)) wxMkdir(stdPath);

  stdPath += s + "weather_routing";

#ifdef __WXOSX__
  // Compatibility with pre-OCPN-4.2; move config dir to
  // ~/Library/Preferences/opencpn if it exists
  {
    wxStandardPathsBase& std_path = wxStandardPathsBase::Get();
    wxString s = wxFileName::GetPathSeparator();
    // should be ~/Library/Preferences/opencpn
    wxString oldPath =
        (std_path.GetUserConfigDir() + s + "plugins" + s + "weather_routing");
    if (wxDirExists(oldPath) && !wxDirExists(stdPath)) {
      wxLogMessage("weather_routing_pi: moving config dir %s to %s", oldPath,
                   stdPath);
      wxRenameFile(oldPath, stdPath);
    }
  }
#endif

  if (!wxDirExists(stdPath)) wxMkdir(stdPath);

  stdPath += s;
  return stdPath;
}

void weather_routing_pi::ShowMenuItems(bool show) {
  SetToolbarItemState(m_leftclick_tool_id, show);
  SetCanvasMenuItemViz(m_position_menu_id, show);
  SetCanvasMenuItemViz(m_waypoint_menu_id, show, "Waypoint");
  // SetCanvasMenuItemViz(m_route_menu_id, show, "Route");
}
