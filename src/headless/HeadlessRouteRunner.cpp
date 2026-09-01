/***************************************************************************
 *   Copyright (C) 2026 by OpenCPN contributors                            *
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 3 of the License, or     *
 *   (at your option) any later version.                                   *
 ***************************************************************************/

#include "HeadlessRouteRunner.h"

#include <cmath>
#include <cstdlib>

#include <wx/wx.h>
#include <wx/datetime.h>
#include <wx/filename.h>

#include "RouteMapOverlay.h"
#include "RoutingScenarioJson.h"
#include "StabilityRouteAdapter.h"
#include "weather_routing_engine/StabilityCorridor.h"

namespace {

wxString EnvString(const char* name) {
  const char* value = getenv(name);
  return value ? wxString::FromUTF8(value) : wxString();
}

weather_routing_engine::RoutingCandidateResult CandidateFromRoute(
    RouteMapOverlay* route) {
  weather_routing_engine::RoutingCandidateResult candidate;
  if (!route) {
    candidate.state = "missing";
    candidate.finalSafety = "unknown";
    candidate.failureReason = "missing route";
    return candidate;
  }

  RouteMapConfiguration configuration = route->GetConfiguration();
  candidate.departure = configuration.StartTime;
  candidate.offsetMinutes = configuration.DepartureTimeOptimizationOffsetMinutes;
  candidate.eta = route->EndTime();
  candidate.distanceNm = route->RouteInfo(RouteMapOverlay::DISTANCE);
  if (candidate.eta.IsValid() && configuration.StartTime.IsValid()) {
    wxTimeSpan elapsed = candidate.eta - configuration.StartTime;
    candidate.elapsedSeconds = elapsed.GetSeconds().ToLong();
  }

  if (route->Finished() && route->ReachedDestination()) {
    candidate.state = "complete";
    candidate.finalSafety = "pass";
  } else if (route->Finished()) {
    candidate.state = "failed";
    candidate.finalSafety = "fail";
  } else {
    candidate.state = "incomplete";
    candidate.finalSafety = "unknown";
  }
  candidate.failureReason = route->GetFailureReason();
  candidate.reverseRecoveryUsed = configuration.ReverseRecoveryUsed;
  candidate.reverseRecoveryStatus = configuration.ReverseRecoveryStatus;
  candidate.reverseLayersBuilt = configuration.ReverseLayersBuilt;
  candidate.reverseNodesGenerated = configuration.ReverseNodesGenerated;
  candidate.reverseNodesFeasible = configuration.ReverseNodesFeasible;
  candidate.reverseConnectionFound = configuration.ReverseConnectionFound;
  candidate.reverseConnectionTime = configuration.ReverseConnectionTime;
  candidate.reverseFailureReason = configuration.ReverseFailureReason;
  candidate.reverseFinalValidationPass = configuration.ReverseFinalValidationPass;
  const auto append_point = [&candidate](double latitude, double longitude,
                                          const wxDateTime& time) {
    if (!candidate.route.empty()) {
      const auto& last = candidate.route.back();
      if (fabs(last.latitudeDegrees - latitude) < 1e-9 &&
          fabs(last.longitudeDegrees - longitude) < 1e-9)
        return;
    }
    candidate.route.emplace_back(latitude, longitude, time);
  };
  append_point(configuration.StartLat, configuration.StartLon,
               configuration.StartTime);
  for (const auto& point : route->GetPlotData(false))
    append_point(point.lat, point.lon, point.time);
  append_point(configuration.EndLat, configuration.EndLon, candidate.eta);
  return candidate;
}

wxString StabilityGeoJsonPath(const wxString& outputPath) {
  if (outputPath.IsEmpty()) return wxString();
  wxFileName filename(outputPath);
  filename.SetExt("stability.geojson");
  return filename.GetFullPath();
}

void PopulateStabilityResult(
    const wxString& outputPath,
    const weather_routing_engine::RoutingScenario& scenario,
    const std::vector<RouteMapOverlay*>& routeMaps,
    weather_routing_engine::RoutingResult& output) {
  if (!scenario.stabilityCorridor.enabled) return;
  auto& summary = output.stabilityCorridor;
  summary.requested = true;
  summary.innerThreshold =
      scenario.stabilityCorridor.innerAgreementThreshold;
  summary.outerThreshold =
      scenario.stabilityCorridor.outerAgreementThreshold;
  if (!scenario.stabilityCorridor.source.IsSameAs("departureCandidates")) {
    summary.status = "unavailable";
    summary.failureReason = wxString::Format(
        "unsupported stability corridor source: %s",
        scenario.stabilityCorridor.source);
    wxLogMessage(
        "WR_STABILITY_CORRIDOR_RESULT source=headless status=unavailable "
        "reason=\"%s\"",
        summary.failureReason);
    return;
  }

  std::vector<weather_routing_engine::StabilityRoute> routes;
  routes.reserve(routeMaps.size());
  routes = BuildValidatedStabilityRoutes(routeMaps);

  RouteMapConfiguration safetyConfiguration;
  bool haveSafetyConfiguration = false;
  for (RouteMapOverlay* route : routeMaps) {
    if (route) {
      safetyConfiguration = route->GetConfiguration();
      haveSafetyConfiguration = true;
      break;
    }
  }
  weather_routing_engine::StabilityCorridorOptions options;
  options.minimumRoutes = scenario.stabilityCorridor.minimumRoutes;
  options.maxEtaPenaltyMinutes =
      scenario.stabilityCorridor.maxEtaPenaltyMinutes;
  options.gridResolutionNm = scenario.stabilityCorridor.gridResolutionNm;
  options.innerAgreementThreshold =
      scenario.stabilityCorridor.innerAgreementThreshold;
  options.outerAgreementThreshold =
      scenario.stabilityCorridor.outerAgreementThreshold;
  options.clusterRoutes = scenario.stabilityCorridor.clusterRoutes;

  wxLogMessage(
      "WR_STABILITY_CORRIDOR_START source=headless candidates=%lu "
      "minimum_routes=%d grid_resolution_nm=%.3f inner=%.3f outer=%.3f "
      "authoritative_final_validation=1 descriptive_only=1",
      static_cast<unsigned long>(routes.size()), options.minimumRoutes,
      options.gridResolutionNm, options.innerAgreementThreshold,
      options.outerAgreementThreshold);
  const auto segmentSafety =
      [&](const weather_routing_engine::StabilityPoint& first,
          const weather_routing_engine::StabilityPoint& last) {
        return !haveSafetyConfiguration ||
               CheckStabilitySafetySegment(safetyConfiguration, first, last);
      };
  const auto cellSafety = [&](double minLat, double minLon, double maxLat,
                              double maxLon) {
    if (!haveSafetyConfiguration || !safetyConfiguration.DetectLand)
      return true;
    return CheckStabilitySafetyCell(safetyConfiguration, minLat, minLon,
                                    maxLat, maxLon);
  };
  weather_routing_engine::StabilityCorridorResult result =
      weather_routing_engine::StabilityCorridorCalculator::Calculate(
          routes, options, segmentSafety, cellSafety);
  wxLogMessage(
      "WR_STABILITY_CORRIDOR_BUILD source=headless valid=%d excluded=%d "
      "families=%lu cells=%d unsafe_cells_excluded=%d elapsed_ms=%ld",
      result.validRoutes, result.excludedRoutes,
      static_cast<unsigned long>(result.families.size()),
      result.rasterCellsUsed, result.unsafeCellsExcluded,
      result.calculationTimeMs);
  for (const auto& family : result.families)
    wxLogMessage(
        "WR_STABILITY_CORRIDOR_CLUSTER source=headless family=%d routes=%lu "
        "representative=%lu median_width_nm=%.3f max_width_nm=%.3f "
        "eta_spread_min=%.1f",
        family.id, static_cast<unsigned long>(family.routeIndices.size()),
        static_cast<unsigned long>(family.representativeRouteIndex),
        family.medianWidthNm, family.maximumWidthNm,
        family.etaSpreadMinutes);
  summary.status = result.success ? "complete" : "unavailable";
  summary.validRoutes = result.validRoutes;
  summary.excludedRoutes = result.excludedRoutes;
  summary.routeFamilies = static_cast<int>(result.families.size());
  summary.failureReason = result.failureReason;
  summary.calculationTimeMs = result.calculationTimeMs;

  if (result.success) {
    const weather_routing_engine::RouteFamily* selected = nullptr;
    for (const auto& family : result.families)
      if (!selected || family.routeIndices.size() > selected->routeIndices.size())
        selected = &family;
    if (selected) {
      summary.selectedFamilyId = selected->id;
      summary.dominantFamilyRoutes =
          static_cast<int>(selected->routeIndices.size());
      summary.medianWidthNm = selected->medianWidthNm;
      summary.maximumWidthNm = selected->maximumWidthNm;
      summary.etaSpreadMinutes = selected->etaSpreadMinutes;
      if (selected->representativeRouteIndex < routes.size())
        summary.representativeCandidateId =
            routes[selected->representativeRouteIndex].id;
    }
    if (scenario.stabilityCorridor.writeGeoJson) {
      wxString error;
      summary.geoJsonPath = StabilityGeoJsonPath(outputPath);
      if (!weather_routing_engine::WriteStabilityCorridorGeoJson(
              summary.geoJsonPath, routes, result, error)) {
        summary.status = "partial";
        summary.failureReason = error;
        summary.geoJsonPath.Clear();
      }
    }
  }
  wxLogMessage(
      "WR_STABILITY_CORRIDOR_RESULT source=headless status=%s valid=%d "
      "excluded=%d families=%d selected_family=%d routes=%d cells=%d "
      "unsafe_cells_excluded=%d elapsed_ms=%ld reason=\"%s\"",
      summary.status, summary.validRoutes, summary.excludedRoutes,
      summary.routeFamilies, summary.selectedFamilyId,
      summary.dominantFamilyRoutes, result.rasterCellsUsed,
      result.unsafeCellsExcluded, result.calculationTimeMs,
      summary.failureReason);
}

}  // namespace

namespace weather_routing_headless {

bool HeadlessRouteRunner::LoadScenarioFromEnv(
    weather_routing_engine::RoutingScenario& scenario,
    wxString& scenarioPath,
    wxString& outputPath,
    wxString& error) {
  scenarioPath = EnvString("WR_HEADLESS_SCENARIO");
  outputPath = EnvString("WR_HEADLESS_OUTPUT");
  if (scenarioPath.IsEmpty()) {
    error = "WR_HEADLESS_SCENARIO is not set";
    return false;
  }
  return LoadRoutingScenarioJson(scenarioPath, scenario, error);
}

bool HeadlessRouteRunner::WriteStartedResult(
    const wxString& outputPath,
    const weather_routing_engine::RoutingScenario& scenario,
    wxString& error) {
  if (outputPath.IsEmpty()) return true;
  weather_routing_engine::RoutingResult result;
  result.scenario = scenario.name;
  result.status = "running";
  return SaveRoutingResultJson(outputPath, result, error);
}

bool HeadlessRouteRunner::WriteSingleRouteResult(
    const wxString& outputPath,
    const weather_routing_engine::RoutingScenario* scenario,
    const wxString& status,
    const wxString& failureReason,
    const std::vector<RouteMapOverlay*>& routes,
    wxString& error) {
  if (outputPath.IsEmpty()) return true;

  weather_routing_engine::RoutingResult result;
  result.scenario =
      scenario ? scenario->name : wxString(_T("Weather Routing headless run"));
  result.status = status;
  result.failureReason = failureReason;
  for (auto route : routes) result.candidates.push_back(CandidateFromRoute(route));
  if (scenario) PopulateStabilityResult(outputPath, *scenario, routes, result);
  return SaveRoutingResultJson(outputPath, result, error);
}

}  // namespace weather_routing_headless
