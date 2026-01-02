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

#ifndef _WEATHER_ROUTING_POSITION_H_
#define _WEATHER_ROUTING_POSITION_H_

#include <json/json.h>
#include <wx/wx.h>

#include "RoutePoint.h"
#include "IsoRoute.h"
#include "ConstraintChecker.h"


class SkipPosition;
class WR_GribRecordSet;

/**
 * Circular linked list node for positions which take equal time to reach.
 */
class Position : public RoutePoint {
public:
  Position(double latitude, double longitude, Position* p = nullptr,
           double pheading = NAN, double pbearing = NAN, int polar_idx = -1,
           int tack_count = 0, int jibe_count = 0,
           int sail_plan_change_count = 0, DataMask data_mask = DataMask::NONE,
           bool data_deficient = false);
           Position* DeepCopy() const;
  Position(const Position* p);
  /**
   * Constructs a Position from a JSON object.
   * See also toJson() method.
   * @param json JSON object containing Position data.
   */
  Position(const Json::Value& json);
  SkipPosition* BuildSkipList();

  /**
   * Propagates a position forward in time, exploring all viable directions.
   *
   * This is the core routing function that expands an isochrone by calculating
   * all possible new positions reachable from the current position after the
   * configured time step. It systematically tests multiple directions to build
   * the next isochrone layer.
   *
   * @param routelist [out] List to store generated route segments
   * @param configuration Route configuration including:
   *        - Boat polars and performance data
   *        - Environmental constraints
   *        - Navigation limits
   *        - Time step parameters
   *
   * @return true if at least 3 valid positions were generated,
   *         false if propagation failed.
   */
  bool Propagate(IsoRouteList& routelist, RouteMapConfiguration& configuration,
                 bool& fatal_error);

  double Distance(const Position* p) const;
  // Return the number of times the sail configuration has changed.
  int SailChanges() const;
  double PropagateToEnd(RouteMapConfiguration& configuration, double& H,
                        DataMask& data_mask);

  /**
   * Generates a list of positions from this position back to the origin
   * by following parent pointers.
   *
   * @return List of positions from origin to this position
   */
  std::list<Position*> BuildRoute() {
    std::list<Position*> route;
    Position* current = this;

    // Follow parent pointers back to origin.
    while (current) {
      route.push_front(current);
      current = current->parent;
    }
    return route;
  }

    /**
    * Serializes the Position object to JSON format.
    *
    * This method converts the Position's data into a JSON object, which can be
    * used for storage, transmission, or debugging purposes. The JSON object will
    * include all relevant fields such as latitude, longitude, parent heading,
    * parent bearing, and propagation status.
    *
    * @param json Reference to a Json::Value object where the Position data will
    *             be stored.
  */
  void toJson(Json::Value &json) const;

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
               double& rk_dist, DataMask& data_mask);
  
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
  SkipPosition* DeepCopy() const;

  Position* point;
  SkipPosition *prev, *next;
  /**
   * Direction quadrant (0-3) indicating relationship between this point and
   * the next.
   */
  int quadrant;
};

void DeletePoints(Position* point);

int ComputeQuadrantFast(Position* p, Position* q);

#endif
