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

#include "WeatherRoutingMessageDialog.h"
#include <wx/wx.h>
#include <wx/stdpaths.h>
#include <wx/timer.h>
#include <wx/treectrl.h>
#include <wx/fileconf.h>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <type_traits>
#ifdef __OCPN__ANDROID__
#include <QCoreApplication>
#include <QEvent>
#endif

#include "Utilities.h"
#include "ChartSafetyHost.h"
#include "Boat.h"
#include "RoutePoint.h"
#include "RouteMap.h"
#include "RouteMapOverlay.h"
#include "RouteWaypointExtractor.h"
#include "RouteSafetyDialog.h"
#include "WeatherRouting.h"
#include "WeatherDataProvider.h"
#include "weather_routing_pi.h"

static_assert(API_VERSION_MINOR == WEATHER_ROUTING_API_HEADER_MINOR,
              "The selected stock API headers were not used");
static_assert(OCPN_API_VERSION_MINOR == 21,
              "Compatibility builds must retain minimum host API 1.21");
static_assert(std::is_base_of<opencpn_plugin_121, weather_routing_pi>::value,
              "Compatibility builds must retain the API 1.21 plugin base");

Json::Value g_ReceivedJSONMsg;
wxString g_ReceivedMessage;

namespace {

long long SteadyMilliseconds() {
  return std::chrono::duration_cast<std::chrono::milliseconds>(
             std::chrono::steady_clock::now().time_since_epoch())
      .count();
}

// Atlas construction is opportunistic and non-essential. A long quiet window
// keeps it away from short pauses while a user is filling a dialog or
// inspecting the chart, instead of interpreting those pauses as permission to
// start chart extraction.
constexpr long long kChartSafetyAtlasGuiQuietMs = 30000;

}  // namespace

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
#include "ChartSafetyDefaults.h"
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
  // Touch devices may select a route before OpenCPN sends any chart cursor.
  m_cursor_lat = NAN;
  m_cursor_lon = NAN;
  m_use_persistent_chart_safe_cache = true;
  m_chart_safety_ram_cache_mib = 0;
  m_chart_safety_atlas_enabled = false;
  m_chart_safety_atlas_max_disk_mib = 2048;
  m_chart_safety_atlas_all_charts = true;
  m_chart_safety_atlas_last_input_ms.store(SteadyMilliseconds(),
                                            std::memory_order_relaxed);
  m_tCursorLatLon.Connect(
      wxEVT_TIMER, wxTimerEventHandler(weather_routing_pi::OnCursorLatLonTimer),
      NULL, this);
  m_chart_safety_atlas_timer.Connect(
      wxEVT_TIMER,
      wxTimerEventHandler(weather_routing_pi::OnChartSafetyAtlasTimer), NULL,
      this);
  m_pWeather_Routing = NULL;
#ifdef __WXMSW__
  m_addressSpaceTimer.Bind(wxEVT_TIMER,
                           &weather_routing_pi::OnAddressSpaceTimer, this);
  m_addressSpaceTimer.Start(5000);
#endif
}

weather_routing_pi::~weather_routing_pi() {
#ifdef __WXMSW__
  m_addressSpaceTimer.Stop();
  m_addressSpaceTimer.Unbind(wxEVT_TIMER,
                             &weather_routing_pi::OnAddressSpaceTimer, this);
  m_addressSpaceMonitor.Shutdown();
#endif
  delete _img_WeatherRouting;
}

#ifdef __WXMSW__
void weather_routing_pi::OnAddressSpaceTimer(wxTimerEvent&) {
  if (!m_addressSpaceMonitor.IsValid()) {
    m_addressSpaceTimer.Stop();
    return;
  }
  m_addressSpaceMonitor.CheckAndAlert();
}
#endif

int weather_routing_pi::Init() {
  AddLocaleCatalog(PLUGIN_CATALOG_NAME);
  wxLogMessage("WR_BUILD version=%d.%d.%d api=%d.%d process_bits=%u",
               PLUGIN_VERSION_MAJOR, PLUGIN_VERSION_MINOR,
               PLUGIN_VERSION_PATCH,
               OCPN_API_VERSION_MAJOR, OCPN_API_VERSION_MINOR,
               static_cast<unsigned>(sizeof(void*) * 8));
#ifdef __WXMSW__
  if (const auto space = m_addressSpaceMonitor.GetAddressSpace()) {
    wxLogMessage("WR_ADDRESS_SPACE total_bytes=%llu available_bytes=%llu "
                 "reserved_committed_bytes=%llu",
                 static_cast<unsigned long long>(space->total),
                 static_cast<unsigned long long>(space->available),
                 static_cast<unsigned long long>(space->UsedBytes()));
  } else {
    wxLogWarning("WR_ADDRESS_SPACE query failed");
  }
#endif

  //    Get a pointer to the opencpn configuration object
  m_pconfig = GetOCPNConfigObject();

  // Get a pointer to the opencpn display canvas, to use as a parent for the
  // WEATHER_ROUTING dialog
  m_parent_window = GetOCPNCanvasWindow();
  m_route_check_viewport_valid = false;
  m_route_check_marker_mouse_down = false;

  m_pWeather_Routing = NULL;

  RouteMapConfiguration::s_plugin_instance = this;

#ifdef PLUGIN_USE_SVG
  const bool svg_icons_available =
      wxFileExists(_svg_weather_routing) &&
      wxFileExists(_svg_weather_routing_rollover) &&
      wxFileExists(_svg_weather_routing_toggled);
  if (svg_icons_available) {
    m_leftclick_tool_id = InsertPlugInToolSVG(
        _T("WeatherRouting"), _svg_weather_routing,
        _svg_weather_routing_rollover, _svg_weather_routing_toggled,
        wxITEM_CHECK, _("WeatherRouting"), _T(""), NULL,
        WEATHER_ROUTING_TOOL_POSITION, 0, this);
  } else {
    // InsertPlugInToolSVG silently produces OpenCPN's generic jigsaw when an
    // asset is unavailable.  The embedded bitmap is always a more useful and
    // recognisable fallback for development builds and incomplete installs.
    wxLogWarning(
        "WeatherRouting SVG toolbar assets are unavailable; using the "
        "embedded weather-routing icon. normal=\"%s\" rollover=\"%s\" "
        "toggled=\"%s\".",
        _svg_weather_routing, _svg_weather_routing_rollover,
        _svg_weather_routing_toggled);
    m_leftclick_tool_id =
        InsertPlugInTool(_T(""), _img_WeatherRouting, _img_WeatherRouting,
                         wxITEM_CHECK, _("WeatherRouting"), _T(""), NULL,
                         WEATHER_ROUTING_TOOL_POSITION, 0, this);
  }
#else
  m_leftclick_tool_id =
      InsertPlugInTool(_T(""), _img_WeatherRouting, _img_WeatherRouting,
                       wxITEM_CHECK, _("WeatherRouting"), _T(""), NULL,
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
  m_route_check_menu_id = AddCanvasMenuItem(
      new wxMenuItem(&dummy_menu, -1, wxString::Format(_("Check this route (%s)..."), GetCommonName())),
      this, "Route");
  wxLogMessage(
      "WeatherRouting route context menu ids: analysis=%d multileg=%d",
      m_route_menu_id, m_route_multileg_menu_id);

  //    And load the configuration items
  LoadConfig();

  wxEvtHandler::AddFilter(this);
  m_chart_safety_atlas_filter_installed = true;

  m_external_planning_provider =
      std::make_unique<ExternalPlanningProvider>(*this);
  m_external_planning_provider->RegisterIfSupported();

  MaybeStartHeadlessRouteTest();
  // Chart providers finish registering after plugin Init().  Atlas work is
  // deliberately delayed and only runs while the route engine is idle.
  ScheduleChartSafetyAtlas(true, 15000);

  return (WANTS_OVERLAY_CALLBACK | WANTS_OPENGL_OVERLAY_CALLBACK |
          WANTS_TOOLBAR_CALLBACK | WANTS_CONFIG | WANTS_CURSOR_LATLON |
          WANTS_NMEA_EVENTS | WANTS_PLUGIN_MESSAGING | USES_AUI_MANAGER |
          WANTS_MOUSE_EVENTS
          );
}

bool weather_routing_pi::DeInit() {
  m_route_check_marker_mouse_down = false;
  m_route_check_viewport_valid = false;
  // Locale changes and plugin disable can occur during a modal startup loop.
  // No delayed fixture may retain a pointer to an unloaded plugin instance.
  delete m_headless_route_test_starter;
  m_headless_route_test_starter = nullptr;
  if (m_route_safety_dialog) delete m_route_safety_dialog.get();
  if (m_route_check_menu_id >= 0) {
    RemoveCanvasMenuItem(m_route_check_menu_id, "Route");
    m_route_check_menu_id = -1;
  }
  m_chart_safety_atlas_timer.Stop();
  if (m_chart_safety_atlas_filter_installed) {
    wxEvtHandler::RemoveFilter(this);
    m_chart_safety_atlas_filter_installed = false;
  }
  WaitForChartSafetyAtlasInspection();
  if (m_external_planning_provider &&
      !m_external_planning_provider->Unregister())
    return false;
  m_external_planning_provider.reset();
  FlushChartSafetyCache();
  weather_routing::chart_safety_host::Shutdown();
  m_tCursorLatLon.Stop();
#ifdef __WXMSW__
  m_addressSpaceTimer.Stop();
  m_addressSpaceMonitor.Shutdown();
#endif
  if (m_pWeather_Routing) m_pWeather_Routing->Close();
  WeatherRouting* wr = m_pWeather_Routing;
  m_pWeather_Routing =
      NULL; /* needed first as destructor may call event loop */
  delete wr;

#ifdef __OCPN__ANDROID__
  // wxQt schedules native widget destruction with deleteLater(). Complete
  // those deletions while the plugin is still loaded: its application/canvas
  // event filters and Qt delegates must not survive a hot plugin replacement.
  QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
#endif

  return true;
}

int weather_routing_pi::FilterEvent(wxEvent& event) {
  const wxEventType type = event.GetEventType();
  const bool user_input = dynamic_cast<wxKeyEvent*>(&event) != nullptr ||
                          dynamic_cast<wxMouseEvent*>(&event) != nullptr ||
                          type == wxEVT_TEXT || type == wxEVT_TEXT_ENTER ||
                          type == wxEVT_COMBOBOX || type == wxEVT_SPINCTRL;
  if (user_input)
    m_chart_safety_atlas_last_input_ms.store(SteadyMilliseconds(),
                                              std::memory_order_relaxed);
  return Event_Skip;
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
        WR_MessageDialog mdlg(
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
        WR_MessageDialog mdlg(
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

// Isolated-profile fixtures only: simulate an external route editor through
// the public host API. The checker itself never calls a route-write API.
class RouteCheckFixtureMutation : public wxTimer {
 public:
  RouteCheckFixtureMutation(wxString guid, wxString action)
      : guid_(std::move(guid)), action_(std::move(action)) { StartOnce(1200); }
  void Notify() override {
    if (action_ == "remove") {
      DeletePlugInRoute(guid_);
    } else if (auto route = GetRoute_Plugin(guid_)) {
      if (action_ == "rename") route->m_NameString += " — edited";
      else if (route->pWaypointList && route->pWaypointList->GetFirst())
        route->pWaypointList->GetFirst()->GetData()->m_lon += .001;
      UpdatePlugInRoute(route.get());
    }
    delete this;
  }
 private:
  wxString guid_, action_;
};

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
        m_waited_for_modal = true;
        StartOnce(1000);
        return;
      }
    }
    if (m_waited_for_modal) {
      m_waited_for_modal = false;
      StartOnce(5000);
      return;
    }

    wxString startup_mode;
    wxGetEnv("WR_HEADLESS_ROUTE_TEST", &startup_mode);
    if (startup_mode.IsSameAs("route-check", false)) {
      wxString scenario;
      wxGetEnv("WR_HEADLESS_SCENARIO", &scenario);
      std::ifstream stream(scenario.ToStdString());
      Json::Value input;
      Json::Reader reader;
      // Command-line GPX import happens during the host's later startup
      // phases. A plugin Init timer can otherwise run before that import.
      if (reader.parse(stream, input) && input["routeGuid"].isString() && !input["waypoints"].isArray() &&
          !GetRoute_Plugin(wxString::FromUTF8(input["routeGuid"].asCString())) &&
          m_route_check_waits++ < 30) {
        wxLogMessage("WR_ROUTE_CHECK_TEST waiting for GPX route import");
        StartOnce(1000);
        return;
      }
    }

    weather_routing_pi* plugin = m_plugin;
    plugin->m_headless_route_test_starter = nullptr;
    delete this;

    wxLogMessage("WR_HEADLESS_ROUTE_TEST timer_fire");
    wxString mode;
    wxGetEnv("WR_HEADLESS_ROUTE_TEST", &mode);
    if (mode.IsSameAs("route-check", false)) {
      wxString scenario, output;
      wxGetEnv("WR_HEADLESS_SCENARIO", &scenario);
      wxGetEnv("WR_HEADLESS_OUTPUT", &output);
      std::ifstream stream(scenario.ToStdString());
      Json::Value input;
      Json::Reader reader;
      if (!reader.parse(stream, input) || !input["routeGuid"].isString()) {
        wxLogError("WR_ROUTE_CHECK_TEST invalid scenario");
        wxTheApp->ExitMainLoop();
        return;
      }
      if (input["waypoints"].isArray() && weather_routing::chart_safety_host::Available()) {
        // Create only a temporary fixture, through the same public route API
        // used by route exporters. The checker itself only reads this route.
        PlugIn_Route fixture;
        fixture.m_GUID = wxString::FromUTF8(input["routeGuid"].asCString());
        fixture.m_NameString = wxString::FromUTF8(input.get("routeName", "Route check fixture").asCString());
        for (const auto& point : input["waypoints"]) {
          fixture.pWaypointList->Append(new PlugIn_Waypoint(point["lat"].asDouble(),
              point["lon"].asDouble(), "diamond", wxString::FromUTF8(point["name"].asCString()),
              wxString::FromUTF8(point["guid"].asCString())));
        }
        if (!AddPlugInRoute(&fixture, false)) {
          wxLogError("WR_ROUTE_CHECK_TEST could not add temporary route");
          wxTheApp->ExitMainLoop();
          return;
        }
      }
      plugin->StartRouteSafetyCheck(wxString::FromUTF8(input["routeGuid"].asCString()),
          output, wxString::FromUTF8(input.get("action", "").asCString()),
          input.get("minimumDepthM", 0.).asDouble(), input.get("landMarginNM", 0.).asDouble());
      const wxString action = wxString::FromUTF8(input.get("action", "").asCString());
      if (action == "edit" || action == "remove" || action == "rename")
        new RouteCheckFixtureMutation(wxString::FromUTF8(input["routeGuid"].asCString()), action);
      return;
    }
    if (!plugin->m_pWeather_Routing) plugin->NewWR();
    if (!plugin->m_pWeather_Routing) {
      wxLogMessage("WR_HEADLESS_ROUTE_TEST abort reason=weather_routing_unavailable");
      wxTheApp->ExitMainLoop();
      return;
    }
    if (mode.IsSameAs("open-only", false)) {
      plugin->m_pWeather_Routing->Show(true);
      wxLogMessage("WR_HEADLESS_ROUTE_TEST open_only ready");
      return;
    }
    plugin->m_pWeather_Routing->RunHeadlessRouteTestFromEnv();
  }

private:
  weather_routing_pi* m_plugin;
  int m_route_check_waits{0};
  bool m_waited_for_modal{false};
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
  delete m_headless_route_test_starter;
  auto* starter = new HeadlessRouteTestStarter(this);
  m_headless_route_test_starter = starter;
  wxString test_mode;
  wxGetEnv("WR_HEADLESS_ROUTE_TEST", &test_mode);
  starter->StartOnce(test_mode.IsSameAs("route-check", false) ? 12000 : 5000);
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
#ifdef __OCPN__ANDROID__
  if (m_pWeather_Routing->AndroidChartPickPending()) {
    m_pWeather_Routing->CancelAndroidChartPick();
    return;
  }
#endif
  m_pWeather_Routing->Show(!m_pWeather_Routing->IsShown());
}

void weather_routing_pi::OnContextMenuItemCallback(int id) {
  if (id == m_route_check_menu_id) {
    StartRouteSafetyCheck(GetSelectedRouteGUID_Plugin());
    return;
  }
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

void weather_routing_pi::StartRouteSafetyCheck(const wxString& route_guid,
    const wxString& test_output, const wxString& test_action,
    double test_depth, double test_margin) {
  double depth = 0, margin = 0;
  if (m_pWeather_Routing && m_pWeather_Routing->FirstCurrentRouteMap()) {
    const auto& config = m_pWeather_Routing->FirstCurrentRouteMap()->GetConfiguration();
    depth = config.MinimumDepthMeters;
    margin = config.SafetyMarginLand;
  }
  const auto old_path = m_pconfig->GetPath();
  m_pconfig->SetPath("/PlugIns/WeatherRouting/RouteCheck");
  m_pconfig->Read("MinimumDepthM", &depth, depth);
  m_pconfig->Read("LandMarginNM", &margin, margin);
  m_pconfig->SetPath(old_path);
  if (!test_output.IsEmpty()) { depth = test_depth; margin = test_margin; }
  if (m_route_safety_dialog) delete m_route_safety_dialog.get();
  m_route_safety_dialog = new RouteSafetyDialog(*this, route_guid, depth, margin);
  if (!test_output.IsEmpty()) m_route_safety_dialog->SetTestOutput(test_output, test_action);
  m_route_safety_dialog->Show();
  m_route_safety_dialog->Start();
}

bool weather_routing_pi::RenderOverlay(wxDC& wxdc, PlugIn_ViewPort* vp) {
  m_route_check_viewport = *vp;
  m_route_check_viewport_valid = true;
  bool rendered = false;
  if (m_route_safety_dialog) {
    piDC dc(wxdc);
    rendered = m_route_safety_dialog->Render(dc, *vp);
  }
#ifdef __OCPN__ANDROID__
  m_androidViewport = *vp;
  m_androidViewportValid = true;
#endif
  if (m_pWeather_Routing && (m_pWeather_Routing->IsShown()
#ifdef __OCPN__ANDROID__
                            || m_pWeather_Routing->AndroidChartVisible()
#endif
                            )) {
    piDC dc(wxdc);
    m_pWeather_Routing->Render(dc, *vp);
    return true;
  }
  return rendered;
}

bool weather_routing_pi::RenderGLOverlay(wxGLContext* pcontext,
                                         PlugIn_ViewPort* vp) {
  m_route_check_viewport = *vp;
  m_route_check_viewport_valid = true;
  bool rendered = false;
  if (m_route_safety_dialog) {
    piDC dc;
    dc.SetVP(vp);
    rendered = m_route_safety_dialog->Render(dc, *vp);
  }
#ifdef __OCPN__ANDROID__
  m_androidViewport = *vp;
  m_androidViewportValid = true;
#endif
  if (m_pWeather_Routing && (m_pWeather_Routing->IsShown()
#ifdef __OCPN__ANDROID__
                            || m_pWeather_Routing->AndroidChartVisible()
#endif
                            )) {
    piDC dc;
    dc.SetVP(vp);
    m_pWeather_Routing->Render(dc, *vp);
    return true;
  }
  return rendered;
}

bool weather_routing_pi::MouseEventHook(wxMouseEvent& event) {
  // Own the whole marker gesture, so the same click cannot start moving an
  // underlying route or waypoint. GTK/touch hosts can filter double clicks;
  // a single click selects a marker consistently on all supported hosts.
  if (m_route_check_marker_mouse_down) {
    if (event.LeftUp()) { m_route_check_marker_mouse_down = false; return true; }
    if (event.Dragging() || event.LeftDClick()) return true;
  }
  if ((event.LeftDown() || event.LeftDClick()) && m_route_safety_dialog &&
      m_route_check_viewport_valid &&
      m_route_safety_dialog->SelectMarker(event.GetPosition(), m_route_check_viewport)) {
    m_route_check_marker_mouse_down = true;
    return true;
  }
#ifdef __OCPN__ANDROID__
  return m_pWeather_Routing && m_androidViewportValid &&
      m_pWeather_Routing->HandleAndroidChartPick(event, &m_androidViewport);
#else
  return false;
#endif
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
  pConf->Read(_T("ChartSafetyAtlasEnabled"),
              &m_chart_safety_atlas_enabled, false);
  long atlas_mib = 2048;
  pConf->Read(_T("ChartSafetyAtlasMaxDiskMiB"), &atlas_mib, 2048L);
  m_chart_safety_atlas_max_disk_mib =
      static_cast<int>(wxMax(256L, wxMin(16384L, atlas_mib)));
  pConf->Read(_T("ChartSafetyAtlasAllCharts"),
              &m_chart_safety_atlas_all_charts, true);
  wxString atlas_paths;
  pConf->Read(_T("ChartSafetyAtlasSelectedPaths"), &atlas_paths, wxEmptyString);
  wxString completed_atlas_identity;
  pConf->Read(_T("ChartSafetyAtlasCompletedIdentity"),
              &completed_atlas_identity, wxEmptyString);
  m_chart_safety_atlas_completed_identity =
      completed_atlas_identity.ToStdString();
  m_chart_safety_atlas_selected_paths.clear();
  for (const wxString& path : wxSplit(atlas_paths, '\n')) {
    if (!path.IsEmpty())
      m_chart_safety_atlas_selected_paths.insert(path.ToStdString());
  }

  const bool had_use_setting =
      pConf->HasEntry(_T("UseExperimentalChartSafety"));
  const bool had_enforce_setting =
      pConf->HasEntry(_T("EnforceExperimentalChartSafety"));

  wxFileName cache_file(StandardPath(), _T("chart_safety_tiles_v1.cache"));
  m_chart_safety_cache.Configure(
      cache_file.GetFullPath().ToStdString(), m_chart_safety_ram_cache_mib,
      m_use_persistent_chart_safe_cache);
  m_chart_safety_cache.SetMaximumDiskMiB(
      m_chart_safety_atlas_max_disk_mib);
  const bool enhanced =
      weather_routing::chart_safety_host::Initialize(&m_chart_safety_cache);
  if (enhanced) {
    // Preserve explicit user choices, but keep fresh standard-plugin installs
    // stock-safe. Users opt into the optional enhanced host capability.
    if (!had_use_setting)
      pConf->Write(
          _T("UseExperimentalChartSafety"),
          weather_routing::chart_safety_defaults::kCheckLoadedCharts);
    if (!had_enforce_setting)
      pConf->Write(
          _T("EnforceExperimentalChartSafety"),
          weather_routing::chart_safety_defaults::kRequireChartDepthChecks);
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
  pConf->Write(_T("ChartSafetyAtlasEnabled"),
               m_chart_safety_atlas_enabled);
  pConf->Write(_T("ChartSafetyAtlasMaxDiskMiB"),
               static_cast<long>(m_chart_safety_atlas_max_disk_mib));
  pConf->Write(_T("ChartSafetyAtlasAllCharts"),
               m_chart_safety_atlas_all_charts);
  wxString atlas_paths;
  for (const auto& path : m_chart_safety_atlas_selected_paths) {
    if (!atlas_paths.IsEmpty()) atlas_paths += '\n';
    atlas_paths += wxString::FromUTF8(path.c_str());
  }
  pConf->Write(_T("ChartSafetyAtlasSelectedPaths"), atlas_paths);
  pConf->Write(_T("ChartSafetyAtlasCompletedIdentity"),
               wxString::FromUTF8(
                   m_chart_safety_atlas_completed_identity.c_str()));
  return true;
}

void weather_routing_pi::SetUsePersistentChartSafeCache(bool enabled,
                                                        bool save) {
  m_use_persistent_chart_safe_cache = enabled;
  m_chart_safety_cache.SetPersistentEnabled(enabled);
  weather_routing::chart_safety_host::SetPersistentCacheEnabled(enabled);
  if (save) SaveConfig();
  ScheduleChartSafetyAtlas(false);
}

void weather_routing_pi::SetChartSafetyRamCacheMiB(int ram_mib) {
  m_chart_safety_ram_cache_mib = wxMax(0, wxMin(8192, ram_mib));
  m_chart_safety_cache.SetRequestedRamMiB(m_chart_safety_ram_cache_mib);
  SaveConfig();
}

void weather_routing_pi::SetChartSafetyAtlasSettings(
    bool enabled, int max_disk_mib, bool all_charts,
    std::set<std::string> selected_paths) {
  m_chart_safety_atlas_enabled = enabled;
  m_chart_safety_atlas_max_disk_mib =
      wxMax(256, wxMin(16384, max_disk_mib));
  m_chart_safety_atlas_all_charts = all_charts;
  m_chart_safety_atlas_selected_paths = std::move(selected_paths);
  m_chart_safety_cache.SetMaximumDiskMiB(
      m_chart_safety_atlas_max_disk_mib);
  SaveConfig();
  ScheduleChartSafetyAtlas(true);
}

void weather_routing_pi::ResetChartSafetyAtlasPlan() {
  WaitForChartSafetyAtlasInspection();
  m_chart_safety_atlas_coverage_tiles.clear();
  m_chart_safety_atlas_tiles.clear();
  m_chart_safety_atlas_cursor = 0;
  m_chart_safety_atlas_metadata_attempts = 0;
  m_chart_safety_atlas_batch_retries = 0;
  m_chart_safety_atlas_failed_batches = 0;
  m_chart_safety_atlas_plan_identity.clear();
  m_chart_safety_atlas_logged_route_pause = false;
  m_chart_safety_atlas_logged_user_pause = false;
  m_chart_safety_atlas_plan_ready = false;
  m_chart_safety_atlas_batch_limit = 1;
  m_chart_safety_atlas_selected_charts = 0;
  m_chart_safety_atlas_estimate_mib = 0.0;
}

void weather_routing_pi::WaitForChartSafetyAtlasInspection() {
  if (!m_chart_safety_atlas_inspection.valid()) return;
  m_chart_safety_atlas_inspection.wait();
  // get() is required to release any exception retained by std::future. The
  // inspection itself reports normal I/O failures in its result.
  try {
    (void)m_chart_safety_atlas_inspection.get();
  } catch (const std::exception& error) {
    wxLogWarning("WR_CHART_ATLAS inspection_worker_error error=%s",
                 error.what());
  }
}

bool weather_routing_pi::ChartSafetyAtlasGuiIdle() const {
  const long long last_input = m_chart_safety_atlas_last_input_ms.load(
      std::memory_order_relaxed);
  return SteadyMilliseconds() - last_input >= kChartSafetyAtlasGuiQuietMs;
}

void weather_routing_pi::ScheduleChartSafetyAtlas(bool rebuild_plan,
                                                   int delay_ms) {
  m_chart_safety_atlas_timer.Stop();
  if (rebuild_plan) ResetChartSafetyAtlasPlan();
  if (!m_chart_safety_atlas_enabled ||
      !m_use_persistent_chart_safe_cache ||
      !weather_routing::chart_safety_host::Available())
    return;
  m_chart_safety_atlas_timer.StartOnce(wxMax(100, delay_ms));
}

void weather_routing_pi::OnChartSafetyAtlasTimer(wxTimerEvent&) {
  if (m_route_safety_dialog && m_route_safety_dialog->IsChecking()) {
    ScheduleChartSafetyAtlas(false, 1000);
    return;
  }
  const bool provider_available =
      weather_routing::chart_safety_host::Available();
  const bool route_idle =
      !m_pWeather_Routing ||
      m_pWeather_Routing->CanStartExternalPlanningScenario();
  const bool gui_idle = ChartSafetyAtlasGuiIdle();
  const auto idle_decision = weather_routing::DecideChartSafetyAtlasIdleWork(
      m_chart_safety_atlas_enabled, m_use_persistent_chart_safe_cache,
      provider_available, route_idle, gui_idle);
  if (idle_decision ==
      weather_routing::ChartSafetyAtlasIdleDecision::Disabled)
    return;

  // Route computations have absolute priority.  The timer performs no chart
  // work until all running/waiting/headless route state has drained.
  if (idle_decision ==
      weather_routing::ChartSafetyAtlasIdleDecision::PauseForRoute) {
    if (!m_chart_safety_atlas_logged_route_pause) {
      wxLogMessage("WR_CHART_ATLAS paused reason=route_active");
      m_chart_safety_atlas_logged_route_pause = true;
    }
    ScheduleChartSafetyAtlas(false, 1000);
    return;
  }
  if (idle_decision ==
      weather_routing::ChartSafetyAtlasIdleDecision::PauseForUser) {
    if (!m_chart_safety_atlas_logged_user_pause) {
      wxLogMessage("WR_CHART_ATLAS paused reason=user_active");
      m_chart_safety_atlas_logged_user_pause = true;
    }
    ScheduleChartSafetyAtlas(false, 500);
    return;
  }
  if (m_chart_safety_atlas_logged_route_pause) {
    wxLogMessage("WR_CHART_ATLAS resumed reason=route_idle");
    m_chart_safety_atlas_logged_route_pause = false;
  }
  if (m_chart_safety_atlas_logged_user_pause) {
    wxLogMessage("WR_CHART_ATLAS resumed reason=gui_idle");
    m_chart_safety_atlas_logged_user_pause = false;
  }

  if (!m_chart_safety_atlas_plan_ready &&
      !m_chart_safety_atlas_inspection.valid()) {
    const auto charts = weather_routing::chart_safety_host::AtlasCharts();
    if (charts.empty()) {
      if (++m_chart_safety_atlas_metadata_attempts <= 8)
        ScheduleChartSafetyAtlas(false, 15000);
      else
        wxLogWarning("WR_CHART_ATLAS stopped reason=no_chart_metadata");
      return;
    }

    const std::uint64_t quota_bytes =
        static_cast<std::uint64_t>(m_chart_safety_atlas_max_disk_mib) *
        1024ULL * 1024ULL;
    // Include the estimator's 25% journal/metadata headroom in the gate.  A
    // scope which cannot fit is never partly generated in arbitrary tile
    // order; the user must narrow the selection or raise the quota.
    const std::uint64_t maximum_tiles = std::max<std::uint64_t>(
        1, (quota_bytes * 4ULL / 5ULL) /
               weather_routing::kChartSafetyAtlasEstimatedBytesPerTile);
    bool coverage_complete = false;
    auto coverage_tiles =
        weather_routing::chart_safety_host::AtlasCoverageTiles(
            charts, m_chart_safety_atlas_selected_paths,
            m_chart_safety_atlas_all_charts, maximum_tiles,
            &coverage_complete);
    const auto estimate = weather_routing::EstimateChartSafetyAtlasCoverage(
        charts, coverage_tiles, m_chart_safety_atlas_selected_paths,
        m_chart_safety_atlas_all_charts, coverage_complete);
    if (!estimate.complete || estimate.recommended_quota_bytes > quota_bytes) {
      wxLogWarning(
          "WR_CHART_ATLAS stopped reason=quota_too_small selected_charts=%llu "
          "estimated_tiles_at_least=%llu quota_mib=%d",
          static_cast<unsigned long long>(estimate.selected_charts),
          static_cast<unsigned long long>(estimate.upper_bound_tiles),
          m_chart_safety_atlas_max_disk_mib);
      return;
    }
    if (const char* estimate_only =
            std::getenv("WR_CHART_ATLAS_ESTIMATE_ONLY");
        estimate_only && std::strcmp(estimate_only, "0") != 0) {
      wxLogMessage(
          "WR_CHART_ATLAS estimate_only charts=%llu tiles=%llu "
          "compact_mib=%.1f recommended_mib=%.1f",
          static_cast<unsigned long long>(estimate.selected_charts),
          static_cast<unsigned long long>(estimate.upper_bound_tiles),
          estimate.compact_bytes / (1024.0 * 1024.0),
          estimate.recommended_quota_bytes / (1024.0 * 1024.0));
      return;
    }
    m_chart_safety_atlas_plan_identity =
        weather_routing::ChartSafetyAtlasIdentity(
            charts, m_chart_safety_atlas_selected_paths,
            m_chart_safety_atlas_all_charts);
    const std::string host_identity =
        weather_routing::chart_safety_host::ConfirmedIdentity();
    if (host_identity.empty()) {
      wxLogWarning("WR_CHART_ATLAS stopped reason=identity_unavailable");
      return;
    }
    m_chart_safety_atlas_plan_identity += ":" + host_identity;
    if (!coverage_complete || coverage_tiles.empty()) {
      wxLogWarning("WR_CHART_ATLAS stopped reason=empty_or_incomplete_plan");
      ResetChartSafetyAtlasPlan();
      return;
    }

    m_chart_safety_atlas_coverage_tiles = std::move(coverage_tiles);
    m_chart_safety_atlas_selected_charts = estimate.selected_charts;
    m_chart_safety_atlas_estimate_mib =
        estimate.compact_bytes / (1024.0 * 1024.0);
    const std::string inspection_identity =
        m_chart_safety_atlas_plan_identity;
    // Indexing and checking hundreds of thousands of durable records can
    // take seconds. It uses only ChartSafetyCache's mutex-protected store and
    // no wx/chart objects, so keep this I/O and index work off the GUI thread.
    m_chart_safety_atlas_inspection = std::async(
        std::launch::async, [this, inspection_identity]() {
          return m_chart_safety_cache.InspectAtlasCoverage(
              inspection_identity, m_chart_safety_atlas_coverage_tiles);
        });
    wxLogMessage(
        "WR_CHART_ATLAS inspecting_async expected=%llu identity=%s",
        static_cast<unsigned long long>(
            m_chart_safety_atlas_coverage_tiles.size()),
        m_chart_safety_atlas_plan_identity.c_str());
    ScheduleChartSafetyAtlas(false, 100);
    return;
  }

  if (!m_chart_safety_atlas_plan_ready) {
    if (!m_chart_safety_atlas_inspection.valid()) return;
    if (m_chart_safety_atlas_inspection.wait_for(
            std::chrono::milliseconds(0)) != std::future_status::ready) {
      ScheduleChartSafetyAtlas(false, 100);
      return;
    }

    weather_routing::ChartSafetyAtlasCacheStatus cache_status;
    try {
      cache_status = m_chart_safety_atlas_inspection.get();
    } catch (const std::exception& error) {
      wxLogWarning("WR_CHART_ATLAS stopped reason=inspection_worker_error "
                   "error=%s",
                   error.what());
      ResetChartSafetyAtlasPlan();
      return;
    }
    wxLogMessage(
        "WR_CHART_ATLAS inspected expected=%llu present=%llu missing=%llu "
        "marker_matches=%d store_ready=%d",
        static_cast<unsigned long long>(cache_status.expected_tiles),
        static_cast<unsigned long long>(cache_status.present_tiles),
        static_cast<unsigned long long>(cache_status.missing_tiles.size()),
        cache_status.completion_marker_matches ? 1 : 0,
        cache_status.store_ready ? 1 : 0);
    if (!cache_status.store_ready) {
      wxLogWarning("WR_CHART_ATLAS stopped reason=cache_unavailable error=%s",
                   cache_status.error.c_str());
      ResetChartSafetyAtlasPlan();
      return;
    }
    if (cache_status.complete) {
      if (m_chart_safety_atlas_completed_identity !=
          m_chart_safety_atlas_plan_identity) {
        m_chart_safety_atlas_completed_identity =
            m_chart_safety_atlas_plan_identity;
        SaveConfig();
      }
      wxLogMessage(
          "WR_CHART_ATLAS already_complete identity=%s verified_tiles=%llu",
          m_chart_safety_atlas_plan_identity.c_str(),
          static_cast<unsigned long long>(cache_status.present_tiles));
      return;
    }
    if (m_chart_safety_atlas_completed_identity ==
        m_chart_safety_atlas_plan_identity) {
      wxLogWarning(
          "WR_CHART_ATLAS stale_config_marker identity=%s missing=%llu",
          m_chart_safety_atlas_plan_identity.c_str(),
          static_cast<unsigned long long>(cache_status.missing_tiles.size()));
      m_chart_safety_atlas_completed_identity.clear();
      SaveConfig();
    }
    if (const char* inspect_only =
            std::getenv("WR_CHART_ATLAS_INSPECT_ONLY");
        inspect_only && std::strcmp(inspect_only, "0") != 0) {
      wxLogMessage(
          "WR_CHART_ATLAS inspect_only expected=%llu present=%llu "
          "missing=%llu",
          static_cast<unsigned long long>(cache_status.expected_tiles),
          static_cast<unsigned long long>(cache_status.present_tiles),
          static_cast<unsigned long long>(cache_status.missing_tiles.size()));
      return;
    }
    if (cache_status.missing_tiles.empty()) {
      if (!m_chart_safety_cache.CommitAtlasCompletion(
              m_chart_safety_atlas_plan_identity,
              m_chart_safety_atlas_coverage_tiles)) {
        wxLogWarning(
            "WR_CHART_ATLAS stopped reason=completion_commit_failed error=%s",
            m_chart_safety_cache.LastError().c_str());
        ResetChartSafetyAtlasPlan();
        return;
      }
      m_chart_safety_atlas_completed_identity =
          m_chart_safety_atlas_plan_identity;
      SaveConfig();
      wxLogMessage(
          "WR_CHART_ATLAS completion_repaired identity=%s verified_tiles=%llu",
          m_chart_safety_atlas_plan_identity.c_str(),
          static_cast<unsigned long long>(cache_status.present_tiles));
      return;
    }
    m_chart_safety_atlas_tiles = std::move(cache_status.missing_tiles);
    m_chart_safety_atlas_plan_ready = true;
    wxLogMessage(
        "WR_CHART_ATLAS started charts=%llu expected_tiles=%llu "
        "present_tiles=%llu missing_tiles=%llu estimate_mib=%.1f quota_mib=%d",
        static_cast<unsigned long long>(m_chart_safety_atlas_selected_charts),
        static_cast<unsigned long long>(
            m_chart_safety_atlas_coverage_tiles.size()),
        static_cast<unsigned long long>(cache_status.present_tiles),
        static_cast<unsigned long long>(m_chart_safety_atlas_tiles.size()),
        m_chart_safety_atlas_estimate_mib,
        m_chart_safety_atlas_max_disk_mib);
  }

  std::vector<std::pair<long, long>> batch;
  batch.reserve(m_chart_safety_atlas_batch_limit);
  const auto batch_bucket = [](long tile) {
    return static_cast<long>(std::floor(static_cast<double>(tile) / 6.0));
  };
  while (m_chart_safety_atlas_cursor < m_chart_safety_atlas_tiles.size() &&
         batch.size() < m_chart_safety_atlas_batch_limit) {
    const auto tile =
        m_chart_safety_atlas_tiles[m_chart_safety_atlas_cursor];
    if (!batch.empty() &&
        (batch_bucket(tile.first) != batch_bucket(batch.front().first) ||
         batch_bucket(tile.second) != batch_bucket(batch.front().second)))
      break;
    batch.push_back(tile);
    ++m_chart_safety_atlas_cursor;
  }

  PlugInSegmentSafetyOptions options = {};
  options.struct_size = sizeof(options);
  options.check_land = 1;
  options.check_depth = 1;
  options.allow_gshhs_fallback = 0;
  PlugInSegmentSafetyResult result = {};
  result.struct_size = sizeof(result);
  wxStopWatch batch_timer;
  const bool ok = weather_routing::chart_safety_host::PrewarmAtlasTiles(
      batch, &options, &result);
  const long batch_elapsed_ms = batch_timer.Time();
  wxLogMessage(
      "WR_CHART_ATLAS batch_ok=%d batch_tiles=%llu completed=%llu total=%llu "
      "build_ms=%d elapsed_ms=%ld next_batch_limit=%llu",
      ok ? 1 : 0, static_cast<unsigned long long>(batch.size()),
      static_cast<unsigned long long>(m_chart_safety_atlas_cursor),
      static_cast<unsigned long long>(m_chart_safety_atlas_tiles.size()),
      result.grid_build_ms, batch_elapsed_ms,
      static_cast<unsigned long long>(
          weather_routing::NextChartSafetyAtlasBatchSize(
              m_chart_safety_atlas_batch_limit, batch_elapsed_ms,
              result.prewarm_base_tiles_built)));

  m_chart_safety_atlas_batch_limit =
      weather_routing::NextChartSafetyAtlasBatchSize(
          m_chart_safety_atlas_batch_limit, batch_elapsed_ms,
          result.prewarm_base_tiles_built);

  if (!ok && m_chart_safety_atlas_batch_retries < 2) {
    m_chart_safety_atlas_cursor -= batch.size();
    ++m_chart_safety_atlas_batch_retries;
    ScheduleChartSafetyAtlas(
        false, weather_routing::ChartSafetyAtlasBatchDelayMs(
                   batch_elapsed_ms));
    return;
  }
  if (!ok) ++m_chart_safety_atlas_failed_batches;
  m_chart_safety_atlas_batch_retries = 0;

  if (m_chart_safety_atlas_cursor >= m_chart_safety_atlas_tiles.size()) {
    FlushChartSafetyCache();
    const bool completion_committed =
        m_chart_safety_atlas_failed_batches == 0 &&
        m_chart_safety_cache.CommitAtlasCompletion(
            m_chart_safety_atlas_plan_identity,
            m_chart_safety_atlas_coverage_tiles);
    if (completion_committed) {
      m_chart_safety_atlas_completed_identity =
          m_chart_safety_atlas_plan_identity;
      SaveConfig();
    } else if (m_chart_safety_atlas_failed_batches == 0) {
      wxLogWarning(
          "WR_CHART_ATLAS completion_unverified error=%s",
          m_chart_safety_cache.LastError().c_str());
    }
    wxLogMessage(
        "WR_CHART_ATLAS complete built_tiles=%llu expected_tiles=%llu "
        "failed_batches=%llu committed=%d",
                 static_cast<unsigned long long>(
                     m_chart_safety_atlas_tiles.size()),
                 static_cast<unsigned long long>(
                     m_chart_safety_atlas_coverage_tiles.size()),
                 static_cast<unsigned long long>(
                     m_chart_safety_atlas_failed_batches),
                 completion_committed ? 1 : 0);
    if (!completion_committed) {
      // Re-audit after a quiet interval.  The next plan is reconstructed from
      // the durable index, so successful tiles are retained and only the
      // genuinely missing subset is retried.
      ResetChartSafetyAtlasPlan();
      ScheduleChartSafetyAtlas(false, 60000);
    }
    return;
  }
  ScheduleChartSafetyAtlas(
      false,
      weather_routing::ChartSafetyAtlasBatchDelayMs(batch_elapsed_ms));
}

bool weather_routing_pi::ClearChartSafetyCache() {
  weather_routing::chart_safety_host::InvalidateDerivedMasks();
  const bool plugin_ok = m_chart_safety_cache.Clear();
  const bool host_ok =
      weather_routing::chart_safety_host::ClearPersistentCache();
  m_chart_safety_atlas_completed_identity.clear();
  SaveConfig();
  ScheduleChartSafetyAtlas(true, 1000);
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
  // Keep GUI migration tests and headless runs independent of the host API's
  // private-data path (some hosts ignore --configdir for that API).
  wxString testDataDir;
  if (wxGetEnv("WR_HEADLESS_DATA_DIR", &testDataDir) && !testDataDir.empty()) {
    wxFileName directory = wxFileName::DirName(testDataDir);
    if (directory.IsAbsolute()) return directory.GetPathWithSep();
  }
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
