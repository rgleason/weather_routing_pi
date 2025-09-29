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

#include <cstdint>
#include <vector>

#include "ConstraintChecker.h"

struct RouteMapConfiguration;
class PlotData;

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
enum class DataMask : uint32_t {
  NONE = 0,
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
  NIGHT_TIME = 64,

  /**
   * Indicates that this segment was traveled using motor instead of sailing.
   * Used for visual differentiation and tracking motor usage in routes.
   */
  MOTOR_USED = 128
};

inline DataMask operator|(DataMask a, DataMask b) {
  return static_cast<DataMask>(static_cast<uint32_t>(a) |
                               static_cast<uint32_t>(b));
}
inline DataMask& operator|=(DataMask& a, DataMask b) {
  a = a | b;
  return a;
}
inline bool operator&(DataMask a, DataMask b) {
  return (static_cast<uint32_t>(a) & static_cast<uint32_t>(b)) != 0;
}

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

class RoutePoint;

/**
 * Stores weather conditions at a specific geographical position.
 *
 * This class contains wind, current, and climatology data but no boat-specific
 * calculations. Data is typically obtained from weather files (GRIB) or
 * climatology database.
 */
class WeatherData {
public:
  double lat;
  double lon;
  double twdOverGround;
  double twsOverGround;
  double twdOverWater;
  double twsOverWater;
  double currentDir;
  double currentSpeed;
  double swell;
  climatology_wind_atlas atlas;

  WeatherData(RoutePoint* position);

  bool ReadWeatherDataAndCheckConstraints(RouteMapConfiguration& configuration,
                                          RoutePoint* position,
                                          DataMask& data_mask,
                                          PropagationError& error_code,
                                          bool end);
};

/**
 * Stores calculated boat motion data for a specific heading and weather
 * condition.
 *
 * This class contains the results of polar calculations - speed values, course
 * angles, and maneuver flags that are recalculated for each potential route
 * segment.
 */
class BoatData {
public:
  BoatData()
      : stw(0),
        cog(0),
        sog(0),
        dist(0),
        tacked(false),
        jibed(false),
        sail_plan_changed(false) {}

  double stw;   // Boat speed through water
  double cog;   // Boat bearing over ground
  double sog;   // Boat speed over ground
  double dist;  // Distance traveled over ground (nm)

  bool tacked;
  bool jibed;
  bool sail_plan_changed;

  /**
   * Calculates boat speed for a given polar and wind conditions.
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
   * @param newpolar [in] Index of the polar to use from the boat's polar array
   * @param twa [in] True Wind Angle (TWA) (degrees)
   * @param ctw [in] Boat's bearing relative to true wind (W+twa)
   * @param data_mask [in/out] Bit mask indicating data sources (GRIB,
   * climatology, etc.)
   * @param bound [in] If true, returns NAN when wind speed is outside the range
   * defined in the polar data. If false, extrapolates the boat speed when wind
   * speed is outside the polar data range.
   * @param caller [in] Name of the calling function (for logging)
   *
   * @return true if computation successful, false if NaN values detected
   */
  bool GetBoatSpeedForPolar(RouteMapConfiguration& configuration,
                            const WeatherData& weather, double timeseconds,
                            int newpolar, double twa, double ctw,
                            DataMask& data_mask, bool bound = true,
                            const char* caller = "unknown");

  /**
   * Find the best polar and calculate boat speed given wind conditions.
   *
   * @param configuration Boat and route configuration settings
   * @param twa [in] True Wind Angle (TWA) (degrees)
   * @param ctw [in] Boat's bearing relative to true wind (W+twa)
   * @param parent_heading [in] Boat's heading from parent position (degrees)
   * @param data_mask [in/out] Bit mask indicating data sources (GRIB,
   * climatology, etc.)
   * @param polar [in] The index to current polar from the parent weather
   * position.
   * @param newpolar [out] The best polar for the current weather conditions.
   * @param timeseconds Duration in seconds for the calculation
   *
   * @return true if computation successful, false if NaN values detected
   */
  bool GetBestPolarAndBoatSpeed(RouteMapConfiguration& configuration,
                                const WeatherData& weather_data, double twa,
                                double ctw, double parent_heading,
                                DataMask& data_mask, int polar, int& newpolar,
                                double& timeseconds);

private:
  void Reset() {
    stw = 0;
    cog = 0;
    sog = 0;
    dist = 0;
    tacked = false;
    jibed = false;
    sail_plan_changed = false;
  }
};

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

             int sail_plan_change_count = 0, DataMask dm = DataMask::NONE,
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
                   RouteMapConfiguration& configuration, PlotData& data) const;
  // Return the wind data at the route point.
  bool GetWindData(RouteMapConfiguration& configuration, double& W, double& VW,
                   DataMask& data_mask);
  // Return the current data at the route point.
  bool GetCurrentData(RouteMapConfiguration& configuration, double& C,
                      double& VC, DataMask& data_mask);

  // Return true if the route point crosses land.
  bool CrossesLand(double dlat, double dlon) const;
  // Return true if the route point enters a boundary.
  bool EntersBoundary(double dlat, double dlon) const;

  /**
   * Propagates along a rhumb line to a destination point, handling long
   * distances by creating intermediate waypoints that check constraints at each
   * step.
   *
   * Unlike PropagateToPoint which only considers the starting point, this
   * method divides the journey into segments and evaluates weather and
   * constraints at each intermediate point, following a rhumb line (constant
   * bearing) path.
   *
   * This makes it suitable to determine if a route following a rhumb line is
   * feasible, even if the destination is far away. If the destination is
   * reachable, the method will return the total time to reach it and populate
   * the intermediatePoints vector with the sequence of waypoints.
   *
   * @param dlat Destination latitude in decimal degrees
   * @param dlon Destination longitude in decimal degrees
   * @param configuration Route configuration with constraints and boat
   * parameters
   * @param intermediatePoints Vector to store the sequence of intermediate
   * waypoints (if successful)
   * @param data_mask [out] Bit flags indicating data sources used
   * @param totalDistance [out] Store the total distance in nautical miles
   * @param averagetSpeed [out] Store the average speed in knots
   *                      required to reach the destination with the same ETA
   * @param maxSegmentLength Maximum length of each segment in nautical miles
   *
   * @return Total time in seconds to reach the target, or NAN if unreachable
   */
  double RhumbLinePropagateToPoint(double dlat, double dlon,
                                   RouteMapConfiguration& configuration,
                                   std::vector<RoutePoint*>& intermediatePoints,
                                   DataMask& data_mask, double& totalDistance,
                                   double& averageSpeed,
                                   double maxSegmentLength = 10.0);

  /**
   * Attempts to reach a specific target point from the current position.
   *
   * Calculates whether and how a vessel can reach a specified target point
   * (dlat, dlon) from the current position. Uses an iterative
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
                          double& heading, DataMask& data_mask,
                          bool end = true);

  DataMask data_mask;
};

#endif
