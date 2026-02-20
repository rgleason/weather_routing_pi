#ifndef _WR_PANEL_H_
#define _WR_PANEL_H_

#include <wx/wx.h>
#include <wx/listctrl.h>
#include <wx/splitter.h>

class WeatherRouting;

class WRPanel : public wxPanel {
public:
  wxSplitterWindow* m_splitter1;
  WRPanel(wxWindow* parent, WeatherRouting& wr);
 
  wxListCtrl* m_lWeatherRoutes;
  wxButton* m_bCompute;
  wxButton* m_bSaveAsTrack;
  wxButton* m_bSaveAsRoute;
  wxButton* m_bExportRoute;

  void SetStopButtonEnabled(bool enabled);
  void SetStatusText(const wxString& text);
  wxGauge* m_gProgress;
 
//  void OnRouteMapUpdate(wxThreadEvent& event);

private:
  WeatherRouting& m_wr;
};
#endif
