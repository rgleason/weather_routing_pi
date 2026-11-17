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
#include "WeatherRoutingMessageDialog.h"
#include <cstdint>

#include <wx/wx.h>
#include <wx/spinctrl.h>
#include "WeatherDataProvider.h"
#include "WeatherRoutingWxCompat.h"
#include <wx/clipbrd.h>
#include <wx/dataobj.h>

#include <map>
#include <algorithm>

#include <stdlib.h>
#include <math.h>
#include <time.h>

#include "ocpn_plugin.h"

#include "Utilities.h"
#include "Boat.h"
#include "RouteMapOverlay.h"

#include "WeatherRouting.h"
#include "AndroidDialogHeader.h"
#ifdef __OCPN__ANDROID__
#include <QTextBrowser>
#include <QClipboard>
#include <QVBoxLayout>
#endif

ReportDialog::ReportDialog(WeatherRouting& weatherrouting)
#ifndef __WXOSX__
    : ReportDialogBase(&weatherrouting),
#else
    : ReportDialogBase(
          &weatherrouting, wxID_ANY, _("Weather Route Report"),
          wxDefaultPosition, wxDefaultSize,
          wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER | wxSTAY_ON_TOP),
#endif
      m_WeatherRouting(weatherrouting) {
  m_bReportStale = true;
  SetTitle(_("Route details and report"));
  m_CopyDiagnostics = new wxButton(this, wxID_ANY, _("Copy diagnostic report"));
  m_CopyDiagnostics->SetToolTip(_("Copy the displayed route positions, settings, errors and warnings for support. Nothing is uploaded."));
  m_CopyDiagnostics->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { CopyDiagnostics(); });
#ifndef __OCPN__ANDROID__
  auto* buttons = static_cast<wxFlexGridSizer*>(m_bClose->GetContainingSizer());
  buttons->SetCols(3);
  buttons->Insert(1, m_CopyDiagnostics, 0, wxALL, 5);
#endif
#ifdef __OCPN__ANDROID__
  SetBackgroundColour(wxColour(233, 238, 241));
  GetHandle()->setAutoFillBackground(true);
  // wxHtmlWindow's painted scroll surface leaves stale text on wxQt. Use
  // Qt's document viewport, with one report per full-height touch page.
  m_bInformation->GetContainingSizer()->Detach(m_bInformation);
  for (auto* child : GetChildren()) child->Hide();
  SetSizer(new wxBoxSizer(wxVERTICAL));
  auto* book = new wxNotebook(this, wxID_ANY);
  auto makeReport = [book](const wxString& title) {
    auto* panel = new wxPanel(book, wxID_ANY);
    auto* layout = new QVBoxLayout(panel->GetHandle());
    layout->setContentsMargins(0, 0, 0, 0);
    auto* text = new QTextBrowser(panel->GetHandle());
    text->setStyleSheet("QTextBrowser { font-size: 17pt; padding: 16px; "
                        "color: #173849; background: white; border: none; }");
    text->setTextInteractionFlags(Qt::NoTextInteraction);
    QScroller::grabGesture(text->viewport(), QScroller::TouchGesture);
    layout->addWidget(text);
    book->AddPage(panel, title);
    return text;
  };
  m_androidConfigurationReport = makeReport(_("Selected routes"));
  m_androidRoutesReport = makeReport(_("All routes"));
  GetSizer()->Add(book, 1, wxEXPAND);
  WR_AddAndroidBookPicker(this, book);
  m_bClose->Hide();
  WR_StyleAndroidControls(this);
  wxPanel* header = WR_AddAndroidDoneHeader(this, _("Routing report"));
  m_bInformation->Reparent(header);
  m_bInformation->Show();
  m_bInformation->SetMinSize(wxSize(200, 72));
  header->GetSizer()->Add(m_bInformation, 0, wxALL, 5);
  m_CopyDiagnostics->Reparent(header);
  m_CopyDiagnostics->SetMinSize(wxSize(240, 72));
  m_CopyDiagnostics->Show();
  header->GetSizer()->Add(m_CopyDiagnostics, 0, wxALL, 5);
  wxSize sz = ::wxGetDisplaySize();
  SetSize(0, 0, sz.x, sz.y - 40);
#endif
  auto* cycloneBox =
      new wxStaticBoxSizer(wxVERTICAL, this, _("Historical cyclone tracks"));
  auto* controls = new wxBoxSizer(wxHORIZONTAL);
  m_CycloneScope = new wxChoice(this, wxID_ANY);
  m_CycloneScope->Append(_("Selected completed routes"));
  m_CycloneScope->Append(_("All completed routes"));
  m_CycloneScope->SetSelection(0);
  controls->Add(m_CycloneScope, 1, wxALL, 4);
  controls->Add(new wxStaticText(this, wxID_ANY, _("Seasonal window (days):")),
                0, wxALL | wxALIGN_CENTER_VERTICAL, 4);
  m_CycloneDays = new wxSpinCtrl(this, wxID_ANY, "30", wxDefaultPosition,
                                 wxDefaultSize, wxSP_ARROW_KEYS, 1, 365, 30);
  controls->Add(m_CycloneDays, 0, wxALL, 4);
  cycloneBox->Add(controls, 0, wxEXPAND);
  auto* actions = new wxBoxSizer(wxHORIZONTAL);
  m_AnalyseCyclones =
      new wxButton(this, wxID_ANY, _("Analyse historical cyclone crossings"));
  m_CancelCyclones = new wxButton(this, wxID_ANY, _("Cancel analysis"));
  m_CancelCyclones->Disable();
  actions->Add(m_AnalyseCyclones, 1, wxALL, 4);
  actions->Add(m_CancelCyclones, 0, wxALL, 4);
  cycloneBox->Add(actions, 0, wxEXPAND);
  m_CycloneResult =
      new wxTextCtrl(this, wxID_ANY, _("Not assessed."), wxDefaultPosition,
                     wxSize(-1, WR_FromDIP(this, wxSize(0, 90)).y),
                     wxTE_MULTILINE | wxTE_READONLY);
  cycloneBox->Add(m_CycloneResult, 0, wxALL | wxEXPAND, 4);
#ifdef __OCPN__ANDROID__
  GetSizer()->Add(cycloneBox, 0, wxEXPAND | wxALL, 4);
  WR_StyleAndroidControls(this);
#else
  GetSizer()->Insert(2, cycloneBox, 0, wxEXPAND | wxALL, 4);
  SetSize(wxSize(750, 700));
#endif
  m_AnalyseCyclones->Bind(
      wxEVT_BUTTON, [this](wxCommandEvent&) { StartCycloneAssessment(); });
  m_CancelCyclones->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) {
    CancelCycloneAssessment();
    PollCycloneAssessment();
  });
  m_CycloneScope->Bind(wxEVT_CHOICE,
                       [this](wxCommandEvent&) { CycloneParametersChanged(); });
  m_CycloneDays->Bind(wxEVT_SPINCTRL,
                      [this](wxSpinEvent&) { CycloneParametersChanged(); });
  m_CycloneDays->Bind(wxEVT_TEXT,
                      [this](wxCommandEvent&) { CycloneParametersChanged(); });
  m_CycloneTimer.SetOwner(this);
  Bind(
      wxEVT_TIMER, [this](wxTimerEvent&) { PollCycloneAssessment(); },
      m_CycloneTimer.GetId());
  Bind(wxEVT_SHOW, [this](wxShowEvent& event) {
    if (event.IsShown())
      m_CycloneTimer.Start(100);
    else {
      CancelCycloneAssessment();
      m_CycloneTimer.Stop();
    }
    event.Skip();
  });
  m_CycloneRevision = ClimatologyThreadGuard::Revision();
  Layout();
  SetRouteMapOverlays(std::list<RouteMapOverlay*>());
}

void ReportDialog::SetConfigurationReportHTML(const wxString& html) {
#ifdef __OCPN__ANDROID__
  wxString page = html;
  page.Replace("<dt>", "<br>");
  m_androidConfigurationReport->setHtml(QString::fromUtf8(page.ToUTF8().data()));
#else
  m_htmlConfigurationReport->SetPage(html);
#endif
}

void ReportDialog::SetRoutesReportHTML(const wxString& html) {
#ifdef __OCPN__ANDROID__
  wxString page = html;
  page.Replace("<dt>", "<br>");
  m_androidRoutesReport->setHtml(QString::fromUtf8(page.ToUTF8().data()));
#else
  m_htmlRoutesReport->SetPage(html);
#endif
}

void ReportDialog::SetRouteMapOverlays(
    std::list<RouteMapOverlay*> routemapoverlays) {
  if(m_bReportStale || m_SelectedRoutes!=routemapoverlays)
    CancelCycloneAssessment(true);
  m_SelectedRoutes=routemapoverlays;
  GenerateRoutesReport();
  m_DiagnosticText = _("Weather Routing diagnostic report\nIncludes the displayed route positions and settings; review before sharing.\n");
  m_CopyDiagnostics->Enable(!routemapoverlays.empty());

  if (routemapoverlays.empty()) {
    SetConfigurationReportHTML(_("No Configuration selected."));
    return;
  }

  wxString page;
  for (std::list<RouteMapOverlay*>::iterator it = routemapoverlays.begin();
       it != routemapoverlays.end(); it++) {
    const auto diagnosis = (*it)->GetDiagnostics();
    const wxString diagnosticText = diagnosis.Text();
    m_DiagnosticText += "\n----------------------------------------\n" + diagnosticText;
    wxString diagnosticHtml = diagnosticText;
    diagnosticHtml.Replace("&", "&amp;");
    diagnosticHtml.Replace("<", "&lt;");
    diagnosticHtml.Replace(">", "&gt;");
    diagnosticHtml.Replace("\n", "<br>");
    page += "<h3>" + _("Checks, errors and warnings") + "</h3><p>" + diagnosticHtml + "</p>";
    page += _T("<p>");
    if (!(*it)->ReachedDestination()) {
      wxString reason = (*it)->GetDiagnosticError();
      reason.Replace("&", "&amp;"); reason.Replace("<", "&lt;"); reason.Replace(">", "&gt;");
      reason.Replace("\n", "<br>");
      page += _("Destination not yet reached.") + _T("<br>") + reason;
      continue;
    }

    RouteMapConfiguration c = (*it)->GetConfiguration();
    Position* d = (*it)->GetDestination();

    page += _("Boat Filename") + _T(" ") +
            wxFileName(c.boatFileName).GetName() + _T("<dt>");
    if (c.StartType == RouteMapConfiguration::START_FROM_BOAT) {
      page += _("Route from ") + _("Boat") + _(" to ") + c.End + _T("<dt>");
    } else {
      page += _("Route from ") + c.Start + _(" to ") + c.End + _T("<dt>");
    }
    page += _("Leaving ") + FormatTime((*it)->StartTime()) + _T("<dt>");
    if (d) {
      page += _("Arriving ") + FormatTime((*it)->EndTime()) + _T("<dt>");
      page += _("Duration ") +
              ((*it)->EndTime() - (*it)->StartTime()).Format() + _T("<dt>");
    }
    wxString coverageNotes = (*it)->ModernNativeCoverageNotes();
    coverageNotes.Replace("&", "&amp;");
    coverageNotes.Replace("<", "&lt;");
    coverageNotes.Replace(">", "&gt;");
    coverageNotes.Replace("\n", "<br>");
    if (!coverageNotes.empty()) page += "<p><b>" + coverageNotes + "</b></p>";
    page += _T("<p>");
    double distance =
        DistGreatCircle_Plugin(c.StartLat, c.StartLon, c.EndLat, c.EndLon);
    double distance_sailed = (*it)->RouteInfo(RouteMapOverlay::DISTANCE);
    page += _("Distance sailed: ") +
            wxString::Format(_T("%.2f NMi : %.2f NMi or %.2f%% "),
                             distance_sailed, distance_sailed - distance,
                             (distance_sailed / distance - 1) * 100.0) +
            _("longer than great circle route") + _T("<br>");

    double avgspeed = (*it)->RouteInfo(RouteMapOverlay::AVGSPEED);
    double avgspeedground = (*it)->RouteInfo(RouteMapOverlay::AVGSPEEDGROUND);
    page += _("Average Speed Over Water (SOW)") + wxString(_T(": ")) +
            wxString::Format(_T(" %.1f"), avgspeed) + _T(" ") + _("knots") +
            _T("<dt>");
    page += _("Average Speed Over Ground (SOG)") + wxString(_T(": ")) +
            wxString::Format(_T(" %.1f"), avgspeedground) + _T(" ") +
            _("knots") + _T("<dt>");
    page += _("Average Wind") + wxString(_T(": ")) +
            wxString::Format(_T(" %.1f"),
                             (*it)->RouteInfo(RouteMapOverlay::AVGWIND)) +
            _T(" ") + _("knots") + _T("<dt>");

    // CUSTOMIZATION
    // Add max wind. I think this is more important than the average
    // wind as it gives an indication on how strong will be the sailing
    // conditions, and if the crew has sufficient experience to handle it.
    page += _("Maximum Wind") + wxString(_T(": ")) +
            wxString::Format(_T(" %.1f"),
                             (*it)->RouteInfo(RouteMapOverlay::MAXWIND)) +
            _T(" ") + _("knots") + _T("<dt>");
    ;

    const double averageSwell = (*it)->RouteInfo(RouteMapOverlay::AVGSWELL);
    page += _("Average wave height") + wxString(_T(": ")) +
            (std::isfinite(averageSwell)
                 ? wxString::Format(_T("%.1f "), averageSwell) + _("meters")
                 : _("Unavailable")) + _T("<dt>");
    page +=
        _("Upwind") + wxString(_T(": ")) +
        wxString::Format(_T(" %.1f%%"),
                         (*it)->RouteInfo(RouteMapOverlay::PERCENTAGE_UPWIND)) +
        _T("<dt>");
    double port_starboard = (*it)->RouteInfo(RouteMapOverlay::PORT_STARBOARD);
    page += _("Port/Starboard") + wxString(_T(": ")) +
            (std::isnan(port_starboard)
                 ? wxString(_T("nan"))
                 : wxString::Format(_T("%d/%d"), (int)port_starboard,
                                    100 - (int)port_starboard)) +
            _T("<dt>");

    if (d) {
      page += _("Number of tacks") + wxString::Format(_T(": %d "), d->tacks) +
              _T("<dt>");
      page += _("Number of jibes") + wxString::Format(_T(": %d "), d->jibes) +
              _T("<dt>");
    }

    // CUSTOMIZATION
    // Display sailing comfort in the report
    page += _("Sailing comfort") + wxString(_T(": ")) +
            (*it)->sailingConditionText(
                (*it)->RouteInfo(RouteMapOverlay::COMFORT)) +
            _T("<dt>\n");

    /* determine if currents significantly improve this (boat over ground speed
       average is 10% or more faster than boat over water)  then attempt to
       determine which current based on lat/lon eg, gulf stream, japan, current
       aghulles current etc.. and report it. */
    page += _T("<p>");
    double wspddiff = avgspeedground / avgspeed;
    if (fabs(1 - wspddiff) > .03) {
      page += wxString::Format(
                  _T("%.1f%% "),
                  ((wspddiff > 1 ? wspddiff : 1 / wspddiff) - 1) * 100.0) +
              _("speed change due to ");
      if (wspddiff > 1)
        page += _("favorable");
      else
        page += _("unfavorable");
      page += _(" currents.");
    }
  }

  SetConfigurationReportHTML(page);
}

wxString ReportDialog::FormatTime(wxDateTime t) {
  return m_WeatherRouting.m_SettingsDialog.FormatTime(t,
                                                      _T("%Y-%m-%d %H:%M"));
}

void ReportDialog::GenerateRoutesReport() {
  if (!m_bReportStale) return;
  m_bReportStale = false;

  /* sort configurations interate over each group of configurations
     with the same start and end to determine best and worst times,
     (historical cyclone assessment is requested separately)
  */

  std::map<wxString, std::list<RouteMapOverlay*> > routes;
  for (std::list<WeatherRoute*>::iterator it =
           m_WeatherRouting.m_WeatherRoutes.begin();
       it != m_WeatherRouting.m_WeatherRoutes.end(); it++) {
    if (!(*it)->routemapoverlay->ReachedDestination()) continue;
    wxString route_string = (*it)->Start + _T(" - ") + (*it)->End;
    std::list<RouteMapOverlay*> overlays = routes[route_string];
    overlays.push_back((*it)->routemapoverlay);
    routes[route_string] = overlays;
  }

  if (routes.size() == 0) {
    SetRoutesReportHTML(_("No routes to report yet."));
    return;
  }

  wxString page;
  for (std::map<wxString, std::list<RouteMapOverlay*> >::iterator it =
           routes.begin();
       it != routes.end(); it++) {
    std::list<RouteMapOverlay*> overlays = it->second;
    if (overlays.begin() == overlays.end()) {
      // XXX not possible? but shut up compilers warnings
      continue;
    }
    RouteMapOverlay* first = *overlays.begin();

    RouteMapConfiguration c = first->GetConfiguration();
    page += _T("<p>");
    page += c.Start + _T(" ") + _("to") + _T(" ") + c.End + _T(" ") +
            wxString::Format(
                _T("(%ld ") + wxString(_("configurations")) + _T(")\n"),
                overlays.size());

    /* determine fastest time */
    wxTimeSpan fastest_time;
    RouteMapOverlay* fastest = nullptr;

    std::multimap<wxDateTime, RouteMapOverlay*> sort_by_start;
    bool any_bad = false;
    bool any_good = false;

    for (std::list<RouteMapOverlay*>::iterator it2 = overlays.begin();
         it2 != overlays.end(); it2++) {
      RouteMapOverlay* r = *it2;
      wxTimeSpan current_time = r->EndTime() - r->StartTime();
      sort_by_start.insert(
          std::pair<wxDateTime, RouteMapOverlay*>(r->StartTime(), r));
      if (r == first || current_time < fastest_time) {
        fastest_time = current_time;
        fastest = r;
      }
      if (r->RouteInfo(RouteMapOverlay::PERCENTAGE_UPWIND) > 50) {
        any_bad = true;
      } else {
        any_good = true;
      }
    }

    page += _("<dt>Fastest configuration ") + FormatTime(fastest->StartTime());
    page += wxString(_T(" ")) + _("avg speed") +
            wxString::Format(_T(": %.1f "),
                             fastest->RouteInfo(RouteMapOverlay::AVGSPEED)) +
            _("knots");

    /* determine best times if upwind percentage is below 50 */
    page += _T("<dt>");
    page += _("Best Times (mostly downwind)") + wxString(_T(": "));
    if (any_good == false) {
      // no downwind route
      page += _("none");
    } else if (any_bad == false) {
      // all routes are downwind
      page += _("any");
    } else {
      bool first_print = true;
      std::multimap<wxDateTime, RouteMapOverlay*> reduce_by_start;
      // merge downwind routes in bigger interval
      // assume most routes with same start time are of same kind (downwind or
      // upwind)
      std::multimap<wxDateTime, RouteMapOverlay*>::iterator it =
          sort_by_start.begin();
      while (it != sort_by_start.end()) {
        // remove first upwind routes, from any_good test there's at least one
        // downwind route
        for (; it != sort_by_start.end(); it++) {
          RouteMapOverlay* r = it->second;
          if (r->RouteInfo(RouteMapOverlay::PERCENTAGE_UPWIND) <= 50) {
            break;
          }
        }
        if (it == sort_by_start.end()) break;

        RouteMapOverlay* r = it->second;
        wxDateTime s = r->StartTime();
        wxDateTime e = r->EndTime();
        // merge downwind
        for (; it != sort_by_start.end(); it++) {
          RouteMapOverlay* r = it->second;
          if (r->RouteInfo(RouteMapOverlay::PERCENTAGE_UPWIND) > 50) {
            break;
          }
          e = r->EndTime();
        }
        if (first_print)
          first_print = false;
        else
          page += _(" and ");
        page += m_WeatherRouting.m_SettingsDialog.FormatTime(
                    s, _T("%d %B "), false) +
                _("to") +
                m_WeatherRouting.m_SettingsDialog.FormatTime(
                    e, _T(" %d %B"), false);
      }
    }

    // CUSTOMIZATION
    // Display the best option to travel in order
    // to get the most comfortable sailing
    page += _T("<dt>");
    page += _("Best Sailing Comfort") + wxString(_T(": "));
    wxDateTime best_comfort_date;
    int best_sailing_comfort = 6;
    for (std::multimap<wxDateTime, RouteMapOverlay*>::iterator it3 =
             sort_by_start.begin();
         it3 != sort_by_start.end(); it3++) {
      RouteMapOverlay* r = it3->second;
      if (!best_comfort_date.IsValid() ||
          (best_comfort_date < r->StartTime() &&
           best_sailing_comfort > r->RouteInfo(RouteMapOverlay::COMFORT))) {
        best_comfort_date = r->StartTime();
        best_sailing_comfort = r->RouteInfo(RouteMapOverlay::COMFORT);
      }
    }
    page += RouteMapOverlay::sailingConditionText(best_sailing_comfort);
    page += _T(" on ");
    page += FormatTime(best_comfort_date);

    page += _T("<dt>") + _("Historical cyclone crossings: use the optional assessment below.");
  }

  SetRoutesReportHTML(page);
}

ReportDialog::~ReportDialog() { ShutdownCycloneAssessment(); }

void ReportDialog::ShutdownCycloneAssessment() {
  m_CycloneTimer.Stop();
  CancelCycloneAssessment();
  m_CycloneWorker.reset();
}
void ReportDialog::CancelCycloneAssessment(bool clear) {
  if (m_CycloneJob) m_CycloneJob->Cancel();
  if (clear) {
    m_CycloneJob.reset();
    m_CycloneRevision = ClimatologyThreadGuard::Revision();
    if (m_CycloneResult) m_CycloneResult->ChangeValue(_("Not assessed."));
  }
  if (m_CancelCyclones) m_CancelCyclones->Disable();
  if (m_AnalyseCyclones) m_AnalyseCyclones->Enable(!m_PreparingCyclones);
}
void ReportDialog::CycloneParametersChanged() {
  const int days = m_CycloneDays->GetValue(),
            scope = m_CycloneScope->GetSelection();
  if (days == m_ObservedCycloneDays && scope == m_ObservedCycloneScope) return;
  m_ObservedCycloneDays = days;
  m_ObservedCycloneScope = scope;
  CancelCycloneAssessment(true);
}
void ReportDialog::StartCycloneAssessment() {
  if (m_PreparingCyclones) return;
  m_ObservedCycloneDays = m_CycloneDays->GetValue();
  m_ObservedCycloneScope = m_CycloneScope->GetSelection();
  CancelCycloneAssessment(true);
  if (!ClimatologyThreadGuard::CycloneWorkerSafe()) {
    m_CycloneResult->ChangeValue(
        _("Unavailable: this Climatology provider does not support background "
          "cyclone assessment. Update Climatology. Routing avoidance settings "
          "are unchanged."));
    return;
  }
  auto collect = [&] {
    std::vector<weather_routing::CycloneAssessmentRoute> snapshots;
    for (auto* item : m_WeatherRouting.m_WeatherRoutes) {
      auto* route = item->routemapoverlay;
      if (!route || route->Running() || !route->ReachedDestination()) continue;
      if (m_CycloneScope->GetSelection() == 0 &&
          std::find(m_SelectedRoutes.begin(), m_SelectedRoutes.end(), route) ==
              m_SelectedRoutes.end())
        continue;
      snapshots.push_back(route->SnapshotCycloneAssessment());
    }
    return snapshots;
  };
  auto routes = collect();
  if (routes.empty()) {
    m_CycloneResult->ChangeValue(_("Not assessed: select a completed route."));
    return;
  }
  if (!ClimatologyThreadGuard::IsPrepared(ClimatologyService::CycloneTracks)) {
    // Legacy provider factories create windows. Their one-time initialization
    // stays on the UI thread and happens only after an explicit user request.
    RouteMapConfiguration c;
    c.StartTime = wxDateTime(
        static_cast<time_t>(routes.front().segments.empty()
                                ? 0
                                : routes.front().segments.front().time));
    c.StartLat = 0;
    c.StartLon = 0;
    c.Currents = false;
    c.ClimatologyType = RouteMapConfiguration::AVERAGE;
    c.AvoidCycloneTracks = true;
    c.CycloneMonths = 0;
    c.CycloneDays = 30;
    m_PreparingCyclones = true;
    m_AnalyseCyclones->Disable();
    m_CycloneTimer.Stop();
    WeatherDataProvider::PrepareClimatologyForWorkers(c);
    m_PreparingCyclones = false;
    m_AnalyseCyclones->Enable();
    if (!IsShown()) return;
    m_CycloneTimer.Start(100);
    routes = collect();
  }
  if (routes.empty()) {
    m_CycloneResult->ChangeValue(_("Not assessed: select a completed route."));
    return;
  }
  m_CycloneRevision = ClimatologyThreadGuard::Revision();
  const auto revision = m_CycloneRevision;
  if (!m_CycloneWorker)
    m_CycloneWorker.reset(new weather_routing::CycloneAssessment);
  m_CycloneJob = m_CycloneWorker->Start(
      std::move(routes), m_CycloneDays->GetValue(), revision,
      ClimatologyThreadGuard::CycloneCacheable(),
      [revision](const weather_routing::CycloneAssessmentSegment& s, int days) {
        std::lock_guard<std::recursive_mutex> lock(
            ClimatologyThreadGuard::InvocationMutex());
        if (ClimatologyThreadGuard::Revision() != revision ||
            !ClimatologyThreadGuard::CycloneWorkerSafe() ||
            !ClimatologyThreadGuard::IsPrepared(
                ClimatologyService::CycloneTracks) ||
            !RouteMap::ClimatologyCycloneTrackCrossings)
          return -1;
        return RouteMap::ClimatologyCycloneTrackCrossings(
            s.lat1, s.lon1, s.lat2, s.lon2,
            wxDateTime(static_cast<time_t>(s.time)), days);
      });
  m_CycloneLogged = false;
  m_AnalyseCyclones->Disable();
  m_CancelCyclones->Enable();
  PollCycloneAssessment();
}
void ReportDialog::PollCycloneAssessment() {
  using Status = weather_routing::CycloneAssessmentStatus;
  if (m_CycloneRevision != ClimatologyThreadGuard::Revision()) {
    CancelCycloneAssessment(true);
    return;
  }
  if (!m_CycloneJob) return;
  const auto state = m_CycloneJob->Snapshot();
  wxString text;
  if (state.status == Status::Running)
    text = wxString::Format(
        _("Assessing historical tracks: %lu of %lu segments checked."),
        static_cast<unsigned long>(state.checked),
        static_cast<unsigned long>(state.total));
  else if (state.status == Status::Cancelled)
    text = _("Cancelled. Assessment incomplete.");
  else
    text = _("Historical track assessment finished.");
  for (const auto& route : state.routes) {
    text += "\n" + wxString::FromUTF8(route.label.c_str()) + ": ";
    switch (route.status) {
      case Status::Complete:
        if (!route.crossing_segments)
          text += _("No historical track crossings found.");
        else
          text += wxString::Format(
              _("Historical track crossings found on %lu route segments."),
              static_cast<unsigned long>(route.crossing_segments));
        if(route.crossing_segments) {
          text+=" "+_("Route months with crossings:")+" ";
          bool first=true;
          for(int month=0;month<12;++month)if(route.months[month]) {
            if(!first)text+=", ";first=false;
            text+=wxDateTime::GetMonthName(static_cast<wxDateTime::Month>(month),wxDateTime::Name_Abbr);
          }
        }
        break;
      case Status::Unavailable:
        text += _("Unavailable. No conclusion.");
        break;
      case Status::Cancelled:
        text += _("Cancelled. Assessment incomplete.");
        break;
      case Status::Running:
        text += _("Assessing...");
        break;
      default:
        text += _("Not assessed.");
        break;
    }
  }
  if (m_CycloneResult->GetValue() != text) m_CycloneResult->ChangeValue(text);
  const bool running = state.status == Status::Running;
  m_CancelCyclones->Enable(running);
  m_AnalyseCyclones->Enable(!running);
  if (!running && !m_CycloneLogged) {
    m_CycloneLogged = true;
    wxLogMessage(
        "WR_CYCLONE_ASSESSMENT revision=%llu routes=%lu checked=%lu calls=%lu "
        "cache_hits=%lu total_ms=%.3f max_call_ms=%.3f cancelled=%d",
        static_cast<unsigned long long>(m_CycloneRevision),
        static_cast<unsigned long>(state.routes.size()),
        static_cast<unsigned long>(state.checked),
        static_cast<unsigned long>(state.provider_calls),
        static_cast<unsigned long>(state.cache_hits),
        state.wait_microseconds / 1000.0, state.max_call_microseconds / 1000.0,
        state.status == Status::Cancelled);
  }
}

void ReportDialog::CopyDiagnostics() {
  if (m_DiagnosticText.empty()) return;
#ifdef __OCPN__ANDROID__
  QApplication::clipboard()->setText(QString::fromUtf8(m_DiagnosticText.ToUTF8().data()));
#else
  if (!wxTheClipboard->Open()) {
    WR_MessageBox(_("The clipboard is unavailable. Please try again."),
                  _("Copy diagnostic report"), wxOK | wxICON_WARNING, this);
    return;
  }
  wxTheClipboard->SetData(new wxTextDataObject(m_DiagnosticText));
  wxTheClipboard->Close();
#endif
}

void ReportDialog::OnInformation(wxCommandEvent& event) {
  wxString mes =
      _("Weather Routing Reports gives an overview of a given route based on \
  multiple configurations.\n\n\
  For example using the configuration batch dialog, it is possible to \
  easily generate multiple otherwise identical configurations which \
  have different starting times.");
  mes +=
      _("\
  Once all of these configurations are computed, they become available \
  to the report generator. An overview can be given of the best times, \
  expected speed, and weather conditions. If climatology is available, \
  cyclone risk and additional weather conditions may be described.");

  WR_MessageDialog mdlg(this, mes, _("Weather Routing Report"),
                       wxOK | wxICON_INFORMATION);
  mdlg.ShowModal();
}
