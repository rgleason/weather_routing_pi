/***************************************************************************
 *   Copyright (C) 2018 by Sean D'Epagnier                                 *
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
#include <wx/glcanvas.h>

#include <algorithm>
#include <cmath>
#include <functional>
#include <list>
#include <vector>

#include "ocpn_plugin.h"
#include "pidc.h"
#include "json/json.h"
#include "Utilities.h"
#include "Boat.h"
#include "ConstraintChecker.h"
#include "WeatherDataProvider.h"
#include "RouteMapOverlay.h"
#include "RoutingFootprint.h"
#include "SettingsDialog.h"
#include "georef.h"
#include "ModernNativeRoute.h"
#include "supercpn/weather_routing/Engine.h"

void WR_GetCanvasPixLL(PlugIn_ViewPort* vp, wxPoint* pp, double lat,
                       double lon) {
  wxPoint2DDouble pix_double;
  GetDoubleCanvasPixLL(vp, &pix_double, lat, lon);
  pp->x = (int)wxRound(pix_double.m_x);
  pp->y = (int)wxRound(pix_double.m_y);
}

RouteMapOverlayThread::RouteMapOverlayThread(RouteMapOverlay& routemapoverlay)
    : wxThread(wxTHREAD_JOINABLE), m_RouteMapOverlay(routemapoverlay) {
  Create();
}

void* RouteMapOverlayThread::Entry() {
  RouteMapConfiguration cf = m_RouteMapOverlay.GetConfiguration();
  const bool defer_destination_update_to_main =
      cf.DetectLand && ConstraintChecker::IsExperimentalChartSafetyEnforced();

  if (!cf.RouteGUID.IsEmpty()) {
    std::unique_ptr<PlugIn_Route> rte = GetRoute_Plugin(cf.RouteGUID);
    PlugIn_Route* proute = rte.get();
    if (proute == nullptr) return 0;

    m_RouteMapOverlay.RouteAnalysis(proute);
  } else {
    const bool cumulativeClimatology =
        cf.ClimatologyType == RouteMapConfiguration::CUMULATIVE_MAP ||
        cf.ClimatologyType == RouteMapConfiguration::CUMULATIVE_MINUS_CALMS;
    if (ModernNativeRouteEnabled(cf)) {
      wxString modernError;
      RunModernNativeRoute(m_RouteMapOverlay, modernError);
      if (!modernError.IsEmpty())
        wxLogMessage("WR_MODERN_NATIVE_RESULT route=\"%s -> %s\" error=\"%s\"",
                     cf.Start, cf.End, modernError);
      return nullptr;
    }
    if (cumulativeClimatology)
      wxLogMessage(
          "WR_MODERN_NATIVE_FALLBACK route=\"%s -> %s\" reason=\"cumulative "
          "climatology distribution requires legacy probabilistic semantics\"",
          cf.Start, cf.End);
    while (!TestDestroy() && !m_RouteMapOverlay.Finished()) {
      {
        RouteMapOverlay::DestinationUpdateGuard destination_update_guard(
            m_RouteMapOverlay);
        if (!m_RouteMapOverlay.Propagate()) {
          wxThread::Sleep(50);
          continue;
        }
        // don't do it inside worker thread, race
        // m_RouteMapOverlay.UpdateCursorPosition();
        if (!defer_destination_update_to_main)
          m_RouteMapOverlay.UpdateDestination();
        wxThread::Sleep(5);
        continue;
      }
    }
  }
  //    m_RouteMapOverlay.m_Thread = nullptr;
  return 0;
}

RouteMapOverlay::RouteMapOverlay()
    : m_UpdateOverlay(true),
      m_bEndRouteVisible(false),
      m_Thread(nullptr),
      m_bUpdatingDestination(false),
      last_cursor_lat(0),
      last_cursor_lon(0),
      last_cursor_position(nullptr),
      destination_position(nullptr),
      last_destination_position(nullptr),
      m_bUpdated(false),
      m_overlaylist(0),
      clear_destination_plotdata(false),
      wind_barb_cache_scale(NAN),
      wind_barb_cache_origin_size(0),
      current_cache_scale(NAN),
      current_cache_origin_size(0) {}

RouteMapOverlay::~RouteMapOverlay() {
  if (m_Thread) {
    Stop();
    DeleteThread();
  }
  if (m_UsesModernNativeResult) {
    for (Position* position : m_ModernRoutePositions) delete position;
    for (Position* position : m_ModernCursorRoutePositions) delete position;
  } else {
    delete destination_position;
  }
}

bool RouteMapOverlay::Start(wxString& error) {
  if (m_Thread) {
    error = _("error, thread already created\n");
    return false;
  }

  error = LoadBoat();
  if (error.size()) return false;

  RouteMapConfiguration configuration = GetConfiguration();
  /* test for cyclone data if needed */
  if (configuration.AvoidCycloneTracks &&
      (!ClimatologyCycloneTrackCrossings ||
       !WeatherDataProvider::CanInvokeClimatology(
           ClimatologyService::CycloneTracks) ||
       ClimatologyCycloneTrackCrossings(0, 0, 0, 0, wxDateTime(), 0) == -1)) {
    error =
        _("Configuration specifies cyclone track avoidance and Climatology "
          "cyclone data is not available");
    return false;
  }

  if (configuration.DetectBoundary &&
      !RouteMap::ODFindClosestBoundaryLineCrossing) {
    error =
        _("Configuration specifies boundary exclusion but ocpn_draw_pi "
          "boundary data not available");
    return false;
  }

  if (!configuration.UseGrib &&
      configuration.ClimatologyType <= RouteMapConfiguration::CURRENTS_ONLY) {
    error = _("Configuration does not allow grib or climatology wind data");
    return false;
  }

  Lock();
  m_ModernProgressStage.clear();
  m_ModernProgressDetail.clear();
  m_ModernProgressUpdated = false;
  Unlock();

  m_Thread = new RouteMapOverlayThread(*this);
  m_Thread->Run();
  return true;
}

void RouteMapOverlay::RouteAnalysis(PlugIn_Route* proute) {
  std::list<PlotData>& plotdata = last_destination_plotdata;
  RouteMapConfiguration configuration = GetConfiguration();

  configuration.polar_status = POLAR_SPEED_SUCCESS;
  configuration.wind_data_status = wxEmptyString;
  configuration.boundary_crossing = false;
  configuration.land_crossing = false;

  wxPlugin_WaypointListNode* pwpnode = proute->pWaypointList->GetFirst();
  PlugIn_Waypoint* pwp;
  wxDateTime curtime;

  RoutePoint rte, *next;
  PlotData data;
  data.time = configuration.StartTime;
  curtime = data.time;
  double dt = configuration.DeltaTime;  // UsedDeltaTime;
  // sog, cog, stw, ctw, VW, W, tws, twd, currentSpeed, currentDir, WVHT;
  // double VW_GUST;
  data.WVHT = 0;
  data.WVDIR = NAN;
  data.WVREL = NAN;
  data.WVPER = NAN;
  data.VW_GUST = 0;
  data.delta = dt;
  bool ok = true;
  data.lat = configuration.StartLat, data.lon = configuration.StartLon;
  while (pwpnode) {
    pwp = pwpnode->GetData();
    configuration.time = data.time;
    data.lat = pwp->m_lat, data.lon = pwp->m_lon;
    double eta = dt;
    pwpnode = pwpnode->GetNext();  // PlugInWaypoint
    if (pwpnode == nullptr) break;

    int data_mask = 0;
    double H;
    pwp = pwpnode->GetData();
    rte.lat = pwp->m_lat, rte.lon = pwp->m_lon;
    next = &rte;
    eta = data.PropagateToPoint(rte.lat, rte.lon, configuration, H, data_mask,
                                false);
    if (std::isnan(eta)) {
      ok = false;
      eta = dt;
    }
    // ll_gc_ll_reverse(data.lat, data.lon, next->lat, next->lon, &data.cog,
    // &data.sog);
    curtime += wxTimeSpan(0, 0, eta);
    if (configuration.wind_data_status == wxEmptyString) {
      data.GetPlotData(next, eta, configuration, data);
      plotdata.push_back(data);
    }
    if (!ok) break;
    data.time = curtime;
  }
  Lock();
  m_bUpdated = true;
  m_UpdateOverlay = true;
  last_destination_position = new Position(
      data.lat, data.lon, nullptr /* position */, NAN /* heading */,
      NAN /* bearing*/, data.polar, 0 /* tacks */, 0 /* jibes */,
      0 /* sailplan changes */, 0 /* data_mask */, true /* data_deficient */);

  last_cursor_plotdata = last_destination_plotdata;
  if (ok) {
    m_EndTime = data.time;
  }
  SetFinished(ok);
  UpdateStatus(configuration);
  Unlock();
}

void RouteMapOverlay::SetModernNativeProgress(
    const supercpn::weather_routing::RoutingProgressUpdate& progress) {
  using supercpn::weather_routing::RoutingProgressStage;
  wxString stage;
  switch (progress.stage) {
    case RoutingProgressStage::Preflight:
      stage = _("Preparing modern native route");
      break;
    case RoutingProgressStage::CurrentCoverage:
      stage = _("Preparing current coverage");
      break;
    case RoutingProgressStage::ForwardIsochrone:
      stage = _("Forward isochrone");
      break;
    case RoutingProgressStage::ReverseRecovery:
      stage = _("Reverse-isocrone recovery");
      break;
    case RoutingProgressStage::FrontierRecovery:
      stage = _("Isochrone graph recovery");
      break;
    case RoutingProgressStage::GraphFallback:
      stage = _("Time-dependent graph fallback");
      break;
    case RoutingProgressStage::Validation:
      stage = _("Independent dense route validation");
      break;
    case RoutingProgressStage::Complete:
      stage = _("Route complete");
      break;
  }
  const wxString detail = wxString::Format(
      _("Attempt %u/%u — %llu states generated, %llu retained, %llu chart "
        "checks"),
      progress.attempt, progress.totalAttempts,
      static_cast<unsigned long long>(progress.generatedStates),
      static_cast<unsigned long long>(progress.retainedStates),
      static_cast<unsigned long long>(progress.landChecks));
  Lock();
  m_ModernProgressStage = stage;
  m_ModernProgressDetail = detail;
  m_ModernProgressUpdated = true;
  Unlock();
}

bool RouteMapOverlay::GetModernNativeProgress(wxString& stage,
                                              wxString& detail) {
  Lock();
  stage = m_ModernProgressStage;
  detail = m_ModernProgressDetail;
  const bool available = !stage.IsEmpty() && m_ModernProgressUpdated;
  m_ModernProgressUpdated = false;
  Unlock();
  return available;
}

void RouteMapOverlay::InstallModernNativeResult(
    const supercpn::weather_routing::RoutingResult& result) {
  namespace wr = supercpn::weather_routing;
  const bool complete =
      result.status == wr::RoutingStatus::Complete ||
      result.status == wr::RoutingStatus::CompleteUsingReverseRecovery ||
      result.status == wr::RoutingStatus::CompleteUsingFrontierRecovery ||
      result.status == wr::RoutingStatus::CompleteUsingGraphFallback;
  SetFailureReason(complete ? wxString() : wxString::FromUTF8(result.message));

  RouteMapConfiguration configuration = GetConfiguration();
  configuration.ReverseRecoveryUsed =
      result.solverPath == wr::SolverPath::ReverseRecovery;
  configuration.ReverseConnectionFound = configuration.ReverseRecoveryUsed;
  configuration.ReverseFinalValidationPass = result.validation.passed;
  configuration.ReverseLayersBuilt =
      static_cast<long>(result.diagnostics.reverseLayers);
  configuration.ReverseNodesGenerated =
      static_cast<long>(result.diagnostics.reverseNodes);
  configuration.ReverseRecoveryStatus =
      configuration.ReverseRecoveryUsed ? _("complete") : wxString();
  SetConfigurationPreserveResult(configuration);

  Lock();
  if (m_UsesModernNativeResult) {
    for (Position* position : m_ModernRoutePositions) delete position;
    for (Position* position : m_ModernCursorRoutePositions) delete position;
  } else {
    delete destination_position;
  }
  m_ModernRoutePositions.clear();
  m_ModernCursorRoutePositions.clear();
  m_ModernCursorLayer = std::numeric_limits<std::size_t>::max();
  m_ModernCursorTrace = std::numeric_limits<std::size_t>::max();
  m_ModernIsochrones.clear();
  destination_position = nullptr;
  last_destination_position = nullptr;
  last_cursor_position = nullptr;
  last_destination_plotdata.clear();
  last_cursor_plotdata.clear();
  m_UsesModernNativeResult = true;

  m_ModernIsochrones.reserve(result.visualization.isochrones.size());
  for (const wr::IsochroneLayer& source : result.visualization.isochrones) {
    ModernIsochroneLayer layer;
    layer.time =
        wxDateTime(static_cast<time_t>(source.time.time_since_epoch().count()));
    layer.reverse = source.reverse;
    layer.contours.reserve(source.contours.size());
    for (const wr::IsochroneContour& sourceContour : source.contours) {
      std::vector<std::pair<double, double>> contour;
      contour.reserve(sourceContour.points.size());
      for (const wr::GeoPoint& point : sourceContour.points)
        contour.emplace_back(point.latitude, point.longitude);
      if (contour.size() >= 2) layer.contours.push_back(std::move(contour));
    }
    layer.traces.reserve(source.traces.size());
    for (const wr::IsochroneTrace& sourceTrace : source.traces) {
      ModernIsochroneLayer::Trace trace;
      trace.endpoint = {sourceTrace.endpoint.latitude,
                        sourceTrace.endpoint.longitude};
      trace.route.reserve(sourceTrace.route.size());
      for (const wr::GeoPoint& point : sourceTrace.route)
        trace.route.emplace_back(point.latitude, point.longitude);
      if (!trace.route.empty()) layer.traces.push_back(std::move(trace));
    }
    m_ModernIsochrones.push_back(std::move(layer));
  }

  if (complete && !result.legs.empty()) {
    Position* parent = new Position(configuration.StartLat,
                                    configuration.StartLon, nullptr, NAN, NAN);
    m_ModernRoutePositions.push_back(parent);
    int tacks = 0;
    int jibes = 0;
    int sailChanges = 0;
    int previousSailPlan = -1;
    for (const wr::RouteLeg& leg : result.legs) {
      PlotData data{};
      data.lat = leg.start.latitude;
      data.lon = leg.start.longitude;
      data.time = wxDateTime(
          static_cast<time_t>(leg.startTime.time_since_epoch().count()));
      data.delta = static_cast<double>((leg.endTime - leg.startTime).count());
      data.stw = leg.speedThroughWaterKnots;
      data.sog = leg.speedOverGroundKnots;
      data.ctw = leg.courseThroughWaterDegrees;
      data.cog = leg.courseOverGroundDegrees;
      data.twsOverWater = wr::vectorMagnitudeKnots(leg.wind);
      data.twdOverWater =
          wr::normalizeHeading(wr::vectorDirectionToDegrees(leg.wind) + 180.0);
      data.twsOverGround = data.twsOverWater;
      data.twdOverGround = data.twdOverWater;
      data.currentSpeed = wr::vectorMagnitudeKnots(leg.current);
      data.currentDir = wr::vectorDirectionToDegrees(leg.current);
      data.WVHT = leg.waves.available
                      ? leg.waves.significantHeightMetres
                      : std::numeric_limits<double>::quiet_NaN();
      data.WVDIR = leg.waves.directionFromDegrees;
      data.WVPER = leg.waves.periodSeconds;
      data.WVREL = std::isfinite(data.WVDIR)
                       ? heading_resolve(data.WVDIR - data.ctw)
                       : std::numeric_limits<double>::quiet_NaN();
      data.VW_GUST = 0.0;
      data.coastalDepartureEgress = leg.coastalDepartureEgress;
      if (leg.tackTransition) ++tacks;
      if (leg.gybeTransition) ++jibes;
      if (previousSailPlan >= 0 && leg.sailPlan >= 0 &&
          previousSailPlan != leg.sailPlan)
        ++sailChanges;
      previousSailPlan = leg.sailPlan;
      data.tacks = tacks;
      data.jibes = jibes;
      data.sail_plan_changes = sailChanges;
      data.polar = leg.sailPlan;
      data.data_mask = Position::GRIB_WIND;
      if (data.currentSpeed > 0.0) data.data_mask |= Position::GRIB_CURRENT;
      if (leg.propulsionMode != wr::PropulsionMode::Sail)
        data.data_mask |= Position::MOTOR_USED;
      last_destination_plotdata.push_back(data);

      Position* next = new Position(
          leg.end.latitude, leg.end.longitude, parent, leg.trueWindAngleDegrees,
          leg.courseOverGroundDegrees, leg.sailPlan, tacks, jibes, sailChanges,
          data.data_mask, false);
      m_ModernRoutePositions.push_back(next);
      parent = next;
    }
    destination_position = parent;
    last_destination_position = parent;
    m_EndTime = wxDateTime(static_cast<time_t>(
        result.legs.back().endTime.time_since_epoch().count()));
  } else {
    m_EndTime = wxDateTime();
  }
  m_bUpdated = true;
  m_UpdateOverlay = true;
  clear_destination_plotdata = false;
  SetFinished(complete);
  Unlock();
}

void RouteMapOverlay::DeleteThread() {
  if (!m_Thread) return;

  m_Thread->Delete();
  delete m_Thread;
  m_Thread = nullptr;
}

static void SetColor(piDC& dc, wxColour c, bool penifgl = false) {
#ifndef __OCPN__ANDROID__
  if (!dc.GetDC()) {
    glColor4ub(c.Red(), c.Green(), c.Blue(), c.Alpha());
    if (!penifgl) return;
  }
#endif
  wxPen pen = dc.GetPen();
  pen.SetColour(c);
  dc.SetPen(pen);
}

static void SetWidth(piDC& dc, int w, bool penifgl = false) {
  if (!dc.GetDC()) {
    glLineWidth(w);
    if (!penifgl) return;
  }
  wxPen pen = dc.GetPen();
  pen.SetWidth(w);
  dc.SetPen(pen);
}

void RouteMapOverlay::DrawLine(RoutePoint* p1, RoutePoint* p2, piDC& dc,
                               PlugIn_ViewPort& vp) {
  wxPoint p1p, p2p;
  WR_GetCanvasPixLL(&vp, &p1p, p1->lat, p1->lon);
  WR_GetCanvasPixLL(&vp, &p2p, p2->lat, p2->lon);

#ifndef __OCPN__ANDROID__
  if (!dc.GetDC()) {
    glVertex2d(p1p.x, p1p.y);
    glVertex2d(p2p.x, p2p.y);
  } else
#endif
  {
    dc.StrokeLine(p1p.x, p1p.y, p2p.x, p2p.y);
  }
}

void RouteMapOverlay::DrawLine(RoutePoint* p1, wxColour& color1, RoutePoint* p2,
                               wxColour& color2, piDC& dc,
                               PlugIn_ViewPort& vp) {
#if 0
    double p1plon, p2plon;
    if(fabs(vp.clon) > 90)
        p1plon = positive_degrees(p1->lon), p2plon = positive_degrees(p2->lon);
    else
        p1plon = heading_resolve(p1->lon), p2plon = heading_resolve(p2->lon);

    if((p1plon+180 < vp.clon && p2plon+180 > vp.clon) ||
       (p1plon+180 > vp.clon && p2plon+180 < vp.clon) ||
       (p1plon-180 < vp.clon && p2plon-180 > vp.clon) ||
       (p1plon-180 > vp.clon && p2plon-180 < vp.clon))
        return;
#endif

  wxPoint p1p, p2p;
  WR_GetCanvasPixLL(&vp, &p1p, p1->lat, p1->lon);
  WR_GetCanvasPixLL(&vp, &p2p, p2->lat, p2->lon);

  SetColor(dc, color1);
#ifndef __OCPN__ANDROID__
  if (!dc.GetDC()) {
    glVertex2d(p1p.x, p1p.y);
    SetColor(dc, color2);
    glVertex2d(p2p.x, p2p.y);
  } else
#endif
  {
    dc.DrawLine(p1p.x, p1p.y, p2p.x, p2p.y);
  }
}

static inline wxColour& PositionColor(Position* p, wxColour& grib_color,
                                      wxColour& climatology_color,
                                      wxColour& grib_deficient_color,
                                      wxColour& climatology_deficient_color) {
  if (p->data_mask & Position::GRIB_WIND) {
    if (p->data_mask & Position::DATA_DEFICIENT_WIND)
      return grib_deficient_color;
    else
      return grib_color;
  }

  if (p->data_mask & Position::CLIMATOLOGY_WIND) {
    if (p->data_mask & Position::DATA_DEFICIENT_WIND)
      return climatology_deficient_color;
    else
      return climatology_color;
  }

  static wxColour black(0, 0, 0);
  return black;
}

static wxColour TransparentColor(wxColor c) {
  return wxColor(c.Red(), c.Green(), c.Blue(), c.Alpha() * 7 / 24);
}

void RouteMapOverlay::RenderIsoRoute(IsoRoute* r, wxDateTime time,
                                     wxColour& grib_color,
                                     wxColour& climatology_color, piDC& dc,
                                     PlugIn_ViewPort& vp) {
  SkipPosition* s = r->skippoints;
  if (!s) return;

  wxColour grib_deficient_color = TransparentColor(grib_color);
  wxColour climatology_deficient_color = TransparentColor(climatology_color);

  Position* p = s->point;
  wxColour* pcolor =
      &PositionColor(p, grib_color, climatology_color, grib_deficient_color,
                     climatology_deficient_color);
  const bool clip_land_crossing_display =
      ConstraintChecker::IsExperimentalChartSafetyEnforced();
#ifndef __OCPN__ANDROID__
  if (!dc.GetDC() && !clip_land_crossing_display) glBegin(GL_LINES);
#endif
  do {
    wxColour& ncolor =
        PositionColor(p->next, grib_color, climatology_color,
                      grib_deficient_color, climatology_deficient_color);
    bool draw_segment = !p->copied || !p->next->copied;
    if (draw_segment && clip_land_crossing_display) {
      RouteMapConfiguration configuration = GetConfiguration();
      wxString failure_reason;
      double bearing = 0.0;
      ll_gc_ll_reverse(p->lat, p->lon, p->next->lat, p->next->lon, &bearing,
                       NULL);
      if (!ConstraintChecker::CheckFinalRouteLandConstraint(
              configuration, p->lat, p->lon, p->next->lat, p->next->lon,
              bearing, &failure_reason)) {
        draw_segment = false;
        static int s_isochrone_clip_logs = 0;
        if (s_isochrone_clip_logs < 20) {
          ++s_isochrone_clip_logs;
          wxLogMessage(
              "FINAL_ROUTE_SAFETY isochrone display segment clipped: "
              "route=\"%s -> %s\" start=(%.8f,%.8f) end=(%.8f,%.8f) "
              "reason=\"%s\"",
              configuration.Start, configuration.End, p->lat, p->lon,
              p->next->lat, p->next->lon,
              failure_reason.IsEmpty() ? _("Chart land crossing")
                                       : failure_reason);
        }
      }
    }
    if (draw_segment) {
#ifndef __OCPN__ANDROID__
      if (!dc.GetDC() && clip_land_crossing_display) glBegin(GL_LINES);
#endif
      DrawLine(p, *pcolor, p->next, ncolor, dc, vp);
#ifndef __OCPN__ANDROID__
      if (!dc.GetDC() && clip_land_crossing_display) glEnd();
#endif
    }
    pcolor = &ncolor;
    p = p->next;
  } while (p != s->point);

#ifndef __OCPN__ANDROID__
  if (!dc.GetDC() && !clip_land_crossing_display) glEnd();
#endif
  /* now render any children */
  wxColour cyan(0, 255, 255), magenta(255, 0, 255);
  for (IsoRouteList::iterator it = r->children.begin(); it != r->children.end();
       ++it)
    RenderIsoRoute(*it, time, cyan, magenta, dc, vp);
}

void RouteMapOverlay::RenderReverseReachabilityDiagnostics(
    piDC& dc, PlugIn_ViewPort& vp) {
  RouteMapConfiguration configuration = GetConfiguration();
  if (!configuration.UseReverseReachabilityRecovery) return;

  std::vector<ReverseReachabilityDebugPoint> points;
  Lock();
  points = m_reverseReachabilityDebugPoints;
  Unlock();
  if (points.empty()) return;

  SetColor(dc, wxColour(160, 0, 220, 180), true);
  SetWidth(dc, 2, true);
  for (const ReverseReachabilityDebugPoint& point : points) {
    if (!std::isfinite(point.lat) || !std::isfinite(point.lon)) continue;
    wxPoint p;
    WR_GetCanvasPixLL(&vp, &p, point.lat, point.lon);
    if (point.connected) {
      SetColor(dc, wxColour(255, 0, 255, 220), true);
      dc.DrawLine(p.x - 5, p.y, p.x + 5, p.y);
      dc.DrawLine(p.x, p.y - 5, p.x, p.y + 5);
      SetColor(dc, wxColour(160, 0, 220, 180), true);
    } else {
      dc.DrawCircle(p.x, p.y, 3);
    }
  }
}

void RouteMapOverlay::RenderAlternateRoute(IsoRoute* r, bool each_parent,
                                           piDC& dc, PlugIn_ViewPort& vp) {
  Position* pos = r->skippoints->point;
  wxColor black = wxColour(0, 0, 0, 192), tblack = TransparentColor(black);
  do {
    wxColour* color =
        pos->data_mask & Position::DATA_DEFICIENT_WIND ? &tblack : &black;
    for (Position* p = pos; p && !p->drawn && p->parent; p = p->parent) {
      //            wxColour &color = p->data_mask &
      //            Position::DATA_DEFICIENT_WIND ? tblack : black;
      wxColour& pcolor =
          p->parent->data_mask & Position::DATA_DEFICIENT_WIND ? tblack : black;
      if (!p->copied || each_parent)
        DrawLine(p, *color, p->parent, pcolor, dc, vp);
      p->drawn = true;
      if (!each_parent) break;
      color = &pcolor;
    }

    pos = pos->next;
  } while (pos != r->skippoints->point);

  wxColour blue(0, 0, 255);
  SetColor(dc, blue);
  for (IsoRouteList::iterator cit = r->children.begin();
       cit != r->children.end(); cit++)
    RenderAlternateRoute(*cit, each_parent, dc, vp);
}

static wxColour Darken(wxColour c) {
  return wxColour(c.Red() * 2 / 3, c.Green() * 2 / 3, c.Blue() * 2 / 3,
                  c.Alpha());
}

static double GetPlatformScaleFactor() {
  double scale_factor = OCPN_GetDisplayContentScaleFactor();
#ifdef __WXMSW__
  scale_factor *= OCPN_GetWinDIPScaleFactor();
#endif
  return scale_factor;
}

void RouteMapOverlay::Render(wxDateTime time, SettingsDialog& settingsdialog,
                             piDC& dc, PlugIn_ViewPort& vp, bool justendroute,
                             RoutePoint* positionOnRoute) {
  dc.SetPen(*wxBLACK);                // reset pen
  dc.SetBrush(*wxTRANSPARENT_BRUSH);  // reset brush
  if (!justendroute) {
    RouteMapConfiguration configuration = GetConfiguration();

    if (!std::isnan(configuration.StartLat)) {
      wxPoint r;
      WR_GetCanvasPixLL(&vp, &r, configuration.StartLat,
                        configuration.StartLon);
      SetColor(dc, *wxBLUE, true);
      SetWidth(dc, 3, true);
      dc.DrawLine(r.x, r.y - 10, r.x + 10, r.y + 7);
      dc.DrawLine(r.x, r.y - 10, r.x - 10, r.y + 7);
      dc.DrawLine(r.x - 10, r.y + 7, r.x + 10, r.y + 7);
    }

    if (!std::isnan(configuration.EndLat)) {
      wxPoint r;
      WR_GetCanvasPixLL(&vp, &r, configuration.EndLat, configuration.EndLon);
      SetColor(dc, *wxRED, true);
      SetWidth(dc, 3, true);
      dc.DrawLine(r.x - 10, r.y - 10, r.x + 10, r.y + 10);
      dc.DrawLine(r.x - 10, r.y + 10, r.x + 10, r.y - 10);
    }

    static const double NORM_FACTOR = 16;

    // Do not use displaylist processing to avoid incorrect vp calculations in
    // O562+
    bool use_dl = false;  // vp.m_projection_type == PI_PROJECTION_MERCATOR;
#ifndef __OCPN__ANDROID__
    if (!dc.GetDC() && use_dl) {
      glPushMatrix();

      /* center display list on start lat/lon */

      wxPoint point;
      WR_GetCanvasPixLL(&vp, &point, configuration.StartLat,
                        configuration.StartLon);

      glTranslated(point.x, point.y, 0);
      glScalef(vp.view_scale_ppm / NORM_FACTOR, vp.view_scale_ppm / NORM_FACTOR,
               1);
      glRotated(vp.rotation * 180 / M_PI, 0, 0, 1);
    }

    if (!dc.GetDC() && !m_UpdateOverlay && use_dl &&
        vp.m_projection_type == m_overlaylist_projection) {
      glCallList(m_overlaylist);
      glPopMatrix();

    } else
#endif
    {
      PlugIn_ViewPort nvp = vp;

#ifndef __OCPN__ANDROID__
      if (!dc.GetDC() && use_dl) {
        m_UpdateOverlay = false;

        if (!m_overlaylist) m_overlaylist = glGenLists(1);

        glNewList(m_overlaylist, GL_COMPILE);

        nvp.clat = configuration.StartLat, nvp.clon = configuration.StartLon;
        nvp.pix_width = nvp.pix_height = 0;
        nvp.view_scale_ppm = NORM_FACTOR;
        nvp.rotation = nvp.skew = 0;

        m_overlaylist_projection = vp.m_projection_type;
      }
#endif
      /* draw alternate routes first */
      int AlternateRouteThickness =
          settingsdialog.m_sAlternateRouteThickness->GetValue();
      if (AlternateRouteThickness) {
        Lock();
        IsoChronList::iterator it;

        /* reset drawn flag for all positions
           this is used to avoid duplicating alternate route segments */
        for (it = origin.begin(); it != origin.end(); ++it)
          (*it)->ResetDrawnFlag();

        bool AlternatesForAll = settingsdialog.m_cbAlternatesForAll->GetValue();
        if (AlternatesForAll)
          it = origin.begin();
        else {
          it = origin.end();
          it--;
        }

        SetWidth(dc, AlternateRouteThickness);
#ifndef __OCPN__ANDROID__
        if (!dc.GetDC()) glBegin(GL_LINES);
#endif
        for (; it != origin.end(); ++it)
          for (IsoRouteList::iterator rit = (*it)->routes.begin();
               rit != (*it)->routes.end(); ++rit) {
            RenderAlternateRoute(*rit, !AlternatesForAll, dc, nvp);
          }

#ifndef __OCPN__ANDROID__
        if (!dc.GetDC()) glEnd();
#endif
        Unlock();
      }

      static const unsigned char routecolors[][3] = {
          {0, 0, 128},   {0, 192, 0},   {0, 128, 192}, {0, 255, 0},
          {0, 0, 255},   {0, 128, 128}, {0, 255, 0},   {0, 192, 192},
          {0, 128, 255}, {0, 255, 128}, {0, 0, 255},   {0, 192, 0},
          {0, 0, 128},   {0, 255, 0},   {0, 192, 128}, {0, 128, 255},
          {0, 192, 0},   {0, 128, 0},   {0, 0, 255},   {0, 192, 192}};
#if 0
                {255, 127,   0}, {255, 127, 127},
                {  0, 255,   0}, {  0, 255, 127},
                {127, 255,   0}, {127, 255, 127},
                {127, 127,   0},                  {127, 127, 255},
                {255,   0,   0}, {255,   0, 127}, {255,   0, 255},
                {127,   0,   0}, {127,   0, 127}, {127,   0, 255},
                {  0, 127,   0}, {  0, 127, 127}, {  0, 127, 255},
                {255, 255,   0},                  };
#endif

      int IsoChronThickness = settingsdialog.m_sIsoChronThickness->GetValue();
      if (IsoChronThickness) {
        Lock();
        if (m_UsesModernNativeResult) {
          int c = 0;
          std::size_t closest = std::numeric_limits<std::size_t>::max();
          wxTimeSpan closestDiff = wxTimeSpan::Days(999);
          if (time.IsValid()) {
            for (std::size_t i = 0; i < m_ModernIsochrones.size(); ++i) {
              wxTimeSpan diff = m_ModernIsochrones[i].time - time;
              if (diff.GetValue() < 0) diff = -diff;
              if (diff < closestDiff) {
                closestDiff = diff;
                closest = i;
              }
            }
          }
          for (std::size_t i = 0; i < m_ModernIsochrones.size(); ++i) {
            const ModernIsochroneLayer& layer = m_ModernIsochrones[i];
            wxColor color = layer.reverse
                                ? wxColor(208, 72, 192, 224)
                                : wxColor(routecolors[c][0], routecolors[c][1],
                                          routecolors[c][2], 224);
            SetColor(dc, color, true);
            SetWidth(dc,
                     i == closest ? IsoChronThickness * 3 : IsoChronThickness,
                     true);
            for (const auto& contour : layer.contours) {
              for (std::size_t p = 1; p < contour.size(); ++p) {
                wxPoint from, to;
                WR_GetCanvasPixLL(&nvp, &from, contour[p - 1].first,
                                  contour[p - 1].second);
                WR_GetCanvasPixLL(&nvp, &to, contour[p].first,
                                  contour[p].second);
                if (std::abs(to.x - from.x) < nvp.pix_width / 2)
                  dc.DrawLine(from.x, from.y, to.x, to.y);
              }
            }
            if (++c ==
                static_cast<int>(sizeof routecolors / sizeof *routecolors))
              c = 0;
          }
          Unlock();
        } else {
          int c = 0;
          // Find the isochron closest to the GRIB time
          IsoChron* closestIsochron = nullptr;
          wxTimeSpan closestDiff =
              wxTimeSpan::Days(999);  // A large initial value
          if (time.IsValid()) {
            for (IsoChronList::iterator i = origin.begin(); i != origin.end();
                 ++i) {
              wxTimeSpan diff = (*i)->time - time;
              if (diff.GetValue() < 0) {
                diff = -diff;
              }
              if (diff < closestDiff) {
                closestDiff = diff;
                closestIsochron = *i;
              }
            }
          }
          for (IsoChronList::iterator i = origin.begin(); i != origin.end();
               ++i) {
            Unlock();
            wxColor grib_color(routecolors[c][0], routecolors[c][1],
                               routecolors[c][2], 224);
            wxColor climatology_color(255 - routecolors[c][0],
                                      routecolors[c][2], routecolors[c][1],
                                      224);
            // If this is the closest isochron to the selected GRIB time, use a
            // thicker line
            if (time.IsValid() && *i == closestIsochron) {
              SetWidth(dc, IsoChronThickness * 3);
            } else {
              SetWidth(dc, IsoChronThickness);
            }
            for (IsoRouteList::iterator j = (*i)->routes.begin();
                 j != (*i)->routes.end(); ++j)
              RenderIsoRoute(*j, time, grib_color, climatology_color, dc, nvp);

            if (++c == (sizeof routecolors) / (sizeof *routecolors)) c = 0;
            Lock();
          }
          Unlock();
        }
      }

      RenderReverseReachabilityDiagnostics(dc, nvp);

#ifndef __OCPN__ANDROID__
      if (!dc.GetDC() && use_dl) {
        glEndList();
        glCallList(m_overlaylist);
        glPopMatrix();
      }
#endif
    }
  }

  int RouteThickness = settingsdialog.m_sRouteThickness->GetValue();
  if (RouteThickness) {
    wxColour CursorColor = settingsdialog.m_cpCursorRoute->GetColour(),
             DestinationColor =
                 settingsdialog.m_cpDestinationRoute->GetColour();
    bool MarkAtPolarChange = settingsdialog.m_cbMarkAtPolarChange->GetValue();

    if (!justendroute && settingsdialog.m_cbDisplayCursorRoute->GetValue()) {
      SetColor(dc, CursorColor, true);
      SetWidth(dc, RouteThickness, true);
      RenderCourse(true, dc, vp);

      if (MarkAtPolarChange) {
        SetColor(dc, Darken(CursorColor), true);
        SetWidth(dc, (RouteThickness + 1) / 2, true);
        RenderPolarChangeMarks(true, dc, vp);
      }
    }
    SetColor(dc, DestinationColor, true);
    SetWidth(dc, RouteThickness, true);
    bool confortOnRoute = settingsdialog.m_cbDisplayComfort->GetValue();
    RenderCourse(false, dc, vp, confortOnRoute);
    SetColor(dc, Darken(DestinationColor), true);
    SetWidth(dc, RouteThickness / 2, true);
    RenderBoatOnCourse(false, time, dc, vp);

    // Start WindBarbsOnRoute customization
    int lineWidth = settingsdialog.m_sWindBarbsOnRouteThickness->GetValue();
    bool apparent = settingsdialog.m_cbDisplayApparentWindBarbs->GetValue();
    if (lineWidth > 0) RenderWindBarbsOnRoute(dc, vp, lineWidth, apparent);

    // CUSTOMIZATION
    // Display the position of the cursor on route
    // where the infos are read from Route Position window
    if (positionOnRoute != nullptr) {
      wxPoint r;
      WR_GetCanvasPixLL(&vp, &r, positionOnRoute->lat, positionOnRoute->lon);
      wxColour ownBlue(20, 83, 186);
      SetColor(dc, ownBlue, true);
      SetWidth(dc, RouteThickness, true);
      double circle_size = 10;  // logical pixels.
      dc.StrokeCircle(r.x, r.y, circle_size);
    }

    if (MarkAtPolarChange) {
      SetColor(dc, Darken(DestinationColor), true);
      SetWidth(dc, (RouteThickness + 1) / 2, true);
      RenderPolarChangeMarks(false, dc, vp);
    }
  }
}

void RouteMapOverlay::RenderPolarChangeMarks(bool cursor_route, piDC& dc,
                                             PlugIn_ViewPort& vp) {
  Position* pos =
      cursor_route ? last_cursor_position : last_destination_position;

  if (!pos) return;

  std::list<PlotData> plot = GetPlotData(cursor_route);
  std::list<PlotData>::iterator itt = plot.begin();
  if (itt == plot.end()) {
    return;
  }

#ifndef __OCPN__ANDROID__
  if (!dc.GetDC()) glBegin(GL_LINES);
#endif

  int polar = itt->polar;
  for (; itt != plot.end(); itt++) {
    if (itt->polar == polar) continue;
    wxPoint r;
    WR_GetCanvasPixLL(&vp, &r, itt->lat, itt->lon);
    int s = 6;
#ifndef __OCPN__ANDROID__
    if (!dc.GetDC()) {
      glVertex2i(r.x - s, r.y - s), glVertex2i(r.x + s, r.y - s);
      glVertex2i(r.x + s, r.y - s), glVertex2i(r.x + s, r.y + s);
      glVertex2i(r.x + s, r.y + s), glVertex2i(r.x - s, r.y + s);
      glVertex2i(r.x - s, r.y + s), glVertex2i(r.x - s, r.y - s);
    } else
#endif
      dc.DrawRectangle(r.x - s, r.y - s, 2 * s, 2 * s);
    polar = itt->polar;
  }
#ifndef __OCPN__ANDROID__
  if (!dc.GetDC()) glEnd();
#endif
}

/* Customization ComfortDisplay
 * ----------------------------------------------------------------------------
 * The idea is to display on the weather route different colors giving an idea
 * of the sailing comfort along the trip:
 *    Green = Light conditions, relax and enjoy
 *    Orange = Can be tough, stay focus
 *    Red = Strong conditions, heavy sailors, be prepared
 */
int RouteMapOverlay::sailingConditionLevel(const PlotData& plot) const {
  /* Method to calculate a indicator between 1 and 3 of the sailing conditions
   * based on wind, wind course and waves.
   *
   * All these calculations are empirical and just made from experience and how
   * people feel sailing comfort which is a highly subjective value...
   */

  double level_calc = 0.0;

  // Define maximum constants. Over this value, sailing comfort is very impacted
  // (coef > 1) and automatically displayed in red.
  // Definitions:
  // AW   - Apparent Wind Direction from the boat (0 = upwind)
  // VW   - Velocity of wind over water
  // WVHT - Swell (if available)
  double MAX_WV = 27;   // Vigilant over 27knts == 7B
  double MAX_AW = 35;   // Upwind start at 35° from wind
  double MAX_WVHT = 5;  // No more than 5m waves

  // Wind impact exponentially on sailing comfort
  // We propose a power 3 function as difficulties increase exponentially
  // Over 30knts, it starts to be tough
  double twsOverWater = plot.twsOverWater;
  double WV_normal = pow(twsOverWater / MAX_WV, 3);

  // Wind direction impact on sailing comfort.
  // Ex: if you decide to sail upwind with 30knts, it is not the same
  // conditions as if you sail downwind (impact of waves, heel, and more).
  // Use a normal distribution to set the maximum difficulty at 35° upwind,
  // and reduce when we go downwind.
  double AW = heading_resolve(plot.ctw - plot.twdOverWater);
  double teta = 30;
  double mu = 35;
  double amp = 20;
  double AW_normal = amp * (1 / (teta * pow((2 * M_PI), 0.5))) *
                     exp(-pow(AW - mu, 2) / (2 * pow(teta, 2)));

  // If available, add swell conditions in comfort model.
  // Use same exponential function for swell as sailing
  // comfort exponentially decrease with swell height.
  double WVHT = plot.WVHT;
  double WVHT_normal = 0.0;
  if (WVHT > 0) WVHT_normal = pow(WVHT / MAX_WVHT, 2);

  // Calculate score
  // Use an OR function X,Y E [0,1], f(X,Y) = 1-(1-X)(1-Y)
  level_calc = 1 - (1 - WV_normal * (1 + AW_normal) * (1 + WVHT_normal));

  if (level_calc <= 0.5)
    // Light conditions, enjoy ;-)
    return 1;
  if (level_calc > 0.5 && level_calc < 1)
    // Can be tough
    return 2;
  if (level_calc >= 1)
    // Strong conditions
    return 3;
  return 0;
}

wxColour RouteMapOverlay::sailingConditionColor(int level) {
  switch (level) {
    case 1:
      return wxColor(50, 205, 50);
    case 2:
      return wxColor(255, 165, 0);
    case 3:
      return *wxRED;
  }
  return *wxBLACK;
}

wxString RouteMapOverlay::sailingConditionText(int level) {
  if (level == 1) return _("Good");
  if (level == 2) return _("Bumpy");
  if (level == 3) return _("Difficult");
  return _("N/A");
}

// -----------------------------------------------------

void RouteMapOverlay::RenderCourse(bool cursor_route, piDC& dc,
                                   PlugIn_ViewPort& vp, bool comfortRoute) {
  Position* pos =
      cursor_route ? last_cursor_position : last_destination_position;
  if (!pos) return;

  Lock();

  bool rte = !GetConfiguration().RouteGUID.IsEmpty();
  if (cursor_route == true) {
    // never draw comfort if cursor route
    assert(comfortRoute == false);
    if (!rte) {
#ifndef __OCPN__ANDROID__
      if (!dc.GetDC()) glBegin(GL_LINES);
#endif
      for (Position* p = pos; p && p->parent; p = p->parent)
        DrawLine(p, p->parent, dc, vp);
#ifndef __OCPN__ANDROID__
      if (!dc.GetDC()) glEnd();
#endif
    }
    Unlock();
    return;
  }
  Unlock();

  /* ComfortDisplay Customization
   * ------------------------------------------------
   * To get weather data (wind, current, waves) on a
   * position and through time, iterate over the
   * position and in parallel on GetPlotData
   * Thanks Sean for your help :-)
   */
  std::list<PlotData> plot = GetPlotData(false);
  std::list<PlotData>::reverse_iterator itt = plot.rbegin();
  std::list<PlotData>::reverse_iterator inext = itt;

  if (itt == plot.rend()) {
    return;
  }

  wxColor lc = sailingConditionColor(sailingConditionLevel(*itt));

  /* draw lines to this route */
#ifndef __OCPN__ANDROID__
  if (!dc.GetDC()) glBegin(GL_LINES);
#endif

  /* end point if reached is not in GetPlotData */
  RoutePoint *to, from;
  for (to = pos; itt != plot.rend(); inext = itt, itt++, to = &(*itt)) {
    RoutePoint* from = &(*inext);
    if (comfortRoute) {
      wxColor c = sailingConditionColor(sailingConditionLevel(*itt));
      DrawLine(to, c, from, lc, dc, vp);
      lc = c;
    } else {
      DrawLine(to, from, dc, vp);
    }
  }

#ifndef __OCPN__ANDROID__
  if (!dc.GetDC()) glEnd();
#endif
}

void RouteMapOverlay::RenderBoatOnCourse(bool cursor_route, wxDateTime time,
                                         piDC& dc, PlugIn_ViewPort& vp) {
  /* Dedicated method to render the boat circle
   * on the weather route to be able to select the
   * color of the maker, and avoid to generate twice
   * (1 normally, 1 for polar changed -- to avoid)
   */
  Position* pos =
      cursor_route ? last_cursor_position : last_destination_position;
  if (!pos) return;

  std::list<PlotData> plot = GetPlotData(cursor_route);
  // Legacy plot data deliberately omits the reached endpoint because the
  // Position chain supplies it while drawing.  The modern engine also uses
  // that representation; add a local endpoint sample so time interpolation
  // can still place the boat on the final leg.
  if (m_UsesModernNativeResult && !plot.empty()) {
    PlotData endpoint = plot.back();
    endpoint.lat = pos->lat;
    endpoint.lon = pos->lon;
    endpoint.time = cursor_route ? m_cursor_time : m_EndTime;
    endpoint.delta = 0.0;
    plot.push_back(endpoint);
  }

  for (auto it = plot.begin(); it != plot.end();) {
    wxDateTime ittime = it->time;

    wxDateTime timestart = ittime;
    double plat = it->lat;
    double plon = it->lon;
    it++;
    if (it == plot.end()) break;

    wxDateTime timeend = it->time;
    if (!(time >= timestart && time <= timeend)) continue;

    wxTimeSpan span = timeend - timestart, cspan = time - timestart;
    double d = cspan.GetSeconds().ToDouble() / span.GetSeconds().ToDouble();

    if (d > 1)
      // d = 1; // draw at end??
      break;  // don't draw if grib time is after end

    wxPoint r;
    WR_GetCanvasPixLL(&vp, &r, plat + d * (it->lat - plat),
                      plon + d * heading_resolve(it->lon - plon));

    SetWidth(dc, 8, true);
    double circle_size = 20;  // logical pixels
    dc.DrawCircle(r.x, r.y, circle_size);

    // Outer circle
    dc.SetPen(wxPen(*wxWHITE, 4));
    // dc.SetBrush(wxBrush(*wxTRANSPARENT_BRUSH));
    dc.DrawCircle(r.x, r.y, circle_size + 4);

    // Inner circle
    // dc.SetPen(wxPen(*wxBLACK, 1));
    dc.DrawCircle(r.x, r.y, circle_size - 4);
    break;
  }
}

void RouteMapOverlay::RenderWindBarbsOnRoute(piDC& dc, PlugIn_ViewPort& vp,
                                             int lineWidth, bool apparentWind) {
  /* Method to render wind barbs on the route that has been generated
   * by WeatherRouting plugin. The idead is to visualize the wind
   * direction and strength at any step of the trip.
   *
   * Customization by: Sylvain Carlioz -- with Pavel Kalian's help ;-)
   * OpenCPN's licence
   * March, 2018
   */

  if (vp.bValid == false) return;

  RouteMapConfiguration configuration = GetConfiguration();

  // Create a specific viewport at position (0,0)
  // to draw the winds barbs, and then translate it
  PlugIn_ViewPort nvp = vp;
  // calculate wind barbs along the route by looping
  // over [GetPlotData(false)] list which contains lat,
  // lon, wind info for each points, only if needed.
  std::list<PlotData> plot = GetPlotData(false);

  // if no route has been calculated by WeatherRouting,
  // then stops the method.
  if (plot.empty()) return;

  for (std::list<PlotData>::iterator it = plot.begin(); it != plot.end();
       it++) {
    wxPoint p;
    WR_GetCanvasPixLL(&nvp, &p, it->lat, it->lon);

    // available
    //   twd tws : winds over ground
    //   W VW : winds over water
    //   currentDir currentSpeed : current
    //
    //   cog sog : boat speed over ground
    //   ctw  stw  : boat speed over water
    float windSpeed = it->twsOverWater;
    float windDirection =
        it->twdOverWater;  // heading_resolve(it->ctw - it->W);

    // By default, display true wind
    float finalWindSpeed = windSpeed;
    float finalWindDirection = windDirection;

    if (apparentWind) {
      finalWindSpeed = Polar::VelocityApparentWind(
          it->stw, heading_resolve(it->ctw - windDirection), windSpeed);
      finalWindDirection = heading_resolve(
          it->ctw -
          Polar::DirectionApparentWind(finalWindSpeed, it->stw,
                                       heading_resolve(it->ctw - windDirection),
                                       it->twsOverWater));
    }

    // Draw barbs
    g_barbsOnRoute_LineBufferOverlay.setLineWidth(lineWidth);
    g_barbsOnRoute_LineBufferOverlay.pushWindArrowWithBarbs(
        wind_barb_route_cache, p.x, p.y, finalWindSpeed,
        deg2rad(finalWindDirection) + nvp.rotation, it->lat < 0, true);
  }
  wind_barb_route_cache.Finalize();

  // Draw the wind barbs
  wxPoint point;
  WR_GetCanvasPixLL(&vp, &point, configuration.StartLat,
                    configuration.StartLon);
  wxColour colour;
  if (apparentWind) {
    wxColour blue(20, 83, 186);
    colour = blue;
  } else {
    wxColour purple(170, 0, 170);
    colour = purple;
  }

  if (dc.GetDC()) {
    dc.SetPen(wxPen(colour, 2));
  }
#if defined(ocpnUSE_GL) && !defined(__OCPN__ANDROID__)
  else {
    // Mandatory to avoid display issue when moving map
    // (map disappear to show a gray background...)
    glPushMatrix();

    // Anti-aliasing options to render
    // wind barbs at best quality (copy from grip_pi)
    glEnable(GL_BLEND);
    glEnable(GL_LINE_SMOOTH);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glHint(GL_LINE_SMOOTH_HINT, GL_NICEST);

    glColor3ub(colour.Red(), colour.Green(), colour.Blue());
    glLineWidth(lineWidth);
    glEnableClientState(GL_VERTEX_ARRAY);
  }
#endif

  wind_barb_route_cache.draw(dc.GetDC());

#if defined(ocpnUSE_GL) && !defined(__OCPN__ANDROID__)
  if (!dc.GetDC()) {
    glDisableClientState(GL_VERTEX_ARRAY);
    glPopMatrix();
  }
#endif
}

void RouteMapOverlay::RenderWindBarbs(piDC& dc, PlugIn_ViewPort& vp) {
  if (origin.size() < 2)  // no map to work with
    return;

  if (vp.bValid == false) return;

  RouteMapConfiguration configuration = GetConfiguration();

  // if zoomed way in, don't cache the arrows for panning, instead we just
  // render what's onscreen
  double latmin, latmax, lonmin, lonmax;
  GetLLBounds(latmin, latmax, lonmin, lonmax);

  PlugIn_ViewPort nvp = vp;
  nvp.clat = configuration.StartLat, nvp.clon = configuration.StartLon;
  nvp.pix_width = nvp.pix_height = 0;
  nvp.rotation = nvp.skew = 0;

  wxPoint p1, p2, p3, p4;
  WR_GetCanvasPixLL(&nvp, &p1, latmin, lonmin);
  WR_GetCanvasPixLL(&nvp, &p2, latmin, lonmax);
  WR_GetCanvasPixLL(&nvp, &p3, latmax, lonmin);
  WR_GetCanvasPixLL(&nvp, &p4, latmax, lonmax);

  wxRect r;
  r.x = wxMin(wxMin(p1.x, p2.x), wxMin(p3.x, p4.x));
  r.y = wxMin(wxMin(p1.y, p2.y), wxMin(p3.y, p4.y));
  r.width = wxMax(wxMax(p1.x, p2.x), wxMax(p3.x, p4.x)) - r.x;
  r.height = wxMax(wxMax(p1.y, p2.y), wxMax(p3.y, p4.y)) - r.y;

  // we could somehow "append" to the cache as passing occurs when zoomed really
  // far in rather than making a complete cache... but how complex does it need
  // to be? quick an dirty, convert to double or integer may overflow
  bool nocache = (double)r.width * (double)r.height >
                     (double)(vp.rv_rect.width * vp.rv_rect.height * 4) ||
                 vp.m_projection_type != PI_PROJECTION_MERCATOR;

  if (nocache || origin.size() != wind_barb_cache_origin_size ||
      vp.view_scale_ppm != wind_barb_cache_scale ||
      vp.m_projection_type != wind_barb_cache_projection) {
    wxStopWatch timer;
    static double step = 36.0;

    wind_barb_cache_origin_size = origin.size();
    wind_barb_cache_scale = vp.view_scale_ppm;
    wind_barb_cache_projection = vp.m_projection_type;

    if (nocache) {
      r = vp.rv_rect;
      nvp = vp;
    }

    Lock();

    wxPoint p;
    WR_GetCanvasPixLL(&nvp, &p, configuration.StartLat, configuration.StartLon);
    int xoff = p.x % (int)step, yoff = p.y % (int)step;

    IsoChronList::iterator it = origin.end();
    it--;
    for (double x = r.x + xoff; x < r.x + r.width; x += step) {
      for (double y = r.y + yoff; y < r.y + r.height; y += step) {
        double lat, lon;
        GetCanvasLLPix(&nvp, wxPoint(x, y), &lat, &lon);

        Position p(lat, configuration.positive_longitudes
                            ? positive_degrees(lon)
                            : lon);

        // find the first isochron we are outside of using the isochron from
        // the last point as an initial guess to reduce the amount of expensive
        // Contains calls
        if (!(*it)->Contains(p)) {
          do {
            if (++it == origin.end()) {  // don't plot outside map
              it--;
              goto skip;
            }
          } while (!(*it)->Contains(p));
          it--;
        } else
          for (it--; it != origin.begin(); it--)
            if (!(*it)->Contains(p)) break;

        {
          double W1, VW1, W2, VW2;
          int data_mask1,
              data_mask2;  // can be used to colorize barbs based on data type
          bool v1, v2;
          // now it is the isochron before p, so we find the two closest
          // postions
          Position* p1 = (*it)->ClosestPosition(lat, lon);
          configuration.grib = (*it)->m_Grib;
          configuration.time = (*it)->time;
          configuration.grib_is_data_deficient =
              (*it)->m_Grib_is_data_deficient;
          p.grib_is_data_deficient = configuration.grib_is_data_deficient;
          v1 = p.GetWindData(configuration, W1, VW1, data_mask1);

          it++;
          Position* p2 = (*it)->ClosestPosition(lat, lon);
          configuration.grib = (*it)->m_Grib;
          configuration.time = (*it)->time;
          configuration.grib_is_data_deficient =
              (*it)->m_Grib_is_data_deficient;
          p.grib_is_data_deficient = configuration.grib_is_data_deficient;
          v2 = p.GetWindData(configuration, W2, VW2, data_mask2);
          if (!v1 || !v2) {
            // not valid data
            goto skip;
          }
          // now polar interpolation of the two wind positions
          double d1 = p.Distance(p1), d2 = p.Distance(p2);
          double d = d1 / (d1 + d2);
#if 0
                double W1r = deg2rad(W1), W2r = deg2rad(W2);
                double W1x = VW1*cos(W1r), W1y = VW1*sin(W1r);
                double W2x = VW2*cos(W2r), W2y = VW2*sin(W2r);
                double Wx = d*W1x + (1-d)*W2x, Wy = d*W1y + (1-d)*W2y;
                double W = rad2deg(atan2(Wy, Wx));
#else
          while (W1 - W2 > 180) W1 -= 360;
          while (W2 - W1 > 180) W2 -= 360;
          double W = d * W1 + (1 - d) * W2;
#endif
          double VW = d * VW1 + (1 - d) * VW2;

          g_LineBufferOverlay.pushWindArrowWithBarbs(
              wind_barb_cache, x, y, VW, deg2rad(W) + nvp.rotation, lat < 0);
        }
      skip:;
      }
    }

    Unlock();

    // evaluate performance, and "cheat" by spacing the barbes more in
    // subsequent frames if peformance is inadequate
    long time = timer.Time();
    if (nocache && time > 100 &&
        step < 300)  // 100 milliseconds is unacceptable per frame
      step *= 1.5;
    else if (time < 10 && step > 40)  // reset step
      step /= 1.5;

    wind_barb_cache.Finalize();
  }

  wxColour colour(180, 140, 14);

  wxPoint point;
  WR_GetCanvasPixLL(&vp, &point, configuration.StartLat,
                    configuration.StartLon);

  if (dc.GetDC()) dc.SetPen(wxPen(colour, 2));
#if defined(ocpnUSE_GL) && !defined(__OCPN__ANDROID__)
  else {
    if (!nocache) {
      glPushMatrix();
      glTranslated(point.x, point.y, 0);
      glRotated(vp.rotation * 180 / M_PI, 0, 0, 1);
    }

    glColor3ub(colour.Red(), colour.Green(), colour.Blue());
    //      Enable anti-aliased lines, at best quality
    glEnable(GL_BLEND);
    glLineWidth(2);
    glEnableClientState(GL_VERTEX_ARRAY);
  }
#endif

  if (dc.GetDC()) {
    if (nocache)
      wind_barb_cache.draw(dc.GetDC());
    else {
      LineBuffer tb;
      tb.pushTransformedBuffer(wind_barb_cache, point.x, point.y, vp.rotation);
      tb.Finalize();
      tb.draw(dc.GetDC());
    }
  } else
    wind_barb_cache.draw(nullptr);

#if defined(ocpnUSE_GL) && !defined(__OCPN__ANDROID__)
  if (!dc.GetDC()) {
    glDisableClientState(GL_VERTEX_ARRAY);

    if (!nocache) glPopMatrix();
  }
#endif
}

void RouteMapOverlay::RenderCurrent(piDC& dc, PlugIn_ViewPort& vp) {
  if (origin.size() < 2)  // no map to work with
    return;

  if (vp.bValid == false) return;

  RouteMapConfiguration configuration = GetConfiguration();

  // if zoomed way in, don't cache the arrows for panning, instead we just
  // render what's onscreen
  double latmin, latmax, lonmin, lonmax;
  GetLLBounds(latmin, latmax, lonmin, lonmax);

  PlugIn_ViewPort nvp = vp;
  nvp.clat = configuration.StartLat, nvp.clon = configuration.StartLon;
  nvp.pix_width = nvp.pix_height = 0;
  nvp.rotation = nvp.skew = 0;

  wxPoint p1, p2, p3, p4;
  WR_GetCanvasPixLL(&nvp, &p1, latmin, lonmin);
  WR_GetCanvasPixLL(&nvp, &p2, latmin, lonmax);
  WR_GetCanvasPixLL(&nvp, &p3, latmax, lonmin);
  WR_GetCanvasPixLL(&nvp, &p4, latmax, lonmax);

  wxRect r;
  r.x = wxMin(wxMin(p1.x, p2.x), wxMin(p3.x, p4.x));
  r.y = wxMin(wxMin(p1.y, p2.y), wxMin(p3.y, p4.y));
  r.width = wxMax(wxMax(p1.x, p2.x), wxMax(p3.x, p4.x)) - r.x;
  r.height = wxMax(wxMax(p1.y, p2.y), wxMax(p3.y, p4.y)) - r.y;

  // we could somehow "append" to the cache as passing occurs when zoomed really
  // far in rather than making a complete cache... but how complex does it need
  // to be? quick an dirty, convert to double or integer may overflow
  bool nocache = (double)r.width * (double)r.height >
                     (double)(vp.rv_rect.width * vp.rv_rect.height * 9) ||
                 vp.m_projection_type != PI_PROJECTION_MERCATOR;

  if (nocache || origin.size() != current_cache_origin_size ||
      vp.view_scale_ppm != current_cache_scale ||
      vp.m_projection_type != current_cache_projection) {
    wxStopWatch timer;
    static double step = 80.0;

    current_cache_origin_size = origin.size();
    current_cache_scale = vp.view_scale_ppm;
    current_cache_projection = vp.m_projection_type;

    if (nocache) {
      r = vp.rv_rect;
      nvp = vp;
    }

    Lock();

    wxPoint p;
    WR_GetCanvasPixLL(&nvp, &p, configuration.StartLat, configuration.StartLon);
    int xoff = p.x % (int)step, yoff = p.y % (int)step;

    IsoChronList::iterator it = origin.end();
    it--;
    for (double x = r.x + xoff; x < r.x + r.width; x += step) {
      for (double y = r.y + yoff; y < r.y + r.height; y += step) {
        double lat, lon;
        GetCanvasLLPix(&nvp, wxPoint(x, y), &lat, &lon);

        Position p(lat, configuration.positive_longitudes
                            ? positive_degrees(lon)
                            : lon);

        // find the first isochron we are outside of using the isochron from
        // the last point as an initial guess to reduce the amount of expensive
        // Contains calls
        if (!(*it)->Contains(p)) {
          do {
            if (++it == origin.end()) {  // don't plot outside map
              it--;
              goto skip;
            }
          } while (!(*it)->Contains(p));
          it--;
        } else
          for (it--; it != origin.begin(); it--)
            if (!(*it)->Contains(p)) break;

        {
          double W1, VW1, W2, VW2;
          int data_mask1,
              data_mask2;  // can be used to colorize barbs based on data type

          // now it is the isochron before p, so we find the two closest
          // postions
          Position* p1 = (*it)->ClosestPosition(lat, lon);
          configuration.grib = (*it)->m_Grib;
          configuration.time = (*it)->time;
          configuration.grib_is_data_deficient =
              (*it)->m_Grib_is_data_deficient;
          p.grib_is_data_deficient = configuration.grib_is_data_deficient;
          bool v1, v2;

          v1 = p.GetCurrentData(configuration, W1, VW1, data_mask1);

          it++;
          Position* p2 = (*it)->ClosestPosition(lat, lon);
          configuration.grib = (*it)->m_Grib;
          configuration.time = (*it)->time;
          configuration.grib_is_data_deficient =
              (*it)->m_Grib_is_data_deficient;
          p.grib_is_data_deficient = configuration.grib_is_data_deficient;
          v2 = p.GetCurrentData(configuration, W2, VW2, data_mask2);
          if (!v1 || !v2) {
            goto skip;
          }
#if 0
                // XX climatology angle is to not from
                if ((data_mask1 & Position::CLIMATOLOGY_CURRENT))
                    W1 += 180.0;
                if ((data_mask2 & Position::CLIMATOLOGY_CURRENT))
                    W2 += 180.0;
#endif

          // now polar interpolation of the two wind positions
          double d1 = p.Distance(p1), d2 = p.Distance(p2);
          double d = d1 / (d1 + d2);
#if 0
                double W1r = deg2rad(W1), W2r = deg2rad(W2);
                double W1x = VW1*cos(W1r), W1y = VW1*sin(W1r);
                double W2x = VW2*cos(W2r), W2y = VW2*sin(W2r);
                double Wx = d*W1x + (1-d)*W2x, Wy = d*W1y + (1-d)*W2y;
                double W = rad2deg(atan2(Wy, Wx));
#else
          while (W1 - W2 > 180) W1 -= 360;
          while (W2 - W1 > 180) W2 -= 360;
          double W = d * W1 + (1 - d) * W2;
#endif
          double VW = d * VW1 + (1 - d) * VW2;

          g_LineBufferOverlay.pushSingleArrow(current_cache, x, y, VW,
                                              deg2rad(W + 180) + nvp.rotation,
                                              lat < 0);
        }
      skip:;
      }
    }

    Unlock();

    // evaluate performance, and "cheat" by spacing the barbes more in
    // subsequent frames if peformance is inadequate
    long time = timer.Time();
    if (nocache && time > 100 &&
        step < 600.)  // 100 milliseconds is unacceptable per frame
      step *= 1.5;
    else if (time < 10 && step > 90.)  // reset step
      step /= 1.5;

    current_cache.Finalize();
  }

  wxColour colour(0, 0, 0);

  wxPoint point;
  WR_GetCanvasPixLL(&vp, &point, configuration.StartLat,
                    configuration.StartLon);

  if (dc.GetDC()) dc.SetPen(wxPen(colour, 2));
#if defined(ocpnUSE_GL) && !defined(__OCPN__ANDROID__)
  else {
    if (!nocache) {
      glPushMatrix();
      glTranslated(point.x, point.y, 0);
      glRotated(vp.rotation * 180 / M_PI, 0, 0, 1);
    }

    glColor3ub(colour.Red(), colour.Green(), colour.Blue());
    //      Enable anti-aliased lines, at best quality
    glEnable(GL_BLEND);
    glLineWidth(2);
    glEnableClientState(GL_VERTEX_ARRAY);
  }
#endif

  if (dc.GetDC()) {
    if (nocache)
      current_cache.draw(dc.GetDC());
    else {
      LineBuffer tb;
      tb.pushTransformedBuffer(current_cache, point.x,
                               dc.GetDC()->GetSize().y - point.y, vp.rotation);
      tb.Finalize();
      tb.draw(dc.GetDC());
    }
  } else
    current_cache.draw(nullptr);

#if defined(ocpnUSE_GL) && !defined(__OCPN__ANDROID__)
  if (!dc.GetDC()) {
    glDisableClientState(GL_VERTEX_ARRAY);

    if (!nocache) glPopMatrix();
  }
#endif
}

void RouteMapOverlay::GetLLBounds(double& latmin, double& latmax,
                                  double& lonmin, double& lonmax) {
  latmin = INFINITY, lonmin = INFINITY;
  latmax = -INFINITY, lonmax = -INFINITY;

  if (m_UsesModernNativeResult) {
    for (const ModernIsochroneLayer& layer : m_ModernIsochrones)
      for (const auto& contour : layer.contours)
        for (const auto& point : contour) {
          latmin = wxMin(latmin, point.first);
          latmax = wxMax(latmax, point.first);
          lonmin = wxMin(lonmin, point.second);
          lonmax = wxMax(lonmax, point.second);
        }
    return;
  }
  if (origin.empty()) return;

  IsoChron* last = origin.back();
  for (IsoRouteList::iterator it = last->routes.begin();
       it != last->routes.end(); ++it) {
    Position* pos = (*it)->skippoints->point;
    do {
      latmin = wxMin(latmin, pos->lat);
      latmax = wxMax(latmax, pos->lat);
      lonmin = wxMin(lonmin, pos->lon);
      lonmax = wxMax(lonmax, pos->lon);
      pos = pos->next;
    } while (pos != (*it)->skippoints->point);
  }
}

std::vector<std::pair<double, double>>
RouteMapOverlay::GetClosestFrontierGeometry() {
  std::vector<std::pair<double, double>> geometry;
  RouteMapConfiguration configuration = GetConfiguration();
  if (m_UsesModernNativeResult) {
    double closestDistance = INFINITY;
    for (const ModernIsochroneLayer& layer : m_ModernIsochrones)
      for (const ModernIsochroneLayer::Trace& trace : layer.traces) {
        const double distance =
            DistGreatCircle_Plugin(trace.endpoint.first, trace.endpoint.second,
                                   configuration.EndLat, configuration.EndLon);
        if (distance < closestDistance) {
          closestDistance = distance;
          geometry = trace.route;
        }
      }
    return geometry;
  }
  Position* closest =
      ClosestPosition(configuration.EndLat, configuration.EndLon);
  if (!closest) return geometry;

  std::vector<Position*> chain;
  for (Position* point = closest; point; point = point->parent)
    chain.push_back(point);
  std::reverse(chain.begin(), chain.end());
  geometry.reserve(chain.size());
  for (std::vector<Position*>::const_iterator it = chain.begin();
       it != chain.end(); ++it) {
    if (*it && std::isfinite((*it)->lat) && std::isfinite((*it)->lon))
      geometry.push_back(std::make_pair((*it)->lat, (*it)->lon));
  }
  return geometry;
}

namespace {

void CollectRetainedFrontierSegments(
    IsoRoute* route, std::vector<RouteMapFrontierSegment>* segments) {
  if (!route || !route->skippoints || !route->skippoints->point || !segments)
    return;

  Position* position = route->skippoints->point;
  do {
    Position* parent = dynamic_cast<Position*>(position->parent);
    if (parent && std::isfinite(parent->lat) && std::isfinite(parent->lon) &&
        std::isfinite(position->lat) && std::isfinite(position->lon)) {
      RouteMapFrontierSegment segment = {parent->lat, parent->lon,
                                         position->lat, position->lon};
      segments->push_back(segment);
    }
    position = position->next;
  } while (position && position != route->skippoints->point);

  for (IsoRouteList::iterator child = route->children.begin();
       child != route->children.end(); ++child)
    CollectRetainedFrontierSegments(*child, segments);
}

}  // namespace

std::vector<RouteMapFrontierSegment>
RouteMapOverlay::GetRetainedFrontierSegments() {
  std::vector<RouteMapFrontierSegment> segments;
  Lock();
  if (m_UsesModernNativeResult) {
    for (const ModernIsochroneLayer& layer : m_ModernIsochrones)
      for (const ModernIsochroneLayer::Trace& trace : layer.traces)
        for (std::size_t i = 1; i < trace.route.size(); ++i)
          segments.push_back({trace.route[i - 1].first,
                              trace.route[i - 1].second, trace.route[i].first,
                              trace.route[i].second});
    Unlock();
    return weather_routing_engine::DeduplicateRoutingFootprint(segments);
  }
  for (IsoChronList::iterator layer = origin.begin(); layer != origin.end();
       ++layer) {
    if (!*layer) continue;
    for (IsoRouteList::iterator route = (*layer)->routes.begin();
         route != (*layer)->routes.end(); ++route)
      CollectRetainedFrontierSegments(*route, &segments);
  }
  Unlock();
  return weather_routing_engine::DeduplicateRoutingFootprint(segments);
}

void RouteMapOverlay::RequestGrib(wxDateTime time,
                                  const wxString& requestToken) {
  Json::Value v;
  // This epoch value is authoritative.  Split fields remain for compatibility
  // with GRIB plugins predating the timezone-independent protocol extension.
  v["EpochSeconds"] =
      wxString::Format("%lld", static_cast<long long>(time.GetTicks()))
          .ToStdString();
  v["TimeZone"] = "UTC";
  v["Day"] = time.GetDay();
  v["Month"] = time.GetMonth();
  v["Year"] = time.GetYear();
  v["Hour"] = time.GetHour();
  v["Minute"] = time.GetMinute();
  v["Second"] = time.GetSecond();
  v["WeatherRoutingRequestToken"] =
      static_cast<std::string>(requestToken.ToUTF8());

  Json::FastWriter w;

  // Acknowledge this key before the synchronous plug-in message. Its response
  // can wake another worker which requests the next key; clearing afterwards
  // would erase that new request.
  RequestedGrib();
  SendPluginMessage("GRIB_TIMELINE_RECORD_REQUEST", w.write(v));
}

std::list<PlotData>& RouteMapOverlay::GetPlotData(bool cursor_route) {
  std::list<PlotData>& plotdata =
      cursor_route ? last_cursor_plotdata : last_destination_plotdata;
  if (!cursor_route && clear_destination_plotdata) {
    clear_destination_plotdata = false;
    plotdata.clear();
  }
  if (plotdata.empty()) {
    Position* next =
        cursor_route ? last_cursor_position : last_destination_position;

    if (!next) return plotdata;

    Position* pos = next->parent;

    RouteMapConfiguration configuration = GetConfiguration();
    Lock();
    IsoChronList::iterator it = origin.begin(), itp;
    bool normal_plot_failed = false;

    for (Position* p = pos; p; p = p->parent)
      if (++it == origin.end()) {
        normal_plot_failed = true;
        break;
      }
    if (!normal_plot_failed) {
      it--;

      while (pos) {
        itp = it;
        itp--;

        configuration.grib = (*it)->m_Grib;
        configuration.time = (*it)->time;
        // printf("grib time %p %d\n", configuration.grib, configuration.time);

        configuration.UsedDeltaTime = (*it)->delta;
        PlotData data;

        double dt = configuration.UsedDeltaTime;
        data.time = (*it)->time;

        if (pos->GetPlotData(next, dt, configuration, data))
          plotdata.push_front(data);

        it = itp;
        next = pos;
        pos = pos->parent;
      }
    }

    Unlock();

    if (!cursor_route && plotdata.empty() && Finished() &&
        ReachedDestination() && last_destination_position &&
        last_destination_position->parent) {
      std::vector<Position*> points;
      for (Position* p = last_destination_position->parent; p; p = p->parent)
        points.push_back(p);
      std::reverse(points.begin(), points.end());

      wxDateTime start_time = configuration.StartTime;
      wxDateTime end_time = m_EndTime;
      double total_seconds = 0.0;
      if (start_time.IsValid() && end_time.IsValid() && end_time > start_time)
        total_seconds = (end_time - start_time).GetSeconds().ToDouble();
      const double segment_seconds =
          points.empty() ? 0.0 : total_seconds / (points.size() + 1);

      RoutePoint* fallback_next = last_destination_position;
      for (std::vector<Position*>::reverse_iterator rit = points.rbegin();
           rit != points.rend(); ++rit) {
        Position* p = *rit;
        PlotData data = PlotData();
        const double dt = segment_seconds > 0.0 ? segment_seconds : 0.0;
        data.time = end_time.IsValid()
                        ? end_time - wxTimeSpan::Seconds(
                                         wxRound((plotdata.size() + 1) * dt))
                        : wxDateTime();
        configuration.time = data.time;
        configuration.UsedDeltaTime = dt;

        if (!p->GetPlotData(fallback_next, dt, configuration, data)) {
          data.lat = p->lat;
          data.lon = p->lon;
          data.tacks = p->tacks;
          data.jibes = p->jibes;
          data.sail_plan_changes = p->sail_plan_changes;
          data.polar = p->polar;
          data.delta = dt;
          ll_gc_ll_reverse(p->lat, p->lon, fallback_next->lat,
                           fallback_next->lon, &data.cog, &data.sog);
          if (dt > 0.0)
            data.sog *= 3600.0 / dt;
          else
            data.sog = 0.0;
          data.stw = data.sog;
          data.ctw = data.cog;
        }
        plotdata.push_front(data);
        fallback_next = p;
      }
      wxLogMessage(
          "WR_ROUTE_PLOTDATA_FALLBACK route=\"%s -> %s\" points=%lu "
          "normal_plot_failed=%d",
          configuration.Start, configuration.End,
          static_cast<unsigned long>(plotdata.size()),
          normal_plot_failed ? 1 : 0);
    }
  }
  return plotdata;
}

double RouteMapOverlay::RouteInfo(enum RouteInfoType type, bool cursor_route) {
  std::list<PlotData>& plotdata = GetPlotData(cursor_route);

  double total = 0, count = 0, lat0 = 0, lon0 = 0;
  int comfort = 0, current_comfort = 0;
  for (std::list<PlotData>::iterator it = plotdata.begin();
       it != plotdata.end(); it++) {
    switch (type) {
      case DISTANCE: {
        if (it != plotdata.begin())
          total += DistGreatCircle_Plugin(lat0, lon0, it->lat, it->lon);

        lat0 = it->lat;
        lon0 = it->lon;
      } break;
      case AVGSPEED:
        total += it->stw;
        break;
      case MAXSPEED:
        if (total < it->stw) total = it->stw;
        break;
      case AVGSPEEDGROUND:
        total += it->sog;
        break;
      case MAXSPEEDGROUND:
        if (total < it->sog) total = it->sog;
        break;
      case AVGWIND:
        total += it->twsOverWater;
        break;
      case MAXWIND:
        if (total < it->twsOverWater) total = it->twsOverWater;
        break;
      case MAXWINDGUST:
        if (total < it->VW_GUST) total = it->VW_GUST;
        break;
      case AVGCURRENT:
        total += it->currentSpeed;
        break;
      case MAXCURRENT:
        if (total < it->currentSpeed) total = it->currentSpeed;
        break;
      case AVGSWELL:
        total += it->WVHT;
        break;
      case MAXSWELL:
        if (total < it->WVHT) total = it->WVHT;
        break;
      case PERCENTAGE_UPWIND:
        if (fabs(heading_resolve(it->ctw - it->twdOverWater)) < 90) total++;
        break;
      case PORT_STARBOARD:
        if (heading_resolve(it->ctw - it->twdOverWater) > 0) total++;
        break;
      // CUSTOMIZATION
      // Comfort on route
      case COMFORT:
        current_comfort = sailingConditionLevel(*it);
        if (current_comfort > comfort) comfort = current_comfort;
        break;
      default:
        break;
    }
    count++;
  }

  /* fixup data */
  switch (type) {
    case TACKS:
      return plotdata.size() ? plotdata.back().tacks : 0;
    case JIBES:
      return plotdata.size() ? plotdata.back().jibes : 0;
    case DISTANCE:
      if (total == 0)
        total = NAN;
      else if (Finished()) {
        RouteMapConfiguration configuration = GetConfiguration();
        total += DistGreatCircle_Plugin(lat0, lon0, configuration.EndLat,
                                        configuration.EndLon);
      }
      return total;
    case COMFORT:
      return comfort;
    case PERCENTAGE_UPWIND:
    case PORT_STARBOARD:
      total *= 100.0;
    case AVGSPEED:
    case AVGSPEEDGROUND:
    case AVGWIND:
    case AVGCURRENT:
    case AVGSWELL:
      total /= count;
    default:
      break;
  }
  return total;
}

/* how many cyclone tracks did we cross? which month? */
int RouteMapOverlay::Cyclones(int* months) {
  if (!RouteMap::ClimatologyCycloneTrackCrossings) return -1;

  int days = 30;  // search for 30 day range
  int cyclones = 0;

  Lock();
  if (m_UsesModernNativeResult) {
    std::list<PlotData>::const_iterator data =
        last_destination_plotdata.begin();
    for (std::size_t index = 1; index < m_ModernRoutePositions.size() &&
                                data != last_destination_plotdata.end();
         ++index, ++data) {
      Position* start = m_ModernRoutePositions[index - 1];
      Position* end = m_ModernRoutePositions[index];
      const wxDateTime ptime = data->time;
      std::lock_guard<std::recursive_mutex> invocationLock(
          ClimatologyThreadGuard::InvocationMutex());
      if (RouteMap::ClimatologyCycloneTrackCrossings(
              start->lat, start->lon, end->lat, end->lon, ptime, days)) {
        if (months && ptime.IsValid()) months[ptime.GetMonth()]++;
        cyclones++;
      }
    }
    Unlock();
    return cyclones;
  }
  wxDateTime ptime = m_EndTime;
  IsoChronList::iterator it = origin.end();

  for (Position* p = destination_position; p && p->parent; p = p->parent) {
    std::lock_guard<std::recursive_mutex> invocationLock(
        ClimatologyThreadGuard::InvocationMutex());
    if (RouteMap::ClimatologyCycloneTrackCrossings(
            p->parent->lat, p->parent->lon, p->lat, p->lon, ptime, days)) {
      if (months) months[ptime.GetMonth()]++;
      cyclones++;
    }

    it--;
    ptime = (*it)->time;
  }

  Unlock();
  return cyclones;
}

void RouteMapOverlay::Clear() {
  if (m_UsesModernNativeResult) {
    for (Position* position : m_ModernRoutePositions) delete position;
    for (Position* position : m_ModernCursorRoutePositions) delete position;
    m_ModernRoutePositions.clear();
    m_ModernCursorRoutePositions.clear();
    m_ModernIsochrones.clear();
    m_ModernCursorLayer = std::numeric_limits<std::size_t>::max();
    m_ModernCursorTrace = std::numeric_limits<std::size_t>::max();
    destination_position = nullptr;
    m_UsesModernNativeResult = false;
  }
  RouteMap::Clear();
  last_cursor_position = nullptr;
  last_destination_position = nullptr;
  clear_destination_plotdata = false;
  // clear_cursor_plotdata = false;
  last_cursor_plotdata.clear();
  last_destination_plotdata.clear();
  m_ModernProgressStage.clear();
  m_ModernProgressDetail.clear();
  m_ModernProgressUpdated = false;
  m_UpdateOverlay = true;
}

void RouteMapOverlay::UpdateCursorPosition() {
  // only called in main thread, no race
  Position* last_last_cursor_position = last_cursor_position;
  if (m_UsesModernNativeResult) {
    last_cursor_position = nullptr;
    std::size_t closestLayer = std::numeric_limits<std::size_t>::max();
    std::size_t closestTrace = std::numeric_limits<std::size_t>::max();
    double closestDistance = INFINITY;
    for (std::size_t layer = 0; layer < m_ModernIsochrones.size(); ++layer) {
      for (std::size_t trace = 0;
           trace < m_ModernIsochrones[layer].traces.size(); ++trace) {
        const auto& endpoint = m_ModernIsochrones[layer].traces[trace].endpoint;
        const double dlon = heading_resolve(last_cursor_lon - endpoint.second);
        const double distance = hypot(last_cursor_lat - endpoint.first, dlon);
        if (distance < closestDistance) {
          closestDistance = distance;
          closestLayer = layer;
          closestTrace = trace;
        }
      }
    }
    if (closestLayer != std::numeric_limits<std::size_t>::max() &&
        (closestLayer != m_ModernCursorLayer ||
         closestTrace != m_ModernCursorTrace)) {
      for (Position* position : m_ModernCursorRoutePositions) delete position;
      m_ModernCursorRoutePositions.clear();
      last_cursor_plotdata.clear();
      const ModernIsochroneLayer& layer = m_ModernIsochrones[closestLayer];
      const auto& route = layer.traces[closestTrace].route;
      Position* parent = nullptr;
      const double seconds = route.size() > 1 && layer.time.IsValid()
                                 ? (layer.time - GetConfiguration().StartTime)
                                           .GetSeconds()
                                           .ToDouble() /
                                       static_cast<double>(route.size() - 1)
                                 : 0.0;
      for (std::size_t index = 0; index < route.size(); ++index) {
        Position* position =
            new Position(route[index].first, route[index].second, parent);
        m_ModernCursorRoutePositions.push_back(position);
        if (index + 1 < route.size()) {
          PlotData data{};
          data.lat = route[index].first;
          data.lon = route[index].second;
          data.time = GetConfiguration().StartTime +
                      wxTimeSpan::Seconds(wxRound(index * seconds));
          data.delta = seconds;
          ll_gc_ll_reverse(route[index].first, route[index].second,
                           route[index + 1].first, route[index + 1].second,
                           &data.cog, &data.sog);
          data.sog = seconds > 0.0 ? data.sog * 3600.0 / seconds : 0.0;
          data.stw = data.sog;
          data.ctw = data.cog;
          data.polar = -1;
          last_cursor_plotdata.push_back(data);
        }
        parent = position;
      }
      last_cursor_position = parent;
      m_cursor_time = layer.time;
      m_ModernCursorLayer = closestLayer;
      m_ModernCursorTrace = closestTrace;
    } else if (!m_ModernCursorRoutePositions.empty()) {
      last_cursor_position = m_ModernCursorRoutePositions.back();
      m_cursor_time = m_ModernIsochrones[closestLayer].time;
    }
  } else {
    last_cursor_position =
        ClosestPosition(last_cursor_lat, last_cursor_lon, &m_cursor_time);
  }
  if (last_last_cursor_position != last_cursor_position)
    if (!m_UsesModernNativeResult) last_cursor_plotdata.clear();
}

bool RouteMapOverlay::SetCursorLatLon(double lat, double lon) {
  Position* p = last_cursor_position;
  last_cursor_lat = lat;
  last_cursor_lon = lon;

  UpdateCursorPosition();
  return p != last_cursor_position;
}

bool RouteMapOverlay::Updated() {
  bool updated = m_bUpdated;
  m_bUpdated = false;
  return updated;
}

bool RouteMapOverlay::ValidateDestinationRouteLand(
    RouteMapConfiguration& configuration) {
  if (!configuration.DetectLand) return true;
  wxStopWatch timer;

  Position* child =
      destination_position ? destination_position : last_destination_position;
  if (!child) return true;

  int checked_segments = 0;
  for (Position* parent = dynamic_cast<Position*>(child->parent);
       parent && child && checked_segments < 100000;
       child = parent, parent = dynamic_cast<Position*>(parent->parent)) {
    double bearing = 0.0;
    ll_gc_ll_reverse(parent->lat, parent->lon, child->lat, child->lon, &bearing,
                     NULL);
    wxString failure_reason;
    if (!ConstraintChecker::CheckFinalRouteLandConstraint(
            configuration, parent->lat, parent->lon, child->lat, child->lon,
            bearing, &failure_reason)) {
      configuration.land_crossing = true;
      if (failure_reason.IsEmpty())
        failure_reason = _("Chart land crossing in final route");
      SetFailureReason(failure_reason);
      m_EndTime = wxDateTime();
      SetFinished(false);
      clear_destination_plotdata = true;
      wxLogMessage(
          "FINAL_ROUTE_SAFETY pass=0 route=\"%s -> %s\" "
          "validation_mode=fine_authoritative "
          "persistent_cache_used_in_final_validation=0 "
          "final_validation_forced_fine_masks=1 "
          "segment_index=%d start=(%.8f,%.8f) end=(%.8f,%.8f) "
          "reason=\"%s\"",
          configuration.Start, configuration.End, checked_segments + 1,
          parent->lat, parent->lon, child->lat, child->lon, failure_reason);
      wxLogMessage(
          "WR_UI_TIMING ValidateDestinationRouteLand total_ms=%ld "
          "route=\"%s -> %s\" segments=%d ui_thread=%d pass=0",
          timer.Time(), configuration.Start, configuration.End,
          checked_segments + 1, wxThread::IsMain() ? 1 : 0);
      return false;
    }
    ++checked_segments;
  }

  wxLogMessage(
      "FINAL_ROUTE_SAFETY pass=1 route=\"%s -> %s\" segments=%d "
      "validation_mode=fine_authoritative "
      "persistent_cache_used_in_final_validation=0 "
      "final_validation_forced_fine_masks=1",
      configuration.Start, configuration.End, checked_segments);
  wxLogMessage(
      "WR_UI_TIMING ValidateDestinationRouteLand total_ms=%ld "
      "route=\"%s -> %s\" segments=%d ui_thread=%d pass=1",
      timer.Time(), configuration.Start, configuration.End, checked_segments,
      wxThread::IsMain() ? 1 : 0);
  return true;
}

bool RouteMapOverlay::ValidatePlottedDestinationRouteLand(
    RouteMapConfiguration& configuration) {
  if (!configuration.DetectLand) return true;
  if (!Finished() || !ReachedDestination()) return true;
  wxStopWatch timer;

  std::list<PlotData>& plotdata = GetPlotData(false);
  if (plotdata.empty()) return true;

  int checked_segments = 0;
  double prev_lat = configuration.StartLat;
  double prev_lon = configuration.StartLon;
  bool have_prev = true;
  bool previous_leg_coastal_egress = false;

  for (std::list<PlotData>::const_iterator it = plotdata.begin();
       it != plotdata.end(); ++it) {
    if (have_prev) {
      double bearing = 0.0;
      double dist_nm = 0.0;
      ll_gc_ll_reverse(prev_lat, prev_lon, it->lat, it->lon, &bearing,
                       &dist_nm);
      if (dist_nm < 1e-5) {
        prev_lat = it->lat;
        prev_lon = it->lon;
        previous_leg_coastal_egress = it->coastalDepartureEgress;
        continue;
      }
      wxString failure_reason;
      RouteMapConfiguration segment_configuration = configuration;
      if (m_UsesModernNativeResult && previous_leg_coastal_egress)
        segment_configuration.SafetyMarginLand = 0.0;
      if (!ConstraintChecker::CheckFinalRouteLandConstraint(
              segment_configuration, prev_lat, prev_lon, it->lat, it->lon,
              bearing, &failure_reason)) {
        configuration.land_crossing = true;
        if (failure_reason.IsEmpty())
          failure_reason = _("Chart land crossing in final route");
        SetFailureReason(failure_reason);
        m_EndTime = wxDateTime();
        SetFinished(false);
        clear_destination_plotdata = true;
        wxLogMessage(
            "FINAL_ROUTE_SAFETY plotted_pass=0 route=\"%s -> %s\" "
            "segment_index=%d start=(%.8f,%.8f) end=(%.8f,%.8f) "
            "reason=\"%s\"",
            configuration.Start, configuration.End, checked_segments + 1,
            prev_lat, prev_lon, it->lat, it->lon, failure_reason);
        wxLogMessage(
            "WR_UI_TIMING ValidatePlottedDestinationRouteLand total_ms=%ld "
            "route=\"%s -> %s\" segments=%d plot_points=%lu ui_thread=%d "
            "pass=0",
            timer.Time(), configuration.Start, configuration.End,
            checked_segments + 1, static_cast<unsigned long>(plotdata.size()),
            wxThread::IsMain() ? 1 : 0);
        return false;
      }
      ++checked_segments;
    }
    prev_lat = it->lat;
    prev_lon = it->lon;
    previous_leg_coastal_egress = it->coastalDepartureEgress;
    have_prev = true;
  }

  if (have_prev) {
    double bearing = 0.0;
    double dist_nm = 0.0;
    ll_gc_ll_reverse(prev_lat, prev_lon, configuration.EndLat,
                     configuration.EndLon, &bearing, &dist_nm);
    if (dist_nm < 1e-5) {
      wxLogMessage(
          "FINAL_ROUTE_SAFETY display_validation route=\"%s -> %s\" "
          "segments=%d plot_points=%lu pass=1",
          configuration.Start, configuration.End, checked_segments,
          static_cast<unsigned long>(plotdata.size()));
      wxLogMessage(
          "WR_UI_TIMING ValidatePlottedDestinationRouteLand total_ms=%ld "
          "route=\"%s -> %s\" segments=%d plot_points=%lu ui_thread=%d "
          "pass=1",
          timer.Time(), configuration.Start, configuration.End,
          checked_segments, static_cast<unsigned long>(plotdata.size()),
          wxThread::IsMain() ? 1 : 0);
      return true;
    }
    wxString failure_reason;
    RouteMapConfiguration segment_configuration = configuration;
    if (m_UsesModernNativeResult && previous_leg_coastal_egress)
      segment_configuration.SafetyMarginLand = 0.0;
    if (!ConstraintChecker::CheckFinalRouteLandConstraint(
            segment_configuration, prev_lat, prev_lon, configuration.EndLat,
            configuration.EndLon, bearing, &failure_reason)) {
      configuration.land_crossing = true;
      if (failure_reason.IsEmpty())
        failure_reason = _("Chart land crossing in final route");
      SetFailureReason(failure_reason);
      m_EndTime = wxDateTime();
      SetFinished(false);
      clear_destination_plotdata = true;
      wxLogMessage(
          "FINAL_ROUTE_SAFETY plotted_pass=0 route=\"%s -> %s\" "
          "segment_index=%d start=(%.8f,%.8f) end=(%.8f,%.8f) "
          "reason=\"%s\"",
          configuration.Start, configuration.End, checked_segments + 1,
          prev_lat, prev_lon, configuration.EndLat, configuration.EndLon,
          failure_reason);
      wxLogMessage(
          "WR_UI_TIMING ValidatePlottedDestinationRouteLand total_ms=%ld "
          "route=\"%s -> %s\" segments=%d plot_points=%lu ui_thread=%d "
          "pass=0",
          timer.Time(), configuration.Start, configuration.End,
          checked_segments + 1, static_cast<unsigned long>(plotdata.size()),
          wxThread::IsMain() ? 1 : 0);
      return false;
    }
    ++checked_segments;
  }

  wxLogMessage(
      "FINAL_ROUTE_SAFETY plotted_pass=1 route=\"%s -> %s\" segments=%d",
      configuration.Start, configuration.End, checked_segments);
  wxLogMessage(
      "WR_UI_TIMING ValidatePlottedDestinationRouteLand total_ms=%ld "
      "route=\"%s -> %s\" segments=%d plot_points=%lu ui_thread=%d pass=1",
      timer.Time(), configuration.Start, configuration.End, checked_segments,
      static_cast<unsigned long>(plotdata.size()), wxThread::IsMain() ? 1 : 0);
  return true;
}

namespace {

struct ReverseReachNode {
  double lat;
  double lon;
  wxDateTime time;
  int successor;
  double heading_to_successor;
  int data_mask;
  Position* source_position;

  ReverseReachNode()
      : lat(0.0),
        lon(0.0),
        successor(-1),
        heading_to_successor(NAN),
        data_mask(0),
        source_position(nullptr) {}
};

struct ReverseEtaEstimate {
  bool valid;
  wxDateTime destination_time;
  wxDateTime source_time;
  double source_lat;
  double source_lon;
  double distance_to_destination_nm;
  double estimated_sog;
  int isochron_index_from_end;

  ReverseEtaEstimate()
      : valid(false),
        source_lat(NAN),
        source_lon(NAN),
        distance_to_destination_nm(NAN),
        estimated_sog(NAN),
        isochron_index_from_end(0) {}
};

void CollectIsoRoutePositions(IsoRoute* route, std::vector<Position*>& out) {
  if (!route || !route->skippoints) return;
  Position* p = route->skippoints->point;
  if (p) {
    do {
      out.push_back(p);
      p = p->next;
    } while (p != route->skippoints->point);
  }
  for (IsoRouteList::iterator it = route->children.begin();
       it != route->children.end(); ++it)
    CollectIsoRoutePositions(*it, out);
}

void CollectIsoChronPositions(IsoChron* isochron, std::vector<Position*>& out) {
  if (!isochron) return;
  for (IsoRouteList::iterator it = isochron->routes.begin();
       it != isochron->routes.end(); ++it)
    CollectIsoRoutePositions(*it, out);
}

ReverseEtaEstimate EstimateReverseDestinationTime(
    const IsoChronList& origin, const RouteMapConfiguration& configuration,
    int max_layers) {
  ReverseEtaEstimate best;
  double best_distance = INFINITY;
  int index_from_end = 0;
  for (IsoChronList::const_reverse_iterator rit = origin.rbegin();
       rit != origin.rend() && index_from_end <= max_layers;
       ++rit, ++index_from_end) {
    IsoChron* isochron = *rit;
    if (!isochron || !isochron->time.IsValid() ||
        !configuration.StartTime.IsValid() ||
        isochron->time <= configuration.StartTime)
      continue;

    std::vector<Position*> positions;
    CollectIsoChronPositions(isochron, positions);
    for (Position* position : positions) {
      double distance_to_destination =
          DistGreatCircle(position->lat, position->lon, configuration.EndLat,
                          configuration.EndLon);
      if (distance_to_destination >= best_distance) continue;

      double elapsed_hours =
          (isochron->time - configuration.StartTime).GetSeconds().ToDouble() /
          3600.0;
      if (elapsed_hours <= 0.0) continue;

      double made_good_nm =
          DistGreatCircle(configuration.StartLat, configuration.StartLon,
                          position->lat, position->lon);
      double estimated_sog = made_good_nm / elapsed_hours;
      if (!std::isfinite(estimated_sog) || estimated_sog < 0.5) continue;
      if (estimated_sog > 20.0) estimated_sog = 20.0;

      double remaining_hours = distance_to_destination / estimated_sog;
      long remaining_seconds =
          static_cast<long>(wxRound(remaining_hours * 3600.0));
      if (remaining_seconds < 60) remaining_seconds = 60;

      best.valid = true;
      best.destination_time =
          isochron->time + wxTimeSpan::Seconds(remaining_seconds);
      best.source_time = isochron->time;
      best.source_lat = position->lat;
      best.source_lon = position->lon;
      best.distance_to_destination_nm = distance_to_destination;
      best.estimated_sog = estimated_sog;
      best.isochron_index_from_end = index_from_end;
      best_distance = distance_to_destination;
    }
  }
  return best;
}

}  // namespace

RouteMapOverlay::ReverseSegmentFeasibility RouteMapOverlay::CanSailSegment(
    Position* start, double end_lat, double end_lon, IsoChron* start_isochron,
    const wxDateTime& target_time, RouteMapConfiguration configuration) {
  ReverseSegmentFeasibility result;
  if (!start || !start_isochron || !start_isochron->time.IsValid() ||
      !target_time.IsValid() || target_time <= start_isochron->time) {
    result.failure_reason = _("invalid reverse segment time");
    return result;
  }

  wxTimeSpan available_span = target_time - start_isochron->time;
  double available_seconds = available_span.GetSeconds().ToDouble();
  if (available_seconds <= 0) {
    result.failure_reason = _("invalid reverse segment interval");
    return result;
  }

  configuration.time = start_isochron->time;
  configuration.UsedDeltaTime = available_seconds;
  configuration.grib = start_isochron->m_Grib;
  configuration.grib_is_data_deficient =
      start_isochron->m_Grib_is_data_deficient;
  configuration.EndLat = end_lat;
  configuration.EndLon = end_lon;

  Position probe(start->lat, start->lon, nullptr, start->parent_heading,
                 start->parent_bearing, start->polar, start->tacks,
                 start->jibes, start->sail_plan_changes, start->data_mask,
                 start->grib_is_data_deficient);
  double heading = NAN;
  int data_mask = start->data_mask;
  double dt = probe.PropagateToPoint(end_lat, end_lon, configuration, heading,
                                     data_mask, true);
  bool endpoint_bridge = DistGreatCircle(end_lat, end_lon, configuration.EndLat,
                                         configuration.EndLon) < 0.01;
  if ((std::isnan(dt) || dt > available_seconds) &&
      configuration.land_crossing && endpoint_bridge &&
      configuration.SafetyMarginLand > 0.0) {
    RouteMapConfiguration relaxed_configuration = configuration;
    relaxed_configuration.SafetyMarginLand = 0.0;
    relaxed_configuration.land_crossing = false;
    Position relaxed_probe(start->lat, start->lon, nullptr,
                           start->parent_heading, start->parent_bearing,
                           start->polar, start->tacks, start->jibes,
                           start->sail_plan_changes, start->data_mask,
                           start->grib_is_data_deficient);
    double relaxed_heading = NAN;
    int relaxed_data_mask = start->data_mask;
    double relaxed_dt = relaxed_probe.PropagateToPoint(
        end_lat, end_lon, relaxed_configuration, relaxed_heading,
        relaxed_data_mask, true);
    if (!std::isnan(relaxed_dt) && relaxed_dt <= available_seconds) {
      dt = relaxed_dt;
      heading = relaxed_heading;
      data_mask = relaxed_data_mask;
      wxLogMessage(
          "WR_REVERSE_REACHABILITY_ENDPOINT_MARGIN_RELAXED route=\"%s -> %s\" "
          "segment=(%.8f,%.8f)->(%.8f,%.8f) margin_nm=%.3f dt=%.1f "
          "available=%.1f",
          configuration.Start, configuration.End, start->lat, start->lon,
          end_lat, end_lon, configuration.SafetyMarginLand, dt,
          available_seconds);
    }
  }
  if (std::isnan(dt) || dt > available_seconds) {
    result.failure_reason = configuration.land_crossing
                                ? _("chart safety")
                                : _("weather/current/polar");
    return result;
  }

  result.feasible = true;
  result.dt = dt;
  result.heading = heading;
  result.data_mask = data_mask;
  return result;
}

bool RouteMapOverlay::TryReverseReachabilityRecovery(
    RouteMapConfiguration& configuration, int isochrons_considered) {
  wxStopWatch timer;
  configuration.ReverseRecoveryUsed = true;
  configuration.ReverseRecoveryStatus = _("started");
  configuration.ReverseFailureReason.Clear();
  configuration.ReverseLayersBuilt = 0;
  configuration.ReverseNodesGenerated = 0;
  configuration.ReverseNodesFeasible = 0;
  configuration.ReverseConnectionFound = false;
  configuration.ReverseConnectionTime = wxDateTime();
  configuration.ReverseFinalValidationPass = false;
  {
    Lock();
    m_reverseReachabilityDebugPoints.clear();
    Unlock();
  }

  if (origin.size() < 2) {
    configuration.ReverseRecoveryStatus = _("failed");
    configuration.ReverseFailureReason = _("not enough forward isochrones");
    wxLogMessage(
        "WR_REVERSE_REACHABILITY_RESULT route=\"%s -> %s\" status=failed "
        "reason=\"%s\" layers=0 nodes_generated=0 nodes_feasible=0 "
        "elapsed_ms=%ld",
        configuration.Start, configuration.End,
        configuration.ReverseFailureReason, timer.Time());
    return false;
  }

  int requested_layers = configuration.ReverseReachabilitySearchBackIsochrones;
  if (requested_layers <= 0) requested_layers = 6;
  const int max_layers = wxMin(12, requested_layers);
  const int max_positions_per_isochron = 360;
  const int max_nodes_per_layer = 96;
  double horizon_hours = configuration.ReverseReachabilityHorizonHours;
  wxDateTime destination_time = origin.back()->time;
  if (!destination_time.IsValid()) destination_time = configuration.time;
  ReverseEtaEstimate eta_estimate =
      EstimateReverseDestinationTime(origin, configuration, max_layers);
  if (eta_estimate.valid && eta_estimate.destination_time.IsValid() &&
      (!destination_time.IsValid() ||
       eta_estimate.destination_time > destination_time)) {
    destination_time = eta_estimate.destination_time;
    wxLogMessage(
        "WR_REVERSE_REACHABILITY_ETA route=\"%s -> %s\" "
        "closest_frontier=(%.8f,%.8f) frontier_time=\"%s\" "
        "distance_to_destination_nm=%.3f estimated_sog=%.3f "
        "estimated_destination_time=\"%s\" isochron_index_from_end=%d",
        configuration.Start, configuration.End, eta_estimate.source_lat,
        eta_estimate.source_lon, eta_estimate.source_time.FormatISOCombined(),
        eta_estimate.distance_to_destination_nm, eta_estimate.estimated_sog,
        eta_estimate.destination_time.FormatISOCombined(),
        eta_estimate.isochron_index_from_end);
  }

  wxLogMessage(
      "WR_REVERSE_REACHABILITY_START route=\"%s -> %s\" destination=(%.8f,"
      "%.8f) layers_requested=%d max_layers=%d horizon_hours=%.2f "
      "isochrons_considered=%d destination_time=\"%s\" eta_estimate=%d",
      configuration.Start, configuration.End, configuration.EndLat,
      configuration.EndLon, requested_layers, max_layers, horizon_hours,
      isochrons_considered,
      destination_time.IsValid() ? destination_time.FormatISOCombined()
                                 : wxString("invalid"),
      eta_estimate.valid ? 1 : 0);

  const wxDateTime initial_destination_time = destination_time;
  int destination_time_attempt = 0;
  const int destination_time_slack_minutes[] = {15, 30, 60};

retry_reverse_destination_time:
  configuration.ReverseLayersBuilt = 0;
  configuration.ReverseNodesGenerated = 0;
  configuration.ReverseNodesFeasible = 0;

  std::vector<ReverseReachNode> nodes;
  std::vector<ReverseReachabilityDebugPoint> debug_points;
  nodes.reserve(1 + max_layers * max_nodes_per_layer);
  ReverseReachNode destination;
  destination.lat = configuration.EndLat;
  destination.lon = configuration.EndLon;
  destination.time = destination_time;
  nodes.push_back(destination);
  debug_points.push_back(
      ReverseReachabilityDebugPoint(destination.lat, destination.lon, 0, true));

  std::vector<int> later_layer;
  later_layer.push_back(0);
  std::vector<int> connection_candidates;
  long safety_rejections = 0;
  long weather_rejections = 0;

  IsoChronList::reverse_iterator rit = origin.rbegin();
  if (rit != origin.rend() &&
      (!destination_time.IsValid() || !(destination_time > (*rit)->time)))
    ++rit;  // skip containing/final isochrone unless ETA window extends beyond
            // it
  for (int layer = 1; rit != origin.rend() && layer <= max_layers;
       ++rit, ++layer) {
    IsoChron* isochron = *rit;
    if (!isochron || !isochron->time.IsValid()) continue;
    if (horizon_hours > 0.0 &&
        (destination_time - isochron->time).GetSeconds().ToDouble() >
            horizon_hours * 3600.0)
      break;

    std::vector<Position*> positions;
    CollectIsoChronPositions(isochron, positions);
    std::sort(positions.begin(), positions.end(),
              [&](Position* a, Position* b) {
                return DistGreatCircle(a->lat, a->lon, configuration.EndLat,
                                       configuration.EndLon) <
                       DistGreatCircle(b->lat, b->lon, configuration.EndLat,
                                       configuration.EndLon);
              });
    if (static_cast<int>(positions.size()) > max_positions_per_isochron)
      positions.resize(max_positions_per_isochron);

    std::vector<int> this_layer;
    long generated_before = configuration.ReverseNodesGenerated;
    long feasible_before = configuration.ReverseNodesFeasible;
    long safety_before = safety_rejections;
    long weather_before = weather_rejections;

    for (Position* position : positions) {
      if (static_cast<int>(this_layer.size()) >= max_nodes_per_layer) break;
      for (int successor : later_layer) {
        configuration.ReverseNodesGenerated++;
        ReverseSegmentFeasibility feasibility =
            CanSailSegment(position, nodes[successor].lat, nodes[successor].lon,
                           isochron, nodes[successor].time, configuration);
        if (!feasibility.feasible) {
          if (feasibility.failure_reason == _("chart safety"))
            ++safety_rejections;
          else
            ++weather_rejections;
          continue;
        }

        ReverseReachNode node;
        node.lat = position->lat;
        node.lon = position->lon;
        node.time = isochron->time;
        node.successor = successor;
        node.heading_to_successor = feasibility.heading;
        node.data_mask = feasibility.data_mask;
        node.source_position = position;
        nodes.push_back(node);
        int node_index = static_cast<int>(nodes.size()) - 1;
        this_layer.push_back(node_index);
        connection_candidates.push_back(node_index);
        debug_points.push_back(
            ReverseReachabilityDebugPoint(node.lat, node.lon, layer, false));
        configuration.ReverseNodesFeasible++;
        break;
      }
    }

    if (this_layer.empty()) {
      wxLogMessage(
          "WR_REVERSE_REACHABILITY_LAYER route=\"%s -> %s\" layer=%d "
          "time=\"%s\" positions=%lu generated=%ld feasible=%ld status=empty",
          configuration.Start, configuration.End, layer,
          isochron->time.FormatISOCombined(),
          static_cast<unsigned long>(positions.size()),
          configuration.ReverseNodesGenerated - generated_before,
          configuration.ReverseNodesFeasible - feasible_before);
      break;
    }

    configuration.ReverseLayersBuilt++;
    later_layer.swap(this_layer);
    wxLogMessage(
        "WR_REVERSE_REACHABILITY_LAYER route=\"%s -> %s\" layer=%d "
        "time=\"%s\" positions=%lu generated=%ld feasible=%ld retained=%lu "
        "safety_rejections=%ld weather_rejections=%ld",
        configuration.Start, configuration.End, layer,
        isochron->time.FormatISOCombined(),
        static_cast<unsigned long>(positions.size()),
        configuration.ReverseNodesGenerated - generated_before,
        configuration.ReverseNodesFeasible - feasible_before,
        static_cast<unsigned long>(later_layer.size()),
        safety_rejections - safety_before, weather_rejections - weather_before);
  }

  if (connection_candidates.empty()) {
    if (destination_time_attempt <
        static_cast<int>(sizeof(destination_time_slack_minutes) /
                         sizeof(destination_time_slack_minutes[0]))) {
      destination_time =
          initial_destination_time +
          wxTimeSpan::Minutes(
              destination_time_slack_minutes[destination_time_attempt]);
      ++destination_time_attempt;
      wxLogMessage(
          "WR_REVERSE_REACHABILITY_RETRY_TIME route=\"%s -> %s\" "
          "attempt=%d destination_time=\"%s\" slack_minutes=%d",
          configuration.Start, configuration.End, destination_time_attempt,
          destination_time.FormatISOCombined(),
          destination_time_slack_minutes[destination_time_attempt - 1]);
      goto retry_reverse_destination_time;
    }
    {
      Lock();
      m_reverseReachabilityDebugPoints.swap(debug_points);
      Unlock();
    }
    configuration.ReverseRecoveryStatus = _("failed");
    configuration.ReverseFailureReason =
        configuration.ReverseNodesGenerated > 0
            ? _("No destination-reachable reverse corridor found")
            : _("No reverse reachability candidates generated");
    wxLogMessage(
        "WR_REVERSE_REACHABILITY_RESULT route=\"%s -> %s\" status=failed "
        "reason=\"%s\" layers=%ld nodes_generated=%ld nodes_feasible=%ld "
        "safety_rejections=%ld weather_rejections=%ld elapsed_ms=%ld",
        configuration.Start, configuration.End,
        configuration.ReverseFailureReason, configuration.ReverseLayersBuilt,
        configuration.ReverseNodesGenerated, configuration.ReverseNodesFeasible,
        safety_rejections, weather_rejections, timer.Time());
    return false;
  }

  std::sort(connection_candidates.begin(), connection_candidates.end(),
            [&](int a, int b) {
              const ReverseReachNode& na = nodes[a];
              const ReverseReachNode& nb = nodes[b];
              if (na.time.IsValid() && nb.time.IsValid() && na.time != nb.time)
                return na.time > nb.time;
              double da = DistGreatCircle(na.lat, na.lon, configuration.EndLat,
                                          configuration.EndLon);
              double db = DistGreatCircle(nb.lat, nb.lon, configuration.EndLat,
                                          configuration.EndLon);
              return da < db;
            });

  connection_candidates.erase(
      std::unique(connection_candidates.begin(), connection_candidates.end()),
      connection_candidates.end());

  int bridge_candidates_tested = 0;
  int bridge_candidates_invalid = 0;
  int bridge_candidates_rejected = 0;
  int accepted_connection = -1;
  std::vector<int> accepted_chain;
  wxString last_validation_failure;

  for (int candidate_node : connection_candidates) {
    std::vector<int> chain;
    for (int node = candidate_node; node >= 0; node = nodes[node].successor) {
      chain.push_back(node);
      if (nodes[node].successor < 0) break;
    }
    if (chain.size() < 2 || !nodes[candidate_node].source_position) {
      ++bridge_candidates_invalid;
      continue;
    }

    delete destination_position;
    destination_position = nullptr;
    Position* parent = nodes[candidate_node].source_position;
    for (size_t i = 0; i + 1 < chain.size(); ++i) {
      const ReverseReachNode& edge = nodes[chain[i]];
      const ReverseReachNode& target = nodes[chain[i + 1]];
      Position* next = new Position(
          target.lat, target.lon, parent, edge.heading_to_successor, NAN,
          parent ? parent->polar : -1, parent ? parent->tacks : 0,
          parent ? parent->jibes : 0, parent ? parent->sail_plan_changes : 0,
          edge.data_mask, parent ? parent->grib_is_data_deficient : false);
      parent = next;
    }

    RouteMapConfiguration validation_configuration = configuration;
    destination_position = parent;
    last_destination_position = destination_position;
    m_EndTime = nodes[0].time;
    clear_destination_plotdata = true;
    SetFinished(true);
    validation_configuration.ReverseConnectionFound = true;
    validation_configuration.ReverseConnectionTime = nodes[candidate_node].time;
    ++bridge_candidates_tested;

    wxLogMessage(
        "WR_REVERSE_REACHABILITY_BRIDGE route=\"%s -> %s\" "
        "candidate=%d/%lu connection=(%.8f,%.8f) connection_time=\"%s\" "
        "chain_nodes=%lu",
        configuration.Start, configuration.End, bridge_candidates_tested,
        static_cast<unsigned long>(connection_candidates.size()),
        nodes[candidate_node].lat, nodes[candidate_node].lon,
        nodes[candidate_node].time.FormatISOCombined(),
        static_cast<unsigned long>(chain.size()));

    if (ValidateDestinationRouteLand(validation_configuration) &&
        ValidatePlottedDestinationRouteLand(validation_configuration)) {
      configuration = validation_configuration;
      accepted_connection = candidate_node;
      accepted_chain = chain;
      break;
    }

    last_validation_failure = GetFailureReason();
    ++bridge_candidates_rejected;
    delete destination_position;
    destination_position = nullptr;
    last_destination_position =
        ClosestPosition(configuration.EndLat, configuration.EndLon);
    m_EndTime = wxDateTime();
    SetFinished(false);
  }

  if (accepted_connection >= 0) {
    for (int node : accepted_chain) {
      if (node >= 0 && node < static_cast<int>(nodes.size()))
        debug_points.push_back(ReverseReachabilityDebugPoint(
            nodes[node].lat, nodes[node].lon, 0, true));
    }
    {
      Lock();
      m_reverseReachabilityDebugPoints.swap(debug_points);
      Unlock();
    }
    configuration.ReverseRecoveryStatus = _("complete");
    configuration.ReverseFinalValidationPass = true;
    configuration.ReverseConnectionFound = true;
    configuration.ReverseConnectionTime = nodes[accepted_connection].time;
    wxLogMessage(
        "WR_REVERSE_REACHABILITY_RESULT route=\"%s -> %s\" status=complete "
        "layers=%ld nodes_generated=%ld nodes_feasible=%ld "
        "bridge_candidates=%lu tested=%d rejected=%d invalid=%d "
        "accepted_connection=(%.8f,%.8f) accepted_connection_time=\"%s\" "
        "final_validation=pass safety_rejections=%ld "
        "weather_rejections=%ld elapsed_ms=%ld",
        configuration.Start, configuration.End,
        configuration.ReverseLayersBuilt, configuration.ReverseNodesGenerated,
        configuration.ReverseNodesFeasible,
        static_cast<unsigned long>(connection_candidates.size()),
        bridge_candidates_tested, bridge_candidates_rejected,
        bridge_candidates_invalid, nodes[accepted_connection].lat,
        nodes[accepted_connection].lon,
        nodes[accepted_connection].time.FormatISOCombined(), safety_rejections,
        weather_rejections, timer.Time());
    return true;
  }

  {
    Lock();
    m_reverseReachabilityDebugPoints.swap(debug_points);
    Unlock();
  }
  configuration.ReverseRecoveryStatus = _("failed");
  configuration.ReverseFailureReason =
      last_validation_failure.IsEmpty()
          ? _("No chart-safe reverse bridge found")
          : last_validation_failure;
  configuration.ReverseFinalValidationPass = false;
  delete destination_position;
  destination_position = nullptr;
  last_destination_position =
      ClosestPosition(configuration.EndLat, configuration.EndLon);
  m_EndTime = wxDateTime();
  SetFinished(false);
  wxLogMessage(
      "WR_REVERSE_REACHABILITY_RESULT route=\"%s -> %s\" status=failed "
      "reason=\"%s\" layers=%ld nodes_generated=%ld nodes_feasible=%ld "
      "bridge_candidates=%lu tested=%d rejected=%d invalid=%d "
      "safety_rejections=%ld weather_rejections=%ld final_validation=fail "
      "elapsed_ms=%ld",
      configuration.Start, configuration.End,
      configuration.ReverseFailureReason, configuration.ReverseLayersBuilt,
      configuration.ReverseNodesGenerated, configuration.ReverseNodesFeasible,
      static_cast<unsigned long>(connection_candidates.size()),
      bridge_candidates_tested, bridge_candidates_rejected,
      bridge_candidates_invalid, safety_rejections, weather_rejections,
      timer.Time());
  return false;
}

bool RouteMapOverlay::AnalyzeReverseReachabilityForFrontierCollapse(
    const wxString& trigger) {
  wxStopWatch timer;
  RouteMapConfiguration configuration = GetConfiguration();
  if (!configuration.UseReverseReachabilityRecovery) return false;
  if (ReachedDestination()) return false;

  configuration.ReverseRecoveryUsed = false;
  configuration.ReverseRecoveryStatus = _("frontier collapse diagnostic");
  configuration.ReverseFailureReason.Clear();
  configuration.ReverseLayersBuilt = 0;
  configuration.ReverseNodesGenerated = 0;
  configuration.ReverseNodesFeasible = 0;
  configuration.ReverseConnectionFound = false;
  configuration.ReverseConnectionTime = wxDateTime();
  configuration.ReverseFinalValidationPass = false;
  {
    Lock();
    m_reverseReachabilityDebugPoints.clear();
    Unlock();
  }

  if (origin.empty()) {
    configuration.ReverseRecoveryStatus = _("frontier diagnostic failed");
    configuration.ReverseFailureReason = _("no forward isochrones available");
    SetConfigurationPreserveResult(configuration);
    wxLogMessage(
        "WR_REVERSE_FRONTIER_COLLAPSE_RESULT route=\"%s -> %s\" "
        "trigger=\"%s\" status=failed reason=\"%s\" layers=0 "
        "nodes_generated=0 nodes_feasible=0 elapsed_ms=%ld",
        configuration.Start, configuration.End, trigger,
        configuration.ReverseFailureReason, timer.Time());
    return false;
  }

  int requested_layers = configuration.ReverseReachabilitySearchBackIsochrones;
  if (requested_layers <= 0) requested_layers = 6;
  const int max_layers = wxMin(12, requested_layers);
  const int max_positions_per_isochron = 360;
  const int max_nodes_per_layer = 96;
  double horizon_hours = configuration.ReverseReachabilityHorizonHours;

  wxDateTime destination_time = configuration.time;
  if (!destination_time.IsValid() && !origin.empty())
    destination_time = origin.back()->time;
  ReverseEtaEstimate eta_estimate =
      EstimateReverseDestinationTime(origin, configuration, max_layers);
  if (eta_estimate.valid && eta_estimate.destination_time.IsValid() &&
      (!destination_time.IsValid() ||
       eta_estimate.destination_time > destination_time)) {
    destination_time = eta_estimate.destination_time;
  }
  if (!destination_time.IsValid()) {
    configuration.ReverseRecoveryStatus = _("frontier diagnostic failed");
    configuration.ReverseFailureReason = _("no valid destination ETA window");
    SetConfigurationPreserveResult(configuration);
    wxLogMessage(
        "WR_REVERSE_FRONTIER_COLLAPSE_RESULT route=\"%s -> %s\" "
        "trigger=\"%s\" status=failed reason=\"%s\" layers=0 "
        "nodes_generated=0 nodes_feasible=0 elapsed_ms=%ld",
        configuration.Start, configuration.End, trigger,
        configuration.ReverseFailureReason, timer.Time());
    return false;
  }

  wxString failure_reason = GetFailureReason();
  wxLogMessage(
      "WR_REVERSE_FRONTIER_COLLAPSE_START route=\"%s -> %s\" "
      "trigger=\"%s\" failure_reason=\"%s\" destination=(%.8f,%.8f) "
      "destination_time=\"%s\" eta_estimate=%d closest_frontier=(%.8f,%.8f) "
      "closest_distance_nm=%.3f estimated_sog=%.3f layers_requested=%d "
      "max_layers=%d horizon_hours=%.2f origin_size=%lu",
      configuration.Start, configuration.End, trigger, failure_reason,
      configuration.EndLat, configuration.EndLon,
      destination_time.FormatISOCombined(), eta_estimate.valid ? 1 : 0,
      eta_estimate.valid ? eta_estimate.source_lat : NAN,
      eta_estimate.valid ? eta_estimate.source_lon : NAN,
      eta_estimate.valid ? eta_estimate.distance_to_destination_nm : NAN,
      eta_estimate.valid ? eta_estimate.estimated_sog : NAN, requested_layers,
      max_layers, horizon_hours, static_cast<unsigned long>(origin.size()));

  std::vector<ReverseReachNode> nodes;
  std::vector<ReverseReachabilityDebugPoint> debug_points;
  nodes.reserve(1 + max_layers * max_nodes_per_layer);
  ReverseReachNode destination;
  destination.lat = configuration.EndLat;
  destination.lon = configuration.EndLon;
  destination.time = destination_time;
  nodes.push_back(destination);
  debug_points.push_back(
      ReverseReachabilityDebugPoint(destination.lat, destination.lon, 0, true));

  std::vector<int> later_layer;
  later_layer.push_back(0);
  int best_connection = -1;
  long safety_rejections = 0;
  long weather_rejections = 0;

  int layer = 1;
  for (IsoChronList::reverse_iterator rit = origin.rbegin();
       rit != origin.rend() && layer <= max_layers; ++rit, ++layer) {
    IsoChron* isochron = *rit;
    if (!isochron || !isochron->time.IsValid()) continue;
    if (isochron->time >= destination_time) continue;
    if (horizon_hours > 0.0 &&
        (destination_time - isochron->time).GetSeconds().ToDouble() >
            horizon_hours * 3600.0)
      break;

    std::vector<Position*> positions;
    CollectIsoChronPositions(isochron, positions);
    std::sort(positions.begin(), positions.end(),
              [&](Position* a, Position* b) {
                return DistGreatCircle(a->lat, a->lon, configuration.EndLat,
                                       configuration.EndLon) <
                       DistGreatCircle(b->lat, b->lon, configuration.EndLat,
                                       configuration.EndLon);
              });
    size_t original_position_count = positions.size();
    if (static_cast<int>(positions.size()) > max_positions_per_isochron)
      positions.resize(max_positions_per_isochron);

    std::vector<int> this_layer;
    long generated_before = configuration.ReverseNodesGenerated;
    long feasible_before = configuration.ReverseNodesFeasible;
    long safety_before = safety_rejections;
    long weather_before = weather_rejections;

    for (Position* position : positions) {
      if (static_cast<int>(this_layer.size()) >= max_nodes_per_layer) break;
      for (int successor : later_layer) {
        configuration.ReverseNodesGenerated++;
        ReverseSegmentFeasibility feasibility =
            CanSailSegment(position, nodes[successor].lat, nodes[successor].lon,
                           isochron, nodes[successor].time, configuration);
        if (!feasibility.feasible) {
          if (feasibility.failure_reason == _("chart safety"))
            ++safety_rejections;
          else
            ++weather_rejections;
          continue;
        }

        ReverseReachNode node;
        node.lat = position->lat;
        node.lon = position->lon;
        node.time = isochron->time;
        node.successor = successor;
        node.heading_to_successor = feasibility.heading;
        node.data_mask = feasibility.data_mask;
        node.source_position = position;
        nodes.push_back(node);
        int node_index = static_cast<int>(nodes.size()) - 1;
        this_layer.push_back(node_index);
        debug_points.push_back(
            ReverseReachabilityDebugPoint(node.lat, node.lon, layer, false));
        configuration.ReverseNodesFeasible++;
        break;
      }
    }

    wxLogMessage(
        "WR_REVERSE_FRONTIER_COLLAPSE_LAYER route=\"%s -> %s\" "
        "layer=%d time=\"%s\" positions=%lu positions_checked=%lu "
        "successors=%lu generated=%ld feasible=%ld retained=%lu "
        "safety_rejections=%ld weather_rejections=%ld",
        configuration.Start, configuration.End, layer,
        isochron->time.FormatISOCombined(),
        static_cast<unsigned long>(original_position_count),
        static_cast<unsigned long>(positions.size()),
        static_cast<unsigned long>(later_layer.size()),
        configuration.ReverseNodesGenerated - generated_before,
        configuration.ReverseNodesFeasible - feasible_before,
        static_cast<unsigned long>(this_layer.size()),
        safety_rejections - safety_before, weather_rejections - weather_before);

    if (this_layer.empty()) break;
    configuration.ReverseLayersBuilt++;
    best_connection = this_layer.front();
    later_layer.swap(this_layer);
  }

  if (best_connection >= 0) {
    configuration.ReverseConnectionFound = true;
    configuration.ReverseConnectionTime = nodes[best_connection].time;
    configuration.ReverseRecoveryStatus =
        _("frontier diagnostic found destination-reachable corridor");
    for (int node = best_connection; node >= 0; node = nodes[node].successor) {
      debug_points.push_back(ReverseReachabilityDebugPoint(
          nodes[node].lat, nodes[node].lon, 0, true));
      if (nodes[node].successor < 0) break;
    }
    wxLogMessage(
        "WR_REVERSE_FRONTIER_COLLAPSE_RESULT route=\"%s -> %s\" "
        "trigger=\"%s\" status=connection_found connection=(%.8f,%.8f) "
        "connection_time=\"%s\" layers=%ld nodes_generated=%ld "
        "nodes_feasible=%ld safety_rejections=%ld weather_rejections=%ld "
        "elapsed_ms=%ld note=\"diagnostic-only, route state unchanged\"",
        configuration.Start, configuration.End, trigger,
        nodes[best_connection].lat, nodes[best_connection].lon,
        nodes[best_connection].time.FormatISOCombined(),
        configuration.ReverseLayersBuilt, configuration.ReverseNodesGenerated,
        configuration.ReverseNodesFeasible, safety_rejections,
        weather_rejections, timer.Time());
  } else {
    configuration.ReverseConnectionFound = false;
    configuration.ReverseRecoveryStatus = _("frontier diagnostic failed");
    configuration.ReverseFailureReason =
        configuration.ReverseNodesGenerated > 0
            ? _("No destination-reachable reverse corridor found")
            : _("No reverse reachability candidates generated");
    wxLogMessage(
        "WR_REVERSE_FRONTIER_COLLAPSE_RESULT route=\"%s -> %s\" "
        "trigger=\"%s\" status=no_connection reason=\"%s\" layers=%ld "
        "nodes_generated=%ld nodes_feasible=%ld safety_rejections=%ld "
        "weather_rejections=%ld elapsed_ms=%ld",
        configuration.Start, configuration.End, trigger,
        configuration.ReverseFailureReason, configuration.ReverseLayersBuilt,
        configuration.ReverseNodesGenerated, configuration.ReverseNodesFeasible,
        safety_rejections, weather_rejections, timer.Time());
  }

  {
    Lock();
    m_reverseReachabilityDebugPoints.swap(debug_points);
    Unlock();
  }
  SetConfigurationPreserveResult(configuration);
  return configuration.ReverseConnectionFound;
}

void RouteMapOverlay::UpdateDestination() {
  RouteMapConfiguration configuration = GetConfiguration();
  const bool defer_chart_validation_to_main =
      configuration.DetectLand &&
      ConstraintChecker::IsExperimentalChartSafetyEnforced() &&
      !wxThread::IsMain();
  Position* last_last_destination_position = last_destination_position;
  bool done = ReachedDestination();
  if (done) {
    Lock();
    delete destination_position;
    destination_position = 0;
    std::vector<IsoRouteDestinationCandidate> destination_candidates;
    int isochrons_considered = 0;
    const int max_destination_isochrons = 8;

    /* This doesn't happen often, so it can afford to be slower.  The
       historical path only tried the second-from-last isochrone.  With chart
       safety enabled that can fail when the fastest frontier approaches the
       destination from the wrong side of land, even though a slightly earlier
       or slower frontier has a safe final approach.  Keep this bounded and
       validate alternatives in absolute ETA order below. */
    if (origin.size() >= 2) {
      IsoChronList::reverse_iterator rit = origin.rbegin();
      ++rit; /* skip the final isochrone which already crossed the target */
      for (; rit != origin.rend() &&
             isochrons_considered < max_destination_isochrons;
           ++rit, ++isochrons_considered) {
        IsoChron* isochron = *rit;
        if (!isochron) continue;
        for (IsoRouteList::iterator it = isochron->routes.begin();
             it != isochron->routes.end(); ++it) {
          configuration.grib = isochron->m_Grib;
          configuration.grib_is_data_deficient =
              isochron->m_Grib_is_data_deficient;

          configuration.time = isochron->time;
          configuration.UsedDeltaTime = isochron->delta;
          (*it)->CollectDestinationCandidates(configuration,
                                              destination_candidates);
        }
      }
    }
    Unlock();

    std::sort(destination_candidates.begin(), destination_candidates.end(),
              [](const IsoRouteDestinationCandidate& a,
                 const IsoRouteDestinationCandidate& b) {
                return a.absolute_dt < b.absolute_dt;
              });

    int alternatives_validated = 0;
    int alternatives_rejected_by_chart = 0;
    bool accepted_destination_candidate = false;
    double fastest_rejected_dt = NAN;
    double accepted_dt = NAN;
    double accepted_absolute_dt = NAN;

    if (destination_candidates.empty()) {
      // destination is between two isochrons
      // but propagate can't reach it (land or boundaries in the way).
      bool recovered =
          configuration.UseReverseReachabilityRecovery &&
          TryReverseReachabilityRecovery(configuration, isochrons_considered);
      if (recovered) {
        SetFailureReason(wxEmptyString);
      } else {
        m_EndTime = wxDateTime();
        last_destination_position =
            ClosestPosition(configuration.EndLat, configuration.EndLon);
        configuration.land_crossing = true;
        wxString reason = _("Final route did not reach destination");
        if (configuration.UseReverseReachabilityRecovery &&
            !configuration.ReverseFailureReason.IsEmpty()) {
          reason = configuration.ReverseFailureReason;
        }
        SetFailureReason(reason);
        wxLogMessage(
            "FINAL_ROUTE_SAFETY pass=0 route=\"%s -> %s\" "
            "reason=direct-final-approach-unreachable "
            "isochrons_considered=%d reverse_enabled=%d "
            "reverse_status=\"%s\" reverse_reason=\"%s\"",
            configuration.Start, configuration.End, isochrons_considered,
            configuration.UseReverseReachabilityRecovery ? 1 : 0,
            configuration.ReverseRecoveryStatus,
            configuration.ReverseFailureReason);
        SetFinished(false);
      }
    } else {
      for (std::vector<IsoRouteDestinationCandidate>::const_iterator it =
               destination_candidates.begin();
           it != destination_candidates.end(); ++it) {
        RouteMapConfiguration validation_configuration = configuration;
        delete destination_position;
        destination_position = new Position(
            validation_configuration.EndLat, validation_configuration.EndLon,
            it->endp, it->heading, NAN, it->endp->polar,
            it->endp->tacks + it->tacked, it->endp->jibes + it->jibed,
            it->endp->sail_plan_changes + it->sail_plan_changed, it->data_mask);

        m_EndTime = it->isochron_time + wxTimeSpan::Milliseconds(1000 * it->dt);
        last_destination_position = destination_position;
        clear_destination_plotdata = true;
        SetFinished(true);
        ++alternatives_validated;

        if (defer_chart_validation_to_main) {
          configuration = validation_configuration;
          accepted_destination_candidate = true;
          accepted_dt = it->dt;
          accepted_absolute_dt = it->absolute_dt;
          wxLogMessage(
              "FINAL_ROUTE_SAFETY alternative_selected_deferred "
              "route=\"%s -> %s\" destination_alternatives=%zu "
              "validated=%d accepted_dt=%.3f absolute_dt=%.3f "
              "isochrons_considered=%d thread=worker",
              configuration.Start, configuration.End,
              destination_candidates.size(), alternatives_validated,
              accepted_dt, it->absolute_dt, isochrons_considered);
          break;
        }

        if (ValidateDestinationRouteLand(validation_configuration) &&
            ValidatePlottedDestinationRouteLand(validation_configuration)) {
          configuration = validation_configuration;
          accepted_destination_candidate = true;
          accepted_dt = it->dt;
          accepted_absolute_dt = it->absolute_dt;
          break;
        }

        if (std::isnan(fastest_rejected_dt)) fastest_rejected_dt = it->dt;
        ++alternatives_rejected_by_chart;
        delete destination_position;
        destination_position = 0;
        m_EndTime = wxDateTime();
        SetFinished(false);
      }

      if (!accepted_destination_candidate) {
        bool recovered =
            configuration.UseReverseReachabilityRecovery &&
            TryReverseReachabilityRecovery(configuration, isochrons_considered);
        if (recovered) {
          SetFailureReason(wxEmptyString);
        } else {
          last_destination_position =
              ClosestPosition(configuration.EndLat, configuration.EndLon);
          configuration.land_crossing = alternatives_rejected_by_chart > 0;
          wxString reason = alternatives_rejected_by_chart > 0
                                ? _("No chart-safe final route found within "
                                    "current search limits")
                                : _("Final route did not reach destination");
          if (configuration.UseReverseReachabilityRecovery &&
              !configuration.ReverseFailureReason.IsEmpty()) {
            reason = configuration.ReverseFailureReason;
          }
          SetFailureReason(reason);
          wxLogMessage(
              "FINAL_ROUTE_SAFETY alternatives_exhausted route=\"%s -> %s\" "
              "destination_alternatives=%zu validated=%d chart_rejected=%d "
              "fastest_rejected_dt=%.3f isochrons_considered=%d "
              "reverse_enabled=%d reverse_status=\"%s\" reverse_reason=\"%s\"",
              configuration.Start, configuration.End,
              destination_candidates.size(), alternatives_validated,
              alternatives_rejected_by_chart, fastest_rejected_dt,
              isochrons_considered,
              configuration.UseReverseReachabilityRecovery ? 1 : 0,
              configuration.ReverseRecoveryStatus,
              configuration.ReverseFailureReason);
        }
      } else {
        wxLogMessage(
            "FINAL_ROUTE_SAFETY alternative_selected route=\"%s -> %s\" "
            "destination_alternatives=%zu validated=%d "
            "chart_rejected_before_accept=%d fastest_rejected_dt=%.3f "
            "accepted_dt=%.3f accepted_absolute_dt=%.3f "
            "isochrons_considered=%d",
            configuration.Start, configuration.End,
            destination_candidates.size(), alternatives_validated,
            alternatives_rejected_by_chart, fastest_rejected_dt, accepted_dt,
            accepted_absolute_dt, isochrons_considered);
      }
    }
    SetConfigurationPreserveResult(configuration);
    UpdateStatus(configuration);
  } else {
    last_destination_position =
        ClosestPosition(configuration.EndLat, configuration.EndLon);

    m_EndTime = wxDateTime();  // invalid
  }

  if (last_last_destination_position != last_destination_position) {
    // we can't clear because we are inside a worker thread
    // and there's a race with GetPlotData
    clear_destination_plotdata = true;
  }

  m_bUpdated = true;
  m_UpdateOverlay = true;
}

// CUSTOMIZATION

Position* RouteMapOverlay::getClosestRoutePositionFromCursor(
    double cursorLat, double cursorLon, PlotData& posData) {
  /* Method to find the closest calculated position of the boat on
   * the weather route based on the cursor position
   */

  double dist = INFINITY;
  const RouteMapConfiguration configuration = GetConfiguration();
  const double normalizedCursorLon = configuration.positive_longitudes
                                         ? positive_degrees(cursorLon)
                                         : cursorLon;
  std::list<PlotData> plot = GetPlotData(false);
  bool found = false;
  posData.time = wxInvalidDateTime;
  for (const auto& it : plot) {
    // Compare longitudes with wrap-around handling at the international
    // dateline.  This planar comparison is only used to select a nearby
    // displayed route point.
    const double dlon = heading_resolve(normalizedCursorLon - it.lon);
    const double tempDist = hypot(cursorLat - it.lat, dlon);
    if (tempDist < dist) {
      posData = it;
      dist = tempDist;
      found = true;
    }
  }
  if (!found) return nullptr;

  // Get full position
  Position* pos = last_destination_position;
  for (Position* p = pos; p && p->parent; p = p->parent) {
    if (p->lat == posData.lat && p->lon == posData.lon) {
      return p;
    }
  }
  return nullptr;
}
