#ifndef _WEATHER_ROUTING_CONFIG_DIALOG_H_
#define _WEATHER_ROUTING_CONFIG_DIALOG_H_

#include <wx/wx.h>
#include "WeatherRouting.h"

class WeatherRoutingConfigDialog : public wxDialog {
public:
  WeatherRoutingConfigDialog(wxWindow* parent, WeatherRoute* route);

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
