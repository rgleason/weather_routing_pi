/***************************************************************************
 *   Copyright (C) 2024 by OpenCPN development team                        *
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 2 of the License, or     *
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
 *   59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.             *
 **************************************************************************/

#ifndef _WEATHER_ROUTING_MOCK_PLUGIN_API_H_
#define _WEATHER_ROUTING_MOCK_PLUGIN_API_H_

#include "ocpn_plugin.h"
#include <vector>
#include <wx/string.h>

// typedef wxWeakRef<Shared_GribRecordSet> Shared_GribRecordSetRef;

// Functions to access mock state for NMEA sentence tracking
void ClearNMEASentences();
const std::vector<wxString>& GetNMEASentences();

// Base mock plugin class implementing all virtual functions with empty
// implementations
class mock_plugin_base : public opencpn_plugin_118 {
public:
  mock_plugin_base(void* ppimgr) : opencpn_plugin_118(ppimgr) {}

  // double DistGreatCircle_Plugin(double slat, double slon,
  //                                                 double dlat, double dlon) { return 1.0; }

  void SetDefaults() override {}
  bool RenderOverlay(wxMemoryDC* dc, PlugIn_ViewPort* vp) override {
    return true;
  }
  void SetPositionFix(PlugIn_Position_Fix& pfix) override {}
  void SetCursorLatLon(double lat, double lon) override {}
  void SetupToolboxPanel(int page_sel, wxNotebook* pnotebook) override {}
  void SetCurrentViewPort(PlugIn_ViewPort& vp) override {}
  void OnCloseToolboxPanel(int page_sel, int ok_apply_cancel) override {}
  void ProcessParentResize(int x, int y) override {}
  int GetToolboxPanelCount(void) override { return 0; }

  // Changed return type to wxArrayString
  wxArrayString GetDynamicChartClassNameArray(void) override {
    return wxArrayString();
  }

  // Changed return types to bool
  bool RenderOverlay(wxDC& dc, PlugIn_ViewPort* vp) override { return true; }
  bool RenderGLOverlay(wxGLContext* pcontext, PlugIn_ViewPort* vp) override {
    return true;
  }

  void SetPluginMessage(wxString& message_id, wxString& message_body) override {
  }
  void SetPositionFixEx(PlugIn_Position_Fix_Ex& pfix) override {}
  void OnSetupOptions() override {}
  void LateInit() override {}

  // Changed return type to bool
  bool MouseEventHook(wxMouseEvent& event) override { return false; }

  void SendVectorChartObjectInfo(wxString& chart, wxString& feature,
                                 wxString& objname, double lat, double lon,
                                 double scale, int nativescale) override {}

  // Changed return type to bool
  bool KeyboardEventHook(wxKeyEvent& event) override { return false; }

  void OnToolbarToolUpCallback(int id) override {}
  void OnToolbarToolDownCallback(int id) override {}
  void PrepareContextMenu(int id) override {}
  void SetActiveLegInfo(Plugin_Active_Leg_Info& leg_info) override {}

  // Changed return type to bool
  bool RenderOverlayMultiCanvas(wxDC& dc, PlugIn_ViewPort* vp, int canvasIndex,
                                int defaultCanvasIndex) override {
    return true;
  }
  bool RenderGLOverlayMultiCanvas(wxGLContext* pcontext, PlugIn_ViewPort* vp,
                                  int canvasIndex,
                                  int defaultCanvasIndex) override {
    return true;
  }
};

#endif  // _WEATHER_ROUTING_MOCK_PLUGIN_API_H_