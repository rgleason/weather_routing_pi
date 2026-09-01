/***************************************************************************
 *   Copyright (C) 2026 by OpenCPN contributors                            *
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 3 of the License, or     *
 *   (at your option) any later version.                                   *
 ***************************************************************************/

#ifndef _WEATHER_ROUTING_ENGINE_ROUTING_RESULT_H_
#define _WEATHER_ROUTING_ENGINE_ROUTING_RESULT_H_

#include <vector>

#include <wx/datetime.h>
#include <wx/string.h>

#include "weather_routing_engine/RoutingDiagnostics.h"

namespace weather_routing_engine {

struct RoutingResultPoint {
  double latitudeDegrees;
  double longitudeDegrees;
  wxDateTime time;

  RoutingResultPoint() : latitudeDegrees(0.0), longitudeDegrees(0.0) {}
  RoutingResultPoint(double latitude, double longitude,
                     const wxDateTime& arrival = wxDateTime())
      : latitudeDegrees(latitude),
        longitudeDegrees(longitude),
        time(arrival) {}
};

struct RoutingCandidateResult {
  wxDateTime departure;
  wxString state;
  wxDateTime eta;
  long elapsedSeconds;
  double distanceNm;
  wxString finalSafety;
  wxString failureReason;
  int offsetMinutes;
  bool reverseRecoveryUsed;
  wxString reverseRecoveryStatus;
  long reverseLayersBuilt;
  long reverseNodesGenerated;
  long reverseNodesFeasible;
  bool reverseConnectionFound;
  wxDateTime reverseConnectionTime;
  wxString reverseFailureReason;
  bool reverseFinalValidationPass;
  std::vector<RoutingResultPoint> route;

  RoutingCandidateResult()
      : elapsedSeconds(-1),
        distanceNm(-1.0),
        offsetMinutes(0),
        reverseRecoveryUsed(false),
        reverseLayersBuilt(-1),
        reverseNodesGenerated(-1),
        reverseNodesFeasible(-1),
        reverseConnectionFound(false),
        reverseFinalValidationPass(false) {}
};

struct StabilityCorridorSummary {
  bool requested;
  wxString status;
  int validRoutes;
  int excludedRoutes;
  int routeFamilies;
  int selectedFamilyId;
  int dominantFamilyRoutes;
  double medianWidthNm;
  double maximumWidthNm;
  double etaSpreadMinutes;
  double innerThreshold;
  double outerThreshold;
  wxString representativeCandidateId;
  wxString geoJsonPath;
  wxString failureReason;
  long calculationTimeMs;

  StabilityCorridorSummary()
      : requested(false),
        status("not_requested"),
        validRoutes(0),
        excludedRoutes(0),
        routeFamilies(0),
        selectedFamilyId(-1),
        dominantFamilyRoutes(0),
        medianWidthNm(0.0),
        maximumWidthNm(0.0),
        etaSpreadMinutes(0.0),
        innerThreshold(0.0),
        outerThreshold(0.0),
        calculationTimeMs(0) {}
};

struct RoutingResult {
  int schemaVersion;
  wxString scenario;
  wxString status;
  wxString failureReason;
  std::vector<RoutingCandidateResult> candidates;
  RoutingDiagnostics diagnostics;
  StabilityCorridorSummary stabilityCorridor;

  RoutingResult() : schemaVersion(1), status("unknown") {}
};

}  // namespace weather_routing_engine

#endif
