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

#include <wx/wx.h>

#include <stdlib.h>
#include <math.h>

#include <list>

#include "StatisticsDialog.h"
#include "AndroidDialogHeader.h"

#include "RouteMapOverlay.h"

StatisticsDialog::StatisticsDialog(wxWindow* parent)
#ifndef __WXOSX__
    : StatisticsDialogBase(parent)
#else
    : StatisticsDialogBase(parent, wxID_ANY, _("Weather Routing Statistics"),
                           wxDefaultPosition, wxDefaultSize,
                           wxDEFAULT_DIALOG_STYLE | wxSTAY_ON_TOP)
#endif
{
  SetRouteMapOverlays(std::list<RouteMapOverlay*>());
#ifdef __OCPN__ANDROID__
  wxSizer* original = GetSizer();
  for (auto* label : {m_staticText511, m_staticText47, m_staticText53,
                      m_staticText55, m_staticText57, m_staticText90, m_staticText49})
    label->Hide();
  for (auto* child : GetChildren()) child->Hide();
  auto* content = new wxScrolledWindow(this, wxID_ANY);
  content->SetScrollRate(0, 16);
  WR_EnableAndroidScrolling(content);
  auto* rows = new wxBoxSizer(wxVERTICAL);
  content->SetSizer(rows);
  const std::pair<wxString, wxStaticText*> fields[] = {
      {_("State"), m_stState}, {_("Run time"), m_stRunTime},
      {_("Isochrones"), m_stIsoChrons}, {_("Routes"), m_stRoutes},
      {_("Inverted routes"), m_stInvRoutes},
      {_("Skipped positions"), m_stSkipPositions},
      {_("Positions"), m_stPositions}};
  for (const auto& field : fields) {
    field.second->GetContainingSizer()->Detach(field.second);
    field.second->Reparent(content);
    field.second->Show();
    auto* row = new wxBoxSizer(wxHORIZONTAL);
    auto* label = new wxStaticText(content, wxID_ANY, field.first);
    label->GetHandle()->setStyleSheet("QLabel { font-size: 17pt; }");
    field.second->GetHandle()->setStyleSheet(
        "QLabel { font-size: 19pt; font-weight: 600; }");
    row->Add(label, 1, wxALIGN_CENTER_VERTICAL | wxALL, 12);
    row->Add(field.second, 0, wxALIGN_CENTER_VERTICAL | wxALL, 12);
    rows->Add(row, 0, wxEXPAND);
  }
  m_androidSearchStatus = new wxStaticText(content, wxID_ANY, wxEmptyString);
  m_androidSearchStatus->GetHandle()->setStyleSheet("QLabel { font-size: 16pt; }");
  rows->Add(m_androidSearchStatus, 0, wxEXPAND | wxALL, 12);
  auto* root = new wxBoxSizer(wxVERTICAL);
  root->Add(content, 1, wxEXPAND);
  SetSizer(root, false);
  delete original;

  WR_AddAndroidDoneHeader(this, _("Statistics"));
  for (auto* label : {m_stState, m_stIsoChrons, m_stRoutes, m_stInvRoutes,
                      m_stSkipPositions, m_stPositions, m_stRunTime}) {
    wxFont font = label->GetFont();
    font.SetPointSize(17);
    label->SetFont(font);
  }
  wxSize sz = ::wxGetDisplaySize();
  const int width = wxMin(sz.x - 40, 850);
  const int height = wxMin(sz.y - 100, 730);
  SetSize((sz.x - width) / 2, (sz.y - height) / 2, width, height);
#endif
}

void StatisticsDialog::SetRouteMapOverlays(
    std::list<RouteMapOverlay*> routemapoverlays) {
  bool running = false;
  bool complete = !routemapoverlays.empty();
#ifdef __OCPN__ANDROID__
  unsigned long long generated = 0, retained = 0, labels = 0;
#endif
  int tisochrons = 0, troutes = 0, tinvroutes = 0, tskippositions = 0,
      tpositions = 0;
  for (std::list<RouteMapOverlay*>::iterator it = routemapoverlays.begin();
       it != routemapoverlays.end(); it++) {
    if ((*it)->Running()) running = true;
    if (!(*it)->ReachedDestination()) complete = false;
#ifdef __OCPN__ANDROID__
    const auto configuration = (*it)->GetConfiguration();
    generated += configuration.routing_generated_states;
    retained += configuration.routing_retained_states;
    labels += configuration.routing_graph_labels;
#endif

    int isochrones, routes, invroutes, skippositions, positions;
    (*it)->GetStatistics(isochrones, routes, invroutes, skippositions,
                         positions);
    tisochrons += isochrones, troutes += routes, tinvroutes += invroutes;
    tskippositions += skippositions, tpositions += positions;
  }

  m_stState->SetLabel(routemapoverlays.empty() ? _("No Route")
                      : running                ? _("Running")
                      : complete               ? _("Complete")
                                               : _("Stopped"));
  m_stIsoChrons->SetLabel(wxString::Format("%d", tisochrons));
  m_stRoutes->SetLabel(wxString::Format("%d", troutes));
  m_stInvRoutes->SetLabel(wxString::Format("%d", tinvroutes));
  m_stSkipPositions->SetLabel(wxString::Format("%d", tskippositions));
  m_stPositions->SetLabel(wxString::Format("%d", tpositions));
#ifdef __OCPN__ANDROID__
  if (m_androidSearchStatus) {
    WR_WrapAndroidText(m_androidSearchStatus, wxString::Format(
        _("Native search: %llu generated states, %llu retained states, "
          "%llu graph labels.\nContour counters above apply to contour routes."),
        generated, retained, labels), wxMax(200, GetClientSize().x - 50));
    auto* scroll = wxDynamicCast(m_androidSearchStatus->GetParent(), wxScrolledWindow);
    scroll->Layout();
    scroll->FitInside();
  }
#endif

#ifndef __OCPN__ANDROID__
  Fit();
#else
  Layout();
#endif
}

void StatisticsDialog::SetRunTime(wxTimeSpan RunTime) {
  m_stRunTime->SetLabel(RunTime.Format());
}
