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
  static bool CheckSwellConstraint(double lat, double lon,
                                   RouteMapConfiguration& configuration,
                                   PropagationError& error_code);
};

#endif