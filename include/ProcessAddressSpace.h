#ifndef WEATHER_ROUTING_PROCESS_ADDRESS_SPACE_H
#define WEATHER_ROUTING_PROCESS_ADDRESS_SPACE_H

#include <cstdint>
#include <optional>

#ifdef _WIN32
#include <windows.h>
#endif

namespace weather_routing {

// Process virtual address space, not physical RAM or heap ownership. Reserved
// regions consume address space even before any pages are committed.
struct ProcessAddressSpace {
  std::uint64_t total;
  std::uint64_t available;

  std::uint64_t UsedBytes() const { return total - available; }
  double UsedPercent() const {
    return 100.0 * static_cast<double>(UsedBytes()) /
           static_cast<double>(total);
  }
};

inline std::optional<ProcessAddressSpace> MakeProcessAddressSpace(
    std::uint64_t total, std::uint64_t available) {
  if (total == 0 || available > total) return std::nullopt;
  return ProcessAddressSpace{total, available};
}

#ifdef _WIN32
inline std::optional<ProcessAddressSpace> QueryProcessAddressSpace() {
  MEMORYSTATUSEX status{};
  status.dwLength = sizeof(status);
  if (!GlobalMemoryStatusEx(&status)) return std::nullopt;
  // Windows accounts for process bitness and the host executable's
  // LARGEADDRESSAWARE flag. A DLL cannot choose its own address-space limit.
  return MakeProcessAddressSpace(status.ullTotalVirtual,
                                status.ullAvailVirtual);
}
#endif

}  // namespace weather_routing
#endif
