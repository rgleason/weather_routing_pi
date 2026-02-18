#ifndef _WEATHER_ROUTING_ROUTEMAP_PANEL_H_
#define _WEATHER_ROUTING_ROUTEMAP_PANEL_H_

class WeatherRouting;

#include <wx/wx.h>
#include <wx/listctrl.h>

class RouteMapPanel : public wxPanel {
public:
  RouteMapPanel(wxWindow* parent, WeatherRouting& wr);

  wxListCtrl* m_lWeatherRoutes;  // Public so WeatherRouting can bind events

  void PopulateRoutes();
  void OnLeftClick(wxMouseEvent& event);

private:
  WeatherRouting& m_WR;
};

#endif
