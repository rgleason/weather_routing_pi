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
#ifndef __WXOSX__
    : SettingsDialogBase(parent)
#ifdef __WXMSW__
      ,
      m_memoryUpdateTimer(this),
      m_staticTextMemoryStats(nullptr)
#endif
#else
    : SettingsDialogBase(
          parent, wxID_ANY, _("Weather Routing Settings"), wxDefaultPosition,
          wxDefaultSize,
          wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER | wxSTAY_ON_TOP)
#ifdef __WXMSW__
      ,
      m_memoryUpdateTimer(this),
      m_staticTextMemoryStats(nullptr)
#endif
#endif
{
#ifdef __WXMSW__

    // SET GAUGE RANGE TO 100
    m_gaugeMemoryUsage->SetRange(100);

    // NOTE: Text label will be created dynamically by UpdateMemoryGauge()
    // when the dialog is first shown, because the sizer is not available
    // during construction.

    // Connect memory monitor event handlers
    m_spinThreshold->Connect(
        wxEVT_SPINCTRLDOUBLE,
        wxSpinDoubleEventHandler(SettingsDialog::OnThresholdChanged), NULL,
        this);

 // Connect memory monitor event handlers
  m_spinThreshold->Connect(
      wxEVT_SPINCTRLDOUBLE,
      wxSpinDoubleEventHandler(SettingsDialog::OnThresholdChanged), NULL, this);

  m_checkSuppressAlert->Connect(
      wxEVT_CHECKBOX,
      wxCommandEventHandler(SettingsDialog::OnSuppressAlertChanged), NULL,
      this);

  m_checkLogUsage->Connect(
      wxEVT_CHECKBOX, wxCommandEventHandler(SettingsDialog::OnLogUsageChanged),
      NULL, this);

  // Connect timer
  m_memoryUpdateTimer.Connect(
      wxEVT_TIMER, wxTimerEventHandler(SettingsDialog::OnMemoryUpdateTimer),
      NULL, this);

   // IMPORTANT: Do NOT start timer or update gauge here
   // Wait until LoadSettings() is called, which happens after full
   // initialization

   // Final layout to accommodate new control
   Layout();
   Fit();

   wxLogMessage(
       "SettingsDialog::Constructor - Initialization complete (timer NOT "
       "started yet)");
#endif
}

// Add this destructor implementation after the constructor
SettingsDialog::~SettingsDialog() {
#ifdef __WXMSW__
  wxLogMessage("SettingsDialog: Destructor starting");

  // STEP 1: Stop the timer IMMEDIATELY
  if (m_memoryUpdateTimer.IsRunning()) {
    m_memoryUpdateTimer.Stop();
    wxLogMessage("SettingsDialog: Destructor - Stopped memory update timer");
  }

  // STEP 2: Disconnect event handlers BEFORE processing events
  m_memoryUpdateTimer.Disconnect(
      wxEVT_TIMER, wxTimerEventHandler(SettingsDialog::OnMemoryUpdateTimer),
      NULL, this);

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

  // STEP 3: Process any pending events that were already queued
  // This prevents them from firing after the dialog is destroyed
  if (wxTheApp) {
    wxTheApp->ProcessPendingEvents();
  }

  // STEP 4: Clear the gauge reference in the monitor
  AddressSpaceMonitor* monitor = GetMonitor();
  if (monitor && monitor->IsValid()) {
    monitor->SetGauge(nullptr);
    wxLogMessage(
        "SettingsDialog: Destructor - Cleared gauge reference from monitor");
  }

  // STEP 5: Clear our pointer to the text label (it will be destroyed by
  // wxWidgets)
  m_staticTextMemoryStats = nullptr;

  wxLogMessage("SettingsDialog: Destructor complete");
#endif
}

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

  // Cursor Route optional
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

  // WindBarbsOnRoute Customization
  int WindBarbsOnRouteThickness = m_sWindBarbsOnRouteThickness->GetValue();
  pConf->Read(_T("WindBarbsOnRouteThickness"), &WindBarbsOnRouteThickness,
              WindBarbsOnRouteThickness);
  m_sWindBarbsOnRouteThickness->SetValue(WindBarbsOnRouteThickness);
  bool WindBarbsOnRouteApparent = m_cbDisplayApparentWindBarbs->GetValue();
  pConf->Read(_T("WindBarbsOnRouteApparent"), &WindBarbsOnRouteApparent,
              WindBarbsOnRouteApparent);
  m_cbDisplayApparentWindBarbs->SetValue(WindBarbsOnRouteApparent);

  // ComfortOnRoute Customization
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

  // set defaults
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
  // Load memory settings
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
  // CursorOnRoute Customization
  pConf->Write(_T("CursorRoute"), m_cbDisplayCursorRoute->GetValue());
  pConf->Write(_T("MarkAtPolarChange"), m_cbMarkAtPolarChange->GetValue());
  pConf->Write(_T("DisplayWindBarbs"), m_cbDisplayWindBarbs->GetValue());
  // WindBarbsOnRoute Customization
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
  // Save memory settings
  SaveMemorySettings();
#endif

  wxPoint p = GetPosition();
  pConf->Write(_T ( "SettingsDialogX" ), p.x);
  pConf->Write(_T ( "SettingsDialogY" ), p.y);
}

#ifdef __WXMSW__
AddressSpaceMonitor* SettingsDialog::GetMonitor() {
  // SettingsDialog's parent should be WeatherRouting
  WeatherRouting* weatherRouting = dynamic_cast<WeatherRouting*>(GetParent());

  if (!weatherRouting) {
    wxLogWarning(
        "SettingsDialog::GetMonitor() - GetParent() is not WeatherRouting");
    return nullptr;
  }

  // WeatherRouting has a reference to the plugin via m_WeatherRouting member
  // Check WeatherRouting.h to see how to access the plugin
  // Typically it's a reference, not a pointer
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
  pConf->SetPath(_T( "/PlugIns/WeatherRouting" ));

  double threshold = 80.0;
  pConf->Read(_T("MemoryThreshold"), &threshold, 80.0);

  bool suppressAlert = false;
  pConf->Read(_T("MemorySuppressAlert"), &suppressAlert, false);

  bool logUsage = false;
  pConf->Read(_T("MemoryLogUsage"), &logUsage, false);

  m_spinThreshold->SetValue(threshold);
  m_checkSuppressAlert->SetValue(suppressAlert);
  m_checkLogUsage->SetValue(logUsage);

  // Apply to the global monitor
  AddressSpaceMonitor* monitor = GetMonitor();
  if (monitor && monitor->IsValid()) {
    wxLogMessage(
        "SettingsDialog::LoadMemorySettings() - Configuring monitor: "
        "threshold=%.1f, suppressAlert=%d, logUsage=%d",
        threshold, suppressAlert, logUsage);
    monitor->SetThresholdPercent(threshold);
    monitor->SetAlertEnabled(!suppressAlert);
    monitor->SetLoggingEnabled(logUsage);
    monitor->SetGauge(m_gaugeMemoryUsage);

    // Start the timer
    if (!m_memoryUpdateTimer.IsRunning()) {
      m_memoryUpdateTimer.Start(2000);
      wxLogMessage(
          "SettingsDialog::LoadMemorySettings() - Started memory update timer");
    }

    // FIX: Force an immediate call to UpdateMemoryGauge to create the text
    // label Use CallAfter to ensure dialog is fully shown first
    wxLogMessage(
        "SettingsDialog::LoadMemorySettings() - Scheduling immediate update "
        "via CallAfter");
    CallAfter(&SettingsDialog::UpdateMemoryGauge);

    wxLogMessage(
        "SettingsDialog::LoadMemorySettings() - Configuration complete");
  } else {
    wxLogError(
        "SettingsDialog::LoadMemorySettings() - GetMonitor() returned NULL or "
        "invalid!");
  }
}

void SettingsDialog::SaveMemorySettings() {
  wxFileConfig* pConf = GetOCPNConfigObject();
  pConf->SetPath(_T( "/PlugIns/WeatherRouting" ));

  pConf->Write(_T("MemoryThreshold"), m_spinThreshold->GetValue());
  pConf->Write(_T("MemorySuppressAlert"), m_checkSuppressAlert->GetValue());
  pConf->Write(_T("MemoryLogUsage"), m_checkLogUsage->GetValue());
}

void SettingsDialog::OnThresholdChanged(wxSpinDoubleEvent& event) {
  AddressSpaceMonitor* monitor = GetMonitor();
  if (!monitor) {
    return;
  }

  double newThreshold = m_spinThreshold->GetValue();
  monitor->SetThresholdPercent(newThreshold);

  // Immediately check with new threshold
  monitor->CheckAndAlert();
}

void SettingsDialog::OnSuppressAlertChanged(wxCommandEvent& event) {
  AddressSpaceMonitor* monitor = GetMonitor();
  if (!monitor) {
    wxLogWarning(
        "SettingsDialog::OnSuppressAlertChanged - No monitor available");
    return;
  }

  if (m_checkSuppressAlert->GetValue()) {
    // User checked the box - suppress alerts
    monitor->DismissAlert();
    wxLogMessage("SettingsDialog: User suppressed alerts via checkbox");
  } else {
    // User unchecked the box - re-enable alerts
    monitor->alertDismissed = false;
    wxLogMessage("SettingsDialog: User re-enabled alerts via checkbox");

    // Immediately check if we should show an alert
    monitor->CheckAndAlert();
  }

  // Save the setting immediately so it persists
  SaveMemorySettings();  // ? OPTIONAL: Auto-save when user changes setting
}

void SettingsDialog::OnLogUsageChanged(wxCommandEvent& event) {
  AddressSpaceMonitor* monitor = GetMonitor();
  if (monitor) {
    monitor->SetLoggingEnabled(m_checkLogUsage->GetValue());
  }
  SaveMemorySettings();
}

void SettingsDialog::OnMemoryUpdateTimer(wxTimerEvent& event) {
  UpdateMemoryGauge();
}

void SettingsDialog::UpdateMemoryGauge() {
  static int callCount = 0;
  callCount++;

  // FIX: Only check IsShown() after the first successful run
  // This allows initial setup to happen
  static bool firstSuccessfulRun = false;

  if (firstSuccessfulRun && !IsShown()) {
    // Dialog was shown before but now is hidden
    wxLogMessage(
        "SettingsDialog::UpdateMemoryGauge() - Dialog hidden, stopping timer "
        "(call #%d)",
        callCount);
    if (m_memoryUpdateTimer.IsRunning()) {
      m_memoryUpdateTimer.Stop();
    }
    return;
  }

  if (!m_gaugeMemoryUsage) {
    wxLogError("SettingsDialog::UpdateMemoryGauge() - Gauge is NULL (call #%d)",
               callCount);
    if (m_memoryUpdateTimer.IsRunning()) {
      m_memoryUpdateTimer.Stop();
    }
    return;
  }

  AddressSpaceMonitor* monitor = GetMonitor();

  // Check pointer validity FIRST, then object validity
  if (!monitor) {
    wxLogError(
        "SettingsDialog::UpdateMemoryGauge() - Monitor is NULL (call #%d)",
        callCount);
    if (m_memoryUpdateTimer.IsRunning()) {
      m_memoryUpdateTimer.Stop();
    }
    return;
  }

  // Catch exceptions from accessing destroyed objects
  try {
    if (!monitor->IsValid()) {
      wxLogError(
          "SettingsDialog::UpdateMemoryGauge() - Monitor is invalid (call #%d)",
          callCount);
      if (m_memoryUpdateTimer.IsRunning()) {
        m_memoryUpdateTimer.Stop();
      }
      return;
    }
  } catch (...) {
    wxLogError(
        "SettingsDialog::UpdateMemoryGauge() - Exception accessing monitor "
        "(call #%d)",
        callCount);
    if (m_memoryUpdateTimer.IsRunning()) {
      m_memoryUpdateTimer.Stop();
    }
    return;
  }

  // Get memory stats
  size_t used = monitor->GetUsedAddressSpace();
  size_t total = monitor->GetTotalAddressSpace();
  double percent = monitor->GetUsagePercent();

  double usedGB = used / (1024.0 * 1024.0 * 1024.0);
  double totalGB = total / (1024.0 * 1024.0 * 1024.0);

  // Log periodically for debugging
  if (callCount % 5 == 0 || callCount <= 5) {
    wxLogMessage(
        "SettingsDialog::UpdateMemoryGauge() - Call #%d: %.1f%% (%.2f GB / "
        "%.1f GB) [Dialog shown=%d]",
        callCount, percent, usedGB, totalGB, IsShown());
  }

  // Update the alert dialog if it's shown
  monitor->UpdateAlertIfShown(usedGB, totalGB, percent);

  // Determine colors
  double threshold = m_spinThreshold->GetValue();
  wxColour gaugeColor;
  wxColour textColor;

  if (percent >= threshold) {
    gaugeColor = *wxRED;
    textColor = *wxRED;
  } else if (percent >= 70.0) {
    gaugeColor = wxColour(255, 165, 0);  // Orange
    textColor = wxColour(255, 140, 0);   // Darker orange
  } else {
    gaugeColor = wxColour(0, 200, 0);  // Bright green
    textColor = wxColour(0, 128, 0);   // Dark green
  }

  // Update gauge
  m_gaugeMemoryUsage->SetForegroundColour(gaugeColor);
  m_gaugeMemoryUsage->SetValue(static_cast<int>(percent));
  m_gaugeMemoryUsage->Refresh();
  m_gaugeMemoryUsage->Update();

  // Try to create text label if it doesn't exist
  if (!m_staticTextMemoryStats) {
    if (callCount <= 5) {
      wxLogMessage(
          "SettingsDialog::UpdateMemoryGauge() - Call #%d: Text label is NULL, "
          "attempting to create...",
          callCount);
    }

    // Try to find the parent with a sizer
    wxWindow* parent_of_gauge = m_gaugeMemoryUsage->GetParent();
    if (!parent_of_gauge) {
      wxLogError("    - Gauge has no parent!");
      return;
    }

    wxSizer* sizer = parent_of_gauge->GetSizer();

    // If parent doesn't have sizer, try grandparent
    if (!sizer) {
      wxLogMessage("    - Parent has no sizer, checking grandparent...");
      wxWindow* grandparent = parent_of_gauge->GetParent();
      if (grandparent) {
        sizer = grandparent->GetSizer();
        if (sizer) {
          wxLogMessage("    - Using grandparent's sizer");
          parent_of_gauge = grandparent;
        }
      }
    }

    if (sizer) {
      // FIX: We need to find which sub-sizer actually contains the gauge
      // and insert the text label into THAT sub-sizer, not the parent

      bool inserted = false;
      wxSizerItemList& children = sizer->GetChildren();

      // Search through all sizer items to find the gauge
      for (wxSizerItemList::iterator it = children.begin();
           it != children.end(); ++it) {
        wxSizerItem* item = *it;
        if (!item) continue;

        // Check if this item is a SIZER (not a window)
        wxSizer* subSizer = item->GetSizer();
        if (subSizer) {
          // Search this sub-sizer for the gauge
          wxSizerItemList& subChildren = subSizer->GetChildren();
          size_t subIndex = 0;

          for (wxSizerItemList::iterator subIt = subChildren.begin();
               subIt != subChildren.end(); ++subIt, ++subIndex) {
            wxSizerItem* subItem = *subIt;
            if (subItem && subItem->GetWindow() == m_gaugeMemoryUsage) {
              // FOUND IT! The gauge is in THIS sub-sizer at subIndex
              wxLogMessage("    - Found gauge in sub-sizer at index %zu",
                           subIndex);

              // Get the parent window that owns this sub-sizer
              // This should be the groupBox, not the panel
              wxWindow* subSizerParent = nullptr;

              // Find the parent by checking which window this sub-sizer belongs
              // to
              for (wxSizerItemList::iterator parentIt = children.begin();
                   parentIt != children.end(); ++parentIt) {
                wxSizerItem* parentItem = *parentIt;
                if (parentItem && parentItem->GetSizer() == subSizer) {
                  // This item contains our sub-sizer
                  // The window we want is the one this sizer is managing
                  break;
                }
              }

              // If we can't find the exact parent, use the gauge's parent
              if (!subSizerParent) {
                subSizerParent = m_gaugeMemoryUsage->GetParent();
              }

              // Create the text label as a child of the SAME parent as the
              // gauge
              m_staticTextMemoryStats = new wxStaticText(
                  subSizerParent, wxID_ANY, wxEmptyString, wxDefaultPosition,
                  wxDefaultSize, wxALIGN_LEFT);

              // Use FromDIP for proper DPI scaling
              m_staticTextMemoryStats->SetMinSize(FromDIP(wxSize(300, 30)));

              // Set font
              wxFont font = subSizerParent->GetFont();
              m_staticTextMemoryStats->SetFont(font);

              // Insert BEFORE the gauge in the sub-sizer
              subSizer->Insert(subIndex, m_staticTextMemoryStats, 0,
                               wxALL | wxEXPAND, FromDIP(8));
              inserted = true;

              wxLogMessage(
                  "    - Inserted text label BEFORE gauge at sub-sizer index "
                  "%zu "
                  "(parent='%s')",
                  subIndex, subSizerParent->GetName());

              // Update layouts
              subSizer->Layout();
              subSizerParent->Layout();
              if (subSizerParent->GetParent()) {
                subSizerParent->GetParent()->Layout();
              }

              break;
            }
          }
          if (inserted) break;
        }
      }

      if (!inserted) {
        wxLogError("    - Could not find gauge in any sub-sizer!");
      } else {
        Fit();
        Layout();
        Refresh();
        wxLogMessage("    - Text label created successfully!");
      }
    } else {
      if (callCount <= 2) {
        wxLogError("    - No sizer found in parent or grandparent!");
      }
    }
  }  // Closes the if (!m_staticTextMemoryStats) block

  // Update text label if it exists
  if (m_staticTextMemoryStats) {
    // Format: percentage first, then GB values (match log format)
    wxString stats = wxString::Format("%.1f%% (%.2f GB / %.1f GB)", percent,
                                      usedGB, totalGB);

    m_staticTextMemoryStats->SetLabel(stats);

    // Force visibility settings
    m_staticTextMemoryStats->SetBackgroundColour(*wxWHITE);
    m_staticTextMemoryStats->SetForegroundColour(*wxBLACK);

    wxFont currentFont = m_staticTextMemoryStats->GetFont();
    currentFont.MakeBold();
    currentFont.SetPointSize(12);  // Large font
    m_staticTextMemoryStats->SetFont(currentFont);

    m_staticTextMemoryStats->Raise();
    m_staticTextMemoryStats->Show(true);
    m_staticTextMemoryStats->Enable(true);

    // Get absolute screen position
    wxPoint screenPos = m_staticTextMemoryStats->GetScreenPosition();
    wxRect rect = m_staticTextMemoryStats->GetRect();
    wxSize clientSize = m_staticTextMemoryStats->GetClientSize();

    m_staticTextMemoryStats->Refresh();
    m_staticTextMemoryStats->Update();

    wxWindow* parent = m_staticTextMemoryStats->GetParent();
    if (parent) {
      parent->Refresh();
      parent->Update();

      wxWindow* grandparent = parent->GetParent();
      if (grandparent) {
        grandparent->Layout();
        grandparent->Refresh();
      }
    }

    if (callCount <= 3) {
      wxLogMessage(
          "SettingsDialog::UpdateMemoryGauge() - Call #%d: Text='%s'\n"
          "  IsShown=%d, IsShownOnScreen=%d, IsEnabled=%d\n"
          "  Size=%dx%d, ClientSize=%dx%d\n"
          "  LocalPos=(%d,%d), ScreenPos=(%d,%d)\n"
          "  Parent='%s', Grandparent='%s'\n"
          "  FG=RGB(%d,%d,%d), BG=RGB(%d,%d,%d)",
          callCount, stats, m_staticTextMemoryStats->IsShown(),
          m_staticTextMemoryStats->IsShownOnScreen(),
          m_staticTextMemoryStats->IsEnabled(),
          m_staticTextMemoryStats->GetSize().GetWidth(),
          m_staticTextMemoryStats->GetSize().GetHeight(), clientSize.GetWidth(),
          clientSize.GetHeight(), rect.x, rect.y, screenPos.x, screenPos.y,
          parent ? parent->GetName() : "NULL",
          (parent && parent->GetParent()) ? parent->GetParent()->GetName()
                                          : "NULL",
          m_staticTextMemoryStats->GetForegroundColour().Red(),
          m_staticTextMemoryStats->GetForegroundColour().Green(),
          m_staticTextMemoryStats->GetForegroundColour().Blue(),
          m_staticTextMemoryStats->GetBackgroundColour().Red(),
          m_staticTextMemoryStats->GetBackgroundColour().Green(),
          m_staticTextMemoryStats->GetBackgroundColour().Blue());
    }

    if (!firstSuccessfulRun) {
      firstSuccessfulRun = true;
      wxLogMessage(
          "SettingsDialog::UpdateMemoryGauge() - First successful update "
          "complete");
    }
  }
}
#endif

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
