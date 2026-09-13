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
  static std::shared_ptr<ShorelineDataset> Prepare();
  static void Show(wxWindow* parent);
  static bool Busy();
  static wxString Description();
};
}  // namespace weather_routing
