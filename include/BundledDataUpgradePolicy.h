// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
namespace weather_routing {
// These feature releases retain the 1.24 boats, polars and examples. Do not
// offer to overwrite personal data merely because the plugin version changed.
inline bool CanSkipBundledDataUpdate(int installed, int major, int minor) {
  return installed >= 124 && installed <= 131 && major == 1 &&
         minor >= 25 && minor <= 32;
}
}  // namespace weather_routing
