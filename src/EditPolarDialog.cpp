/***************************************************************************
 *
 * Project:  OpenCPN Weather Routing plugin
 * Author:   Sean D'Epagnier
 *
 ***************************************************************************
 *   Copyright (C) 2016 by Sean D'Epagnier                                 *
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
 */

#include "WeatherRoutingMessageDialog.h"
#include <cstdint>

#include <wx/wx.h>

#include "Boat.h"
#include "BoatDialog.h"
#include "EditPolarDialog.h"
#include "AndroidDialogHeader.h"

enum {
  spTRUE_WIND_SPEED,
  spTRUE_WIND_DIRECTION,
  spAPPARENT_WIND_SPEED,
  spAPPARENT_WIND_DIRECTION,
  spBOAT_SPEED,
  spETA
};

EditPolarDialog::EditPolarDialog(wxWindow* parent)
#ifndef __WXOSX__
    : EditPolarDialogBase(parent),
#else
    : EditPolarDialogBase(
          parent, wxID_ANY, _("Edit Polar"), wxDefaultPosition, wxDefaultSize,
          wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER | wxSTAY_ON_TOP),
#endif
      m_BoatDialog(static_cast<BoatDialog*>(parent)) {
  //	m_gPolar->Connect( wxEVT_GRID_CELL_CHANGED, wxGridEventHandler(
  // EditPolarDialogBase::OnPolarGridChanged ), NULL, this );

  m_lMeasurements->InsertColumn(spTRUE_WIND_SPEED, _("True Wind Speed"));
  m_lMeasurements->InsertColumn(spTRUE_WIND_DIRECTION,
                                _("True Wind Direction"));
  m_lMeasurements->InsertColumn(spAPPARENT_WIND_SPEED,
                                _("Apparent Wind Speed"));
  m_lMeasurements->InsertColumn(spAPPARENT_WIND_DIRECTION,
                                _("Apparent Wind Direction"));
  m_lMeasurements->InsertColumn(spBOAT_SPEED, _("Boat Speed"));
  m_lMeasurements->InsertColumn(spETA, _("Sailboat Transform ETA"));
#ifdef __OCPN__ANDROID__
  GetSizer()->Detach(m_notebook6);
  SetSizer(nullptr, false);
  m_sdbSizer6Save->Hide();
  m_sdbSizer6Cancel->Hide();
  auto* layout = new wxBoxSizer(wxVERTICAL);
  layout->Add(m_notebook6, 1, wxEXPAND);
  SetSizer(layout, true);
  m_panel19->GetSizer()->Detach(m_gPolar);
  m_gPolar->Hide();
  m_panel19->GetSizer()->Detach(m_staticText1351);
  auto* speedLayout = new wxBoxSizer(wxVERTICAL);
  m_androidAngle = new wxChoice(m_panel19, wxID_ANY);
  speedLayout->Add(m_androidAngle, 0, wxEXPAND | wxALL, 12);
  speedLayout->Add(m_staticText1351, 0, wxEXPAND | wxALL, 12);
  m_androidSpeeds = new wxScrolledWindow(m_panel19, wxID_ANY);
  m_androidSpeeds->SetScrollRate(0, 16);
  m_androidSpeedFields = new wxBoxSizer(wxVERTICAL);
  m_androidSpeeds->SetSizer(m_androidSpeedFields);
  speedLayout->Add(m_androidSpeeds, 1, wxEXPAND | wxALL, 12);
  m_panel19->SetSizer(speedLayout, true);
  m_notebook6->SetPageText(0, _("Polar speeds"));
  m_androidAngle->Bind(wxEVT_CHOICE, [this](wxCommandEvent&) {
    if (!ValidateAndroidSpeeds()) {
      m_androidAngle->SetSelection(m_androidDisplayedAngle);
      return;
    }
    RefreshAndroidSpeeds();
  });
  // A compact picker replaces each dense dimension list. The hidden lists
  // remain the controller model used by the existing add/remove handlers.
  auto* dimensionScroll = new wxScrolledWindow(m_panel20, wxID_ANY);
  dimensionScroll->SetMinSize(wxSize(0, 0));
  dimensionScroll->SetScrollRate(0, 16);
  auto* dimensionContent = new wxPanel(dimensionScroll, wxID_ANY);
  auto* dimensionFields = new wxBoxSizer(wxVERTICAL);
  auto addDimension = [&](const wxString& title, wxTextCtrl* input,
      wxListBox* list, wxButton* add, wxButton* remove, wxChoice*& choice) {
    auto* label = new wxStaticText(dimensionContent, wxID_ANY, title);
    dimensionFields->Add(label, 0, wxEXPAND | wxALL, 12);
    for (wxWindow* control : {static_cast<wxWindow*>(input),
         static_cast<wxWindow*>(add), static_cast<wxWindow*>(remove)}) {
      control->GetContainingSizer()->Detach(control);
      control->Reparent(dimensionContent);
    }
    dimensionFields->Add(input, 0, wxEXPAND | wxALL, 12);
    choice = new wxChoice(dimensionContent, wxID_ANY);
    dimensionFields->Add(choice, 0, wxEXPAND | wxALL, 12);
    auto* actions = new wxBoxSizer(wxHORIZONTAL);
    actions->Add(add, 1, wxEXPAND | wxALL, 8);
    actions->Add(remove, 1, wxEXPAND | wxALL, 8);
    dimensionFields->Add(actions, 0, wxEXPAND | wxALL, 4);
    choice->Bind(wxEVT_CHOICE, [list, choice](wxCommandEvent&) {
      list->SetSelection(choice->GetSelection());
    });
  };
  addDimension(_("True wind angles (degrees)"), m_tTrueWindAngle,
      m_lTrueWindAngles, m_bAddTrueWindAngle, m_bRemoveTrueWindAngle, m_androidDimensionAngle);
  addDimension(_("True wind speeds (knots)"), m_tTrueWindSpeed,
      m_lTrueWindSpeeds, m_bAddTrueWindSpeed, m_bRemoveTrueWindSpeed, m_androidDimensionWind);
  dimensionContent->SetSizer(dimensionFields);
  auto* dimensionViewport = new wxBoxSizer(wxVERTICAL);
  dimensionViewport->Add(dimensionContent, 1, wxEXPAND);
  dimensionScroll->SetSizer(dimensionViewport);
  auto* dimensionLayout = new wxBoxSizer(wxVERTICAL);
  dimensionLayout->Add(dimensionScroll, 1, wxEXPAND | wxRIGHT, 42);
  for (auto* list : {m_lTrueWindAngles, m_lTrueWindSpeeds}) {
    list->GetContainingSizer()->Detach(list);
    list->Reparent(m_panel20);
    list->Hide();
  }
  // wxStaticBoxSizer owns its box, including children still parented to it.
  // Hide obsolete windows while alive, before replacing the old sizer.
  for (auto* child : m_panel20->GetChildren())
    if (child != dimensionScroll) child->Hide();
  m_panel20->SetSizer(dimensionLayout, true);

  // Measurements are a full page. The desktop boat-characteristic subpage
  // consists of explicitly unused controls and has no generation handler.
  m_notebook61->RemovePage(0);
  m_panel22->Reparent(m_notebook6);
  m_notebook6->RemovePage(2);
  m_panel21->Hide();
  m_notebook6->AddPage(m_panel22, _("Measurements"));
  auto* measurementLayout = new wxBoxSizer(wxVERTICAL);
  auto* entry = new wxScrolledWindow(m_panel22, wxID_ANY);
  entry->SetMinSize(wxSize(0, 0));
  entry->SetScrollRate(0, 16);
  auto* measurementContent = new wxPanel(entry, wxID_ANY);
  auto* entryFields = new wxBoxSizer(wxVERTICAL);
  auto move = [&](wxWindow* control, wxSizer* into) {
    if (control->GetContainingSizer()) control->GetContainingSizer()->Detach(control);
    control->Reparent(measurementContent);
    into->Add(control, 0, wxEXPAND | wxALL, 8);
  };
  auto* windType = new wxBoxSizer(wxHORIZONTAL);
  move(m_rbTrueWind, windType);
  move(m_rbApparentWind, windType);
  entryFields->Add(windType, 0, wxEXPAND);
  for (wxWindow* control : {static_cast<wxWindow*>(m_staticText133),
      static_cast<wxWindow*>(m_tWindSpeed), static_cast<wxWindow*>(m_staticText134),
      static_cast<wxWindow*>(m_tWindDirection), static_cast<wxWindow*>(m_staticText135),
      static_cast<wxWindow*>(m_tBoatSpeed), static_cast<wxWindow*>(m_button46)})
    move(control, entryFields);
  m_rbTrueWind->SetValue(true);
  m_staticText133->SetLabel(_("Wind speed (knots)"));
  m_staticText134->SetLabel(_("Wind angle (degrees, 0 to 180)"));
  m_staticText135->SetLabel(_("Boat speed through water (knots)"));
  m_androidMeasurementPicker = new wxChoice(measurementContent, wxID_ANY);
  entryFields->Add(m_androidMeasurementPicker, 0, wxEXPAND | wxALL, 8);
  m_androidMeasurementDetails = new wxStaticText(measurementContent, wxID_ANY, wxString());
  entryFields->Add(m_androidMeasurementDetails, 0, wxEXPAND | wxALL, 8);
  m_androidMeasurementPicker->Bind(wxEVT_CHOICE, [this](wxCommandEvent&) {
    ShowAndroidMeasurement();
  });
  auto* measurementActions = new wxGridSizer(0, 2, 8, 8);
  for (auto* button : {m_bRemoveMeasurement, m_bRemoveAllMeasurements, m_button50})
    move(button, measurementActions);
  entryFields->Add(measurementActions, 0, wxEXPAND);
  measurementContent->SetSizer(entryFields);
  auto* measurementViewport = new wxBoxSizer(wxVERTICAL);
  measurementViewport->Add(measurementContent, 1, wxEXPAND);
  entry->SetSizer(measurementViewport);
  measurementLayout->Add(entry, 1, wxEXPAND | wxALL, 8);
  for (wxWindow* control : {static_cast<wxWindow*>(m_lMeasurements),
                            static_cast<wxWindow*>(m_button48)}) {
    control->GetContainingSizer()->Detach(control);
    control->Reparent(m_panel22);
    control->Hide();
  }
  for (auto* child : m_panel22->GetChildren())
    if (wxDynamicCast(child, wxStaticBox)) child->Hide();
  m_panel22->SetSizer(measurementLayout, true);
  // The desktop page's best size otherwise forces the whole notebook taller
  // than a landscape canvas, leaving Generate below an unscrollable viewport.
  for (wxWindow* page : {static_cast<wxWindow*>(m_panel19),
                        static_cast<wxWindow*>(m_panel20),
                        static_cast<wxWindow*>(m_panel22),
                        static_cast<wxWindow*>(m_notebook6)})
    page->SetMinSize(wxSize(0, 0));
  for (auto* scroll : {dimensionScroll, entry})
    scroll->Bind(wxEVT_SIZE, [scroll](wxSizeEvent& event) {
      scroll->Layout();
      scroll->FitInside();
      event.Skip();
    });

  WR_StyleAndroidControls(this);
  auto* header = WR_AddAndroidDoneHeader(this, _("Edit polar"), [this]() {
    EndModal(wxID_CANCEL);
  }, _("Cancel"));
  auto* save = new wxButton(header, wxID_ANY, _("Save polar"));
  save->SetMinSize(wxSize(180, 72));
  save->GetHandle()->setStyleSheet(
      "QPushButton { font-size: 16pt; min-height: 62px; color: #173849; background: white; }");
  header->GetSizer()->Insert(1, save, 0, wxALL, 8);
  save->Bind(wxEVT_BUTTON, [this](wxCommandEvent& event) { OnSave(event); });
  WR_AddAndroidBookPicker(this, m_notebook6, 1);
  WR_WrapAndroidText(m_staticText1351, m_staticText1351->GetLabel(), wxGetDisplaySize().x - 90);
  m_panel19->Bind(wxEVT_SIZE, [this](wxSizeEvent& event) {
    WR_WrapAndroidText(m_staticText1351, m_staticText1351->GetLabel(),
                      m_panel19->GetClientSize().x - 48);
    m_panel19->Layout();
    event.Skip();
  });
#endif

}

void EditPolarDialog::SetPolarIndex(int i) {
  polarindex = i;
  RebuildTrueWindAngles();
  RebuildTrueWindSpeeds();
  RebuildGrid();
  m_lMeasurements->DeleteAllItems();

#ifdef __OCPN__ANDROID__
  m_androidSaved = false;
  m_androidMeasurements.clear();
  RefreshAndroidMeasurements();
  wxSize sz = ::wxGetDisplaySize();
  SetSize(0, 0, sz.x, sz.y - 40);
#endif
}

void EditPolarDialog::OnPolarGridChanged(wxGridEvent& event) {
  wxString str = m_gPolar->GetCellValue(event.GetRow(), event.GetCol());
  if (str == "0") str = "";
  double stw;
  if (!str.ToDouble(&stw)) stw = NAN;
  GetPolar()->wind_speeds[event.GetCol()].orig_speeds[event.GetRow()] = stw;
  GetPolar()->UpdateSpeeds();
  m_BoatDialog->Refresh();
}

void EditPolarDialog::OnAddTrueWindAngle(wxCommandEvent& event) {
#ifdef __OCPN__ANDROID__
  if (!ValidateAndroidSpeeds()) return;
#endif
  double twa;
  if (!m_tTrueWindAngle->GetValue().ToDouble(&twa) || !std::isfinite(twa) || twa < 0 || twa > 180) {
#ifdef __OCPN__ANDROID__
    WR_MessageBox(_("Enter a true wind angle from 0 to 180 degrees."),
                 _("Polar dimensions"), wxOK | wxICON_INFORMATION, this);
    m_tTrueWindAngle->SetFocus();
    return;
#else
    return m_tTrueWindAngle->Clear();
#endif
  }
  GetPolar()->AddDegreeStep(twa);
  RebuildTrueWindAngles();
  RebuildGrid();
}

void EditPolarDialog::OnRemoveTrueWindAngle(wxCommandEvent& event) {
#ifdef __OCPN__ANDROID__
  if (!ValidateAndroidSpeeds()) return;
#endif
  int sel = m_lTrueWindAngles->GetSelection();
  if (sel == -1) return;
  GetPolar()->RemoveDegreeStep(sel);
  RebuildTrueWindAngles();
  RebuildGrid();
}

void EditPolarDialog::OnAddTrueWindSpeed(wxCommandEvent& event) {
#ifdef __OCPN__ANDROID__
  if (!ValidateAndroidSpeeds()) return;
#endif
  double tws;
  if (!m_tTrueWindSpeed->GetValue().ToDouble(&tws) || !std::isfinite(tws) || tws < 0 || tws > 180) {
#ifdef __OCPN__ANDROID__
    WR_MessageBox(_("Enter a true wind speed from 0 to 180 knots."),
                 _("Polar dimensions"), wxOK | wxICON_INFORMATION, this);
    m_tTrueWindSpeed->SetFocus();
#endif
    return;
  }
  m_tTrueWindSpeed->Clear();
  GetPolar()->AddWindSpeed(tws);
  RebuildTrueWindSpeeds();
  RebuildGrid();
}

void EditPolarDialog::OnRemoveTrueWindSpeed(wxCommandEvent& event) {
#ifdef __OCPN__ANDROID__
  if (!ValidateAndroidSpeeds()) return;
#endif
  int sel = m_lTrueWindSpeeds->GetSelection();
  if (sel == -1) return;
  GetPolar()->RemoveWindSpeed(sel);
  RebuildTrueWindSpeeds();
  RebuildGrid();
}

static wxString dtos(double d) { return wxString::Format(_T("%f"), d); }

void EditPolarDialog::OnAddMeasurement(wxCommandEvent& event) {
#ifdef __OCPN__ANDROID__
  double speed, angle, boat;
  if (!m_tWindSpeed->GetValue().ToDouble(&speed) || !std::isfinite(speed) || speed < 0 ||
      !m_tWindDirection->GetValue().ToDouble(&angle) || !std::isfinite(angle) || angle < 0 || angle > 180 ||
      !m_tBoatSpeed->GetValue().ToDouble(&boat) || !std::isfinite(boat) || boat <= 0) {
    WR_MessageBox(_("Enter a non-negative wind speed, a positive boat speed, and a wind angle from 0 to 180 degrees."),
                 _("Measurement"), wxOK | wxICON_INFORMATION, this);
    return;
  }
  PolarMeasurement measurement(speed, angle, boat, m_rbApparentWind->GetValue());
  m_androidMeasurements.push_back({measurement.aws, measurement.awa, measurement.stw});
  RefreshAndroidMeasurements();
  return;
#endif
  wxListItem info;
  info.SetId(m_lMeasurements->GetItemCount());
  //        info.SetData(i);
  long idx = m_lMeasurements->InsertItem(info);

  double windspeed, winddirection, stw;

  m_tWindSpeed->GetValue().ToDouble(&windspeed);
  m_tWindDirection->GetValue().ToDouble(&winddirection);

  m_tBoatSpeed->GetValue().ToDouble(&stw);
  PolarMeasurement m(windspeed, winddirection, stw,
                     m_rbApparentWind->GetValue());

  m_lMeasurements->SetItem(idx, spTRUE_WIND_SPEED, dtos(m.getTWS()));
  m_lMeasurements->SetItem(idx, spTRUE_WIND_DIRECTION, dtos(m.getTWA()));
  m_lMeasurements->SetItem(idx, spAPPARENT_WIND_SPEED, dtos(m.aws));
  m_lMeasurements->SetItem(idx, spAPPARENT_WIND_DIRECTION, dtos(m.awa));
  m_lMeasurements->SetItem(idx, spBOAT_SPEED, dtos(m.stw));
  m_lMeasurements->SetItem(idx, spETA, dtos(m.eta));
}

void EditPolarDialog::OnRemoveMeasurement(wxCommandEvent& event) {
#ifdef __OCPN__ANDROID__
  const int selected = m_androidMeasurementPicker->GetSelection();
  if (selected < 0 || selected >= static_cast<int>(m_androidMeasurements.size())) return;
  m_androidMeasurements.erase(m_androidMeasurements.begin() + selected);
  RefreshAndroidMeasurements();
  return;
#endif
  long index =
      m_lMeasurements->GetNextItem(-1, wxLIST_NEXT_ALL, wxLIST_STATE_SELECTED);
  if (index == -1) return;

  m_lMeasurements->DeleteItem(index);
}

void EditPolarDialog::OnRemoveAllMeasurements(wxCommandEvent& event) {
  m_lMeasurements->DeleteAllItems();
#ifdef __OCPN__ANDROID__
  m_androidMeasurements.clear();
  RefreshAndroidMeasurements();
#endif
}

void EditPolarDialog::OnGeneratePolar(wxCommandEvent& event) {
  std::list<PolarMeasurement> measurements;
#ifdef __OCPN__ANDROID__
  if (!ValidateAndroidSpeeds()) return;
  if (m_androidMeasurements.empty()) {
    WR_MessageBox(_("Add measurements before generating a polar."), _("Generate polar"),
                 wxOK | wxICON_INFORMATION, this);
    return;
  }
  for (const auto& m : m_androidMeasurements)
    measurements.emplace_back(m[0], m[1], m[2]);
#else
  for (int i = 0; i < m_lMeasurements->GetItemCount(); i++) {
    double v[3];
    const int ind[3] = {spAPPARENT_WIND_SPEED, spAPPARENT_WIND_DIRECTION,
                        spBOAT_SPEED};
    for (int j = 0; j < 3; j++)
      m_lMeasurements->GetItemText(i, ind[j]).ToDouble(v + j);
    PolarMeasurement m(v[0], v[1], v[2]);
    measurements.push_back(m);
  }
#endif
  GetPolar()->Generate(measurements);
#ifdef __OCPN__ANDROID__
  // Generated speeds become the editable input, so subsequent edits and
  // interpolation retain the generated polar and Save includes every cell.
  for (auto& wind : GetPolar()->wind_speeds) wind.orig_speeds = wind.speeds;
  GetPolar()->UpdateSpeeds();
#endif
  RebuildGrid();
}

void EditPolarDialog::RebuildTrueWindAngles() {
  m_lTrueWindAngles->Clear();

  if (!GetPolar()) return;

  for (unsigned int i = 0; i < GetPolar()->degree_steps.size(); i++)
    m_lTrueWindAngles->Append(
        wxString::Format(_T("%4.1f"), GetPolar()->degree_steps[i]));
#ifdef __OCPN__ANDROID__
  m_androidDimensionAngle->Clear();
  for (const auto angle : GetPolar()->degree_steps)
    m_androidDimensionAngle->Append(wxString::Format("%.1f", angle));
  if (m_androidDimensionAngle->GetCount()) {
    m_androidDimensionAngle->SetSelection(0);
    m_lTrueWindAngles->SetSelection(0);
  }
#endif
}

void EditPolarDialog::RebuildTrueWindSpeeds() {
  m_lTrueWindSpeeds->Clear();

  if (!GetPolar()) return;

  for (unsigned int i = 0; i < GetPolar()->wind_speeds.size(); i++)
    m_lTrueWindSpeeds->Append(
        wxString::Format(_T("%4.1f"), GetPolar()->wind_speeds[i].tws));
#ifdef __OCPN__ANDROID__
  m_androidDimensionWind->Clear();
  for (const auto& wind : GetPolar()->wind_speeds)
    m_androidDimensionWind->Append(wxString::Format("%.1f", wind.tws));
  if (m_androidDimensionWind->GetCount()) {
    m_androidDimensionWind->SetSelection(0);
    m_lTrueWindSpeeds->SetSelection(0);
  }
#endif
}

void EditPolarDialog::RebuildGrid() {
  if (m_gPolar->GetNumberRows())
    m_gPolar->DeleteRows(0, m_gPolar->GetNumberRows());
  if (m_gPolar->GetNumberCols())
    m_gPolar->DeleteCols(0, m_gPolar->GetNumberCols());

  if (!GetPolar()) return;

  m_gPolar->InsertRows(0, GetPolar()->degree_steps.size());
  for (unsigned int i = 0; i < GetPolar()->degree_steps.size(); i++)
    m_gPolar->SetRowLabelValue(
        i, wxString::Format(_T("%4.1f"), GetPolar()->degree_steps[i]));

  m_gPolar->InsertCols(0, GetPolar()->wind_speeds.size());
  for (unsigned int i = 0; i < GetPolar()->wind_speeds.size(); i++) {
    m_gPolar->SetColLabelValue(
        i, wxString::Format(_T("%4.1f"), GetPolar()->wind_speeds[i].tws));

    for (unsigned int j = 0; j < GetPolar()->degree_steps.size(); j++) {
      double v = GetPolar()->wind_speeds[i].orig_speeds[j];
      wxString str = std::isnan(v) ? wxString()
                     : v == 0      ? wxString(_T("0.0"))
                                   : wxString::Format(_T("%4.1f"), v);
      m_gPolar->SetCellValue(j, i, str);
    }
  }

#ifdef __OCPN__ANDROID__
  if (m_androidAngle) {
    const int selection = wxMax(0, m_androidAngle->GetSelection());
    m_androidAngle->Clear();
    for (const auto angle : GetPolar()->degree_steps)
      m_androidAngle->Append(wxString::Format(_("True wind angle: %.1f degrees"), angle));
    m_androidAngle->SetSelection(wxMin(selection, static_cast<int>(m_androidAngle->GetCount()) - 1));
    RefreshAndroidSpeeds();
  }
#endif
  m_BoatDialog->Refresh();
}

#ifdef __OCPN__ANDROID__
bool EditPolarDialog::ValidateAndroidPolar() {
  if (!ValidateAndroidSpeeds()) return false;
  Polar* polar = GetPolar();
  if (!polar || polar->degree_steps.empty() || polar->wind_speeds.empty()) {
    WR_MessageBox(_("Add at least one true wind angle and one true wind speed before saving."),
                 _("Save polar"), wxOK | wxICON_INFORMATION, this);
    return false;
  }
  for (const auto& wind : polar->wind_speeds)
    for (double speed : wind.orig_speeds)
      if (std::isfinite(speed) && speed > 0) return true;
  WR_MessageBox(_("Enter a positive boat speed or generate speeds from measurements before saving."),
               _("Save polar"), wxOK | wxICON_INFORMATION, this);
  return false;
}

bool EditPolarDialog::ValidateAndroidSpeeds() {
  for (auto* card : m_androidSpeeds->GetChildren())
    for (auto* child : card->GetChildren())
      if (auto* input = wxDynamicCast(child, wxTextCtrl)) {
        double value;
        const wxString text = input->GetValue();
        if (!text.IsEmpty() && (!text.ToDouble(&value) || !std::isfinite(value) || value < 0)) {
          WR_MessageBox(_("Enter a non-negative boat speed, or leave the field blank for interpolation."),
                       _("Polar speed"), wxOK | wxICON_INFORMATION, this);
          input->SetFocus();
          return false;
        }
      }
  return true;
}

void EditPolarDialog::RefreshAndroidSpeeds() {
  m_androidSpeedFields->Clear(true);
  const int row = m_androidAngle->GetSelection();
  m_androidDisplayedAngle = row;
  if (!GetPolar() || row < 0) return;
  for (size_t col = 0; col < GetPolar()->wind_speeds.size(); ++col) {
    auto* card = new wxPanel(m_androidSpeeds, wxID_ANY);
    auto* fields = new wxBoxSizer(wxVERTICAL);
    auto* label = new wxStaticText(card, wxID_ANY,
        wxString::Format(_("Boat speed at %.1f knots true wind"), GetPolar()->wind_speeds[col].tws));
    const double speed = GetPolar()->wind_speeds[col].orig_speeds[row];
    auto* input = new wxTextCtrl(card, wxID_ANY,
        std::isfinite(speed) ? wxString::Format("%.5g", speed) : wxString());
    fields->Add(label, 0, wxEXPAND | wxALL, 8);
    fields->Add(input, 0, wxEXPAND | wxALL, 8);
    card->SetSizer(fields);
    WR_StyleAndroidControls(card);
    input->Bind(wxEVT_TEXT, [this, input, row, col](wxCommandEvent&) {
      double value = NAN;
      const wxString text = input->GetValue();
      if (!text.IsEmpty() && (!text.ToDouble(&value) || !std::isfinite(value) || value < 0)) return;
      GetPolar()->wind_speeds[col].orig_speeds[row] = value;
      GetPolar()->UpdateSpeeds();
      m_gPolar->SetCellValue(row, col, text);
      m_BoatDialog->Refresh();
    });
    m_androidSpeedFields->Add(card, 0, wxEXPAND | wxBOTTOM, 12);
  }
  m_androidSpeeds->Layout();
  m_androidSpeeds->FitInside();
}

void EditPolarDialog::RefreshAndroidMeasurements() {
  if (!m_androidMeasurementPicker) return;
  m_androidMeasurementPicker->Clear();
  for (size_t i = 0; i < m_androidMeasurements.size(); ++i) {
    const auto& values = m_androidMeasurements[i];
    const PolarMeasurement m(values[0], values[1], values[2]);
    m_androidMeasurementPicker->Append(wxString::Format(
        _("Measurement %d: %.1f kt at %.1f degrees true wind"),
        static_cast<int>(i + 1), m.getTWS(), m.getTWA()));
  }
  if (!m_androidMeasurements.empty())
    m_androidMeasurementPicker->SetSelection(m_androidMeasurements.size() - 1);
  const bool any = !m_androidMeasurements.empty();
  m_bRemoveMeasurement->Enable(any);
  m_bRemoveAllMeasurements->Enable(any);
  m_button50->Enable(any);
  ShowAndroidMeasurement();
}

void EditPolarDialog::ShowAndroidMeasurement() {
  const int selected = m_androidMeasurementPicker->GetSelection();
  wxString details;
  if (selected >= 0 && selected < static_cast<int>(m_androidMeasurements.size())) {
    const auto& values = m_androidMeasurements[selected];
    const PolarMeasurement m(values[0], values[1], values[2]);
    details = wxString::Format(
        _("True wind: %.1f kt at %.1f degrees\nApparent wind: %.1f kt at %.1f degrees\nBoat speed: %.1f kt\nSailboat transform ETA: %.3f"),
        m.getTWS(), m.getTWA(), m.aws, m.awa, m.stw, m.eta);
  }
  WR_WrapAndroidText(m_androidMeasurementDetails, details,
                    GetClientSize().x - 90);
  wxWindow* content = m_androidMeasurementDetails->GetParent();
  content->Layout();
  for (wxWindow* parent = content; parent; parent = parent->GetParent())
    if (auto* scroll = wxDynamicCast(parent, wxScrolledWindow)) {
      scroll->Layout();
      scroll->FitInside();
      break;
    }
}

#endif

Polar* EditPolarDialog::GetPolar() {
  return polarindex == -1 ? NULL : &m_BoatDialog->m_Boat.Polars[polarindex];
}
