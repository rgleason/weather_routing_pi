#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>

namespace supercpn::weather_routing {

constexpr unsigned kDefaultRoutingEffortPercent = 100;
constexpr unsigned kMinimumRoutingEffortPercent = 100;
constexpr unsigned kMaximumRoutingEffortPercent = 400;

inline unsigned normalizeRoutingEffortPercent(unsigned percent) {
  if (percent <= 125) return 100;
  if (percent <= 175) return 150;
  if (percent <= 300) return 200;
  return 400;
}

struct RoutingResourcePolicy {
  std::uint64_t maximumGeneratedStates{};
  std::uint64_t maximumCoastalEndpointGeneratedStates{};
  std::uint64_t maximumForwardGeneratedStates{};
  std::uint64_t maximumReverseCandidates{};
  std::uint64_t maximumReverseBridgeAttempts{};
  std::uint64_t maximumFrontierRecoveryGeneratedStates{};
  std::uint64_t maximumFrontierRecoveryLabels{};
  std::uint64_t maximumGraphGeneratedStates{};
  std::uint64_t maximumRetainedStates{};
  std::uint64_t maximumGraphLabels{};
};

inline std::uint64_t scaleRoutingResource(std::uint64_t base, double scale,
                                          unsigned effortPercent) {
  const long double baselineValue =
      static_cast<long double>(base) * static_cast<long double>(scale);
  const long double maximum = std::numeric_limits<std::uint64_t>::max();
  if (baselineValue >= maximum)
    return std::numeric_limits<std::uint64_t>::max();
  const std::uint64_t baseline =
      static_cast<std::uint64_t>(std::floor(baselineValue + 0.5L));
  const long double value =
      static_cast<long double>(baseline) *
      static_cast<long double>(normalizeRoutingEffortPercent(effortPercent)) /
      100.0L;
  if (value >= maximum) return std::numeric_limits<std::uint64_t>::max();
  return static_cast<std::uint64_t>(std::floor(value + 0.5L));
}

inline RoutingResourcePolicy selectRoutingResourcePolicy(
    double routeDistanceNm, unsigned effortPercent, bool scoutPreview = false) {
  const double finiteDistance =
      std::isfinite(routeDistanceNm) ? routeDistanceNm : 100.0;
  const double distanceScale =
      std::clamp(finiteDistance / 100.0, 0.6, 4.0);
  if (scoutPreview) {
    const std::uint64_t endpoint =
        scaleRoutingResource(4000, 1.0, 100);
    const std::uint64_t forward =
        scaleRoutingResource(60000, distanceScale, 100);
    return {endpoint + forward, endpoint, forward, 0, 0, 0, 0, 0,
            scaleRoutingResource(10000, distanceScale, 100), 1};
  }
  const std::uint64_t endpoint =
      scaleRoutingResource(20000, 1.0, effortPercent);
  const std::uint64_t forward =
      scaleRoutingResource(675000, distanceScale, effortPercent);
  const std::uint64_t frontier =
      scaleRoutingResource(225000, distanceScale, effortPercent);
  const std::uint64_t graph =
      scaleRoutingResource(225000, distanceScale, effortPercent);
  return {endpoint + forward + frontier + graph,
          endpoint,
          forward,
          scaleRoutingResource(512, distanceScale, effortPercent),
          scaleRoutingResource(40960, distanceScale, effortPercent),
          frontier,
          scaleRoutingResource(90000, distanceScale, effortPercent),
          graph,
          scaleRoutingResource(70000, distanceScale, effortPercent),
          scaleRoutingResource(180000, distanceScale, effortPercent)};
}

}  // namespace supercpn::weather_routing
