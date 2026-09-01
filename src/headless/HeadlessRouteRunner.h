/***************************************************************************
 *   Copyright (C) 2026 by OpenCPN contributors                            *
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 3 of the License, or     *
 *   (at your option) any later version.                                   *
 ***************************************************************************/

#ifndef _WEATHER_ROUTING_HEADLESS_HEADLESS_ROUTE_RUNNER_H_
#define _WEATHER_ROUTING_HEADLESS_HEADLESS_ROUTE_RUNNER_H_

#include <vector>

#include <wx/string.h>

#include "weather_routing_engine/RoutingResult.h"
#include "weather_routing_engine/RoutingScenario.h"

class RouteMapOverlay;

namespace weather_routing_headless {

class HeadlessRouteRunner {
public:
  static bool LoadScenarioFromEnv(
      weather_routing_engine::RoutingScenario& scenario,
      wxString& scenarioPath,
      wxString& outputPath,
      wxString& error);

  static bool WriteStartedResult(
      const wxString& outputPath,
      const weather_routing_engine::RoutingScenario& scenario,
      wxString& error);

  static bool WriteSingleRouteResult(
      const wxString& outputPath,
      const weather_routing_engine::RoutingScenario* scenario,
      const wxString& status,
      const wxString& failureReason,
      const std::vector<RouteMapOverlay*>& routes,
      wxString& error);
};

}  // namespace weather_routing_headless

#endif
