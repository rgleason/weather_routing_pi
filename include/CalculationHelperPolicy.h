// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <wx/config.h>

namespace weather_routing {
struct CalculationHelperPreferences {
  bool additional_helpers{false};
  bool parallel_methods{false};
};
inline CalculationHelperPreferences ReadCalculationHelperPreferences(wxConfigBase& config) {
  CalculationHelperPreferences value;
  config.Read("AdditionalCalculationHelpers", &value.additional_helpers, false);
  config.Read("ParallelRoutingMethods", &value.parallel_methods, false);
  return value;
}
inline void WriteCalculationHelperPreferences(wxConfigBase& config,
    const CalculationHelperPreferences& value) {
  config.Write("AdditionalCalculationHelpers", value.additional_helpers);
  config.Write("ParallelRoutingMethods", value.parallel_methods);
}
// Allow roughly one sixty-fourth of currently available RAM for prepared
// samples, estimating 256 bytes per map entry. Unknown memory is conservative.
inline std::size_t PreparedWeatherSampleLimit(bool helpers, bool quick,
                                             std::uint64_t available_mib) {
  const std::size_t requested = helpers && !quick ? 500000u : 32768u;
  const std::size_t memory_limit = available_mib
      ? std::max<std::size_t>(1024u, std::min<std::uint64_t>(available_mib,8192u)*64u)
      : 32768u;
  return std::min(requested,memory_limit);
}
inline std::size_t WeatherSampleEvictionBatch(std::size_t capacity) {
  return std::min<std::size_t>(256u,std::max<std::size_t>(1u,capacity/10u));
}
} // namespace weather_routing
