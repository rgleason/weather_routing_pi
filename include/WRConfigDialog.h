#ifndef _WR_CONFIG_DIALOG_H_
#define _WR_CONFIG_DIALOG_H_

#include <wx/wx.h>
#include "WeatherRouting.h"

class WRConfigDialog : public wxDialog {
public:
  WRConfigDialog(wxWindow* parent, WeatherRoute* route);

private:
  WeatherRoute* m_route;

  // Example controls ? expand later
  wxTextCtrl* m_txtBoatName;
  wxTextCtrl* m_txtStartLat;
  wxTextCtrl* m_txtStartLon;

  void OnOK(wxCommandEvent& event);

  wxDECLARE_EVENT_TABLE();
};

#endif
