// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "Fixtures.h"
#include <fstream>
#include <sstream>
#include <stdexcept>

namespace fixture {
// The benchmark reads the normal OpenCPN TWA\\TWS table and supplies the same
// native PolarPerformanceModel to every engine. No Atlas-specific
// interpolation.
inline void loadPolar(const std::string& path, wr::RoutingRequest& request) {
  std::ifstream input(path);
  if (!input) throw std::runtime_error("Cannot open polar: " + path);
  std::string line, title;
  if (!std::getline(input, line)) throw std::runtime_error("Empty polar");
  std::istringstream header(line);
  header >> title;
  wr::PerformanceProfile profile;
  profile.identity = path;
  double wind;
  while (header >> wind) {
    if (!std::isfinite(wind) || wind < 0 ||
        (!profile.rows.empty() &&
         wind <= profile.rows.back().trueWindSpeedKnots))
      throw std::runtime_error("Invalid polar wind columns");
    profile.rows.push_back({wind, {}});
  }
  if (profile.rows.size() < 2)
    throw std::runtime_error("Insufficient polar wind columns");
  double previous = -1;
  while (std::getline(input, line)) {
    if (line.empty() || line[0] == '#') continue;
    std::istringstream row(line);
    double angle, speed;
    if (!(row >> angle) || !std::isfinite(angle) || angle < 0 ||
        angle <= previous || angle > 180)
      throw std::runtime_error("Invalid polar angle row");
    previous = angle;
    for (auto& column : profile.rows) {
      if (!(row >> speed) || !std::isfinite(speed) || speed < 0)
        throw std::runtime_error("Invalid polar speed");
      column.points.push_back({angle, speed});
    }
    if (row >> speed) throw std::runtime_error("Extra polar speed column");
  }
  request.vessel.profiles = {std::move(profile)};
}
}  // namespace fixture
