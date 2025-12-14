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

#include <wx/wx.h>

#include "ocpn_plugin.h"
#include "Utilities.h"
#include "RouteSimplifier.h"
#include "RouteMapOverlay.h"

#include <algorithm>
#include <cmath>

RouteSimplifier::RouteSimplifier(RouteMapOverlay* routemap)
    : m_routemap(routemap) {
  if (routemap) {
    m_configuration = routemap->GetConfiguration();
    m_originalRoute = ExtractPositionsFromRouteMap(routemap);
  }
}

RouteSimplifier::~RouteSimplifier() {
  // Clean up any positions we created during simplification.
  for (Position* pos : m_newPositions) {
    delete pos;
  }
}

std::list<Position*> RouteSimplifier::ExtractPositionsFromRouteMap(
    RouteMapOverlay* routemap) {
  std::list<Position*> positions;

  if (!routemap) return positions;

  // Get the last destination position (always set after route completion).
  // This may be the exact destination or the position closest to the
  // destination. In most cases, it's not the exact destination because the
  // destination is somewhere between positions in the isochrones.
  Position* pos = routemap->GetLastDestination();
  if (!pos) {
    // No route has been calculated yet.
    return positions;
  }

  // Traverse back through parents to build the complete route to the origin
  // (the starting position).
  while (pos) {
    positions.push_front(pos);
    pos = pos->parent;
  }
  assert(routemap->GetLastDestination() == positions.back());
  return positions;
}

std::vector<RouteSegment> RouteSimplifier::SplitRouteByManeuvers(
    const std::list<Position*>& route) {
  std::vector<RouteSegment> segments;

  if (route.size() < 2) {
    // Not enough points to segment.
    if (!route.empty()) {
      RouteSegment segment;
      segment.points.push_back(route.front());
      segments.push_back(segment);
    }
    return segments;
  }

  RouteSegment currentSegment;
  currentSegment.points.push_back(route.front());

  auto it = std::next(route.begin());
  while (it != route.end()) {
    Position* prevPos = currentSegment.points.back();
    Position* currentPos = *it;

    // Add to current segment.
    currentSegment.points.push_back(currentPos);

    // Check if a maneuver occurred between prevPos and currentPos.
    bool isManeuver = false;
    int maneuverType = RouteSegment::NONE;

    // Compare the cumulative maneuver counts.
    if (currentPos->tacks > prevPos->tacks) {
      isManeuver = true;
      maneuverType = RouteSegment::TACK;
    } else if (currentPos->jibes > prevPos->jibes) {
      isManeuver = true;
      maneuverType = RouteSegment::JIBE;
    } else if (currentPos->sail_plan_changes > prevPos->sail_plan_changes) {
      isManeuver = true;
      maneuverType = RouteSegment::SAIL_CHANGE;
    }

    // If maneuver occurred and not at the end of route, finish this segment.
    if (isManeuver && std::next(it) != route.end()) {
      currentSegment.maneuverType = maneuverType;

      // Calculate original time for this segment.
      currentSegment.originalDuration =
          CalculateSegmentDuration(currentSegment.points);

      segments.push_back(currentSegment);

      // Start a new segment with the maneuver point.
      currentSegment = RouteSegment();
      currentSegment.points.push_back(currentPos);
    }

    ++it;
  }

  // Add the last segment if not empty.
  if (!currentSegment.points.empty()) {
    currentSegment.originalDuration =
        CalculateSegmentDuration(currentSegment.points);
    segments.push_back(currentSegment);
  }
  wxLogGeneric(
      wxLOG_Debug,
      "RouteSimplifier: Split route into %zu segments at maneuver points",
      segments.size());
  return segments;
}

wxTimeSpan RouteSimplifier::CalculateSegmentDuration(
    const std::list<Position*>& segment) const {
  if (segment.size() <= 1) {
    wxLogGeneric(wxLOG_Debug, "Segment has only %zu waypoints", segment.size());
    return wxTimeSpan(-1);  // No duration for a single point or empty segment.
  }

  // Calculate duration by finding which isochrones the positions belong to
  // and using their actual time stamps, rather than assuming constant delta
  // time.
  Position* firstPos = segment.front();
  Position* lastPos = segment.back();

  if (!m_routemap) {
    wxLogGeneric(
        wxLOG_Debug,
        "RouteSimplifier: No routemap available for duration calculation");
    return wxTimeSpan(-1);
  }

  const IsoChronList& isochrones = m_routemap->GetIsoChronList();
  if (isochrones.empty()) {
    wxLogGeneric(
        wxLOG_Debug,
        "RouteSimplifier: No isochrones available for duration calculation");
    return wxTimeSpan(-1);
  }

  // Find which isochrone each position belongs to by traversing parent chain
  wxTimeSpan firstTime = FindPositionTime(firstPos, isochrones);
  wxTimeSpan lastTime = FindPositionTime(lastPos, isochrones);

  if (firstTime < 0 || lastTime < 0) {
    wxLogMessage(
        "RouteSimplifier: Failed to find position times in isochrones");
    return wxTimeSpan(-1);
  }

  if (lastTime < firstTime) {
    wxLogMessage("RouteSimplifier: Invalid time order: first=%s, last=%s",
                 firstTime.Format("%D days %H:%M:%S"),
                 lastTime.Format("%D days %H:%M:%S"));
    return wxTimeSpan(-1);
  }

  wxTimeSpan duration = lastTime - firstTime;

  wxLogGeneric(
      wxLOG_Debug,
      "RouteSimplifier: Segment duration calculated: first=%s, last=%s, "
      "duration=%s",
      firstTime.Format("%D days %H:%M:%S"), lastTime.Format("%D days %H:%M:%S"),
      duration.Format("%D days %H:%M:%S"));

  return duration;
}

SimplificationResult RouteSimplifier::Simplify(
    const SimplificationParams& params) {
  SimplificationResult result;
  result.success = false;
  result.waypointReduction = 0;
  result.durationDelta = wxTimeSpan(0);
  result.durationDeltaPercent = 0.0;
  result.originalManeuverCount = 0;
  result.simplifiedRoute.clear();

  // Validate input.
  if (m_originalRoute.size() < 3) {
    result.message = wxString::Format(
        "Route too short to simplify (%zu points)", m_originalRoute.size());
    wxLogMessage("RouteSimplifier: %s", result.message);
    return result;
  }

  wxLogMessage(
      "RouteSimplifier: Simplifying route with %zu waypoints. "
      "maxDurationPenalty=%.1f%%, "
      "maxIterations=%d",
      m_originalRoute.size(), params.maxDurationPenaltyPercent * 100.0,
      params.maxIterations);

  // Find alternate routes with fewer maneuvers.
  std::list<Position*> routeToSimplify = m_originalRoute;
  wxTimeSpan originalDuration = CalculateRouteDuration(m_originalRoute);
  if (originalDuration <= 0) {
    result.message = wxString::Format("Failed to get route duration");
    return result;
  }

  // Since maneuver counts are cumulative, we can get totals from last position.
  int initialTacks = 0, initialJibes = 0, initialSailChanges = 0;
  if (!m_originalRoute.empty()) {
    Position* lastPos = m_originalRoute.back();
    initialTacks = lastPos->tacks;
    initialJibes = lastPos->jibes;
    initialSailChanges = lastPos->sail_plan_changes;
  }
  int initialManeuvers = initialTacks + initialJibes + initialSailChanges;
  result.originalManeuverCount = initialManeuvers;
  result.simplifiedManeuverCount = initialManeuvers;

  // Only try to find alternate routes if the original route has meaningful
  // number of maneuvers to reduce.
  if (initialTacks > 2 || initialJibes > 2 || initialSailChanges >= 2) {
    std::vector<RouteStats> alternateRoutes =
        FindAlternateRoutesWithFewerManeuvers(params, 5 /*max_routes*/);

    if (alternateRoutes.size() > 1) {
      // If alternates were found (first route is original)
      // Find the best alternate with smallest duration penalty.
      for (size_t i = 1; i < alternateRoutes.size(); i++) {
        const RouteStats& alternate = alternateRoutes[i];
        double durationPenaltyPercent =
            (alternate.totalDuration.GetSeconds().ToDouble() -
             originalDuration.GetSeconds().ToDouble()) /
            originalDuration.GetSeconds().ToDouble();

        if (durationPenaltyPercent <= params.maxDurationPenaltyPercent &&
            alternate.totalManeuvers < initialManeuvers) {
          wxLogGeneric(
              wxLOG_Debug,
              "RouteSimplifier: Selected alternate route with %d maneuvers "
              "(reduced from %d) with %.1f%% duration penalty",
              alternate.totalManeuvers, initialManeuvers,
              durationPenaltyPercent * 100.0);

          routeToSimplify = alternate.waypoints;
          result.maneuverReduction =
              initialManeuvers - alternate.totalManeuvers;
          result.simplifiedManeuverCount = alternate.totalManeuvers;
          break;
        }
      }
    }
  }

  // Split route into segments at maneuver points.
  wxLogGeneric(wxLOG_Debug, "RouteSimplifier: Splitting route by maneuvers");
  std::vector<RouteSegment> segments = SplitRouteByManeuvers(routeToSimplify);

  // Simplify each segment with Douglas-Peucker.
  wxLogGeneric(wxLOG_Debug,
               "RouteSimplifier: Geometric simplification of segments");
  double epsilon = CalculateInitialEpsilon();

  size_t originalTotalPoints = routeToSimplify.size();
  size_t simplifiedTotalPoints = 0;
  // Total duration of the entire route after simplification.
  wxTimeSpan totalSimplifiedDuration = wxTimeSpan(0);

  // Process each segment.
  for (size_t i = 0; i < segments.size(); ++i) {
    RouteSegment& segment = segments[i];
    size_t originalSegmentSize = segment.points.size();

    // Skip segments that are too short to simplify.
    if (segment.points.size() < 3) {
      simplifiedTotalPoints += segment.points.size();
      result.simplifiedRoute.insert(result.simplifiedRoute.end(),
                                    segment.points.begin(),
                                    segment.points.end());
      totalSimplifiedDuration += segment.originalDuration;
      wxLogGeneric(
          wxLOG_Debug,
          "RouteSimplifier: Segment %zu too short to simplify (%zu points). "
          "Duration: %s",
          i, segment.points.size(),
          segment.originalDuration.Format("%D days %H:%M:%S"));
      continue;
    }

    // Apply Douglas-Peucker simplification to this segment.
    std::list<Position*> simplifiedSegment = segment.points;
    ApplyDouglasPeucker(simplifiedSegment, epsilon);

    wxLogGeneric(
        wxLOG_Debug,
        "RouteSimplifier: Segment %zu simplified from %zu to %zu points", i,
        originalSegmentSize, simplifiedSegment.size());

    // Validate simplified segment can be sailed.
    wxLogGeneric(wxLOG_Debug,
                 "RouteSimplifier: Validating simplified segment %zu with "
                 "RhumbLinePropagateToPoint",
                 i);

    // Only apply RhumbLinePropagateToPoint if simplification actually reduced
    // the number of points.
    if (simplifiedSegment.size() < segment.points.size()) {
      std::list<Position*> validatedSegment;
      validatedSegment.push_back(
          simplifiedSegment.front());  // Always start with the first point

      auto it = simplifiedSegment.begin();
      auto next = std::next(it);

      while (next != simplifiedSegment.end()) {
        Position* start = *it;
        Position* end = *next;

        // Maximum segment length in nautical miles.
        const double maxSegmentLength = 30.0;

        // Calculate direct distance.
        double distance =
            DistGreatCircle_Plugin(start->lat, start->lon, end->lat, end->lon);

        // For longer segments, use RhumbLinePropagateToPoint to validate.
        if (distance > maxSegmentLength) {
          std::vector<RoutePoint*> intermediatePoints;
          // Use the data mask from the starting point.
          DataMask data_mask = start->data_mask;
          double totalDistance = 0.0;
          double averageSpeed = 0.0;

          // Create a copy of the configuration to use for validation.
          RouteMapConfiguration tempConfig = m_configuration;

          wxLogGeneric(
              wxLOG_Debug,
              "RouteSimplifier: Segment from (%.6f,%.6f) to (%.6f,%.6f) "
              "distance=%.1f nm, validating with RhumbLinePropagateToPoint",
              start->lat, start->lon, end->lat, end->lon, distance);

          // Try to propagate along the rhumb line.
          double propagationTime = start->RhumbLinePropagateToPoint(
              end->lat, end->lon, tempConfig, intermediatePoints, data_mask,
              totalDistance, averageSpeed, maxSegmentLength);

          if (!std::isnan(propagationTime)) {
            wxLogGeneric(wxLOG_Debug,
                         "RouteSimplifier: Validated rhumb line segment: "
                         "%.1f nm, %.1f knots avg",
                         totalDistance, averageSpeed);

            // Add all intermediate waypoints to our validated segment.
            for (size_t j = 0; j < intermediatePoints.size(); j++) {
              RoutePoint* rp = intermediatePoints[j];

              // Convert RoutePoint to Position.
              // Skip the first point as it's already in our route.
              if (j > 0 || rp->lat != start->lat || rp->lon != start->lon) {
                Position* pos = new Position(rp->lat, rp->lon);
                // Set parent to maintain route structure.
                pos->parent = validatedSegment.back();
                pos->polar = rp->polar;
                pos->tacks = rp->tacks;
                pos->jibes = rp->jibes;
                pos->sail_plan_changes = rp->sail_plan_changes;
                pos->data_mask = rp->data_mask;
                // Add to our list for later cleanup.
                m_newPositions.push_back(pos);
                validatedSegment.push_back(pos);
              }

              // Clean up RoutePoint since we converted it to Position.
              delete rp;
            }
          } else {
            // If rhumb line propagation failed, fall back to direct connection.
            wxLogGeneric(
                wxLOG_Debug,
                "RouteSimplifier: Rhumb line propagation failed, using direct "
                "connection");
            validatedSegment.push_back(end);
          }
        } else {
          // For shorter segments, just add the endpoint directly.
          validatedSegment.push_back(end);
        }

        // Move to next segment.
        it = next;
        ++next;
      }

      // Replace simplified segment with validated segment if it's reasonable.
      if (!validatedSegment.empty() &&
          validatedSegment.size() <= segment.points.size() * 0.8) {
        wxLogGeneric(
            wxLOG_Debug,
            "RouteSimplifier: Using validated segment with %zu points (vs "
            "original %zu)",
            validatedSegment.size(), segment.points.size());
        simplifiedSegment = validatedSegment;
      }
    }

    // Calculate time for simplified segment.
    wxTimeSpan segmentSimplifiedDuration =
        CalculateRouteDuration(simplifiedSegment);
    if (segmentSimplifiedDuration <= 0) {
      continue;
    }
    totalSimplifiedDuration += segmentSimplifiedDuration;
    wxLogGeneric(wxLOG_Debug, "RouteSimplifier: Segment %zu duration = %s", i,
                 segmentSimplifiedDuration.Format("%D days %H:%M:%S"));

    // Add this segment to the final route.
    result.simplifiedRoute.insert(result.simplifiedRoute.end(),
                                  simplifiedSegment.begin(),
                                  simplifiedSegment.end());

    // Update statistics.
    simplifiedTotalPoints += simplifiedSegment.size();
  }

  // Calculate final statistics.
  int waypointReduction = originalTotalPoints - simplifiedTotalPoints;
  // Prepare result.
  if (waypointReduction > 0 || result.maneuverReduction > 0) {
    result.success = true;
    result.waypointReduction = waypointReduction;
    result.totalDuration = totalSimplifiedDuration;
    // Positive delta means we took longer than original route.
    result.durationDelta = totalSimplifiedDuration - originalDuration;
    result.durationDeltaPercent =
        (result.durationDelta.GetSeconds().ToDouble() /
         originalDuration.GetSeconds().ToDouble());

    result.message = wxString::Format(
        "Waypoints: from %zu to %zu\n"
        "Maneuvers: from %d to %d\n"
        "Duration: %s\n"
        "Delta: %s",
        originalTotalPoints, simplifiedTotalPoints,
        result.originalManeuverCount, result.simplifiedManeuverCount,
        result.totalDuration.Format(_("%D days %H:%M:%S")),
        result.durationDelta.Format(_("%D days %H:%M:%S")));
  } else {
    result.success = false;
    result.simplifiedRoute = m_originalRoute;
    result.message = "Failed to find acceptable simplification";
  }

  wxLogMessage("RouteSimplifier: %s", result.message);
  return result;
}

std::list<Position*> RouteSimplifier::BuildValidatedRoute(
    const std::list<Position*>& candidateRoute) {
  std::list<Position*> validatedRoute;

  // Always start with the first point.
  validatedRoute.push_back(candidateRoute.front());

  auto it1 = candidateRoute.begin();
  auto it2 = std::next(it1);

  while (it2 != candidateRoute.end()) {
    Position* validatedEnd = nullptr;

    if (ValidateSegment(*it1, *it2, validatedEnd)) {
      // Direct segment is valid.
      validatedRoute.push_back(validatedEnd);
    } else {
      // Segment failed validation.
      // Use a more relaxed insertion strategy.
      InsertRequiredWaypoints(validatedRoute, *it1, *it2);
      validatedRoute.push_back(*it2);
    }

    it1 = it2;
    ++it2;
  }

  return validatedRoute;
}

bool RouteSimplifier::ValidateSegment(Position* start, Position* end,
                                      Position*& validatedEnd) {
  // First check segment distance - if it's too long, it likely needs
  // intermediate points
  double distance =
      DistGreatCircle_Plugin(start->lat, start->lon, end->lat, end->lon);

  // If segment is very long, it probably needs intermediate waypoints
  if (distance > 50.0) {  // 50 nautical miles threshold
    return false;
  }

  // Try simple propagation
  double heading;
  DataMask data_mask = DataMask::NONE;
  // Create non-const copy of configuration to pass to PropagateToPoint
  RouteMapConfiguration config = m_configuration;
  double time = start->PropagateToPoint(end->lat, end->lon, config, heading,
                                        data_mask, false);

  if (!std::isnan(time) && !std::isinf(time) && time >= 0) {
    validatedEnd = end;
    return true;
  }

  // If simple propagation failed and the segment is short, accept it anyway
  if (distance < 20.0) {  // Short segments (< 20nm) are generally acceptable
    validatedEnd = end;
    return true;
  }

  // For medium-length segments, try more sophisticated validation
  return ValidateSegmentWithDetailedPropagation(start, end, validatedEnd);
}

bool RouteSimplifier::ValidateSegmentWithDetailedPropagation(
    Position* start, Position* end, Position*& validatedEnd) {
  // Calculate bearing and distance
  double bearing, distance;
  DistanceBearingMercator_Plugin(start->lat, start->lon, end->lat, end->lon,
                                 &bearing, &distance);

  // Set up narrow search configuration
  RouteMapConfiguration tempConfig = m_configuration;
  const double narrowAngle = 10.0;  // Slightly wider angle for better chances
  tempConfig.MaxSearchAngle = narrowAngle;

  // Get wind data for proper angle calculation
  double twd, tws;
  DataMask data_mask = DataMask::NONE;
  if (!start->GetWindData(tempConfig, twd, tws, data_mask)) {
    // If we can't get wind data, accept the segment anyway for short distances
    if (distance < 30.0) {
      validatedEnd = end;
      return true;
    }
    return false;
  }

  // Calculate TWA for desired bearing
  double twa = heading_resolve(bearing - twd);

  // Set up narrow degree range
  tempConfig.FromDegree = twa - narrowAngle;
  tempConfig.ToDegree = twa + narrowAngle;
  tempConfig.ByDegrees = narrowAngle / 3.0;
  tempConfig.EndLat = end->lat;
  tempConfig.EndLon = end->lon;

  // Generate degree steps
  tempConfig.DegreeSteps.clear();
  double angle = tempConfig.FromDegree;
  while (angle <=
         tempConfig.ToDegree + 1E-3) {  // Avoid missing the last step by a tiny
                                        // fraction, due to rounding errors
    tempConfig.DegreeSteps.push_back(heading_resolve(angle));
    angle += tempConfig.ByDegrees;
  }

  // Try propagation
  IsoRouteList routelist;
  if (start->Propagate(routelist, tempConfig) && !routelist.empty()) {
    // Find closest position to target
    Position* bestPosition = nullptr;
    double minDistance = INFINITY;

    for (IsoRoute* route : routelist) {
      Position* pos = FindClosestPositionInRoute(route, end->lat, end->lon);
      if (pos) {
        double dist =
            DistGreatCircle_Plugin(pos->lat, pos->lon, end->lat, end->lon);
        if (dist < minDistance) {
          minDistance = dist;
          bestPosition = pos;
        }
      }
    }

    // Accept if close enough
    if (bestPosition && minDistance < 1.0) {  // Within 1 nm
      validatedEnd = new Position(
          bestPosition->lat, bestPosition->lon, start,
          bestPosition->parent_heading, bestPosition->parent_bearing,
          bestPosition->polar, bestPosition->tacks, bestPosition->jibes,
          bestPosition->sail_plan_changes, bestPosition->data_mask);
      m_newPositions.push_back(validatedEnd);
      return true;
    }
  }

  // If propagation failed but segment is relatively short, accept it
  if (distance < 40.0) {
    validatedEnd = end;
    return true;
  }

  return false;
}

void RouteSimplifier::InsertRequiredWaypoints(std::list<Position*>& route,
                                              Position* start, Position* end) {
  // Find the segment in original route
  auto startIt =
      std::find(m_originalRoute.begin(), m_originalRoute.end(), start);
  auto endIt = std::find(m_originalRoute.begin(), m_originalRoute.end(), end);

  if (startIt == m_originalRoute.end() || endIt == m_originalRoute.end()) {
    wxLogMessage(
        "RouteSimplifier: Warning - positions not found in original route");
    return;
  }

  int segmentLength = std::distance(startIt, endIt);

  if (segmentLength <= 1) {
    return;  // Adjacent points, nothing to insert
  }

  // Calculate distance of the segment
  double distance =
      DistGreatCircle_Plugin(start->lat, start->lon, end->lat, end->lon);

  // For short segments, be more aggressive about simplification
  if (distance < 30.0) {  // Short distance
    // Only insert critical waypoints
    if (segmentLength <= 3) {
      // Very short segment in original route - insert all
      for (auto it = std::next(startIt); it != endIt; ++it) {
        route.push_back(*it);
      }
      return;
    }

    // For longer segments, insert every N-th waypoint based on distance
    int skipFactor = std::max(1, static_cast<int>(30.0 / distance));
    skipFactor = std::min(skipFactor, segmentLength / 3);

    int count = 0;
    for (auto it = std::next(startIt); it != endIt; ++it) {
      if (++count % skipFactor == 0) {
        route.push_back(*it);
      }
    }
    return;
  }

  // For longer segments, use adaptive strategy
  if (segmentLength <= 5) {
    // Medium segment: insert all points
    for (auto it = std::next(startIt); it != endIt; ++it) {
      route.push_back(*it);
    }
  } else {
    // Large segment: use binary subdivision with relaxed validation
    BinarySubdivision(route, startIt, endIt);
  }
}

void RouteSimplifier::BinarySubdivision(std::list<Position*>& route,
                                        std::list<Position*>::iterator startIt,
                                        std::list<Position*>::iterator endIt) {
  int distance = std::distance(startIt, endIt);
  if (distance <= 1) return;

  auto midIt = startIt;
  std::advance(midIt, distance / 2);

  Position* validatedMid = nullptr;
  if (ValidateSegment(*startIt, *midIt, validatedMid)) {
    route.push_back(validatedMid);
    BinarySubdivision(route, midIt, endIt);
  } else {
    // First half failed
    // For very long segments, don't insert everything
    if (distance > 10) {
      // Recurse on first half
      BinarySubdivision(route, startIt, midIt);
      route.push_back(*midIt);
      BinarySubdivision(route, midIt, endIt);
    } else {
      // Small enough to insert all
      for (auto it = std::next(startIt); it != midIt; ++it) {
        route.push_back(*it);
      }
      route.push_back(*midIt);
      BinarySubdivision(route, midIt, endIt);
    }
  }
}

double RouteSimplifier::CalculateDurationPenaltyPercent() const {
  if (m_originalRoute.empty() || m_simplifiedRoute.empty()) return 0.0;

  wxTimeSpan originalTime = CalculateRouteDuration(m_originalRoute);
  wxTimeSpan simplifiedTime = CalculateRouteDuration(m_simplifiedRoute);

  // Check if timespan is valid/usable.
  if (originalTime < 0) {
    return INFINITY;
  }

  if (originalTime <= 0 || simplifiedTime <= 0) {
    return INFINITY;
  }

  // Calculate penalty as percentage
  double originalSeconds = originalTime.GetSeconds().ToDouble();
  double simplifiedSeconds = simplifiedTime.GetSeconds().ToDouble();

  return ((simplifiedSeconds - originalSeconds) / originalSeconds);
}

wxTimeSpan RouteSimplifier::CalculateRouteDuration(
    const std::list<Position*>& route) const {
  // This is essentially a wrapper around CalculateSegmentDuration()
  // since a route is just a single segment in this context
  return CalculateSegmentDuration(route);
}

double RouteSimplifier::CalculateInitialEpsilon() const {
  // Calculate geographic bounds
  double minLat = 90.0, maxLat = -90.0, minLon = 180.0, maxLon = -180.0;

  for (Position* pos : m_originalRoute) {
    minLat = std::min(minLat, pos->lat);
    maxLat = std::max(maxLat, pos->lat);
    minLon = std::min(minLon, pos->lon);
    maxLon = std::max(maxLon, pos->lon);
  }

  // Calculate route diagonal
  double latRange = maxLat - minLat;
  double lonRange = maxLon - minLon;
  double diagonal = std::sqrt(latRange * latRange + lonRange * lonRange);

  // Initial epsilon is proportional to route size
  return diagonal * 0.0001;
}

void RouteSimplifier::ApplyDouglasPeucker(std::list<Position*>& route,
                                          double epsilon) {
  if (route.size() <= 2) return;

  // Convert to vector for easier indexing
  std::vector<Position*> points(route.begin(), route.end());
  std::vector<bool> keep(points.size(), false);

  // Always keep endpoints
  keep[0] = true;
  keep[points.size() - 1] = true;

  // Recursive simplification
  DouglasPeuckerRecursive(points, 0, points.size() - 1, epsilon, keep);

  // Rebuild route with kept points
  route.clear();
  for (size_t i = 0; i < points.size(); ++i) {
    if (keep[i]) {
      route.push_back(points[i]);
    }
  }
}

void RouteSimplifier::DouglasPeuckerRecursive(
    const std::vector<Position*>& points, int startIdx, int endIdx,
    double epsilon, std::vector<bool>& keep) {
  if (endIdx <= startIdx + 1) return;

  // Find point with maximum distance from line
  double maxDist = 0;
  int maxIdx = startIdx;

  for (int i = startIdx + 1; i < endIdx; ++i) {
    double dist =
        PerpendicularDistance(points[i], points[startIdx], points[endIdx]);
    if (dist > maxDist) {
      maxDist = dist;
      maxIdx = i;
    }
  }

  // If max distance exceeds epsilon, keep point and recurse
  if (maxDist > epsilon) {
    keep[maxIdx] = true;
    DouglasPeuckerRecursive(points, startIdx, maxIdx, epsilon, keep);
    DouglasPeuckerRecursive(points, maxIdx, endIdx, epsilon, keep);
  }
}

double RouteSimplifier::PerpendicularDistance(Position* point,
                                              Position* lineStart,
                                              Position* lineEnd) {
  // Convert to local coordinates for more accurate calculation
  double x0 = point->lon;
  double y0 = point->lat;
  double x1 = lineStart->lon;
  double y1 = lineStart->lat;
  double x2 = lineEnd->lon;
  double y2 = lineEnd->lat;

  // Handle degenerate case
  double dx = x2 - x1;
  double dy = y2 - y1;
  double lineLengthSq = dx * dx + dy * dy;

  if (lineLengthSq < 1e-10) {
    // Points are essentially the same
    dx = x0 - x1;
    dy = y0 - y1;
    return std::sqrt(dx * dx + dy * dy);
  }

  // Calculate perpendicular distance
  double t = ((x0 - x1) * dx + (y0 - y1) * dy) / lineLengthSq;

  if (t < 0) {
    // Closest to start point
    dx = x0 - x1;
    dy = y0 - y1;
  } else if (t > 1) {
    // Closest to end point
    dx = x0 - x2;
    dy = y0 - y2;
  } else {
    // Perpendicular to line
    double projX = x1 + t * dx;
    double projY = y1 + t * dy;
    dx = x0 - projX;
    dy = y0 - projY;
  }

  return std::sqrt(dx * dx + dy * dy);
}

Position* RouteSimplifier::FindClosestPositionInRoute(IsoRoute* route,
                                                      double lat, double lon) {
  if (!route || !route->skippoints) return nullptr;

  Position* closest = nullptr;
  double minDist = INFINITY;

  SkipPosition* skippos = route->skippoints;
  do {
    Position* pos = skippos->point;
    if (pos) {
      double dist = DistGreatCircle_Plugin(pos->lat, pos->lon, lat, lon);
      if (dist < minDist) {
        minDist = dist;
        closest = pos;
      }
    }
    skippos = skippos->next;
  } while (skippos != route->skippoints);

  return closest;
}

wxTimeSpan RouteSimplifier::FindPositionTime(
    Position* position, const IsoChronList& isochrones) const {
  if (!position || isochrones.empty()) {
    return wxTimeSpan(-1);
  }

  // Get the start time from the first isochrone
  wxDateTime startTime;
  if (!isochrones.empty() && isochrones.front()) {
    startTime = isochrones.front()->time;
  } else {
    return wxTimeSpan(-1);
  }

  // Search through each isochrone to find the one containing our position
  for (const IsoChron* isochron : isochrones) {
    if (!isochron) continue;

    // Check each route within this isochrone
    for (IsoRoute* route : isochron->routes) {
      if (!route || !route->skippoints) continue;

      // Check each position in this route
      SkipPosition* skippos = route->skippoints;
      do {
        if (skippos->point == position) {
          // Found the position! Return the time difference from start
          wxTimeSpan timeFromStart = isochron->time - startTime;
          wxLogGeneric(wxLOG_Debug,
                       "RouteSimplifier: Found position at time %s (offset %s)",
                       isochron->time.Format("%H:%M:%S"),
                       timeFromStart.Format("%D days %H:%M:%S"));
          return timeFromStart;
        }
        skippos = skippos->next;
      } while (skippos != route->skippoints);
    }
  }

  // If not found by pointer, try geometric containment check
  // This handles destination positions that are interpolated within isochrone
  // polygons
  for (const IsoChron* isochron : isochrones) {
    if (!isochron) continue;

    // Need to cast away const for Contains method
    IsoChron* nonConstIsochron = const_cast<IsoChron*>(isochron);
    if (nonConstIsochron->Contains(*position)) {
      wxTimeSpan timeFromStart = isochron->time - startTime;
      wxLogGeneric(
          wxLOG_Debug,
          "RouteSimplifier: Position found by geometric containment at time %s "
          "(offset %s)",
          isochron->time.Format("%H:%M:%S"),
          timeFromStart.Format("%D days %H:%M:%S"));
      return timeFromStart;
    }
  }

  // If not found directly, try to find by exact coordinates as fallback
  // This handles cases where positions might have been created during
  // simplification
  const double tolerance =
      1e-8;  // Very small tolerance for coordinate comparison
  for (const IsoChron* isochron : isochrones) {
    if (!isochron) continue;

    for (IsoRoute* route : isochron->routes) {
      if (!route || !route->skippoints) continue;

      SkipPosition* skippos = route->skippoints;
      do {
        Position* pos = skippos->point;
        if (pos && std::abs(pos->lat - position->lat) < tolerance &&
            std::abs(pos->lon - position->lon) < tolerance) {
          wxTimeSpan timeFromStart = isochron->time - startTime;
          wxLogGeneric(
              wxLOG_Debug,
              "RouteSimplifier: Found position by coordinates at time %s "
              "(offset %s)",
              isochron->time.Format("%H:%M:%S"),
              timeFromStart.Format("%D days %H:%M:%S"));
          return timeFromStart;
        }
        skippos = skippos->next;
      } while (skippos != route->skippoints);
    }
  }

  // Position not found in isochrones. Use parent chain fallback method.
  // This handles positions created outside the normal isochrone propagation
  wxLogMessage(
      "RouteSimplifier: Position not found in any isochrone (%.6f, %.6f), "
      "using parent chain fallback",
      position->lat, position->lon);

  return FindPositionTimeByParentChain(position, isochrones, startTime);
}

wxTimeSpan RouteSimplifier::FindPositionTimeByParentChain(
    Position* position, const IsoChronList& isochrones,
    const wxDateTime& startTime) const {
  if (!position) {
    return wxTimeSpan(-1);
  }

  // Walk up the parent chain to find a position that exists in the isochrones
  Position* current = position;
  std::vector<Position*> parentChain;

  while (current) {
    parentChain.push_back(current);

    // Check if this position exists in any isochrone
    for (const IsoChron* isochron : isochrones) {
      if (!isochron) continue;

      for (IsoRoute* route : isochron->routes) {
        if (!route || !route->skippoints) continue;

        SkipPosition* skippos = route->skippoints;
        do {
          if (skippos->point == current) {
            // Found a parent position in the isochrones!
            wxTimeSpan parentTime = isochron->time - startTime;

            // If this is the position we're looking for, return its time
            if (current == position) {
              wxLogGeneric(
                  wxLOG_Debug,
                  "RouteSimplifier: Found position in parent chain at time %s",
                  parentTime.Format("%D days %H:%M:%S"));
              return parentTime;
            }

            // Otherwise, estimate the time by using the average delta time
            // from the parent to the target position
            int stepsFromParent = parentChain.size() - 1;
            if (stepsFromParent > 0 && isochrones.size() >= 2) {
              // Calculate average delta time between isochrones
              auto it1 = isochrones.begin();
              auto it2 = std::next(it1);
              wxTimeSpan avgDelta = (*it2)->time - (*it1)->time;

              wxTimeSpan estimatedTime =
                  parentTime + avgDelta * stepsFromParent;
              wxLogGeneric(
                  wxLOG_Debug,
                  "RouteSimplifier: Estimated time for position: parent time "
                  "%s + %d steps * %s = %s",
                  parentTime.Format("%D days %H:%M:%S"), stepsFromParent,
                  avgDelta.Format("%D days %H:%M:%S"),
                  estimatedTime.Format("%D days %H:%M:%S"));
              return estimatedTime;
            }

            // If we can't estimate, just return the parent time
            wxLogGeneric(wxLOG_Debug,
                         "RouteSimplifier: Using parent time %s for position",
                         parentTime.Format("%D days %H:%M:%S"));
            return parentTime;
          }
          skippos = skippos->next;
        } while (skippos != route->skippoints);
      }
    }

    current = current->parent;
  }

  // If we get here, no position in the parent chain was found in isochrones
  // This shouldn't happen in a properly constructed route, but let's handle it
  // gracefully
  wxLogGeneric(
      wxLOG_Debug,
      "RouteSimplifier: No position in parent chain found in isochrones");

  // As a last resort, estimate based on position in route and total route time
  if (!isochrones.empty()) {
    wxTimeSpan totalRouteTime = isochrones.back()->time - startTime;

    // Find position's location in the original route to estimate its relative
    // time
    auto it =
        std::find(m_originalRoute.begin(), m_originalRoute.end(), position);
    if (it != m_originalRoute.end()) {
      size_t positionIndex = std::distance(m_originalRoute.begin(), it);
      double relativePosition =
          static_cast<double>(positionIndex) / (m_originalRoute.size() - 1);
      wxTimeSpan estimatedTime = totalRouteTime * relativePosition;

      wxLogGeneric(
          wxLOG_Debug,
          "RouteSimplifier: Estimated time based on route position: %zu/%zu = "
          "%.2f%% of total time %s = %s",
          positionIndex, m_originalRoute.size() - 1, relativePosition * 100.0,
          totalRouteTime.Format("%D days %H:%M:%S"),
          estimatedTime.Format("%D days %H:%M:%S"));
      return estimatedTime;
    }
  }

  return wxTimeSpan(-1);
}

std::vector<RouteSimplifier::RouteStats>
RouteSimplifier::FindAlternateRoutesWithFewerManeuvers(
    const SimplificationParams& params, int maxRoutes) {
  std::vector<RouteStats> alternateRoutes;

  // Create stats from the original route.
  RouteStats originalRoute;
  originalRoute.waypoints = m_originalRoute;
  originalRoute.totalDuration = CalculateRouteDuration(m_originalRoute);

  // Get maneuver counts directly from the last position.
  if (!m_originalRoute.empty()) {
    Position* lastPos = m_originalRoute.back();
    originalRoute.totalTacks = lastPos->tacks;
    originalRoute.totalJibes = lastPos->jibes;
    originalRoute.totalSailChanges = lastPos->sail_plan_changes;
  }
  originalRoute.totalManeuvers = originalRoute.totalTacks +
                                 originalRoute.totalJibes +
                                 originalRoute.totalSailChanges;

  alternateRoutes.push_back(originalRoute);

  // If there are no maneuvers, we can't reduce them further.
  if (originalRoute.totalManeuvers == 0) {
    wxLogGeneric(
        wxLOG_Debug,
        "RouteSimplifier: Original route has no maneuvers, skipping alternate "
        "route search");
    return alternateRoutes;
  }

  wxLogGeneric(
      wxLOG_Debug,
      "RouteSimplifier: Original route has %d tacks, %d jibes, %d sail changes "
      "(total: %d maneuvers). Duration: %s",
      originalRoute.totalTacks, originalRoute.totalJibes,
      originalRoute.totalSailChanges, originalRoute.totalManeuvers,
      originalRoute.totalDuration.Format("%D days %H:%M:%S"));

  // Access the routemap and ensure we can access the isochrones.
  if (!m_routemap) {
    wxLogGeneric(
        wxLOG_Debug,
        "RouteSimplifier: No routemap available, skipping alternate routes");
    return alternateRoutes;
  }

  const IsoChronList& isochrones = m_routemap->GetIsoChronList();
  if (isochrones.empty()) {
    wxLogGeneric(
        wxLOG_Debug,
        "RouteSimplifier: Empty isochrone list, skipping alternate routes");
    return alternateRoutes;
  }

  // Define a series of increasing maneuver durations to try.
  // These are the penalties we will apply to tacking, jibing, and sail
  // changes.
  std::vector<double> maneuverDurations = {10.0, 30.0, 60.0, 120.0, 240.0};

  // For each penalty factor, revisit isochrones to find an alternate route.
  for (double maneuverDuration : maneuverDurations) {
    if (alternateRoutes.size() >= static_cast<std::size_t>(maxRoutes)) break;

    // Create a modified configuration with increased maneuver penalties
    RouteMapConfiguration routeConfig = m_configuration;
    // Apply penalties to discourage maneuvers
    routeConfig.TackingTime = maneuverDuration;
    routeConfig.JibingTime = maneuverDuration;
    routeConfig.SailPlanChangeTime = maneuverDuration;

    wxLogGeneric(
        wxLOG_Debug,
        "RouteSimplifier: Attempting alternate route with maneuver duration "
        "%.1f seconds",
        maneuverDuration);

    // Look for alternate routes in the existing isochrones to the last
    // destination (either the exact destination or the closest to the
    // destination). We don't want to recompute isochrones (expensive), we
    // analyze the ones we already have but pretend the maneuver penalties were
    // higher.
    if (!isochrones.empty()) {
      Position* dest = m_routemap->GetLastDestination();
      if (!dest) {
        continue;
      }
      // Find alternate paths through existing isochrones.
      RouteStats alternate;
      bool found = FindAlternatePathThroughIsochrones(
          params, isochrones, routeConfig, dest, alternate);

      if (!found || alternate.totalManeuvers >= originalRoute.totalManeuvers) {
        wxLogGeneric(
            wxLOG_Debug,
            "RouteSimplifier: maneuver time %.1f. Found: %d, Maneuvers: %d, "
            "Original: %d",
            maneuverDuration, found, alternate.totalManeuvers,
            originalRoute.totalManeuvers);
        continue;
      }
      wxLogGeneric(wxLOG_Debug,
                   "RouteSimplifier: Found alternate route: "
                   "%d tacks, %d jibes, %d sail changes, delta: %s",
                   alternate.totalManeuvers, alternate.totalTacks,
                   alternate.totalJibes, alternate.totalSailChanges,
                   (alternate.totalDuration - originalRoute.totalDuration)
                       .Format("%D days %H:%M:%S"));

      // Check if it's sufficiently different from existing routes.
      bool isDuplicate = false;
      for (const RouteStats& existing : alternateRoutes) {
        // Consider it a duplicate if it has the same number of maneuvers
        // and similar duration.
        if (alternate.totalManeuvers == existing.totalManeuvers &&
            (alternate.totalDuration - existing.totalDuration)
                    .Abs()
                    .GetSeconds()
                    .ToDouble() <
                0.01 * existing.totalDuration.GetSeconds().ToDouble()) {
          isDuplicate = true;
          break;
        }
      }

      if (!isDuplicate) {
        alternateRoutes.push_back(alternate);
      }
    }
  }

  // Sort the alternate routes by increasing time
  std::sort(alternateRoutes.begin(), alternateRoutes.end(),
            [](const RouteStats& a, const RouteStats& b) -> bool {
              if (a.totalManeuvers != b.totalManeuvers)
                return a.totalManeuvers < b.totalManeuvers;
              return a.totalDuration < b.totalDuration;
            });

  return alternateRoutes;
}

bool RouteSimplifier::FindAlternatePathThroughIsochrones(
    const SimplificationParams& params, const IsoChronList& isochrones,
    const RouteMapConfiguration& config, Position* destination,
    RouteStats& result) {
  if (isochrones.empty() || !destination) {
    return false;
  }

  // Get the original route's destination coordinates.
  Position* originalDestination = nullptr;
  if (!m_originalRoute.empty()) {
    originalDestination = m_originalRoute.back();
  }

  if (!originalDestination) {
    return false;
  }

  // Get the original route duration for comparison.
  wxTimeSpan originalRouteDuration = CalculateRouteDuration(m_originalRoute);
  if (originalRouteDuration <= 0) {
    return false;
  }

  // Start with the final isochrone and examine all possible endpoints.
  const IsoChron* lastIsochron = isochrones.back();
  if (!lastIsochron) return false;

  // Track the best candidate found.
  Position* bestCandidate = nullptr;
  int fewestManeuvers = INT_MAX;
  wxTimeSpan bestDuration = wxTimeSpan(-1);

  // Analyze all possible positions in the last isochrone.
  for (IsoRoute* route : lastIsochron->routes) {
    if (!route || !route->skippoints) continue;

    SkipPosition* skippos = route->skippoints;
    do {
      Position* pos = skippos->point;
      if (!pos) {
        skippos = skippos->next;
        continue;
      }

      // If this position can reach the destination, consider it.
      double distance =
          DistGreatCircle_Plugin(pos->lat, pos->lon, originalDestination->lat,
                                 originalDestination->lon);

      // Skip positions too far from destination.
      if (distance > 5.0) {  // 5 nautical miles threshold
        skippos = skippos->next;
        continue;
      }

      // Build the candidate route from this position.
      std::list<Position*> candidateRoute = pos->BuildRoute();

      // Add final connection to exact destination if needed.
      if (distance > 0.1) {  // If not practically at the destination already.
        // Try to validate the final segment.
        RouteMapConfiguration tempConfig = config;
        double heading;
        DataMask data_mask = DataMask::NONE;
        double time = pos->PropagateToPoint(
            originalDestination->lat, originalDestination->lon, tempConfig,
            heading, data_mask, true);

        // Skip if we can't reach the destination
        if (std::isnan(time) || std::isinf(time)) {
          skippos = skippos->next;
          continue;
        }

        // Create a position at the destination with copied maneuver counts.
        Position* finalPos =
            new Position(originalDestination->lat, originalDestination->lon);
        finalPos->parent = pos;
        finalPos->tacks = pos->tacks;
        finalPos->jibes = pos->jibes;
        finalPos->sail_plan_changes = pos->sail_plan_changes;
        m_newPositions.push_back(finalPos);
        candidateRoute.push_back(finalPos);
      }

      // Count maneuvers and calculate duration.
      int tacks = 0, jibes = 0, sailChanges = 0;
      const Position* prevPos = nullptr;
      for (const Position* p : candidateRoute) {
        if (prevPos) {
          if (p->tacks > prevPos->tacks) tacks++;
          if (p->jibes > prevPos->jibes) jibes++;
          if (p->sail_plan_changes > prevPos->sail_plan_changes) sailChanges++;
        }
        prevPos = p;
      }
      int totalManeuvers = tacks + jibes + sailChanges;

      // Calculate route duration.
      wxTimeSpan routeDuration = CalculateRouteDuration(candidateRoute);
      if (routeDuration <= 0) {
        continue;
      }
      double durationPenaltyPercent =
          (routeDuration.GetSeconds().ToDouble() -
           originalRouteDuration.GetSeconds().ToDouble()) /
          originalRouteDuration.GetSeconds().ToDouble();

      // Only consider if time penalty is acceptable.
      if (durationPenaltyPercent <= params.maxDurationPenaltyPercent) {
        // Update best candidate if this has fewer maneuvers or same but less
        // time.
        if (totalManeuvers < fewestManeuvers ||
            (totalManeuvers == fewestManeuvers &&
             routeDuration < bestDuration)) {
          bestCandidate = pos;
          fewestManeuvers = totalManeuvers;
          bestDuration = routeDuration;

          // Store these values for the result.
          result.waypoints = candidateRoute;
          result.totalDuration = routeDuration;
          result.totalTacks = tacks;
          result.totalJibes = jibes;
          result.totalSailChanges = sailChanges;
          result.totalManeuvers = totalManeuvers;
        }
      }

      skippos = skippos->next;
    } while (skippos != route->skippoints);
  }

  return bestCandidate != nullptr;
}