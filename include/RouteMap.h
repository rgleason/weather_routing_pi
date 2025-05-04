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

#include "wx/datetime.h"
#include <wx/object.h>
#include <wx/weakref.h>

#include <list>

#include "ODAPI.h"
#include "GribRecordSet.h"

struct RouteMapConfiguration;
class IsoRoute;

typedef std::list<IsoRoute*> IsoRouteList;

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

/**
 * Represents a wind rose summary of climatological wind data for a location.
 *
 * This data structure holds statistical wind data typically derived from
 * historical weather patterns. The data is organized into 8 directional sectors
 * (at 45 degree intervals), storing both wind speeds and frequency of
 * occurrence.
 */
struct climatology_wind_atlas {
  double W[8];  //!< Probability/weight of wind occurring in each direction
                //!< sector (0-1)
  double
      VW[8];  //!< Most common wind speed (in knots) for each direction sector
  double storm;  //!< Probability of storm conditions (0-1)
  double calm;   //!< Probability of calm conditions (0-1)
  /**Central wind direction (in degrees) for each sector:
   * typically [0, 45, 90, 135, 180, 225, 270, 315]
   */
  double directions[8];
};

class WR_GribRecordSet;

/**
 * A base class representing a geographical position that is part of a route.
 *
 * RoutePoint stores the essential information about a point along a route,
 * including its latitude and longitude coordinates, which polar was used
 * to reach this point, and how many tacks were required. It also provides
 * methods for retrieving weather data at this position and determining if
 * the route point intersects with land or boundaries.
 *
 * This serves as the foundation class for more specialized route position
 * classes like Position and PlotData.
 */
class RoutePoint {
public:
  RoutePoint(double latitude = 0., double longitude = 0., int polar_idx = -1,
             int tack_count = 0, int jibe_count = 0,
             int sail_plan_change_count = 0, int dm = 0,
             bool data_deficient = false)
      : lat(latitude),
        lon(longitude),
        polar(polar_idx),
        tacks(tack_count),
        jibes(jibe_count),
        sail_plan_changes(sail_plan_change_count),
        grib_is_data_deficient(data_deficient),
        data_mask(dm) {}

  virtual ~RoutePoint() {};

  double lat;
  double lon;
  /** The index of the polar to use at this position. */
  int polar;
  /** The cumulative number of tack maneuvers to get to this position. */
  int tacks;
  /** The cumulative number of jibe maneuvers to get to this position. */
  int jibes;
  /** The cumulative number of sail plan changes to get to this position. */
  int sail_plan_changes;

  bool grib_is_data_deficient;

  bool GetPlotData(RoutePoint* next, double dt,
                   RouteMapConfiguration& configuration, PlotData& data);
  // Return the wind data at the route point.
  bool GetWindData(RouteMapConfiguration& configuration, double& W, double& VW,
                   int& data_mask);
  // Return the current data at the route point.
  bool GetCurrentData(RouteMapConfiguration& configuration, double& C,
                      double& VC, int& data_mask);

  // Return true if the route point crosses land.
  bool CrossesLand(double dlat, double dlon);
  // Return true if the route point enters a boundary.
  bool EntersBoundary(double dlat, double dlon);
  /**
   * Propagates from current position to a specific target point.
   *
   * Attempts to find a valid route from the current position to the specified
   * lat/lon coordinates within the constraints of the configuration.
   *
   * @param dlat Target latitude
   * @param dlon Target longitude
   * @param configuration Route configuration parameters
   * @param heading [out] Final heading to reach target
   * @param data_mask [out] Mask indicating data sources used
   * @param end If true, indicates this is the final destination point
   *
   * @return Time in seconds to reach target point, or NAN if not possible
   */
  double PropagateToPoint(double dlat, double dlon, RouteMapConfiguration& cf,
                          double& heading, int& data_mask, bool end = true);

  /**
   * Calculates boat performance based on environmental conditions and desired
   * heading.
   *
   * Given a specific boat heading relative to wind, this function calculates:
   * 1. How fast the boat will move through water (speed through water)
   * 2. The actual direction the boat will travel through water (course through
   * water)
   * 3. How fast the boat will move over ground after accounting for currents
   * (speed over ground)
   * 4. The actual direction the boat will travel over ground (course over
   * ground)
   *
   * @param configuration Boat and route configuration settings
   * @param timeseconds Duration in seconds for the calculation
   * @param twd [in] Wind direction over ground (degrees)
   * @param tws [in] Wind speed over ground (knots)
   * @param windDirOverWater [in] Wind direction through water (degrees)
   * @param windSpeedOverWater [in] Wind speed through water (knots)
   * @param currentDir [in] Current direction (degrees)
   * @param currentSpeed [in] Current speed (knots)
   * @param twa [in] True Wind Angle (TWA) (degrees)
   * @param atlas [in] Wind climatology atlas containing probability
   * distributions
   * @param data_mask [in/out] Bit mask indicating data sources (GRIB,
   * climatology, etc.)
   * @param ctw [in] Boat's bearing relative to true wind (W+twa), initialized
   * by caller
   * @param stw [out] Boat speed through water
   * @param cog [out] Boat bearing over ground
   * @param sog [out]Boat speed over ground
   * @param dist [out] Distance traveled over ground (nm)
   * @param newpolar [in] Index of the polar to use from the boat's polar array
   * @param bound [in] If true, returns NAN when wind speed is outside the range
   * defined in the polar data. If false, extrapolates the boat speed when wind
   * speed is outside the polar data range.
   *
   * @return true if computation successful, false if NaN values detected
   */
  bool ComputeBoatSpeed(RouteMapConfiguration& configuration,
                        double timeseconds, double twd, double tws,
                        double windDirOverWater, double windSpeedOverWater,
                        double currentDir, double currentSpeed, double twa,
                        climatology_wind_atlas& atlas, double ctw, double& stw,
                        double& cog, double& sog, double& dist, int newpolar,
                        bool bound = true, const char* caller = "unknown");

  /**
   * Bit flags indicating what data sources were used for wind and current
   * calculations and other routing conditions.
   *
   * These flags track various aspects of each position in the routing
   * calculation:
   * 1. The origin of wind and current data (GRIB or climatology)
   * 2. Whether the data was "deficient" (outside optimal time/location range)
   * 3. Environmental conditions like day/night status
   *
   * The flags can be combined using bitwise OR operations to represent multiple
   * conditions simultaneously. For example, a position at night using GRIB
   * current data and climatology wind data would have a data_mask containing:
   * (GRIB_CURRENT | CLIMATOLOGY_WIND | NIGHT_TIME)
   *
   * These flags serve multiple purposes:
   * - Visual differentiation in the route display (different colors for data
   * sources)
   * - Performance adjustments (efficiency factors for different conditions)
   * - Analytical reporting of route segments and their data quality
   *
   * When examining a route, these flags provide important context about the
   * reliability and characteristics of each segment.
   */
  enum DataMask {
    /** Wind data originated from GRIB files. */
    GRIB_WIND = 1,

    /** Wind data originated from climatology data. */
    CLIMATOLOGY_WIND = 2,

    /**
     * Wind data is from GRIB but is considered "data deficient".
     * This typically means the data is from outside the requested time
     * or location range but was used because better data was not available.
     */
    DATA_DEFICIENT_WIND = 4,

    /** Current data originated from GRIB files. */
    GRIB_CURRENT = 8,

    /** Current data originated from climatology data. */
    CLIMATOLOGY_CURRENT = 16,

    /**
     * Current data is from GRIB but is considered "data deficient".
     * This typically means the data is from outside the requested time
     * or location range but was used because better data was not available.
     */
    DATA_DEFICIENT_CURRENT = 32,

    /**
     * Indicates that this position occurs during nighttime.
     * Used to apply nighttime efficiency factor and darker display colors.
     */
    NIGHT_TIME = 64
  };
  int data_mask;
};

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
  /** The time when the boat reaches this position, based on the route
   * calculation. TODO: is it UTC? */
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
};

class SkipPosition;
class weather_routing_pi;

enum PropagationError {
  PROPAGATION_NO_ERROR = 0,
  /** Position has already been propagated. */
  PROPAGATION_ALREADY_PROPAGATED = 1,
  /** Swell exceeds max value at position. */
  PROPAGATION_EXCEEDED_MAX_SWELL = 2,
  /** Position exceeds maximum latitude. */
  PROPAGATION_EXCEEDED_MAX_LATITUDE = 3,
  /** Wind data is missing or invalid. */
  PROPAGATION_WIND_DATA_FAILED = 4,
  /** Wind speed exceeds maximum value at position. */
  PROPAGATION_EXCEEDED_MAX_WIND = 5,
  /** Wind speed exceeds current speed at position. */
  PROPAGATION_EXCEEDED_WIND_VS_CURRENT = 6,
  /** Propagation angle is outside search limits. */
  PROPAGATION_ANGLE_OUTSIDE_SEARCH_LIMITS = 7,
  /** Propagation violates polar constraints. */
  PROPAGATION_POLAR_CONSTRAINTS = 8,
  /** Boat speed computation failed. */
  PROPAGATION_BOAT_SPEED_COMPUTATION_FAILED = 9,
  /** Apparent wind speed exceeds maximum value at position. */
  PROPAGATION_EXCEEDED_APPARENT_WIND = 10,
  /** Land intersection detected during propagation. */
  PROPAGATION_LAND_INTERSECTION = 11,
  /** Land safety margin exceeded during propagation. */
  PROPAGATION_LAND_SAFETY_MARGIN = 12,
  /** Boundary intersection detected during propagation. */
  PROPAGATION_BOUNDARY_INTERSECTION = 13,
  /** Cyclone track crossing detected during propagation. */
  PROPAGATION_CYCLONE_TRACK_CROSSING = 14,
  /** Propagation angle error. */
  PROPAGATION_ANGLE_ERROR = 15
};

/**
 * Circular linked list node for positions which take equal time to reach.
 */
class Position : public RoutePoint {
public:
  Position(double latitude, double longitude, Position* p = nullptr,
           double pheading = NAN, double pbearing = NAN, int polar_idx = -1,
           int tack_count = 0, int jibe_count = 0,
           int sail_plan_change_count = 0, int data_mask = 0,
           bool data_deficient = false);
  Position(Position* p);

  SkipPosition* BuildSkipList();

  /**
   * Propagates a position forward in time, generating new possible positions.
   *
   * This is a key routing function that expands the isochron by calculating all
   * possible new positions reachable from the current position after the
   * configured time step. It checks various constraints like wind, currents,
   * land avoidance etc.
   *
   * @param routelist List to store the generated routes
   * @param configuration Route configuration parameters
   *
   * @return true if propagation was successful and new routes were added,
   *         false if no valid propagation was possible
   */
  bool Propagate(IsoRouteList& routelist, RouteMapConfiguration& configuration);

  double Distance(Position* p);
  // Return the number of times the sail configuration has changed.
  int SailChanges();
  double PropagateToEnd(RouteMapConfiguration& configuration, double& H,
                        int& data_mask);

  /** Helper method to get error as string. */
  static wxString GetErrorText(PropagationError error);

  /** Structure to track errors for specific angle attempts. */
  struct AngleStatus {
    double angle;  //!< The True Wind Angle that was attempted (degrees).
    PropagationError error;  //!< Error code for this angle attempt.
  };

  /**
   * Angle relative to true wind direction when traveling from parent to this
   * position.
   *
   * This value represents the vessel's heading relative to the true wind when
   * moving from the parent position to this position. It is measured in degrees
   * where:
   * - 0 degrees is directly upwind
   * - 90 degrees is perpendicular to the wind (beam reach)
   * - 180 degrees is directly downwind
   *
   * This angle is used to determine if tacking occurred (sign change in
   * heading) and to apply tacking penalties. It's also used for optimizing sail
   * configurations and evaluating polar performance.
   */
  double parent_heading;

  /**
   * Geographic bearing from parent position to this position.
   *
   * This value represents the compass bearing (in degrees) when traveling from
   * the parent position to this position, where:
   * - 0 degrees is North
   * - 90 degrees is East
   * - 180 degrees is South
   * - 270 degrees is West
   *
   * This bearing is used to constrain the angular sector for the next
   * propagation step based on the MaxSearchAngle configuration parameter.
   */
  double parent_bearing;

  Position* parent;      /* previous position in time */
  Position *prev, *next; /* doubly linked circular list of positions */

  bool propagated;
  bool drawn, copied;

  /** Indicates why propagation failed. */
  PropagationError propagation_error;

private:
  /** Reset error tracking information. */
  void ResetErrorTracking();

  /** Get error information for debugging. */
  wxString GetErrorInfo() const;
  /** Get detailed error information for debugging. */
  wxString GetDetailedErrorInfo() const;

  bool rk_step(double timeseconds, double cog, double dist, double twa,
               RouteMapConfiguration& configuration, WR_GribRecordSet* grib,
               const wxDateTime& time, int newpolar, double& rk_BG,
               double& rk_dist, int& data_mask);
};

/**
 * A circular linked list that optimizes route path traversal by grouping
 * consecutive positions that share the same directional quadrant. This allows
 * the routing algorithm to quickly "skip" over large segments of consistent
 * direction, which improves performance when checking for intersections or
 * containment in routes with many points.
 *
 * Each SkipPosition points to:
 * 1. A Position object (a point in the route)
 * 2. The next SkipPosition in the list (which may skip over many intermediate
 * Positions)
 * 3. The previous SkipPosition in the list
 *
 * The quadrant value (0-3) represents the directional relationship between
 * consecutive positions, calculated based on their relative latitude and
 * longitude:
 * - Quadrant 0: Northwest movement (lat decreasing, lon decreasing)
 * - Quadrant 1: Northeast movement (lat decreasing, lon increasing)
 * - Quadrant 2: Southwest movement (lat increasing, lon decreasing)
 * - Quadrant 3: Southeast movement (lat increasing, lon increasing)
 */
class SkipPosition {
public:
  /**
   * Constructs a new SkipPosition object.
   *
   * @param p Pointer to a Position object that this SkipPosition will reference
   * @param q Quadrant value (0-3) indicating the directional relationship
   *          with the next position.
   */
  SkipPosition(Position* p, int q);

  /**
   * Removes this SkipPosition from the circular list.
   *
   * Connects the previous and next positions, maintaining the circular
   * structure before deleting this instance.
   */
  void Remove();
  /**
   * Creates a deep copy of the entire skip list including referenced positions.
   *
   * This method recursively traverses the circular skip list, duplicating all
   * SkipPosition objects along with their corresponding Position objects,
   * maintaining the same structure and connectivity as the original list.
   *
   * @return Pointer to the first SkipPosition in the new copied list
   */
  SkipPosition* Copy();

  Position* point;
  SkipPosition *prev, *next;
  /**
   * Direction quadrant (0-3) indicating relationship between this point and
   * the next.
   */
  int quadrant;
};

/**
 * Represents a closed loop of positions forming an isochrone boundary.
 *
 * An IsoRoute defines a closed contour of geographic positions that can be
 * reached within the same amount of time from a starting point. "Closed loop"
 * means the positions form a complete circuit where the last position connects
 * back to the first position, creating a continuous perimeter with no gaps or
 * endpoints.
 *
 * These boundaries (isochrones) expand outward as travel time increases,
 * creating concentric regions of accessibility from the starting point.
 *
 * The class supports:
 * - Normal regions (direction = 1) representing areas the vessel can reach.
 * - Inverted regions (direction = -1) representing "holes" or areas within a
 * normal region that cannot be reached (e.g., islands or other obstacles)
 * - Parent-child relationships for handling nested regions.
 *
 * IsoRoute provides methods for merging, normalizing (removing intersections),
 * checking containment, and propagating routes to create the next isochrone.
 */
class IsoRoute {
public:
  /**
   * Constructs an IsoRoute from a SkipPosition list.
   *
   * @param p Pointer to a SkipPosition that defines the start of the boundary
   * @param dir Direction value: 1 for normal regions, -1 for inverted regions
   */
  IsoRoute(SkipPosition* p, int dir = 1);
  /**
   * Copy constructor that optionally sets a parent.
   *
   * Creates a deep copy of another IsoRoute, including all Position and
   * SkipPosition objects, and optionally establishes a parent-child
   * relationship.
   *
   * @param r Source IsoRoute to copy
   * @param p Parent IsoRoute (nullptr if no parent)
   */
  IsoRoute(IsoRoute* r, IsoRoute* p = nullptr);
  ~IsoRoute();

  /**
   * Outputs route information for debugging.
   *
   * Prints all positions in the route to standard output for debugging
   * purposes.
   */
  void Print();
  /**
   * Outputs skip list information for debugging.
   *
   * Prints all skip positions in the route to standard output for debugging
   * purposes.
   */
  void PrintSkip();

  /**
   * Adjusts skippoints to start at the minimum latitude.
   *
   * Ensures the skippoints list starts at the position with minimum latitude,
   * which helps identify the outer boundary of the route for intersection
   * testing.
   */
  void MinimizeLat();
  /**
   * Counts intersections between a position and this route.
   *
   * Determines how many times a ray from the given position to infinity
   * crosses the route boundary. Used to determine if a position is inside
   * or outside the route.
   *
   * @param pos Position to test
   * @return Number of intersections, or -1 if the test is inconclusive
   */
  int IntersectionCount(Position& pos);
  /**
   * Tests if a position is contained within this route.
   *
   * Uses ray casting algorithm to determine if a position is inside this route,
   * optionally testing children routes as well.
   *
   * @param pos Position to test
   * @param test_children Whether to test child routes as well
   * @return 1 if position is inside, 0 if outside, -1 if test is inconclusive
   */
  int Contains(Position& pos, bool test_children);

  /**
   * Tests if this route completely contains another route.
   *
   * Checks if every position in route r is contained within this route.
   *
   * @param r The route to test for containment
   * @return true if r is completely contained within this route
   */
  bool CompletelyContained(IsoRoute* r);
  /**
   * Tests if this route contains another route.
   *
   * Tests if the first position of route r is contained within this route.
   * If that test is inconclusive, tries other positions.
   *
   * @param r The route to test for containment
   * @return true if r is contained within this route
   */
  bool ContainsRoute(IsoRoute* r);

  /**
   * Simplifies the route by removing points that are very close together.
   *
   * Removes positions that are within a small epsilon distance of each other
   * to improve computational efficiency without significantly changing the
   * route.
   */
  void ReduceClosePoints();
  //    bool ApplyCurrents(GribRecordSet *grib, wxDateTime time,
  //    RouteMapConfiguration &configuration);
  /**
   * Finds the bounding box of this route.
   *
   * Calculates the minimum and maximum latitude and longitude values
   * that encompass the entire route.
   *
   * @param bounds[4] Array to store results: [min_lon, max_lon, min_lat,
   * max_lat]
   */
  void FindIsoRouteBounds(double bounds[4]);

  /**
   * Removes a position from the route.
   *
   * Removes and deletes a position from the route, updating the skip list
   * if necessary.
   *
   * @param s The SkipPosition preceding the position to remove
   * @param p The Position to remove
   */
  void RemovePosition(SkipPosition* s, Position* p);
  /**
   * Finds the closest position in the route to a given lat/lon.
   *
   * Searches the route and its children for the position closest to
   * the specified coordinates.
   *
   * @param lat Latitude to find closest position to
   * @param lon Longitude to find closest position to
   * @param dist Pointer to store the squared distance (optional)
   * @return Pointer to the closest position
   */
  Position* ClosestPosition(double lat, double lon, double* dist = 0);
  /**
   * Attempts to propagate from all positions in this route.
   *
   * Creates new potential routes by propagating from each position in this
   * route according to the specified configuration.
   *
   * @param routelist List to store new routes generated by propagation
   * @param configuration Route configuration parameters
   * @return true if at least one successful propagations occurred.
   */
  bool Propagate(IsoRouteList& routelist, RouteMapConfiguration& configuration);
  /**
   * Propagates directly to the configured end position.
   *
   * Attempts to find the best path from this route directly to the
   * destination point specified in the configuration.
   *
   * @param configuration Route configuration parameters
   * @param mindt [out] Minimum time to reach destination
   * @param endp [out] Position from which the end is reached
   * @param minH [out] Final heading at destination
   * @param mintacked [out] Whether tacking occurred on final approach
   * @param minjibed [out] Whether jibing occurred on final approach
   * @param minsail_plan_changed [out] Whether sailplan changed on final
   * approach
   * @param mindata_mask [out] Data source mask for the final approach
   */
  void PropagateToEnd(RouteMapConfiguration& configuration, double& mindt,
                      Position*& endp, double& minH, bool& mintacked,
                      bool& minjibed, bool& minsail_plan_changed,
                      int& mindata_mask);

  /**
   * Counts the number of skip positions in this route.
   *
   * @return Number of skip positions
   */
  int SkipCount();
  /**
   * Counts the total number of positions in this route.
   *
   * @return Number of positions
   */
  int Count();
  /**
   * Updates various statistics about this route and its children.
   *
   * Calculates totals for routes, inverted routes, skip positions,
   * and positions across this route and all its children.
   *
   * @param routes [in/out] Counter for number of routes
   * @param invroutes [in/out] Counter for number of inverted routes
   * @param skippositions [in/out] Counter for number of skip positions
   * @param positions [in/out] Counter for number of positions
   */
  void UpdateStatistics(int& routes, int& invroutes, int& skippositions,
                        int& positions);
  /**
   * Resets the drawn flag on all positions in this route and its children.
   *
   * Marks all positions as not drawn, used before rendering the route.
   */
  void ResetDrawnFlag();

  /**
   * Skip list of positions for efficient traversal.
   *
   * The skippoints list provides optimized access to the positions in this
   * route, allowing rapid traversal of segments that maintain the same
   * directional quadrant.
   */

  SkipPosition* skippoints;

  /**
   * Indicates whether this is a normal (1) or inverted (-1) region.
   *
   * Direction = 1: Normal region the vessel can reach.
   * Direction = -1: Inverted region (hole) representing areas that cannot be
   * reached.
   */
  int direction;

  /**
   * Pointer to the parent route if this is a child (inner region).
   *
   * nullptr if this is a top-level route, otherwise points to the outer region
   * that contains this route.
   */
  IsoRoute* parent;
  /**
   * List of child routes representing inner inverted regions.
   *
   * Contains routes representing areas inside this route that cannot be
   * reached (e.g., islands, obstacles, or other exclusion zones).
   */
  IsoRouteList children;
};

// -----------------
class WR_GribRecordSet {
public:
  WR_GribRecordSet(unsigned int id) : m_Reference_Time(-1), m_ID(id) {
    for (int i = 0; i < Idx_COUNT; i++) {
      m_GribRecordPtrArray[i] = 0;
      m_GribRecordUnref[i] = false;
    }
  }

  virtual ~WR_GribRecordSet() { RemoveGribRecords(); }

  /* copy and paste by plugins, keep functions in header */
  void SetUnRefGribRecord(int i, GribRecord* pGR) {
    assert(i >= 0 && i < Idx_COUNT);
    if (m_GribRecordUnref[i] == true) {
      delete m_GribRecordPtrArray[i];
    }
    m_GribRecordPtrArray[i] = pGR;
    m_GribRecordUnref[i] = true;
  }

  void RemoveGribRecords() {
    for (int i = 0; i < Idx_COUNT; i++) {
      if (m_GribRecordUnref[i] == true) {
        delete m_GribRecordPtrArray[i];
      }
    }
  }

  time_t m_Reference_Time;
  unsigned int m_ID;

  GribRecord* m_GribRecordPtrArray[Idx_COUNT];

private:
  // grib records files are stored and owned by reader mapGribRecords
  // interpolated grib are not, keep track of them
  bool m_GribRecordUnref[Idx_COUNT];
};

// ------
class Shared_GribRecordSetData : public wxRefCounter {
public:
  Shared_GribRecordSetData(WR_GribRecordSet* gribset = 0)
      : m_GribRecordSet(gribset) {}
  Shared_GribRecordSetData(const Shared_GribRecordSetData& data)
      : m_GribRecordSet(data.m_GribRecordSet) {}

  void SetGribRecordSet(WR_GribRecordSet* gribset) {
    m_GribRecordSet = gribset;
  }
  WR_GribRecordSet* GetGribRecordSet() const { return m_GribRecordSet; }

  ~Shared_GribRecordSetData();

protected:
  WR_GribRecordSet* m_GribRecordSet;
};

// ------
class Shared_GribRecordSet : public wxTrackable {
public:
  // initializes this, assigning to the
  // internal data pointer a new instance of Shared_GribRecordSetData
  Shared_GribRecordSet(WR_GribRecordSet* ptr = 0)
      : m_data(new Shared_GribRecordSetData(ptr)) {}
  Shared_GribRecordSet& operator=(const Shared_GribRecordSet& tocopy) {
    // shallow copy: this is just a fast copy of pointers; the real
    // memory-consuming data which typically is stored inside
    m_data = tocopy.m_data;
    return *this;
  }

  void SetGribRecordSet(WR_GribRecordSet* ptr) {
    // make sure changes to this class do not affect other instances
    // currently sharing our same refcounted data:
    UnShare();
    m_data->SetGribRecordSet(ptr);
  }

  WR_GribRecordSet* GetGribRecordSet() const {
    return m_data->GetGribRecordSet();
  }

  bool operator==(const Shared_GribRecordSet& other) const {
    if (m_data.get() == other.m_data.get())
      return true;  // this instance and the 'other' one share the same data...
    return (m_data->GetGribRecordSet() == other.m_data->GetGribRecordSet());
  }

  wxObjectDataPtr<Shared_GribRecordSetData> m_data;

protected:
  void UnShare() {
    if (m_data->GetRefCount() == 1) return;
    m_data.reset(new Shared_GribRecordSetData(*m_data));
  }
};

/**
 * Manages a collection of IsoRoute objects that represent equal-time boundaries
 * from a starting point.
 *
 * While an IsoRoute represents a single closed boundary, an IsoChron
 * encapsulates a complete set of multiple IsoRoute objects that together define
 * all regions reachable within the same amount of time.
 *
 * An IsoChron contains:
 * - A list of IsoRoute objects (which may include normal regions and their
 * inverted "hole" regions)
 * - The specific time value this isochrone represents (when these points can be
 * reached)
 * - The time delta from the previous isochrone
 * - Associated weather data (GRIB) for this time period
 *
 * The IsoChron class forms the backbone of the routing algorithm, with
 * successive IsoChron objects representing expanding time boundaries as the
 * vessel travels further from the origin. Each IsoChron builds upon the
 * previous one by propagating routes outward based on vessel capabilities and
 * environmental conditions.
 */
class IsoChron {
public:
  /**
   * Constructs an IsoChron from a list of routes and associated time and
   * weather data.
   *
   * @param r List of IsoRoute objects forming this isochrone
   * @param t Time when the vessel would reach positions on this isochrone
   * @param d Time increment (in seconds) from previous isochrone
   * @param g Shared GRIB weather data associated with this time period
   * @param grib_is_data_deficient Flag indicating if GRIB data has limitations
   */
  IsoChron(IsoRouteList r, wxDateTime t, double d, Shared_GribRecordSet& g,
           bool grib_is_data_deficient);
  ~IsoChron();

  /**
   * Propagates all routes in this isochron to create the next isochrone.
   *
   * From each position on each route in the current isochrone, calculates all
   * possible new positions the vessel could reach in the next time increment,
   * creating a new expanded set of routes.
   *
   * @param routelist Output list to store the newly generated routes
   * @param configuration Route configuration parameters controlling propagation
   */
  void PropagateIntoList(IsoRouteList& routelist,
                         RouteMapConfiguration& configuration);
  /**
   * Tests if a position is contained within any route in this isochrone.
   *
   * @param p Position to test
   * @return true if the position is inside any route in this isochrone
   */
  bool Contains(Position& p);
  /**
   * Tests if a geographic coordinate is contained within any route in this
   * isochrone.
   *
   * @param lat Latitude to test
   * @param lon Longitude to test
   * @return true if the coordinate is inside any route in this isochrone
   */
  bool Contains(double lat, double lon);
  /**
   * Finds the closest position in any route to the given coordinates.
   *
   * @param lat Latitude to find closest position to
   * @param lon Longitude to find closest position to
   * @param t Optional pointer to store the time at the closest position
   * @param dist Optional pointer to store the distance to the closest position
   * @return Pointer to the closest position
   */
  Position* ClosestPosition(double lat, double lon, wxDateTime* t = 0,
                            double* dist = 0);
  void ResetDrawnFlag();

  /**
   * List of IsoRoute objects that together form this isochrone.
   *
   * Each IsoRoute represents a distinct region reachable within the same time,
   * and collectively they define the complete isochrone boundary.
   */
  IsoRouteList routes;
  /**
   * The time at which all positions on this isochrone can be reached.
   *
   * All routes in this isochrone have equal travel time from the starting
   * point.
   */
  wxDateTime time;
  /**
   * The time increment (in seconds) from the previous isochrone to this one.
   *
   * Represents how much more time is needed to reach this isochrone compared
   * to the previous one.
   */
  double delta;
  /**
   * Shared reference to GRIB weather data for this time period.
   *
   * Contains wind, current, and other weather information used for
   * calculations.
   */
  Shared_GribRecordSet m_SharedGrib;
  /**
   * Pointer to the GRIB record set for this time period.
   *
   * Direct access to weather data used in propagation calculations.
   */
  WR_GribRecordSet* m_Grib;
  /**
   * Flag indicating if the GRIB data has limitations or deficiencies.
   *
   * When true, weather data may be incomplete or extrapolated.
   */
  bool m_Grib_is_data_deficient;
};

typedef std::list<IsoChron*> IsoChronList;

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

  void GetStatistics(int& isochrons, int& routes, int& invroutes,
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

  /** Collect error information from all positions in the most recent isochron.
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
