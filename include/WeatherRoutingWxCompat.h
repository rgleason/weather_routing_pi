#pragma once

#include <wx/window.h>

// Dave's Android wxQt build predates wxWindow::FromDIP.  Its window
// coordinates are already pixels (GetContentScaleFactor defaults to 1).
template <typename T>
T WR_FromDIP(wxWindow* window, const T& value) {
#ifdef __OCPN__ANDROID__
  (void)window;
  return value;
#else
  return window->FromDIP(value);
#endif
}
