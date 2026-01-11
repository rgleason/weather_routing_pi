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

#include "SettingsDialog.h"
#include "Utilities.h"
#include "Boat.h"
#include "RouteMapOverlay.h"
#include "weather_routing_pi.h"
#include "WeatherRouting.h"

#ifdef __WXMSW__
#include "AddressSpaceMonitor.h"
#endif

const wxString SettingsDialog::column_names[] = {"",  // "Visible" column
                                                 "Boat",
                                                 "Start Type",
                                                 "Start",
                                                 "Start Time",
                                                 "End",
                                                 "End Time",
                                                 "Time",
                                                 "Distance",
                                                 "Avg Speed",
                                                 "Max Speed",
                                                 "Avg Speed Ground",
                                                 "Max Speed Ground",
                                                 "Avg Wind",
                                                 "Max Wind",
                                                 "Max Wind Gust",
                                                 "Avg Current",
                                                 "Max Current",
                                                 "Avg Swell",
                                                 "Max Swell",
                                                 "Upwind Percentage",
                                                 "Port Starboard",
                                                 "Tacks",
                                                 "Jibes",
                                                 "Sail Plan Changes",
                                                 "Sailing Comfort",
                                                 "State"};

SettingsDialog::SettingsDialog(wxWindow* parent)
    : SettingsDialogBase(
#ifdef __WXOSX__
          parent, wxID_ANY, _("Weather Routing Settings"), wxDefaultPosition,
          wxDefaultSize,
          wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER | wxSTAY_ON_TOP
#else
          parent
#endif
          )
#ifdef __WXMSW__
      ,
      m_staticTextMemoryStats(nullptr),
      m_usageSizer(nullptr),
      m_spinAutoStopThreshold(nullptr),
      m_checkEnableAutoStop(nullptr),
      m_spinMemoryCheckInterval(nullptr)
#endif
{

#ifdef __WXMSW__
  wxSizer* groupSizer = nullptr;  // <-- Declare here

  wxLogMessage("SettingsDialog::Constructor - Windows-specific code starting");
  
  // Set Gauge Range to 100
  m_gaugeMemoryUsage->SetRange(100);
  wxLogMessage("SettingsDialog::Constructor - Gauge range set to 100");

  // Reorganize the AddressSpaceMonitor groupbox layout dynamically
  wxWindow* gaugeParent = m_gaugeMemoryUsage->GetParent();
  wxLogMessage("SettingsDialog::Constructor - gaugeParent = %p", gaugeParent);

  // Get gaugeParent
  if (gaugeParent) {
    groupSizer = gaugeParent->GetSizer();   // Find groupSizer if null
    wxLogMessage("SettingsDialog::Constructor - groupSizer = %p (BEFORE fix)",
                 groupSizer);  

    // If groupSizer is NULL, try to get it from the parent's containing
      if (!groupSizer && gaugeParent->GetParent()) {
      wxWindow* grandParent = gaugeParent->GetParent();
      wxSizer* grandParentSizer = grandParent->GetSizer();

      if (grandParentSizer) {
        wxLogMessage(
            "SettingsDialog::Constructor - Searching grandParent sizer for "
            "wxStaticBox sizer");
        wxSizerItemList& items = grandParentSizer->GetChildren();
        for (wxSizerItemList::iterator it = items.begin(); it != items.end();
             ++it) {
          wxSizer* subSizer = (*it)->GetSizer();
          if (subSizer) {
            wxStaticBoxSizer* staticBoxSizer =
                dynamic_cast<wxStaticBoxSizer*>(subSizer);
            if (staticBoxSizer &&
                staticBoxSizer->GetStaticBox() == gaugeParent) {
              groupSizer = staticBoxSizer;
              wxLogMessage(
                  "SettingsDialog::Constructor - Found wxStaticBoxSizer: %p",
                  groupSizer);
              break;
            }
          }
        }
      }
    }
  

    wxLogMessage("SettingsDialog::Constructor - groupSizer = %p (AFTER fix)",
                 groupSizer);

    if (groupSizer) {
      wxLogMessage(
          "SettingsDialog::Constructor - Starting dynamic layout "
          "reorganization");

      // --- Begin custom order for AddressSpaceMonitor settings ---

      // 1. Checkbox "Alert Threshold"
      m_checkSuppressAlert =
          new wxCheckBox(gaugeParent, wxID_ANY, _("Enable Alert Threshold"));
      groupSizer->Add(m_checkSuppressAlert, 0, wxALL | wxEXPAND, FromDIP(5));

      // 2. Checkbox "AutoStop Threshold"
      m_checkEnableAutoStop =
          new wxCheckBox(gaugeParent, wxID_ANY, _("Enable AutoStop Threshold"));
      groupSizer->Add(m_checkEnableAutoStop, 0, wxALL | wxEXPAND, FromDIP(5));

      // 3. Checkbox "Log messages"
      m_checkLogUsage =
          new wxCheckBox(gaugeParent, wxID_ANY, _("Log messages"));
      groupSizer->Add(m_checkLogUsage, 0, wxALL | wxEXPAND, FromDIP(5));

      // 4. "Alert Threshold" adjustable value
      wxBoxSizer* alertThresholdSizer = new wxBoxSizer(wxHORIZONTAL);
      wxStaticText* alertLabel =
          new wxStaticText(gaugeParent, wxID_ANY, _("Alert Threshold (%)"));
      m_spinThreshold = new wxSpinCtrlDouble(
          gaugeParent, wxID_ANY, wxEmptyString, wxDefaultPosition,
          wxDefaultSize, wxSP_ARROW_KEYS, 0.0, 100.0, 80.0, 1.0);
      m_spinThreshold->SetDigits(0);
      alertThresholdSizer->Add(alertLabel, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT,
                               FromDIP(5));
      alertThresholdSizer->Add(m_spinThreshold, 0, wxALIGN_CENTER_VERTICAL);
      alertThresholdSizer->AddStretchSpacer(1);
      groupSizer->Add(alertThresholdSizer, 0, wxEXPAND | wxALL, FromDIP(5));

      // 5. "AutoStop Threshold" adjustable value
      wxBoxSizer* autoStopThresholdSizer = new wxBoxSizer(wxHORIZONTAL);
      wxStaticText* autoStopLabel =
          new wxStaticText(gaugeParent, wxID_ANY, _("AutoStop Threshold (%)"));
      m_spinAutoStopThreshold = new wxSpinCtrlDouble(
          gaugeParent, wxID_ANY, wxEmptyString, wxDefaultPosition,
          wxDefaultSize, wxSP_ARROW_KEYS, 0.0, 100.0, 85.0, 1.0);
      m_spinAutoStopThreshold->SetDigits(0);
      autoStopThresholdSizer->Add(
          autoStopLabel, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, FromDIP(5));
      autoStopThresholdSizer->Add(m_spinAutoStopThreshold, 0,
                                  wxALIGN_CENTER_VERTICAL);
      autoStopThresholdSizer->AddStretchSpacer(1);
      groupSizer->Add(autoStopThresholdSizer, 0, wxEXPAND | wxALL, FromDIP(5));

      // 6. "Memory Check Interval" adjustable value
      wxBoxSizer* intervalSizer = new wxBoxSizer(wxHORIZONTAL);
      wxStaticText* intervalLabel = new wxStaticText(
          gaugeParent, wxID_ANY, _("Memory Check Interval (ms)"));
      m_spinMemoryCheckInterval = new wxSpinCtrl(
          gaugeParent, wxID_ANY, wxEmptyString, wxDefaultPosition,
          wxDefaultSize, wxSP_ARROW_KEYS, 100, 10000, 500);
      intervalSizer->Add(intervalLabel, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT,
                         FromDIP(5));
      intervalSizer->Add(m_spinMemoryCheckInterval, 0, wxALIGN_CENTER_VERTICAL);
      intervalSizer->AddStretchSpacer(1);
      groupSizer->Add(intervalSizer, 0, wxEXPAND | wxALL, FromDIP(5));

      // Connect event handlers
      m_spinThreshold->Connect(
          wxEVT_SPINCTRLDOUBLE,
          wxSpinDoubleEventHandler(SettingsDialog::OnThresholdChanged), NULL,
          this);
      m_checkSuppressAlert->Connect(
          wxEVT_CHECKBOX,
          wxCommandEventHandler(SettingsDialog::OnSuppressAlertChanged), NULL,
          this);
      m_checkLogUsage->Connect(
          wxEVT_CHECKBOX,
          wxCommandEventHandler(SettingsDialog::OnLogUsageChanged), NULL, this);
      m_spinAutoStopThreshold->Connect(
          wxEVT_SPINCTRLDOUBLE,
          wxSpinDoubleEventHandler(SettingsDialog::OnAutoStopThresholdChanged),
          NULL, this);
      m_checkEnableAutoStop->Connect(
          wxEVT_CHECKBOX,
          wxCommandEventHandler(SettingsDialog::OnAutoStopEnabledChanged), NULL,
          this);
      m_spinMemoryCheckInterval->Connect(
          wxEVT_SPINCTRL,
          wxSpinEventHandler(SettingsDialog::OnMemoryCheckIntervalChanged),
          NULL, this);

      // Layout after adding controls
      groupSizer->Layout();
      gaugeParent->Layout();
      wxLogMessage(
          "SettingsDialog::Constructor - Dynamic layout complete (custom order "
          "applied)");
    }
  }

  // Final layout
  Layout();
  Fit();

  wxLogMessage("SettingsDialog::Constructor - Initialization complete");

#endif
}


SettingsDialog::~SettingsDialog() {
#ifdef __WXMSW__
  wxLogMessage("SettingsDialog: Destructor starting");

  if (m_spinThreshold) {
    m_spinThreshold->Disconnect(
        wxEVT_SPINCTRLDOUBLE,
        wxSpinDoubleEventHandler(SettingsDialog::OnThresholdChanged), NULL,
        this);
  }
  if (m_checkSuppressAlert) {
    m_checkSuppressAlert->Disconnect(
        wxEVT_CHECKBOX,
        wxCommandEventHandler(SettingsDialog::OnSuppressAlertChanged), NULL,
        this);
  }
  if (m_checkLogUsage) {
    m_checkLogUsage->Disconnect(
        wxEVT_CHECKBOX,
        wxCommandEventHandler(SettingsDialog::OnLogUsageChanged), NULL, this);
  }
  if (m_spinAutoStopThreshold) {
    m_spinAutoStopThreshold->Disconnect(
        wxEVT_SPINCTRLDOUBLE,
        wxSpinDoubleEventHandler(SettingsDialog::OnAutoStopThresholdChanged),
        NULL, this);
  }
  if (m_checkEnableAutoStop) {
    m_checkEnableAutoStop->Disconnect(
        wxEVT_CHECKBOX,
        wxCommandEventHandler(SettingsDialog::OnAutoStopEnabledChanged), NULL,
        this);
  }
  if (m_spinMemoryCheckInterval) {
    m_spinMemoryCheckInterval->Disconnect(
        wxEVT_SPINCTRL,
        wxSpinEventHandler(SettingsDialog::OnMemoryCheckIntervalChanged), NULL,
        this);
  }

  // Clear the gauge and text label references in the monitor
  AddressSpaceMonitor* monitor = GetMonitor();
  if (monitor && monitor->IsValid()) {
    monitor->SetGauge(nullptr);
    monitor->SetTextLabel(nullptr);
    wxLogMessage("SettingsDialog: Destructor - Cleared monitor UI references");
  }
  m_staticTextMemoryStats = nullptr;
  m_usageSizer = nullptr;
  m_spinAutoStopThreshold = nullptr;
  m_checkEnableAutoStop = nullptr;
  m_spinMemoryCheckInterval = nullptr;

  wxLogMessage("SettingsDialog: Destructor complete");
#endif
}

// Event handlers
void SettingsDialog::LoadSettings() {
  wxFileConfig* pConf = GetOCPNConfigObject();
  pConf->SetPath(_T( "/PlugIns/WeatherRouting" ));

  wxString CursorColorStr = m_cpCursorRoute->GetColour().GetAsString();
  pConf->Read(_T("CursorColor"), &CursorColorStr, CursorColorStr);
  m_cpCursorRoute->SetColour(wxColour(CursorColorStr));

  wxString DestinationColorStr =
      m_cpDestinationRoute->GetColour().GetAsString();
  pConf->Read(_T("DestinationColor"), &DestinationColorStr,
              DestinationColorStr);
  m_cpDestinationRoute->SetColour(wxColour(DestinationColorStr));

  int RouteThickness = m_sRouteThickness->GetValue();
  pConf->Read(_T("RouteThickness"), &RouteThickness, RouteThickness);
  m_sRouteThickness->SetValue(RouteThickness);

  int IsoChronThickness = m_sIsoChronThickness->GetValue();
  pConf->Read(_T("IsoChronThickness"), &IsoChronThickness, IsoChronThickness);
  m_sIsoChronThickness->SetValue(IsoChronThickness);

  int AlternateRouteThickness = m_sAlternateRouteThickness->GetValue();
  pConf->Read(_T("AlternateRouteThickness"), &AlternateRouteThickness,
              AlternateRouteThickness);
  m_sAlternateRouteThickness->SetValue(AlternateRouteThickness);

  bool DisplayCursorRoute = m_cbDisplayCursorRoute->GetValue();
  pConf->Read(_T("CursorRoute"), &DisplayCursorRoute, DisplayCursorRoute);
  m_cbDisplayCursorRoute->SetValue(DisplayCursorRoute);

  bool AlternatesForAll = m_cbAlternatesForAll->GetValue();
  pConf->Read(_T("AlternatesForAll"), &AlternatesForAll, AlternatesForAll);
  m_cbAlternatesForAll->SetValue(AlternatesForAll);

  bool MarkAtPolarChange = m_cbMarkAtPolarChange->GetValue();
  pConf->Read(_T("MarkAtPolarChange"), &MarkAtPolarChange, MarkAtPolarChange);
  m_cbMarkAtPolarChange->SetValue(MarkAtPolarChange);

  bool DisplayWindBarbs = m_cbDisplayWindBarbs->GetValue();
  pConf->Read(_T("DisplayWindBarbs"), &DisplayWindBarbs, DisplayWindBarbs);
  m_cbDisplayWindBarbs->SetValue(DisplayWindBarbs);

  int WindBarbsOnRouteThickness = m_sWindBarbsOnRouteThickness->GetValue();
  pConf->Read(_T("WindBarbsOnRouteThickness"), &WindBarbsOnRouteThickness,
              WindBarbsOnRouteThickness);
  m_sWindBarbsOnRouteThickness->SetValue(WindBarbsOnRouteThickness);

  bool WindBarbsOnRouteApparent = m_cbDisplayApparentWindBarbs->GetValue();
  pConf->Read(_T("WindBarbsOnRouteApparent"), &WindBarbsOnRouteApparent,
              WindBarbsOnRouteApparent);
  m_cbDisplayApparentWindBarbs->SetValue(WindBarbsOnRouteApparent);

  bool DisplayComfortOnRoute = m_cbDisplayComfort->GetValue();
  pConf->Read(_T("DisplayComfortOnRoute"), &DisplayComfortOnRoute,
              DisplayComfortOnRoute);
  m_cbDisplayComfort->SetValue(DisplayComfortOnRoute);

  bool DisplayCurrent = m_cbDisplayCurrent->GetValue();
  pConf->Read(_T("DisplayCurrent"), &DisplayCurrent, DisplayCurrent);
  m_cbDisplayCurrent->SetValue(DisplayCurrent);

  int ConcurrentThreads = wxThread::GetCPUCount();
  pConf->Read(_T("ConcurrentThreads"), &ConcurrentThreads, ConcurrentThreads);
  m_sConcurrentThreads->SetValue(ConcurrentThreads);

  // Set defaults
  bool columns[WeatherRouting::NUM_COLS];
  for (int i = 0; i < WeatherRouting::NUM_COLS; i++)
    columns[i] = i != WeatherRouting::BOAT &&
                 (i <= WeatherRouting::DISTANCE || i == WeatherRouting::STATE);

  for (int i = 0; i < WeatherRouting::NUM_COLS; i++) {
    if (i == 0)
      m_cblFields->Append(_("Visible"));
    else
      m_cblFields->Append(_(column_names[i]));
    pConf->Read(wxString::Format(_T("Column_") + _(column_names[i]), i),
                &columns[i], columns[i]);
    m_cblFields->Check(i, columns[i]);
  }

  m_cbUseLocalTime->SetValue((bool)pConf->Read(_T("UseLocalTime"), 0L));

#ifdef __WXMSW__
  LoadMemorySettings();
#endif

  Fit();

  wxPoint p = GetPosition();
  pConf->Read(_T ( "SettingsDialogX" ), &p.x, p.x);
  pConf->Read(_T ( "SettingsDialogY" ), &p.y, p.y);
  SetPosition(p);
#ifdef __OCPN__ANDROID__
  wxSize sz = ::wxGetDisplaySize();
  SetSize(0, 0, sz.x, sz.y - 40);
#endif
}

void SettingsDialog::SaveSettings() {
  wxFileConfig* pConf = GetOCPNConfigObject();
  pConf->SetPath(_T( "/PlugIns/WeatherRouting" ));

  pConf->Write(_T("CursorColor"), m_cpCursorRoute->GetColour().GetAsString());
  pConf->Write(_T("DestinationColor"),
               m_cpDestinationRoute->GetColour().GetAsString());
  pConf->Write(_T("RouteThickness"), m_sRouteThickness->GetValue());
  pConf->Write(_T("IsoChronThickness"), m_sIsoChronThickness->GetValue());
  pConf->Write(_T("AlternateRouteThickness"),
               m_sAlternateRouteThickness->GetValue());
  pConf->Write(_T("AlternatesForAll"), m_cbAlternatesForAll->GetValue());
  pConf->Write(_T("CursorRoute"), m_cbDisplayCursorRoute->GetValue());
  pConf->Write(_T("MarkAtPolarChange"), m_cbMarkAtPolarChange->GetValue());
  pConf->Write(_T("DisplayWindBarbs"), m_cbDisplayWindBarbs->GetValue());
  pConf->Write(_T("WindBarbsOnRouteThickness"),
               m_sWindBarbsOnRouteThickness->GetValue());
  pConf->Write(_T("WindBarbsOnRouteApparent"),
               m_cbDisplayApparentWindBarbs->GetValue());
  pConf->Write(_T("DisplayComfortOnRoute"), m_cbDisplayComfort->GetValue());
  pConf->Write(_T("DisplayCurrent"), m_cbDisplayCurrent->GetValue());
  pConf->Write(_T("ConcurrentThreads"), m_sConcurrentThreads->GetValue());

  for (int i = 0; i < WeatherRouting::NUM_COLS; i++)
    pConf->Write(wxString::Format(_T("Column_") + _(column_names[i]), i),
                 m_cblFields->IsChecked(i));

  pConf->Write(_T("UseLocalTime"), m_cbUseLocalTime->GetValue());

#ifdef __WXMSW__
  SaveMemorySettings();
#endif

  wxPoint p = GetPosition();
  pConf->Write(_T ( "SettingsDialogX" ), p.x);
  pConf->Write(_T ( "SettingsDialogY" ), p.y);
}


#ifdef __WXMSW__
AddressSpaceMonitor* SettingsDialog::GetMonitor() {
  WeatherRouting* weatherRouting = dynamic_cast<WeatherRouting*>(GetParent());

  if (!weatherRouting) {
    wxLogWarning(
        "SettingsDialog::GetMonitor() - GetParent() is not WeatherRouting");
    return nullptr;
  }

  weather_routing_pi* plugin = &weatherRouting->GetPlugin();

  if (!plugin) {
    wxLogWarning(
        "SettingsDialog::GetMonitor() - Could not get plugin reference from "
        "WeatherRouting");
    return nullptr;
  }

  wxLogMessage(
      "SettingsDialog::GetMonitor() - Successfully got monitor reference");
  return &plugin->GetAddressSpaceMonitor();
}


void SettingsDialog::LoadMemorySettings() {
  wxFileConfig* pConf = GetOCPNConfigObject();
  pConf->SetPath(_T("/PlugIns/WeatherRouting"));

  // Load values in the new order
  bool enableAlert = true;  // Default: alerts enabled
  pConf->Read(_T("MemoryEnableAlert"), &enableAlert, true);
  m_checkSuppressAlert->SetValue(enableAlert);

  bool enableAutoStop = true;  // Default: enabled
  pConf->Read(_T("MemoryEnableAutoStop"), &enableAutoStop, true);
  m_checkEnableAutoStop->SetValue(enableAutoStop);

  bool logUsage = false;
  pConf->Read(_T("MemoryLogUsage"), &logUsage, false);
  m_checkLogUsage->SetValue(logUsage);

  double threshold = 80.0;
  pConf->Read(_T("MemoryThreshold"), &threshold, 80.0);
  m_spinThreshold->SetValue(threshold);

  double autoStopThreshold = 85.0;
  pConf->Read(_T("MemoryAutoStopThreshold"), &autoStopThreshold, 85.0);
  m_spinAutoStopThreshold->SetValue(autoStopThreshold);

  int memoryCheckInterval = 500;
  pConf->Read(_T("MemoryCheckInterval"), &memoryCheckInterval, 500);
  m_spinMemoryCheckInterval->SetValue(memoryCheckInterval);

  // Apply to the monitor
  AddressSpaceMonitor* monitor = GetMonitor();
  if (monitor && monitor->IsValid()) {
    monitor->SetAlertEnabled(enableAlert);
    monitor->SetAutoStopEnabled(enableAutoStop);
    monitor->SetLoggingEnabled(logUsage);
    monitor->SetThresholdPercent(threshold);
    monitor->SetAutoStopThreshold(autoStopThreshold);
    monitor->SetMemoryCheckInterval(memoryCheckInterval); 
    monitor->SetGauge(m_gaugeMemoryUsage);
    monitor->SetTextLabel(m_staticTextMemoryStats);
  }
}


void SettingsDialog::SaveMemorySettings() {
  wxFileConfig* pConf = GetOCPNConfigObject();
  pConf->SetPath(_T("/PlugIns/WeatherRouting"));

  // Save values in the new order
  pConf->Write(_T("MemoryEnableAlert"), m_checkSuppressAlert->GetValue());
  pConf->Write(_T("MemoryEnableAutoStop"), m_checkEnableAutoStop->GetValue());
  pConf->Write(_T("MemoryLogUsage"), m_checkLogUsage->GetValue());
  pConf->Write(_T("MemoryThreshold"), m_spinThreshold->GetValue());
  pConf->Write(_T("MemoryAutoStopThreshold"), m_spinAutoStopThreshold->GetValue());
  pConf->Write(_T("MemoryCheckInterval"), m_spinMemoryCheckInterval->GetValue());
}

void SettingsDialog::OnThresholdChanged(wxSpinDoubleEvent& event) {
  AddressSpaceMonitor* monitor = GetMonitor();
  if (!monitor) return;
  double newAlert = m_spinThreshold->GetValue();
  double autoStop = m_spinAutoStopThreshold->GetValue();

  // Enforce: Alert must be at least 3% less than AutoStop
  if (newAlert >= autoStop - 2.99) {
    double fixedAlert = autoStop - 3.0;
    m_spinThreshold->SetValue(fixedAlert);
    monitor->SetThresholdPercent(fixedAlert);

    wxMessageBox(
        _("Alert threshold must be at least 3% less than AutoStop threshold."),
        _("Threshold Conflict"), wxOK | wxICON_WARNING, this);
  } else {
    monitor->SetThresholdPercent(newAlert);
    wxLogMessage("SettingsDialog: Alert threshold changed to %.0f%%", newAlert);
  }

  UpdateThresholdRanges();
  monitor->CheckAndAlert();
  SaveMemorySettings();
}

void SettingsDialog::UpdateThresholdRanges() {
  double autoStop = m_spinAutoStopThreshold->GetValue();
  double alert = m_spinThreshold->GetValue();

  // Set Alert's max to AutoStop - 3
  double alertMin = m_spinThreshold->GetMin();  // or your intended min
  double alertMax = autoStop - 3.0;
  if (alertMax < alertMin) alertMax = alertMin;

  m_spinThreshold->SetRange(alertMin, alertMax);

  // Set AutoStop's min to Alert + 3
  double autoStopMax =
      m_spinAutoStopThreshold->GetMax();  // or your intended max
  double autoStopMin = alert + 3.0;
  if (autoStopMin > autoStopMax) autoStopMin = autoStopMax;

  m_spinAutoStopThreshold->SetRange(autoStopMin, autoStopMax);
}


void SettingsDialog::OnSuppressAlertChanged(wxCommandEvent& event) {
  AddressSpaceMonitor* monitor = GetMonitor();
  if (!monitor) {
    wxLogWarning(
        "SettingsDialog::OnSuppressAlertChanged - No monitor available");
    return;
  }

  bool enableAlert = m_checkSuppressAlert->GetValue();
  monitor->SetAlertEnabled(enableAlert);
  wxLogMessage("SettingsDialog: Alert threshold message %s",
               enableAlert ? "enabled" : "disabled");

  SaveMemorySettings();
}

void SettingsDialog::OnLogUsageChanged(wxCommandEvent& event) {
  AddressSpaceMonitor* monitor = GetMonitor();
  if (monitor) {
    monitor->SetLoggingEnabled(m_checkLogUsage->GetValue());
  }
  SaveMemorySettings();
}

void SettingsDialog::OnAutoStopThresholdChanged(wxSpinDoubleEvent& event) {
  AddressSpaceMonitor* monitor = GetMonitor();
  if (monitor) {
    double newAutoStop = m_spinAutoStopThreshold->GetValue();
    double alert = m_spinThreshold->GetValue();

    // Enforce: AutoStop must be at least 3% greater than Alert
    if (newAutoStop <= alert + 2.99) {
      double fixedAutoStop = alert + 3.0;
      m_spinAutoStopThreshold->SetValue(fixedAutoStop);
      monitor->SetAutoStopThreshold(fixedAutoStop);

      wxMessageBox(_("AutoStop threshold must be at least 3% greater than "
                     "Alert threshold."),
                   _("Threshold Conflict"), wxOK | wxICON_WARNING, this);
    } else {
      monitor->SetAutoStopThreshold(newAutoStop);
      wxLogMessage("SettingsDialog: Auto-stop threshold changed to %.0f%%",
                   newAutoStop);
    }
  }
  UpdateThresholdRanges();
  SaveMemorySettings();
}


void SettingsDialog::OnAutoStopEnabledChanged(wxCommandEvent& event) {
  AddressSpaceMonitor* monitor = GetMonitor();
  if (monitor) {
    bool enabled = m_checkEnableAutoStop->GetValue();
    monitor->SetAutoStopEnabled(enabled);
    wxLogMessage("SettingsDialog: Auto-stop %s",
                 enabled ? "enabled" : "disabled");
  }
  SaveMemorySettings();
}

void SettingsDialog::OnMemoryCheckIntervalChanged(wxSpinEvent & event) {
  AddressSpaceMonitor* monitor = GetMonitor();
  if (monitor) {
    int interval = m_spinMemoryCheckInterval->GetValue();
    monitor->SetMemoryCheckInterval(interval);
  }
  SaveMemorySettings();
}
#endif

// Platform-independent methods

void SettingsDialog::OnUpdate() {
  WeatherRouting* weather_routing = dynamic_cast<WeatherRouting*>(GetParent());
  if (weather_routing) weather_routing->UpdateDisplaySettings();
}

void SettingsDialog::OnUpdateColumns(wxCommandEvent& event) {
  WeatherRouting* weather_routing = dynamic_cast<WeatherRouting*>(GetParent());
  if (weather_routing) weather_routing->UpdateColumns();
}

void SettingsDialog::OnHelp(wxCommandEvent& event) {
#ifdef __OCPN__ANDROID__
  wxSize sz = ::wxGetDisplaySize();
  SetSize(0, 0, sz.x, sz.y - 40);
#endif
  wxString mes =
      _("\
Cursor Route -- optimal route closest to the cursor\n\
Destination Route -- optimal route to the desired destination\n\
Route Thickness -- thickness to draw Cursor and Destination Routes\n\
Iso Chron Thickness -- thickness for isochrones on map\n\
Alternate Routes Thickness -- thickness for alternate routes\n");

  mes +=
      _("Note: All thicknesses can be set to 0 to disable their display\n\
Alternates for all Isochrones -- display all alternate routes not only \
the ones which reach the last isochrone\n\
Squares At Sail Changes -- render squares along Routes whenever \
a sail change is made\n");

  mes +=
      _("Filter Routes by Climatology -- This currently does nothing, \
but I intended to make weather route maps which derive data \
from grib and climatology clearly render which data was used where \n\\n\
Number of Concurrent Threads -- if there are multiple configurations, \
they can be computed in separate threads which allows a speedup \
if there are multiple processors\n");

  wxMessageDialog mdlg(this, mes, _("Weather Routing"),
                       wxOK | wxICON_INFORMATION);
  mdlg.ShowModal();
}
