/***************************************************************************
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
 ***************************************************************************/

/***************************************************************************
 * Weather Routing Plugin ? modernized header
 * WeatherRoute  : per?route model
 * WeatherRouting: controller + UI + scheduler
 ***************************************************************************/

#ifndef _WEATHER_ROUTING_H_
#define _WEATHER_ROUTING_H_

// THEN includes
#include <wx/event.h>
#include <wx/thread.h>  // belt-and-suspenders; makes wxThreadEvent visible
#include <wx/collpane.h>
#include <list>
#include <vector>
#include <atomic>
#include <wx/wx.h>
#include <wx/listctrl.h>
#include <wx/timer.h>
#include <wx/filename.h>
#include <wx/datetime.h>
#include <wx/aui/aui.h>
#include <map>


// Forward declarations FIRST
class WRPanel;
class weather_routing_pi;
class RoutingTablePanel;
class WeatherRouting;
class RouteMapPanel;
class AddressSpaceMonitor;
class RouteMapOverlay;
class TiXmlElement;
class Position;
class RoutePoint;


#include "WRPanel.h"
#include "WeatherRoutingUI.h"
#include "RouteMapOverlay.h"
#include "ConfigurationDialog.h"
#include "ConfigurationBatchDialog.h"
#include "BoatDialog.h"
#include "SettingsDialog.h"
#include "StatisticsDialog.h"
#include "ReportDialog.h"
#include "PlotDialog.h"
#include "FilterRoutesDialog.h"
#include "RouteMapPanel.h"



// NOW declare the event
wxDECLARE_EVENT(EVT_ROUTEMAP_UPDATE, wxThreadEvent);


// ---------------------------------------------------------------------------
// Menu IDs (UI wiring)
// ---------------------------------------------------------------------------

enum {
  ID_FILE_OPEN = wxID_HIGHEST + 1,
  ID_FILE_SAVE,
  ID_FILE_SAVEAS,
  ID_FILE_CLOSE,

  ID_POSITION_NEW,
  ID_POSITION_EDIT,
  ID_POSITION_UPDATE_BOAT,
  ID_POSITION_DELETE,
  ID_POSITION_DELETE_ALL,

  ID_ROUTING_NEW,
  ID_ROUTING_BATCH,
  ID_ROUTING_EDIT,
  ID_ROUTING_GOTO,
  ID_ROUTING_DELETE,
  ID_ROUTING_DELETE_ALL,
  ID_ROUTING_COMPUTE,
  ID_ROUTING_COMPUTE_ALL,
  ID_ROUTING_STOP,
  ID_ROUTING_STOP_ALL,
  ID_ROUTING_RESET,
  ID_ROUTING_RESET_ALL,
  ID_ROUTING_SAVE_TRACK,
  ID_ROUTING_SAVE_ALL_TRACKS,
  ID_ROUTING_SAVE_ROUTE,
  ID_ROUTING_EXPORT_GPX,
  ID_ROUTING_FILTER,
  ID_TOGGLE_VISIBILITY,
  ID_GOTO_ROUTING,

  ID_VIEW_SETTINGS,
  ID_VIEW_STATISTICS,
  ID_VIEW_REPORT,
  ID_VIEW_PLOT,
  ID_VIEW_CURSOR_POSITION,
  ID_VIEW_ROUTE_POSITION,
  ID_VIEW_ROUTING_TABLE,
  D_VIEW_ROUTE_POSITION = wxID_HIGHEST + 5000,

  ID_HELP_INFORMATION,
  ID_HELP_MANUAL,
  ID_HELP_ABOUT
};


// ---------------------------------------------------------------------------
// WeatherRoute: model for a single weather route configuration and its state
// ---------------------------------------------------------------------------

// MODERN VERSION FOR LATER
/*
struct WeatherRoute {
  wxString BoatFilename;

  // Start/End names (user-entered)
  wxString Start;
  wxString End;

  // Start/End times
  wxDateTime StartTime;
  wxDateTime EndTime;

  // Route state
  wxString State;
  wxString StateDetail;

  // Modern per-route positions list
  std::vector<WeatherPoint> Positions;

  // Overlay that owns all computed values
  RouteMapOverlay* routemapoverlay = nullptr;
};
*/

// ---------------------------------------------------------------------------
// WeatherPoint: modern per-route position
// ---------------------------------------------------------------------------
    struct WeatherPoint {
  wxString Name;
  double lat = 0.0;
  double lon = 0.0;
};

// ---------------------------------------------------------------------------
// WeatherRoute: unified legacy + modern model
// ---------------------------------------------------------------------------
class WeatherRoute {
public:
  WeatherRoute(WeatherRouting* parent);
  ~WeatherRoute();

  // Controller link
  WeatherRouting* m_parent{nullptr};

  // Overlay
  RouteMapOverlay* routemapoverlay{nullptr};

  // Legacy geometry pointers (still required by dialogs)
  Position* Start{nullptr};
  Position* End{nullptr};

  // Modern per-route positions
  std::vector<WeatherPoint> Positions;

  // User-entered names
  wxString StartName;
  wxString EndName;

  // Times
  wxDateTime StartTime;
  wxDateTime EndTime;

  // Boat
  wxString BoatFilename;

  // State
  int State{0};
  wxString StateDetail;  // state info error messages
  bool Filtered{false};

  // Legacy compatibility  used by WRConfigDialog, etc.
  wxString BoatName;  // legacy name; can mirror BoatFilename if needed
  double StartLat = 0.0;
  double StartLon = 0.0;
  std::list<RoutePoint*> routepoints;   // if old code iterates this

  // Methods
  void Update(WeatherRouting* wr);   // legacy
  void Update(WeatherRouting* wr, bool stateonly);  // new
  void ClearComputedFields();

};

// ---------------------------------------------------------------------------
// WeatherRouting: main controller for plugin state, UI, and routing lifecycle
// ---------------------------------------------------------------------------

class WeatherRouting : public WeatherRoutingBase {
public:
  WeatherRouting(wxWindow* parent, weather_routing_pi& plugin);
  ~WeatherRouting() override;

  // Column model for routing table
  // Column ids used by SettingsDialog, RoutingTablePanel, etc.
  enum Column {
    VISIBLE = 0,
    BOAT,
    STARTTYPE,
    START,
    STARTTIME,
    END,
    ENDTIME,
    TIME,
    DISTANCE,
    AVGSPEED,
    MAXSPEED,
    AVGSPEEDGROUND,
    MAXSPEEDGROUND,
    AVGWIND,
    MAXWIND,
    MAXWINDGUST,
    AVGCURRENT,
    MAXCURRENT,
    AVGSWELL,
    MAXSWELL,
    UPWINDPERCENTAGE,
    PORTSTARBOARD,
    TACKS,
    JIBES,
    SAILPLANCHANGES,
    COMFORT,
    STATE,
    NUM_COLS  // <?? this is the only NUM_COLS you need
  };

  // Column metadata
  static const wxString column_names[NUM_COLS];

  // High-level commands (toolbar/menu)
  void OnCompute(wxCommandEvent& event);
  void OnStop(wxCommandEvent& event);
  void OnResetAll(wxCommandEvent& event);
  void OnRouteMapUpdate(wxThreadEvent& event);

  // Missing event handlers still implemented in WeatherRouting.cpp
  void OnGotoRouting(wxCommandEvent& event);
  void OnNewRouting(wxCommandEvent& event);
  void OnBatchRouting(wxCommandEvent& event);
  void OnResetRouting(wxCommandEvent& event);
  void OnStopRouting(wxCommandEvent& event);
  void OnStopAllRoutings(wxCommandEvent& event);

  void OnEditPositionClick(wxCommandEvent& event);

  void OnWeatherRouteSort(wxListEvent& event);
  void OnWeatherRouteSelected(wxListEvent& event);

//  void OnComputationTimer(wxTimerEvent& event);
  void OnHideConfigurationTimer(wxTimerEvent& event);
//  void OnRenderedTimer(wxTimerEvent& event);   //old
//  void OnCollPaneChanged(wxCollapsiblePaneEvent& event);  //old

  

  // Additional routing helpers
  void ComputeSelectedRoute();
  WeatherRoute* GetSelectedRoute();

  // Modern implementations (formerly inline stubs)
  void UpdateDisplaySettings();
  void UpdateColumns();
  void RebuildList();
  void UpdateBoatFilename(const wxString& filename);
  void GenerateBatch();
  void UpdateCursorPositionDialog();
  std::vector<RouteMapOverlay*> GetSelectedOverlays() const;
//  void AddPosition(double lat, double lon);
//  void AddPosition(double lat, double lon, const wxString& name,
//                   const wxString& guid);
  // old code mostly rewritten
  // but these are still called by WeatherRouting.cpp
  // and expected to exist. This code is still used by ReportDialog,
  // FilterRoutesDialog, and legacy code in WeatherRouting.cpp
  void UpdateConfigurations();
  void UpdateRouteMap(RouteMapOverlay* routemapoverlay);
  RoutePoint* m_positionOnRoute;
  RoutePoint m_savedPosition;

  // newer code??
  void AddRoute(const wxString& name);
  void ScheduleAutoSave();
  void UpdateRoutePositionDialog();
  void CursorRouteChanged();

  // File operations (XML project load/save)
  void OnFileOpen(wxCommandEvent& event);
  void OnFileSave(wxCommandEvent& event);
  void OnFileSaveAs(wxCommandEvent& event);
  void OnFileClose(wxCommandEvent& event);

  bool OpenXML(wxString filename, bool reportfailure = true);
  void SaveXML(wxString filename);
  void AutoSaveXML();
  void OnAutoSaveXMLTimer(wxTimerEvent& event);
  void CopyDataFiles(wxString from, wxString to);

  // Routing actions & lifecycle (modern, no scheduler)
  void ComputeAllRoutes();
  void Stop(RouteMapOverlay* ov);
  void StopAll();
  void StopSelected();
  void Reset(RouteMapOverlay* overlay);
  void ResetAll();
  void ResetSelected();
  void DeleteRouteMaps(const std::list<RouteMapOverlay*>& overlays);
  void DeleteRouteMap(RouteMapOverlay* ov);
//  void WaitForAllRoutesToStop();  // no-op stub in modern architecture

  // Legacy route container (used by ReportDialog, etc.)
  // WeatherRouting owns all WeatherRoute objects, absolutely essential.
  std::list<WeatherRoute*> m_WeatherRoutes;

  // UI update pipeline (states, dialogs, table rows)
  void UpdateStates();
  wxString ComputeStateString(int state) const;
  void UpdateDialogs();
  void UpdateComputeState();
  void UpdateCurrentConfigurations();
  void UpdateRoutePositionDialog(RoutePositionDialog& dlg);
  std::vector<RouteMapOverlay*> GetAllOverlays();

  void UpdateItem(long row, bool refreshState);
  void UpdateAllItems(bool changed);
  void UpdateSelectedItems(bool changed);
  void RefreshUI();

  void SetColumn(long index, int column, const wxString& value);
  void ClearComputedColumns(long index);
  void UpdateStaticColumns(long index, WeatherRoute* wr);
  // void UpdateComputedColumns(long index, WeatherRoute* wr);

  // Routing table management
  void AddRoutingPanel();
  void AddRouteToList(WeatherRoute* wr);
  void RemoveRouteFromList(long index);
  void SelectRouteInList(WeatherRoute* wr);
  void PopulateRoutes();
  long GetRouteRow(WeatherRoute* wr) const;

  // Return first ?current? RouteMapOverlay used by plotting/reporting
  RouteMapOverlay* FirstCurrentRouteMap();

  // Position management
 // void OnNewPosition(wxCommandEvent& event);
 // void OnEditPosition(wxCommandEvent& event);
//  void OnDeletePosition(wxCommandEvent& event);
    void OnDeleteAllPositions(wxCommandEvent& event);

//  void PopulatePositions();
//  void AddPositionRow(size_t index);
//  void UpdatePositionRow(size_t index);

//  void AddPosition_GUID(WeatherRoute* wr, double lat, double lon,
//                        const wxString& name, const wxString& GUID);

  void MarkRouteDirty(WeatherRoute* wr);
  void UpdateBoat();

  // Export & save operations (tracks, routes, GPX)
  void OnSaveAsTrack(wxCommandEvent& event);
  void OnSaveAllAsTracks(wxCommandEvent& event);
  void OnSaveAsRoute(wxCommandEvent& event);
  void OnExportRouteAsGPX(wxCommandEvent& event);

  TiXmlElement* SaveSimplifiedRouteAsGPXFile(
      const RouteMapOverlay& overlay, const std::list<Position*>& simplified);

  // Menu commands (settings, statistics, report, plot, help)
  void OnSettings(wxCommandEvent& event);
  void OnStatistics(wxCommandEvent& event);
  void OnReport(wxCommandEvent& event);
  void OnPlot(wxCommandEvent& event);
  void OnCursorPosition(wxCommandEvent& event);
  void OnRoutePosition(wxCommandEvent& event);
  void OnFilter(wxCommandEvent& event);

  void OnInformation(wxCommandEvent& event);
  void OnManual(wxCommandEvent& event);
  void OnAbout(wxCommandEvent& event);

  // Visibility & selection
  void OnToggleVisibility(wxCommandEvent& event);
  void OnRouteSelected(wxListEvent& event);
//  void OnPositionSelected(wxListEvent& event);

  // Sorting
  void OnSortColumn(wxListEvent& event);

  // Mouse & interaction events
  void OnLeftDown(wxMouseEvent& event);
  void OnLeftUp(wxMouseEvent& event);
  void OnDownTimer(wxTimerEvent& event);
  void OnRightUp(wxMouseEvent& event);

  // Rendering & viewport integration
  void Render(piDC& dc, PlugIn_ViewPort& vp);


  // User settings & column management
  void SaveColumnWidth(int col, int width);
  int LoadColumnWidth(int col) const;

  // Dialog accessors & plugin hook
  ConfigurationDialog& GetConfigurationDialog() {
    return m_ConfigurationDialog;
  }
  SettingsDialog& GetSettingsDialog() { return m_SettingsDialog; }
  BoatDialog& GetBoatDialog() { return m_BoatDialog; }
  weather_routing_pi& GetPlugin() { return m_weather_routing_pi; }

 RouteMapOverlay*& RouteMapOverlayNeedingGrib();

 //Older updated during refactor, but still used by WeatherRouting.cpp and
 //ReportDialog, etc.WeatherRoute *GetWeatherRouteForOverlay(RouteMapOverlay* overlay);
 void OnOpen(wxCommandEvent& event);
 void OnSave(wxCommandEvent& event);
 void OnSaveAs(wxCommandEvent& event);
 void OnClose(wxCommandEvent& event);
 void OnNew(wxCommandEvent& event);
 // void OnSize(wxSizeEvent& event);  // old
 void OnResetSelected(wxCommandEvent& event);
 void OnComputeAll(wxCommandEvent& event);
 void OnWeatherRoutesListLeftDown(wxMouseEvent& event);

 // Needed by legacy code in WeatherRouting.cpp, ReportDialog, etc.
 void OnDelete(wxCommandEvent& event);
 void OnGoTo(wxCommandEvent& event);
 void OnClose(wxCloseEvent& event);
 void OnWeatherRouteKeyDown(wxListEvent& event);
 double ComputeRouteDistance(const Position* dest);
 bool AddConfiguration(RouteMapConfiguration& configuration);

 // These are still called by legacy code in WeatherRouting.cpp and expected to
 // exist, but are now implemented in the modern style.
 // Configuration / settings
 void OnEditConfiguration();                          // internal helper
 void OnEditConfiguration(wxCommandEvent& event);     // menu/command wrapper
 void OnEditConfigurationClick(wxMouseEvent& event);  // mouse wrapper

 // These are still called by legacy code in WeatherRouting.cpp and expected to
 // exist, but are now implemented in the modern style.
 // Boat position update
 void OnUpdateBoatPosition(wxCommandEvent& event);  // menu/command wrapper
 void OnUpdateBoat(wxCommandEvent& event);          // core implementation


 // Legacy menu / UI handlers still implemented in WeatherRouting.cpp
 void OnBatch(wxCommandEvent& event) override;
 void OnWeatherTable(wxCommandEvent& event) override;
 void OnDefaultConfiguration(wxCommandEvent& event);
 void OnDeleteSelected(wxCommandEvent& event);
 void OnDeleteAll(wxCommandEvent& event);
 void RemoveSelectedRoutes();
 void RemoveAllRoutes();
 void SaveAsTrack(RouteMapOverlay& routemapoverlay);
 void SaveAsRoute(RouteMapOverlay& routemapoverlay);
 RouteMapConfiguration DefaultConfiguration();

 
 struct SaveRouteOptions {
   bool dialogAccepted = false;
   bool simplifyRoute = false;
   double maxTimePenalty = 0.0;  // expressed as fraction (0.05 = 5%)
 };
 SaveRouteOptions ShowRouteSaveOptionsDialog();

 


private:


  // Legacy members still required by WeatherRouting.cpp
  RoutingTablePanel* m_RoutingTablePanel{nullptr};
  bool m_disable_colpane{false};
  bool m_shuttingDown{false};

  // RouteMapOverlay container
  WRPanel* m_panel{nullptr};

  std::list<RouteMapOverlay*> m_RouteMapOverlays;
  std::map<int, int> m_ColumnWidths;

  wxMutex m_OverlayListMutex;

  // GRIB request coordination
  RouteMapOverlay* m_RouteMapOverlayNeedingGrib{nullptr};

  // Panels & core UI widgets
  // WeatherRoutingPositionPanel* m_PositionPanel{nullptr};
  RouteMapPanel* m_RouteMapPanel{nullptr};
  RouteMapOverlay* m_pRouteMapOverlay{nullptr};
  weather_routing_pi& m_weather_routing_pi;

  wxWindow* m_colpaneWindow{nullptr};
  wxCollapsiblePane* m_colpane{nullptr};

    // Button row
  wxButton* m_btnCompute{nullptr};
  wxButton* m_btnSaveTrack{nullptr};
  wxButton* m_btnSaveRoute{nullptr};
  wxButton* m_btnExportGPX{nullptr};
  wxButton* m_btnReset{nullptr};
  wxButton* m_btnGotoRouting{nullptr};
  wxString m_default_configuration_path{""};

  // Timers (scheduler timer removed)
  wxTimer m_tHideConfiguration;
  wxTimer m_tDownTimer;
  wxTimer m_tAutoSaveXML;

  // Dialog instances
  ConfigurationDialog m_ConfigurationDialog;
  ConfigurationBatchDialog m_ConfigurationBatchDialog;
  CursorPositionDialog m_CursorPositionDialog;
  RoutePositionDialog m_RoutePositionDialog;
  BoatDialog m_BoatDialog;
  SettingsDialog m_SettingsDialog;
  StatisticsDialog m_StatisticsDialog;
  ReportDialog m_ReportDialog;
  PlotDialog m_PlotDialog;
  FilterRoutesDialog m_FilterRoutesDialog;

  // Dialog visibility flags
  bool m_bShowConfiguration{false};
  bool m_bShowConfigurationBatch{false};
  bool m_bShowRoutePosition{false};
  bool m_bShowSettings{false};
  bool m_bShowStatistics{false};
  bool m_bShowReport{false};
  bool m_bShowPlot{false};
  bool m_bShowFilter{false};

  
  // Mouse state (drag / long-press)
  wxPoint m_downPos;
  wxPoint m_startPos;
  wxPoint m_startMouse;

  //Columns
  int columns[NUM_COLS];


  // Runtime tracking in
  // used for auto-saving, and to display route computation time in the UI
  // StatisticsDialog also uses this to display the total time taken for route
  // calculations.
  wxTimeSpan m_RunTime;


  // Window state
  wxSize m_size;
  wxFileName m_FileName;

#ifdef __WXMSW__
  // Address space monitoring and compute gating
  AddressSpaceMonitor* m_addressSpaceMonitor{nullptr};
  std::atomic<bool> m_disableNewComputations{false};

  void DisableNewComputations() { m_disableNewComputations.store(true); }
  void EnableNewComputations() { m_disableNewComputations.store(false); }
  bool AreNewComputationsDisabled() const {
    return m_disableNewComputations.load();
  }
#endif

  // Initialization & invariants
  void LoadConfigurationAndData();
  void InitializeUI();
  void BindEvents();
  void AssertAllInvariants();
  void AssertSchedulerInvariants();
  void AssertThreadLifecycleInvariants();


  // Memory alert handlers
  void OnMemoryAlertStop(wxCommandEvent& event);
  void OnMemoryAutoReset(wxCommandEvent& event);
};

#endif  // _WEATHER_ROUTING_H_
