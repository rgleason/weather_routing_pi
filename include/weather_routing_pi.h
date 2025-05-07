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
  static wxString StandardPath();
  void ShowMenuItems(bool show);

  wxWindow* GetParentWindow() { return m_parent_window; }

  double m_boat_lat;    //!< Latitude of the boat position, in degrees.
  double m_boat_lon;    //!< Longitude of the boat position, in degrees.
  double m_cursor_lat;  //!< Latitude of the cursor position, in degrees.
  double m_cursor_lon;  //!< Longitude of the cursor position, in degrees.

private:
  void OnCursorLatLonTimer(wxTimerEvent&);
  void RequestOcpnDrawSetting();
  void NewWR();

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

#endif

#endif
