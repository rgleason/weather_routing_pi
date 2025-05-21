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

#ifndef _WEATHER_ROUTING_ROUTE_MAP_H_
#define _WEATHER_ROUTING_ROUTE_MAP_H_

#include "wx/datetime.h"
#include <wx/object.h>
#include <wx/weakref.h>

#include <list>

#include "ODAPI.h"
#include "GribRecordSet.h"
#include "ConstraintChecker.h"
#include "RoutePoint.h"
#include "Position.h"
#include "Boat.h"

struct RouteMapConfiguration;
class IsoRoute;

class PlotData;

/**
 * Enumeration of error codes for GRIB and climatology requests.
 */
enum WeatherForecastStatus {
  /** GRIB request was successful. */
  WEATHER_FORECAST_SUCCESS = 0,
  /**
   * There is no GRIB data for the requested time.
   * For example, the timestamp is beyond the weather forecast range.
   */
  WEATHER_FORECAST_NO_GRIB_DATA,
  /** GRIB contains no wind data. */
  WEATHER_FORECAST_NO_WIND_DATA,
  /** There is no climatology data. */
  WEATHER_FORECAST_NO_CLIMATOLOGY_DATA,
  /** Climatology data is disabled. */
  WEATHER_FORECAST_CLIMATOLOGY_DISABLED,
  /** Other GRIB error (catch all) */
  WEATHER_FORECAST_OTHER_ERROR,
};

class WR_GribRecordSet;

/*
 * A RoutePoint that has a time associated with it, along with navigation and
 * weather data.
 *
 * Variable name mapping from old to new:
 * - delta    → timeInterval   : Time from previous position (seconds)
 * - VBG      → sog            : Speed Over Ground (knots)
 * - BG       → cog            : Course Over Ground (degrees)
 * - VB       → stw            : Speed Through Water (knots)
 * - B        → ctw            : Course Through Water (degrees)
 * - VW       → twsWater       : True wind speed over water (knots)
 * - W        → twdWater       : True wind direction over water (degrees)
 * - VWG      → tws            : True wind speed over ground (knots)
 * - WG       → twd            : True wind direction over ground (degrees)
 * - VC       → currentSpeed   : Current speed (knots)
 * - C        → currentDir     : Current direction (degrees)
 * - WVHT     → swellHeight    : Significant wave height (meters)
 * - VW_GUST  → gustSpeed      : Gust wind speed (knots)
 * - VA       → aws            : Apparent wind speed (knots)
 * - A        → awa            : Apparent wind angle (degrees)
 */
class PlotData : public RoutePoint {
public:
  /** The time in UTC when the boat reaches this position, based on the route
   * calculation. */
  wxDateTime time;
  /** The time in seconds from the previous position to this position. */
  double delta;
  double sog;  //!< Speed Over Ground (SOG) in knots.
  double cog;  //!< Course Over Ground in degrees.
  double stw;  //!< Speed Through Water (STW) in knots.
  double ctw;  //!< Course Through Water (CTW) in degrees.
  /**
   * True Wind Speed relative to water (TWS over water) in knots, as predicted
   * by the forecast.
   *
   * This is the wind speed relative to the water's frame of reference.
   * Calculated from weather models/GRIBs for routing purposes.
   *
   * @note If the estimate is accurate, this should match the TWS value
   * displayed by standard marine instruments, which typically calculate TWS
   * using apparent wind data and the vessel's speed through water.
   */
  double twsOverWater;
  /**
   * True Wind Direction relative to water (TWD over water) in degrees, as
   * predicted by the forecast.
   *
   * The direction FROM which the true wind is coming, measured
   * in degrees clockwise from true north (0-359°).
   * This is relative to water, affected by water current.
   *
   * @note If the estimate is accurate, this should match the TWD value
   * displayed by standard marine instruments, which typically calculate TWD
   * using apparent wind data and the vessel's speed through water.
   */
  double twdOverWater;
  /**
   * True Wind Speed (TWS) over ground in knots, as predicted by the forecast.
   *
   * The wind speed relative to land/ground, regardless of water current.
   * Calculated from weather models/GRIBs for routing purposes.
   *
   * @note This corresponds to the wind speed values provided in weather
   * forecasts and GRIB files. Not typically displayed on standard marine
   * instruments unless they specifically calculate ground-referenced wind.
   */
  double twsOverGround;
  /**
   * True Wind Direction (TWD) over ground in degrees, as predicted by the
   * forecast.
   *
   * The direction FROM which the true wind is coming, measured
   * in degrees clockwise from true north (0-359°).
   * This is relative to ground, not affected by water current.
   *
   * @note This corresponds to the wind direction values provided in weather
   * forecasts and GRIB files. May differ from instrument-displayed TWD if
   * significant current is present.
   */
  double twdOverGround;
  double currentSpeed;  //!< Speed of sea current over ground in knots.
  double currentDir;    //!< Sea current direction over ground in degrees.
  double WVHT;          //!< Significant swell height in meters.
  double VW_GUST;       //!< Gust wind speed in knots.

  double cloud_cover;        //!< Cloud cover in percent (0-100%).
  double rain_mm_per_hour;   //!< Rainfall in mm.
  double air_temp;           //!< Air temperature in degrees Celsius.
  double sea_surface_temp;   //!< Sea surface temperature in degrees Celsius.
  double cape;               //!< The CAPE value in J/kg.
  double relative_humidity;  //!< Relative humidity in percent (0-100%).
  double air_pressure;       //!< Surface air pressure in hPa.
  double reflectivity;       //!< Reflectivity in dBZ.
  DataMask data_mask;        //!< Bitmask indicating data sources used.
};

class weather_routing_pi;

/**
 * Represents a named geographic position that can be used as a start or end
 * point for routing.
 *
 * RouteMapPosition stores essential information about a saved position
 * including:
 * - A human-readable name for the position
 * - An optional GUID for linking to navigation objects
 * - Latitude and longitude coordinates
 * - A unique numeric identifier
 */
struct RouteMapPosition {
  RouteMapPosition(wxString n, double lat0, double lon0,
                   wxString guid = wxEmptyString)
      : Name(n), GUID(guid), lat(lat0), lon(lon0) {
    ID = ++s_ID;
  }

  wxString Name;  //!< Human-readable name identifying this position
  wxString GUID;  //!< Optional unique identifier linking to navigation objects
  double lat;     //!< Latitude in decimal degrees
  double lon;     //!< Longitude in decimal degrees
  long ID;        //!< Unique numeric identifier for this position
  static long s_ID;  //!< Static counter used to generate unique IDs
};

/**
 * Contains both configuration parameters and runtime state for a weather
 * routing calculation.
 *
 * This struct serves multiple purposes in the weather routing system:
 * 1. Stores user-defined configuration settings that control the routing
 * algorithm
 * 2. Maintains runtime state during the routing calculation
 * 3. Tracks error conditions and other status information
 *
 * The dual nature of this struct (both configuration and state) is important to
 * understand:
 * - Configuration fields are typically set before starting a routing
 * calculation and remain unchanged during execution
 * - State fields are updated during the routing process and reflect the current
 * progress and conditions
 *
 * Configuration aspects include boat parameters, weather data sources, routing
 * constraints, and algorithm settings. State aspects include the current
 * position, timestamp, error flags, and intermediate calculation results.
 */
struct RouteMapConfiguration {
  /**
   * Defines the source for the starting point of the route.
   */
  enum StartDataType {
    START_FROM_POSITION,  //!< Start from named position, resolved to lat/lon.
    START_FROM_BOAT       //!< Start from boat's current position.
  };

  RouteMapConfiguration(); /* avoid waiting forever in update longitudes */

  /**
   * Updates the route configuration with the latest position information.
   *
   * This method performs several important functions:
   * 1. Resolves named positions or GUIDs to actual latitude/longitude
   * coordinates
   * 2. Handles the special case of starting from the boat's current position
   * 3. Normalizes longitude values for consistent calculations
   * 4. Calculates the bearing between the start and end points
   * 5. Generates the list of course angles to test during route calculation
   *
   * @return true if both start and end positions are valid, false otherwise
   */
  bool Update();

  wxString RouteGUID; /* Route GUID if any */
  /** The name of starting position, which is resolved to StartLat/StartLon. */
  wxString Start;
  /** The type of starting poiht, either from named position or boat current
   * position. */
  StartDataType StartType;
  wxString StartGUID;
  /** The name of the destination position, which is resolved to EndLat/EndLon.
   */
  wxString End;
  wxString EndGUID;

  /** The time when the boat leaves the starting position. */
  wxDateTime StartTime;
  /** Flag to use the current time as the start time. */
  bool UseCurrentTime;
  /** Default time in seconds between propagations. */
  double DeltaTime;
  /** Time in seconds between propagations. */
  double UsedDeltaTime;

  /** The polars of the boat, used for the route calculation. */
  Boat boat;
  /** The name of the boat XML file referencing polars. */
  wxString boatFileName;

  enum IntegratorType { NEWTON, RUNGE_KUTTA } Integrator;

  /**
   * The maximum angle the boat can be diverted from the bearing to the
   * destination, at each step of the route calculation, in degrees.
   *
   * This represents the angle away from Start to End bearing (StartEndBearing).
   * The normal setting is 100 degrees, which speeds up calculations. If the
   * route needs to go around land, islands or peninsulas, the user can increase
   * the value. E.g. the boat may have to go in the opposite direction then back
   * to the destination bearing.
   */
  double MaxDivertedCourse;

  /**
   * The maximum angle the boat can be diverted from the bearing to the
   * destination, based on the starting position to the destination (unlike
   * MaxDivertedCourse which is the angle at each step of the route
   * calculation).
   */
  double MaxCourseAngle;

  /**
   * How much the boat course can change at each step of the route calculation.
   *
   * A value of 180 gives the maximum flexibility of boat movement, but
   * increases the computation time. A minimum of 90 is usually needed for
   * tacking, a value of 120 is recommended with strong currents.
   */
  double MaxSearchAngle;

  /**
   * The calculated route will avoid a path where the true wind is above this
   * value in knots.
   */
  double MaxTrueWindKnots;

  /**
   * The calculated route will avoid a path where the apparent wind is above
   * this value in knots.
   */
  double MaxApparentWindKnots;

  /**
   * The calculated route will avoid swells larger than this value in meters.
   *
   * If the grib data does not contain swell information, the maximum swell
   * value is ignored. If there is no route within the maximum swell value, the
   * route calculation will fail.
   */
  double MaxSwellMeters;

  /**
   * The calculated route will not go beyond this latitude, as an absolute
   * value.
   *
   * If the starting or destination position is beyond this latitude, the route
   * calculation will fail.
   */
  double MaxLatitude;

  /**
   * The penalty time to tack the boat, in seconds.
   *
   * The penalty time is added to the route calculation for each tack.
   */
  double TackingTime;

  /**
   * The penalty time to jibe the boat, in seconds.
   *
   * The penalty time is added to the route calculation for each tack.
   */
  double JibingTime;

  /**
   * The penalty time to change the sail plan, in seconds.
   *
   * The penalty time is added to the route calculation for each sail plan
   * change.
   */
  double SailPlanChangeTime;

  /**
   * Maximum opposing wind vs current value to avoid dangerous sea conditions.
   *
   * When wind opposes current rough seas can be produced.
   * This constraint takes the dot product of the current and wind vectors, and
   * if the result exceeds this value, navigation in this area is avoided. For
   * example, a value of 60 would avoid 30 knots of wind opposing a 2 knot
   * current as well as 20 knots of wind opposing a 3 knot current. Higher
   * values allow for rougher conditions. The special value 0 (default) allows
   * any conditions.
   */
  double WindVSCurrent;

  /**
   * The minimum safety distance to land, in nautical miles.
   *
   * The calculated route will avoid land within this distance.
   */
  double SafetyMarginLand;

  /**
   * When enabled, the routing algorithm will avoid historical cyclone tracks.
   *
   * Uses climatology data to identify areas where cyclones have historically
   * occurred during the relevant season based on CycloneMonths and CycloneDays
   * parameters, and attempts to route around these dangerous zones.
   */
  bool AvoidCycloneTracks;
  // Avoid cyclone tracks within ( 30*CycloneMonths + CycloneDays ) days of
  // climatology data.
  int CycloneMonths;
  int CycloneDays;

  /**
   * Controls whether GRIB weather data is used for weather routing
   * calculations.
   *
   * When set to true:
   *   - The system attempts to use available GRIB data for wind and currents.
   *   - Fallback to climatology data may occur depending on configuration
   * settings.
   *
   * When set to false:
   *   - GRIB data is ignored even if available.
   *   - Routing will rely only on climatology data or other configured data
   * sources.
   *   - This can be useful for theoretical routing or when comparing against
   * climatological averages.
   */
  bool UseGrib;
  /**
   * Controls how climatology data is used for weather routing calculations.
   *
   * This enum defines the different modes for incorporating climatological
   * data.
   */
  enum ClimatologyDataType {
    /**
     * Climatology data is not used at all. Routing will rely solely on GRIB
     * data or fail if insufficient GRIB data is available.
     */
    DISABLED,
    /**
     * Only current/ocean data from climatology is used. Wind and other weather
     * data must come from GRIB files or other sources.
     */
    CURRENTS_ONLY,
    /**
     * Uses the full probability distribution of winds from climatology data.
     * This approach considers all possible wind scenarios weighted by their
     * probability of occurrence.
     */
    CUMULATIVE_MAP,
    /**
     * Similar to CUMULATIVE_MAP, but excludes calm conditions (no wind) from
     * the calculation. This can provide more realistic routing in areas prone
     * to periods of no wind.
     */
    CUMULATIVE_MINUS_CALMS,
    /** Uses only the most probable wind scenario from climatology. This is
     * faster but may miss alternative routing options that could be more
     * optimal in certain conditions.
     */
    MOST_LIKELY,
    /** Uses the average wind values from climatology. This provides the
     * simplest model but may not accurately represent areas with variable wind
     * patterns.
     */
    AVERAGE
  };
  enum ClimatologyDataType ClimatologyType;
  /**
   * Controls whether to use weather data outside its valid time range.
   *
   * When set to true, allows the routing algorithm to use GRIB data beyond its
   * valid time range as a fallback when no valid data is available.
   */
  bool AllowDataDeficient;
  /** wind speed multiplier. 1.0 is 100% of the wind speed in the grib. */
  double WindStrength;

  /**
   * Efficiency coefficient for upwind sailing (percentage).
   * 1.0 is 100% of the polar performance.
   */
  double UpwindEfficiency;

  /**
   * Efficiency coefficient for downwind sailing (percentage).
   * 1.0 is 100% of the polar performance.
   */
  double DownwindEfficiency;

  /**
   * Cumulative efficiency coefficient for night sailing (percentage).
   * 1.0 is 100% of the day time performance.
   */
  double NightCumulativeEfficiency;

  /**
   * If true, the route calculation will avoid land, outside the
   * SafetyMarginLand.
   */
  bool DetectLand;

  /**
   * If true, the route calculation will avoid exclusion boundaries.
   *
   * When enabled, the routing algorithm will check for and avoid entering any
   * defined exclusion zones or boundary areas during route calculation.
   */
  bool DetectBoundary;

  /**
   * If true and grib data contains ocean currents, the route calculation will
   * use ocean current data.
   *
   * Ocean currents can significantly affect routing, either aiding or hindering
   * vessel progress. When enabled, the routing algorithm takes into account
   * current direction and speed from GRIB data when calculating optimal routes.
   */
  bool Currents;

  /**
   * If true, avoid polar dead zones.
   * If false, avoid upwind course (polar angle too low) or downwind no-go zone
   * (polar angle too high).
   *
   * This setting affects how the routing algorithm handles sailing angles that
   * are at the limits of the vessel's polar performance data.
   */
  bool OptimizeTacking;

  /**
   * In some cases it may be possible to reach a location from two different
   * routes (imagine either side of an island) which is further away from the
   * destination before the destination can be reached. The algorithm must
   * invert and work inwards on this inverted region to possibly reach the
   * destination.
   */
  bool InvertedRegions;

  /**
   * If true, allows the vessel to anchor when facing adverse currents.
   *
   * In some cases, it may be preferable to anchor (assuming it isn't too deep)
   * rather than continue to navigate if there is a contrary current which is
   * swifter than the boat can travel. This allows the route to reach the
   * destination sooner by sitting in place until the current abades.
   */
  bool Anchoring;

  /**
   * Do not go below this minimum True Wind angle at each step of the route
   * calculation. The default value is 0 degrees.
   */
  double FromDegree;
  /**
   * Do not go above this maximum True Wind angle at each step of the route
   * calculation. The default value is 180 degrees.
   */
  double ToDegree;
  /**
   * The angular resolution at each step of the route calculation, in degrees.
   * Lower values provide finer resolution but increase computation time.
   * Higher values provide coarser resolution, but faster computation time.
   * The allowed range of resolution is from 0.1 to 60 degrees.
   * The default value is 5 degrees.
   */
  double ByDegrees;

  /* computed values */
  /**
   * Collection of angular steps used for vessel propagation calculations.
   *
   * This list contains the discrete angular steps (in degrees) that represent
   * the possible headings relative to the true wind direction that the vessel
   * can take during propagation. These angles are pre-computed based on the
   * FromDegree, ToDegree, and ByDegrees configuration parameters.
   *
   * During the propagation process, the algorithm tests each of these angles to
   * determine which headings are viable considering the vessel's capabilities,
   * weather conditions, and other constraints. Each angle represents a
   * potential direction of travel relative to the wind, where 0 degrees is
   * directly upwind.
   *
   * The granularity of these steps (controlled by ByDegrees) directly affects
   * both the accuracy of the routing calculation and its computational
   * complexity. Smaller step sizes provide more precise routing but require
   * more calculations.
   */
  std::list<double> DegreeSteps;
  /** The latitude of the starting position, in decimal degrees. */
  double StartLat;
  /** The longitude of the starting position, in decimal degrees. */
  double StartLon;
  /** The latitude of the destination position, in decimal degrees. */
  double EndLat;
  /** The longitude of the destination position, in decimal degrees. */
  double EndLon;

  /**
   * The initial bearing from Start position to End position, following the
   * Great Circle route and taking into consideration the ellipsoidal shape of
   * the Earth. Note: a boat sailing the great circle route will gradually
   * change the bearing to the destination.
   */
  double StartEndBearing;
  /**
   * longitudes are either 0 to 360 or -180 to 180,
   * this means the map cannot cross both 0 and 180 longitude.
   * To fully support this requires a lot more logic and would probably slow the
   * algorithm by about 8%.  Is it even useful?
   */
  bool positive_longitudes;

  // parameters
  WR_GribRecordSet* grib;

  /** Returns the current latitude of the boat, in degrees. */
  static double GetBoatLat();
  /** Returns the current longitude of the boat, in degrees. */
  static double GetBoatLon();

  static weather_routing_pi* s_plugin_instance;

  /**
   * Current timestamp in the routing calculation in UTC.
   * This is initialized to StartTime and advances with each propagation step.
   * The plugin maintains times in UTC internally, though some interactions with
   * other components may require conversion to local time.
   */
  wxDateTime time;

  /**
   * Indicates if the current GRIB data is being used outside its valid time
   * range.
   *
   * This flag is set to true when:
   * 1. The routing calculation has progressed beyond the time range covered by
   * the loaded GRIB file
   * 2. The AllowDataDeficient setting is enabled, permitting the use of
   * potentially outdated weather data
   * 3. Weather data from the GRIB file is being extrapolated or reused beyond
   * its intended validity period
   *
   * When this flag is true, weather data is being used in a "data deficient"
   * mode, meaning the wind and current information may be less accurate. Routes
   * calculated using data-deficient mode should be treated as approximations
   * rather than reliable forecasts.
   *
   * This flag is propagated to Position objects created during routing to
   * indicate which segments of a route were calculated with potentially
   * compromised weather data.
   */
  bool grib_is_data_deficient;
  /**
   * Indicates the status of the polar computation.
   * Errors can happen if the polar data is invalid, or there is no polar data
   * for the wind conditions.
   */
  PolarSpeedStatus polar_status;
  wxString wind_data_status;
  // Set to true if the route crossed land.
  bool land_crossing;
  // Set to true if the route crossed a boundary.
  bool boundary_crossing;
};

bool operator!=(const RouteMapConfiguration& c1,
                const RouteMapConfiguration& c2);

/**
 * Manages the complete weather routing calculation process from start to
 * destination.
 *
 * The RouteMap class serves as the primary controller for the entire weather
 * routing algorithm. It manages a sequence of IsoChron objects, each
 * representing positions reachable at successive time intervals. Together,
 * these isochrones form an expanding "map" of possible routes from the starting
 * point.
 *
 * RouteMap handles:
 * - The overall routing computation process from start to finish
 * - Management of configuration parameters for the routing calculation
 * - Integration with weather data (GRIB files and climatology)
 * - Error conditions and status reporting
 * - Thread safety for the potentially long-running calculation
 *
 * The core algorithm works by repeatedly propagating from the current isochrone
 * to create new ones until either the destination is reached or no further
 * progress can be made. At each step, positions are evaluated based on vessel
 * capabilities, weather conditions, land avoidance, and other configured
 * constraints.
 *
 * A RouteMap instance maintains the complete state of a routing calculation,
 * including the origin isochrones list, configuration settings, and status
 * information.
 */
class RouteMap {
public:
  RouteMap();
  virtual ~RouteMap();

  /**
   * Resolves a named position to its latitude and longitude coordinates.
   *
   * @param Name Position name to resolve
   * @param lat [out] Latitude of the resolved position
   * @param lon [out] Longitude of the resolved position
   */
  static void PositionLatLon(wxString Name, double& lat, double& lon);

  /**
   * Resets the RouteMap to initial state, clearing all isochrones and results.
   */
  void Reset();

#define LOCKING_ACCESSOR(name, flag) \
  bool name() {                      \
    Lock();                          \
    bool ret = flag;                 \
    Unlock();                        \
    return ret;                      \
  }
  /**
   * Thread-safe accessor to check if the routing calculation has finished.
   *
   * @return true if the routing calculation is complete
   */
  LOCKING_ACCESSOR(Finished, m_bFinished)
  /**
   * Thread-safe accessor to check if the destination was successfully reached.
   *
   * @return true if a valid route to the destination was found
   */
  LOCKING_ACCESSOR(ReachedDestination, m_bReachedDestination)
  /**
   * Thread-safe accessor to check if the RouteMap is in a valid state.
   *
   * @return true if the RouteMap is properly configured and ready for
   * calculation
   */
  LOCKING_ACCESSOR(Valid, m_bValid)
  /**
   * Thread-safe accessor to check if any land crossing was detected.
   *
   * @return true if the route crosses land
   */
  LOCKING_ACCESSOR(LandCrossing, m_bLandCrossing)
  /**
   * Thread-safe accessor to check if any boundary crossing was detected.
   *
   * @return true if the route crosses a boundary
   */
  LOCKING_ACCESSOR(BoundaryCrossing, m_bBoundaryCrossing)

  /**
   * Thread-safe accessor to get the polar computation status.
   *
   * @return Status code indicating any issues with polar data
   */
  PolarSpeedStatus GetPolarStatus() {
    Lock();
    PolarSpeedStatus status = m_bPolarStatus;
    Unlock();
    return status;
  }

  /**
   * Thread-safe accessor to check if there was insufficient weather data for
   * the calculation.
   *
   * @return true if weather data was missing or insufficient
   */
  wxString GetGribError() {
    Lock();
    wxString ret = m_bGribError;
    Unlock();
    return ret;
  }

  static wxString GetWeatherForecastStatusMessage(WeatherForecastStatus status);

  /**
   * Thread-safe accessor to get the weather forecast status.
   *
   * @return Status code indicating the state of weather data
   */
  WeatherForecastStatus GetWeatherForecastStatus() {
    Lock();
    WeatherForecastStatus status = m_bWeatherForecastStatus;
    Unlock();
    return status;
  }

  /**
   * Thread-safe accessor to check if the RouteMap is empty.
   *
   * @return true if no isochrones have been generated
   */
  bool Empty() {
    Lock();
    bool empty = origin.size() == 0;
    Unlock();
    return empty;
  }
  /**
   * Thread-safe accessor to check if GRIB data is needed.
   *
   * @return true if the calculation is waiting for GRIB data
   */
  bool NeedsGrib() {
    Lock();
    bool needsgrib = m_bNeedsGrib;
    Unlock();
    return needsgrib;
  }
  void RequestedGrib() {
    Lock();
    m_bNeedsGrib = false;
    Unlock();
  }
  void SetNewGrib(GribRecordSet* grib);
  void SetNewGrib(WR_GribRecordSet* grib);
  /**
   * Thread-safe accessor to get the time when new weather data is needed.
   *
   * @return Time when new weather data is required
   */
  wxDateTime NewTime() {
    Lock();
    wxDateTime time = m_NewTime;
    Unlock();
    return time;
  }
  /**
   * Thread-safe accessor to get the starting time of the route.
   *
   * @return The configured start time
   */
  wxDateTime StartTime() {
    Lock();
    wxDateTime time = m_Configuration.StartTime;
    Unlock();
    return time;
  }

  void SetConfiguration(const RouteMapConfiguration& o) {
    Lock();
    m_Configuration = o;
    m_bValid = m_Configuration.Update();
    m_bFinished = false;
    Unlock();
  }
  RouteMapConfiguration GetConfiguration() {
    Lock();
    RouteMapConfiguration o = m_Configuration;
    Unlock();
    return o;
  }

  void GetStatistics(int& isochrones, int& routes, int& invroutes,
                     int& skippositions, int& positions);
  /**
   * Performs one step of the routing propagation algorithm.
   *
   * This is the core computational method that advances the routing calculation
   * by generating a new isochrone from the current one. It handles route
   * merging, land avoidance, and all other configured constraints.
   *
   * @return true if propagation was successful and should continue
   */
  bool Propagate();

  /**
   * Function pointer for accessing climatology data.
   *
   * This allows the routing algorithm to integrate with external climatology
   * data sources.
   */
  static bool (*ClimatologyData)(int setting, const wxDateTime&, double, double,
                                 double&, double&);
  /**
   * Function pointer for accessing wind atlas data from climatology.
   *
   * This provides statistical wind information for a given location and time.
   */
  static bool (*ClimatologyWindAtlasData)(const wxDateTime&, double, double,
                                          int& count, double*, double*, double&,
                                          double&);
  /**
   * Function pointer for checking cyclone track crossings.
   *
   * This helps avoid areas with historical cyclone activity.
   */
  static int (*ClimatologyCycloneTrackCrossings)(double lat1, double lon1,
                                                 double lat2, double lon2,
                                                 const wxDateTime& date,
                                                 int dayrange);

  static OD_FindClosestBoundaryLineCrossing ODFindClosestBoundaryLineCrossing;

  /**
   * List of named positions available for routing.
   *
   * This static list provides access to saved positions that can be used
   * as starting points or destinations.
   */
  static std::list<RouteMapPosition> Positions;
  /**
   * Stops the routing calculation.
   *
   * Marks the calculation as finished to terminate any ongoing processing.
   */
  void Stop() {
    Lock();
    m_bFinished = true;
    Unlock();
  }
  void ResetFinished() {
    Lock();
    m_bFinished = false;
    Unlock();
  }
  /**
   * Loads the boat configuration from XML file.
   *
   * @return Error message if loading failed, or empty string on success
   */
  wxString LoadBoat() {
    return m_Configuration.boat.OpenXML(m_Configuration.boatFileName);
  }

  // XXX Isn't wxString refcounting thread safe?
  wxString GetError() {
    Lock();
    wxString ret = m_ErrorMsg;
    Unlock();
    return ret;
  }

  void SetError(wxString msg) {
    Lock();
    m_ErrorMsg = msg;
    m_bValid = false;
    m_bFinished = false;
    Unlock();
  }

  wxString GetWeatherForecastError() {
    Lock();
    wxString ret = m_bWeatherForecastError;
    Unlock();
    return ret;
  }

  void SetWeatherForecastError(wxString msg) {
    Lock();
    m_bWeatherForecastError = msg;
    m_bValid = false;
    m_bFinished = false;
    Unlock();
  }

  /** Collect error information from all positions in the most recent isochrone.
   */
  wxString GetRoutingErrorInfo();

protected:
  void SetFinished(bool destination) {
    m_bReachedDestination = destination;
    m_bFinished = true;
  }

  void UpdateStatus(const RouteMapConfiguration& configuration) {
    if (configuration.polar_status != POLAR_SPEED_SUCCESS) {
      m_bPolarStatus = configuration.polar_status;
    }

    if (configuration.wind_data_status != wxEmptyString)
      m_bGribError = configuration.wind_data_status;

    if (configuration.boundary_crossing) m_bBoundaryCrossing = true;

    if (configuration.land_crossing) m_bLandCrossing = true;
  }

  virtual void Clear();
  /**
   * Reduces a list of routes by merging overlapping ones.
   *
   * This is a key part of the routing algorithm that consolidates the
   * potentially large number of routes generated during propagation into a
   * minimal set.
   *
   * @param merged [out] Output list for the merged routes
   * @param routelist Input list of routes to merge
   * @param configuration Routing configuration
   * @return true if reduction was successful
   */
  bool ReduceList(IsoRouteList& merged, IsoRouteList& routelist,
                  RouteMapConfiguration& configuration);
  /**
   * Finds the closest position to given coordinates across all isochrones.
   *
   * @param lat Latitude to find closest position to
   * @param lon Longitude to find closest position to
   * @param t [out] Optional pointer to store time at closest position
   * @param dist [out] Optional pointer to store distance to closest position
   * @return Pointer to the closest position
   */
  Position* ClosestPosition(double lat, double lon, wxDateTime* t = 0,
                            double* dist = 0);

  /* protect any member variables with mutexes if needed */
  virtual void Lock() = 0;
  virtual void Unlock() = 0;
  virtual bool TestAbort() = 0;

  /**
   * Determines the time step for the next isochrone generation based on current
   * routing conditions.
   *
   * This function dynamically adjusts the time step to provide more detailed
   * isochrones in critical areas:
   * 1. Near the starting point
   * 2. Approaching the destination
   *
   * @return The calculated time step in seconds to use for the next isochrone.
   */
  double DetermineDeltaTime();

  /**
   * List of isochrones in chronological order.
   *
   * This is the core data structure that maintains all generated isochrones
   * from the starting point outward.
   */
  IsoChronList origin;
  bool m_bNeedsGrib;
  /**
   * Shared reference to GRIB data.
   */
  Shared_GribRecordSet m_SharedNewGrib;
  WR_GribRecordSet* m_NewGrib;

private:
  /** Helper method to collect errors from a position and its parents. */
  void CollectPositionErrors(Position* position,
                             std::vector<Position*>& failed_positions);

  RouteMapConfiguration m_Configuration;
  bool m_bFinished, m_bValid;
  bool m_bReachedDestination;
  /**
   * Stores the status code for weather forecast errors.
   *
   * This variable contains an enum value from WeatherForecastStatus indicating
   * the specific reason why weather data (GRIB or climatology) was insufficient
   * or unavailable for completing the routing calculation.
   */
  WeatherForecastStatus m_bWeatherForecastStatus;
  /**
   * Detailed error message for weather forecast issues.
   *
   * Contains a human-readable description of any weather forecast errors,
   * including details such as the specific timestamp for which GRIB data
   * was missing or insufficient.
   *
   * @see WeatherForecastStatus
   */
  wxString m_bWeatherForecastError;
  /**
   * Stores the status code for polar data errors.
   *
   * This variable contains an enum value from PolarSpeedStatus indicating any
   * issues encountered when trying to use the vessel's polar performance data,
   * such as wind angles outside the polar range or wind speeds that are too
   * light or strong for the available data.
   */
  PolarSpeedStatus m_bPolarStatus;
  /**  */
  wxString m_bGribError;
  bool m_bLandCrossing;
  bool m_bBoundaryCrossing;

  wxString m_ErrorMsg;

  wxDateTime m_NewTime;
};

#endif
