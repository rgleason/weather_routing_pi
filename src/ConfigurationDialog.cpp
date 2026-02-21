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
#include <time.h>

#include "tinyxml.h"

#include "Utilities.h"
#include "Boat.h"
#include "RouteMapOverlay.h"
#include "ConfigurationDialog.h"
#include "BoatDialog.h"
#include "weather_routing_pi.h"
#include "WeatherRouting.h"
#include "icons.h"

#include <algorithm>

ConfigurationDialog::ConfigurationDialog(WeatherRouting& weatherrouting)
#ifndef __WXOSX__
    : ConfigurationDialogBase(&weatherrouting),
#else
    : ConfigurationDialogBase(&weatherrouting, wxID_ANY,
                              _("Weather Routing Configuration"),
                              wxDefaultPosition, wxDefaultSize,
                              wxDEFAULT_DIALOG_STYLE | wxSTAY_ON_TOP),
#endif
      m_WeatherRouting(weatherrouting),
      m_bBlockUpdate(false) {
  // DO NOT build UI here
  // DO NOT read config here
  // DO NOT set size or position here
  // All UI work must happen in InitUI()
}

void ConfigurationDialog::InitUI() {
  wxFileConfig* pConf = GetOCPNConfigObject();
  pConf->SetPath(_T("/PlugIns/WeatherRouting"));

#ifdef __OCPN__ANDROID__
  wxSize sz = ::wxGetDisplaySize();
  SetSize(0, 0, sz.x, sz.y - 40);
#else
  wxPoint p = GetPosition();
  pConf->Read(_T("ConfigurationX"), &p.x, p.x);
  pConf->Read(_T("ConfigurationY"), &p.y, p.y);
  SetPosition(p);
#endif

  // If the dialog normally builds controls or sizers in the constructor,
  // move that code here as well.

  Layout();
  Fit();
  Update();
  Refresh();
}


ConfigurationDialog::~ConfigurationDialog() {
  wxFileConfig* pConf = GetOCPNConfigObject();
  pConf->SetPath(_T( "/PlugIns/WeatherRouting" ));

  wxPoint p = GetPosition();
  pConf->Write(_T ( "ConfigurationX" ), p.x);
  pConf->Write(_T ( "ConfigurationY" ), p.y);
}

void ConfigurationDialog::EditBoat() {
  m_WeatherRouting.GetBoatDialog().LoadPolar(m_tBoat->GetValue());
  m_WeatherRouting.GetBoatDialog().Show();
}

// Unused: event handler not referenced by UI or logic.
void ConfigurationDialog::OnGribTime(wxCommandEvent& event) {
  SetStartDateTime(m_GribTimelineTime);
  Update();
}

void ConfigurationDialog::OnCurrentTime(wxCommandEvent& event) {
  SetStartDateTime(wxDateTime::Now().ToUTC());
  Update();
}

void ConfigurationDialog::OnUseCurrentTime(wxCommandEvent& event) {
  m_dpStartDate->Enable(!m_cbUseCurrentTime->IsChecked());
  m_tpTime->Enable(!m_cbUseCurrentTime->IsChecked());
  m_bGribTime->Enable(!m_cbUseCurrentTime->IsChecked());
  m_bCurrentTime->Enable(!m_cbUseCurrentTime->IsChecked());
  Update();
}

void ConfigurationDialog::OnStartFromBoat(wxCommandEvent& event) {
  m_cStart->Enable(!m_rbStartFromBoat->GetValue());
  Update();
}

void ConfigurationDialog::OnStartFromPosition(wxCommandEvent& event) {
  m_cStart->Enable(m_rbStartPositionSelection->GetValue());
  Update();
}

void ConfigurationDialog::OnAvoidCyclones(wxCommandEvent& event) { Update(); }

void ConfigurationDialog::OnUseMotor(wxCommandEvent& event) {
  // Enable/disable motor controls based on checkbox state
  bool useMotor = m_cbUseMotor->IsChecked();
  m_sMotorSpeedThreshold->Enable(useMotor);
  m_sMotorSpeed->Enable(useMotor);
  Update();
}

void ConfigurationDialog::OnBoatFilename(wxCommandEvent& event) {
  wxFileDialog openDialog(
      this, _("Select Boat File"), wxFileName(m_tBoat->GetValue()).GetPath(),
      wxT(""), wxT("xml (*.xml)|*.XML;*.xml|All files (*.*)|*.*"), wxFD_OPEN);

  if (openDialog.ShowModal() == wxID_OK) SetBoatFilename(openDialog.GetPath());
}

// --- Macros for filling controls from configurations --------------------------------
// Control helper used by many SET_* macros
#define SET_CONTROL_VALUE(VALUE, CONTROL, SETTER, TYPE, NULLVALUE)          \
  do {                                                                      \
    bool allsame = true;                                                    \
    std::list<RouteMapConfiguration>::iterator it = configurations.begin(); \
    TYPE value = VALUE;                                                     \
    for (it++; it != configurations.end(); ++it) {                          \
      if (value != VALUE) {                                                 \
        allsame = false;                                                    \
        break;                                                              \
      }                                                                     \
    }                                                                       \
    CONTROL->SETTER(allsame ? value : NULLVALUE);                           \
    wxSize s(CONTROL->GetSize());                                           \
    if (allsame)                                                            \
      CONTROL->SetForegroundColour(wxColour(0, 0, 0));                      \
    else                                                                    \
      CONTROL->SetForegroundColour(wxColour(180, 180, 180));                \
    CONTROL->Fit();                                                         \
    CONTROL->SetSize(s);                                                    \
  } while (0)

#define SET_CONTROL(FIELD, CONTROL, SETTER, TYPE, NULLVALUE) \
  SET_CONTROL_VALUE((*it).FIELD, CONTROL, SETTER, TYPE, NULLVALUE)

#define SET_CHOICE_VALUE(FIELD, VALUE)                                        \
  do {                                                                        \
    bool allsame = true;                                                      \
    std::list<RouteMapConfiguration>::iterator it = configurations.begin();   \
    wxString value = VALUE;                                                   \
    for (it++; it != configurations.end(); ++it) {                            \
      if (value != VALUE) {                                                   \
        allsame = false;                                                      \
        break;                                                                \
      }                                                                       \
    }                                                                         \
    if (allsame)                                                              \
      m_c##FIELD->SetValue(value);                                            \
    else {                                                                    \
      if (m_c##FIELD->GetString(m_c##FIELD->GetCount() - 1) != wxEmptyString) \
        m_c##FIELD->Append(wxEmptyString);                                    \
      m_c##FIELD->SetValue(wxEmptyString);                                    \
    }                                                                         \
  } while (0)

#define SET_CHOICE(FIELD) SET_CHOICE_VALUE(FIELD, (*it).FIELD)

#define SET_SPIN_VALUE(FIELD, VALUE) \
  SET_CONTROL_VALUE(VALUE, m_s##FIELD, SetValue, int, value)

#define SET_SPIN(FIELD) SET_SPIN_VALUE(FIELD, (*it).FIELD)

#define SET_SPIN_DOUBLE_VALUE(FIELD, VALUE) \
  SET_CONTROL_VALUE(VALUE, m_s##FIELD, SetValue, double, value)

#define SET_SPIN_DOUBLE(FIELD) SET_SPIN_DOUBLE_VALUE(FIELD, (*it).FIELD)

// Safer checkbox macros: use internal wr_cfg loop and safe fallback for 2-state
#define SET_CHECKBOX_FIELD(FIELD, VALUE_EXPR)                             \
  do {                                                                    \
    bool wr_alltrue = true, wr_allfalse = true;                           \
    for (const auto& wr_cfg : configurations) {                           \
      if (VALUE_EXPR)                                                      \
        wr_allfalse = false;                                               \
      else                                                                 \
        wr_alltrue = false;                                                \
    }                                                                      \
    wxCheckBox* cb = m_cb##FIELD;                                          \
    if ( cb && cb->Is3State() ) {                                          \
      cb->Set3StateValue(wr_alltrue ? wxCHK_CHECKED                        \
                         : wr_allfalse ? wxCHK_UNCHECKED                   \
                                       : wxCHK_UNDETERMINED);              \
    } else if ( cb ) {                                                     \
      /* 2-state fallback: if unanimous keep value, otherwise choose false */\
      if (wr_alltrue) cb->SetValue(true);                                  \
      else cb->SetValue(false);                                            \
    }                                                                      \
  } while (0)

#define SET_CHECKBOX(FIELD) SET_CHECKBOX_FIELD(FIELD, wr_cfg.FIELD)

#define GET_CHECKBOX(FIELD)                                                 \
  do {                                                                       \
    wxCheckBox* cb = m_cb##FIELD;                                            \
    if (cb && cb->Is3State()) {                                              \
      wxCheckBoxState wr_st = cb->Get3StateValue();                          \
      if (wr_st == wxCHK_UNCHECKED)                                          \
        configuration.FIELD = false;                                         \
      else if (wr_st == wxCHK_CHECKED)                                       \
        configuration.FIELD = true;                                          \
      /* UNDETERMINED: leave configuration.FIELD unchanged */                \
    } else if (cb) {                                                         \
      /* 2-state checkbox: use boolean value */                              \
      configuration.FIELD = cb->GetValue();                                  \
    }                                                                        \
  } while (0)

#ifdef __OCPN__ANDROID__
#define NO_EDITED_CONTROLS 1
#else
#define NO_EDITED_CONTROLS 0
#endif
// ---------------------------------------------------------------------------------

void ConfigurationDialog::SetConfigurations(
    std::list<RouteMapConfiguration> configurations) {
  m_bBlockUpdate = true;

  m_edited_controls.clear();

  //-----------

  // Keep original behaviour for multi-selection fields
  SET_CHOICE(Start);

  // If there are no configurations, nothing to populate - bail out safely.
  if (configurations.empty()) {
    m_bBlockUpdate = false;
    return;
  }

  // Create iterator used by the existing macros that expect (*it)
  std::list<RouteMapConfiguration>::iterator it = configurations.begin();

  const bool ult =
      m_WeatherRouting.GetSettingsDialog().m_cbUseLocalTime->GetValue();
#define STARTTIME (ult ? it->StartTime.FromUTC() : it->StartTime)

  // Populate date/time from the first configuration safely
  if (it != configurations.end()) {
    // Date part: only set if valid (avoid MSW assert), otherwise set "none"
    // only when the control supports wxDP_ALLOWNONE
    wxDateTime dateVal = STARTTIME.GetDateOnly();
    wxSize s(m_dpStartDate->GetSize());
    if (dateVal.IsValid()) {
      m_dpStartDate->SetValue(dateVal);
      m_dpStartDate->SetForegroundColour(wxColour(0, 0, 0));
    } else if (m_dpStartDate->GetWindowStyle() & wxDP_ALLOWNONE) {
      m_dpStartDate->SetValue(wxDefaultDateTime);
      m_dpStartDate->SetForegroundColour(wxColour(180, 180, 180));
    }  // else leave control unchanged
    m_dpStartDate->Fit();
    m_dpStartDate->SetSize(s);

    // Time part: only set if valid (time control usually requires a valid
    // value)
    wxDateTime timeVal = STARTTIME;
    wxSize s2(m_tpTime->GetSize());
    if (timeVal.IsValid()) {
      m_tpTime->SetValue(timeVal);
      m_tpTime->SetForegroundColour(wxColour(0, 0, 0));
    }  // else leave control unchanged
    m_tpTime->Fit();
    m_tpTime->SetSize(s2);
  }

// -------------

  SET_CHECKBOX(UseCurrentTime);

  bool timeButtonsEnabled = m_tpTime->IsEnabled() &&
                            m_dpStartDate->IsEnabled() &&
                            !m_cbUseCurrentTime->IsChecked();
  m_dpStartDate->Enable(timeButtonsEnabled);
  m_tpTime->Enable(timeButtonsEnabled);
  m_bCurrentTime->Enable(timeButtonsEnabled);
  m_bGribTime->Enable(timeButtonsEnabled);

  SET_SPIN_VALUE(TimeStepHours, (int)((*it).DeltaTime / 3600));
  SET_SPIN_VALUE(TimeStepMinutes, ((int)(*it).DeltaTime / 60) % 60);

  SET_CONTROL(boatFileName, m_tBoat, SetValue, wxString, _T(""));
  long l = m_tBoat->GetValue().Length();
  m_tBoat->SetSelection(l, l);

  SET_CHOICE(End);

  // if there's a Route GUID it's an OpenCPN route, in that case disable start
  // and end.
  bool oRoute = false;
  bool allStartFromBoat = true;
  bool allStartFromPosition = true;
  for (auto it : configurations) {
    if (!it.RouteGUID.IsEmpty()) {
      oRoute = true;
      break;
    }
    if (it.StartType != RouteMapConfiguration::START_FROM_BOAT)
      allStartFromBoat = false;
    if (it.StartType != RouteMapConfiguration::START_FROM_POSITION)
      allStartFromPosition = false;
  }
  m_rbStartFromBoat->Enable(!oRoute);
  m_rbStartPositionSelection->Enable(!oRoute);
  m_rbStartFromBoat->SetValue(allStartFromBoat);
  m_rbStartPositionSelection->SetValue(allStartFromPosition);

  m_cStart->Enable(!oRoute && !m_rbStartFromBoat->GetValue());
  m_cEnd->Enable(!oRoute);

  m_bGribTime->Enable(!m_cbUseCurrentTime->IsChecked());
  m_bCurrentTime->Enable(!m_cbUseCurrentTime->IsChecked());
  m_dpStartDate->Enable(!m_cbUseCurrentTime->IsChecked());
  m_tpTime->Enable(!m_cbUseCurrentTime->IsChecked());

  SET_SPIN(FromDegree);
  SET_SPIN(ToDegree);
  SET_SPIN_DOUBLE(ByDegrees);

  // Motor settings
  SET_CHECKBOX(UseMotor);
  SET_SPIN_DOUBLE_VALUE(MotorSpeedThreshold, (*it).MotorSpeedThreshold);
  SET_SPIN_DOUBLE_VALUE(MotorSpeed, (*it).MotorSpeed);

  // Enable/disable motor controls based on checkbox state
  bool motorEnabled = m_cbUseMotor->IsChecked();
  m_sMotorSpeedThreshold->Enable(motorEnabled);
  m_sMotorSpeed->Enable(motorEnabled);

  SET_CHOICE_VALUE(Integrator,
                   ((*it).Integrator == RouteMapConfiguration::RUNGE_KUTTA
                        ? _T("Runge Kutta")
                        : _T("Newton")));

  SET_SPIN(MaxDivertedCourse);
  SET_SPIN(MaxCourseAngle);
  SET_SPIN(MaxSearchAngle);
  SET_SPIN(MaxTrueWindKnots);
  SET_SPIN(MaxApparentWindKnots);

  SET_SPIN_DOUBLE(MaxSwellMeters);
  SET_SPIN(MaxLatitude);
  SET_SPIN(TackingTime);
  SET_SPIN(JibingTime);
  SET_SPIN(SailPlanChangeTime);
  SET_SPIN(WindVSCurrent);

  SET_CHECKBOX(AvoidCycloneTracks);
  SET_SPIN(CycloneMonths);
  SET_SPIN(CycloneDays);
  SET_SPIN_DOUBLE(SafetyMarginLand);

  SET_CHECKBOX(DetectLand);
  SET_CHECKBOX(DetectBoundary);
  SET_CHECKBOX(Currents);
  SET_CHECKBOX(OptimizeTacking);

  SET_CHECKBOX(InvertedRegions);
  SET_CHECKBOX(Anchoring);

  SET_CHECKBOX(UseGrib);
  SET_CONTROL(ClimatologyType, m_cClimatologyType, SetSelection, int, -1);
  SET_CHECKBOX(AllowDataDeficient);
  SET_SPIN_VALUE(WindStrength, (int)((*it).WindStrength * 100));

  SET_SPIN_VALUE(UpwindEfficiency, (int)((*it).UpwindEfficiency * 100));
  SET_SPIN_VALUE(DownwindEfficiency, (int)((*it).DownwindEfficiency * 100));
  SET_SPIN_VALUE(NightCumulativeEfficiency,
                 (int)((*it).NightCumulativeEfficiency * 100));

  m_bBlockUpdate = false;
}

void ConfigurationDialog::AddSource(wxString name) {
  m_cStart->Append(name);
  m_cEnd->Append(name);
}

void ConfigurationDialog::RemoveSource(wxString name) {
  int i = m_cStart->FindString(name, true);
  if (i >= 0) {
    m_cStart->Delete(i);
    m_cEnd->Delete(i);
  }
}

void ConfigurationDialog::ClearSources() {
  m_cStart->Clear();
  m_cEnd->Clear();
}

void ConfigurationDialog::SetBoatFilename(wxString path) {
  m_tBoat->SetValue(path);
  long l = m_tBoat->GetValue().Length();
  m_tBoat->SetSelection(l, l);

  Update();
}

void ConfigurationDialog::OnResetAdvanced(wxCommandEvent& event) {
  m_bBlockUpdate = true;

  // constraints
  m_sMaxLatitude->SetValue(90);
  m_sWindVSCurrent->SetValue(0);
  m_sMaxCourseAngle->SetValue(180);
  m_sMaxSearchAngle->SetValue(120);
  m_cbAvoidCycloneTracks->SetValue(false);
  // XXX missing 2

  // Options
  m_cbInvertedRegions->SetValue(false);
  m_cbAnchoring->SetValue(false);
  m_cIntegrator->SetSelection(0);
  m_sWindStrength->SetValue(100);
  m_sUpwindEfficiency->SetValue(100);
  m_sDownwindEfficiency->SetValue(100);
  m_sNightCumulativeEfficiency->SetValue(100);
  m_sTackingTime->SetValue(0);
  m_sJibingTime->SetValue(0);
  m_sSailPlanChangeTime->SetValue(0);
  m_sSafetyMarginLand->SetValue(0.);

  m_sFromDegree->SetValue(0);
  m_sToDegree->SetValue(180);
  m_sByDegrees->SetValue(5.0);

  // Motor settings
  m_cbUseMotor->SetValue(false);
  m_sMotorSpeedThreshold->SetValue(2.0);
  m_sMotorSpeed->SetValue(5.0);
  m_sMotorSpeedThreshold->Enable(false);
  m_sMotorSpeed->Enable(false);

  m_bBlockUpdate = false;
  Update();
}

void ConfigurationDialog::SetStartDateTime(wxDateTime datetime) {
  if (datetime.IsValid()) {
    if (m_WeatherRouting.GetSettingsDialog().m_cbUseLocalTime->GetValue())
      datetime = datetime.FromUTC();

    m_dpStartDate->SetValue(datetime);
    m_tpTime->SetValue(datetime);
    m_edited_controls.push_back(m_tpTime);
    m_edited_controls.push_back(m_dpStartDate);
  } else {
    wxMessageDialog mdlg(this, _("Invalid Date Time."),
                         wxString(_("Weather Routing"), wxOK | wxICON_WARNING));
    mdlg.ShowModal();
  }
}

#define GET_SPIN(FIELD)                                              \
  if (NO_EDITED_CONTROLS ||                                          \
      std::find(m_edited_controls.begin(), m_edited_controls.end(),  \
                (wxObject*)m_s##FIELD) != m_edited_controls.end()) { \
    configuration.FIELD = m_s##FIELD->GetValue();                    \
    m_s##FIELD->SetForegroundColour(wxColour(0, 0, 0));              \
  }

#define GET_CHOICE(FIELD)                                                     \
  if (NO_EDITED_CONTROLS ||                                                   \
      std::find(m_edited_controls.begin(), m_edited_controls.end(),           \
                (wxObject*)m_c##FIELD) != m_edited_controls.end())            \
    if (m_c##FIELD->GetValue() != wxEmptyString) {                            \
      configuration.FIELD = m_c##FIELD->GetValue();                           \
      if (m_c##FIELD->GetString(m_c##FIELD->GetCount() - 1) == wxEmptyString) \
        m_c##FIELD->Delete(m_c##FIELD->GetCount() - 1);                       \
    }
	

void ConfigurationDialog::Update() {
  // Prevent recursive updates when controls trigger each other
  if (m_bBlockUpdate) return;

  // Enable/disable the start-position combobox depending on radio selection
  m_cStart->Enable(!m_rbStartFromBoat->GetValue());

  bool refresh = false;

  // ---------------------------------------------------------------------
  // 1. Select the active route
  //
  // The modern WeatherRouting architecture uses the list control as the
  // authoritative source of selection. This dialog edits the configuration
  // of the *currently selected* route only.
  //
  // (Multi-route editing can be added later by iterating over all selected
  // overlays, but for now we keep it simple and stable.)
  // ---------------------------------------------------------------------
  RouteMapOverlay* overlay = m_WeatherRouting.FirstCurrentRouteMap();
  if (!overlay) {
    // No route selected ? nothing to update
    return;
  }

  // Retrieve the current configuration for the selected route
  RouteMapConfiguration configuration = overlay->GetConfiguration();

  // ---------------------------------------------------------------------
  // 2. Start type (boat vs. fixed position)
  // ---------------------------------------------------------------------
  if (m_rbStartFromBoat->GetValue())
    configuration.StartType = RouteMapConfiguration::START_FROM_BOAT;
  else
    configuration.StartType = RouteMapConfiguration::START_FROM_POSITION;

  // Only allow selecting a start position if not starting from boat
  if (configuration.StartType == RouteMapConfiguration::START_FROM_POSITION)
    GET_CHOICE(Start);

  GET_CHOICE(End);
  GET_CHECKBOX(UseCurrentTime);

  // ---------------------------------------------------------------------
  // 3. Start date (preserve time component)
  //
  // wxDateTime has strict validity rules. Passing an invalid wxDateTime
  // into SetValue() or converting between UTC/local can trigger wxCHECK
  // assertions. To avoid this, we:
  //
  //   ? defensively check configuration.StartTime.IsValid()
  //   ? fall back to wxDateTime::Now() if invalid
  //   ? convert UTC?local only *after* ensuring validity
  //
  // This ensures the dialog never crashes due to invalid timestamps.
  // ---------------------------------------------------------------------
  if (NO_EDITED_CONTROLS ||
      std::find(m_edited_controls.begin(), m_edited_controls.end(),
                (wxObject*)m_dpStartDate) != m_edited_controls.end()) {
    if (!m_dpStartDate->GetDateCtrlValue().IsValid()) goto after_updates;

    wxDateTime time = configuration.StartTime;

    if (!time.IsValid()) {
      // Defensive fallback:
      // Avoid passing invalid wxDateTime into SetValue() or FromUTC().
      // Using "now" keeps the UI populated and avoids wxCHECK aborts.
      time = wxDateTime::Now();
    }

    // Convert to local time *only if requested*
    if (m_WeatherRouting.GetSettingsDialog().m_cbUseLocalTime->GetValue())
      time = time.FromUTC();

    // Preserve the time-of-day while updating the date
    wxDateTime date = m_dpStartDate->GetDateCtrlValue();
    date.SetHour(time.GetHour());
    date.SetMinute(time.GetMinute());
    date.SetSecond(time.GetSecond());

    // Convert back to UTC if needed
    if (m_WeatherRouting.GetSettingsDialog().m_cbUseLocalTime->GetValue())
      date = date.ToUTC();

    configuration.StartTime = date;
    m_dpStartDate->SetForegroundColour(*wxBLACK);
  }

  // ---------------------------------------------------------------------
  // 4. Start time (preserve date component)
  //
  // Same defensive pattern as above:
  //   ? ensure configuration.StartTime is valid
  //   ? convert UTC?local only after validity is guaranteed
  //   ? apply user-selected time-of-day
  //   ? convert back to UTC if needed
  //
  // This avoids DST-related errors and wxDateTime assertion failures.
  // ---------------------------------------------------------------------
  if (NO_EDITED_CONTROLS ||
      std::find(m_edited_controls.begin(), m_edited_controls.end(),
                (wxObject*)m_tpTime) != m_edited_controls.end()) {
    wxDateTime time = configuration.StartTime;

    if (!time.IsValid()) {
      // Defensive fallback for invalid timestamps
      time = wxDateTime::Now();
    }

    if (m_WeatherRouting.GetSettingsDialog().m_cbUseLocalTime->GetValue())
      time = time.FromUTC();

    // Apply the time-of-day from the control
    time.SetHour(m_tpTime->GetTimeCtrlValue().GetHour());
    time.SetMinute(m_tpTime->GetTimeCtrlValue().GetMinute());
    time.SetSecond(m_tpTime->GetTimeCtrlValue().GetSecond());

    if (m_WeatherRouting.GetSettingsDialog().m_cbUseLocalTime->GetValue())
      time = time.ToUTC();

    configuration.StartTime = time;
    m_tpTime->SetForegroundColour(*wxBLACK);
  }

  // ---------------------------------------------------------------------
  // 5. Boat file
  // ---------------------------------------------------------------------
  if (!m_tBoat->GetValue().empty()) {
    configuration.boatFileName = m_tBoat->GetValue();
    m_tBoat->SetForegroundColour(*wxBLACK);
  }

  // ---------------------------------------------------------------------
  // 6. Time step (hours + minutes)
  // ---------------------------------------------------------------------
  if (NO_EDITED_CONTROLS ||
      std::find(m_edited_controls.begin(), m_edited_controls.end(),
                (wxObject*)m_sTimeStepHours) != m_edited_controls.end() ||
      std::find(m_edited_controls.begin(), m_edited_controls.end(),
                (wxObject*)m_sTimeStepMinutes) != m_edited_controls.end()) {
    configuration.DeltaTime = 60 * (60 * m_sTimeStepHours->GetValue() +
                                    m_sTimeStepMinutes->GetValue());

    m_sTimeStepHours->SetForegroundColour(*wxBLACK);
    m_sTimeStepMinutes->SetForegroundColour(*wxBLACK);
  }

  // ---------------------------------------------------------------------
  // 7. Integrator selection
  // ---------------------------------------------------------------------
  if (m_cIntegrator->GetValue() == _T("Newton"))
    configuration.Integrator = RouteMapConfiguration::NEWTON;
  else if (m_cIntegrator->GetValue() == _T("Runge Kutta"))
    configuration.Integrator = RouteMapConfiguration::RUNGE_KUTTA;

  // ---------------------------------------------------------------------
  // 8. Numeric configuration fields
  // ---------------------------------------------------------------------
  GET_SPIN(MaxDivertedCourse);
  GET_SPIN(MaxCourseAngle);
  GET_SPIN(MaxSearchAngle);
  GET_SPIN(MaxTrueWindKnots);
  GET_SPIN(MaxApparentWindKnots);

  GET_SPIN(MaxSwellMeters);
  GET_SPIN(MaxLatitude);
  GET_SPIN(TackingTime);
  GET_SPIN(JibingTime);
  GET_SPIN(SailPlanChangeTime);
  GET_SPIN(WindVSCurrent);

  if (m_sWindStrength->IsEnabled())
    configuration.WindStrength = m_sWindStrength->GetValue() / 100.0;

  if (m_sUpwindEfficiency->IsEnabled())
    configuration.UpwindEfficiency = m_sUpwindEfficiency->GetValue() / 100.0;

  if (m_sDownwindEfficiency->IsEnabled())
    configuration.DownwindEfficiency =
        m_sDownwindEfficiency->GetValue() / 100.0;

  if (m_sNightCumulativeEfficiency->IsEnabled())
    configuration.NightCumulativeEfficiency =
        m_sNightCumulativeEfficiency->GetValue() / 100.0;

  // ---------------------------------------------------------------------
  // 9. Checkboxes and climatology settings
  // ---------------------------------------------------------------------
  GET_CHECKBOX(AvoidCycloneTracks);
  GET_SPIN(CycloneMonths);
  GET_SPIN(CycloneDays);
  GET_SPIN(SafetyMarginLand);

  GET_CHECKBOX(DetectLand);
  GET_CHECKBOX(DetectBoundary);
  GET_CHECKBOX(Currents);
  GET_CHECKBOX(OptimizeTacking);

  GET_CHECKBOX(InvertedRegions);
  GET_CHECKBOX(Anchoring);

  GET_CHECKBOX(UseGrib);

  if (m_cClimatologyType->GetSelection() != -1)
    configuration.ClimatologyType = (RouteMapConfiguration::ClimatologyDataType)
                                        m_cClimatologyType->GetSelection();

  GET_CHECKBOX(AllowDataDeficient);

  GET_SPIN(FromDegree);
  GET_SPIN(ToDegree);
  GET_SPIN(ByDegrees);

  // ---------------------------------------------------------------------
  // 10. Motor settings
  // ---------------------------------------------------------------------
  GET_CHECKBOX(UseMotor);

  if (NO_EDITED_CONTROLS ||
      std::find(m_edited_controls.begin(), m_edited_controls.end(),
                (wxObject*)m_sMotorSpeedThreshold) != m_edited_controls.end()) {
    configuration.MotorSpeedThreshold = m_sMotorSpeedThreshold->GetValue();
    m_sMotorSpeedThreshold->SetForegroundColour(*wxBLACK);
  }

  if (NO_EDITED_CONTROLS ||
      std::find(m_edited_controls.begin(), m_edited_controls.end(),
                (wxObject*)m_sMotorSpeed) != m_edited_controls.end()) {
    configuration.MotorSpeed = m_sMotorSpeed->GetValue();
    m_sMotorSpeed->SetForegroundColour(*wxBLACK);
  }

  // ---------------------------------------------------------------------
  // 11. Apply updated configuration to the selected route
  // ---------------------------------------------------------------------
  overlay->SetConfiguration(configuration);

  // If start or end changed, route must be recomputed
  {
    RouteMapConfiguration newc = overlay->GetConfiguration();

    if (newc.StartLat != configuration.StartLat ||
        newc.StartLon != configuration.StartLon) {
      overlay->Reset();
      refresh = true;

    } else if (newc.EndLat != configuration.EndLat ||
               newc.EndLon != configuration.EndLon) {
      refresh = true;
    }
  }

after_updates:

  // ---------------------------------------------------------------------
  // 12. Sanity check for degree stepping
  // ---------------------------------------------------------------------
  double by = m_sByDegrees->GetValue();
  if (m_sToDegree->GetValue() - m_sFromDegree->GetValue() < 2 * by) {
    wxMessageDialog mdlg(
        this, _("Warning: less than 4 different degree steps specified\n"),
        wxString(_("Weather Routing"), wxOK | wxICON_WARNING));
    mdlg.ShowModal();
  }

  // Update internal caches
  m_WeatherRouting.UpdateCurrentConfigurations();

  // Refresh chart if needed
  if (refresh) m_WeatherRouting.GetParent()->Refresh();

  // Schedule auto-save
  m_WeatherRouting.ScheduleAutoSave();
}
