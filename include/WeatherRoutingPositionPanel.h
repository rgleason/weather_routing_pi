#ifndef _WEATHER_ROUTING_POSITION_PANEL_H_
#define _WEATHER_ROUTING_POSITION_PANEL_H_

#include <wx/wx.h>
#include <wx/listctrl.h>

class WeatherRouting;

class WeatherRoutingPositionPanel : public wxPanel {
public:
  WeatherRoutingPositionPanel(wxWindow* parent, WeatherRouting& wr);

  wxListCtrl* m_lPositions;  // Public so WeatherRouting can bind events

  void PopulatePositions();  // Called by WeatherRouting when positions change

private:
  WeatherRouting& m_WR;
};

#endif
