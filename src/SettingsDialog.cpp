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

#include "SettingsDialog.h"
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
      m_staticTextMemoryStats(nullptr),
      m_usageSizer(nullptr)
#endif
#else
    : SettingsDialogBase(
          parent, wxID_ANY, _("Weather Routing Settings"), wxDefaultPosition,
          wxDefaultSize,
          wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER | wxSTAY_ON_TOP)
#ifdef __WXMSW__
      ,
      m_staticTextMemoryStats(nullptr),
      m_usageSizer(nullptr)
#endif
#endif
{
#ifdef __WXMSW__

  wxLogMessage("SettingsDialog::Constructor - Windows-specific code starting");

  // SET GAUGE RANGE TO 100
  m_gaugeMemoryUsage->SetRange(100);
  wxLogMessage("SettingsDialog::Constructor - Gauge range set to 100");

  // FIX: Reorganize the AddressSpaceMonitor groupbox layout dynamically
  wxWindow* gaugeParent = m_gaugeMemoryUsage->GetParent();
  wxLogMessage("SettingsDialog::Constructor - gaugeParent = %p", gaugeParent);

  if (gaugeParent) {
    wxSizer* groupSizer = gaugeParent->GetSizer();
    wxLogMessage("SettingsDialog::Constructor - groupSizer = %p (BEFORE fix)",
                 groupSizer);

    // FIX: If groupSizer is NULL, try to get it from the parent's containing
    // sizer
    if (!groupSizer && gaugeParent->GetParent()) {
      wxWindow* grandParent = gaugeParent->GetParent();
      wxSizer* grandParentSizer = grandParent->GetSizer();

      if (grandParentSizer) {
        wxLogMessage(
            "SettingsDialog::Constructor - Searching grandParent sizer for "
            "wxStaticBox sizer");

        // Look for a wxStaticBoxSizer that contains our wxStaticBox
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

      // PART 1: Find and reorganize threshold label + spin control
      wxStaticText* thresholdLabel = nullptr;
      wxSizerItemList& children = groupSizer->GetChildren();

      // Find the label that's immediately before the spin control
      for (wxSizerItemList::iterator it = children.begin();
           it != children.end(); ++it) {
        wxSizerItem* item = *it;
        if (item && item->GetWindow() == m_spinThreshold) {
          if (it != children.begin()) {
            wxSizerItemList::iterator prevIt = it;
            --prevIt;
            wxSizerItem* prevItem = *prevIt;
            if (prevItem && prevItem->GetWindow()) {
              thresholdLabel =
                  dynamic_cast<wxStaticText*>(prevItem->GetWindow());
            }
          }
          break;
        }
      }

      if (thresholdLabel && m_spinThreshold) {
        wxLogMessage("    - Found threshold label: '%s'",
                     thresholdLabel->GetLabel());

        // Create horizontal sizer for threshold label + spin
        wxBoxSizer* thresholdSizer = new wxBoxSizer(wxHORIZONTAL);

        // Remove from parent sizer
        groupSizer->Detach(thresholdLabel);
        groupSizer->Detach(m_spinThreshold);

        thresholdSizer->Add(thresholdLabel, 0,
                            wxALIGN_CENTER_VERTICAL | wxRIGHT, FromDIP(5));
        thresholdSizer->Add(m_spinThreshold, 0, wxALIGN_CENTER_VERTICAL);
        thresholdSizer->AddStretchSpacer(1);

        // Find where to insert (look for first checkbox)
        size_t insertPos = 0;
        children = groupSizer->GetChildren();
        for (wxSizerItemList::iterator it = children.begin();
             it != children.end(); ++it, ++insertPos) {
          wxWindow* win = (*it)->GetWindow();
          if (win == m_checkSuppressAlert) {
            break;
          }
        }

        // Insert the horizontal sizer
        groupSizer->Insert(insertPos, thresholdSizer, 0, wxEXPAND | wxALL,
                           FromDIP(5));
        wxLogMessage("    - Created horizontal threshold sizer at position %zu",
                     insertPos);
      }

      // PART 2: Find and reorganize "Usage:" label
      wxStaticText* usageLabel = nullptr;
      children = groupSizer->GetChildren();

      wxLogMessage(
          "    - PART 2: Searching for usage label... (children count: %zu)",
          children.size());

      // Search for usage label (may be in sub-sizer)
      for (wxSizerItemList::iterator it = children.begin();
           it != children.end(); ++it) {
        wxSizerItem* item = *it;
        if (!item) continue;

        wxWindow* win = item->GetWindow();
        wxSizer* sizer = item->GetSizer();

        if (win) {
          wxStaticText* txt = dynamic_cast<wxStaticText*>(win);
          if (txt) {
            wxString label = txt->GetLabel().Lower();
            if (label.Contains("usage") || label.Contains("live") ||
                label.Contains("address") || label.Contains("space")) {
              usageLabel = txt;
              break;
            }
          }
        } else if (sizer) {
          // Check sub-sizer children
          wxSizerItemList& subChildren = sizer->GetChildren();
          for (wxSizerItemList::iterator subIt = subChildren.begin();
               subIt != subChildren.end(); ++subIt) {
            wxWindow* subWin = (*subIt)->GetWindow();
            if (subWin) {
              wxStaticText* subTxt = dynamic_cast<wxStaticText*>(subWin);
              if (subTxt) {
                wxString label = subTxt->GetLabel().Lower();
                if (label.Contains("usage") || label.Contains("live") ||
                    label.Contains("address") || label.Contains("space")) {
                  usageLabel = subTxt;
                  break;
                }
              }
            }
          }
          if (usageLabel) break;
        }
      }

      wxLogMessage("    - Search complete. usageLabel = %p", usageLabel);

      if (usageLabel) {
        wxLogMessage(
            "    - Found usage label: '%s', detaching from parent sizer",
            usageLabel->GetLabel());

        // Find the actual parent sizer
        wxSizer* labelParentSizer = nullptr;
        children = groupSizer->GetChildren();

        for (wxSizerItemList::iterator it = children.begin();
             it != children.end(); ++it) {
          wxWindow* win = (*it)->GetWindow();
          if (win == usageLabel) {
            labelParentSizer = groupSizer;
            break;
          }

          wxSizer* subSizer = (*it)->GetSizer();
          if (subSizer) {
            wxSizerItemList& subChildren = subSizer->GetChildren();
            for (wxSizerItemList::iterator subIt = subChildren.begin();
                 subIt != subChildren.end(); ++subIt) {
              wxWindow* subWin = (*subIt)->GetWindow();
              if (subWin == usageLabel) {
                labelParentSizer = subSizer;
                break;
              }
            }
            if (labelParentSizer) break;
          }
        }

        if (!labelParentSizer) {
          wxLogError("    - Could not find parent sizer for usage label!");
        } else {
          // Change the label text
          usageLabel->SetLabel("Usage:");

          // Ensure label uses normal font weight
          wxFont labelFont = usageLabel->GetFont();
          labelFont.SetWeight(wxFONTWEIGHT_NORMAL);
          usageLabel->SetFont(labelFont);

          // Create horizontal sizer for usage label + dynamic text
          m_usageSizer = new wxBoxSizer(wxHORIZONTAL);
          wxLogMessage("    - Created m_usageSizer: %p", m_usageSizer);

          // Remove label from its parent sizer
          bool detached = labelParentSizer->Detach(usageLabel);
          wxLogMessage("    - Detached label from parent sizer: %s",
                       detached ? "SUCCESS" : "FAILED");

          // Add label to new horizontal sizer
          m_usageSizer->Add(usageLabel, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT,
                            FromDIP(5));

          // Find where to insert (before gauge)
          size_t insertPos = 0;
          children = groupSizer->GetChildren();
          for (wxSizerItemList::iterator it = children.begin();
               it != children.end(); ++it, ++insertPos) {
            wxWindow* win = (*it)->GetWindow();
            if (win == m_gaugeMemoryUsage) {
              break;
            }
          }

          // Insert the horizontal sizer BEFORE the gauge
          groupSizer->Insert(insertPos, m_usageSizer, 0, wxEXPAND | wxALL,
                             FromDIP(5));
          wxLogMessage(
              "    - Inserted m_usageSizer at position %zu (m_usageSizer=%p)",
              insertPos, m_usageSizer);
        }
      } else {
        wxLogError(
            "    - FAILED to find usage label! m_usageSizer will be NULL!");
      }

      groupSizer->Layout();
      gaugeParent->Layout();
      wxLogMessage(
          "SettingsDialog::Constructor - Dynamic layout complete "
          "(m_usageSizer=%p)",
          m_usageSizer);
    }
  }

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

  // Clear the gauge and text label references in the monitor
  AddressSpaceMonitor* monitor = GetMonitor();
  if (monitor && monitor->IsValid()) {
    monitor->SetGauge(nullptr);
    monitor->SetTextLabel(nullptr);
    wxLogMessage("SettingsDialog: Destructor - Cleared monitor UI references");
  }

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

    // Create text label if it doesn't exist yet
    if (!m_staticTextMemoryStats && m_usageSizer) {
      wxWindow* parent = m_gaugeMemoryUsage->GetParent();
      m_staticTextMemoryStats = new wxStaticText(
          parent, wxID_ANY, "", wxDefaultPosition, wxDefaultSize, wxALIGN_LEFT);
      m_staticTextMemoryStats->SetMinSize(FromDIP(wxSize(300, 30)));
      m_usageSizer->Add(m_staticTextMemoryStats, 0, wxALIGN_BOTTOM | wxLEFT,
                        FromDIP(5));
      m_usageSizer->Layout();
      wxLogMessage("SettingsDialog: Created text label for monitor");
    }

    monitor->SetTextLabel(m_staticTextMemoryStats);

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
    monitor->DismissAlert();
    wxLogMessage("SettingsDialog: User suppressed alerts via checkbox");
  } else {
    monitor->alertDismissed = false;
    wxLogMessage("SettingsDialog: User re-enabled alerts via checkbox");
    monitor->CheckAndAlert();
  }

  SaveMemorySettings();
}

void SettingsDialog::OnLogUsageChanged(wxCommandEvent& event) {
  AddressSpaceMonitor* monitor = GetMonitor();
  if (monitor) {
    monitor->SetLoggingEnabled(m_checkLogUsage->GetValue());
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
