/***************************************************************************
 *   Copyright (C) 2026 by OpenCPN contributors                            *
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 3 of the License, or     *
 *   (at your option) any later version.                                   *
 ***************************************************************************/

#ifndef _WEATHER_ROUTING_ENGINE_STABILITY_CORRIDOR_H_
#define _WEATHER_ROUTING_ENGINE_STABILITY_CORRIDOR_H_

#include <functional>
#include <vector>

#include <wx/datetime.h>
#include <wx/string.h>

namespace weather_routing_engine {

struct StabilityPoint {
  double lat;
  double lon;

  StabilityPoint() : lat(0.0), lon(0.0) {}
  StabilityPoint(double latitude, double longitude)
      : lat(latitude), lon(longitude) {}
};

struct StabilityRoute {
  wxString id;
  wxDateTime departure;
  wxDateTime eta;
  long elapsedSeconds;
  bool complete;
  bool finalValidationPass;
  std::vector<StabilityPoint> points;

  StabilityRoute()
      : elapsedSeconds(-1), complete(false), finalValidationPass(false) {}
};

struct StabilityCorridorOptions {
  int minimumRoutes;
  double maxEtaPenaltyMinutes;
  double gridResolutionNm;
  double innerAgreementThreshold;
  double outerAgreementThreshold;
  bool clusterRoutes;
  double clusterDistanceNm;
  double routeInfluenceRadiusNm;

  StabilityCorridorOptions();
};

struct StabilityCell {
  int x;
  int y;
  double minLat;
  double minLon;
  double maxLat;
  double maxLon;
  double agreement;

  StabilityCell()
      : x(0),
        y(0),
        minLat(0.0),
        minLon(0.0),
        maxLat(0.0),
        maxLon(0.0),
        agreement(0.0) {}
};

struct RouteFamily {
  int id;
  std::vector<size_t> routeIndices;
  size_t representativeRouteIndex;
  std::vector<StabilityCell> innerCells;
  std::vector<StabilityCell> outerCells;
  double medianWidthNm;
  double maximumWidthNm;
  double etaSpreadMinutes;

  RouteFamily()
      : id(-1),
        representativeRouteIndex(0),
        medianWidthNm(0.0),
        maximumWidthNm(0.0),
        etaSpreadMinutes(0.0) {}
};

struct StabilityCorridorResult {
  bool success;
  int inputRoutes;
  int validRoutes;
  int excludedRoutes;
  int etaExcludedRoutes;
  int unsafeCellsExcluded;
  int rasterCellsUsed;
  long calculationTimeMs;
  wxString failureReason;
  std::vector<RouteFamily> families;

  StabilityCorridorResult()
      : success(false),
        inputRoutes(0),
        validRoutes(0),
        excludedRoutes(0),
        etaExcludedRoutes(0),
        unsafeCellsExcluded(0),
        rasterCellsUsed(0),
        calculationTimeMs(0) {}
};

class StabilityCorridorCalculator {
public:
  typedef std::function<bool(const StabilityPoint&, const StabilityPoint&)>
      SegmentSafetyCheck;
  typedef std::function<bool(double, double, double, double)> CellSafetyCheck;

  static StabilityCorridorResult Calculate(
      const std::vector<StabilityRoute>& routes,
      const StabilityCorridorOptions& options,
      const SegmentSafetyCheck& connectorSafetyCheck = SegmentSafetyCheck(),
      const CellSafetyCheck& cellSafetyCheck = CellSafetyCheck());

  static int FindFamilyForRoute(const StabilityCorridorResult& result,
                                size_t routeIndex);
};

bool WriteStabilityCorridorGeoJson(const wxString& path,
                                   const std::vector<StabilityRoute>& routes,
                                   const StabilityCorridorResult& result,
                                   wxString& error);

}  // namespace weather_routing_engine

#endif
