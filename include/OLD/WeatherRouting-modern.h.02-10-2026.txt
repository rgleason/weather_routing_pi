/******************************************************************************
 *  WeatherRouting.h — Modernized Architecture (2026)
 *
 *  Canonical two‑panel UI:
 *      • RouteMapPanel     → m_lWeatherRoutes
 *      • PositionPanel     → m_lPositions
 *
 *  Clean separation of:
 *      • UI dialogs
 *      • Routing table column system
 *      • Overlay model + compute scheduler
 *      • Route cursor + routing table panel
 *      • Batch, save, export, and configuration subsystems
 *
 *****************************************************************************/

#ifndef _WEATHER_ROUTING_H_
#define _WEATHER_ROUTING_H_

#include <wx/wx.h>
#include <wx/listctrl.h>
#include <wx/filename.h>
#include <wx/timer.h>
#include <wx/datetime.h>
#include <wx/dir.h>
#include <wx/mutex.h>

#include "RouteMapOverlay.h"
#include "WeatherRoute.h"
#include "Dialogs/ConfigurationDialog.h"
#include "Dialogs/ConfigurationBatchDialog.h"
#include "Dialogs/CursorPositionDialog.h"
#include "Dialogs/RoutePositionDialog.h"
#include "Dialogs/BoatDialog.h"
#include "Dialogs/SettingsDialog.h"
#include "Dialogs/StatisticsDialog.h"
#include "Dialogs/ReportDialog.h"
#include "Dialogs/PlotDialog.h"
#include "Dialogs/FilterRoutesDialog.h"
#include "Panels/RouteMapPanel.h"
#include "Panels/PositionPanel.h"
#include "Panels/RoutingTablePanel.h"

class WeatherRoutingBase;

class WeatherRouting : public WeatherRoutingBase {
public:
    WeatherRouting(wxWindow* parent);
    virtual ~WeatherRouting();

    /* ============================================================
     *  PUBLIC API (called by plugin)
     * ============================================================ */
    void RefreshUI();
    void UpdateDialogs();
    void UpdateComputeState();
    void UpdateCurrentConfigurations();
    void UpdateAllItems(bool changed);
    void UpdateSelectedItems(bool changed);
    void UpdateRouteMap(RouteMapOverlay* ov);

    RouteMapOverlay* FirstCurrentRouteMap();

    /* ============================================================
     *  ROUTE SAVING
     * ============================================================ */
    struct SaveRouteOptions {
        bool dialogAccepted;
        bool simplifyRoute;
        double maxTimePenalty;
    };

    SaveRouteOptions ShowRouteSaveOptionsDialog();
    void SaveAsTrack(RouteMapOverlay& ov);
    void SaveAsRoute(RouteMapOverlay& ov);
    void SaveSimplifiedRoute(RouteMapOverlay& ov,
                             const std::list<Position*>& simplifiedRoute);

    /* ============================================================
     *  POSITION MANAGEMENT
     * ============================================================ */
    void AddPosition(double lat, double lon);
    void AddPosition(double lat, double lon, wxString name, bool suppress_prompt);
    void AddPosition(double lat, double lon, wxString name, wxString GUID);

    void OnEditPosition();
    void OnDeletePosition(wxCommandEvent& event);
    void OnDeleteAllPositions(wxCommandEvent& event);

    /* ============================================================
     *  ROUTE MANAGEMENT
     * ============================================================ */
    void AddRoute(wxString& GUID);
    void AddRouteToList(WeatherRoute* wr);
    void RemoveRouteFromList(long index);
    void RemoveSelectedRoutes();
    void RemoveAllRoutes();

    void SelectRouteInList(WeatherRoute* wr);

    /* ============================================================
     *  COMPUTE SCHEDULER
     * ============================================================ */
    void StartAll();
    void StopAll();
    void ResetAll();
    void ResetSelected();
    void ResetSelectedRoutes();

    void Stop(RouteMapOverlay* ov);
    void Reset(RouteMapOverlay* ov);

    void WaitForAllRoutesToStop();
    void WaitForRoutesToStop(const std::list<RouteMapOverlay*>& overlays);

    void OnComputationTimer(wxTimerEvent& event);
    void OnHideConfigurationTimer(wxTimerEvent& event);

    /* ============================================================
     *  BATCH ROUTING
     * ============================================================ */
    void GenerateBatch();
    void DeleteRouteMaps(const std::list<RouteMapOverlay*>& overlays);

    /* ============================================================
     *  UI OVERRIDES
     * ============================================================ */
    bool Show(bool show) override;

    /* ============================================================
     *  FILE UTILITIES
     * ============================================================ */
    void CopyDataFiles(wxString from, wxString to);

private:

/* ======================================================================
 *  1. MODERN CHILD PANELS
 * ====================================================================== */
    RouteMapPanel*   m_RouteMapPanel;     // owns m_lWeatherRoutes
    PositionPanel*   m_PositionPanel;     // owns m_lPositions

/* ======================================================================
 *  2. DIALOGS
 * ====================================================================== */
    ConfigurationDialog        m_ConfigurationDialog;
    ConfigurationBatchDialog   m_ConfigurationBatchDialog;
    CursorPositionDialog       m_CursorPositionDialog;
    RoutePositionDialog        m_RoutePositionDialog;
    BoatDialog                 m_BoatDialog;
    SettingsDialog             m_SettingsDialog;
    StatisticsDialog           m_StatisticsDialog;
    ReportDialog               m_ReportDialog;
    PlotDialog                 m_PlotDialog;
    FilterRoutesDialog         m_FilterRoutesDialog;

/* ======================================================================
 *  3. UI VISIBILITY FLAGS
 * ====================================================================== */
    bool m_bShowConfiguration;
    bool m_bShowConfigurationBatch;
    bool m_bShowRoutePosition;
    bool m_bShowSettings;
    bool m_bShowStatistics;
    bool m_bShowReport;
    bool m_bShowPlot;
    bool m_bShowFilter;

/* ======================================================================
 *  4. ROUTE TABLE COLUMN SYSTEM
 * ====================================================================== */
public:
    enum {
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
        NUM_COLS
    };

    long columns[NUM_COLS];
    static const wxString column_names[NUM_COLS];

    enum {
        POSITION_NAME = 0,
        POSITION_LAT,
        POSITION_LON
    };

    void SetColumn(long index, int col, const wxString& value);
    void ClearComputedColumns(long index);
    void UpdateStaticColumns(long index, WeatherRoute* wr);
    void UpdateComputedColumns(long index, WeatherRoute* wr);

/* ======================================================================
 *  5. OVERLAY MODEL + CURSOR STATE
 * ====================================================================== */
    wxMutex m_OverlayListMutex;
    std::vector<RouteMapOverlay*> m_RouteMapOverlays;

    RoutePoint* m_positionOnRoute;
    RoutePoint  m_savedPosition;

    RoutingTablePanel* m_RoutingTablePanel;

/* ======================================================================
 *  6. COMPUTE SCHEDULER STATE
 * ====================================================================== */
    wxTimer m_tCompute;
    wxTimer m_tHideConfiguration;
    wxTimer m_tDownTimer;

    bool        m_bRunning;
    wxTimeSpan  m_RunTime;
    wxDateTime  m_StartTime;

    int         m_RoutesToRun;
    bool        m_bSkipUpdateCurrentItems;

    wxString    m_default_configuration_path;

/* ======================================================================
 *  7. MOUSE + WINDOW STATE
 * ====================================================================== */
    wxPoint     m_downPos;
    wxPoint     m_startPos;
    wxPoint     m_startMouse;
    wxSize      m_size;
    wxFileName  m_FileName;

/* ======================================================================
 *  8. COLUMN WIDTH PERSISTENCE
 * ====================================================================== */
    void SaveColumnWidth(int col, int width);
    int  LoadColumnWidth(int col) const;
};

#endif // _WEATHER_ROUTING_H_
