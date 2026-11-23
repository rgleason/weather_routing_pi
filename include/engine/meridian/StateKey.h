// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "MotionKernel.h"
#include <algorithm>
#include <cmath>
#include <tuple>

namespace meridian_routing {
namespace wr = supercpn::weather_routing;
using StateKey = std::tuple<bool, int, int, long long, int, int, int, int, bool, std::string>;
inline StateKey key(const wr::internal::MotionState& s, wr::TimePoint departure, bool fine) {
  const double nm = fine ? .1 : 1.;
  const auto y = int(std::floor(s.position.latitude * 60 / nm));
  // One longitude scale per latitude row avoids artificial within-cell shear.
  const auto rowLatitude = (y + .5) * nm / 60;
  const auto x = int(std::floor(wr::normalizeLongitude(s.position.longitude) * 60 *
      std::max(.05, std::cos(rowLatitude * std::acos(-1) / 180)) / nm));
  // Resolution is part of identity: .1 NM and 1 NM cells must not alias.
  // Time is retained because earlier arrivals do not dominate later weather.
  return {fine, y, x, (s.time - departure).count() / (fine ? 300 : 1800),
          int(s.tack), int(s.mode), s.sailPlan,
          int(wr::normalizeHeading(s.incomingHeading) / 15),
          s.departureEgressActive, s.profileIdentity};
}
}  // namespace meridian_routing
