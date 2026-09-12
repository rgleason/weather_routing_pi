#include "supercpn/weather_routing/Providers.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace supercpn::weather_routing {
namespace {
double rowSpeed(const PolarRow& row, double angle) {
  angle = std::clamp(std::abs(angle), 0.0, 180.0);
  auto upper = std::lower_bound(row.points.begin(), row.points.end(), angle,
                                [](const PolarPoint& point, double value) {
                                  return point.trueWindAngleDegrees < value;
                                });
  if (upper == row.points.begin()) return upper->boatSpeedKnots;
  if (upper == row.points.end()) return row.points.back().boatSpeedKnots;
  const auto& high = *upper;
  const auto& low = *(upper - 1);
  const double span = high.trueWindAngleDegrees - low.trueWindAngleDegrees;
  const double factor =
      span <= 0.0 ? 0.0 : (angle - low.trueWindAngleDegrees) / span;
  return low.boatSpeedKnots +
         factor * (high.boatSpeedKnots - low.boatSpeedKnots);
}

double waveFactor(const std::vector<WavePerformancePoint>& response,
                  double height, double period) {
  if (response.empty()) return 1.0;
  std::vector<std::pair<double, double>> nearest;
  nearest.reserve(response.size());
  for (const auto& point : response) {
    const double distance =
        std::hypot((height - point.significantHeightMetres) / 2.0,
                   (period - point.periodSeconds) / 6.0);
    if (distance < 1e-9) return point.speedFactor;
    nearest.emplace_back(distance, point.speedFactor);
  }
  std::stable_sort(nearest.begin(), nearest.end());
  double weighted = 0.0;
  double weights = 0.0;
  for (std::size_t i = 0; i < std::min<std::size_t>(4, nearest.size()); ++i) {
    const double weight = 1.0 / (nearest[i].first * nearest[i].first);
    weighted += nearest[i].second * weight;
    weights += weight;
  }
  return std::clamp(weighted / weights, 0.0, 1.5);
}
}  // namespace

PolarPerformanceModel::PolarPerformanceModel(VesselConfiguration configuration)
    : configuration_(std::move(configuration)) {}

bool PolarPerformanceModel::validateProfile(const PerformanceProfile& profile,
                                            std::string* reason) {
  auto fail = [&](const std::string& message) {
    if (reason) *reason = message;
    return false;
  };
  if (profile.identity.empty()) return fail("profile identity is empty");
  if (profile.rows.empty()) return fail("profile has no wind-speed rows");
  double previousWind = -1.0;
  for (const auto& row : profile.rows) {
    if (!std::isfinite(row.trueWindSpeedKnots) ||
        row.trueWindSpeedKnots <= previousWind)
      return fail("true wind speeds must be finite and strictly increasing");
    if (row.points.size() < 2)
      return fail("each polar row needs at least two angles");
    double previousAngle = -1.0;
    for (const auto& point : row.points) {
      if (!std::isfinite(point.trueWindAngleDegrees) ||
          !std::isfinite(point.boatSpeedKnots) ||
          point.trueWindAngleDegrees < 0.0 ||
          point.trueWindAngleDegrees > 180.0 ||
          point.trueWindAngleDegrees <= previousAngle ||
          point.boatSpeedKnots < 0.0)
        return fail(
            "polar angles/speeds are invalid or not strictly increasing");
      previousAngle = point.trueWindAngleDegrees;
    }
    previousWind = row.trueWindSpeedKnots;
  }
  if (!std::isfinite(profile.efficiency) || profile.efficiency <= 0.0)
    return fail("profile efficiency must be positive");
  for (const auto& point : profile.wavePerformance)
    if (!std::isfinite(point.significantHeightMetres) ||
        point.significantHeightMetres < 0.0 ||
        !std::isfinite(point.periodSeconds) || point.periodSeconds <= 0.0 ||
        !std::isfinite(point.speedFactor) || point.speedFactor < 0.0 ||
        point.speedFactor > 1.5)
      return fail("wave performance points are invalid");
  return true;
}

bool PolarPerformanceModel::valid(std::string* reason) const {
  const auto* sailing = profile(ProfileRole::SailOnly);
  if (!sailing) {
    if (reason) *reason = "a sail-only profile is required";
    return false;
  }
  for (const auto& item : configuration_.profiles)
    if (!validateProfile(item, reason)) return false;
  if (configuration_.propulsion.allowMotor &&
      configuration_.propulsion.configuredMotorSpeedKnots <= 0.0 &&
      !profile(ProfileRole::MotorOnly)) {
    if (reason)
      *reason = "motor mode needs a motor profile or configured motor speed";
    return false;
  }
  return true;
}

const PerformanceProfile* PolarPerformanceModel::profile(
    ProfileRole role, const std::string& identity) const {
  for (const auto& item : configuration_.profiles)
    if (item.role == role && (identity.empty() || item.identity == identity))
      return &item;
  return nullptr;
}

double PolarPerformanceModel::interpolate(const PerformanceProfile& profile,
                                          double tws, double twa) {
  if (profile.rows.empty()) return std::numeric_limits<double>::quiet_NaN();
  auto upper = std::lower_bound(profile.rows.begin(), profile.rows.end(), tws,
                                [](const PolarRow& row, double value) {
                                  return row.trueWindSpeedKnots < value;
                                });
  if (upper == profile.rows.begin()) return rowSpeed(*upper, twa);
  if (upper == profile.rows.end()) return rowSpeed(profile.rows.back(), twa);
  const auto& high = *upper;
  const auto& low = *(upper - 1);
  const double factor = (tws - low.trueWindSpeedKnots) /
                        (high.trueWindSpeedKnots - low.trueWindSpeedKnots);
  return rowSpeed(low, twa) +
         factor * (rowSpeed(high, twa) - rowSpeed(low, twa));
}

PerformanceCandidate PolarPerformanceModel::evaluateProfile(
    const PerformanceProfile& profileValue, double tws, double twa,
    const WaveSample& waves) const {
  double speed = interpolate(profileValue, tws, twa) * profileValue.efficiency;
  const double directionalEfficiency = twa <= 90.0
                                           ? configuration_.upwindEfficiency
                                           : configuration_.downwindEfficiency;
  speed *= directionalEfficiency;
  if (waves.available) {
    if (!profileValue.wavePerformance.empty())
      speed *= waveFactor(profileValue.wavePerformance,
                          waves.significantHeightMetres, waves.periodSeconds);
    else if (configuration_.wavePenaltyPerMetre > 0.0)
      speed *= std::max(0.0, 1.0 - waves.significantHeightMetres *
                                       configuration_.wavePenaltyPerMetre);
  }
  PerformanceCandidate result;
  result.valid = std::isfinite(speed) && speed > 0.0;
  result.role = profileValue.role;
  result.profileIdentity = profileValue.identity;
  result.sailPlan =
      static_cast<int>(&profileValue - configuration_.profiles.data());
  result.speedThroughWaterKnots = std::max(0.0, speed);
  result.mode = profileValue.role == ProfileRole::SailOnly
                    ? PropulsionMode::Sail
                : profileValue.role == ProfileRole::MotorSailing
                    ? PropulsionMode::MotorSail
                    : PropulsionMode::Motor;
  if (result.mode != PropulsionMode::Sail &&
      configuration_.propulsion.fuelConsumptionLitresPerHour)
    result.fuelLitresPerHour =
        *configuration_.propulsion.fuelConsumptionLitresPerHour;
  return result;
}

PerformanceCandidate PolarPerformanceModel::evaluate(
    PropulsionMode mode, ProfileRole role, const std::string& identity,
    double tws, double twa, const WaveSample& waves) const {
  if (const auto* selected = profile(role, identity)) {
    auto result = evaluateProfile(*selected, tws, twa, waves);
    result.mode = mode;
    return result;
  }
  PerformanceCandidate result;
  result.mode = mode;
  result.role = role;
  result.profileIdentity =
      identity.empty() ? "configured-constant-motor" : identity;
  if (mode == PropulsionMode::Motor && configuration_.propulsion.allowMotor &&
      configuration_.propulsion.configuredMotorSpeedKnots > 0.0) {
    result.valid = true;
    result.speedThroughWaterKnots =
        configuration_.propulsion.configuredMotorSpeedKnots;
  } else if (mode == PropulsionMode::MotorSail &&
             configuration_.propulsion.allowMotorSailing &&
             configuration_.propulsion.motorSailingBoostKnots > 0.0) {
    if (const auto* sail = profile(ProfileRole::SailOnly)) {
      result = evaluateProfile(*sail, tws, twa, waves);
      result.mode = PropulsionMode::MotorSail;
      result.role = ProfileRole::MotorSailing;
      result.profileIdentity = "sail-plus-configured-motor-assist";
      result.speedThroughWaterKnots +=
          configuration_.propulsion.motorSailingBoostKnots;
    }
  }
  if (result.valid && mode != PropulsionMode::Sail &&
      configuration_.propulsion.fuelConsumptionLitresPerHour)
    result.fuelLitresPerHour =
        *configuration_.propulsion.fuelConsumptionLitresPerHour;
  return result;
}

std::vector<PerformanceCandidate> PolarPerformanceModel::candidates(
    double tws, double twa, const WaveSample& waves,
    PropulsionMode previousMode, Duration previousModeDuration) const {
  std::vector<PerformanceCandidate> result;
  PerformanceCandidate sail;
  if (configuration_.propulsion.allowSailing) {
    for (const auto& sailing : configuration_.profiles)
      if (sailing.role == ProfileRole::SailOnly) {
        auto candidate = evaluateProfile(sailing, tws, twa, waves);
        if (!candidate.valid) continue;
        if (!sail.valid || candidate.speedThroughWaterKnots >
                               sail.speedThroughWaterKnots)
          sail = candidate;
        result.push_back(std::move(candidate));
      }
  }
  const bool minimumRunBlocksSail =
      previousMode != PropulsionMode::Sail &&
      previousModeDuration < configuration_.propulsion.minimumMotorRun;
  if (minimumRunBlocksSail)
    result.erase(std::remove_if(result.begin(), result.end(),
                                [](const auto& item) {
                                  return item.mode == PropulsionMode::Sail;
                                }),
                 result.end());

  const double threshold =
      configuration_.propulsion.motorBelowSailingSpeedKnots;
  const bool motorNeeded =
      minimumRunBlocksSail || !sail.valid || threshold <= 0.0 ||
      sail.speedThroughWaterKnots <
          threshold +
              (previousMode == PropulsionMode::Sail
                   ? 0.0
                   : configuration_.propulsion.crossoverHysteresisKnots);
  if (motorNeeded && configuration_.propulsion.allowMotorSailing) {
    auto motorSail = evaluate(PropulsionMode::MotorSail,
                              ProfileRole::MotorSailing, {}, tws, twa, waves);
    if (motorSail.valid) result.push_back(std::move(motorSail));
  }
  if (motorNeeded && configuration_.propulsion.allowMotor) {
    auto motor = evaluate(PropulsionMode::Motor, ProfileRole::MotorOnly, {},
                          tws, twa, waves);
    if (motor.valid) result.push_back(std::move(motor));
  }
  return result;
}

}  // namespace supercpn::weather_routing
