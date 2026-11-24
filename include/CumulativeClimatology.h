// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cmath>
#include <iterator>
#include <vector>

#include "Polar.h"
#include "RoutePoint.h"
#include "Utilities.h"

inline double CumulativeSailingSpeed(
    std::vector<Polar>& polars, std::size_t preferred,
    const climatology_wind_atlas& atlas, double heading, bool optimizeTacking,
    bool accountForCalms, PolarSpeedStatus& status, bool& estimated) {
  double weightedSpeed = 0.0;
  for (std::size_t i = 0; i < std::size(atlas.directions); ++i) {
    const double weight = atlas.directions[i];
    if (weight == 0.0) continue;

    const double angle = std::abs(heading_resolve(heading - atlas.W[i]));
    const auto sailSpeed = [&](std::size_t polar, PolarSpeedStatus& polarStatus,
                               bool& polarEstimated) {
      const double speed = PolarSpeedForRouting(polars, polar, angle,
          atlas.VW[i], &polarStatus, optimizeTacking, &polarEstimated);
      if (polarStatus == POLAR_SPEED_NO_POLAR_DATA ||
          polarStatus == POLAR_SPEED_INVALID_SAIL_CONFIGURATION) return speed;
      const double minimumAngle = polars[polar].MinDegreeStep();
      if (angle >= minimumAngle) return speed;
      // Project the speed made good by tacking onto the trial course.
      return PolarSpeedForRouting(polars, polar, minimumAngle,
          atlas.VW[i], &polarStatus, optimizeTacking, &polarEstimated) *
          std::cos(deg2rad(minimumAngle)) / std::cos(deg2rad(angle));
    };

    bool sectorEstimated = false;
    double speed = sailSpeed(preferred, status, sectorEstimated);
    if (!std::isfinite(speed)) {
      // The prevailing-wind sail plan may cover only part of the atlas.
      // Use a policy-permitted plan for this sector when one is available.
      for (std::size_t polar = 0; polar < polars.size(); ++polar) {
        if (polar == preferred) continue;
        PolarSpeedStatus candidateStatus;
        bool candidateEstimated = false;
        const double candidate = sailSpeed(polar, candidateStatus, candidateEstimated);
        if (std::isfinite(candidate) && (!std::isfinite(speed) || candidate > speed)) {
          speed = candidate;
          sectorEstimated = candidateEstimated;
        }
      }
      if (!std::isfinite(speed)) return speed;
    }
    weightedSpeed += weight * speed;
    estimated |= sectorEstimated;
  }
  status = POLAR_SPEED_SUCCESS;
  return accountForCalms ? weightedSpeed * (1 - atlas.calm) : weightedSpeed;
}
