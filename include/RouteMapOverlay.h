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
 ***************************************************************************/

#ifndef _WEATHER_ROUTING_ROUTE_MAP_OVERLAY_H_
#define _WEATHER_ROUTING_ROUTE_MAP_OVERLAY_H_

#include <atomic>   
#include "RouteMap.h"
#include "LineBufferOverlay.h"
#include "WeatherRouting.h"

class PlugIn_ViewPort;
class PlugIn_Route;

class piDC;
class RouteMapOverlay;
class SettingsDialog;

/**
 * Thread class for route map overlay calculations.
 * Handles the background processing for weather route generation.
 */
 
 
class RouteMapOverlayThread : public wxThread {
public:
  /**
   * Constructor for the thread.
   * @param routemapoverlay Reference to the parent RouteMapOverlay object.
   * @param parent wxEvtHandler that receives EVT_ROUTEMAP_UPDATE
   */
  RouteMapOverlayThread(RouteMapOverlay& routemapoverlay, wxEvtHandler* parent);

  /**
   * Thread entry point that performs the route calculation.
   * @return Thread exit code.
   */
  void* Entry();

  virtual void OnExit() override;

private:
  /** Reference to the parent RouteMapOverlay object. */
  RouteMapOverlay& m_RouteMapOverlay;

  /** Event handler to receive EVT_ROUTEMAP_UPDATE */
  wxEvtHandler* m_parent;
};


  

/**
 * The central class for weather routing calculation, visualization, and
 * analysis.
 *
 * RouteMapOverlay extends the core RouteMap calculation engine with rendering
 * capabilities and user interaction features. It serves as a bridge between the
 * mathematical weather routing algorithms and the visual presentation of routes
 * to the user.
 *
 * This class handles multiple complex tasks:
 *
 * 1. Route Computation:
 *    - Spawns and manages background threads to calculate optimal sailing
 * routes
 *    - Interfaces with weather data (GRIB files and/or climatology)
 *    - Implements isochrone propagation algorithms considering wind, currents,
 * and obstacles
 *    - Handles routing constraints (wind, waves, land avoidance, etc.)
 *
 * 2. Visualization:
 *    - Renders calculated routes on the OpenCPN chart display
 *    - Draws weather data visualization (wind barbs, currents)
 *    - Manages color-coding of routes based on data source or sailing
 * conditions
 *    - Provides visual feedback on cursor interaction with routes
 *
 * 3. Route Analysis:
 *    - Provides statistical information about routes (speed, distance, wind
 * exposure)
 *    - Tracks sailing maneuvers (tacks, jibes) and comfort levels
 *    - Extracts route data for plotting and reporting
 *
 * 4. User Interface:
 *    - Responds to cursor movement over calculated routes
 *    - Provides data for position-specific information display
 *    - Manages interactions with route editing features
 *
 * The class maintains multiple data structures for route representation,
 * including isochrones (representing equal-time contours), position lists
 * (representing specific points along routes), and visualization caches (for
 * efficient rendering).
 *
 * Thread safety is managed through mutex locks, ensuring the computational
 * threads don't corrupt data while it's being accessed for display or analysis.
 *
 * @see RouteMap The base class providing core routing algorithms
 * @see WeatherRoute The UI representation of routes calculated by this class
 * @see WeatherRouting The main UI controller that manages multiple
 * RouteMapOverlays
 */
class RouteMapOverlay : public RouteMap {
  friend class RouteMapOverlayThread;

public:
  /**
   * Information types available for route analysis.
   */
  enum RouteInfoType {
    DISTANCE,           //!< Total distance of the route
    AVGSPEED,           //!< Average speed through water
    MAXSPEED,           //!< Maximum speed through water
    AVGSPEEDGROUND,     //!< Average speed over ground
    MAXSPEEDGROUND,     //!< Maximum speed over ground
    AVGWIND,            //!< Average wind speed
    MAXWIND,            //!< Maximum wind speed
    MAXWINDGUST,        //!< Maximum wind gust
    AVGCURRENT,         //!< Average current speed
    MAXCURRENT,         //!< Maximum current speed
    AVGSWELL,           //!< Average swell height
    MAXSWELL,           //!< Maximum swell height
    PERCENTAGE_UPWIND,  //!< Percentage of time sailing upwind
    PORT_STARBOARD,     //!< Percentage of time on port tack
    TACKS,              //!< Number of tacks performed
    JIBES,              //!< Number of jibes performed
    SAIL_PLAN_CHANGES,  //!< Number of sail changes performed
    COMFORT             //!< Sailing comfort level
  };

  /**
   * Default constructor.
   * Initializes a new RouteMapOverlay with default values.
   */
  RouteMapOverlay(wxEvtHandler* parent);

  /**
   * Destructor.
   * Cleans up resources and stops any running threads.
   */
  ~RouteMapOverlay();

  /**
   * Dirty flag indicating that this overlay's internal routing state
   * has been cleared (via Clear() / Reset()) and the UI must refresh.
   */
  bool IsDirty() const { return m_dirty.load(std::memory_order_acquire); }
  void ClearDirty() { m_dirty.store(false, std::memory_order_release); }
  void MarkDirty() { m_dirty.store(true, std::memory_order_release); }
  bool ConsumeDirty() {return m_dirty.exchange(false, std::memory_order_acq_rel); }

 
  /** Flag indicating the overlay was intentionally stopped */
  bool m_Stopped = false;

  /** Flag indicating the overlay needs to be redrawn */
  bool m_UpdateOverlay = false;

  /** Flag indicating if the end route should be visible */
  bool m_bEndRouteVisible = true;

  void SetConfiguration(const RouteMapConfiguration& cfg);

  /**
   * Updates the cursor position on the route map.
   * @param lat Latitude of the cursor position.
   * @param lon Longitude of the cursor position.
   * @return True if the cursor position changed, false otherwise.
   */
  bool SetCursorLatLon(double lat, double lon);

  /**
   * Renders the route map overlay on the viewport.
   * @param time Current time for boat position rendering.
   * @param settingsdialog Reference to settings dialog for display options.
   * @param dc Device context for drawing.
   * @param vp ViewPort for coordinate transformations.
   * @param justendroute If true, only renders the end route.
   * @param positionOnRoute Optional position to mark on the route.
   */
  void Render(wxDateTime time, SettingsDialog& settingsdialog, piDC& dc,
              PlugIn_ViewPort& vp, bool justendroute,
              const RoutePoint* positionOnRoute = nullptr);

  /**
   * Gets a color representing a sailing comfort level.
   * @param level Comfort level (1-3).
   * @return Color corresponding to the comfort level.
   */
  static wxColour sailingConditionColor(int level);

  /**
   * Gets a text description of a sailing comfort level.
   * @param level Comfort level (1-3).
   * @return Text description of the comfort level.
   */
  static wxString sailingConditionText(int level);

  /**
   * Renders wind barbs across the map.
   * @param dc Device context for drawing.
   * @param vp ViewPort for coordinate transformations.
   */
  void RenderWindBarbs(piDC& dc, PlugIn_ViewPort& vp);

  /**
   * Renders current arrows across the map.
   * @param dc Device context for drawing.
   * @param vp ViewPort for coordinate transformations.
   */
  void RenderCurrent(piDC& dc, PlugIn_ViewPort& vp);

  /**
   * Gets the latitude and longitude bounds of the route map.
   * @param latmin Output parameter for minimum latitude.
   * @param latmax Output parameter for maximum latitude.
   * @param lonmin Output parameter for minimum longitude.
   * @param lonmax Output parameter for maximum longitude.
   */
  void GetLLBounds(double& latmin, double& latmax, double& lonmin,
                   double& lonmax);

  /**
   * Requests grib data for a specific time.
   * @param time Time for which to request grib data.
   */
  void RequestGrib(wxDateTime time);

  /**
   * Gets plot data for either the cursor route or destination route.
   * @param cursor_route If true, gets data for cursor route, otherwise for
   * destination route.
   * @return Reference to a list of plot data points.
   */
  std::list<PlotData>& GetPlotData(bool cursor_route = false);

  /**
   * Gets specific route information based on type.
   * @param type Type of information to retrieve.
   * @param cursor_route If true, gets info for cursor route, otherwise for
   * destination route.
   * @return The requested route information value.
   */
  double RouteInfo(enum RouteInfoType type, bool cursor_route = false);

  /**
   * Counts the number of cyclone track crossings.
   * @param months Optional array to count crossings by month.
   * @return Number of cyclone crossings, or -1 if cyclone data is unavailable.
   */
  int Cyclones(int* months);

  /**
   * Return final arrival point(only valid if ReachedDestination () == true)
   * Pointer to the destination position for calc of distance sailed
   */
  const Position* GetDestinationPosition() const { return destination_position; }

  /**
   * Gets the best achievable position or null if not completed.
   * 1. Exact if reached  successfully.
   * 2. Closest calculated position to the destination if exact arrival  isn't possible.
   */
  Position* GetLastDestination() { return last_destination_position; }  

  /**
   * Checks if the route has been updated.
   * @return True if the route has been updated since last check.
   */
  bool Updated();

  /**
   * Updates the cursor position based on cached lat/lon.
   * Updates the internal cursor position state.
   */
  void UpdateCursorPosition();

  /**
   * Updates the destination position.
   * Calculates the closest reachable position to the destination.
   */
  void UpdateDestination();

  /**
   * Gets the end time of the route.
   * @return The calculated end time.
   */
  wxDateTime EndTime() { return m_EndTime; }

  /**
   * Clears all route data.
   * Resets the route map to its initial state.
   */
  virtual void Clear();

  /**
   * Locks the route map for thread-safe access.
   */
  virtual void Lock() { routemutex.Lock(); }

  /**
   * Unlocks the route map after thread-safe access.
   */
  virtual void Unlock() { routemutex.Unlock(); }

  /**
   * Checks if the calculation thread is still running.
   * @return True if the thread is running.
   * Scheduler must NEVER use m_Thread again,
   * Use m_bThreadAlive and m_bThreadExited
   * Have moved to flag?based lifecycle, which is correct.
   */
  bool Running() const {
    return m_bThreadAlive.load(std::memory_order_acquire) &&
           !m_bThreadExited.load(std::memory_order_acquire);
  }

  bool ThreadExited() const {
    return m_bThreadExited.load(std::memory_order_acquire);
  }

  bool HasThread() const {
      return m_Thread != nullptr;
  }

  bool Finished();

  void SetFinished(bool f);

  /**
   * Starts the route calculation thread.
   * @param error Output parameter for error messages.
   * @return True if the thread started successfully.
   */
  bool Start(wxString& error);

  void Stop(); // NEW  Stops

  /**
   * Deletes the calculation thread.
   * Waits for thread completion before deleting.
   */
  void DeleteThread();  // like Stop(), but waits until the thread is deleted

  /**
   * Gets the last cursor position.
   * @return Pointer to the last cursor position.
   */
  const Position* GetLastCursorPosition() const { return last_cursor_position; }

  /**
   * Gets the time at the last cursor position.
   * @return Time at the last cursor position.
   */
  wxDateTime GetLastCursorTime() { return m_cursor_time; }

  /**
   * Gets the closest route position to a given cursor latitude and longitude.
   * @param cursorLat Cursor latitude.
   * @param cursorLon Cursor longitude.
   * @param posData Output parameter for position data.
   * @return Pointer to the closest route position.
   */
  Position* getClosestRoutePositionFromCursor(double cursorLat,
                                              double cursorLon,
                                              PlotData& posData);
   /**
   * Performs route analysis on a predefined route.
   * @param proute Pointer to the route to analyze.
   */
  void RouteAnalysis(PlugIn_Route* proute);

  /**
   * Calculates the sailing comfort level for a given plot position.
   * @param plot The plot data to analyze.
   * @return Comfort level (1-3).
   */
  int sailingConditionLevel(const PlotData& plot) const;

  /**
   * Gets the list of isochrones used for route calculation.
   * @return Reference to the list of isochrones.
   */
  const IsoChronList& GetIsoChronList() const { return origin; }


private:

  wxEvtHandler* m_parentHandler;  // ? correct location

  /* -------------------- Thread + State Flags -------------------- */

  /** True when overlay has new data requiring UI refresh */
  bool m_bUpdated = false;

  /** Dirty flag for external consumers (WeatherRouting) */
  std::atomic<bool> m_dirty{false};

  /** Worker thread has entered Entry() */
  std::atomic<bool> m_bThreadAlive{false};

  /** Worker thread has exited Entry() */
  std::atomic<bool> m_bThreadExited{false};

  /* -------------------- Rendering Helpers -------------------- */

  void RenderAlternateRoute(IsoRoute* r, bool each_parent, piDC& dc,
                            PlugIn_ViewPort& vp);

  void RenderIsoRoute(IsoRoute* r, wxDateTime time, wxColour& grib_color,
                      wxColour& climatology_color, piDC& dc,
                      PlugIn_ViewPort& vp);

  void RenderPolarChangeMarks(bool cursor_route, piDC& dc, PlugIn_ViewPort& vp);

  void RenderBoatOnCourse(bool cursor_route, wxDateTime time, piDC& dc,
                          PlugIn_ViewPort& vp);

  void RenderCourse(bool cursor_route, piDC& dc, PlugIn_ViewPort& vp,
                    bool comfortRoute = false);

  void RenderWindBarbsOnRoute(piDC& dc, PlugIn_ViewPort& vp, int lineWidth,
                              bool apparentWind);

  /** Abort test used by RouteMap propagation */
  bool TestAbort() override { return Finished(); }

  /* -------------------- Thread + Synchronization -------------------- */

  /** Worker thread performing route propagation */
  RouteMapOverlayThread* m_Thread = nullptr;

  /** Mutex protecting route data during rendering/updates */
  wxMutex routemutex;

  /* -------------------- Drawing Utilities -------------------- */

  void SetPointColor(piDC& dc, Position* p);

  void DrawLine(const RoutePoint* p1, const RoutePoint* p2, piDC& dc,
                PlugIn_ViewPort& vp);

  void DrawLine(const RoutePoint* p1, wxColour& color1, const RoutePoint* p2,
                wxColour& color2, piDC& dc, PlugIn_ViewPort& vp);

  /* -------------------- Cursor + Destination Tracking -------------------- */

  /** Last cursor latitude */
  double last_cursor_lat = 0.0;

  /** Last cursor longitude */
  double last_cursor_lon = 0.0;

  /** Position nearest the cursor */
  Position* last_cursor_position = nullptr;

  /** Exact destination position (if reached) */
  Position* destination_position = nullptr;

  /** Best achievable position toward destination */
  Position* last_destination_position = nullptr;

  /* -------------------- UI Integration -------------------- */

  /** Parent window for posting EVT_ROUTEMAP_UPDATE events */
  wxWindow* m_parent = nullptr;

  /* -------------------- Time + Display State -------------------- */

  /** Timestamp at cursor position */
  wxDateTime m_cursor_time;

  /** End time of the route */
  wxDateTime m_EndTime;

  /** OpenGL display list ID */
  int m_overlaylist = 0;

  /** Projection type for display list */
  int m_overlaylist_projection = 0;

  /** Whether destination plot data should be cleared */
  bool clear_destination_plotdata = false;

  /* -------------------- Plot Data -------------------- */

  /** Plot data for destination route */
  std::list<PlotData> last_destination_plotdata;

  /** Plot data for cursor route */
  std::list<PlotData> last_cursor_plotdata;

  /* -------------------- Wind Barb Caches -------------------- */

  LineBuffer wind_barb_cache;
  double wind_barb_cache_scale = 0.0;
  size_t wind_barb_cache_origin_size = 0;
  int wind_barb_cache_projection = 0;

  LineBuffer wind_barb_route_cache;

  /* -------------------- Current Arrow Cache -------------------- */

  int m_sailingComfort = 0;

  LineBuffer current_cache;
  double current_cache_scale = 0.0;
  size_t current_cache_origin_size = 0;
  int current_cache_projection = 0;
};
#endif  // _WEATHER_ROUTING_ROUTE_MAP_OVERLAY_H_
