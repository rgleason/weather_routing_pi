/***************************************************************************
 * Copyright (C) 2026 OpenCPN contributors
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 2 of the License, or (at your option)
 * any later version.
 ***************************************************************************/

#include "SystemMemory.h"

#include <algorithm>
#include <cstdint>
#include <fstream>
#include <limits>
#include <optional>
#include <string>

#if defined(_WIN32) && !defined(NOMINMAX)
#define NOMINMAX
#endif
#include <wx/utils.h>

#ifdef _WIN32
#include <windows.h>
#elif defined(__APPLE__)
#include <mach/mach.h>
#include <mach/mach_host.h>
#endif

namespace {

#if defined(__linux__)
std::optional<std::uint64_t> ReadUnsignedFile(const std::string& path) {
  std::ifstream input(path);
  std::string value;
  if (!(input >> value) || value == "max") return std::nullopt;
  try {
    return std::stoull(value);
  } catch (...) {
    return std::nullopt;
  }
}

std::uint64_t LinuxMemAvailableBytes() {
  std::ifstream input("/proc/meminfo");
  std::string key;
  std::uint64_t value = 0;
  std::string units;
  while (input >> key >> value >> units) {
    if (key == "MemAvailable:")
      return std::max<std::uint64_t>(1, value * 1024ULL);
  }
  return 0;
}

std::uint64_t LinuxCgroupAvailableBytes() {
  std::uint64_t available = 0;
  const auto inspect = [&available](const std::string& directory,
                                    bool unified) {
    const auto maximum = ReadUnsignedFile(directory +
        (unified ? "/memory.max" : "/memory.limit_in_bytes"));
    const auto current = ReadUnsignedFile(directory +
        (unified ? "/memory.current" : "/memory.usage_in_bytes"));
    if (!maximum || !current) return;
    // Keep known exhaustion distinct from an unavailable measurement.
    const auto headroom = *maximum > *current ? *maximum - *current : 1;
    available = available ? std::min(available, headroom) : headroom;
  };
  inspect("/sys/fs/cgroup", true);  // Also works in a cgroup namespace.
  std::ifstream groups("/proc/self/cgroup");
  std::string line;
  while (std::getline(groups, line)) {
    const auto first = line.find(':');
    const auto second = first == std::string::npos ? first : line.find(':', first + 1);
    if (second == std::string::npos) continue;
    const auto controllers = line.substr(first + 1, second - first - 1);
    const bool unified = controllers.empty();
    if (!unified && ("," + controllers + ",").find(",memory,") == std::string::npos)
      continue;
    const auto relative = line.substr(second + 1);
    if (relative.empty() || relative.front() != '/' ||
        relative.find("/../") != std::string::npos ||
        relative.compare(relative.size() >= 3 ? relative.size() - 3 : 0, 3, "/..") == 0)
      continue;
    const std::string root = unified ? "/sys/fs/cgroup" : "/sys/fs/cgroup/memory";
    std::string directory = root + (relative == "/" ? "" : relative);
    // A parent may impose a tighter shared limit than the process's group.
    for (;;) {
      inspect(directory, unified);
      if (directory.size() <= root.size()) break;
      directory.resize(directory.find_last_of('/'));
    }
  }
  return available;
}
#endif

}  // namespace

namespace weather_routing {

std::uint64_t AvailablePhysicalMemoryBytes() {
#ifdef _WIN32
  MEMORYSTATUSEX status{};
  status.dwLength = sizeof(status);
  if (GlobalMemoryStatusEx(&status))
    return std::max<std::uint64_t>(1, status.ullAvailPhys);
#elif defined(__APPLE__)
  const mach_port_t host = mach_host_self();
  vm_size_t page_size = 0;
  vm_statistics64_data_t statistics{};
  mach_msg_type_number_t count = HOST_VM_INFO64_COUNT;
  const bool measured = host_page_size(host, &page_size) == KERN_SUCCESS &&
      host_statistics64(host, HOST_VM_INFO64,
          reinterpret_cast<host_info64_t>(&statistics), &count) == KERN_SUCCESS;
  mach_port_deallocate(mach_task_self(), host);
  if (measured) {
    // A conservative physical estimate. Do not count compressed or swap
    // storage, or add speculative/purgeable pages again to their parent sets.
    const auto pages = static_cast<std::uint64_t>(statistics.free_count) +
        statistics.inactive_count;
    return std::max<std::uint64_t>(1, pages * page_size);
  }
#elif defined(__linux__)
  const std::uint64_t physical = LinuxMemAvailableBytes();
  const std::uint64_t cgroup = LinuxCgroupAvailableBytes();
  if (physical && cgroup) return std::min(physical, cgroup);
  if (physical) return physical;
  if (cgroup) return cgroup;
#endif
  const wxMemorySize available = wxGetFreeMemory();
#if wxUSE_LONGLONG
  const wxLongLong_t value = available.GetValue();
  return value > 0 ? static_cast<std::uint64_t>(value) : 0;
#else
  return available > 0 ? static_cast<std::uint64_t>(available) : 0;
#endif
}

std::uint64_t AvailablePhysicalMemoryMiB() {
  const auto bytes = AvailablePhysicalMemoryBytes();
  return bytes ? std::max<std::uint64_t>(1, bytes / (1024ULL * 1024ULL)) : 0;
}

}  // namespace weather_routing
