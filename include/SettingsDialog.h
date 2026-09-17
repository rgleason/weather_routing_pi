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

#ifndef _WEATHER_ROUTING_SETTINGS_H_
#define _WEATHER_ROUTING_SETTINGS_H_

#include <wx/treectrl.h>
#include <wx/fileconf.h>

#include "WeatherRoutingUI.h"
#include "TimeZoneDisplay.h"

#ifdef __WXMSW__
class AddressSpaceMonitor;
#endif

class SettingsDialog : public SettingsDialogBase {
public:
  SettingsDialog(wxWindow* parent);
  ~SettingsDialog();

  void LoadSettings();
  void SaveSettings();

  void OnUpdateColor(wxColourPickerEvent& event) { OnUpdate(); }
  void OnUpdateSpin(wxSpinEvent& event) { OnUpdate(); }
  void OnUpdate(wxCommandEvent& event) { OnUpdate(); }
  void OnUpdate();
  void OnUpdateColumns(wxCommandEvent& event);
  void OnHelp(wxCommandEvent& event);
  bool UseLocalTimeZone() const { return m_useLocalTimeZone; }
  const wxString& DisplayTimeZone() const { return m_displayTimeZone; }
  void SetDisplayTimeZone(bool enabled, const wxString& zoneName);
  wxString FormatTime(const wxDateTime& utc, const wxString& format,
                      bool appendAbbreviation = true) const;
  wxDateTime ToDisplayWallClock(const wxDateTime& utc) const;
  marine_time::WallClockConversion DisplayWallClockToUtc(
      int year, int month, int day, int hour, int minute, int second) const;
  static const wxString column_names[];

#ifdef __WXMSW__
  void OnThresholdChanged(wxSpinDoubleEvent& event);
  void OnSuppressAlertChanged(wxCommandEvent& event);
  void OnLogUsageChanged(wxCommandEvent& event);
  void LoadMemorySettings();
  void SaveMemorySettings();
  AddressSpaceMonitor* GetMonitor();
#endif

private:
  bool m_useLocalTimeZone = false;
  wxString m_displayTimeZone = "UTC";
#ifdef __WXMSW__
  wxStaticText* m_staticTextMemoryStats = nullptr;
  wxBoxSizer* m_usageSizer = nullptr;
#endif
};

#endif
