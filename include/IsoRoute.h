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

#ifndef _WEATHER_ROUTING_ISO_ROUTE_H_
#define _WEATHER_ROUTING_ISO_ROUTE_H_

#include <wx/wx.h>

#include <list>

#include "WeatherDataProvider.h"

class SkipPosition;
class Position;
struct RouteMapConfiguration;
class IsoRoute;

typedef std::list<IsoRoute*> IsoRouteList;

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
                      DataMask& mindata_mask);

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
   * Propagates all routes in this isochrone to create the next isochrone.
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

bool Merge(IsoRouteList& rl, IsoRoute* route1, IsoRoute* route2, int level,
           bool inverted_regions);

#endif
