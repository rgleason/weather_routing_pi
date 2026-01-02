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
   isochrone line segments which describe the position of the boat at a given
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

Shared_GribRecordSetData::~Shared_GribRecordSetData() {
  delete m_GribRecordSet;
}

weather_routing_pi* RouteMapConfiguration::s_plugin_instance = nullptr;

RouteMapConfiguration::RouteMapConfiguration()
    : StartType(START_FROM_POSITION),
      UpwindEfficiency(1.),
      DownwindEfficiency(1.),
      NightCumulativeEfficiency(1.),
      UseMotor(false),
      MotorSpeedThreshold(2.0),
      MotorSpeed(5.0),
      StartLon(0),
      EndLon(0),
      grib(nullptr),
      grib_is_data_deficient(false) {}

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
  PlugIn_Waypoint waypoint;

  if (StartType == RouteMapConfiguration::START_FROM_BOAT) {
    StartLat = GetBoatLat();
    StartLon = GetBoatLon();
    if (StartLat != NAN && StartLon != NAN) {
      havestart = true;
    }
  }

  if (!RouteGUID.IsEmpty()) {
    if (StartType == RouteMapConfiguration::START_FROM_POSITION &&
        !StartGUID.IsEmpty() && GetSingleWaypoint(StartGUID, &waypoint)) {
      StartLat = waypoint.m_lat;
      StartLon = waypoint.m_lon;
      havestart = true;
    }
    if (!EndGUID.IsEmpty() && GetSingleWaypoint(EndGUID, &waypoint)) {
      EndLat = waypoint.m_lat;
      EndLon = waypoint.m_lon;
      haveend = true;
    }
  }
  for (const auto& it : RouteMap::Positions) {
    if (StartType == RouteMapConfiguration::START_FROM_POSITION &&
        Start == it.Name) {
      double lat = it.lat;
      double lon = it.lon;
      if (!it.GUID.IsEmpty() && GetSingleWaypoint(it.GUID, &waypoint)) {
        lat = waypoint.m_lat;
        lon = waypoint.m_lon;
      }
      StartLat = lat;
      StartLon = lon;

      havestart = true;
    }
    if (End == it.Name) {
      double lat = it.lat;
      double lon = it.lon;
      if (!it.GUID.IsEmpty() && GetSingleWaypoint(it.GUID, &waypoint)) {
        lat = waypoint.m_lat;
        lon = waypoint.m_lon;
      }
      EndLat = lat;
      EndLon = lon;
      haveend = true;
    }
  }

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

RouteMap::RouteMap() {}

RouteMap::~RouteMap() { Clear(); }

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

  // request the next grib
  // in a different thread (grib record averaging going in parallel)
  delta = DetermineDeltaTime();
  m_NewTime += wxTimeSpan(0, 0, delta);
  m_bNeedsGrib = configuration.UseGrib;

  Unlock();

  IsoRouteList routelist;
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

    origin.back()->PropagateIntoList(routelist, configuration);
  }

  IsoChron* update;
  if (routelist.empty()) {
    update = nullptr;
  } else {
    IsoRouteList merged;
    if (!ReduceList(merged, routelist, configuration)) return false;

    for (IsoRouteList::iterator it = merged.begin(); it != merged.end(); ++it)
      (*it)->ReduceClosePoints();

    update =
        new IsoChron(merged, time, delta, shared_grib, grib_is_data_deficient);
  }

  Lock();
  if (update) {
    origin.push_back(update);
    if (update->Contains(m_Configuration.EndLat, m_Configuration.EndLon)) {
      SetFinished(true);  // Route reached the destination
    }
  } else {
    // No further propagation possible, but we may still have a useful partial
    // route Mark as finished but indicate destination wasn't reached
    SetFinished(false);
  }

  // take note of possible failure reasons
  UpdateStatus(configuration);

  // Maintain land cache periodically
  maintain_land_cache();

  Unlock();

  return true;
}

double RouteMap::DetermineDeltaTime() {
  double deltaTime = m_Configuration.DeltaTime;

  // Find the closest IsoRoute::IsoRoute(IsoRoute* r, IsoRoute* p)
  //    skippoints(r->skippoints->DeepCopy()), direction(r->direction), parent(p) {}
  //    to source and destination in the last isochrone.
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
    // Get the last isochrone
    IsoChron* lastIsochron = origin.back();

    // Count positions and failed propagations for adaptive time step.
    int totalPositions = 0;
    int failedPropagations = 0;

    for (IsoRouteList::iterator it = lastIsochron->routes.begin();
         it != lastIsochron->routes.end(); ++it) {
      const Position* pos = (*it)->skippoints->point;
      do {
        totalPositions++;

        // If this position failed to propagate (has no child positions in the
        // next isochrone) We'd need a way to track this information
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
  // Since the minimum configured delta time is 60 seconds, we allow a
  // minimum of 10 seconds for the adaptive time step.
  const double minDeltaTime = 10.0;
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
  Lock();
  Clear();

  m_NewGrib = nullptr;
  m_SharedNewGrib.SetGribRecordSet(0);

  m_NewTime = m_Configuration.StartTime;
  m_bNeedsGrib = m_Configuration.UseGrib && m_Configuration.RouteGUID.IsEmpty();
  m_ErrorMsg = wxEmptyString;

  m_bReachedDestination = false;
  m_bWeatherForecastStatus = WEATHER_FORECAST_SUCCESS;
  m_bPolarStatus = POLAR_SPEED_SUCCESS;
  m_bGribError = wxEmptyString;
  m_bFinished = false;
  m_bLandCrossing = false;
  m_bBoundaryCrossing = false;

  Unlock();
}

typedef wxWeakRef<Shared_GribRecordSet> Shared_GribRecordSetRef;
std::map<time_t, Shared_GribRecordSetRef> grib_key;
wxMutex s_key_mutex;

void RouteMap::SetNewGrib(GribRecordSet* grib) {
  if (!grib || !grib->m_GribRecordPtrArray[Idx_WIND_VX] ||
      !grib->m_GribRecordPtrArray[Idx_WIND_VY])
    return;

  // XXX should be grib->m_ID in a newer OpenCPN version
  unsigned int bogus_ID;  // grib->m_ID

  GribRecord* tmp = grib->m_GribRecordPtrArray[Idx_WIND_VX];
  // RecordRefDate is time_t and high byte is likely the same in many grib
  // files, add some entropy
  bogus_ID = tmp->getRecordRefDate() ^ (tmp->getIdCenter() << 24) ^
             (tmp->getNi() << 16);

  {
    std::map<time_t, Shared_GribRecordSetRef>::iterator it;
    wxMutexLocker lock(s_key_mutex);
    it = grib_key.find(grib->m_Reference_Time);
    if (it != grib_key.end() && it->second != 0) {
      m_SharedNewGrib = *it->second;
      m_NewGrib = m_SharedNewGrib.GetGribRecordSet();
      // compute fake generation grib->m_ID
      if (m_NewGrib->m_ID == bogus_ID) {
        return;
      }
    }
  }
  /* copy the grib record set */
  m_NewGrib = new WR_GribRecordSet(bogus_ID /* XXX */);
  m_NewGrib->m_Reference_Time = grib->m_Reference_Time;
  for (int i = 0; i < Idx_COUNT; i++) {
    switch (i) {
      case Idx_HTSIGW:  // significant wave height
      case Idx_WVDIR:   // wave direction
      case Idx_WVPER:   // wave period
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
}

void RouteMap::SetNewGrib(WR_GribRecordSet* grib) {
  if (!grib || !grib->m_GribRecordPtrArray[Idx_WIND_VX] ||
      !grib->m_GribRecordPtrArray[Idx_WIND_VY])
    return;

  {
    std::map<time_t, Shared_GribRecordSetRef>::iterator it;
    wxMutexLocker lock(s_key_mutex);
    it = grib_key.find(grib->m_Reference_Time);
    if (it != grib_key.end() && it->second != 0) {
      m_SharedNewGrib = *it->second;
      m_NewGrib = m_SharedNewGrib.GetGribRecordSet();
      if (m_NewGrib->m_ID == grib->m_ID) {
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
}

void RouteMap::GetStatistics(int& isochrones, int& routes, int& invroutes,
                             int& skippositions, int& positions) {
  Lock();
  isochrones = origin.size();
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
  // If this position has an error, add it to the list
  if (position->propagation_error != PROPAGATION_NO_ERROR) {
    failed_positions.push_back(position);
  }

  // Check parent positions recursively to find chain of propagation
  if (position->parent && !position->parent->propagated) {
    CollectPositionErrors(dynamic_cast<Position*>(position->parent),
                          failed_positions);
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

  // Get the most recent isochrone
  IsoChron* latest = origin.back();
  std::vector<Position*> failed_positions;

  // Track error counts to find most common issues
  std::map<PropagationError, int> error_counts;

  // Look at all positions in the latest isochrone
  for (IsoRouteList::iterator it = latest->routes.begin();
       it != latest->routes.end(); ++it) {
    Position* p = (*it)->skippoints->point;
    do {
      // If this position wasn't able to propagate further, add it to analysis
      if (p->propagated && p->propagation_error != PROPAGATION_NO_ERROR) {
        failed_positions.push_back(p);
        error_counts[p->propagation_error]++;
      }
      p = p->next;
    } while (p != (*it)->skippoints->point);
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
