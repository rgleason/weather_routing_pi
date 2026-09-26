/***************************************************************************
 *
 * Project:  OpenCPN Weather Routing plugin
 * Author:   Sean D'Epagnier
 *
 ***************************************************************************
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
 ***************************************************************************
 *
 */

#include "WeatherRoutingMessageDialog.h"
#include <wx/wx.h>
#include <wx/stdpaths.h>

#include <stdlib.h>
#include <math.h>

#include "Boat.h"
#include "BoatDialog.h"
#include "RouteMapOverlay.h"
#include "weather_routing_pi.h"
#include "WeatherRouting.h"
#include "ConfigurationBatchDialog.h"
#include "AndroidDialogHeader.h"
#include "WeatherRoutingFileDialog.h"
#ifdef __OCPN__ANDROID__
#include <QSpinBox>
#endif

ConfigurationBatchDialog::ConfigurationBatchDialog(WeatherRouting* parent)
#ifndef __WXOSX__
    : ConfigurationBatchDialogBase(parent),
#else
    : ConfigurationBatchDialogBase(
          parent, wxID_ANY, _("Weather Routing Configuration Batch"),
          wxDefaultPosition, wxDefaultSize,
          wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER | wxSTAY_ON_TOP),
#endif
      m_WeatherRouting(*parent) {
  Reset();
#ifdef __OCPN__ANDROID__
  // Preserve the existing controls and handlers, rebuilding their layout for
  // touch. Reparent every retained control before deleting static-box sizers.
  auto page = [](wxPanel* panel) {
    auto* scroll = new wxScrolledWindow(panel, wxID_ANY);
    scroll->SetScrollRate(0, 16);
    scroll->SetSizer(new wxBoxSizer(wxVERTICAL));
    return scroll;
  };
  auto move = [](wxWindow* control, wxScrolledWindow* scroll, int proportion = 0) {
    if (control->GetContainingSizer()) control->GetContainingSizer()->Detach(control);
    control->Reparent(scroll);
    control->SetMaxSize(wxSize(-1, -1));
    scroll->GetSizer()->Add(control, proportion, wxEXPAND | wxALL, 12);
  };
  auto label = [](wxScrolledWindow* scroll, const wxString& text) {
    scroll->GetSizer()->Add(new wxStaticText(scroll, wxID_ANY, text),
                           0, wxEXPAND | wxALL, 12);
  };
  auto field = [&](wxScrolledWindow* scroll, const wxString& title, wxWindow* input) {
    label(scroll, title);
    input->SetMinSize(wxSize(0, 72));
    move(input, scroll);
  };
  auto buttons = [](wxScrolledWindow* scroll, std::initializer_list<wxButton*> controls) {
    auto* row = new wxGridSizer(0, 2, 8, 8);
    for (auto* control : controls) {
      control->GetContainingSizer()->Detach(control);
      control->Reparent(scroll);
      row->Add(control, 1, wxEXPAND | wxALL, 8);
    }
    scroll->GetSizer()->Add(row, 0, wxEXPAND | wxALL, 4);
  };
  auto finish = [](wxPanel* panel, wxScrolledWindow* scroll) {
    for (auto* child : panel->GetChildren()) if (child != scroll) child->Hide();
    auto* root = new wxBoxSizer(wxVERTICAL);
    root->Add(scroll, 1, wxEXPAND | wxRIGHT, 42);
    panel->SetSizer(root, true);
  };
  auto* time = page(m_panel8);
  label(time, _("Departure of the selected route"));
  move(m_stStartDateTime, time);
  buttons(time, {m_button41, m_button38, m_button39, m_button40});
  field(time, _("Repeat departures for: days"), m_tStartDays);
  field(time, _("Additional hours"), m_tStartHours);
  field(time, _("Spacing between departures: days"), m_tStartSpacingDays);
  field(time, _("Additional spacing: hours"), m_tStartSpacingHours);
  finish(m_panel8, time);
  auto* routes = page(m_pRoutes);
  label(routes, _("Choose a departure position, then select its destinations."));
  label(routes, _("Departure position"));
  m_lSources->SetMinSize(wxSize(0, 300));
  move(m_lSources, routes);
  label(routes, _("Destinations (tap to select or deselect)"));
  m_lDestinations->SetMinSize(wxSize(0, 300));
  move(m_lDestinations, routes);
  field(routes, _("Automatically connect positions within this distance (NM)"), m_tMiles);
  buttons(routes, {m_bConnect, m_bDisconnectAll});
  m_staticText1241->Hide();
  m_staticText1251->Hide();
  finish(m_pRoutes, routes);
  auto* boats = page(m_panel9);
  label(boats, _("Boat files used for each route pair and departure"));
  m_lBoats->SetMinSize(wxSize(0, 300));
  move(m_lBoats, boats);
  buttons(boats, {m_bAddBoat, m_bRemoveBoat});
  finish(m_panel9, boats);
  auto* wind = page(m_panel17);
  label(wind, _("Generate variations of forecast wind strength."));
  buttons(wind, {m_button46, m_button47});
  field(wind, _("Minimum wind strength (%)"), m_sWindStrengthMin);
  field(wind, _("Maximum wind strength (%)"), m_sWindStrengthMax);
  field(wind, _("Step between variations (%)"), m_sWindStrengthStep);
  finish(m_panel17, wind);
  GetSizer()->Detach(m_notebookConfigurations);
  auto* actions = new wxPanel(this, wxID_ANY);
  auto* actionSizer = new wxBoxSizer(wxHORIZONTAL);
  for (wxButton* action : {m_bInformation, m_bReset, m_bGenerate}) {
    action->GetContainingSizer()->Detach(action);
    action->Reparent(actions);
    actionSizer->Add(action, 1, wxALL | wxEXPAND, 5);
  }
  actions->SetSizer(actionSizer);
  m_bOK->Hide();
  auto* root = new wxBoxSizer(wxVERTICAL);
  root->Add(actions, 0, wxEXPAND);
  root->Add(m_notebookConfigurations, 1, wxEXPAND);
  SetSizer(root, true);
  WR_StyleAndroidControls(this);
  WR_StyleAndroidFileList(m_lBoats);
  WR_AddAndroidDoneHeader(this, _("Routing batch"));
  WR_AddAndroidBookPicker(this, m_notebookConfigurations, 1);
  const wxSize canvas = GetCanvasByIndex(0)->GetClientSize();
  SetSize(canvas.x - 24, canvas.y - 24);
  CentreOnParent();

#endif
}

void ConfigurationBatchDialog::Render(piDC& dc, PlugIn_ViewPort& vp) {
  if (!IsShown() || m_notebookConfigurations->GetCurrentPage() != m_pRoutes)
    return;

  wxFont mfont(12, wxFONTFAMILY_DEFAULT, wxFONTSTYLE_NORMAL,
               wxFONTWEIGHT_NORMAL);
  dc.SetFont(mfont);

  dc.SetTextForeground(*wxRED);
  //    dc.SetTextBackground(*wxTRANSPARENT);
  //    dc.SetTextBackground(wxColour(0, 0, 0,  wxALPHA_TRANSPARENT));

  dc.SetPen(wxPen(*wxRED, 3));

  for (std::vector<BatchSource*>::iterator it = sources.begin();
       it != sources.end(); it++) {
    wxPoint p1, p2;
    double lat, lon;
    RouteMap::PositionLatLon((*it)->Name, lat, lon);
    GetCanvasPixLL(&vp, &p1, lat, lon);

    dc.DrawText((*it)->Name, p1.x, p1.y);
    dc.DrawCircle(p1.x, p1.y, 5);

    for (std::list<BatchDestination*>::iterator it2 =
             (*it)->destinations.begin();
         it2 != (*it)->destinations.end(); it2++) {
      RouteMap::PositionLatLon((*it2)->Name, lat, lon);
      GetCanvasPixLL(&vp, &p2, lat, lon);

      dc.DrawLine(p1.x, p1.y, p2.x, p2.y);

      wxPoint p3((2 * p1.x + 3 * p2.x) / 5, (2 * p1.y + 3 * p2.y) / 5);
      wxPoint p4((p1.x + p2.x) / 2, (p1.y + p2.y) / 2);
      wxPoint p5((p2.y - p1.y) / 8, (p1.x - p2.x) / 8);
      dc.DrawLine(p3.x, p3.y, p4.x + p5.x, p4.y + p5.y);
      dc.DrawLine(p3.x, p3.y, p4.x - p5.x, p4.y - p5.y);
    }
  }
}

void ConfigurationBatchDialog::AddSource(wxString name) {
  sources.push_back(new BatchSource(name));
  m_lSources->Append(name);
  m_lDestinations->Append(name);
}

void ConfigurationBatchDialog::RemoveSource(wxString name) {
  int i = 0;
  for (std::vector<BatchSource*>::iterator it = sources.begin();
       it != sources.end();) {
    for (std::list<BatchDestination*>::iterator it2 =
             (*it)->destinations.begin();
         it2 != (*it)->destinations.end();)
      if ((*it2)->Name == name)
        it2 = (*it)->destinations.erase(it2);
      else
        it2++;

    if ((*it)->Name == name) {
      delete *it;
      it = sources.erase(it);
      m_lSources->Delete(i);
      m_lDestinations->Delete(i);
    } else {
      it++;
      i++;
    }
  }
}

void ConfigurationBatchDialog::RenameSource(const wxString& oldName,
                                            const wxString& newName) {
  for (size_t i = 0; i < sources.size(); ++i) {
    BatchSource* source = sources[i];
    if (source->Name == oldName) {
      source->Name = newName;
      m_lSources->SetString(i, newName);
      m_lDestinations->SetString(i, newName);
    }
    for (BatchDestination* destination : source->destinations)
      if (destination->Name == oldName) destination->Name = newName;
  }
}

void ConfigurationBatchDialog::ClearSources() {
  for (std::vector<BatchSource*>::iterator it = sources.begin();
       it != sources.end(); it++)
    delete *it;
  sources.clear();

  m_lSources->Clear();
  m_lDestinations->Clear();
}

void ConfigurationBatchDialog::OnOnce(wxCommandEvent& event) {
  m_tStartDays->SetValue(_T("0"));
  m_tStartHours->SetValue(_T("0"));
  m_tStartSpacingDays->SetValue(_T("1"));
  m_tStartSpacingHours->SetValue(_T("0"));
}

void ConfigurationBatchDialog::OnDaily(wxCommandEvent& event) {
  m_tStartDays->SetValue(_T("365"));
  m_tStartHours->SetValue(_T("0"));
  m_tStartSpacingDays->SetValue(_T("1"));
  m_tStartSpacingHours->SetValue(_T("0"));
}

void ConfigurationBatchDialog::OnWeekly(wxCommandEvent& event) {
  m_tStartDays->SetValue(_T("360"));
  m_tStartHours->SetValue(_T("0"));
  m_tStartSpacingDays->SetValue(_T("7"));
  m_tStartSpacingHours->SetValue(_T("0"));
}

void ConfigurationBatchDialog::OnMonthly(wxCommandEvent& event) {
  m_tStartDays->SetValue(_T("340"));
  m_tStartHours->SetValue(_T("0"));
  m_tStartSpacingDays->SetValue(_T("30"));
  m_tStartSpacingHours->SetValue(_T("0"));
}

void ConfigurationBatchDialog::OnSources(wxCommandEvent& event) {
  m_lDestinations->DeselectAll();

  int index = m_lSources->GetSelection();
  if (index < 0) return;

  for (unsigned int i = 0; i < m_lDestinations->GetCount(); i++)
    for (std::list<BatchDestination*>::iterator it =
             sources[index]->destinations.begin();
         it != sources[index]->destinations.end(); it++)
      if ((*it)->Name == m_lDestinations->GetString(i))
        m_lDestinations->SetSelection(i);
}

void ConfigurationBatchDialog::OnDestinations(wxCommandEvent& event) {
  int index = m_lSources->GetSelection();
  if (index < 0) return;

  sources[index]->destinations.clear();
  for (int i = 0; i < (int)m_lDestinations->GetCount(); i++)
    if (m_lDestinations->IsSelected(i)) {
      if (i == index)
        m_lDestinations->Deselect(i);
      else
        sources[index]->destinations.push_back(sources[i]);
    }
}

void ConfigurationBatchDialog::OnRemoveSource(wxCommandEvent& event) {
  int index = m_lSources->GetSelection();
  if (index < 0) return;

  RemoveSource(m_lSources->GetString(index));

  m_lSources->Delete(index);
  m_lDestinations->Delete(index);
}

void ConfigurationBatchDialog::OnClearSources(wxCommandEvent& event) {
  ClearSources();
  m_lSources->Clear();
  m_lDestinations->Clear();
}

void ConfigurationBatchDialog::OnConnect(wxCommandEvent& event) {
  double nm = 0;
  if (!m_tMiles->GetValue().ToDouble(&nm) || !std::isfinite(nm) || nm < 0) {
    WR_MessageBox(_("Enter a non-negative connection distance in nautical miles."),
                 _("Routing batch"), wxOK | wxICON_INFORMATION, this);
    return;
  }

  for (std::vector<BatchSource*>::iterator it = sources.begin();
       it != sources.end(); it++) {
    (*it)->destinations.clear();

    for (std::vector<BatchSource*>::iterator it2 = sources.begin();
         it2 != sources.end(); it2++)
      if (*it != *it2) {
        double distance;

        double lat1, lon1, lat2, lon2;
        RouteMap::PositionLatLon((*it)->Name, lat1, lon1);
        RouteMap::PositionLatLon((*it2)->Name, lat2, lon2);

        DistanceBearingMercator_Plugin(lat1, lon1, lat2, lon2, 0, &distance);
        if (distance <= nm) (*it)->destinations.push_back(*it2);
      }
  }

  m_lSources->SetSelection(-1);
  m_lDestinations->DeselectAll();
}

void ConfigurationBatchDialog::OnDisconnectAll(wxCommandEvent& event) {
  for (std::vector<BatchSource*>::iterator it = sources.begin();
       it != sources.end(); it++)
    (*it)->destinations.clear();
  m_lDestinations->DeselectAll();
}

void ConfigurationBatchDialog::OnAddBoat(wxCommandEvent& event) {
  WR_FileDialog openDialog(
      this, _("Select Polar"), weather_routing_pi::StandardPath() + "boats",
      wxT(""),
      wxT("XML Weather Routing files (*.xml)|*.XML;*.xml|All files (*.*)|*.*"),
      wxFD_OPEN);

  if (openDialog.ShowModal() == wxID_OK &&
      m_lBoats->FindString(openDialog.GetPath(), true) == wxNOT_FOUND)
    m_lBoats->Append(openDialog.GetPath());
}

void ConfigurationBatchDialog::OnRemoveBoat(wxCommandEvent& event) {
  int index = m_lBoats->GetSelection();
  if (index >= 0) m_lBoats->Delete(index);
}

void ConfigurationBatchDialog::On100(wxCommandEvent& event) {
  m_sWindStrengthMin->SetValue(100);
  m_sWindStrengthMax->SetValue(100);
}

void ConfigurationBatchDialog::On80to120(wxCommandEvent& event) {
  m_sWindStrengthMin->SetValue(80);
  m_sWindStrengthMax->SetValue(120);
  m_sWindStrengthStep->SetValue(10);
}

void ConfigurationBatchDialog::OnReset(wxCommandEvent& event) { Reset(); }

void ConfigurationBatchDialog::OnInformation(wxCommandEvent& event) {
  WR_MessageDialog mdlg(
      this, _("Batch mode generates multiple configurations based on \
the selected configuration."),
      _("Weather Routing"), wxOK);
  mdlg.ShowModal();
}

void ConfigurationBatchDialog::OnClose(wxCommandEvent& event) { Hide(); }

void ConfigurationBatchDialog::OnGenerate(wxCommandEvent& event) {
#ifdef __OCPN__ANDROID__
  double days, hours, spacingDays, spacingHours;
  if (!m_tStartDays->GetValue().ToDouble(&days) || !std::isfinite(days) || days < 0 ||
      !m_tStartHours->GetValue().ToDouble(&hours) || !std::isfinite(hours) || hours < 0 ||
      !m_tStartSpacingDays->GetValue().ToDouble(&spacingDays) || !std::isfinite(spacingDays) || spacingDays < 0 ||
      !m_tStartSpacingHours->GetValue().ToDouble(&spacingHours) || !std::isfinite(spacingHours) || spacingHours < 0 ||
      spacingDays * 24 + spacingHours <= 0) {
    WR_MessageBox(_("Enter non-negative durations and a spacing greater than zero."),
                 _("Routing batch"), wxOK | wxICON_INFORMATION, this);
    return;
  }
  const double durationSeconds = std::floor(days * 86400 + hours * 3600);
  const double spacingSeconds = std::floor(spacingDays * 86400 + spacingHours * 3600);
  if (!std::isfinite(durationSeconds) || !std::isfinite(spacingSeconds) ||
      durationSeconds > 2147483647 || spacingSeconds > 2147483647 || spacingSeconds < 1) {
    WR_MessageBox(_("Use durations up to 68 years and a spacing of at least one second."),
                 _("Routing batch"), wxOK | wxICON_INFORMATION, this);
    return;
  }
  for (auto* input : {m_sWindStrengthMin, m_sWindStrengthMax, m_sWindStrengthStep}) {
    auto* spin = qobject_cast<QSpinBox*>(input->GetHandle());
    if (!spin) spin = input->GetHandle()->findChild<QSpinBox*>();
    if (spin) spin->interpretText();
  }
  if (m_sWindStrengthMin->GetValue() > m_sWindStrengthMax->GetValue()) {
    WR_MessageBox(_("The maximum wind strength must be at least the minimum."),
                 _("Routing batch"), wxOK | wxICON_INFORMATION, this);
    return;
  }
  size_t pairs = 0;
  for (const auto* source : sources) pairs += source->destinations.size();
  const size_t templates = m_WeatherRouting.CurrentRouteMaps(true).size();
  const double departures = std::floor(durationSeconds / spacingSeconds) + 1;
  const int variations = (m_sWindStrengthMax->GetValue() - m_sWindStrengthMin->GetValue()) /
                         m_sWindStrengthStep->GetValue() + 1;
  const double count = double(templates) * pairs * m_lBoats->GetCount() * departures * variations;
  if (!std::isfinite(departures) || !std::isfinite(count) || count <= 0 || count > 2147483647) {
    WR_MessageBox(_("Select a route template, connect a route pair and add a boat before generating."),
                 _("Routing batch"), wxOK | wxICON_INFORMATION, this);
    return;
  }
  const wxString preview = wxString::Format(
      _("Create %.0f route configurations?\n\n%lu templates x %lu route pairs x %u boats x %.0f departures x %d wind strengths.\nThe selected templates will be replaced by these routes. Calculations will not start."),
      count, static_cast<unsigned long>(templates), static_cast<unsigned long>(pairs),
      static_cast<unsigned int>(m_lBoats->GetCount()), departures, variations);
  if (WR_MessageBox(preview, _("Generate batch"), wxYES_NO | wxNO_DEFAULT, this) != wxYES) return;
#endif
  m_WeatherRouting.GenerateBatch();
}

void ConfigurationBatchDialog::Reset() {
  m_tStartDays->SetValue(_T("0"));
  m_tStartHours->SetValue(_T("0"));
  m_tStartSpacingDays->SetValue(_T("1"));
  m_tStartSpacingHours->SetValue(_T("0"));

  for (std::vector<BatchSource*>::iterator bit = sources.begin();
       bit != sources.end(); bit++)
    (*bit)->destinations.clear();

  std::list<RouteMapOverlay*> currentroutemaps =
      m_WeatherRouting.CurrentRouteMaps();
  m_lBoats->Clear();
#ifdef __OCPN__ANDROID__
  m_stStartDateTime->SetLabel(currentroutemaps.empty()
      ? _("Select a route on Routes first.")
      : currentroutemaps.front()->GetConfiguration().UseCurrentTime
          ? _("Current time when you generate")
          : m_WeatherRouting.m_SettingsDialog.FormatTime(currentroutemaps.front()->StartTime(), "%Y-%m-%d %H:%M"));
#endif
  for (std::list<RouteMapOverlay*>::iterator it = currentroutemaps.begin();
       it != currentroutemaps.end(); it++) {
    RouteMapConfiguration configuration = (*it)->GetConfiguration();

    for (std::vector<BatchSource*>::iterator bit = sources.begin();
         bit != sources.end(); bit++) {
      if ((*bit)->Name == configuration.Start)
        for (std::vector<BatchSource*>::iterator bit2 = sources.begin();
             bit2 != sources.end(); bit2++)
          if ((*bit2)->Name == configuration.End) {
            bool have = false;
            for (std::list<BatchDestination*>::iterator bit3 =
                     (*bit)->destinations.begin();
                 bit3 != (*bit)->destinations.end(); bit3++)
              if (*bit3 == *bit2) have = true;
            if (!have) (*bit)->destinations.push_back(*bit2);
          }
    }

    if (m_lBoats->FindString(configuration.boatFileName, true) == wxNOT_FOUND)
      m_lBoats->Append(configuration.boatFileName);
  }

  m_sWindStrengthMin->SetValue(100);
  m_sWindStrengthMax->SetValue(100);
  m_sWindStrengthStep->SetValue(10);
}
