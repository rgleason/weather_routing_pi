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

#include <wx/wx.h>

#include "ConstraintChecker.h"
#include "ChartSafetyHost.h"
#include "ChartSafetyPolicy.h"
#include "WeatherDataProvider.h"
#include "RouteMap.h"
#include "Utilities.h"

#include "georef.h"
#include "ocpn_plugin.h"

namespace {

// Constraint checks run on route workers.  Policy, performance guards and
// diagnostics are deliberately worker-local so one departure candidate can
// never change another candidate's acceptance decisions.  Every worker resets
// this state at the start of a route; the UI thread has its own independent
// state for scout preparation and output validation.
thread_local bool s_loggedChartSegmentSafety = false;
thread_local bool s_loggedGshhsSegmentSafetyFallback = false;
thread_local bool s_loggedSegmentSafetyNoData = false;
thread_local bool s_loggedGshhsDefault = false;
thread_local bool s_loggedExperimentalForcedFallback = false;
thread_local bool s_useExperimentalChartSafety = false;
thread_local bool s_enforceExperimentalChartSafety = false;
thread_local bool s_forceGshhsForPerformance = false;
thread_local wxString s_segmentSafetyDiagnosticContext;
thread_local long s_chartWouldRejectLogs = 0;
thread_local long s_unexpectedTileBuildLogs = 0;
thread_local long s_segmentSafetyApiCalls = 0;
thread_local long s_finalChartHitLogs = 0;
thread_local long s_finalChartHitLogsSuppressed = 0;
thread_local long s_endpointMarginRelaxedLogs = 0;
thread_local long s_endpointMarginRelaxedLogsSuppressed = 0;
thread_local long s_chartAvailableChecks = 0;
thread_local long s_chartUnavailableFallbacks = 0;
thread_local long s_gshhsSafetyCalls = 0;
thread_local long s_chartSelectMs = 0;
thread_local long s_cacheBuildMs = 0;
thread_local long s_geometryCheckMs = 0;
thread_local long s_pointCacheHits = 0;
thread_local long s_pointCacheMisses = 0;
thread_local long s_gridCacheHits = 0;
thread_local long s_gridCacheMisses = 0;
thread_local long s_gridBuildMs = 0;
thread_local long s_gridCellsTotal = 0;
thread_local long s_gridCellsLand = 0;
thread_local long s_gridCellsWater = 0;
thread_local long s_gridCellsDrying = 0;
thread_local long s_gridCellsUnknown = 0;
thread_local long s_gridLookups = 0;
thread_local long s_gridLookupMs = 0;
thread_local long s_segmentSampleCount = 0;
thread_local long s_waterTileShortcuts = 0;
thread_local long s_segmentCacheHits = 0;
thread_local long s_segmentCacheMisses = 0;
thread_local long s_segmentCacheStores = 0;
thread_local long s_gridCacheSize = 0;
thread_local long s_gridCacheEvictions = 0;
thread_local long s_unexpectedTileBuilds = 0;
thread_local long s_chartLandRejections = 0;
thread_local long s_chartAcceptedSegments = 0;
thread_local long s_finalRouteValidationChecks = 0;
thread_local long s_landRingTotal = 0;
thread_local long s_bboxRingTests = 0;
thread_local long s_edgeTests = 0;
thread_local long s_maxBBoxRingTests = 0;
thread_local long s_maxEdgeTests = 0;
thread_local long s_noChartDatabase = 0;
thread_local long s_noCandidateChart = 0;
thread_local long s_rasterOnly = 0;
thread_local long s_unsupportedChartType = 0;
thread_local long s_chartLoadFailed = 0;
thread_local long s_noLandareGeometry = 0;
thread_local long s_chartGeometryClear = 0;
thread_local long s_chartGeometryHit = 0;

const long kMaxSegmentSafetyApiCallsPerRun = 500;
const long kMaxChartSafetyMsPerRun = 250;
const long kMaxChartWouldRejectLogsPerRun = 10;
const long kMaxUnexpectedTileBuildLogsPerRun = 10;
const long kMaxFinalChartHitLogsPerRun = 20;

long ChartSafetyMeasuredMs() {
  return s_chartSelectMs + s_cacheBuildMs + s_geometryCheckMs +
         s_gridBuildMs + s_gridLookupMs;
}

wxString SegmentSafetyDiagnosticSummary(const wxString& context) {
  wxString message = wxString::Format(
      "WR_GRID summary context=%s api_calls=%ld chart_available=%ld "
      "unavailable_fallbacks=%ld gshhs_calls=%ld chart_select_ms=%ld "
      "cache_build_ms=%ld geometry_ms=%ld point_cache_hits=%ld "
      "point_cache_misses=%ld ",
      context, s_segmentSafetyApiCalls, s_chartAvailableChecks,
      s_chartUnavailableFallbacks, s_gshhsSafetyCalls, s_chartSelectMs,
      s_cacheBuildMs, s_geometryCheckMs, s_pointCacheHits,
      s_pointCacheMisses);
  message += wxString::Format(
      "grid_cache_hits=%ld grid_cache_misses=%ld grid_build_ms=%ld "
      "grid_cells=%ld land=%ld water=%ld drying=%ld unknown=%ld "
      "grid_lookups=%ld grid_lookup_ms=%ld samples=%ld "
      "avg_samples_per_call=%.2f water_tile_shortcuts=%ld "
      "segment_cache_hits=%ld segment_cache_misses=%ld "
      "segment_cache_stores=%ld grid_cache_size=%ld "
      "grid_cache_evictions=%ld unexpected_tile_builds=%ld "
      "chart_land_rejections=%ld chart_checked_accepts=%ld "
      "final_route_checks=%ld final_chart_hit_logs=%ld "
      "final_chart_hit_logs_suppressed=%ld "
      "endpoint_margin_relaxed=%ld "
      "endpoint_margin_relaxed_suppressed=%ld ",
      s_gridCacheHits, s_gridCacheMisses, s_gridBuildMs, s_gridCellsTotal,
      s_gridCellsLand, s_gridCellsWater, s_gridCellsDrying,
      s_gridCellsUnknown, s_gridLookups, s_gridLookupMs,
      s_segmentSampleCount,
      s_segmentSafetyApiCalls
          ? (double)s_segmentSampleCount / (double)s_segmentSafetyApiCalls
          : 0.0,
      s_waterTileShortcuts, s_segmentCacheHits, s_segmentCacheMisses,
      s_segmentCacheStores, s_gridCacheSize, s_gridCacheEvictions,
      s_unexpectedTileBuilds, s_chartLandRejections, s_chartAcceptedSegments,
      s_finalRouteValidationChecks, s_finalChartHitLogs,
      s_finalChartHitLogsSuppressed, s_endpointMarginRelaxedLogs,
      s_endpointMarginRelaxedLogsSuppressed);
  message += wxString::Format(
      "land_rings_seen=%ld bbox_ring_tests=%ld edge_tests=%ld "
      "max_bbox_rings_per_call=%ld max_edges_per_call=%ld "
      "reasons{no_db=%ld no_candidate=%ld raster_only=%ld unsupported=%ld "
      "load_failed=%ld no_lndare=%ld clear=%ld hit=%ld}.",
      s_landRingTotal, s_bboxRingTests, s_edgeTests, s_maxBBoxRingTests,
      s_maxEdgeTests, s_noChartDatabase, s_noCandidateChart, s_rasterOnly,
      s_unsupportedChartType, s_chartLoadFailed, s_noLandareGeometry,
      s_chartGeometryClear, s_chartGeometryHit);
  return message;
}

void CountSegmentSafetyDiagnostic(int reason) {
  switch (reason) {
    case PI_SEGMENT_SAFETY_DIAG_NO_CHART_DATABASE:
      ++s_noChartDatabase;
      break;
    case PI_SEGMENT_SAFETY_DIAG_NO_CANDIDATE_CHART:
      ++s_noCandidateChart;
      break;
    case PI_SEGMENT_SAFETY_DIAG_RASTER_ONLY:
      ++s_rasterOnly;
      break;
    case PI_SEGMENT_SAFETY_DIAG_UNSUPPORTED_CHART_TYPE:
      ++s_unsupportedChartType;
      break;
    case PI_SEGMENT_SAFETY_DIAG_CHART_LOAD_FAILED:
      ++s_chartLoadFailed;
      break;
    case PI_SEGMENT_SAFETY_DIAG_NO_LANDARE_GEOMETRY:
      ++s_noLandareGeometry;
      break;
    case PI_SEGMENT_SAFETY_DIAG_CHART_GEOMETRY_CLEAR:
      ++s_chartGeometryClear;
      break;
    case PI_SEGMENT_SAFETY_DIAG_CHART_GEOMETRY_HIT:
      ++s_chartGeometryHit;
      break;
  }
}

void AccumulateSegmentSafetyDiagnostics(
    const PlugInSegmentSafetyResult& result) {
  CountSegmentSafetyDiagnostic(result.diagnostic_reason);
  s_chartSelectMs += result.chart_select_ms;
  s_cacheBuildMs += result.cache_build_ms;
  s_geometryCheckMs += result.geometry_check_ms;
  s_pointCacheHits += result.point_cache_hits;
  s_pointCacheMisses += result.point_cache_misses;
  s_gridCacheHits += result.grid_cache_hits;
  s_gridCacheMisses += result.grid_cache_misses;
  s_gridBuildMs += result.grid_build_ms;
  s_gridCellsTotal += result.grid_cells_total;
  s_gridCellsLand += result.grid_cells_land;
  s_gridCellsWater += result.grid_cells_water;
  s_gridCellsDrying += result.grid_cells_drying;
  s_gridCellsUnknown += result.grid_cells_unknown;
  s_gridLookups += result.grid_lookups;
  s_gridLookupMs += result.grid_lookup_ms;
  s_segmentSampleCount += result.segment_sample_count;
  s_waterTileShortcuts += result.water_tile_shortcuts;
  s_segmentCacheHits += result.segment_cache_hits;
  s_segmentCacheMisses += result.segment_cache_misses;
  s_segmentCacheStores += result.segment_cache_stores;
  s_gridCacheSize = wxMax(s_gridCacheSize, (long)result.grid_cache_size);
  s_gridCacheEvictions =
      wxMax(s_gridCacheEvictions, (long)result.grid_cache_evictions);
  s_unexpectedTileBuilds += result.unexpected_tile_builds;
  s_landRingTotal += result.land_ring_count;
  s_bboxRingTests += result.bbox_ring_tests;
  s_edgeTests += result.edge_tests;
  s_maxBBoxRingTests = wxMax(s_maxBBoxRingTests, (long)result.bbox_ring_tests);
  s_maxEdgeTests = wxMax(s_maxEdgeTests, (long)result.edge_tests);
}

void LogSegmentSafetyGuard(const char* reason) {
  if (s_loggedExperimentalForcedFallback) return;
  wxString message = wxString::Format(
      "WeatherRouting Detect Land: experimental chart-based land checks "
      "disabled for propagation for the rest of this compute run (%s). "
      "Final-route chart validation remains active when enforcement is "
      "enabled. ",
      reason);
  message += wxString::Format(
      "api_calls=%ld chart_available=%ld unavailable_fallbacks=%ld "
      "gshhs_calls=%ld chart_select_ms=%ld cache_build_ms=%ld "
      "geometry_ms=%ld point_cache_hits=%ld point_cache_misses=%ld ",
      s_segmentSafetyApiCalls, s_chartAvailableChecks,
      s_chartUnavailableFallbacks, s_gshhsSafetyCalls, s_chartSelectMs,
      s_cacheBuildMs, s_geometryCheckMs, s_pointCacheHits,
      s_pointCacheMisses);
  message += wxString::Format(
      "grid_cache_hits=%ld grid_cache_misses=%ld grid_build_ms=%ld "
      "grid_cells=%ld land=%ld water=%ld drying=%ld unknown=%ld "
      "grid_lookups=%ld grid_lookup_ms=%ld samples=%ld "
      "avg_samples_per_call=%.2f water_tile_shortcuts=%ld "
      "unexpected_tile_builds=%ld chart_land_rejections=%ld "
      "final_route_checks=%ld ",
      s_gridCacheHits, s_gridCacheMisses, s_gridBuildMs, s_gridCellsTotal,
      s_gridCellsLand, s_gridCellsWater, s_gridCellsDrying,
      s_gridCellsUnknown, s_gridLookups, s_gridLookupMs,
      s_segmentSampleCount,
      s_segmentSafetyApiCalls
          ? (double)s_segmentSampleCount / (double)s_segmentSafetyApiCalls
          : 0.0,
      s_waterTileShortcuts, s_unexpectedTileBuilds, s_chartLandRejections,
      s_finalRouteValidationChecks);
  message += wxString::Format(
      "land_rings_seen=%ld bbox_ring_tests=%ld "
      "edge_tests=%ld max_bbox_rings_per_call=%ld max_edges_per_call=%ld ",
      s_landRingTotal, s_bboxRingTests, s_edgeTests, s_maxBBoxRingTests,
      s_maxEdgeTests);
  message += wxString::Format(
      "reasons{no_db=%ld no_candidate=%ld raster_only=%ld unsupported=%ld "
      "load_failed=%ld no_lndare=%ld clear=%ld hit=%ld}.",
      s_noChartDatabase, s_noCandidateChart, s_rasterOnly,
      s_unsupportedChartType, s_chartLoadFailed, s_noLandareGeometry,
      s_chartGeometryClear, s_chartGeometryHit);
  wxLogMessage("%s", message.c_str());
  s_loggedExperimentalForcedFallback = true;
}

wxString FormatChartLandCrossingReason(
    const PlugInSegmentSafetyResult& result) {
  wxString reason = _("Chart land crossing in final route");
  if (result.hit_sample_count > 0) {
    reason += wxString::Format(_(": %.6f, %.6f"),
                               result.hit_sample_lat,
                               result.hit_sample_lon);
  }
  if (result.hit_object[0]) {
    reason += _T(" ");
    reason += wxString::FromUTF8(result.hit_object);
  }
  return reason;
}

bool GshhsSegmentSafetyHitsLand(double lat1, double lon1, double lat2,
                                double lon2, double safety_margin_nm) {
  ++s_gshhsSafetyCalls;
  if (PlugIn_GSHHS_CrossesLand(lat1, lon1, lat2, lon2)) return true;

  if (safety_margin_nm <= 0.0) return false;

  double bearing = 0.0;
  double dist_nm = 0.0;
  ll_gc_ll_reverse(lat1, lon1, lat2, lon2, &bearing, &dist_nm);

  double lat_up1, lon_up1, lat_up2, lon_up2;
  double lat_down1, lon_down1, lat_down2, lon_down2;
  ll_gc_ll(lat1, lon1, heading_resolve(bearing - 90.0), safety_margin_nm,
           &lat_up1, &lon_up1);
  ll_gc_ll(lat2, lon2, heading_resolve(bearing - 90.0), safety_margin_nm,
           &lat_up2, &lon_up2);
  ll_gc_ll(lat1, lon1, heading_resolve(bearing + 90.0), safety_margin_nm,
           &lat_down1, &lon_down1);
  ll_gc_ll(lat2, lon2, heading_resolve(bearing + 90.0), safety_margin_nm,
           &lat_down2, &lon_down2);

  return PlugIn_GSHHS_CrossesLand(lat_up1, lon_up1, lat_up2, lon_up2) ||
         PlugIn_GSHHS_CrossesLand(lat_down1, lon_down1, lat_down2,
                                  lon_down2) ||
         PlugIn_GSHHS_CrossesLand(lat_up1, lon_up1, lat_down2, lon_down2) ||
         PlugIn_GSHHS_CrossesLand(lat_down1, lon_down1, lat_up2, lon_up2);
}

bool SegmentTouchesEndpointMarginZone(RouteMapConfiguration* configuration,
                                      double lat1, double lon1, double lat2,
                                      double lon2,
                                      double safety_margin_nm) {
  if (!configuration || safety_margin_nm <= 0.0) return false;

  /*
   * A start/end waypoint can legitimately be close to land, for example just
   * outside a harbour or headland.  The chart safety margin should not make it
   * impossible to leave or arrive at such a waypoint, but only for the route
   * edges inside the endpoint reach actually demonstrated by the
   * chart-independent scout and only for margin-only hits.  Actual
   * land/drying/depth/no-chart hazards are still checked by a zero-margin
   * segment safety call before any relaxation is allowed.  The reach is
   * search-derived for this route rather than a fixed distance.
   */
  const double endpoint_coordinate_tolerance_nm = 0.001;
  double bearing = 0.0;
  double dist_nm = 0.0;

  const double start_reach = wxMax(
      endpoint_coordinate_tolerance_nm,
      configuration->chart_safety_start_endpoint_reach_nm);
  const double end_reach = wxMax(
      endpoint_coordinate_tolerance_nm,
      configuration->chart_safety_end_endpoint_reach_nm);

  ll_gc_ll_reverse(configuration->StartLat, configuration->StartLon, lat1,
                   lon1, &bearing, &dist_nm);
  if (dist_nm <= start_reach) return true;
  ll_gc_ll_reverse(configuration->StartLat, configuration->StartLon, lat2,
                   lon2, &bearing, &dist_nm);
  if (dist_nm <= start_reach) return true;
  ll_gc_ll_reverse(configuration->EndLat, configuration->EndLon, lat1, lon1,
                   &bearing, &dist_nm);
  if (dist_nm <= end_reach) return true;
  ll_gc_ll_reverse(configuration->EndLat, configuration->EndLon, lat2, lon2,
                   &bearing, &dist_nm);
  return dist_nm <= end_reach;
}

void RecordMissingChartSafetyData(RouteMapConfiguration* configuration,
                                  const PlugInSegmentSafetyResult& result) {
  if (!configuration || result.unexpected_tile_builds <= 0) return;

  configuration->chart_safety_missing_tile_rejections +=
      result.unexpected_tile_builds;
  double tile_min_lat = result.unexpected_tile_min_lat;
  double tile_min_lon = result.unexpected_tile_min_lon;
  double tile_max_lat = tile_min_lat + 0.05;
  double tile_max_lon = tile_min_lon + 0.05;
  if (!std::isfinite(
          configuration->chart_safety_missing_tile_first_min_lat)) {
    configuration->chart_safety_missing_tile_first_lat_tile =
        result.unexpected_lat_tile;
    configuration->chart_safety_missing_tile_first_lon_tile =
        result.unexpected_lon_tile;
    configuration->chart_safety_missing_tile_first_min_lat =
        result.unexpected_tile_min_lat;
    configuration->chart_safety_missing_tile_first_min_lon =
        result.unexpected_tile_min_lon;
    configuration->chart_safety_missing_tile_min_lat = tile_min_lat;
    configuration->chart_safety_missing_tile_max_lat = tile_max_lat;
    configuration->chart_safety_missing_tile_min_lon = tile_min_lon;
    configuration->chart_safety_missing_tile_max_lon = tile_max_lon;
  } else {
    configuration->chart_safety_missing_tile_min_lat =
        wxMin(configuration->chart_safety_missing_tile_min_lat, tile_min_lat);
    configuration->chart_safety_missing_tile_max_lat =
        wxMax(configuration->chart_safety_missing_tile_max_lat, tile_max_lat);
    configuration->chart_safety_missing_tile_min_lon =
        wxMin(configuration->chart_safety_missing_tile_min_lon, tile_min_lon);
    configuration->chart_safety_missing_tile_max_lon =
        wxMax(configuration->chart_safety_missing_tile_max_lon, tile_max_lon);
  }
}

bool EndpointMarginOnlyHitIsZeroMarginSafe(RouteMapConfiguration* configuration,
                                           double lat1, double lon1,
                                           double lat2, double lon2,
                                           double safety_margin_nm,
                                           const char* context) {
  if (!configuration ||
      !SegmentTouchesEndpointMarginZone(configuration, lat1, lon1, lat2, lon2,
                                        safety_margin_nm))
    return false;

  PlugInSegmentSafetyOptions zero_margin_options = {};
  zero_margin_options.struct_size = sizeof(zero_margin_options);
  zero_margin_options.safety_margin_nm = 0.0;
  zero_margin_options.check_land = true;
  zero_margin_options.allow_gshhs_fallback = false;
  weather_routing::ApplyMinimumDepthPolicy(
      zero_margin_options, configuration->MinimumDepthMeters);

  PlugInSegmentSafetyResult zero_margin_result = {};
  zero_margin_result.struct_size = sizeof(zero_margin_result);
  if (!weather_routing::chart_safety_host::CheckSegment(
          lat1, lon1, lat2, lon2, &zero_margin_options,
          &zero_margin_result))
    return false;

  AccumulateSegmentSafetyDiagnostics(zero_margin_result);
  RecordMissingChartSafetyData(configuration, zero_margin_result);

  if (zero_margin_result.status != PI_SEGMENT_SAFETY_SAFE) return false;

  if (s_endpointMarginRelaxedLogs < 40) {
    wxLogMessage(
        "WR_ENDPOINT_MARGIN_RELAXED context=%s route=\"%s -> %s\" "
        "segment=(%.8f,%.8f)->(%.8f,%.8f) margin_nm=%.3f "
        "zero_margin_status=%d source=%d",
        context ? context : "unknown", configuration->Start, configuration->End,
        lat1, lon1, lat2, lon2, safety_margin_nm, zero_margin_result.status,
        zero_margin_result.source);
    ++s_endpointMarginRelaxedLogs;
  } else {
    ++s_endpointMarginRelaxedLogsSuppressed;
  }
  return true;
}

bool SegmentSafetyRejectsLand(RouteMapConfiguration* configuration,
                              double lat1, double lon1, double lat2,
                              double lon2, double safety_margin_nm) {
  if (!s_useExperimentalChartSafety || !configuration ||
      !configuration->UseChartSafetyForPropagation ||
      (s_forceGshhsForPerformance && !s_enforceExperimentalChartSafety)) {
    if (!s_loggedGshhsDefault) {
      wxLogMessage(
          s_useExperimentalChartSafety && s_enforceExperimentalChartSafety &&
                  configuration && !configuration->UseChartSafetyForPropagation
              ? "WeatherRouting Detect Land: using GSHHS for a non-deliverable "
                "search-envelope scout; production routes require "
                "authoritative chart propagation."
              : "WeatherRouting Detect Land: using GSHHS shoreline checks. "
                "Experimental chart-based land checks are disabled.");
      s_loggedGshhsDefault = true;
    }
    return GshhsSegmentSafetyHitsLand(lat1, lon1, lat2, lon2,
                                      safety_margin_nm);
  }

  PlugInSegmentSafetyOptions options = {};
  options.struct_size = sizeof(options);
  options.safety_margin_nm = safety_margin_nm;
  options.check_land = true;
  weather_routing::ApplyMinimumDepthPolicy(options,
                                           configuration->MinimumDepthMeters);
  /*
   * Final/display route validation is the last safety gate before a route can
   * be shown, applied, saved, or exported.  When experimental chart safety is
   * explicitly enforced, do not let a temporary chart-grid miss degrade to
   * GSHHS and pass as safe; the chart result must be available and safe.
   */
  options.allow_gshhs_fallback = false;

  PlugInSegmentSafetyResult result = {};
  result.struct_size = sizeof(result);
  ++s_segmentSafetyApiCalls;
  if (!weather_routing::chart_safety_host::CheckSegment(
          lat1, lon1, lat2, lon2, &options, &result)) {
    ++s_chartUnavailableFallbacks;
    if (s_enforceExperimentalChartSafety) {
      if (!s_loggedExperimentalForcedFallback) {
        wxLogError(
            "WeatherRouting chart safety is enforced but the enhanced "
            "OpenCPN chart-safety capability is unavailable; rejecting "
            "segments until the capability is restored.");
        s_loggedExperimentalForcedFallback = true;
      }
      return true;
    }
    return GshhsSegmentSafetyHitsLand(lat1, lon1, lat2, lon2,
                                      safety_margin_nm);
  }
  AccumulateSegmentSafetyDiagnostics(result);

  RecordMissingChartSafetyData(configuration, result);

  if (result.unexpected_tile_builds > 0 &&
      s_unexpectedTileBuildLogs < kMaxUnexpectedTileBuildLogsPerRun) {
    ++s_unexpectedTileBuildLogs;
    wxLogMessage(
        "WR_GRID_UNEXPECTED_TILE_BUILD_DURING_PROPAGATION "
        "context=%s lat1=%.8f lon1=%.8f lat2=%.8f lon2=%.8f "
        "margin_nm=%.3f tile=(%d,%d) tile_min=(%.6f,%.6f) "
        "tile_builds=%d grid_build_ms=%d grid_cache_hits=%d "
        "grid_cache_misses=%d grid_lookups=%d samples=%d.",
        s_segmentSafetyDiagnosticContext, lat1, lon1, lat2, lon2,
        safety_margin_nm, result.unexpected_lat_tile,
        result.unexpected_lon_tile, result.unexpected_tile_min_lat,
        result.unexpected_tile_min_lon,
        result.unexpected_tile_builds, result.grid_build_ms,
        result.grid_cache_hits, result.grid_cache_misses,
        result.grid_lookups, result.segment_sample_count);
  }

  if (result.used_fallback ||
      result.source == PI_SEGMENT_SAFETY_SOURCE_GSHHS_FALLBACK) {
    ++s_chartUnavailableFallbacks;
    ++s_gshhsSafetyCalls;
    if (!s_loggedGshhsSegmentSafetyFallback) {
      wxLogMessage(
          "WeatherRouting Detect Land: chart-based segment safety unavailable; "
          "using GSHHS fallback. reason=%d message=%s",
          result.diagnostic_reason, result.message);
      s_loggedGshhsSegmentSafetyFallback = true;
    }
  } else if (result.source == PI_SEGMENT_SAFETY_SOURCE_VECTOR_CHART ||
             result.source == PI_SEGMENT_SAFETY_SOURCE_PLUGIN_VECTOR ||
             result.source == PI_SEGMENT_SAFETY_SOURCE_CM93) {
    ++s_chartAvailableChecks;
    if (!s_loggedChartSegmentSafety) {
      wxLogMessage(
          "WeatherRouting Detect Land: using chart-based segment safety "
          "checks.");
      s_loggedChartSegmentSafety = true;
    }
  } else if (result.status == PI_SEGMENT_SAFETY_NO_DATA ||
             result.status == PI_SEGMENT_SAFETY_ERROR) {
    if (!s_loggedSegmentSafetyNoData) {
      wxLogMessage(
          "WeatherRouting Detect Land: chart-based segment safety returned no "
          "usable chart land data.");
      s_loggedSegmentSafetyNoData = true;
    }
  }

  if (!s_enforceExperimentalChartSafety &&
      ChartSafetyMeasuredMs() >= kMaxChartSafetyMsPerRun) {
    s_forceGshhsForPerformance = true;
    LogSegmentSafetyGuard("chart-time-limit");
  } else if (!s_enforceExperimentalChartSafety &&
             s_segmentSafetyApiCalls >= kMaxSegmentSafetyApiCallsPerRun &&
             s_chartUnavailableFallbacks > s_chartAvailableChecks * 4) {
    s_forceGshhsForPerformance = true;
    LogSegmentSafetyGuard("unavailable-fallback-limit");
  } else if (s_enforceExperimentalChartSafety &&
             ChartSafetyMeasuredMs() >= kMaxChartSafetyMsPerRun &&
             !s_loggedExperimentalForcedFallback) {
    wxString message = wxString::Format(
        "WeatherRouting Detect Land: experimental chart checks exceeded "
        "the propagation performance budget, but enforcement remains active. "
        "api_calls=%ld chart_available=%ld gshhs_calls=%ld "
        "chart_select_ms=%ld cache_build_ms=%ld geometry_ms=%ld "
        "point_cache_hits=%ld point_cache_misses=%ld "
        "grid_cache_hits=%ld grid_cache_misses=%ld grid_build_ms=%ld "
        "grid_cells=%ld land=%ld water=%ld drying=%ld unknown=%ld "
        "grid_lookups=%ld grid_lookup_ms=%ld samples=%ld "
        "avg_samples_per_call=%.2f water_tile_shortcuts=%ld "
        "segment_cache_hits=%ld segment_cache_misses=%ld "
        "segment_cache_stores=%ld grid_cache_size=%ld "
        "grid_cache_evictions=%ld unexpected_tile_builds=%ld "
        "chart_land_rejections=%ld final_route_checks=%ld.",
        s_segmentSafetyApiCalls, s_chartAvailableChecks, s_gshhsSafetyCalls,
        s_chartSelectMs, s_cacheBuildMs, s_geometryCheckMs, s_pointCacheHits,
        s_pointCacheMisses, s_gridCacheHits, s_gridCacheMisses, s_gridBuildMs,
        s_gridCellsTotal, s_gridCellsLand, s_gridCellsWater, s_gridCellsDrying,
        s_gridCellsUnknown, s_gridLookups, s_gridLookupMs,
        s_segmentSampleCount,
        s_segmentSafetyApiCalls
            ? (double)s_segmentSampleCount / (double)s_segmentSafetyApiCalls
            : 0.0,
        s_waterTileShortcuts, s_segmentCacheHits, s_segmentCacheMisses,
        s_segmentCacheStores, s_gridCacheSize, s_gridCacheEvictions,
        s_unexpectedTileBuilds, s_chartLandRejections,
        s_finalRouteValidationChecks);
    wxLogMessage("%s", message.c_str());
    s_loggedExperimentalForcedFallback = true;
  }

  bool chart_rejects =
      result.status == PI_SEGMENT_SAFETY_CROSSES_LAND ||
      result.status == PI_SEGMENT_SAFETY_WITHIN_LAND_MARGIN ||
      result.status == PI_SEGMENT_SAFETY_UNSAFE_AREA ||
      result.status == PI_SEGMENT_SAFETY_DRYING_AREA ||
      result.status == PI_SEGMENT_SAFETY_TOO_SHALLOW ||
      result.status == PI_SEGMENT_SAFETY_UNKNOWN_DEPTH ||
                 result.status == PI_SEGMENT_SAFETY_NO_DATA ||
                 result.status == PI_SEGMENT_SAFETY_ERROR;

  if (chart_rejects &&
      result.status == PI_SEGMENT_SAFETY_WITHIN_LAND_MARGIN &&
      EndpointMarginOnlyHitIsZeroMarginSafe(configuration, lat1, lon1, lat2,
                                            lon2, safety_margin_nm,
                                            "propagation")) {
    chart_rejects = false;
  }

  if (chart_rejects && !result.used_fallback &&
      result.source != PI_SEGMENT_SAFETY_SOURCE_GSHHS_FALLBACK)
    ++s_chartLandRejections;
  else if (!chart_rejects && !result.used_fallback &&
           (result.source == PI_SEGMENT_SAFETY_SOURCE_VECTOR_CHART ||
            result.source == PI_SEGMENT_SAFETY_SOURCE_PLUGIN_VECTOR ||
            result.source == PI_SEGMENT_SAFETY_SOURCE_CM93))
    ++s_chartAcceptedSegments;

  if (chart_rejects && !result.used_fallback &&
      result.source != PI_SEGMENT_SAFETY_SOURCE_GSHHS_FALLBACK &&
      s_chartWouldRejectLogs < kMaxChartWouldRejectLogsPerRun) {
    ++s_chartWouldRejectLogs;
    wxString message = wxString::Format(
        "WeatherRouting Detect Land: experimental chart land rejection %s "
        "segment #%ld lat1=%.8f lon1=%.8f lat2=%.8f lon2=%.8f "
        "margin_nm=%.3f status=%d source=%d reason=%d message=%s ",
        s_enforceExperimentalChartSafety ? "rejected" : "would reject",
        s_chartWouldRejectLogs, lat1, lon1, lat2, lon2, safety_margin_nm,
        result.status, result.source, result.diagnostic_reason, result.message);
    message += wxString::Format(
        "sample=(%.8f,%.8f) sample_index=%d/%d object=\"%s\" "
        "chart_db_index=%d chart_scale=%d chart_path=\"%s\" ",
        result.hit_sample_lat, result.hit_sample_lon,
        result.hit_sample_index + 1, result.hit_sample_count,
        result.hit_object, result.chart_db_index, result.chart_scale,
        result.chart_path);
    message += wxString::Format(
        "land_rings=%d bbox_tests=%d edge_tests=%d "
        "point_cache_hits=%d point_cache_misses=%d ",
        result.land_ring_count, result.bbox_ring_tests,
        result.edge_tests, result.point_cache_hits, result.point_cache_misses);
    message += wxString::Format(
        "grid_cache_hits=%d grid_cache_misses=%d grid_build_ms=%d "
        "grid_lookups=%d grid_lookup_ms=%d samples=%d "
        "water_tile_shortcuts=%d unexpected_tile_builds=%d enforce=%d.",
        result.grid_cache_hits, result.grid_cache_misses,
        result.grid_build_ms, result.grid_lookups, result.grid_lookup_ms,
        result.segment_sample_count, result.water_tile_shortcuts,
        result.unexpected_tile_builds,
        s_enforceExperimentalChartSafety ? 1 : 0);
    wxLogMessage("%s", message.c_str());
  }

  if (s_enforceExperimentalChartSafety) return chart_rejects;

  return GshhsSegmentSafetyHitsLand(lat1, lon1, lat2, lon2,
                                    safety_margin_nm);
}

bool FinalRouteSegmentSafetyRejectsLand(RouteMapConfiguration* configuration,
                                        double lat1, double lon1, double lat2,
                                        double lon2,
                                        double safety_margin_nm,
                                        wxString* failure_reason) {
  if (!s_useExperimentalChartSafety || !s_enforceExperimentalChartSafety) {
    bool rejects = GshhsSegmentSafetyHitsLand(lat1, lon1, lat2, lon2,
                                             safety_margin_nm);
    if (rejects && failure_reason)
      *failure_reason = _("Land crossing in final route");
    return rejects;
  }

  // This is the final gate before a route is displayed, applied or exported.
  // GSHHS may guide the cheap search, but it cannot turn unavailable official
  // vector evidence into an authoritative SAFE result.
  PlugInSegmentSafetyOptions options =
      weather_routing::MakeFinalRouteSegmentSafetyOptions(
          safety_margin_nm, configuration->MinimumDepthMeters);

  PlugInSegmentSafetyResult result = {};
  result.struct_size = sizeof(result);
  ++s_finalRouteValidationChecks;
  if (!weather_routing::chart_safety_host::CheckSegment(
          lat1, lon1, lat2, lon2, &options, &result)) {
    if (failure_reason)
      *failure_reason =
          _("Authoritative chart safety is unavailable in this OpenCPN "
            "build; enforced chart-aware routing failed closed");
    return true;
  }

  bool rejects = result.status == PI_SEGMENT_SAFETY_CROSSES_LAND ||
                 result.status == PI_SEGMENT_SAFETY_WITHIN_LAND_MARGIN ||
                 result.status == PI_SEGMENT_SAFETY_UNSAFE_AREA ||
                 result.status == PI_SEGMENT_SAFETY_DRYING_AREA ||
                 result.status == PI_SEGMENT_SAFETY_TOO_SHALLOW ||
                 result.status == PI_SEGMENT_SAFETY_UNKNOWN_DEPTH ||
                 result.status == PI_SEGMENT_SAFETY_NO_DATA ||
                 result.status == PI_SEGMENT_SAFETY_ERROR;
  if (rejects &&
      result.status == PI_SEGMENT_SAFETY_WITHIN_LAND_MARGIN &&
      EndpointMarginOnlyHitIsZeroMarginSafe(configuration, lat1, lon1, lat2,
                                            lon2, safety_margin_nm,
                                            "final-route")) {
    return false;
  }
  if (!rejects) return false;

  if (failure_reason) {
    if (result.status == PI_SEGMENT_SAFETY_NO_DATA ||
        result.status == PI_SEGMENT_SAFETY_ERROR) {
      *failure_reason = _("Chart safety data unavailable in final route");
    } else if (!result.used_fallback &&
        result.source != PI_SEGMENT_SAFETY_SOURCE_GSHHS_FALLBACK)
      *failure_reason = FormatChartLandCrossingReason(result);
    else
      *failure_reason = _("Land crossing in final route");
  }

  if (s_finalChartHitLogs < kMaxFinalChartHitLogsPerRun) {
    ++s_finalChartHitLogs;
    wxLogMessage(
        "FINAL_ROUTE_SAFETY chart_hit #%ld status=%d source=%d fallback=%d "
        "reason=%d message=\"%s\" sample=(%.8f,%.8f) sample_index=%d/%d "
        "object=\"%s\" chart_db_index=%d chart_scale=%d chart_path=\"%s\"",
        s_finalChartHitLogs, result.status, result.source,
        result.used_fallback, result.diagnostic_reason, result.message,
        result.hit_sample_lat, result.hit_sample_lon,
        result.hit_sample_index + 1, result.hit_sample_count,
        result.hit_object, result.chart_db_index, result.chart_scale,
        result.chart_path);
  } else {
    ++s_finalChartHitLogsSuppressed;
  }

  return true;
}

}  // namespace

void ConstraintChecker::ResetSegmentSafetyDiagnostics(
    bool use_experimental_chart_safety,
    bool enforce_experimental_chart_safety) {
  s_loggedChartSegmentSafety = false;
  s_loggedGshhsSegmentSafetyFallback = false;
  s_loggedSegmentSafetyNoData = false;
  s_loggedGshhsDefault = false;
  s_loggedExperimentalForcedFallback = false;
  s_useExperimentalChartSafety = use_experimental_chart_safety;
  s_enforceExperimentalChartSafety = enforce_experimental_chart_safety;
  s_forceGshhsForPerformance = false;
  s_chartWouldRejectLogs = 0;
  s_unexpectedTileBuildLogs = 0;
  s_segmentSafetyApiCalls = 0;
  s_finalChartHitLogs = 0;
  s_finalChartHitLogsSuppressed = 0;
  s_endpointMarginRelaxedLogs = 0;
  s_endpointMarginRelaxedLogsSuppressed = 0;
  s_chartAvailableChecks = 0;
  s_chartUnavailableFallbacks = 0;
  s_gshhsSafetyCalls = 0;
  s_chartSelectMs = 0;
  s_cacheBuildMs = 0;
  s_geometryCheckMs = 0;
  s_pointCacheHits = 0;
  s_pointCacheMisses = 0;
  s_gridCacheHits = 0;
  s_gridCacheMisses = 0;
  s_gridBuildMs = 0;
  s_gridCellsTotal = 0;
  s_gridCellsLand = 0;
  s_gridCellsWater = 0;
  s_gridCellsDrying = 0;
  s_gridCellsUnknown = 0;
  s_gridLookups = 0;
  s_gridLookupMs = 0;
  s_segmentSampleCount = 0;
  s_waterTileShortcuts = 0;
  s_segmentCacheHits = 0;
  s_segmentCacheMisses = 0;
  s_segmentCacheStores = 0;
  s_gridCacheSize = 0;
  s_gridCacheEvictions = 0;
  s_unexpectedTileBuilds = 0;
  s_chartLandRejections = 0;
  s_chartAcceptedSegments = 0;
  s_finalRouteValidationChecks = 0;
  s_landRingTotal = 0;
  s_bboxRingTests = 0;
  s_edgeTests = 0;
  s_maxBBoxRingTests = 0;
  s_maxEdgeTests = 0;
  s_noChartDatabase = 0;
  s_noCandidateChart = 0;
  s_rasterOnly = 0;
  s_unsupportedChartType = 0;
  s_chartLoadFailed = 0;
  s_noLandareGeometry = 0;
  s_chartGeometryClear = 0;
  s_chartGeometryHit = 0;
  s_segmentSafetyDiagnosticContext.Clear();
}

void ConstraintChecker::SetSegmentSafetyDiagnosticContext(
    const wxString& context) {
  s_segmentSafetyDiagnosticContext = context;
}

void ConstraintChecker::LogSegmentSafetyDiagnostics(const wxString& context) {
  wxLogMessage("%s", SegmentSafetyDiagnosticSummary(context).c_str());
}

bool ConstraintChecker::IsExperimentalChartSafetyEnforced() {
  return s_useExperimentalChartSafety && s_enforceExperimentalChartSafety;
}

bool ConstraintChecker::CheckSwellConstraint(
    RouteMapConfiguration& configuration, double lat, double lon, double& swell,
    PropagationError& error_code) {
  swell = WeatherDataProvider::GetSwell(configuration, lat, lon);
  if (swell > configuration.MaxSwellMeters) {
    error_code = PROPAGATION_EXCEEDED_MAX_SWELL;
    return false;
  }
  return true;
}

bool ConstraintChecker::CheckMaxLatitudeConstraint(
    RouteMapConfiguration& configuration, double lat,
    PropagationError& error_code) {
  if (fabs(lat) > configuration.MaxLatitude) {
    error_code = PROPAGATION_EXCEEDED_MAX_LATITUDE;
    return false;
  }
  return true;
}

bool ConstraintChecker::CheckCycloneTrackConstraint(
    RouteMapConfiguration& configuration, double lat, double lon, double dlat,
    double dlon) {
  if (configuration.AvoidCycloneTracks &&
      RouteMap::ClimatologyCycloneTrackCrossings &&
      WeatherDataProvider::CanInvokeClimatology(
          ClimatologyService::CycloneTracks)) {
    std::lock_guard<std::recursive_mutex> invocationLock(
        ClimatologyThreadGuard::InvocationMutex());
    int crossings = RouteMap::ClimatologyCycloneTrackCrossings(
        lat, lon, dlat, dlon, configuration.time,
        configuration.CycloneMonths * 30 + configuration.CycloneDays);
    if (crossings > 0) {
      return false;
    }
  }
  return true;
}

bool ConstraintChecker::CheckMaxCourseAngleConstraint(
    RouteMapConfiguration& configuration, double dlat, double dlon) {
  if (configuration.MaxCourseAngle < 180) {
    double bearing;
    // this is faster than gc distance, and actually works better in higher
    // latitudes
    double d1 = dlat - configuration.StartLat,
           d2 = dlon - configuration.StartLon;
    d2 *= cos(deg2rad(dlat)) / 2;  // correct for latitude
    bearing = rad2deg(atan2(d2, d1));

    if (fabs(heading_resolve(configuration.StartEndBearing - bearing)) >
        configuration.MaxCourseAngle) {
      return false;
    }
  }
  return true;
}

bool ConstraintChecker::CheckMaxDivertedCourse(
    RouteMapConfiguration& configuration, double dlat, double dlon) {
  if (configuration.MaxDivertedCourse < 180) {
    double bearing, dist;
    double bearing1, dist1;

    double d1 = dlat - configuration.EndLat, d2 = dlon - configuration.EndLon;
    d2 *= cos(deg2rad(dlat)) / 2;  // correct for latitude
    bearing = rad2deg(atan2(d2, d1));
    dist = sqrt(pow(d1, 2) + pow(d2, 2));

    d1 = configuration.StartLat - dlat, d2 = configuration.StartLon - dlon;
    bearing1 = rad2deg(atan2(d2, d1));
    dist1 = sqrt(pow(d1, 2) + pow(d2, 2));

    double term = (dist1 + dist) / dist;
    term = pow(term / 16, 4) + 1;  // make 1 until the end, then make big

    if (fabs(heading_resolve(bearing1 - bearing)) >
        configuration.MaxDivertedCourse * term) {
      return false;
    }
  }
  return true;
}

bool ConstraintChecker::CheckLandConstraint(
    RouteMapConfiguration& configuration, double lat, double lon, double dlat1,
    double dlon1, double cog) {
  if (configuration.DetectLand) {
    double ndlon1 = dlon1;

    // Check first if crossing land.
    if (ndlon1 > 360) {
      ndlon1 -= 360;
    }
    if (SegmentSafetyRejectsLand(&configuration, lat, lon, dlat1, ndlon1,
                                 configuration.SafetyMarginLand)) {
      return false;
    }
  }
  return true;
}

bool ConstraintChecker::CheckFinalRouteLandConstraint(
    RouteMapConfiguration& configuration, double lat, double lon, double dlat1,
    double dlon1, double cog, wxString* failure_reason) {
  if (configuration.DetectLand) {
    double ndlon1 = dlon1;
    if (ndlon1 > 360) {
      ndlon1 -= 360;
    }
    if (FinalRouteSegmentSafetyRejectsLand(&configuration, lat, lon, dlat1,
                                           ndlon1,
                                           configuration.SafetyMarginLand,
                                           failure_reason)) {
      return false;
    }
  }
  return true;
}

bool ConstraintChecker::CheckMaxTrueWindConstraint(
    RouteMapConfiguration& configuration, double twsOverWater,
    PropagationError& error_code) {
  if (twsOverWater > configuration.MaxTrueWindKnots) {
    error_code = PROPAGATION_EXCEEDED_MAX_WIND;
    return false;
  }
  return true;
}

bool ConstraintChecker::CheckMaxApparentWindConstraint(
    RouteMapConfiguration& configuration, double stw, double twa,
    double twsOverWater, PropagationError& error_code) {
  if (stw + twsOverWater > configuration.MaxApparentWindKnots &&
      Polar::VelocityApparentWind(stw, twa, twsOverWater) >
          configuration.MaxApparentWindKnots) {
    error_code = PROPAGATION_EXCEEDED_APPARENT_WIND;
    return false;
  }
  return true;
}

bool ConstraintChecker::CheckWindVsCurrentConstraint(
    RouteMapConfiguration& configuration, double twsOverWater,
    double twdOverWater, double currentSpeed, double currentDir,
    PropagationError& error_code) {
  if (configuration.WindVSCurrent) {
    /* Calculate the wind vector (Wx, Wy) and ocean current vector (Cx, Cy). */
    /* these are already computed in GroundToWaterFrame could optimize by
     * reusing them
     */
    double Wx = twsOverWater * cos(deg2rad(twdOverWater)),
           Wy = twsOverWater * sin(deg2rad(twdOverWater));
    double Cx = currentSpeed * cos(deg2rad(currentDir) + M_PI),
           Cy = currentSpeed * sin(deg2rad(currentDir) + M_PI);

    if (Wx * Cx + Wy * Cy + configuration.WindVSCurrent < 0) {
      error_code = PROPAGATION_EXCEEDED_WIND_VS_CURRENT;
      return false;
    }
  }
  return true;
}
