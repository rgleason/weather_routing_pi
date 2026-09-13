/***************************************************************************
 *   Copyright (C) 2026 by OpenCPN contributors                            *
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 3 of the License, or     *
 *   (at your option) any later version.                                   *
 ***************************************************************************/

#ifndef _WEATHER_ROUTING_ENGINE_ROUTING_SCENARIO_H_
#define _WEATHER_ROUTING_ENGINE_ROUTING_SCENARIO_H_

#include <wx/datetime.h>
#include <wx/string.h>

namespace weather_routing_engine {

struct RoutingScenarioPosition {
  wxString name;
  double lat;
  double lon;

  RoutingScenarioPosition() : lat(0.0), lon(0.0) {}
};

struct RoutingScenarioDepartureOptimization {
  bool enabled;
  int beforeMinutes;
  int afterMinutes;
  int stepMinutes;
  int concurrentRoutes;

  RoutingScenarioDepartureOptimization()
      : enabled(false),
        beforeMinutes(0),
        afterMinutes(0),
        stepMinutes(60),
        concurrentRoutes(0) {}
};

struct RoutingScenarioEnvironment {
  bool useGrib;
  bool hasUseGrib;
  bool useCurrents;
  bool hasUseCurrents;
  bool allowClimatologyFallback;
  bool hasAllowClimatologyFallback;

  RoutingScenarioEnvironment()
      : useGrib(false),
        hasUseGrib(false),
        useCurrents(false),
        hasUseCurrents(false),
        allowClimatologyFallback(false),
        hasAllowClimatologyFallback(false) {}
};

struct RoutingScenarioRouteSettings {
  wxString boatFile;
  bool hasBoatFile;
  int timeStepSeconds;
  bool hasTimeStepSeconds;
  int routingEffortPercent;
  bool hasRoutingEffortPercent;
  double headingFromDegrees;
  bool hasHeadingFromDegrees;
  double headingToDegrees;
  bool hasHeadingToDegrees;
  double headingStepDegrees;
  bool hasHeadingStepDegrees;
  double maxDivertedCourseDegrees;
  bool hasMaxDivertedCourseDegrees;
  double maxCourseAngleDegrees;
  bool hasMaxCourseAngleDegrees;
  double maxSearchAngleDegrees;
  bool hasMaxSearchAngleDegrees;
  double maxTrueWindKnots;
  bool hasMaxTrueWindKnots;
  double maxApparentWindKnots;
  bool hasMaxApparentWindKnots;
  bool optimizeTacking;
  bool hasOptimizeTacking;
  double upwindEfficiency;
  bool hasUpwindEfficiency;
  double downwindEfficiency;
  bool hasDownwindEfficiency;
  double nightEfficiency;
  bool hasNightEfficiency;
  bool useMotor;
  bool hasUseMotor;
  double motorSpeedThresholdKnots;
  bool hasMotorSpeedThresholdKnots;
  double motorSpeedKnots;
  bool hasMotorSpeedKnots;

  RoutingScenarioRouteSettings()
      : hasBoatFile(false),
        timeStepSeconds(0),
        hasTimeStepSeconds(false),
        routingEffortPercent(100),
        hasRoutingEffortPercent(false),
        headingFromDegrees(0.0),
        hasHeadingFromDegrees(false),
        headingToDegrees(0.0),
        hasHeadingToDegrees(false),
        headingStepDegrees(0.0),
        hasHeadingStepDegrees(false),
        maxDivertedCourseDegrees(0.0),
        hasMaxDivertedCourseDegrees(false),
        maxCourseAngleDegrees(0.0),
        hasMaxCourseAngleDegrees(false),
        maxSearchAngleDegrees(0.0),
        hasMaxSearchAngleDegrees(false),
        maxTrueWindKnots(0.0),
        hasMaxTrueWindKnots(false),
        maxApparentWindKnots(0.0),
        hasMaxApparentWindKnots(false),
        optimizeTacking(false),
        hasOptimizeTacking(false),
        upwindEfficiency(0.0),
        hasUpwindEfficiency(false),
        downwindEfficiency(0.0),
        hasDownwindEfficiency(false),
        nightEfficiency(0.0),
        hasNightEfficiency(false),
        useMotor(false),
        hasUseMotor(false),
        motorSpeedThresholdKnots(0.0),
        hasMotorSpeedThresholdKnots(false),
        motorSpeedKnots(0.0),
        hasMotorSpeedKnots(false) {}
};

struct RoutingScenarioSafety {
  wxString mode;  // none, gshhs, chart
  bool enforce;
  bool hasEnforce;
  double landMarginNm;
  bool hasLandMarginNm;
  double minimumDepthM;
  bool hasMinimumDepthM;
  bool persistentCertifiedCacheEnabled;
  bool hasPersistentCertifiedCacheEnabled;

  RoutingScenarioSafety()
      : mode(""),
        enforce(false),
        hasEnforce(false),
        landMarginNm(0.0),
        hasLandMarginNm(false),
        minimumDepthM(0.0),
        hasMinimumDepthM(false),
        persistentCertifiedCacheEnabled(false),
        hasPersistentCertifiedCacheEnabled(false) {}
};

struct RoutingScenarioReverseReachability {
  bool enabled;
  wxDateTime targetTime;
  bool hasTargetTime;
  int searchBackIsochrones;
  bool hasSearchBackIsochrones;
  double horizonHours;
  bool hasHorizonHours;
  bool diagnostics;
  bool hasDiagnostics;

  RoutingScenarioReverseReachability()
      : enabled(false),
        hasTargetTime(false),
        searchBackIsochrones(6),
        hasSearchBackIsochrones(false),
        horizonHours(0.0),
        hasHorizonHours(false),
        diagnostics(false),
        hasDiagnostics(false) {}
};

struct RoutingScenarioStabilityCorridor {
  bool enabled;
  wxString source;
  int minimumRoutes;
  double maxEtaPenaltyMinutes;
  double gridResolutionNm;
  double innerAgreementThreshold;
  double outerAgreementThreshold;
  bool clusterRoutes;
  bool writeGeoJson;

  RoutingScenarioStabilityCorridor()
      : enabled(false),
        source("departureCandidates"),
        minimumRoutes(3),
        maxEtaPenaltyMinutes(120.0),
        gridResolutionNm(0.5),
        innerAgreementThreshold(0.7),
        outerAgreementThreshold(0.4),
        clusterRoutes(true),
        writeGeoJson(false) {}
};

struct RoutingScenario {
  int schemaVersion;
  wxString name;
  RoutingScenarioPosition start;
  RoutingScenarioPosition end;
  wxDateTime startTime;
  RoutingScenarioDepartureOptimization departureOptimization;
  RoutingScenarioEnvironment environment;
  RoutingScenarioRouteSettings route;
  RoutingScenarioSafety safety;
  RoutingScenarioReverseReachability reverseReachability;
  RoutingScenarioStabilityCorridor stabilityCorridor;

  RoutingScenario() : schemaVersion(1) {}
};

}  // namespace weather_routing_engine

#endif
