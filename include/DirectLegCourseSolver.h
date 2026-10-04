#pragma once

#include <cmath>

namespace weather_routing {

// The speed callback returns the resulting course over ground after applying
// wind, polar and current. Iterate using that course, including across north.
template <typename SpeedCalculation>
bool SolveDirectLegCourse(double bearing, double wind_direction,
                          double& heading, SpeedCalculation calculate) {
  if (!std::isfinite(bearing) || !std::isfinite(wind_direction)) return false;
  double course = wind_direction;
  heading = 0.0;
  for (int attempt = 0; attempt < 10; ++attempt) {
    heading += std::remainder(bearing - course, 360.0);
    if (!calculate(heading, wind_direction + heading, course) ||
        !std::isfinite(course)) return false;
    if (std::fabs(std::remainder(bearing - course, 360.0)) <= 1e-3)
      return true;
  }
  return false;
}

}  // namespace weather_routing
