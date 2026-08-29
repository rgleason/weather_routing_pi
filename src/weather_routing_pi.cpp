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
#include <wx/timer.h>
#include <wx/treectrl.h>
#include <wx/fileconf.h>
#include <cstring>

#include "Utilities.h"
#include "ChartSafetyHost.h"
#include "Boat.h"
#include "RoutePoint.h"
#include "RouteMap.h"
#include "RouteMapOverlay.h"
#include "RouteWaypointExtractor.h"
#include "WeatherRouting.h"
#include "WeatherDataProvider.h"
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
#include "ExternalPlanningProvider.h"

weather_routing_pi::weather_routing_pi(void* ppimgr)
    : opencpn_plugin_121(ppimgr) {
  // Create the PlugIn icons
  initialize_images();

  // Create the PlugIn icons  -from shipdriver
  // loads png file for the listing panel icon
  const wxString path =
      WeatherRoutingDataFile(_T("weather_routing_panel.png"));

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
  // End of from Shipdriver

  b_in_boundary_reply = false;
  m_use_persistent_chart_safe_cache = true;
  m_chart_safety_ram_cache_mib = 0;
  m_tCursorLatLon.Connect(
      wxEVT_TIMER, wxTimerEventHandler(weather_routing_pi::OnCursorLatLonTimer),
      NULL, this);
  m_pWeather_Routing = NULL;
}

weather_routing_pi::~weather_routing_pi() { delete _img_WeatherRouting; }

int weather_routing_pi::Init() {
  AddLocaleCatalog(PLUGIN_CATALOG_NAME);

  //    Get a pointer to the opencpn configuration object
  m_pconfig = GetOCPNConfigObject();

  // Get a pointer to the opencpn display canvas, to use as a parent for the
  // WEATHER_ROUTING dialog
  m_parent_window = GetOCPNCanvasWindow();

  m_pWeather_Routing = NULL;

  RouteMapConfiguration::s_plugin_instance = this;

#ifdef PLUGIN_USE_SVG
  const bool svg_icons_available =
      wxFileExists(_svg_weather_routing) &&
      wxFileExists(_svg_weather_routing_rollover) &&
      wxFileExists(_svg_weather_routing_toggled);
  if (svg_icons_available) {
    m_leftclick_tool_id = InsertPlugInToolSVG(
        _T("xWeatherRouting"), _svg_weather_routing,
        _svg_weather_routing_rollover, _svg_weather_routing_toggled,
        wxITEM_CHECK, _("xWeatherRouting"), _T(""), NULL,
        WEATHER_ROUTING_TOOL_POSITION, 0, this);
  } else {
    // InsertPlugInToolSVG silently produces OpenCPN's generic jigsaw when an
    // asset is unavailable.  The embedded bitmap is always a more useful and
    // recognisable fallback for development builds and incomplete installs.
    wxLogWarning(
        "xWeatherRouting SVG toolbar assets are unavailable; using the "
        "embedded weather-routing icon. normal=\"%s\" rollover=\"%s\" "
        "toggled=\"%s\".",
        _svg_weather_routing, _svg_weather_routing_rollover,
        _svg_weather_routing_toggled);
    m_leftclick_tool_id =
        InsertPlugInTool(_T(""), _img_WeatherRouting, _img_WeatherRouting,
                         wxITEM_CHECK, _("xWeatherRouting"), _T(""), NULL,
                         WEATHER_ROUTING_TOOL_POSITION, 0, this);
  }
#else
  m_leftclick_tool_id =
      InsertPlugInTool(_T(""), _img_WeatherRouting, _img_WeatherRouting,
                       wxITEM_CHECK, _("xWeatherRouting"), _T(""), NULL,
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

  m_route_multileg_menu_id = AddCanvasMenuItem(
      new wxMenuItem(&dummy_menu, -1, _("Create Weather Routing Legs...")),
      this,
      "Route");
  wxLogMessage(
      "WeatherRouting route context menu ids: analysis=%d multileg=%d",
      m_route_menu_id, m_route_multileg_menu_id);

  //    And load the configuration items
  LoadConfig();

  m_external_planning_provider =
      std::make_unique<ExternalPlanningProvider>(*this);
  m_external_planning_provider->RegisterIfSupported();

  MaybeStartHeadlessRouteTest();

  return (WANTS_OVERLAY_CALLBACK | WANTS_OPENGL_OVERLAY_CALLBACK |
          WANTS_TOOLBAR_CALLBACK | WANTS_CONFIG | WANTS_CURSOR_LATLON |
          WANTS_NMEA_EVENTS | WANTS_PLUGIN_MESSAGING | USES_AUI_MANAGER);
}

bool weather_routing_pi::DeInit() {
  if (m_external_planning_provider &&
      !m_external_planning_provider->Unregister())
    return false;
  m_external_planning_provider.reset();
  FlushChartSafetyCache();
  weather_routing::chart_safety_host::Shutdown();
  m_tCursorLatLon.Stop();
  if (m_pWeather_Routing) m_pWeather_Routing->Close();
  WeatherRouting* wr = m_pWeather_Routing;
  m_pWeather_Routing =
      NULL; /* needed first as destructor may call event loop */
  delete wr;

  return true;
}

bool weather_routing_pi::StartExternalPlanningScenario(
    const wxString& scenario_path, const wxString& output_path,
    long timeout_ms) {
  if (!m_pWeather_Routing) NewWR();
  if (!m_pWeather_Routing ||
      !m_pWeather_Routing->CanStartExternalPlanningScenario())
    return false;
  wxSetEnv("WR_HEADLESS_ROUTE_TEST", "scenario");
  wxSetEnv("WR_HEADLESS_SCENARIO", scenario_path);
  wxSetEnv("WR_HEADLESS_OUTPUT", output_path);
  wxSetEnv("WR_HEADLESS_TIMEOUT_MS", wxString::Format("%ld", timeout_ms));
  wxSetEnv("WR_HEADLESS_NO_EXIT", "resident");
  m_pWeather_Routing->RunHeadlessRouteTestFromEnv();
  return true;
}

void weather_routing_pi::CancelExternalPlanningScenario() {
  if (m_pWeather_Routing) m_pWeather_Routing->CancelExternalPlanningScenario();
}

void weather_routing_pi::ClearExternalPlanningScenario() {
  if (m_pWeather_Routing) m_pWeather_Routing->ClearExternalPlanningScenario();
  wxUnsetEnv("WR_HEADLESS_ROUTE_TEST");
  wxUnsetEnv("WR_HEADLESS_SCENARIO");
  wxUnsetEnv("WR_HEADLESS_OUTPUT");
  wxUnsetEnv("WR_HEADLESS_TIMEOUT_MS");
  wxUnsetEnv("WR_HEADLESS_NO_EXIT");
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

wxString weather_routing_pi::GetCommonName() { return _T(PLUGIN_COMMON_NAME); }

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

void weather_routing_pi::SetPluginMessage(wxString& message_id,
                                          wxString& message_body) {
  if (message_id == _T("GRIB_VALUES")) {
    Json::Value root;
    Json::Reader reader;
    // wxString    sLogMessage;
    if (reader.parse(static_cast<std::string>(message_body), root)) {
      g_ReceivedJSONMsg = root;
      g_ReceivedMessage = message_body;
    }
  } else if (message_id == _T("GRIB_TIMELINE")) {
    Json::Reader r;
    Json::Value v;
    r.parse(static_cast<std::string>(message_body), v);

    if (v.isMember("EpochSeconds") || v["Day"].asInt() != -1) {
      wxDateTime time;
      if (v.isMember("EpochSeconds") && v["EpochSeconds"].isString()) {
        wxLongLong_t epochSeconds = 0;
        if (wxString::FromUTF8(v["EpochSeconds"].asCString())
                .ToLongLong(&epochSeconds))
          time = wxDateTime(static_cast<time_t>(epochSeconds));
      }
      if (!time.IsValid())
        time.Set(v["Day"].asInt(), (wxDateTime::Month)v["Month"].asInt(),
                 v["Year"].asInt(), v["Hour"].asInt(), v["Minute"].asInt(),
                 v["Second"].asInt());

      if (m_pWeather_Routing && time.IsValid()) {
        m_pWeather_Routing->m_ConfigurationDialog.m_GribTimelineTime = time;
        //            m_pWeather_Routing->m_ConfigurationDialog.m_cbUseGrib->Enable();
        RequestRefresh(m_parent_window);
      }
    }
  } else if (message_id == _T("GRIB_TIMELINE_RECORD")) {
    Json::Reader r;
    Json::Value v;
    r.parse(static_cast<std::string>(message_body), v);

    static bool shown_warnings;
    if (!shown_warnings) {
      shown_warnings = true;

      int grib_version_major = v["GribVersionMajor"].asInt();
      int grib_version_minor = v["GribVersionMinor"].asInt();

      int grib_version = 1000 * grib_version_major + grib_version_minor;
      int grib_min = 1000 * GRIB_MIN_MAJOR + GRIB_MIN_MINOR;
      int grib_max = 1000 * GRIB_MAX_MAJOR + GRIB_MAX_MINOR;

      if (grib_version < grib_min || grib_version > grib_max) {
        wxString ver = _("Use versions");
        wxMessageDialog mdlg(
            m_parent_window,
            _("Grib plugin version not supported.") + _T("\n\n") +
                wxString::Format("%s %d.%d to %d.%d", ver, GRIB_MIN_MAJOR,
                                 GRIB_MIN_MINOR, GRIB_MAX_MAJOR,
                                 GRIB_MAX_MINOR),
            _("Weather Routing"), wxOK | wxICON_WARNING);
        mdlg.ShowModal();
      }
    }

    wxString sptr = v["TimelineSetPtr"].asString();
    wxCharBuffer bptr = sptr.To8BitData();
    const char* ptr = bptr.data();

    GribRecordSet* gptr = nullptr;
    if (sscanf(ptr, "%p", &gptr) != 1) gptr = nullptr;

    if (m_pWeather_Routing) {
      const wxString requestToken =
          wxString::FromUTF8(v["WeatherRoutingRequestToken"].asCString());
      if (!requestToken.IsEmpty())
        m_pWeather_Routing->HandleGribTimelineFrame(requestToken, gptr);
      else
        wxLogWarning(
            "WR_GRIB_BROKER ignored timeline response without request token");
    }
  } else if (message_id == _T("CLIMATOLOGY")) {
    if (!m_pWeather_Routing) return; /* not ready */

    Json::Reader r;
    Json::Value v;
    r.parse(static_cast<std::string>(message_body), v);

    static bool shown_warnings;
    if (!shown_warnings) {
      shown_warnings = true;

      int climatology_version_major = v["ClimatologyVersionMajor"].asInt();
      int climatology_version_minor = v["ClimatologyVersionMinor"].asInt();

      int climatology_version =
          1000 * climatology_version_major + climatology_version_minor;
      int climatology_min =
          1000 * CLIMATOLOGY_MIN_MAJOR + CLIMATOLOGY_MIN_MINOR;
      int climatology_max =
          1000 * CLIMATOLOGY_MAX_MAJOR + CLIMATOLOGY_MAX_MINOR;

      if (climatology_version < climatology_min ||
          climatology_version > climatology_max) {
        wxString ver = _("Use versions");
        wxMessageDialog mdlg(
            m_parent_window,
            _("Climatology plugin version not supported, no climatology "
              "data.") +
                _T("\n\n") +
                wxString::Format("%s %d.%d to %d.%d", ver,
                                 CLIMATOLOGY_MIN_MAJOR, CLIMATOLOGY_MIN_MINOR,
                                 CLIMATOLOGY_MAX_MAJOR, CLIMATOLOGY_MAX_MINOR),
            _("Weather Routing"), wxOK | wxICON_WARNING);
        mdlg.ShowModal();
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

    WeatherDataProvider::ResetClimatologyPreparation();

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
      wxLogMessage(_T("weather_routing_pi: Error parsing JSON message - ") +
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

class HeadlessRouteTestStarter : public wxTimer {
public:
  explicit HeadlessRouteTestStarter(weather_routing_pi* plugin)
      : m_plugin(plugin) {}

  void Notify() override {
    // Timers are also dispatched inside modal startup loops. Never construct
    // Weather Routing until OpenCPN has completed those loops and initialized
    // its waypoint manager.
    for (wxWindowList::compatibility_iterator node =
             wxTopLevelWindows.GetFirst();
         node; node = node->GetNext()) {
      wxDialog* dialog = wxDynamicCast(node->GetData(), wxDialog);
      if (dialog && dialog->IsModal()) {
        wxLogMessage(
            "WR_HEADLESS_ROUTE_TEST startup_wait reason=modal_dialog "
            "title=\"%s\"",
            dialog->GetTitle());
        StartOnce(1000);
        return;
      }
    }

    weather_routing_pi* plugin = m_plugin;
    delete this;

    wxLogMessage("WR_HEADLESS_ROUTE_TEST timer_fire");
    if (!plugin->m_pWeather_Routing) plugin->NewWR();
    if (!plugin->m_pWeather_Routing) {
      wxLogMessage("WR_HEADLESS_ROUTE_TEST abort reason=weather_routing_unavailable");
      wxTheApp->ExitMainLoop();
      return;
    }
    wxString mode;
    wxGetEnv("WR_HEADLESS_ROUTE_TEST", &mode);
    if (mode.IsSameAs("open-only", false)) {
      plugin->m_pWeather_Routing->Show(true);
      wxLogMessage("WR_HEADLESS_ROUTE_TEST open_only ready");
      return;
    }
    plugin->m_pWeather_Routing->RunHeadlessRouteTestFromEnv();
  }

private:
  weather_routing_pi* m_plugin;
};

void weather_routing_pi::MaybeStartHeadlessRouteTest() {
  const char* enabled = getenv("WR_HEADLESS_ROUTE_TEST");
  const char* scenario = getenv("WR_HEADLESS_SCENARIO");
  if ((!enabled || !*enabled) && (!scenario || !*scenario)) return;
  if ((!enabled || !*enabled) && scenario && *scenario)
    wxSetEnv("WR_HEADLESS_ROUTE_TEST", "scenario");

  // OpenCPN continues loading GPX/waypoint state after plugin Init().  Starting
  // Weather Routing immediately can resolve waypoint GUIDs before the waypoint
  // manager is ready, so defer this test-only entry point until app startup has
  // settled.
  wxLogMessage("WR_HEADLESS_ROUTE_TEST timer_scheduled mode=%s scenario=%s",
               enabled ? enabled : "", scenario ? scenario : "");
  HeadlessRouteTestStarter* starter = new HeadlessRouteTestStarter(this);
  starter->StartOnce(5000);
}

void weather_routing_pi::NewWR() {
  if (m_pWeather_Routing) return;

  m_pWeather_Routing = new WeatherRouting(m_parent_window, *this);
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

  wxLogMessage(
      "WeatherRouting context menu callback: id=%d analysis_id=%d "
      "multileg_id=%d",
      id, m_route_menu_id, m_route_multileg_menu_id);

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

    std::vector<RouteWaypointInfo> route_waypoints;
    wxString extraction_error;
    if (ExtractOpenCPNRouteWaypoints(GUID, route_waypoints,
                                     extraction_error)) {
      wxLogMessage(
          "WeatherRouting multi-leg route extraction: route=%s count=%zu",
          GUID, route_waypoints.size());
      for (const RouteWaypointInfo& waypoint : route_waypoints) {
        wxLogMessage(
            "WeatherRouting multi-leg waypoint #%d: name=%s guid=%s lat=%.8f "
            "lon=%.8f",
            waypoint.index, waypoint.name, waypoint.guid, waypoint.lat,
            waypoint.lon);
      }
    } else {
      wxLogMessage("WeatherRouting multi-leg route extraction failed: route=%s "
                   "error=%s",
                   GUID, extraction_error);
    }

    m_pWeather_Routing->AddRoute(GUID);
  } else if (id == m_route_multileg_menu_id) {
    wxString GUID = GetSelectedRouteGUID_Plugin();
    wxLogMessage("WeatherRouting multi-leg selected route: route=%s", GUID);
    m_pWeather_Routing->CreateMultiLegConfigurationsFromRoute(GUID);
    return;
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

  pConf->SetPath(_T( "/PlugIns/WeatherRouting" ));
  pConf->Read(_T("UsePersistentCertifiedSafeAreaCache"),
              &m_use_persistent_chart_safe_cache, true);
  long ram_mib = 0;
  pConf->Read(_T("ChartSafetyRamCacheMiB"), &ram_mib, 0L);
  m_chart_safety_ram_cache_mib =
      static_cast<int>(wxMax(0L, wxMin(8192L, ram_mib)));

  const bool had_use_setting =
      pConf->HasEntry(_T("UseExperimentalChartSafety"));
  const bool had_enforce_setting =
      pConf->HasEntry(_T("EnforceExperimentalChartSafety"));

  wxFileName cache_file(StandardPath(), _T("chart_safety_tiles_v1.cache"));
  m_chart_safety_cache.Configure(
      cache_file.GetFullPath().ToStdString(), m_chart_safety_ram_cache_mib,
      m_use_persistent_chart_safe_cache);
  const bool enhanced =
      weather_routing::chart_safety_host::Initialize(&m_chart_safety_cache);
  if (enhanced) {
    // The enhanced capability is safe-by-construction. On a new installation
    // make it the default while preserving any explicit user choice.
    if (!had_use_setting)
      pConf->Write(_T("UseExperimentalChartSafety"), true);
    if (!had_enforce_setting)
      pConf->Write(_T("EnforceExperimentalChartSafety"), true);
  }
  wxLogMessage("WeatherRouting chart safety: %s RAM=%d MiB persistent=%d",
               weather_routing::chart_safety_host::Status().c_str(),
               m_chart_safety_cache.EffectiveRamMiB(),
               m_use_persistent_chart_safe_cache ? 1 : 0);

  const char* clear_cache = getenv("WR_HEADLESS_CLEAR_CERT_SAFE_CACHE");
  if (clear_cache && !strcmp(clear_cache, "1"))
    ClearChartSafetyCache();
  const char* cache_override = getenv("WR_HEADLESS_PERSISTENT_CERT_SAFE_CACHE");
  if (cache_override) {
    wxString value(cache_override);
    value.MakeLower();
    m_use_persistent_chart_safe_cache =
        value == "1" || value == "true" || value == "yes";
  }
  const char* ram_override = getenv("WR_HEADLESS_CHART_SAFETY_RAM_MIB");
  if (ram_override) {
    long override_mib = 0;
    if (wxString(ram_override).ToLong(&override_mib))
      m_chart_safety_ram_cache_mib =
          static_cast<int>(wxMax(0L, wxMin(8192L, override_mib)));
  }
  m_chart_safety_cache.SetRequestedRamMiB(m_chart_safety_ram_cache_mib);
  m_chart_safety_cache.SetPersistentEnabled(
      m_use_persistent_chart_safe_cache);
  weather_routing::chart_safety_host::SetPersistentCacheEnabled(
      m_use_persistent_chart_safe_cache);
  return true;
}

bool weather_routing_pi::SaveConfig() {
  wxFileConfig* pConf = (wxFileConfig*)m_pconfig;

  if (!pConf) return false;

  pConf->SetPath(_T ( "/PlugIns/WeatherRouting" ));
  pConf->Write(_T("UsePersistentCertifiedSafeAreaCache"),
               m_use_persistent_chart_safe_cache);
  pConf->Write(_T("ChartSafetyRamCacheMiB"),
               static_cast<long>(m_chart_safety_ram_cache_mib));
  return true;
}

void weather_routing_pi::SetUsePersistentChartSafeCache(bool enabled,
                                                        bool save) {
  m_use_persistent_chart_safe_cache = enabled;
  m_chart_safety_cache.SetPersistentEnabled(enabled);
  weather_routing::chart_safety_host::SetPersistentCacheEnabled(enabled);
  if (save) SaveConfig();
}

void weather_routing_pi::SetChartSafetyRamCacheMiB(int ram_mib) {
  m_chart_safety_ram_cache_mib = wxMax(0, wxMin(8192, ram_mib));
  m_chart_safety_cache.SetRequestedRamMiB(m_chart_safety_ram_cache_mib);
  SaveConfig();
}

bool weather_routing_pi::ClearChartSafetyCache() {
  weather_routing::chart_safety_host::InvalidateDerivedMasks();
  const bool plugin_ok = m_chart_safety_cache.Clear();
  const bool host_ok =
      weather_routing::chart_safety_host::ClearPersistentCache();
  return plugin_ok && host_ok;
}

bool weather_routing_pi::FlushChartSafetyCache() {
  const bool plugin_ok = m_chart_safety_cache.Flush(false);
  const bool host_ok = weather_routing::chart_safety_host::SavePersistentCache();
  return plugin_ok && host_ok;
}

bool weather_routing_pi::HasEnhancedChartSafety() const {
  return weather_routing::chart_safety_host::Available();
}

void weather_routing_pi::SetColorScheme(PI_ColorScheme cs) {
  DimeWindow(m_pWeather_Routing);
}

wxString weather_routing_pi::StandardPath() {
  wxString s = wxFileName::GetPathSeparator();
  wxString stdPath = *GetpPrivateApplicationDataLocation();

  stdPath += s + _T("plugins");
  if (!wxDirExists(stdPath)) wxMkdir(stdPath);

  stdPath += s + _T("weather_routing");

#ifdef __WXOSX__
  // Compatibility with pre-OCPN-4.2; move config dir to
  // ~/Library/Preferences/opencpn if it exists
  {
    wxStandardPathsBase& std_path = wxStandardPathsBase::Get();
    wxString s = wxFileName::GetPathSeparator();
    // should be ~/Library/Preferences/opencpn
    wxString oldPath = (std_path.GetUserConfigDir() + s + _T("plugins") + s +
                        _T("weather_routing"));
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
  // Route context actions remain registered and visible while the plugin is
  // active. Toggling them here can leave one route action hidden after the
  // Weather Routing window is closed while other route actions remain visible.
  // SetCanvasMenuItemViz(m_route_multileg_menu_id, show, "Route");
}
