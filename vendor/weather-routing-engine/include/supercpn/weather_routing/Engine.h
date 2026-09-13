#pragma once

#include <span>

#include "supercpn/weather_routing/Providers.h"

namespace supercpn::weather_routing {

double normalizeLongitude(double longitude);
double normalizeHeading(double heading);
double angularDifferenceDegrees(double a, double b);
double distanceNm(GeoPoint a, GeoPoint b);
double initialBearingDegrees(GeoPoint a, GeoPoint b);
GeoPoint destinationPoint(GeoPoint start, double bearingDegrees,
                          double distanceNm);
double crossTrackDistanceNm(GeoPoint lineStart, GeoPoint lineEnd,
                            GeoPoint point);
double vectorDirectionToDegrees(Vector2 vector);
double vectorMagnitudeKnots(Vector2 vector);
Vector2 speedDirectionToVector(double speedKnots, double directionToDegrees);
double trueWindAngleDegrees(Vector2 windToward, double courseThroughWater);

class RouteValidator {
public:
  RouteValidationResult validate(
      const RoutingRequest& request, const RoutingEnvironment& environment,
      const VesselPerformanceModel& performance, std::span<const RouteLeg> legs,
      RoutingDiagnostics* diagnostics = nullptr) const;
  RouteValidationResult validatePrefix(
      const RoutingRequest& request, const RoutingEnvironment& environment,
      const VesselPerformanceModel& performance, std::span<const RouteLeg> legs,
      RoutingDiagnostics* diagnostics = nullptr) const;
};

class RoutingEngine {
public:
  RoutingPreflightResult preflight(const RoutingRequest& request,
                                   const RoutingEnvironment& environment) const;
  RoutingResult route(const RoutingRequest& request,
                      const RoutingEnvironment& environment) const;
  RoutingResult routeEnsemble(
      const RoutingRequest& request,
      std::span<const RoutingEnvironment> members) const;

private:
  RoutingResult routeMember(const RoutingRequest& request,
                            const RoutingEnvironment& environment) const;
};

std::string toString(RoutingStatus status);
std::string toString(SolverPath path);
std::string toString(PropulsionMode mode);
std::string toString(ProfileRole role);
std::string toString(EnvironmentalSource source);

}  // namespace supercpn::weather_routing
