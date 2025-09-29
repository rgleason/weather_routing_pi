/***************************************************************************
 *   Copyright (C) 2016 by OpenCPN Development Team                        *
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

#ifndef _WEATHER_ROUTING_ROUTE_SIMPLIFIER_H_
#define _WEATHER_ROUTING_ROUTE_SIMPLIFIER_H_

#include "RouteMap.h"
class RouteMapOverlay;
#include <list>
#include <vector>
#include <wx/string.h>

/**
 * Parameters for route simplification.
 */
struct SimplificationParams {
  double maxDurationPenaltyPercent =
      0.05;                //!< Maximum acceptable duration penalty (%)
  int maxIterations = 15;  //!< Maximum optimization iterations

  SimplificationParams() = default;
  SimplificationParams(double durationPenaltyPercent, int iterations)
      : maxDurationPenaltyPercent(durationPenaltyPercent),
        maxIterations(iterations) {}
};

/**
 * Result of route simplification.
 */
struct SimplificationResult {
  bool success;                          //!< Whether simplification succeeded
  std::list<Position*> simplifiedRoute;  //!< The simplified route
  int waypointReduction;                 //!< Number of waypoints removed
  int maneuverReduction = 0;             //!< Number of maneuvers reduced
  int originalManeuverCount = 0;         //!< Original number of maneuvers
  int simplifiedManeuverCount = 0;       //!< Simplified number of maneuvers
  wxTimeSpan totalDuration = 0;          //!< Total duration of the route
  wxTimeSpan durationDelta = 0;       //!< Duration penalty compared to original
  double durationDeltaPercent = 0.0;  //!< Duration penalty as percentage
  wxString message;                   //!< Status message
};

/**
 * Represents a segment of a route between tack, jibe and sail change maneuvers.
 */
struct RouteSegment {
  enum ManeuverType { NONE = 0, TACK, JIBE, SAIL_CHANGE };
  /** Points in this segment */
  std::list<Position*> points;
  /** Original duration for this segment, before simplification. */
  wxTimeSpan originalDuration = wxTimeSpan(0);
  /** Type of maneuver at end of segment. */
  int maneuverType = NONE;

  RouteSegment() = default;
  bool isEmpty() const { return points.empty(); }
  /** Get the number of waypoints in this segment. */
  size_t size() const { return points.size(); }
};

/**
 * Simplifies weather routing paths while preserving navigability.
 *
 * This class implements an intelligent route simplification algorithm that:
 * - Reduces waypoints while maintaining route validity
 * - Ensures segments remain navigable under weather conditions
 * - Optimizes for minimal time penalty
 * - Uses adaptive epsilon adjustment for optimal results
 */
class RouteSimplifier {
public:
  /**
   * Construct a new Route Simplifier.
   * @param routemap RouteMapOverlay containing the route to simplify
   */
  explicit RouteSimplifier(RouteMapOverlay* routemap);

  /**
   * Destroy the Route Simplifier and clean up resources.
   */
  ~RouteSimplifier();

  /**
   * Split route into segments at sailing maneuver (tack, jib, sail change)
   * points.
   * @param route Route to split
   * @return Vector of route segments
   */
  std::vector<RouteSegment> SplitRouteByManeuvers(
      const std::list<Position*>& route);

  /**
   * Main simplification method with configurable parameters.
   * @param params Simplification parameters
   * @return SimplificationResult containing the result and status
   */
  SimplificationResult Simplify(const SimplificationParams& params);

  /**
   * Get the original route point count.
   * @return Number of points in the original route
   */
  size_t GetOriginalWayPointCount() const { return m_originalRoute.size(); }

private:
  /**
   * Find alternate routes with different maneuver characteristics.
   * @param maxRoutes Maximum number of alternative routes to return
   * @return Vector of simplified routes with maneuver statistics
   */
  struct RouteStats {
    std::list<Position*> waypoints;            //!< Waypoints in this route
    wxTimeSpan totalDuration = wxTimeSpan(0);  //!< Total route duration
    int totalTacks = 0;                        //!< Total number of tacks
    int totalJibes = 0;                        //!< Total number of jibes
    int totalSailChanges = 0;                  //!< Total sail plan changes
    int totalManeuvers = 0;                    //!< Sum of all maneuvers

    bool operator<(const RouteStats& other) const {
      return totalDuration < other.totalDuration;
    }
  };

  /**
   * Find alternate routes with fewer sailing maneuvers.
   * @param params Simplification parameters
   * @param maxRoutes Maximum number of routes to return
   * @return Vector of route statistics for each route
   */
  std::vector<RouteStats> FindAlternateRoutesWithFewerManeuvers(
      const SimplificationParams& params, int maxRoutes = 10);

  /**
   * Select optimal route based on time/maneuver tradeoff.
   * @param alternateRoutes Vector of candidate routes
   * @param params Simplification parameters
   * @return Best route based on criteria
   */
  RouteStats SelectOptimalRoute(const std::vector<RouteStats>& alternateRoutes,
                                const SimplificationParams& params);

  /**
   * Extract positions from the route map.
   * @param routemap The RouteMapOverlay to extract from
   * @return List of Position pointers representing the route
   */
  std::list<Position*> ExtractPositionsFromRouteMap(RouteMapOverlay* routemap);

  /**
   * Calculate duration of a segment.
   * @param segment List of positions in segment
   * @return Duration as a wxTimeSpan
   */
  wxTimeSpan CalculateSegmentDuration(
      const std::list<Position*>& segment) const;

  /**
   * Build a validated route from candidate waypoints.
   * @param candidateRoute Points selected by Douglas-Peucker
   * @return Validated route with necessary intermediate waypoints
   */
  std::list<Position*> BuildValidatedRoute(
      const std::list<Position*>& candidateRoute);

  /**
   * Validate a route segment for navigability.
   * @param start Start position
   * @param end End position
   * @param validatedEnd Output: validated end position
   * @return true if segment is valid, false otherwise
   */
  bool ValidateSegment(Position* start, Position* end, Position*& validatedEnd);

  /**
   * Advanced validation using detailed propagation.
   * @param start Start position
   * @param end End position
   * @param validatedEnd Output: validated end position
   * @return true if segment is valid, false otherwise
   */
  bool ValidateSegmentWithDetailedPropagation(Position* start, Position* end,
                                              Position*& validatedEnd);

  /**
   * Insert required intermediate waypoints.
   * @param route Route being built
   * @param start Start position
   * @param end End position
   */
  void InsertRequiredWaypoints(std::list<Position*>& route, Position* start,
                               Position* end);

  /**
   * Binary subdivision for waypoint insertion.
   * @param route Route being built
   * @param startIt Iterator to start position
   * @param endIt Iterator to end position
   */
  void BinarySubdivision(std::list<Position*>& route,
                         std::list<Position*>::iterator startIt,
                         std::list<Position*>::iterator endIt);

  /**
   * Calculate time penalty of simplified route.
   * @return Time penalty as percentage
   */
  double CalculateDurationPenaltyPercent() const;

  /**
   * Calculate total route time.
   * @param route Route to calculate time for
   * @return Total time in hours
   */
  wxTimeSpan CalculateRouteDuration(const std::list<Position*>& route) const;

  /**
   * Calculate initial epsilon for Douglas-Peucker.
   * @return Initial epsilon value
   */
  double CalculateInitialEpsilon() const;

  /**
   * Apply Douglas-Peucker simplification.
   * @param route Route to simplify (modified in place)
   * @param epsilon Distance threshold
   */
  void ApplyDouglasPeucker(std::list<Position*>& route, double epsilon);

  /**
   * Recursive Douglas-Peucker implementation.
   * @param waypoints Vector of positions
   * @param startIdx Start index
   * @param endIdx End index
   * @param epsilon Distance threshold
   * @param keep Vector marking points to keep
   */
  void DouglasPeuckerRecursive(const std::vector<Position*>& waypoints,
                               int startIdx, int endIdx, double epsilon,
                               std::vector<bool>& keep);

  /**
   * Calculate perpendicular distance from waypoint to line.
   * @param waypoint Waypoint to measure from
   * @param lineStart Start of line segment
   * @param lineEnd End of line segment
   * @return Perpendicular distance
   */
  double PerpendicularDistance(Position* waypoint, Position* lineStart,
                               Position* lineEnd);

  /**
   * Find closest position in an IsoRoute to given coordinates.
   * @param route IsoRoute to search
   * @param lat Target latitude
   * @param lon Target longitude
   * @return Closest position or nullptr
   */
  static Position* FindClosestPositionInRoute(IsoRoute* route, double lat,
                                              double lon);

  /**
   * Find the time at which a position was reached by searching through
   * isochrones.
   * @param position Position to find time for
   * @param isochrones List of isochrones to search
   * @return Time span from start, or -1 if not found
   */
  wxTimeSpan FindPositionTime(Position* position,
                              const IsoChronList& isochrones) const;

  /**
   * Find time for a position using parent chain when not found in isochrones.
   * @param position Position to find time for
   * @param isochrones List of isochrones to search
   * @param startTime Start time from first isochrone
   * @return Time span from start, or -1 if not found
   */
  wxTimeSpan FindPositionTimeByParentChain(Position* position,
                                           const IsoChronList& isochrones,
                                           const wxDateTime& startTime) const;

  /**
   * Find an alternate path through existing isochrones with fewer maneuvers.
   * @param isochrones List of existing isochrones
   * @param config Configuration with modified maneuver penalties
   * @param destination Original destination position
   * @param result [out] Alternate route if found
   * @return true if a valid alternate path was found
   */
  bool FindAlternatePathThroughIsochrones(const SimplificationParams& params,
                                          const IsoChronList& isochrones,
                                          const RouteMapConfiguration& config,
                                          Position* destination,
                                          RouteStats& result);

  /**
   * Find an alternate path through existing isochrones with fewer maneuvers.
   * @param isochrones List of existing isochrones
   * @param config Configuration with modified maneuver penalties
   * @param destination Original destination position
   * @param penaltyAmount Amount of penalty applied to maneuvers
   * @param result [out] Alternate route if found
   * @return true if a valid alternate path was found
   */
  bool FindAlternatePath(const IsoChronList& isochrones,
                         const RouteMapConfiguration& config,
                         Position* destination, double penaltyAmount,
                         RouteStats& result);

  RouteMapOverlay* m_routemap;
  RouteMapConfiguration m_configuration;
  /** Original route configuration. */
  std::list<Position*> m_originalRoute;
  /** Simplified route configuration. */
  std::list<Position*> m_simplifiedRoute;

  std::vector<Position*> m_newPositions;
};

#endif
