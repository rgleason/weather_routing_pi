// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <string>

class wxDateTime;

namespace weather_routing {

// The catalogue 1.6 family and the 2.0 / 2026-data release advertise the
// same three process-local callbacks. The typed query API is additive.
struct ClimatologyMessage {
  using Data = bool (*)(int, const wxDateTime&, double, double, double&, double&);
  using WindAtlas = bool (*)(const wxDateTime&, double, double, int&, double*,
                             double*, double&, double&);
  using Cyclones = int (*)(double, double, double, double, const wxDateTime&, int);

  bool valid = false;
  bool supported = false;
  int major = -1;
  int minor = -1;
  std::string dataset = "legacy/unversioned";
  Data data = nullptr;
  WindAtlas wind_atlas = nullptr;
  Cyclones cyclones = nullptr;
};

ClimatologyMessage ReadClimatologyMessage(const std::string& message);

}  // namespace weather_routing
