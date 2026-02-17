/***************************************************************************
 *   Copyright (C) 2015 by Sean D'Epagnier                                 *
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
 **************************************************************************/

#ifndef _WEATHER_ROUTING_PI_H_
#define _WEATHER_ROUTING_PI_H_

#ifdef DEBUG_BUILD
#define DEBUGSL(x)                                 \
  do {                                             \
    time_t now = time(0);                          \
    tm* localtm = localtime(&now);                 \
    char* stime = asctime(localtm);                \
    stime[strlen(stime) - 1] = 0;                  \
    std::cout << stime << " : " << x << std::endl; \
  } while (0)

#define DEBUGST(x)                    \
  do {                                \
    time_t now = time(0);             \
    tm* localtm = localtime(&now);    \
    char* stime = asctime(localtm);   \
    stime[strlen(stime) - 1] = 0;     \
    std::cout << stime << " : " << x; \
  } while (0)

#define DEBUGCONT(x) \
  do {               \
    std::cout << x;  \
  } while (0)

#define DEBUGEND(x)              \
  do {                           \
    std::cout << x << std::endl; \
  } while (0)
#else
#define DEBUGSL(x) \
  do {             \
  } while (0)
#define DEBUGST(x) \
  do {             \
  } while (0)
#define DEBUGCONT(x) \
  do {               \
  } while (0)
#define DEBUGEND(x) \
  do {              \
  } while (0)
#endif

#ifndef _WEATHER_ROUTINGPI_H_
#define _WEATHER_ROUTINGPI_H_

// #ifndef __OCPN__ANDROID__
#define GetDateCtrlValue GetValue
#define GetTimeCtrlValue GetValue
// #endif



#include "version.h"

#define ABOUT_AUTHOR_URL "http://seandepagnier.users.sourceforge.net"

#include "ocpn_plugin.h"
#include "pidc.h"
#include "qtstylesheet.h"

/* make some warnings go away */
#ifdef MIN
#undef MIN
#endif

#ifdef MAX
#undef MAX
#endif

#include <json/json.h>

#ifdef __WXMSW__
#include "AddressSpaceMonitor.h"
#endif




// In WeatherRouting.h, before class WeatherRouting: class TiXmlElement;
class TiXmlElement;

//----------------------------------------------------------------------------------------------------------
//    The PlugIn Class Definition
//----------------------------------------------------------------------------------------------------------

#define WEATHER_ROUTING_TOOL_POSITION \
  -1  // Request default positioning of toolbar tool

class WeatherRouting;

/**
 * OpenCPN Weather Routing plugin main class.
 *
 * Implements the OpenCPN Weather Routing plugin that provides
 * weather routing capabilities to OpenCPN. It handles initialization,
 * UI management, and interactions with the OpenCPN application.
 */

class weather_routing_pi : public wxEvtHandler, public opencpn_plugin_118 {
public:
  weather_routing_pi(void* ppimgr);
  ~weather_routing_pi();

  int Init();
  bool DeInit();

  int GetAPIVersionMajor();
  int GetAPIVersionMinor();
  int GetPlugInVersionMajor();
  int GetPlugInVersionMinor();
  int GetPlugInVersionPatch();
  int GetPlugInVersionPost();

  wxBitmap* GetPlugInBitmap();
  wxString GetCommonName();
  wxString GetShortDescription();
  wxString GetLongDescription();
  // from Shipdriver for definition of panel icon
  wxBitmap m_panelBitmap;

  bool InBoundary(double lat, double lon);

  bool RenderOverlay(wxDC& dc, PlugIn_ViewPort* vp);
  bool RenderGLOverlay(wxGLContext* pcontext, PlugIn_ViewPort* vp);

  void SetDefaults();

  int GetToolbarToolCount();

  /**
   * Receives cursor lat/lon position updates.
   *
   * @param lat Latitude of the cursor.
   * @param lon Longitude of the cursor.
   */
  void SetCursorLatLon(double lat, double lon);

  void SetPluginMessage(wxString& message_id, wxString& message_body);
  /**
   * Handle position fix information (boat position).
   *
   * @param pfix Position fix information.
   */
  void SetPositionFixEx(PlugIn_Position_Fix_Ex& pfix);
  void ShowPreferencesDialog(wxWindow* parent);

  void OnToolbarToolCallback(int id);
  void OnContextMenuItemCallback(int id);

  void SetColorScheme(PI_ColorScheme cs);
  /**
   * Gets the plugin's private data directory path.
   *
   * Creates and returns the path where the Weather Routing plugin should store
   * its private configuration and data files. The path is constructed as:
   * [OpenCPN private data location]/plugins/weather_routing/
   *AddressSpaceMonitor& GetAddressSpaceMonitor
   * Key behaviors:
   * - Creates the directory structure if it doesn't exist
   * - Always returns a path ending with a path separator
   * - Ensures the returned path is user-writable
   *
   * @return A wxString containing the absolute path to the plugin's data
   * directory, always ending with a path separator
   */
  static wxString StandardPath();
  void ShowMenuItems(bool show);

   /**
   * @brief Gets the parent window for plugin dialogs.
   * @return Pointer to the OpenCPN canvas window.
   */
  wxWindow* GetParentWindow() { return m_parent_window; }

  // ========== Position Tracking ==========

  double m_boat_lat;    //!< Latitude of the boat position, in degrees.
  double m_boat_lon;    //!< Longitude of the boat position, in degrees.
  double m_cursor_lat;  //!< Latitude of the cursor position, in degrees.
  double m_cursor_lon;  //!< Longitude of the cursor position, in degrees.

#ifdef __WXMSW__
  /**
   * @brief Gets pointer to the address space monitor (Windows only).
   *
   * @return Pointer to the AddressSpaceMonitor instance, or nullptr if not
   * available.
   *
   * @details Provides access to the plugin's AddressSpaceMonitor for use by
   * SettingsDialog. Callers must check for nullptr before use.
   */
  AddressSpaceMonitor* GetAddressSpaceMonitor() {
    return m_addressSpaceMonitor.get();
  }
#endif

private:
  void OnCursorLatLonTimer(wxTimerEvent&);
  void RequestOcpnDrawSetting();
  void NewWR();

#ifdef __WXMSW__
  #include <memory>
  std::unique_ptr<AddressSpaceMonitor> m_addressSpaceMonitor;  // Windows-only: Address space monitoring
  wxTimer m_addressSpaceTimer;                                 // 5-second timer for monitoring
  /**
   * Timer callback for address space monitoring.
   * Called every 5 seconds to check memory usage and display alerts if needed.
   */
  void OnAddressSpaceTimer(wxTimerEvent& event);  // <-- Only declare ONCE here!
  void ReassignAddressSpaceMonitor();
#endif

  /**
   * Warns the user if a plugin version is outside the supported range.
   * @param  plugin_name Name of the plugin to display.
   * @param version_major Major version of the plugin.
   * @param version_minor Minor version of the plugin.
   * @param min_major Minimum supported major version.
   * @param min_minor Minimum supported minor version.
   * @param max_major Maximum supported major version (default 100, 
   *  i.e. effectively none, for backwards compatible plugins).
   * @param max_minor Maximum supported minor version (default 100).
   * @param consequence_msg Message to display about potential consequences of 
   *  using an unsupported version (default "otherwise unexpected results may occur").
   * @param recommended_version_msg_prefix Prefix for the recommended version message 
   *  (default "Use versions").
   * @param version_msg_suffix Suffix for the version message 
   *  (default "is not officially supported.").
   * @return true if a warning was shown, false otherwise.
   */
  bool WarnAboutPluginVersion(
    const std::string plugin_name,
    int version_major, int version_minor, 
    int min_major, int min_minor, 
    int max_major = 100, int max_minor = 100,
    const std::string consequence_msg = _(", otherwise unexpected results may occur.").ToStdString(),
    const std::string recommended_version_msg_prefix = _("Use versions").ToStdString(), 
    const std::string version_msg_suffix = _("is not officially supported.").ToStdString());

  bool LoadConfig();
  bool SaveConfig();

  bool b_in_boundary_reply;

  wxFileConfig* m_pconfig;
  wxWindow* m_parent_window;

  WeatherRouting* m_pWeather_Routing;
  wxDateTime m_GribTime;

  int m_display_width, m_display_height;
  int m_leftclick_tool_id;
  int m_position_menu_id;
  int m_waypoint_menu_id;
  int m_route_menu_id;

  wxTimer m_tCursorLatLon;

};
#endif // _WEATHER_ROUTING_PI_H_
#endif // _WEATHER_ROUTING_PI_H_
