#include "supercpn/weather_routing/Engine.h"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace supercpn::weather_routing {
namespace {
constexpr double kEarthRadiusNm = 3440.065;
double radians(double value) { return value * std::numbers::pi / 180.0; }
double degrees(double value) { return value * 180.0 / std::numbers::pi; }
double angularDistance(GeoPoint a, GeoPoint b) {
  const double lat1 = radians(a.latitude);
  const double lat2 = radians(b.latitude);
  const double dlat = lat2 - lat1;
  const double dlon = radians(normalizeLongitude(b.longitude - a.longitude));
  const double h =
      std::pow(std::sin(dlat / 2.0), 2.0) +
      std::cos(lat1) * std::cos(lat2) * std::pow(std::sin(dlon / 2.0), 2.0);
  return 2.0 * std::asin(std::sqrt(std::clamp(h, 0.0, 1.0)));
}
}  // namespace

double normalizeLongitude(double longitude) {
  double value = std::fmod(longitude + 180.0, 360.0);
  if (value < 0.0) value += 360.0;
  return value - 180.0;
}

double normalizeHeading(double heading) {
  double value = std::fmod(heading, 360.0);
  if (value < 0.0) value += 360.0;
  return value;
}

double angularDifferenceDegrees(double a, double b) {
  double value = normalizeHeading(a) - normalizeHeading(b);
  if (value > 180.0) value -= 360.0;
  if (value < -180.0) value += 360.0;
  return value;
}

double distanceNm(GeoPoint a, GeoPoint b) {
  return angularDistance(a, b) * kEarthRadiusNm;
}

double initialBearingDegrees(GeoPoint a, GeoPoint b) {
  const double lat1 = radians(a.latitude);
  const double lat2 = radians(b.latitude);
  const double dlon = radians(normalizeLongitude(b.longitude - a.longitude));
  const double y = std::sin(dlon) * std::cos(lat2);
  const double x = std::cos(lat1) * std::sin(lat2) -
                   std::sin(lat1) * std::cos(lat2) * std::cos(dlon);
  return normalizeHeading(degrees(std::atan2(y, x)));
}

GeoPoint destinationPoint(GeoPoint start, double bearingDegrees,
                          double distanceNmValue) {
  const double angular = distanceNmValue / kEarthRadiusNm;
  const double bearing = radians(bearingDegrees);
  const double lat1 = radians(start.latitude);
  const double lon1 = radians(start.longitude);
  const double lat2 =
      std::asin(std::sin(lat1) * std::cos(angular) +
                std::cos(lat1) * std::sin(angular) * std::cos(bearing));
  const double lon2 =
      lon1 + std::atan2(std::sin(bearing) * std::sin(angular) * std::cos(lat1),
                        std::cos(angular) - std::sin(lat1) * std::sin(lat2));
  return {degrees(lat2), normalizeLongitude(degrees(lon2))};
}

double crossTrackDistanceNm(GeoPoint lineStart, GeoPoint lineEnd,
                            GeoPoint point) {
  if (distanceNm(lineStart, lineEnd) < 1e-9)
    return distanceNm(lineStart, point);
  const double d13 = angularDistance(lineStart, point);
  const double bearing13 = radians(initialBearingDegrees(lineStart, point));
  const double bearing12 = radians(initialBearingDegrees(lineStart, lineEnd));
  return std::asin(std::clamp(std::sin(d13) * std::sin(bearing13 - bearing12),
                              -1.0, 1.0)) *
         kEarthRadiusNm;
}

double vectorDirectionToDegrees(Vector2 vector) {
  if (std::hypot(vector.eastKnots, vector.northKnots) < 1e-12) return 0.0;
  return normalizeHeading(
      degrees(std::atan2(vector.eastKnots, vector.northKnots)));
}

double vectorMagnitudeKnots(Vector2 vector) {
  return std::hypot(vector.eastKnots, vector.northKnots);
}

Vector2 speedDirectionToVector(double speedKnots, double directionToDegrees) {
  const double angle = radians(directionToDegrees);
  return {speedKnots * std::sin(angle), speedKnots * std::cos(angle)};
}

double trueWindAngleDegrees(Vector2 windToward, double courseThroughWater) {
  // Wind-from is 180 degrees opposite the velocity vector.
  const double windFrom =
      normalizeHeading(vectorDirectionToDegrees(windToward) + 180.0);
  return std::abs(angularDifferenceDegrees(courseThroughWater, windFrom));
}

std::string toString(RoutingStatus status) {
  switch (status) {
    case RoutingStatus::Complete:
      return "complete";
    case RoutingStatus::CompleteUsingReverseRecovery:
      return "complete_using_reverse_recovery";
    case RoutingStatus::CompleteUsingFrontierRecovery:
      return "complete_using_frontier_recovery";
    case RoutingStatus::CompleteUsingGraphFallback:
      return "complete_using_graph_fallback";
    case RoutingStatus::NoFeasibleRoute:
      return "no_feasible_route";
    case RoutingStatus::SearchIncomplete:
      return "search_incomplete";
    case RoutingStatus::ResourceLimitReached:
      return "resource_limit_reached";
    case RoutingStatus::WindForecastRequired:
      return "wind_forecast_required";
    case RoutingStatus::CurrentDataRequired:
      return "current_data_required";
    case RoutingStatus::WaveDataRequired:
      return "wave_data_required";
    case RoutingStatus::WeatherCoverageInsufficient:
      return "weather_coverage_insufficient";
    case RoutingStatus::InvalidPolar:
      return "invalid_polar";
    case RoutingStatus::InvalidVesselConfiguration:
      return "invalid_vessel_configuration";
    case RoutingStatus::InvalidStart:
      return "invalid_start";
    case RoutingStatus::InvalidDestination:
      return "invalid_destination";
    case RoutingStatus::ValidationFailure:
      return "validation_failure";
    case RoutingStatus::Cancelled:
      return "cancelled";
    case RoutingStatus::InternalError:
      return "internal_error";
  }
  return "internal_error";
}

std::string toString(SolverPath path) {
  switch (path) {
    case SolverPath::None:
      return "none";
    case SolverPath::AdaptiveIsochrone:
      return "adaptive_isochrone";
    case SolverPath::ReverseRecovery:
      return "reverse_recovery";
    case SolverPath::FrontierRecovery:
      return "frontier_recovery";
    case SolverPath::GraphFallback:
      return "graph_fallback";
  }
  return "none";
}

std::string toString(PropulsionMode mode) {
  switch (mode) {
    case PropulsionMode::Sail:
      return "sail";
    case PropulsionMode::MotorSail:
      return "motor_sail";
    case PropulsionMode::Motor:
      return "motor";
  }
  return "sail";
}

std::string toString(ProfileRole role) {
  switch (role) {
    case ProfileRole::SailOnly:
      return "sail_only";
    case ProfileRole::MotorSailing:
      return "motor_sailing";
    case ProfileRole::MotorOnly:
      return "motor_only";
  }
  return "sail_only";
}

std::string toString(EnvironmentalSource source) {
  switch (source) {
    case EnvironmentalSource::GribForecast:
      return "grib_forecast";
    case EnvironmentalSource::Climatology:
      return "climatology";
    case EnvironmentalSource::XtdCurrentPrediction:
      return "xtd_current_prediction";
    case EnvironmentalSource::NoDataAssumedZero:
      return "no_data_assumed_zero";
    case EnvironmentalSource::SyntheticTestField:
      return "synthetic_test_field";
    case EnvironmentalSource::Missing:
      return "missing";
  }
  return "missing";
}

}  // namespace supercpn::weather_routing
