// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "StepSchedule.h"

namespace weather_routing::adaptive {
struct WorkForecast {
  bool available{};
  bool endpointModel{};
  double hourlyFraction{}, layers{}, proposals{};
};

// Quick2 proposes 96 headings for at most 128 frontier labels per layer.
// This is a useful offshore cost estimate, rather than a duration-only rule.
// Near endpoints native reductions and pruning still determine actual work.
inline WorkForecast Quick2Work(const Features& f, const StepSchedule& schedule,
                               wr::Duration nominal,
                               bool endpointAware = false) {
  WorkForecast estimate;
  if (!f.sketchResolved || f.hours <= 0 || !std::isfinite(f.hours) ||
      f.path.size() < 2)
    return estimate;
  double length = 0, hourly = 0;
  for (std::size_t i = 1; i < f.path.size(); ++i) {
    const double segment = wr::distanceNm(f.path[i - 1], f.path[i]);
    const double bearing = wr::initialBearingDegrees(f.path[i - 1], f.path[i]);
    for (unsigned sample = 0; sample < 16; ++sample) {
      const auto point = wr::destinationPoint(f.path[i - 1], bearing,
                                              segment * (sample + .5) / 16.);
      length += segment / 16.;
      if (schedule.cap(point) <= wr::Duration{3600}) hourly += segment / 16.;
    }
  }
  estimate.available = true;
  estimate.hourlyFraction = length > 0 ? hourly / length : 1.;
  const double hours = std::clamp(nominal.count() / 3600., 1. / 60., 6.);
  estimate.layers = f.hours * (estimate.hourlyFraction / std::min(1., hours) +
                               (1. - estimate.hourlyFraction) / hours);
  // Small endpoint allowance. The cost model intentionally does not pretend
  // to predict every recovery, heading lattice or terrain-induced detour.
  estimate.layers += 4.;
  if (endpointAware && f.vmgKnots > 0) {
    estimate.endpointModel = true;
    const double localHours = std::min(.25, hours);
    const double endpointHours =
        std::min(f.hours, 15. / f.vmgKnots + 2 * localHours);
    const double middle = std::max(0., f.hours - endpointHours);
    estimate.layers = endpointHours / localHours +
                      middle * (estimate.hourlyFraction / std::min(1., hours) +
                                (1. - estimate.hourlyFraction) / hours) +
                      2.;
  }
  estimate.proposals = estimate.layers * 96 * 128;
  return estimate;
}
}  // namespace weather_routing::adaptive
