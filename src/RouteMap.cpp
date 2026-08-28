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

/* generate a datastructure which contains positions for
   isochron line segments which describe the position of the boat at a given
   time..

   Starting at a given location, propagate outwards in all directions.
   the outward propagation is guarenteed a closed region, and circular linked
   lists are used. If the route comes upon a boundary or reason to stop
   searching, then the point is flagged so that it is not propagated any
   further.

   To merge regions requires virtually the same algorithm for descrambling
   (normalizing) a single region.

   To normalize a region means that no two line segments intersect.

   For each segment go through and see if it intersects
   with any other line segment.  When it does the old route will follow
   the correct direction of the intersection on the intersected route,
   and the new region generated will be recursively normalized and then
   merged.

   A positive intersection comes in from the right.  Negative intersections
   signal negative regions.

   For each segment in a given route
   If the intersection occurs with the route and itself
   a new region is created with the same sign as the intersection
   and added to the list of either positive or negative subregions
   otherwise if the intersection occurs on different routes
   the intersecting route is merged into this one,
   swapping their connections

   Once we reach the end of the route, we can declare that it is complete,
   so in turn recursively normalize each inner subroute.  The subregions
   with the same sign are inner routes.  Once these regions are all normalized,
   the remaining regions with a different sign are the perminent subregions.
   Any inner routes remaining with matching sign can be discarded.

   Any outer subregions are also normalized to give outer regions
   with both signs which can be appended to the incomming lists

   Any remaining routes should be tested to ensure they are outside this one,
   Any inside routes may be discarded leaving only inverted subroutes
*/

#include <wx/wx.h>

#include <stdlib.h>
#include <math.h>
#include <cmath>
#include <functional>
#include <list>
#include <map>

#include "Utilities.h"
#include "Boat.h"
#include "ConstraintChecker.h"
#include "RoutePoint.h"
#include "IsoRoute.h"
#include "RouteMap.h"
#include "SunCalculator.h"
#include "WeatherDataProvider.h"
#include "weather_routing_pi.h"

#include "georef.h"

long RouteMapPosition::s_ID = 0;

weather_routing_pi* RouteMapConfiguration::s_plugin_instance = nullptr;

namespace {

bool ResolveWaypoint(wxString& name, wxString& guid, double& lat, double& lon) {
  // OpenCPN can instantiate plugins before its waypoint manager exists.  The
  // public enumeration API safely returns an empty list in that interval,
  // whereas GetSingleWaypoint historically dereferences the manager.  Always
  // enumerate first and call the single-waypoint API only for a GUID which the
  // host has just advertised.
  const wxArrayString waypoint_guids = GetWaypointGUIDArray();
  PlugIn_Waypoint waypoint;
  if (!guid.IsEmpty() && waypoint_guids.Index(guid) != wxNOT_FOUND &&
      GetSingleWaypoint(guid, &waypoint)) {
    name = waypoint.m_MarkName;
    lat = waypoint.m_lat;
    lon = waypoint.m_lon;
    return true;
  }

  for (const auto& waypoint_guid : waypoint_guids) {
    if (!GetSingleWaypoint(waypoint_guid, &waypoint)) continue;
    if (waypoint.m_MarkName != name) continue;

    guid = waypoint_guid;
    lat = waypoint.m_lat;
    lon = waypoint.m_lon;
    return true;
  }

  return false;
}

bool ResolvePosition(const wxString& name, double& lat, double& lon) {
  for (const auto& position : RouteMap::Positions) {
    if (name != position.Name) continue;

    lat = position.lat;
    lon = position.lon;
    if (!position.GUID.IsEmpty()) {
      PlugIn_Waypoint waypoint;
      if (GetSingleWaypoint(position.GUID, &waypoint)) {
        lat = waypoint.m_lat;
        lon = waypoint.m_lon;
      }
    }
    return true;
  }

  return false;
}

}  // namespace

RouteMapConfiguration::RouteMapConfiguration()
    : StartType(START_FROM_POSITION),
      EndType(END_AT_POSITION),
      TimeMode(ROUTE_BY_DEPARTURE_TIME),
      ArrivalSearchHorizonMinutes(30 * 24 * 60),
      ArrivalSafetyMarginMinutes(30),
      ArrivalPlanningEvaluatedRoutes(0),
      ArrivalPlanningFeasibleRoutes(0),
      ArrivalPlanningScheduleMarginSeconds(0),
      DepartureTimeOptimizationEnabled(false),
      DepartureTimeOptimizationRangeMinutes(360),
      DepartureTimeOptimizationStepMinutes(60),
      DepartureTimeOptimizationConcurrentRoutes(0),
      RoutingEffortPercent(100),
      DepartureTimeOptimizationCandidate(false),
      DepartureTimeOptimizationOffsetMinutes(0),
      IsMultiLegGenerated(false),
      MultiLegLegIndex(0),
      MultiLegLegCount(0),
      MinimumDepthMeters(0.0),
      UpwindEfficiency(1.),
      DownwindEfficiency(1.),
      NightCumulativeEfficiency(1.),
      UseChartSafetyForPropagation(false),
      ChartSafetyPropagationFallbackTried(false),
      chart_safety_runtime_available(false),
      chart_safety_runtime_enforced(false),
      UseReverseReachabilityRecovery(false),
      ReverseReachabilitySearchBackIsochrones(6),
      ReverseReachabilityHorizonHours(0.0),
      ReverseReachabilityDiagnostics(false),
      ReverseRecoveryUsed(false),
      ReverseLayersBuilt(0),
      ReverseNodesGenerated(0),
      ReverseNodesFeasible(0),
      ReverseConnectionFound(false),
      ReverseFinalValidationPass(false),
      UseOptimalAngles(false),
      UseMotor(false),
      MotorSpeedThreshold(2.0),
      MotorSpeed(5.0),
      StartLon(0),
      EndLon(0),
      grib(nullptr),
      grib_is_data_deficient(false),
      accepted_candidate_count(0),
      generated_candidate_count(0),
      frontier_positions_before_merge(0),
      frontier_positions_after_merge(0),
      frontier_positions_after_reduce(0),
      frontier_routes_before_merge(0),
      frontier_routes_after_merge(0),
      frontier_routes_after_reduce(0),
      sparse_legal_frontiers_retained(0),
      sparse_legal_frontiers_dropped(0),
      weather_data_read_attempts(0),
      weather_data_read_successes(0),
      grib_wind_data_reads(0),
      climatology_wind_data_reads(0),
      deficient_wind_data_reads(0),
      current_data_read_attempts(0),
      current_data_reads(0),
      missing_current_data_reads(0),
      nonfinite_boat_speed_rejections(0),
      zero_boat_speed_rejections(0),
      max_current_speed_seen(0),
      sum_current_speed_seen(0),
      current_speed_samples(0),
      chart_land_refinement_angles(0),
      chart_land_refinement_accepted(0),
      chart_safety_missing_tile_rejections(0),
      chart_safety_missing_tile_retry_count(0),
      chart_safety_missing_tile_first_lat_tile(0),
      chart_safety_missing_tile_first_lon_tile(0),
      chart_safety_missing_tile_first_min_lat(NAN),
      chart_safety_missing_tile_first_min_lon(NAN),
      chart_safety_missing_tile_min_lat(NAN),
      chart_safety_missing_tile_max_lat(NAN),
      chart_safety_missing_tile_min_lon(NAN),
      chart_safety_missing_tile_max_lon(NAN),
      chart_safety_start_endpoint_reach_nm(0.0),
      chart_safety_end_endpoint_reach_nm(0.0),
      chart_safety_scout_preview(false),
      routing_generated_states(0),
      routing_generated_state_limit(0),
      routing_retained_states(0),
      routing_retained_state_limit(0),
      routing_graph_labels(0),
      routing_graph_label_limit(0) {}

double RouteMapConfiguration::GetBoatLat() {
  if (s_plugin_instance) return s_plugin_instance->m_boat_lat;
  return NAN;
}

double RouteMapConfiguration::GetBoatLon() {
  if (s_plugin_instance) return s_plugin_instance->m_boat_lon;
  return NAN;
}

bool RouteMapConfiguration::Update() {
  bool havestart = false, haveend = false;

  if (StartType == RouteMapConfiguration::START_FROM_BOAT) {
    StartLat = GetBoatLat();
    StartLon = GetBoatLon();
    if (!std::isnan(StartLat) && !std::isnan(StartLon)) {
      havestart = true;
    }
  }

  if (!RouteGUID.IsEmpty()) {
    if (StartType != RouteMapConfiguration::START_FROM_BOAT &&
        ResolveWaypoint(Start, StartGUID, StartLat, StartLon)) {
      havestart = true;
    }
    if (ResolveWaypoint(End, EndGUID, EndLat, EndLon)) {
      haveend = true;
    }
  }

  if (!havestart && StartType == RouteMapConfiguration::START_FROM_WAYPOINT) {
    havestart = ResolveWaypoint(Start, StartGUID, StartLat, StartLon);
  }
  if (!havestart && StartType == RouteMapConfiguration::START_FROM_POSITION) {
    havestart = ResolvePosition(Start, StartLat, StartLon);
  }
  if (EndType == RouteMapConfiguration::END_AT_WAYPOINT)
    haveend = ResolveWaypoint(End, EndGUID, EndLat, EndLon);
  else
    haveend = ResolvePosition(End, EndLat, EndLon);

  if (!havestart || !haveend) {
    StartLat = StartLon = EndLat = EndLon = NAN;
    return false;
  }

  if ((positive_longitudes = fabs(average_longitude(StartLon, EndLon)) > 90)) {
    StartLon = positive_degrees(StartLon);
    EndLon = positive_degrees(EndLon);
  }

  // Calculate the bearing between the start and end points.
  ll_gc_ll_reverse(StartLat, StartLon, EndLat, EndLon, &StartEndBearing,
                   nullptr);

  DegreeSteps.clear();
  if (RouteGUID.IsEmpty()) {
    // ensure validity
    FromDegree = wxMax(wxMin(FromDegree, 180), 0);
    ToDegree = wxMax(wxMin(ToDegree, 180), 0);
    if (FromDegree > ToDegree) FromDegree = ToDegree;
    ByDegrees = wxMax(wxMin(ByDegrees, 60), .1);

    for (double step = FromDegree; step <= ToDegree; step += ByDegrees) {
      DegreeSteps.push_back(step);
      if (step > 0 && step < 180) DegreeSteps.push_back(360 - step);
    }
  } else {
    DegreeSteps.push_back(0.);
  }
  DegreeSteps.sort();

  return true;
}

bool (*RouteMap::ClimatologyData)(int setting, const wxDateTime&, double,
                                  double, double&, double&) = nullptr;
bool (*RouteMap::ClimatologyWindAtlasData)(const wxDateTime&, double, double,
                                           int& count, double*, double*,
                                           double&, double&) = nullptr;
int (*RouteMap::ClimatologyCycloneTrackCrossings)(double, double, double,
                                                  double, const wxDateTime&,
                                                  int) = nullptr;

OD_FindClosestBoundaryLineCrossing RouteMap::ODFindClosestBoundaryLineCrossing =
    nullptr;

std::list<RouteMapPosition> RouteMap::Positions;

RouteMap::RouteMap()
    : m_bNeedsGrib(false),
      m_bNeedsChartSafetyData(false),
      m_NewGrib(NULL),
      m_CancellationFlag(std::make_shared<std::atomic_bool>(false)) {}

RouteMap::~RouteMap() { Clear(); }

namespace {
std::int64_t GribTimelineKey(const wxDateTime& time) {
  return time.IsValid() ? static_cast<std::int64_t>(time.GetTicks()) : -1;
}
}  // namespace

bool RouteMap::AcquireGribTimelineFrame(const wxDateTime& time,
                                        Shared_GribRecordSet& frame,
                                        long timeoutMilliseconds) {
  const std::int64_t key = GribTimelineKey(time);
  if (key < 0) return false;
  return m_GribTimelineCache.Acquire(
      key, &frame, timeoutMilliseconds,
      [this, time](const std::int64_t&) {
        Lock();
        m_NewTime = time;
        m_bNeedsGrib = true;
        Unlock();
      },
      [this] { return m_CancellationFlag->load(std::memory_order_relaxed); });
}

void RouteMap::PublishTimelineFrame(std::int64_t timeline_key,
                                    const Shared_GribRecordSet& frame) {
  if (timeline_key < 0) return;
  m_GribTimelineCache.Publish(timeline_key, frame,
                              frame.GetGribRecordSet() != nullptr);
}

static long CountIsoRouteListPositions(const IsoRouteList& routes) {
  long count = 0;
  for (auto route : routes) {
    if (route) count += route->Count();
  }
  return count;
}

static void DeleteIsoRouteList(IsoRouteList& routes) {
  for (IsoRouteList::iterator it = routes.begin(); it != routes.end(); ++it)
    delete *it;
  routes.clear();
}

struct PositionPropagationState {
  Position* position;
  bool propagated;
  PropagationError propagation_error;
};

static void SnapshotIsoRoutePropagationState(
    IsoRoute* route, std::vector<PositionPropagationState>* states) {
  if (!route || !route->skippoints || !route->skippoints->point || !states)
    return;

  Position* position = route->skippoints->point;
  do {
    PositionPropagationState state = {position, position->propagated,
                                      position->propagation_error};
    states->push_back(state);
    position = position->next;
  } while (position && position != route->skippoints->point);

  for (IsoRouteList::iterator child = route->children.begin();
       child != route->children.end(); ++child)
    SnapshotIsoRoutePropagationState(*child, states);
}

static void SnapshotIsoChronPropagationState(
    IsoChron* isochron, std::vector<PositionPropagationState>* states) {
  if (!isochron || !states) return;
  states->clear();
  for (IsoRouteList::iterator route = isochron->routes.begin();
       route != isochron->routes.end(); ++route)
    SnapshotIsoRoutePropagationState(*route, states);
}

static void RestoreIsoChronPropagationState(
    const std::vector<PositionPropagationState>& states) {
  for (std::vector<PositionPropagationState>::const_iterator state =
           states.begin();
       state != states.end(); ++state) {
    state->position->propagated = state->propagated;
    state->position->propagation_error = state->propagation_error;
  }
}

void RouteMap::PositionLatLon(wxString Name, double& lat, double& lon) {
  for (std::list<RouteMapPosition>::iterator it = Positions.begin();
       it != Positions.end(); it++)
    if ((*it).Name == Name) {
      lat = (*it).lat;
      lon = (*it).lon;
    }
}

bool RouteMap::ReduceList(IsoRouteList& merged, IsoRouteList& routelist,
                          RouteMapConfiguration& configuration) {
  IsoRouteList unmerged;
  while (!routelist.empty()) {
    IsoRoute* r1 = routelist.front();
    routelist.pop_front();
    while (!routelist.empty()) {
      if (TestAbort()) return false;

      IsoRoute* r2 = routelist.front();
      routelist.pop_front();
      IsoRouteList rl;

      if (Merge(rl, r1, r2, 0, configuration.InvertedRegions)) {
        routelist.splice(routelist.end(), rl);
        goto remerge;
      } else
        unmerged.push_back(r2);
    }
    /* none more in list so nothing left to merge with */
    merged.push_back(r1);

  remerge:
    /* put any unmerged back in list to continue */
    routelist.splice(routelist.end(), unmerged);
  }
  return true;
}

/* enlarge the map by 1 level */
bool RouteMap::Propagate() {
  Lock();

  if (m_bNeedsChartSafetyData) {
    Unlock();
    return false;
  }

  if (m_bNeedsGrib) {  // waiting for timer in main thread to request the grib
    Unlock();
    return false;
  }

  if (!m_bValid) { /* config change */
    m_bFinished = true;
    Unlock();
    return false;
  }

  //
  RouteMapConfiguration configuration = m_Configuration;
  configuration.polar_status = POLAR_SPEED_SUCCESS;
  configuration.wind_data_status = wxEmptyString;
  configuration.boundary_crossing = false;
  configuration.land_crossing = false;
  for (int i = 0; i <= PROPAGATION_ANGLE_ERROR; ++i)
    configuration.rejection_counts[i] = 0;
  configuration.accepted_candidate_count = 0;
  configuration.generated_candidate_count = 0;
  configuration.frontier_positions_before_merge = 0;
  configuration.frontier_positions_after_merge = 0;
  configuration.frontier_positions_after_reduce = 0;
  configuration.frontier_routes_before_merge = 0;
  configuration.frontier_routes_after_merge = 0;
  configuration.frontier_routes_after_reduce = 0;
  configuration.sparse_legal_frontiers_retained = 0;
  configuration.sparse_legal_frontiers_dropped = 0;
  configuration.weather_data_read_attempts = 0;
  configuration.weather_data_read_successes = 0;
  configuration.grib_wind_data_reads = 0;
  configuration.climatology_wind_data_reads = 0;
  configuration.deficient_wind_data_reads = 0;
  configuration.current_data_read_attempts = 0;
  configuration.current_data_reads = 0;
  configuration.missing_current_data_reads = 0;
  configuration.nonfinite_boat_speed_rejections = 0;
  configuration.zero_boat_speed_rejections = 0;
  configuration.max_current_speed_seen = 0;
  configuration.sum_current_speed_seen = 0;
  configuration.current_speed_samples = 0;
  configuration.chart_land_refinement_angles = 0;
  configuration.chart_land_refinement_accepted = 0;

  // reset grib data deficient flag
  bool grib_is_data_deficient = false;

  if (m_Configuration.AllowDataDeficient &&
      (!m_NewGrib || !m_NewGrib->m_GribRecordPtrArray[Idx_WIND_VX] ||
       !m_NewGrib->m_GribRecordPtrArray[Idx_WIND_VY]) &&
      origin.size() &&
      /*m_Configuration.ClimatologyType <= RouteMapConfiguration::CURRENTS_ONLY
         &&*/
      m_Configuration.UseGrib) {
    SetNewGrib(origin.back()->m_Grib);
    grib_is_data_deficient = true;
  }

  Shared_GribRecordSet shared_grib = m_SharedNewGrib;
  wxDateTime time = m_NewTime;
  double delta;

  m_NewGrib = 0;
  m_SharedNewGrib.SetGribRecordSet(0);

  // Request the next GRIB while propagation uses the current record.
  delta = DetermineDeltaTime();
  m_NewTime += wxTimeSpan(0, 0, delta);
  m_bNeedsGrib = configuration.UseGrib;

  Unlock();

  IsoRouteList routelist;
  std::vector<PositionPropagationState> sourcePropagationState;
  wxStopWatch propagateTimer;
  if (origin.empty()) {
    // The routing calculation has not started yet.
    Position* np = new Position(configuration.StartLat, configuration.StartLon);
    np->prev = np->next = np;
    routelist.push_back(new IsoRoute(np->BuildSkipList()));
    configuration.grib = nullptr;
  } else {
    // At least one isochrone has been calculated.
    configuration.grib = origin.back()->m_Grib;
    configuration.time = origin.back()->time;
    configuration.UsedDeltaTime = origin.back()->delta;
    configuration.grib_is_data_deficient =
        origin.back()->m_Grib_is_data_deficient;
    // will the grib data work for us?
    if (m_Configuration.UseGrib &&
        (!configuration.grib ||
         !configuration.grib->m_GribRecordPtrArray[Idx_WIND_VX] ||
         !configuration.grib->m_GribRecordPtrArray[Idx_WIND_VY]) &&
        (!RouteMap::ClimatologyData ||
         m_Configuration.ClimatologyType <=
             RouteMapConfiguration::CURRENTS_ONLY)) {
      // This route is supposed to use GRIB data without climatology, but the
      // GRIB data is not available.
      Lock();
      m_bFinished = true;
      if (!configuration.grib) {
        m_bWeatherForecastStatus = WEATHER_FORECAST_NO_GRIB_DATA;
        wxString txt = _("Isochrone exceeds GRIB data range at: ");
        m_bWeatherForecastError = wxString::Format(
            "%s %s", txt, configuration.time.Format("%Y-%m-%d %H:%M:%S"));
      } else if (!configuration.grib->m_GribRecordPtrArray[Idx_WIND_VX] ||
                 !configuration.grib->m_GribRecordPtrArray[Idx_WIND_VY]) {
        m_bWeatherForecastStatus = WEATHER_FORECAST_NO_WIND_DATA;
        wxString txt = _("Missing wind data in GRIB for time: ");
        m_bWeatherForecastError = wxString::Format(
            "%s %s", txt, configuration.time.Format("%Y-%m-%d %H:%M:%S"));
      } else if (!RouteMap::ClimatologyData) {
        m_bWeatherForecastStatus = WEATHER_FORECAST_NO_CLIMATOLOGY_DATA;
        m_bWeatherForecastError =
            _("Route requires climatology data (currently disabled)");
      } else if (m_Configuration.ClimatologyType <=
                 RouteMapConfiguration::CURRENTS_ONLY) {
        m_bWeatherForecastStatus = WEATHER_FORECAST_CLIMATOLOGY_DISABLED;
        m_bWeatherForecastError =
            _("Missing required climatology data for this configuration");
      } else {
        m_bWeatherForecastStatus = WEATHER_FORECAST_OTHER_ERROR;
        m_bWeatherForecastError = _("Unknown weather forecast error occurred");
      }
      Unlock();
      return false;
    }

    if (ConstraintChecker::IsExperimentalChartSafetyEnforced())
      SnapshotIsoChronPropagationState(origin.back(), &sourcePropagationState);
    origin.back()->PropagateIntoList(routelist, configuration);
  }
  long propagateMs = propagateTimer.Time();

  if (configuration.DetectLand &&
      ConstraintChecker::IsExperimentalChartSafetyEnforced() &&
      configuration.chart_safety_missing_tile_rejections > 0) {
    wxLogMessage(
        "WR_GRID_TILE_RETRY_NEEDED route=\"%s -> %s\" "
        "missing_rejections=%ld first_tile=(%d,%d) "
        "first_tile_min=(%.6f,%.6f) "
        "missing_bbox=[lat %.6f..%.6f lon %.6f..%.6f] "
        "propagate_ms=%ld "
        "generated=%ld accepted=%ld. Discarding this provisional isochrone "
        "and pausing for main-thread chart-safety request service.",
        m_Configuration.Start, m_Configuration.End,
        configuration.chart_safety_missing_tile_rejections,
        configuration.chart_safety_missing_tile_first_lat_tile,
        configuration.chart_safety_missing_tile_first_lon_tile,
        configuration.chart_safety_missing_tile_first_min_lat,
        configuration.chart_safety_missing_tile_first_min_lon,
        configuration.chart_safety_missing_tile_min_lat,
        configuration.chart_safety_missing_tile_max_lat,
        configuration.chart_safety_missing_tile_min_lon,
        configuration.chart_safety_missing_tile_max_lon, propagateMs,
        configuration.generated_candidate_count,
        configuration.accepted_candidate_count);
    DeleteIsoRouteList(routelist);
    // Position::Propagate marks every source frontier point as propagated.
    // Restore the exact pre-layer state so the same frontier can be replayed
    // after the main thread publishes the requested chart mask.
    RestoreIsoChronPropagationState(sourcePropagationState);
    Lock();
    // Restore the prepared record and time for the compatibility replay path.
    // Normal operation prewarms the deterministic tile halo and does not enter
    // this path; exact worker requests remain a safe last-resort backstop.
    m_NewTime = time;
    m_SharedNewGrib = shared_grib;
    m_NewGrib = m_SharedNewGrib.GetGribRecordSet();
    m_bNeedsGrib = false;
    m_bNeedsChartSafetyData = true;
    m_Configuration.chart_safety_missing_tile_rejections =
        configuration.chart_safety_missing_tile_rejections;
    m_Configuration.chart_safety_missing_tile_first_lat_tile =
        configuration.chart_safety_missing_tile_first_lat_tile;
    m_Configuration.chart_safety_missing_tile_first_lon_tile =
        configuration.chart_safety_missing_tile_first_lon_tile;
    m_Configuration.chart_safety_missing_tile_first_min_lat =
        configuration.chart_safety_missing_tile_first_min_lat;
    m_Configuration.chart_safety_missing_tile_first_min_lon =
        configuration.chart_safety_missing_tile_first_min_lon;
    m_Configuration.chart_safety_missing_tile_min_lat =
        configuration.chart_safety_missing_tile_min_lat;
    m_Configuration.chart_safety_missing_tile_max_lat =
        configuration.chart_safety_missing_tile_max_lat;
    m_Configuration.chart_safety_missing_tile_min_lon =
        configuration.chart_safety_missing_tile_min_lon;
    m_Configuration.chart_safety_missing_tile_max_lon =
        configuration.chart_safety_missing_tile_max_lon;
    Unlock();
    return false;
  }

  IsoChron* update;
  if (routelist.empty()) {
    update = nullptr;
  } else {
    wxStopWatch reduceInputTimer;
    for (IsoRouteList::iterator it = routelist.begin(); it != routelist.end();
         ++it)
      (*it)->ReduceClosePoints();
    long reduceInputMs = reduceInputTimer.Time();
    configuration.frontier_routes_before_merge = routelist.size();
    configuration.frontier_positions_before_merge =
        CountIsoRouteListPositions(routelist);
    IsoRouteList merged;
    wxStopWatch mergeTimer;
    if (!ReduceList(merged, routelist, configuration)) return false;
    long mergeMs = mergeTimer.Time();
    configuration.frontier_routes_after_merge = merged.size();
    configuration.frontier_positions_after_merge =
        CountIsoRouteListPositions(merged);

    wxStopWatch reduceOutputTimer;
    for (IsoRouteList::iterator it = merged.begin(); it != merged.end(); ++it)
      (*it)->ReduceClosePoints();
    long reduceOutputMs = reduceOutputTimer.Time();
    configuration.frontier_routes_after_reduce = merged.size();
    configuration.frontier_positions_after_reduce =
        CountIsoRouteListPositions(merged);

    long frontierThinMs = 0;
    long frontierThinRemoved = 0;
    const long max_chart_safe_frontier_positions = 280;
    if (configuration.DetectLand &&
        ConstraintChecker::IsExperimentalChartSafetyEnforced() &&
        configuration.frontier_positions_after_reduce >
            max_chart_safe_frontier_positions) {
      wxStopWatch thinTimer;
      for (IsoRouteList::iterator it = merged.begin(); it != merged.end(); ++it)
        frontierThinRemoved +=
            (*it)->ThinPositions(max_chart_safe_frontier_positions);
      frontierThinMs = thinTimer.Time();
      long positions_after_thin = CountIsoRouteListPositions(merged);
      wxLogMessage(
          "WR_ROUTE_FRONTIER_THINNING route=\"%s -> %s\" before=%ld "
          "after=%ld removed=%ld max_per_route=%ld thin_ms=%ld",
          m_Configuration.Start, m_Configuration.End,
          configuration.frontier_positions_after_reduce, positions_after_thin,
          frontierThinRemoved, max_chart_safe_frontier_positions,
          frontierThinMs);
      configuration.frontier_positions_after_reduce = positions_after_thin;
    }
    if (propagateMs + reduceInputMs + mergeMs + reduceOutputMs > 1000) {
      wxLogMessage(
          "WR_ROUTE_WORKER_TIMING route=\"%s -> %s\" propagate_ms=%ld "
          "premerge_reduce_ms=%ld merge_ms=%ld postmerge_reduce_ms=%ld "
          "frontier_thin_ms=%ld frontier_thin_removed=%ld "
          "routes_before=%ld positions_before=%ld routes_after_merge=%ld "
          "positions_after_merge=%ld routes_after_reduce=%ld "
          "positions_after_reduce=%ld",
          m_Configuration.Start, m_Configuration.End, propagateMs,
          reduceInputMs, mergeMs, reduceOutputMs, frontierThinMs,
          frontierThinRemoved, configuration.frontier_routes_before_merge,
          configuration.frontier_positions_before_merge,
          configuration.frontier_routes_after_merge,
          configuration.frontier_positions_after_merge,
          configuration.frontier_routes_after_reduce,
          configuration.frontier_positions_after_reduce);
    }

    update =
        new IsoChron(merged, time, delta, shared_grib, grib_is_data_deficient);
  }

  Lock();
  if (update) {
    origin.push_back(update);
    if (update->Contains(m_Configuration.EndLat, m_Configuration.EndLon)) {
      SetFinished(true);
    }
  } else {
    m_bFinished = true;
    long dominant_count = 0;
    PropagationError dominant_error = PROPAGATION_NO_ERROR;
    for (int i = PROPAGATION_WIND_DATA_FAILED; i <= PROPAGATION_ANGLE_ERROR;
         ++i) {
      if (configuration.rejection_counts[i] > dominant_count) {
        dominant_count = configuration.rejection_counts[i];
        dominant_error = (PropagationError)i;
      }
    }
    long land_rejections =
        configuration.rejection_counts[PROPAGATION_LAND_INTERSECTION] +
        configuration.rejection_counts[PROPAGATION_LAND_SAFETY_MARGIN];
    long weather_rejections =
        configuration.rejection_counts[PROPAGATION_WIND_DATA_FAILED] +
        configuration.rejection_counts[PROPAGATION_EXCEEDED_MAX_WIND] +
        configuration.rejection_counts[PROPAGATION_EXCEEDED_APPARENT_WIND] +
        configuration.rejection_counts[PROPAGATION_EXCEEDED_WIND_VS_CURRENT];
    long polar_rejections =
        configuration
            .rejection_counts[PROPAGATION_BOAT_SPEED_COMPUTATION_FAILED] +
        configuration.rejection_counts[PROPAGATION_POLAR_CONSTRAINTS];
    long angle_rejections =
        configuration
            .rejection_counts[PROPAGATION_ANGLE_OUTSIDE_SEARCH_LIMITS] +
        configuration.rejection_counts[PROPAGATION_ANGLE_ERROR];
    long boundary_rejections =
        configuration.rejection_counts[PROPAGATION_BOUNDARY_INTERSECTION];

    if (configuration.chart_safety_missing_tile_rejections > 0) {
      m_FailureReason = wxString::Format(
          _("No reachable route points: missing chart safety data after "
            "prewarm/retry (%ld candidate segments need chart tiles)"),
          configuration.chart_safety_missing_tile_rejections);
    } else if (configuration.weather_data_read_attempts > 0 &&
               configuration.weather_data_read_successes == 0) {
      m_FailureReason = wxString::Format(
          _("No reachable route points: no weather data at route time/window "
            "(%ld weather reads failed)"),
          configuration.weather_data_read_attempts);
    } else if (configuration.Currents &&
               configuration.current_data_read_attempts > 0 &&
               configuration.current_data_reads == 0 &&
               configuration.grib_wind_data_reads > 0) {
      m_FailureReason = wxString::Format(
          _("No reachable route points: no current data available; routing "
            "used zero-current fallback for %ld samples"),
          configuration.missing_current_data_reads);
    } else if (configuration.nonfinite_boat_speed_rejections > 0) {
      m_FailureReason = wxString::Format(
          _("No reachable route points: current/polar calculation produced "
            "invalid boat speed/SOG (%ld rejected moves)"),
          configuration.nonfinite_boat_speed_rejections);
    } else if (configuration.accepted_candidate_count > 0 &&
               configuration.frontier_positions_before_merge == 0 &&
               configuration.sparse_legal_frontiers_dropped > 0) {
      m_FailureReason = wxString::Format(
          _("No reachable route points: pruning/frontier collapse (%ld sparse "
            "legal frontiers with 1-2 moves were dropped, %ld accepted moves "
            "before pruning)"),
          configuration.sparse_legal_frontiers_dropped,
          configuration.accepted_candidate_count);
    } else if (land_rejections > 0 && land_rejections >= weather_rejections &&
               land_rejections >= polar_rejections &&
               land_rejections >= angle_rejections &&
               land_rejections >= boundary_rejections) {
      m_FailureReason = wxString::Format(
          _("No reachable route points: all branches blocked mostly by "
            "chart land/depth constraints (%ld land/depth rejections, "
            "%ld accepted moves)"),
          land_rejections, configuration.accepted_candidate_count);
    } else if (weather_rejections > 0 &&
               weather_rejections >= polar_rejections &&
               weather_rejections >= angle_rejections) {
      m_FailureReason = wxString::Format(
          _("No reachable route points: weather/current constraints prevent "
            "progress (%ld weather/current rejections)"),
          weather_rejections);
    } else if (polar_rejections > 0 && polar_rejections >= angle_rejections) {
      m_FailureReason = wxString::Format(
          _("No reachable route points: polar/sail configuration prevents "
            "progress (%ld polar rejections)"),
          polar_rejections);
    } else if (angle_rejections > 0) {
      m_FailureReason = wxString::Format(
          _("No reachable route points: search angle/course limits prevent "
            "progress (%ld angle-limit rejections)"),
          angle_rejections);
    } else if (dominant_count > 0) {
      m_FailureReason = wxString::Format(
          _("No reachable route points; most candidates rejected by: %s"),
          Position::GetErrorText(dominant_error));
    } else {
      m_FailureReason = _("No reachable route points");
    }
    wxString summaryLog = wxString::Format(
        "WeatherRouting propagation summary route=\"%s -> %s\" "
        "candidate_offset=%d leg=%d/%d generated=%ld accepted=%ld ",
        m_Configuration.Start, m_Configuration.End,
        m_Configuration.DepartureTimeOptimizationOffsetMinutes,
        m_Configuration.MultiLegLegIndex, m_Configuration.MultiLegLegCount,
        configuration.generated_candidate_count,
        configuration.accepted_candidate_count);
    summaryLog += wxString::Format(
        "frontier_before_merge{routes=%ld positions=%ld} "
        "frontier_after_merge{routes=%ld positions=%ld} "
        "frontier_after_reduce{routes=%ld positions=%ld} "
        "sparse_frontiers{retained=%ld dropped=%ld} ",
        configuration.frontier_routes_before_merge,
        configuration.frontier_positions_before_merge,
        configuration.frontier_routes_after_merge,
        configuration.frontier_positions_after_merge,
        configuration.frontier_routes_after_reduce,
        configuration.frontier_positions_after_reduce,
        configuration.sparse_legal_frontiers_retained,
        configuration.sparse_legal_frontiers_dropped);
    summaryLog += wxString::Format(
        "data{weather_attempts=%ld weather_success=%ld grib_wind=%ld "
        "climatology_wind=%ld deficient_wind=%ld current_attempts=%ld "
        "current_success=%ld current_missing=%ld current_max=%.3f "
        "current_avg=%.3f nonfinite_boat_speed=%ld zero_boat_speed=%ld} ",
        configuration.weather_data_read_attempts,
        configuration.weather_data_read_successes,
        configuration.grib_wind_data_reads,
        configuration.climatology_wind_data_reads,
        configuration.deficient_wind_data_reads,
        configuration.current_data_read_attempts,
        configuration.current_data_reads,
        configuration.missing_current_data_reads,
        configuration.max_current_speed_seen,
        configuration.current_speed_samples > 0
            ? configuration.sum_current_speed_seen /
                  configuration.current_speed_samples
            : 0.0,
        configuration.nonfinite_boat_speed_rejections,
        configuration.zero_boat_speed_rejections);
    summaryLog += wxString::Format(
        "refinement_angles=%ld refinement_accepted=%ld "
        "rejected{polar=%ld land=%ld boundary=%ld weather=%ld wind=%ld "
        "apparent_wind=%ld angle=%ld missing_safety_tiles=%ld "
        "first_missing_tile=(%d,%d) first_missing_tile_min=(%.6f,%.6f) "
        "missing_tile_bbox=[lat %.6f..%.6f lon %.6f..%.6f]} "
        "reason=\"%s\"",
        configuration.chart_land_refinement_angles,
        configuration.chart_land_refinement_accepted,
        configuration
                .rejection_counts[PROPAGATION_BOAT_SPEED_COMPUTATION_FAILED] +
            configuration.rejection_counts[PROPAGATION_POLAR_CONSTRAINTS],
        configuration.rejection_counts[PROPAGATION_LAND_INTERSECTION] +
            configuration.rejection_counts[PROPAGATION_LAND_SAFETY_MARGIN],
        configuration.rejection_counts[PROPAGATION_BOUNDARY_INTERSECTION],
        configuration.rejection_counts[PROPAGATION_WIND_DATA_FAILED],
        configuration.rejection_counts[PROPAGATION_EXCEEDED_MAX_WIND],
        configuration.rejection_counts[PROPAGATION_EXCEEDED_APPARENT_WIND],
        configuration.rejection_counts[PROPAGATION_ANGLE_ERROR],
        configuration.chart_safety_missing_tile_rejections,
        configuration.chart_safety_missing_tile_first_lat_tile,
        configuration.chart_safety_missing_tile_first_lon_tile,
        configuration.chart_safety_missing_tile_first_min_lat,
        configuration.chart_safety_missing_tile_first_min_lon,
        configuration.chart_safety_missing_tile_min_lat,
        configuration.chart_safety_missing_tile_max_lat,
        configuration.chart_safety_missing_tile_min_lon,
        configuration.chart_safety_missing_tile_max_lon, m_FailureReason);
    wxLogMessage("%s", summaryLog);
  }

  // take note of possible failure reasons
  UpdateStatus(configuration);
  m_Configuration.chart_safety_missing_tile_rejections =
      configuration.chart_safety_missing_tile_rejections;
  m_Configuration.chart_safety_missing_tile_first_lat_tile =
      configuration.chart_safety_missing_tile_first_lat_tile;
  m_Configuration.chart_safety_missing_tile_first_lon_tile =
      configuration.chart_safety_missing_tile_first_lon_tile;
  m_Configuration.chart_safety_missing_tile_first_min_lat =
      configuration.chart_safety_missing_tile_first_min_lat;
  m_Configuration.chart_safety_missing_tile_first_min_lon =
      configuration.chart_safety_missing_tile_first_min_lon;
  m_Configuration.chart_safety_missing_tile_min_lat =
      configuration.chart_safety_missing_tile_min_lat;
  m_Configuration.chart_safety_missing_tile_max_lat =
      configuration.chart_safety_missing_tile_max_lat;
  m_Configuration.chart_safety_missing_tile_min_lon =
      configuration.chart_safety_missing_tile_min_lon;
  m_Configuration.chart_safety_missing_tile_max_lon =
      configuration.chart_safety_missing_tile_max_lon;

  long land_rejections =
      configuration.rejection_counts[PROPAGATION_LAND_INTERSECTION] +
      configuration.rejection_counts[PROPAGATION_LAND_SAFETY_MARGIN];
  if (configuration.chart_land_refinement_angles > 0 &&
      (land_rejections > 0 ||
       configuration.chart_land_refinement_accepted > 0)) {
    wxLogMessage(
        "WeatherRouting detour refinement route=\"%s -> %s\" "
        "generated=%ld accepted=%ld land_rejections=%ld "
        "refinement_angles=%ld refinement_accepted=%ld "
        "sparse_frontiers_retained=%ld sparse_frontiers_dropped=%ld",
        m_Configuration.Start, m_Configuration.End,
        configuration.generated_candidate_count,
        configuration.accepted_candidate_count, land_rejections,
        configuration.chart_land_refinement_angles,
        configuration.chart_land_refinement_accepted,
        configuration.sparse_legal_frontiers_retained,
        configuration.sparse_legal_frontiers_dropped);
  }

  Unlock();

  return true;
}

double RouteMap::DetermineDeltaTime() {
  double deltaTime = m_Configuration.DeltaTime;

  // Find the closest position to source and destination in the last isochron.
  double minDistToEnd = INFINITY;
  double maxDistFromStart = -INFINITY;

  const double proximityThreshold = 40.0;  // nautical miles
  const double minReductionFactor =
      0.1;  // Minimum reduction factor (10% of normal time step)
  // Will be adjusted based on distances
  double startReductionFactor = 1.0;
  double endReductionFactor = 1.0;

  // Reduced time step when leaving source or approaching destination.
  if (!origin.empty()) {
    // Get the last isochron
    IsoChron* lastIsochron = origin.back();

    // Count positions and failed propagations for adaptive time step.
    int totalPositions = 0;
    int failedPropagations = 0;

    for (IsoRouteList::iterator it = lastIsochron->routes.begin();
         it != lastIsochron->routes.end(); ++it) {
      Position* pos = (*it)->skippoints->point;
      do {
        totalPositions++;

        // If this position failed to propagate (has no child positions in the
        // next isochron) We'd need a way to track this information
        if (pos->propagation_error != PROPAGATION_NO_ERROR &&
            pos->propagation_error != PROPAGATION_ALREADY_PROPAGATED) {
          failedPropagations++;
        }

        double distFromSource =
            DistGreatCircle(pos->lat, pos->lon, m_Configuration.StartLat,
                            m_Configuration.StartLon);
        double distToDest = DistGreatCircle(
            pos->lat, pos->lon, m_Configuration.EndLat, m_Configuration.EndLon);
        minDistToEnd = std::min(minDistToEnd, distToDest);
        maxDistFromStart = std::max(maxDistFromStart, distFromSource);
        pos = pos->next;
      } while (pos != (*it)->skippoints->point);
    }

    // Calculate gradual reduction factors

    // For starting point: gradually increase from minReductionFactor to 1.0
    if (maxDistFromStart < proximityThreshold) {
      // As we move away from the start, the time step increases.
      startReductionFactor =
          minReductionFactor + (0.9 * maxDistFromStart / proximityThreshold);
    }

    // For destination: gradually decrease from 1.0 to minReductionFactor
    if (minDistToEnd < proximityThreshold) {
      // As we get closer to the destination, the time step decreases.
      endReductionFactor =
          minReductionFactor + (0.9 * minDistToEnd / proximityThreshold);
    }

    // Apply the minimum of both reduction factors.
    // This ensures proper handling when we're both near start and destination.
    deltaTime *= std::min(startReductionFactor, endReductionFactor);
  } else {
    // For the first step, use the minimum reduction factor.
    deltaTime = m_Configuration.DeltaTime * minReductionFactor;
  }

  // Ensure delta time doesn't go below a reasonable minimum.
  const double minDeltaTime = 60.0;  // in seconds
  return std::max(deltaTime, minDeltaTime);
}

Position* RouteMap::ClosestPosition(double lat, double lon, wxDateTime* t,
                                    double* d) {
  if (origin.empty()) return nullptr;

  Position* minpos = nullptr;
  double mindist = INFINITY;
  bool inside;
  bool first = (t != 0);
  wxDateTime min_t;
  Lock();

  IsoChronList::iterator it = origin.end();

  Position p(lat,
             m_Configuration.positive_longitudes ? positive_degrees(lon) : lon);
  do {
    it--;
    double dist;
    wxDateTime cur_t;
    Position* pos = (*it)->ClosestPosition(p.lat, p.lon, &cur_t, &dist);

    if (dist > mindist) break;

    if (pos && dist <= mindist) {
      minpos = pos;
      mindist = dist;
      if (!min_t.IsValid() || (cur_t.IsValid() && cur_t < min_t)) min_t = cur_t;
    }
    /* bail if we don't contain because obviously we aren't getting any closer
     */

    inside = (*it)->Contains(p);
    if (!inside && !first) break;
    if (inside) first = false;
  } while (it != origin.begin());

  Unlock();

  if (d) *d = mindist;
  if (t) *t = min_t;
  return minpos;
}

void RouteMap::Reset() {
  m_CancellationFlag->store(false, std::memory_order_relaxed);
  Lock();
  Clear();

  m_NewGrib = nullptr;
  m_SharedNewGrib.SetGribRecordSet(0);

  m_NewTime = m_Configuration.StartTime;
  m_bNeedsGrib = m_Configuration.UseGrib && m_Configuration.RouteGUID.IsEmpty();
  m_bNeedsChartSafetyData = false;
  m_ErrorMsg = wxEmptyString;
  m_FailureReason = wxEmptyString;

  m_bReachedDestination = false;
  m_bWeatherForecastStatus = WEATHER_FORECAST_SUCCESS;
  m_bPolarStatus = POLAR_SPEED_SUCCESS;
  m_bGribError = wxEmptyString;
  m_bFinished = false;
  m_bLandCrossing = false;
  m_bBoundaryCrossing = false;

  Unlock();
}

void RouteMap::ChartSafetyDataServiced() {
  Lock();
  m_bNeedsChartSafetyData = false;
  m_Configuration.chart_safety_missing_tile_rejections = 0;
  m_Configuration.chart_safety_missing_tile_first_lat_tile = 0;
  m_Configuration.chart_safety_missing_tile_first_lon_tile = 0;
  m_Configuration.chart_safety_missing_tile_first_min_lat = NAN;
  m_Configuration.chart_safety_missing_tile_first_min_lon = NAN;
  m_Configuration.chart_safety_missing_tile_min_lat = NAN;
  m_Configuration.chart_safety_missing_tile_max_lat = NAN;
  m_Configuration.chart_safety_missing_tile_min_lon = NAN;
  m_Configuration.chart_safety_missing_tile_max_lon = NAN;
  Unlock();
}

bool RouteMap::AwaitChartSafetyData(long timeoutMilliseconds) {
  Lock();
  m_bNeedsChartSafetyData = true;
  Unlock();
  const wxLongLong started = wxGetUTCTimeMillis();
  while (NeedsChartSafetyData()) {
    if (Finished()) return false;
    if ((wxGetUTCTimeMillis() - started).ToLong() >= timeoutMilliseconds)
      return false;
    wxMilliSleep(10);
  }
  return true;
}

std::map<time_t, std::weak_ptr<WR_GribRecordSet>> grib_key;
wxMutex s_key_mutex;

void RouteMap::SetNewGrib(GribRecordSet* grib, std::int64_t timeline_key) {
  if (!grib || !grib->m_GribRecordPtrArray[Idx_WIND_VX] ||
      !grib->m_GribRecordPtrArray[Idx_WIND_VY]) {
    PublishTimelineFrame(timeline_key, Shared_GribRecordSet());
    return;
  }

  // XXX should be grib->m_ID in a newer OpenCPN version
  unsigned int bogus_ID;  // grib->m_ID

  GribRecord* tmp = grib->m_GribRecordPtrArray[Idx_WIND_VX];
  // RecordRefDate is time_t and high byte is likely the same in many grib
  // files, add some entropy
  bogus_ID = tmp->getRecordRefDate() ^ (tmp->getIdCenter() << 24) ^
             (tmp->getNi() << 16);

  {
    std::map<time_t, std::weak_ptr<WR_GribRecordSet>>::iterator it;
    wxMutexLocker lock(s_key_mutex);
    it = grib_key.find(grib->m_Reference_Time);
    const std::shared_ptr<WR_GribRecordSet> existing =
        it == grib_key.end() ? nullptr : it->second.lock();
    if (existing) {
      m_SharedNewGrib.SetSharedGribRecordSet(existing);
      m_NewGrib = m_SharedNewGrib.GetGribRecordSet();
      // compute fake generation grib->m_ID
      if (m_NewGrib->m_ID == bogus_ID) {
        PublishTimelineFrame(timeline_key, m_SharedNewGrib);
        return;
      }
    }
  }
  /* copy the grib record set */
  m_NewGrib = new WR_GribRecordSet(bogus_ID /* XXX */);
  m_NewGrib->m_Reference_Time = grib->m_Reference_Time;
  for (int i = 0; i < Idx_COUNT; i++) {
    switch (i) {
      case Idx_HTSIGW:
      case Idx_WVDIR:
      case Idx_WVPER:
      case Idx_WIND_GUST:
      case Idx_WIND_VX:
      case Idx_WIND_VY:
      case Idx_SEACURRENT_VX:
      case Idx_SEACURRENT_VY:
      case Idx_AIR_TEMP:
      case Idx_CAPE:
      case Idx_CLOUD_TOT:
      case Idx_HUMID_RE:
      case Idx_PRECIP_TOT:
      case Idx_SEA_TEMP:
      case Idx_PRESSURE:
      case Idx_COMP_REFL:
        if (grib->m_GribRecordPtrArray[i]) {
          m_NewGrib->SetUnRefGribRecord(
              i, new GribRecord(*grib->m_GribRecordPtrArray[i]));
        }
        break;
      default:
        break;
    }
  }
  m_SharedNewGrib.SetGribRecordSet(m_NewGrib);
  {
    wxMutexLocker lock(s_key_mutex);
    grib_key[m_NewGrib->m_Reference_Time] =
        m_SharedNewGrib.GetSharedGribRecordSet();
  }
  PublishTimelineFrame(timeline_key, m_SharedNewGrib);
}

void RouteMap::SetNewGrib(WR_GribRecordSet* grib, std::int64_t timeline_key) {
  if (!grib || !grib->m_GribRecordPtrArray[Idx_WIND_VX] ||
      !grib->m_GribRecordPtrArray[Idx_WIND_VY]) {
    PublishTimelineFrame(timeline_key, Shared_GribRecordSet());
    return;
  }

  {
    std::map<time_t, std::weak_ptr<WR_GribRecordSet>>::iterator it;
    wxMutexLocker lock(s_key_mutex);
    it = grib_key.find(grib->m_Reference_Time);
    const std::shared_ptr<WR_GribRecordSet> existing =
        it == grib_key.end() ? nullptr : it->second.lock();
    if (existing) {
      m_SharedNewGrib.SetSharedGribRecordSet(existing);
      m_NewGrib = m_SharedNewGrib.GetGribRecordSet();
      if (m_NewGrib->m_ID == grib->m_ID) {
        PublishTimelineFrame(timeline_key, m_SharedNewGrib);
        return;
      }
    }
  }
  /* copy the grib record set */
  m_NewGrib = new WR_GribRecordSet(grib->m_ID);
  m_NewGrib->m_Reference_Time = grib->m_Reference_Time;
  for (int i = 0; i < Idx_COUNT; i++) {
    switch (i) {
      case Idx_HTSIGW:
      case Idx_WVDIR:
      case Idx_WVPER:
      case Idx_WIND_GUST:
      case Idx_WIND_VX:
      case Idx_WIND_VY:
      case Idx_SEACURRENT_VX:
      case Idx_SEACURRENT_VY:
        if (grib->m_GribRecordPtrArray[i]) {
          m_NewGrib->SetUnRefGribRecord(
              i, new GribRecord(*grib->m_GribRecordPtrArray[i]));
        }
        break;
      default:
        break;
    }
  }
  m_SharedNewGrib.SetGribRecordSet(m_NewGrib);
  {
    wxMutexLocker lock(s_key_mutex);
    grib_key[m_NewGrib->m_Reference_Time] =
        m_SharedNewGrib.GetSharedGribRecordSet();
  }
  PublishTimelineFrame(timeline_key, m_SharedNewGrib);
}

void RouteMap::GetStatistics(int& isochrons, int& routes, int& invroutes,
                             int& skippositions, int& positions) {
  Lock();
  isochrons = origin.size();
  routes = invroutes = skippositions = positions = 0;
  for (IsoChronList::iterator it = origin.begin(); it != origin.end(); ++it)
    for (IsoRouteList::iterator rit = (*it)->routes.begin();
         rit != (*it)->routes.end(); ++rit)
      (*rit)->UpdateStatistics(routes, invroutes, skippositions, positions);
  Unlock();
}

void RouteMap::Clear() {
  for (IsoChronList::iterator it = origin.begin(); it != origin.end(); ++it)
    delete *it;

  origin.clear();
}

/**
 * Get a human-readable, translatable message for a weather forecast status
 * code.
 *
 * This helper method converts a WeatherForecastStatus code into a user-friendly
 * message that can be displayed in the UI.
 *
 * @param status The status code to convert to a message.
 * @return wxString containing the translated status message.
 */
wxString RouteMap::GetWeatherForecastStatusMessage(
    WeatherForecastStatus status) {
  switch (status) {
    case WEATHER_FORECAST_SUCCESS:
      return wxEmptyString;
    case WEATHER_FORECAST_NO_GRIB_DATA:
      return _("GRIB has no data");
    case WEATHER_FORECAST_NO_WIND_DATA:
      return _("GRIB does not contain wind data");
    case WEATHER_FORECAST_NO_CLIMATOLOGY_DATA:
      return _("No climatology data available");
    case WEATHER_FORECAST_CLIMATOLOGY_DISABLED:
      return _("Climatology is disabled");
    case WEATHER_FORECAST_OTHER_ERROR:
      return _("Other GRIB error");
    default:
      return _("Unknown error");
  }
}

// Implementation for route error reporting

void RouteMap::CollectPositionErrors(Position* position,
                                     std::vector<Position*>& failed_positions) {
  if (!position) return;

  // If this position has an error, add it to the list
  if (position->propagation_error != PROPAGATION_NO_ERROR) {
    failed_positions.push_back(position);
  }

  // Check parent positions recursively to find chain of propagation
  if (position->parent && !position->parent->propagated) {
    Position* parent = dynamic_cast<Position*>(position->parent);
    if (parent) CollectPositionErrors(parent, failed_positions);
  }
}

wxString RouteMap::GetRoutingErrorInfo() {
  wxString info;
  Lock();

  if (origin.empty()) {
    info = _("No routing data available.");
    Unlock();
    return info;
  }

  // Get the most recent isochron
  IsoChron* latest = origin.back();
  if (!latest) {
    info = _("No routing data available.");
    Unlock();
    return info;
  }
  std::vector<Position*> failed_positions;

  // Track error counts to find most common issues
  std::map<PropagationError, int> error_counts;

  // Look at all positions in the latest isochron
  for (IsoRouteList::iterator it = latest->routes.begin();
       it != latest->routes.end(); ++it) {
    if (!*it || !(*it)->skippoints || !(*it)->skippoints->point) continue;

    Position* p = (*it)->skippoints->point;
    Position* start = p;
    int guard = 0;
    while (p && guard++ < 100000) {
      // If this position wasn't able to propagate further, add it to analysis
      if (p->propagated && p->propagation_error != PROPAGATION_NO_ERROR) {
        failed_positions.push_back(p);
        error_counts[p->propagation_error]++;
      }
      p = p->next;
      if (p == start) break;
    }
  }

  if (failed_positions.empty()) {
    if (m_bReachedDestination) {
      info = _("Route calculation completed successfully.");
    } else {
      info =
          _("Route calculation terminated without finding a path to "
            "destination.");

      if (m_bLandCrossing) {
        info +=
            _("\nLand crossing detected - the destination may be unreachable "
              "by water.");
      }

      if (m_bBoundaryCrossing) {
        info +=
            _("\nBoundary crossing detected - the destination may be inside a "
              "boundary area.");
      }

      if (m_bGribError != wxEmptyString) {
        info += "\n" + m_bGribError;
      }
    }
  } else {
    // Report the most common propagation errors
    info = _("Route calculation failed to reach destination. Common issues:\n");

    // Sort errors by frequency
    std::vector<std::pair<PropagationError, int>> sorted_errors;
    for (const auto& error : error_counts) {
      sorted_errors.push_back(error);
    }
    std::sort(sorted_errors.begin(), sorted_errors.end(),
              [](const std::pair<PropagationError, int>& a,
                 const std::pair<PropagationError, int>& b) {
                return a.second > b.second;
              });

    // List top errors
    for (size_t i = 0; i < std::min(size_t(5), sorted_errors.size()); i++) {
      wxString error = Position::GetErrorText(sorted_errors[i].first);
      int count = sorted_errors[i].second;
      wxString txt = _("occurrences");
      info += wxString::Format("  * %s: %d %s\n", error, count, txt);
    }

    // Detailed analysis of a few positions
    info += _("\nSample position analysis:\n");

    // Sort failed positions by error type for better readability
    std::sort(failed_positions.begin(), failed_positions.end(),
              [](Position* a, Position* b) {
                return a->propagation_error < b->propagation_error;
              });

    // Show details for up to 3 positions
    for (size_t i = 0; i < std::min(size_t(3), failed_positions.size()); i++) {
      Position* pos = failed_positions[i];
      wxString txt = _("Position");
      info += wxString::Format("%s %.6f, %.6f - %s\n", txt, pos->lat, pos->lon,
                               Position::GetErrorText(pos->propagation_error));
    }

    // General route advice based on errors
    if (error_counts[PROPAGATION_LAND_INTERSECTION] > 0) {
      info +=
          wxString::Format(_("\nRoute is blocked by land. Consider increasing "
                             "'%s' or checking if destination is reachable by "
                             "water."),
                           _("Max Diverted Course"));
    }

    if (error_counts[PROPAGATION_EXCEEDED_MAX_WIND] > 0) {
      info +=
          wxString::Format(_("\nWind exceeds limits. Increase '%s' if safe."),
                           _("Max True Wind"));
    }

    if (error_counts[PROPAGATION_BOAT_SPEED_COMPUTATION_FAILED] > 0) {
      info +=
          _("\nPolar data limits blocking progress. Verify polar matches "
            "conditions.");
    }
  }

  Unlock();
  return info;
}
