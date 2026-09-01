/***************************************************************************
 *   Copyright (C) 2026 by OpenCPN contributors                            *
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 3 of the License, or     *
 *   (at your option) any later version.                                   *
 ***************************************************************************/

#ifndef _WEATHER_ROUTING_HEADLESS_ROUTING_SCENARIO_JSON_H_
#define _WEATHER_ROUTING_HEADLESS_ROUTING_SCENARIO_JSON_H_

#include <wx/string.h>

#include "weather_routing_engine/RoutingResult.h"
#include "weather_routing_engine/RoutingScenario.h"

namespace weather_routing_headless {

bool LoadRoutingScenarioJson(
    const wxString& path,
    weather_routing_engine::RoutingScenario& scenario,
    wxString& error);

bool SaveRoutingResultJson(
    const wxString& path,
    const weather_routing_engine::RoutingResult& result,
    wxString& error);

}  // namespace weather_routing_headless

#endif
