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

#include "Boat.h"
#include "BoatDialog.h"
#include "RouteMapOverlay.h"
#include "weather_routing_pi.h"
#include "WeatherRouting.h"
#include "FilterRoutesDialog.h"
#include "AndroidDialogHeader.h"

wxString FilterNames[] = {_("Start"), _("Start Time"), _("End"),
                          _("BoatFileName"), _("State")};

FilterRoutesDialog::FilterRoutesDialog(WeatherRouting* weatherrouting)
#ifndef __WXOSX__
    : FilterRoutesDialogBase(weatherrouting),
#else
    : FilterRoutesDialogBase(weatherrouting, wxID_ANY, _("Filter Routes"),
                             wxDefaultPosition, wxDefaultSize,
                             wxDEFAULT_DIALOG_STYLE | wxSTAY_ON_TOP),
#endif
      m_WeatherRouting(weatherrouting) {
  for (int i = 0; i < NUM_FILTERS; i++) m_cCategory->Append(FilterNames[i]);
  m_cCategory->SetSelection(0);
#ifdef __OCPN__ANDROID__
  m_cCategory->SetString(BOATFILENAME, _("Boat"));
  m_cCategory->GetContainingSizer()->Detach(m_cCategory);
  m_tFilter->GetContainingSizer()->Detach(m_tFilter);
  m_button48->GetContainingSizer()->Detach(m_button48);
  for (auto* child : GetChildren()) child->Hide();
  auto* scroll = new wxScrolledWindow(this, wxID_ANY);
  scroll->SetScrollRate(0, 20);
  for (wxWindow* control : {static_cast<wxWindow*>(m_cCategory),
                            static_cast<wxWindow*>(m_tFilter),
                            static_cast<wxWindow*>(m_button48)}) {
    control->Reparent(scroll);
    control->Show();
  }
  auto* contents = new wxBoxSizer(wxVERTICAL);
  auto* guidance = new wxStaticText(scroll, wxID_ANY,
      _("Choose a field and enter text to match. Filters for different fields apply together."));
  contents->Add(guidance, 0, wxEXPAND | wxALL, 16);
  contents->Add(new wxStaticText(scroll, wxID_ANY, _("Field")), 0, wxALL, 16);
  contents->Add(m_cCategory, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 16);
  contents->Add(new wxStaticText(scroll, wxID_ANY, _("Contains")), 0, wxALL, 16);
  m_tFilter->SetHint(_("Text to match"));
  contents->Add(m_tFilter, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 16);
  m_button48->SetLabel(_("Clear all filters"));
  contents->Add(m_button48, 0, wxEXPAND | wxALL, 16);
  contents->AddSpacer(42);
  scroll->SetSizer(contents);
  auto* root = new wxBoxSizer(wxVERTICAL);
  root->Add(scroll, 1, wxEXPAND);
  SetSizer(root, true);
  WR_StyleAndroidControls(this);
  scroll->Bind(wxEVT_SIZE, [scroll, guidance](wxSizeEvent& event) {
    WR_WrapAndroidText(guidance, guidance->GetLabel(),
        wxMax(200, scroll->GetClientSize().x - 64));
    scroll->Layout(); scroll->FitInside(); event.Skip();
  });
  WR_AddAndroidDoneHeader(this, _("Filter routes"));
#endif
}

void FilterRoutesDialog::OnCategory(wxCommandEvent& event) {
  m_tFilter->SetValue(m_Filters[m_cCategory->GetSelection()]);
}

void FilterRoutesDialog::OnFilterText(wxCommandEvent& event) {
  m_Filters[m_cCategory->GetSelection()] = m_tFilter->GetValue();
  ApplyFilters();
}

void FilterRoutesDialog::OnResetAll(wxCommandEvent& event) {
  for (int i = 0; i < NUM_FILTERS; i++) m_Filters[i] = _T("");
  m_tFilter->ChangeValue(wxEmptyString);
  ApplyFilters();
}

void FilterRoutesDialog::OnDone(wxCommandEvent& event) { Hide(); }

void FilterRoutesDialog::ApplyFilters() {
  for (std::list<WeatherRoute*>::iterator it =
           m_WeatherRouting->m_WeatherRoutes.begin();
       it != m_WeatherRouting->m_WeatherRoutes.end(); it++) {
    (*it)->Filtered = 0;
    for (int f = 0; f < NUM_FILTERS; f++) {
      wxString value;
      switch (f) {
        case START:
          value = (*it)->Start;
          break;
        case STARTTIME:
          value = (*it)->StartTime;
          break;
        case END:
          value = (*it)->End;
          break;
        case BOATFILENAME:
          value = (*it)->BoatFilename;
          break;
        case STATE:
          value = (*it)->State;
          break;
      }

      if (!value.Matches(_T("*") + m_Filters[f] + _T("*"))) (*it)->Filtered = 1;
    }
  }

  m_WeatherRouting->RebuildList();
}
