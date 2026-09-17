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
#include <string>

#include <wx/utils.h>

#ifdef _WIN32
#include <windows.h>
#endif

namespace {

#if defined(__linux__)
std::uint64_t ReadUnsignedFile(const char* path) {
  std::ifstream input(path);
  std::string value;
  if (!(input >> value) || value == "max") return 0;
  try {
    return std::stoull(value);
  } catch (...) {
    return 0;
  }
}

std::uint64_t LinuxMemAvailableBytes() {
  std::ifstream input("/proc/meminfo");
  std::string key;
  std::uint64_t value = 0;
  std::string units;
  while (input >> key >> value >> units) {
    if (key == "MemAvailable:") return value * 1024ULL;
  }
  return 0;
}

std::uint64_t LinuxCgroupAvailableBytes() {
  const std::uint64_t maximum =
      ReadUnsignedFile("/sys/fs/cgroup/memory.max");
  const std::uint64_t current =
      ReadUnsignedFile("/sys/fs/cgroup/memory.current");
  return maximum > current ? maximum - current : 0;
}
#endif

}  // namespace

namespace weather_routing {

std::uint64_t AvailablePhysicalMemoryBytes() {
#ifdef _WIN32
  MEMORYSTATUSEX status{};
  status.dwLength = sizeof(status);
  if (GlobalMemoryStatusEx(&status)) return status.ullAvailPhys;
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

}  // namespace weather_routing
