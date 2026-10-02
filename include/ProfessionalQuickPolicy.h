// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <algorithm>
#include <cstdint>

namespace weather_routing {

struct ProfessionalQuickAdmission {
  unsigned savedMiB{};
  unsigned targetMiB{};
  unsigned effectiveMiB{};
  std::uint64_t availableMiB{};
  std::uint64_t requiredBeforeMiB{};
};

// The integrated Quick may search harder than standalone Quick. On a 64-bit
// host, grow the saved allowance up to fourfold (growth target at most 2 GiB;
// larger saved allowances remain larger), but only if
// physical/cgroup headroom can contain that allowance plus three times its
// size and a further 2 GiB for OpenCPN, GRIBs and other applications.
// Unknown headroom never authorizes an increase. Zero means skip Quick.
inline ProfessionalQuickAdmission SelectProfessionalQuickAdmission(
    unsigned savedMiB, std::uint64_t availableMiB,
    unsigned processBits = sizeof(void*) * 8U) {
  ProfessionalQuickAdmission result;
  result.savedMiB = std::clamp(savedMiB, 1U, 4096U);
  result.availableMiB = availableMiB;
  const bool smallProcess = processBits <= 32;
  const unsigned processMaximum = smallProcess ? 192U : 4096U;
  const std::uint64_t reserveMiB = smallProcess ? 256ULL : 2048ULL;
  result.targetMiB = smallProcess
      ? std::min(result.savedMiB, processMaximum)
      : std::min(processMaximum,
                 std::max(result.savedMiB,
                          std::min(2048U, result.savedMiB * 4U)));
  if (!availableMiB) {
    result.effectiveMiB = std::min(result.savedMiB, processMaximum);
  } else if (availableMiB > reserveMiB) {
    const std::uint64_t safeMiB = (availableMiB - reserveMiB) / 4ULL;
    if (safeMiB >= 64)
      result.effectiveMiB = static_cast<unsigned>(
          std::min<std::uint64_t>(result.targetMiB, safeMiB));
  }
  result.requiredBeforeMiB = reserveMiB +
      4ULL * static_cast<std::uint64_t>(result.effectiveMiB);
  return result;
}

}  // namespace weather_routing
