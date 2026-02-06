/***************************************************************************
 *   Copyright (C) 2015 by Sean D'Epagnier                                 *
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 3 of the License, or     *
 *   (at your option) any later version.                                   *
 *                                                                         *
 *   This program is distributed in the hope that it will be useful,       *
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of        *
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the         *
 *   GNU General Public License for more details.                          *
 *                                                                         *
 *   You should have received a copy of the GNU General Public License     *
 *   along with this program; if not, write to the                         *
 *   Free Software Foundation, Inc.,                                       *
 *   51 Franklin Street, Fifth Floor, Boston, MA 02110-1301,  USA.         *
 **************************************************************************/

#ifndef _WEATHER_ROUTING_PLOT_DIALOG_H_
#define _WEATHER_ROUTING_PLOT_DIALOG_H_

#include <list>

#include <wx/fileconf.h>

#include "WeatherRoutingUI.h"

class weather_routing_pi;
class WeatherRouting;
class RouteMapOverlay;

class PlotDialog : public PlotDialogBase {
public:
  PlotDialog(WeatherRouting& weatherrouting);
  ~PlotDialog();

  void InitUI();
  void SetRouteMapOverlay(RouteMapOverlay* routemapoverlay);

private:
  void OnMouseEventsPlot(wxMouseEvent& event);
  void OnPaintPlot(wxPaintEvent& event);
  void OnSizePlot(wxSizeEvent& event) { m_PlotWindow->Refresh(); }
  void OnUpdatePlot(wxScrollEvent& event) { m_PlotWindow->Refresh(); }
  void OnUpdatePlotVariable(wxCommandEvent& event) {
    GetScale();
    m_PlotWindow->Refresh();
  }
  void OnUpdateRoute(wxCommandEvent& event);
  void OnUpdateUI(wxUpdateUIEvent& event);

private:
  double GetValue(PlotData& data, Variable variable);
  int GetType(int var);
  void GetScale();

  wxDateTime m_StartTime;

  double m_mintime, m_maxtime;
  double m_minvalue[3], m_maxvalue[3];

  std::list<PlotData> m_PlotData;

  WeatherRouting& m_WeatherRouting;
};

#endif
