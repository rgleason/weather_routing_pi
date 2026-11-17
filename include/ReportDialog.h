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

#ifndef _WEATHER_ROUTING_REPORT_DIALOG_H_
#define _WEATHER_ROUTING_REPORT_DIALOG_H_

#include <list>
#include <memory>
#include <wx/timer.h>
#include "CycloneAssessment.h"
class wxSpinCtrl;

#include "WeatherRoutingUI.h"

class RouteMapOverlay;
class WeatherRouting;
#ifdef __OCPN__ANDROID__
class QTextBrowser;
#endif

class ReportDialog : public ReportDialogBase {
public:
  ReportDialog(WeatherRouting& weatherrouting);
  ~ReportDialog() override;
  void ShutdownCycloneAssessment();

  void SetRouteMapOverlays(std::list<RouteMapOverlay*> routemapoverlays);

  bool m_bReportStale;

protected:
  void GenerateRoutesReport();
  void OnInformation(wxCommandEvent& event);
  void OnClose(wxCommandEvent& event) { Hide(); }

private:
  void SetConfigurationReportHTML(const wxString& html);
  void SetRoutesReportHTML(const wxString& html);
#ifdef __OCPN__ANDROID__
  QTextBrowser* m_androidConfigurationReport = nullptr;
  QTextBrowser* m_androidRoutesReport = nullptr;
#endif
  wxString m_DiagnosticText;
  wxButton* m_CopyDiagnostics = nullptr;
  void CopyDiagnostics();
  void StartCycloneAssessment();
  void CycloneParametersChanged();
  void CancelCycloneAssessment(bool clear = false);
  void PollCycloneAssessment();
  std::list<RouteMapOverlay*> m_SelectedRoutes;
  std::unique_ptr<weather_routing::CycloneAssessment> m_CycloneWorker;
  std::shared_ptr<weather_routing::CycloneAssessment::Job> m_CycloneJob;
  wxTimer m_CycloneTimer;
  wxButton *m_AnalyseCyclones = nullptr, *m_CancelCyclones = nullptr;
  wxChoice* m_CycloneScope = nullptr;
  wxSpinCtrl* m_CycloneDays = nullptr;
  wxTextCtrl* m_CycloneResult = nullptr;
  std::uint64_t m_CycloneRevision = 0;
  bool m_PreparingCyclones = false, m_CycloneLogged = false;
  int m_ObservedCycloneDays = 30, m_ObservedCycloneScope = 0;
  wxString FormatTime(wxDateTime t);
  WeatherRouting& m_WeatherRouting;
};

#endif
