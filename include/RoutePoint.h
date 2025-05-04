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

#ifndef _WEATHER_ROUTING_ROUTEPOINT_H_
#define _WEATHER_ROUTING_ROUTEPOINT_H_

struct RouteMapConfiguration;
class PlotData;
struct climatology_wind_atlas;

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
   * Attempts to reach a specific target point from the current position.
   *
   * Calculates whether and how a vessel can reach a specified target point
   * (dlat, dlon) from the current position. This function uses an iterative
   * solver to determine the correct heading that accounts for current drift,
   * but does NOT optimize for best route - it simply tries to go directly to
   * the target.
   *
   * Key characteristics:
   * - Attempts direct path to target (not necessarily optimal)
   * - Uses weather data only from starting position
   * - Performs iterative calculation to compensate for currents
   * - Suitable for short distances where weather remains constant
   * - No route optimization or alternative path consideration
   *
   * @param dlat Target latitude in decimal degrees
   * @param dlon Target longitude in decimal degrees
   * @param configuration Route configuration with constraints and boat
   * parameters
   * @param heading [out] Heading required to reach target (degrees), accounting
   * for current
   * @param data_mask [out] Bit flags indicating data sources used
   * @param end If true, this is final destination (affects optimization
   * settings)
   *
   * @return Time in seconds to reach target, or NAN if unreachable.
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

#endif
