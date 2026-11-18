// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <cstdint>

namespace weather_routing {
constexpr std::int64_t kWeatherSliceSeconds = 900;
inline std::int64_t RoundedWeatherTime(std::int64_t seconds) {
  return ((seconds + kWeatherSliceSeconds / 2) / kWeatherSliceSeconds) *
         kWeatherSliceSeconds;
}
// Coverage endpoints are inclusive. A bucket crossing either endpoint needs
// exact requested times, so rounding cannot invent forecast availability.
inline bool ForecastBoundaryInBucket(std::int64_t centre, std::int64_t first,
                                     std::int64_t last) {
  if (first < 0 || last < first) return false;
  const auto left = centre - kWeatherSliceSeconds / 2;
  const auto right = centre + kWeatherSliceSeconds / 2;
  return (first >= left && first <= right) ||
         (last >= left && last <= right);
}
inline bool ForecastBucketChanges(unsigned before, unsigned centre,
                                  unsigned after) {
  return before != centre || centre != after;
}
}  // namespace weather_routing
