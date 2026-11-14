// SPDX-License-Identifier: GPL-3.0-or-later
// Public adaptive headers must compile on their own and after Windows' legacy
// near/far macros. This is a compile contract; no runtime fixture is needed.
#ifndef near
#define near
#endif
#ifndef far
#define far
#endif
#include "engine/adaptive/AdvisoryWeather.h"
#include "engine/adaptive/StandardRecovery.h"
#include "engine/adaptive/Controller.h"

static_assert(sizeof(supercpn::weather_routing::AdaptiveStandardDiagnostics) > 0);
