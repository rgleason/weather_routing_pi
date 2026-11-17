/***************************************************************************
 *   Copyright (C) 2026 by OpenCPN development team                        *
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 3 of the License, or     *
 *   (at your option) any later version.                                   *
 ***************************************************************************/

#include "ClimatologyThreadGuard.h"

#include <atomic>

namespace {

std::atomic<unsigned> s_prepared{0};
std::atomic<unsigned> s_blockedLogged{0};
std::recursive_mutex s_invocationMutex;
std::atomic<std::uint64_t> s_revision{1};
std::atomic<bool> s_cycloneWorkerSafe{false},s_cycloneCacheable{false};

unsigned ServiceBit(ClimatologyService service) {
  switch (service) {
    case ClimatologyService::Wind:
      return 1u << 0;
    case ClimatologyService::Current:
      return 1u << 1;
    case ClimatologyService::WindAtlas:
      return 1u << 2;
    case ClimatologyService::CycloneTracks:
      return 1u << 3;
  }
  return 0;
}

}  // namespace

std::uint64_t ClimatologyThreadGuard::Revision() { return s_revision.load(); }
void ClimatologyThreadGuard::InvalidateCycloneReports() { ++s_revision; }
void ClimatologyThreadGuard::SetCycloneCapabilities(bool safe,bool revision) {
  s_cycloneWorkerSafe.store(safe);s_cycloneCacheable.store(revision);
}
bool ClimatologyThreadGuard::CycloneWorkerSafe() { return s_cycloneWorkerSafe.load(); }
bool ClimatologyThreadGuard::CycloneCacheable() { return s_cycloneCacheable.load(); }
void ClimatologyThreadGuard::Reset() {
  InvalidateCycloneReports();
  SetCycloneCapabilities(false,false);
  s_prepared.store(0, std::memory_order_release);
  s_blockedLogged.store(0, std::memory_order_release);
}

void ClimatologyThreadGuard::MarkPrepared(ClimatologyService service) {
  s_prepared.fetch_or(ServiceBit(service), std::memory_order_release);
}

bool ClimatologyThreadGuard::IsPrepared(ClimatologyService service) {
  return (s_prepared.load(std::memory_order_acquire) & ServiceBit(service)) !=
         0;
}

bool ClimatologyThreadGuard::CanInvoke(ClimatologyService service,
                                       bool isMainThread) {
  return isMainThread || IsPrepared(service);
}

bool ClimatologyThreadGuard::ShouldLogBlocked(ClimatologyService service) {
  const unsigned bit = ServiceBit(service);
  const unsigned previous =
      s_blockedLogged.fetch_or(bit, std::memory_order_acq_rel);
  return (previous & bit) == 0;
}

std::recursive_mutex& ClimatologyThreadGuard::InvocationMutex() {
  return s_invocationMutex;
}
