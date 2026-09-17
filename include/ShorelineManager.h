// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "ShorelineDataset.h"
#include <wx/string.h>
class wxWindow;
namespace weather_routing {
// Main-thread setup/UI; immutable dataset snapshots are passed to route
// workers.
class ShorelineManager {
public:
  static std::shared_ptr<ShorelineDataset> Prepare(int resolution);
  static int DefaultResolution();
  // Installed optional data or a bundled base resolution can be selected.
  // The full checksum and format validation still runs in Prepare().
  static bool Available(int resolution);
  static void Show(wxWindow* parent);
  static bool Busy();
  static wxString Description(int resolution);
};
}  // namespace weather_routing
