/***************************************************************************
 *   Copyright (C) 2015 by OpenCPN development team                        *
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

#ifndef _WEATHER_ROUTING_CONSTRAINT_CHECKER_H_
#define _WEATHER_ROUTING_CONSTRAINT_CHECKER_H_

struct RouteMapConfiguration;

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
 * Class for checking routing constraints.
 *
 * This class centralizes constraint checking logic that is shared between
 * different route propagation methods (Propagate, PropagateToPoint, etc.).
 * It ensures consistent application of constraints across the codebase.
 */
class ConstraintChecker {
public:
  /**
   * Check if swell constraint is met at the given position.
   *
   * Verifies if the swell at the specified position is within the maximum
   * allowed limits configured in the route map configuration.
   *
   * @param configuration The route map configuration containing constraint
   * parameters
   * @param lat Latitude of the position to check
   * @param lon Longitude of the position to check
   * @param swell [out] The swell value at the position if available
   * @param error_code [out] Error code set to PROPAGATION_EXCEEDED_MAX_SWELL on
   * failure
   * @return true if constraint is met, false otherwise
   */
  static bool CheckSwellConstraint(RouteMapConfiguration& configuration,
                                   double lat, double lon, double& swell,
                                   PropagationError& error_code);

  /**
   * Check if the position is within the maximum latitude constraint.
   *
   * Verifies if the given latitude is within the maximum allowed latitude
   * defined in the route map configuration.
   *
   * @param configuration The route map configuration containing constraint
   * parameters
   * @param lat Latitude of the position to check
   * @param error_code [out] Error code set to PROPAGATION_EXCEEDED_MAX_LATITUDE
   * on failure
   * @return true if constraint is met, false otherwise
   */
  static bool CheckMaxLatitudeConstraint(RouteMapConfiguration& configuration,
                                         double lat,
                                         PropagationError& error_code);

  /**
   * Check if a route segment crosses any cyclone tracks.
   *
   * Verifies if traveling from (lat,lon) to (dlat,dlon) would cross
   * any cyclone tracks defined in the route map configuration.
   *
   * @param configuration The route map configuration containing cyclone track
   * data
   * @param lat Starting latitude
   * @param lon Starting longitude
   * @param dlat Destination latitude
   * @param dlon Destination longitude
   * @return true if the route segment doesn't cross any cyclone tracks, false
   * otherwise
   */
  static bool CheckCycloneTrackConstraint(RouteMapConfiguration& configuration,
                                          double lat, double lon, double dlat,
                                          double dlon);

  /**
   * Check if the maximum course angle constraint is met.
   *
   * Verifies if the course to the destination position (dlat,dlon)
   * doesn't exceed the maximum allowed course angle in the configuration.
   *
   * @param configuration The route map configuration containing constraint
   * parameters
   * @param dlat Destination latitude
   * @param dlon Destination longitude
   * @return true if constraint is met, false otherwise
   */
  static bool CheckMaxCourseAngleConstraint(
      RouteMapConfiguration& configuration, double dlat, double dlon);

  /**
   * Check if the maximum diverted course constraint is met.
   *
   * Verifies if the course to the destination position (dlat,dlon)
   * doesn't divert too far from the ideal course to the final destination.
   *
   * @param configuration The route map configuration containing constraint
   * parameters
   * @param dlat Destination latitude
   * @param dlon Destination longitude
   * @return true if constraint is met, false otherwise
   */
  static bool CheckMaxDivertedCourse(RouteMapConfiguration& configuration,
                                     double dlat, double dlon);

  /**
   * Check if a position avoids land areas with appropriate safety margin.
   *
   * Verifies if the destination position (dlat,dlon) doesn't intersect land
   * or violate the configured land safety margin.
   *
   * @param configuration The route map configuration containing constraint
   * parameters
   * @param lat Starting latitude
   * @param lon Starting longitude
   * @param dlat Destination latitude
   * @param dlon Destination longitude
   * @return true if constraint is met, false otherwise
   */
  static bool CheckLandConstraint(RouteMapConfiguration& configuration,
                                  double lat, double lon, double dlat,
                                  double dlon, double cog);
};

#endif