#ifndef _WR_PANEL_H_
#define _WR_PANEL_H_

#include <wx/wx.h>
#include <wx/listctrl.h>

class WeatherRouting;

class WRPanel : public wxPanel {
public:
  WRPanel(wxWindow* parent, WeatherRouting& wr);

  wxListCtrl* m_lWeatherRoutes;

private:
  WeatherRouting& m_wr;
};

#endif
