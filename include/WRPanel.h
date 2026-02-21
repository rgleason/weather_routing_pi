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

  wxButton* m_bCompute = nullptr;
  wxButton* m_bComputeAll = nullptr;
  wxButton* m_bStop = nullptr;
  wxButton* m_bReset = nullptr;
  wxButton* m_bDelete = nullptr;
  wxButton* m_bFilter = nullptr;

  wxButton* m_bSaveAsTrack = nullptr;
  wxButton* m_bSaveAsRoute = nullptr;
  wxButton* m_bExportRoute;


  void SetStopButtonEnabled(bool enabled);
  void SetStatusText(const wxString& text);
  wxGauge* m_gProgress;
 
//  void OnRouteMapUpdate(wxThreadEvent& event);

private:
  WeatherRouting& m_wr;
};
#endif
