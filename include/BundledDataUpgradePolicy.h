// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
namespace weather_routing {
// These feature releases retain the 1.24 boats, polars and examples. Do not
// offer to overwrite personal data merely because the plugin version changed.
inline bool CanSkipBundledDataUpdate(int installed, int major, int minor) {
  // 1.35 changes toolkit compatibility only; its bundled data is identical
  // to 1.34. Keep older installations on the ordinary data-upgrade path.
  if (installed == 134 && major == 1 && minor == 35) return true;
  // 1.37 changes computation/reporting only; retain 1.36 user data.
  if (installed == 136 && major == 1 && minor == 37) return true;
  // The routing performance releases keep the previous bundled data.
  if (major == 1 && minor == 38) return installed == 136 || installed == 137;
  if (major == 2 && minor == 5) return installed == 203 || installed == 204;
  return installed >= 124 && installed <= 132 && major == 1 &&
         minor >= 25 && minor <= 33;
}
}  // namespace weather_routing
