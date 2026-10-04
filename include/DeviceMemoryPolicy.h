#ifndef WEATHER_ROUTING_DEVICE_MEMORY_POLICY_H
#define WEATHER_ROUTING_DEVICE_MEMORY_POLICY_H

#include <algorithm>
#include <cstdint>

namespace weather_routing {

// Use physical headroom, never swap/ZRAM, to budget optional retained data.
// The minimum accommodates a small cache; it is not an allocation guarantee.
inline int PressureCacheLimitMiB(int requested, std::uint64_t available_mib,
                                 unsigned share_divisor = 8) {
  if (!available_mib) return requested;
  const auto allowance = std::max<std::uint64_t>(
      16, available_mib / std::max(1U, share_divisor));
  return static_cast<int>(std::min<std::uint64_t>(
      std::max(16, requested), allowance));
}

// A running search already owns its working set. Budget only additional
// workers against current free headroom; never cancel an existing route merely
// because another app starts, or change the routing resolution/effort.
inline int MemoryAwareRouteWorkerLimit(int requested, int running,
                                       std::uint64_t available_mib,
                                       int estimated_route_mib = 256) {
  requested = std::max(1, requested);
  running = std::max(0, running);
  if (!available_mib) {
#ifdef __OCPN__ANDROID__
    return std::max(1, running);
#else
    return requested;
#endif
  }
  const auto additional = available_mib > 256
      ? (available_mib - 256) / std::max(256, estimated_route_mib) : 0;
  const auto limit = std::max<std::uint64_t>(1,
      static_cast<std::uint64_t>(running) + additional);
  return static_cast<int>(std::min<std::uint64_t>(requested, limit));
}

}  // namespace weather_routing
#endif
