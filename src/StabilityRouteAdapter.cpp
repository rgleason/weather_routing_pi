#include "StabilityRouteAdapter.h"

#include <cmath>

#include <wx/wx.h>

#include "ConstraintChecker.h"
#include "RouteMapOverlay.h"
#include "Utilities.h"
#include "georef.h"

bool CheckStabilitySafetySegment(
    const RouteMapConfiguration& configuration,
    const weather_routing_engine::StabilityPoint& first,
    const weather_routing_engine::StabilityPoint& last) {
  if (!configuration.DetectLand) return true;
  double course = 0.0;
  double distance = 0.0;
  ll_gc_ll_reverse(first.lat, first.lon, last.lat, last.lon, &course,
                   &distance);
  wxString reason;
  RouteMapConfiguration validation = configuration;
  return ConstraintChecker::CheckFinalRouteLandConstraint(
      validation, first.lat, first.lon, last.lat, last.lon, course, &reason);
}

bool CheckStabilitySafetyCell(const RouteMapConfiguration& configuration,
                              double minLat, double minLon, double maxLat,
                              double maxLon) {
  if (!configuration.DetectLand) return true;
  // A 0.05 NM lattice is finer than the current chart-safety grid cell.  Scan
  // full rows and columns so a blocked cell wholly inside the display cell
  // cannot be hidden by checking only its perimeter or diagonals.
  const double spacingNm = 0.05;
  const double centerLat = (minLat + maxLat) * 0.5;
  const double heightNm = std::fabs(maxLat - minLat) * 60.0;
  const double widthNm = std::fabs(maxLon - minLon) * 60.0 *
                         std::max(0.05, std::cos(centerLat * M_PI / 180.0));
  const int rows =
      std::max(1, static_cast<int>(std::ceil(heightNm / spacingNm)));
  const int columns =
      std::max(1, static_cast<int>(std::ceil(widthNm / spacingNm)));
  for (int row = 0; row <= rows; ++row) {
    const double lat = minLat + (maxLat - minLat) * row / rows;
    if (!CheckStabilitySafetySegment(
            configuration, weather_routing_engine::StabilityPoint(lat, minLon),
            weather_routing_engine::StabilityPoint(lat, maxLon)))
      return false;
  }
  for (int column = 0; column <= columns; ++column) {
    const double lon = minLon + (maxLon - minLon) * column / columns;
    if (!CheckStabilitySafetySegment(
            configuration, weather_routing_engine::StabilityPoint(minLat, lon),
            weather_routing_engine::StabilityPoint(maxLat, lon)))
      return false;
  }
  return true;
}

std::vector<weather_routing_engine::StabilityRoute>
BuildValidatedStabilityRoutes(const std::vector<RouteMapOverlay*>& routeMaps) {
  std::vector<weather_routing_engine::StabilityRoute> routes;
  routes.reserve(routeMaps.size());
  for (size_t index = 0; index < routeMaps.size(); ++index) {
    weather_routing_engine::StabilityRoute stability;
    RouteMapOverlay* route = routeMaps[index];
    if (!route) {
      routes.push_back(stability);
      continue;
    }
    RouteMapConfiguration configuration = route->GetConfiguration();
    stability.id = wxString::Format(
        "candidate-%+d", configuration.DepartureTimeOptimizationOffsetMinutes);
    stability.departure = configuration.StartTime;
    stability.eta = route->EndTime();
    if (stability.departure.IsValid() && stability.eta.IsValid())
      stability.elapsedSeconds =
          (stability.eta - stability.departure).GetSeconds().ToLong();
    stability.complete = route->Finished() && route->ReachedDestination();
    if (stability.complete) {
      RouteMapConfiguration validation = configuration;
      stability.finalValidationPass =
          route->ValidatePlottedDestinationRouteLand(validation);
    }
    std::list<PlotData> plot = route->GetPlotData(false);
    for (const PlotData& point : plot)
      stability.points.push_back(
          weather_routing_engine::StabilityPoint(point.lat, point.lon));
    Position* destination = route->GetDestination();
    if (destination &&
        (stability.points.empty() ||
         std::fabs(stability.points.back().lat - destination->lat) > 1e-8 ||
         std::fabs(heading_resolve(stability.points.back().lon -
                                   destination->lon)) > 1e-8))
      stability.points.push_back(weather_routing_engine::StabilityPoint(
          destination->lat, heading_resolve(destination->lon)));
    routes.push_back(stability);
  }
  return routes;
}

std::vector<weather_routing_engine::StabilityRoute>
BuildValidatedMultiLegStabilityRoutes(
    const std::vector<std::vector<RouteMapOverlay*> >& candidateRoutes) {
  std::vector<weather_routing_engine::StabilityRoute> routes;
  routes.reserve(candidateRoutes.size());
  for (size_t candidateIndex = 0; candidateIndex < candidateRoutes.size();
       ++candidateIndex) {
    weather_routing_engine::StabilityRoute stability;
    const std::vector<RouteMapOverlay*>& legs = candidateRoutes[candidateIndex];
    stability.id = wxString::Format("multi-leg-candidate-%lu",
                                    static_cast<unsigned long>(candidateIndex));
    stability.complete = !legs.empty();
    stability.finalValidationPass = !legs.empty();
    RouteMapConfiguration firstConfiguration;
    bool haveFirstConfiguration = false;
    for (RouteMapOverlay* leg : legs) {
      if (!leg) {
        stability.complete = false;
        stability.finalValidationPass = false;
        continue;
      }
      RouteMapConfiguration configuration = leg->GetConfiguration();
      if (!haveFirstConfiguration) {
        firstConfiguration = configuration;
        stability.departure = configuration.StartTime;
        stability.id = wxString::Format(
            "multi-leg-candidate-%+d",
            configuration.DepartureTimeOptimizationOffsetMinutes);
        haveFirstConfiguration = true;
      }
      if (!leg->Finished() || !leg->ReachedDestination())
        stability.complete = false;
      RouteMapConfiguration validation = configuration;
      if (!leg->ValidatePlottedDestinationRouteLand(validation))
        stability.finalValidationPass = false;

      std::vector<weather_routing_engine::StabilityPoint> legPoints;
      const std::list<PlotData> plot = leg->GetPlotData(false);
      for (const PlotData& point : plot)
        legPoints.push_back(
            weather_routing_engine::StabilityPoint(point.lat, point.lon));
      Position* destination = leg->GetDestination();
      if (destination &&
          (legPoints.empty() ||
           std::fabs(legPoints.back().lat - destination->lat) > 1e-8 ||
           std::fabs(heading_resolve(legPoints.back().lon - destination->lon)) >
               1e-8))
        legPoints.push_back(weather_routing_engine::StabilityPoint(
            destination->lat, heading_resolve(destination->lon)));
      if (!stability.points.empty() && !legPoints.empty() &&
          !CheckStabilitySafetySegment(
              firstConfiguration, stability.points.back(), legPoints.front()))
        stability.finalValidationPass = false;
      for (const auto& point : legPoints) {
        if (!stability.points.empty() &&
            std::fabs(stability.points.back().lat - point.lat) <= 1e-8 &&
            std::fabs(heading_resolve(stability.points.back().lon -
                                      point.lon)) <= 1e-8)
          continue;
        stability.points.push_back(point);
      }
      stability.eta = leg->EndTime();
    }
    if (stability.departure.IsValid() && stability.eta.IsValid())
      stability.elapsedSeconds =
          (stability.eta - stability.departure).GetSeconds().ToLong();
    if (stability.points.size() < 2) {
      stability.complete = false;
      stability.finalValidationPass = false;
    }
    routes.push_back(stability);
  }
  return routes;
}
