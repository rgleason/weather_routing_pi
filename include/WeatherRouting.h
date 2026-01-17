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

#ifndef _WEATHER_ROUTING_H_
#define _WEATHER_ROUTING_H_

#include <wx/treectrl.h>
#include <wx/fileconf.h>
#include <wx/collpane.h>

#ifdef __WXMSW__
    #include <atomic>
#endif  

#ifdef __OCPN__ANDROID__
#include <wx/qt/private/wxQtGesture.h>
#endif
#include "ocpn_plugin.h"

#include "WeatherRoutingUI.h"
#include "ConfigurationDialog.h"
#include "ConfigurationBatchDialog.h"
#include "BoatDialog.h"
#include "SettingsDialog.h"
#include "StatisticsDialog.h"
#include "ReportDialog.h"
#include "PlotDialog.h"
#include "FilterRoutesDialog.h"
#include "RoutingTablePanel.h"

class weather_routing_pi;
class WeatherRouting;

/**
 * Class representing a weather routing configuration and its associated route.
 *
 * This class serves as a UI-focused wrapper that combines both configuration
 * settings and calculation results. While RouteMapConfiguration stores the raw
 * routing parameters and RouteMapOverlay handles the actual route computation
 * and display, WeatherRoute maintains the human-readable representation of
 * route data for display in the UI.
 *
 * The class stores formatted strings for distance, speed, weather conditions,
 * and other metrics rather than raw numerical values. It works closely with the
 * WeatherRouting dialog to present route information in a user-friendly format.
 *
 * @see RouteMapConfiguration For the underlying route parameters and settings
 * @see RouteMapOverlay For the actual route calculation and display
 * functionality
 * @see WeatherRouting For the main routing interface that manages these routes
 */
class WeatherRoute {
public:
  WeatherRoute();
  ~WeatherRoute();

  /**
   * Updates the weather route object with current configuration and calculation
   * status.
   *
   * This method updates both the route configuration data and status
   * information. It's used whenever route information needs to be refreshed in
   * the UI.
   *
   * @param wr Pointer to the WeatherRouting main object to access settings.
   * @param stateonly If true, only update the State field, not configuration
   * data.
   */

  void Update(WeatherRouting* wr, bool stateonly = false);

  /** Flag indicating if this route is filtered out in the UI display. */
  bool Filtered;

  /** Path to the boat characteristics file used for this route. */
  wxString BoatFilename;

  /** Starting position name/coordinates. */
  wxString Start;

  /** Specifies whether to set the StartTime to the current computer time at the
   * start of the calculation. */
  wxString UseCurrentTime;

  /** Starting position type (boat or named). */
  wxString StartType;

  /** Departure time for the route. */
  wxString StartTime;

  /** Destination position name/coordinates. */
  wxString End;

  /** Estimated arrival time at destination. */
  wxString EndTime;

  /** Total route duration. */
  wxString Time;

  /** Total route distance in nautical miles. */
  wxString Distance;

  /** Average boat speed through water in knots. */
  wxString AvgSpeed;

  /** Maximum boat speed through water in knots. */
  wxString MaxSpeed;

  /** Average boat speed over ground including currents in knots. */
  wxString AvgSpeedGround;

  /** Maximum boat speed over ground in knots. */
  wxString MaxSpeedGround;

  /** Average wind speed encountered in knots. */
  wxString AvgWind;

  /** Maximum sustained wind speed encountered in knots. */
  wxString MaxWind;

  /** Maximum wind gust encountered in knots. */
  wxString MaxWindGust;

  /** Average current speed encountered in knots. */
  wxString AvgCurrent;

  /** Maximum current speed encountered in knots. */
  wxString MaxCurrent;

  /** Average swell height encountered in meters. */
  wxString AvgSwell;

  /** Maximum swell height encountered in meters. */
  wxString MaxSwell;

  /** Percentage of time spent sailing upwind. */
  wxString UpwindPercentage;

  /** Distribution between port and starboard tacks. */
  wxString PortStarboard;

  /** Number of tacks performed. */
  wxString Tacks;
  /** Number of jibes performed */
  wxString Jibes;
  /** Number of sail plan changes performed. */
  wxString SailPlanChanges;

  /** Current computation state of the route. */
  wxString State;

  /** Comfort/safety metrics for the route conditions. */
  wxString Comfort;

  /** Pointer to the actual route calculation and display overlay. */
  RouteMapOverlay* routemapoverlay;
};

/**
 * Class that handles the main Weather Routing functionality.
 *
 * WeatherRouting provides the main dialog interface for the Weather Routing
 * plugin, allowing configuration, calculation, display, and export of optimal
 * sailing routes based on weather data, boat polar performance, and routing
 * constraints.
 *
 * This class manages multiple route calculations, configuration UI, position
 * management, and visualization of routes. It coordinates the interaction
 * between various dialogs including configuration, batch processing,
 * statistics, and reporting. It also handles file operations for saving and
 * loading routing configurations.
 *
 * The class serves as the central coordinator between the following components:
 * - Route map overlays (RouteMapOverlay) - The actual route computation
 * - Weather routes (WeatherRoute) - The UI representation of routes
 * - Positions (RouteMapPosition) - Start/end locations for routes
 * - Dialog interfaces - For configuration and display of route information
 *
 * Weather routing calculations are performed in background threads managed by
 * this class. The routing data can be exported to OpenCPN routes and displayed
 * on the chart.
 *
 * @see WeatherRoute For the UI data model for routes
 * @see RouteMapOverlay For the route calculation engine and display
 * @see RouteMapConfiguration For the routing parameters
 */
class WeatherRouting : public WeatherRoutingBase {
  friend class AddressSpaceMonitor;  // Allow AddressSpaceMonitor to access to
                                     // private members // private members
private:
  bool m_disable_colpane;
  wxCollapsiblePane* m_colpane;
  wxWindow* m_colpaneWindow;
  WeatherRoutingPanel* m_panel;
  /** Timer for auto-saving positions and routes. */
  wxTimer m_tAutoSaveXML;
  /** Menu item for resetting selected routes - Move to UI */
// Disable Reset Selected for now
//  wxMenuItem* m_mResetSelected = nullptr;
//  void OnResetSelected(wxCommandEvent& event);

public:
  /**
   * Structure to store route saving options selected by the user
   */

  void StopAll();  /// Stop all route calculations AutoStop

  struct SaveRouteOptions {
    bool dialogAccepted;        //!< Whether the user confirmed the dialog
    bool simplifyRoute;         //!< Whether to simplify the route
    double maxDurationPenalty;  //!< Maximum permitted ETA loss (percent)
  };

  enum {
    POSITION_NAME = 0,  //!< Position identifier/name
    POSITION_LAT,       //!< Latitude coordinate
    POSITION_LON        //!< Longitude coordinate
  };

  enum {
    VISIBLE = 0,        //!< Route visibility toggle state
    BOAT,               //!< Boat configuration file name
    START_TYPE,         //!< Starting position type (boat or named)
    START,              //!< Starting position name/coordinates
    STARTTIME,          //!< Route departure time
    END,                //!< Destination position name/coordinates
    ENDTIME,            //!< Estimated arrival time
    TIME,               //!< Total route duration
    DISTANCE,           //!< Total route distance in nautical miles
    AVGSPEED,           //!< Average boat speed through water in knots
    MAXSPEED,           //!< Maximum boat speed through water in knots
    AVGSPEEDGROUND,     //!< Average boat speed over ground in knots
    MAXSPEEDGROUND,     //!< Maximum boat speed over ground in knots
    AVGWIND,            //!< Average wind speed encountered in knots
    MAXWIND,            //!< Maximum sustained wind speed in knots
    MAXWINDGUST,        //!< Maximum wind gust encountered in knots
    AVGCURRENT,         //!< Average current speed encountered in knots
    MAXCURRENT,         //!< Maximum current speed encountered in knots
    AVGSWELL,           //!< Average swell height encountered in meters
    MAXSWELL,           //!< Maximum swell height encountered in meters
    UPWIND_PERCENTAGE,  //!< Percentage of time spent sailing upwind
    PORT_STARBOARD,     //!< Distribution between port and starboard tacks
    TACKS,              //!< Number of tacks performed
    JIBES,              //!< Number of jibes performed
    SAIL_PLAN_CHANGES,  //!< Number of sail plan changes performed
    COMFORT,            //!< Comfort/safety metrics for conditions
    STATE,              //!< Current computation state of route
    NUM_COLS            //!< Total number of display columns
  };
  long columns[NUM_COLS];
  static const wxString column_names[NUM_COLS];
  int sashpos;

  WeatherRouting(wxWindow* parent, weather_routing_pi& plugin);
  ~WeatherRouting();

#ifdef __OCPN__ANDROID__
  void OnEvtPanGesture(wxQT_PanGestureEvent& event);
#endif
  void OnLeftDown(wxMouseEvent& event);
  void OnLeftUp(wxMouseEvent& event);
  void OnDownTimer(wxTimerEvent&);
  void OnRightUp(wxMouseEvent& event);

  void Reset();
  void ResetSelected();

  void Render(piDC& dc, PlugIn_ViewPort& vp);
  ConfigurationDialog m_ConfigurationDialog;
  ConfigurationBatchDialog m_ConfigurationBatchDialog;
  CursorPositionDialog m_CursorPositionDialog;
  RoutePositionDialog m_RoutePositionDialog;
  BoatDialog m_BoatDialog;

  void SetConfigurationRoute(WeatherRoute* weatherroute);
  void UpdateBoatFilename(wxString boatFileName);

  void UpdateCurrentConfigurations();
  void UpdateStates();
  SaveRouteOptions ShowRouteSaveOptionsDialog();

  /**
   * Get list of currently selected route maps in the weather routes list
   *
   * @param messagedialog If true, show warning dialog when no routes are
   * selected
   * @return List of RouteMapOverlay pointers for selected routes
   */
  std::list<RouteMapOverlay*> CurrentRouteMaps(bool messagedialog = false);
  RouteMapOverlay* FirstCurrentRouteMap();
  RouteMapOverlay* m_RouteMapOverlayNeedingGrib;

  void RebuildList();
  /**
   * List of route map overlays currently being computed in background threads.
   *
   * This list tracks all route map overlays that have active computation
   * threads. The WeatherRouting class uses this list to:
   * - Monitor computation progress of each route
   * - Check if any routes need updated GRIB data during computation
   * - Limit the number of concurrent computations based on settings
   * - Update the UI when computations complete
   *
   * Routes move from m_WaitingRouteMaps to m_RunningRouteMaps when computation
   * threads are started, then are removed when computation completes.
   *
   * @see m_WaitingRouteMaps For routes waiting to be computed
   * @see RouteMapOverlay::Running() To check if computation is still active
   * @see OnComputationTimer() For the timer handler that processes this list
   */
  std::list<RouteMapOverlay*> m_RunningRouteMaps;
  /**
   * List of route map overlays queued for computation but not yet started.
   *
   * This list contains route map overlays that have been scheduled for
   * computation but are waiting for resources to become available (e.g., when
   * the number of concurrent computations is limited by settings).
   */
  std::list<RouteMapOverlay*> m_WaitingRouteMaps;
  /**
   * Master list of all weather routes managed by the application.
   *
   * This list contains all weather routes created by the user, regardless of
   * computation state. Each WeatherRoute object contains a RouteMapOverlay that
   * handles the actual route calculation and a collection of formatted UI
   * strings for displaying route information.
   *
   * The WeatherRouting class maintains this list for:
   * - Displaying routes in the UI list control
   * - Saving/loading route configurations
   * - Generating statistics, reports, and plots
   * - Performing batch operations on multiple routes
   */
  std::list<WeatherRoute*> m_WeatherRoutes;

  void GenerateBatch();
  bool Show(bool show);

  void UpdateDisplaySettings();

  /**
   * Adds a new position with prompted name.
   *
   * Displays a dialog prompting the user to enter a name for the new position,
   * then adds the position with the provided latitude, longitude, and the
   * user-entered name.
   *
   * @param lat Latitude of the position in decimal degrees
   * @param lon Longitude of the position in decimal degrees
   * @see AddPosition(double lat, double lon, wxString name) The method that
   * performs the actual addition
   */
  void AddPosition(double lat, double lon);

  /**
   * Adds a position with specified latitude, longitude, and name.
   *
   * Verifies that the name doesn't already exist (prompting for replacement if
   * it does), then adds the position to the position list, updates UI elements,
   * and triggers configurations to update with the new position.
   *
   * @param lat Latitude of the position in decimal degrees
   * @param lon Longitude of the position in decimal degrees
   * @param name Name identifier for the position
   * @param suppress_prompt If true, suppresses the prompt for replacement and
   * does the replacement
   * @see UpdateConfigurations() For updating route configurations with the new
   * position
   */
  void AddPosition(double lat, double lon, wxString name,
                   const bool suppress_prompt);
  /**
   * Adds a position with specified GUID (Globally Unique Identifier).
   *
   * Used primarily for loading saved configurations or when importing positions
   * from routes or waypoints. If a position with the specified GUID already
   * exists, it updates that position instead of creating a new one.
   *
   * @param lat Latitude of the position in decimal degrees
   * @param lon Longitude of the position in decimal degrees
   * @param name Name identifier for the position
   * @param GUID Unique identifier, typically from OpenCPN waypoints
   * @see AddPosition(double lat, double lon, wxString name) Called when GUID is
   * empty
   */
  void AddPosition(double lat, double lon, wxString name, wxString GUID);
  void AddRoute(wxString& GUID);

  void CursorRouteChanged();
  void UpdateColumns();

  /**
   * Updates the Cursor Position dialog with data from the currently selected
   * route.
   *
   * This method refreshes the CursorPositionDialog with information about the
   * route position closest to where the user's cursor is hovering on the chart.
   
   * This method is called:
   * 1. When cursor movements trigger position changes
   * 2. Whenever the dialog is shown
   * 3. Periodically during route rendering to keep information current
   */
  void UpdateCursorPositionDialog();
  /**
   * Updates the Route Position dialog with detailed information about a
   * position along the route.
   *
   * This method refreshes the RoutePositionDialog with information about the
   * route position closest to the user's cursor on the chart.
   */
  void UpdateRoutePositionDialog();

  /**
   * Schedule an auto-save operation to occur after a delay.
   * This is a public method that can be called from dialog classes
   * to trigger auto-save when configuration changes.
   */
  void ScheduleAutoSave() { m_tAutoSaveXML.Start(5000, true); }

  /**
   * Get reference to the plugin instance.
   * @return Reference to the weather_routing_pi plugin
   */
  weather_routing_pi& GetPlugin() { return m_weather_routing_pi; }

  SettingsDialog m_SettingsDialog;

private:
  void CopyDataFiles(wxString from, wxString to);
  void OnCollPaneChanged(wxCollapsiblePaneEvent& event);
  void OnNewPosition(wxCommandEvent& event);
  void OnUpdateBoat(wxCommandEvent& event);
  void OnDeletePosition(wxCommandEvent& event);
  void OnDeleteAllPositions(wxCommandEvent& event);
  void OnClose(wxCloseEvent& event) { Hide(); }
  void OnPositionKeyDown(wxListEvent& event);
  void OnEditConfiguration();
  void OnEditPosition();

  void SaveSimplifiedRoute(RouteMapOverlay& routemapoverlay,
                           const std::list<Position*>& simplifiedRoute);

  /**
   * Loads a weather routing configuration from an XML file.
   *
   * This method displays a file dialog allowing the user to select an XML
   * configuration file to load. Upon selection, it clears all existing
   * positions and configurations, then loads the new configuration using
   * OpenXML(). The loaded file becomes the current working file for the
   * session.
   *
   * @param event The command event (unused)
   * @see OpenXML() For the actual file parsing functionality
   * @see SaveXML() For the complementary save operation
   */
  void OnOpen(wxCommandEvent& event);
  /**
   * Saves the current weather routing configuration to the current file path.
   *
   * If no file path is set (first save), it functions like OnSaveAs() and
   * prompts for a location. Otherwise, it saves directly to the current file
   * path without showing a dialog. After saving, it updates the window title
   * with the filename and stops any pending auto-save operations.
   *
   * @param event The command event (unused)
   * @see SaveXML() For the actual file writing functionality
   * @see OnSaveAs() Used when no existing file path is set
   */
  void OnSave(wxCommandEvent& event);
  /**
   * Saves the current weather routing configuration to a new file path.
   *
   * Always displays a file save dialog allowing the user to specify a new
   * file location. After saving, it updates the window title with the selected
   * filename and stops any pending auto-save operations.
   *
   * @param event The command event (unused)
   * @see SaveXML() For the actual file writing functionality
   */
  void OnSaveAs(wxCommandEvent& event);
  void OnClose(wxCommandEvent& event);
  void OnSize(wxSizeEvent& event);
  void OnNew(wxCommandEvent& event);
  void OnEditConfigurationClick(wxMouseEvent& event) { OnEditConfiguration(); }
  void OnEditPositionClick(wxMouseEvent& event) { OnEditPosition(); }
  void OnWeatherRouteSort(wxListEvent& event);
  void OnWeatherRouteSelected();
  void OnWeatherRouteSelected(wxListEvent& event) { OnWeatherRouteSelected(); }
  void OnWeatherPositionSelected();
  void OnWeatherPositionSelected(wxListEvent& event) {
    OnWeatherPositionSelected();
  }
  void OnWeatherRouteKeyDown(wxListEvent& event);
  void OnWeatherRoutesListLeftDown(wxMouseEvent& event);
  void UpdateComputeState();
  void OnCompute(wxCommandEvent& event);
  void OnComputeAll(wxCommandEvent& event);
  void OnStop(wxCommandEvent& event);
  void OnResetAll(wxCommandEvent& event);
  void OnPositions(wxCommandEvent& event);
  void OnBatch(wxCommandEvent& event);
  void OnEditConfiguration(wxCommandEvent& event) { OnEditConfiguration(); }
  void OnEditPosition(wxCommandEvent& event) { OnEditPosition(); }
  void OnGoTo(wxCommandEvent& event);
  void OnDelete(wxCommandEvent& event);
  void OnDeleteAll(wxCommandEvent& event);
  void OnFilter(wxCommandEvent& event);
  /** Callback invoked when user clicks "Save as Track" menu item. */
  void OnSaveAsTrack(wxCommandEvent& event);
  /** Callback invoked when user clicks "Save as Route" menu item. */
  void OnSaveAsRoute(wxCommandEvent& event);
  /** Export route as GPX file. */
  void OnExportRouteAsGPX(wxCommandEvent& event);
  /** Callback invoked when user clicks "Save All as Tracks" menu item. */
  void OnSaveAllAsTracks(wxCommandEvent& event);
  void OnSettings(wxCommandEvent& event);
  void OnStatistics(wxCommandEvent& event);
  void OnReport(wxCommandEvent& event);
  void OnPlot(wxCommandEvent& event);
  void OnCursorPosition(wxCommandEvent& event);
  // CUSTOMIZATION
  void OnRoutePosition(wxCommandEvent& event);
  void OnWeatherTable(wxCommandEvent& event);
  void OnManual(wxCommandEvent& event);
  void OnInformation(wxCommandEvent& event);
  void OnAbout(wxCommandEvent& event);

  void OnComputationTimer(wxTimerEvent&);
  void OnHideConfigurationTimer(wxTimerEvent&);
  void OnAutoSaveXMLTimer(wxTimerEvent&);
  void OnRenderedTimer(wxTimerEvent&);

  bool OpenXML(wxString filename, bool reportfailure = true);
  void SaveXML(wxString filename);
  /** Auto save on positions/routes changes. */
  void AutoSaveXML();

  void SetEnableConfigurationMenu();

  void UpdateConfigurations();
  void UpdateDialogs();

  /**
   * Adds a new configuration to the weather routing system.
   *
   * Creates a new WeatherRoute object based on the provided configuration,
   * initializes a RouteMapOverlay for it, and adds it to the UI list.
   * If the configuration references a route by GUID, it retrieves the route's
   * waypoints to set start and end positions.
   *
   * @param configuration The routing configuration to add
   * @return True if the configuration was successfully added, false otherwise
   * @see WeatherRoute For the UI data model created from the configuration
   * @see RouteMapOverlay For the route calculation and display engine
   */
  bool AddConfiguration(RouteMapConfiguration& configuration);
  /**
   * Updates a route map overlay in the UI.
   *
   * Finds the WeatherRoute object associated with the given RouteMapOverlay
   * and updates its UI information. This is typically called after calculation
   * state changes or when configuration parameters are modified.
   *
   * @param routemapoverlay Pointer to the RouteMapOverlay to update
   */
  void UpdateRouteMap(RouteMapOverlay* routemapoverlay);
  /**
   * Updates a specific item in the weather routes list.
   *
   * Updates the display values for a weather route at the specified index in
   * the list. Updates all fields (boat filename, positions, times, speeds,
   * etc.) unless stateonly is true, in which case it only updates the
   * computation state information.
   *
   * @param index The index of the item to update in the weather routes list
   * @param stateonly If true, only update the state field, not all
   * configuration data
   */
  void UpdateItem(long index, bool stateonly = false);
  /**
   * Adds or modifies a new position with the given name.
   *
   * Internal method to process the contents of the NewPosition dialog
   *
   * @param latitude_degrees Latitude of the position (degrees, signed integer
   * number)
   * @param latitude_minutes Latitude of the position (minutes, unsigned
   * floating point number)
   * @param longitude_degrees Longitude of the position (degrees, signed integer
   * number)
   * @param longitude_minutes Longitude of the position (minutes, unsigned
   * floating point number)
   * @param name
   * @param suppress_prompt Suppresses the prompt and replaces existing position
   * without asking
   * @see AddPosition(double lat, double lon, wxString name) The method that
   * performs the actual addition
   */
  void AddPosition(const wxString& latitude_degrees,
                   const wxString& latitude_minutes,
                   const wxString& longitude_degrees,
                   const wxString& longitude_minutes, wxString name,
                   const bool suppress_prompt);

  RouteMap* SelectedRouteMap();
  /** Save weather routing as OpenCPN track. */
  void SaveAsTrack(RouteMapOverlay& routemapoverlay);
  /** Save weather routing as OpenCPN route. */
  void SaveAsRoute(RouteMapOverlay& routemapoverlay);
  void ExportRoute(RouteMapOverlay& routemapoverlay);
  /**
   * Initiates route calculation for a specific route map overlay.
   *
   * This method handles the pre-computation setup for a route map overlay:
   * - If starting from boat position, it updates the start coordinates
   * - It attempts to start the computation thread
   * - It handles any errors that occur during startup
   * - It adds successful starts to the running routes list
   *
   * @param routemapoverlay Pointer to the route map overlay to compute
   */
  void Start(RouteMapOverlay* routemapoverlay);
  void StartAll();
  /* Stop the computation of the specified route. */
  void Stop(RouteMapOverlay* routemapoverlay);
  /* Stop the computation of all routes. */
  /* Moved to Public for AddressSpaceMonitor use */
  // void StopAll();

  void DeleteRouteMaps(std::list<RouteMapOverlay*> routemapoverlays);
  RouteMapConfiguration DefaultConfiguration();

  void AddRoutingPanel();

  /** The dialog to display routing statistics. */
  StatisticsDialog m_StatisticsDialog;
  /** The dialog to display a routing report. */
  ReportDialog m_ReportDialog;
  /** The dialog to display routing plots (wind, current, boat speed, etc). */
  PlotDialog m_PlotDialog;
  FilterRoutesDialog m_FilterRoutesDialog;

  wxTimer m_tCompute, m_tHideConfiguration;

  bool m_bRunning;
  wxTimeSpan m_RunTime;
  wxDateTime m_StartTime;

  wxString m_default_configuration_path;

  int m_RoutesToRun;
  bool m_bSkipUpdateCurrentItems;

  bool m_bShowConfiguration;
  bool m_bShowConfigurationBatch;
  bool m_bShowRoutePosition;
  bool m_bShowSettings;
  bool m_bShowStatistics;
  bool m_bShowReport;
  bool m_bShowPlot;
  bool m_bShowFilter;

  wxPoint m_downPos, m_startPos, m_startMouse;
  wxTimer m_tDownTimer;

  weather_routing_pi& m_weather_routing_pi;

  wxFileName m_FileName;

  wxSize m_size;

  /**
   * Pointer to the closest Position object on the route to the user's cursor
   * location.
   *
   * This member variable is used to track and display the position on the
   * computed weather route closest to where the user has placed their cursor on
   * the chart. It's utilized by:
   *
   * 1. The RoutePositionDialog to display detailed data about this specific
   * route point
   * 2. The Render method to visually highlight this position on the chart
   *
   * When the user moves their cursor over the chart, this pointer is updated to
   * point to the closest calculated position on the route by
   * getClosestRoutePositionFromCursor(). If a direct Position* cannot be
   * obtained but plot data is available, a temporary position (m_savedPosition)
   * may be used instead.
   *
   * This allows the user to explore detailed information about any point along
   * the computed route by simply moving their cursor near that point on the
   * chart.
   *
   * @see UpdateRoutePositionDialog() For the method that updates this pointer
   * @see RouteMapOverlay::getClosestRoutePositionFromCursor() For the
   * calculation method
   * @see RoutePositionDialog For the UI that displays data from this position
   */
  RoutePoint* m_positionOnRoute;
  /**
   * Temporary storage for position data when a direct Position* pointer is
   * unavailable.
   *
   * When getClosestRoutePositionFromCursor() can find plot data for a point but
   * not a direct Position* pointer, this member variable stores the position
   * information to ensure the UI can still display details about that point on
   * the route.
   *
   * This prevents information loss in situations where the route data is
   * available in one form (PlotData) but not another (Position*).
   *
   * @see m_positionOnRoute The main position pointer this serves as a backup
   * for
   */
  RoutePoint m_savedPosition;

  RoutingTablePanel* m_RoutingTablePanel;

#ifdef __WXMSW__

  AddressSpaceMonitor* m_addressSpaceMonitor = nullptr;

private:
    std::atomic<bool> m_disableNewComputations{false};

public:
    void DisableNewComputations() { m_disableNewComputations.store(true); }
    void EnableNewComputations()  { m_disableNewComputations.store(false); }
    bool AreNewComputationsDisabled() const { return m_disableNewComputations.load(); }
};
#endif  // __WXMSW__

#endif
