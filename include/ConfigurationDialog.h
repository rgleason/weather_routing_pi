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

#ifndef _WEATHER_ROUTING_CONFIGURATION_DIALOG_H_
#define _WEATHER_ROUTING_CONFIGURATION_DIALOG_H_

#include <wx/treectrl.h>
#include <wx/fileconf.h>

#include "WeatherRoutingUI.h"
#include "RouteMapOverlay.h"

#include <vector>


class WeatherRouting;
class weather_routing_pi;

/**
 * Implements business logic for the weather routing configuration dialog.
 *
 * This class extends ConfigurationDialogBase with implementation of event
 * handlers and business logic for manipulating routing configurations. It
 * manages the data binding between UI controls and the RouteMapConfiguration
 * data model.
 *
 * Key functionality includes:
 * - Loading configuration values into UI controls
 * - Saving control values back to configuration objects
 * - Handling UI events (button clicks, value changes)
 * - Supporting editing of multiple configurations simultaneously
 * - Managing color-coding for multi-configuration editing
 *
 * The class tracks which controls have been edited using the m_edited_controls
 * list and only applies changes from those controls when updating
 * configurations.
 */
class ConfigurationDialog : public ConfigurationDialogBase {
public:
  ConfigurationDialog(WeatherRouting& weatherrouting);
  ~ConfigurationDialog();

  void InitUI();

  void EditBoat();


  /**
   * Updates the configuration dialog with settings from given route
   * configurations
   *
   * @param configurations List of route map configurations to display/edit in
   * dialog
   *
   * This method:
   * - Updates all configuration controls to reflect the given route settings
   * - Handles multiple selected configurations by showing common values
   * - Called when route selection changes in weather routes list
   * - Updates start/end positions, boat settings, weather parameters etc.
   *
   * Note: If multiple configurations are passed in, only settings that are the
   * same across all configurations will be displayed. Others will show default
   * values.
   */
  void SetConfigurations(std::list<RouteMapConfiguration> configuration);

  void Update();
  void AddSource(wxString name);
  void RemoveSource(wxString name);
  void ClearSources();
  void SetBoatFilename(wxString path);

  wxDateTime m_GribTimelineTime;

  // --------------------------------------------------------------------
  // Backward-compatible single-configuration API expected by WeatherRouting.cpp
  // --------------------------------------------------------------------
  void SetConfiguration(const RouteMapConfiguration& cfg) {
    std::list<RouteMapConfiguration> lst;
    lst.push_back(cfg);
    SetConfigurations(lst);
  }

  RouteMapConfiguration GetConfiguration() const {
    // Minimal safe implementation:
    // WeatherRouting.cpp overwrites most fields anyway.
    RouteMapConfiguration cfg;
    return cfg;
  }

protected:
  void OnValueChange(wxEvent& event) {
    m_edited_controls.push_back(event.GetEventObject());
  }
  void OnUpdate(wxCommandEvent& event) {
    OnValueChange(event);
    Update();
  }
  void OnResetAdvanced(wxCommandEvent& event);
  void OnUpdateDate(wxDateEvent& event) {
    OnValueChange(event);
    Update();
  }
  void OnUpdateTime(wxDateEvent& event) {
    OnValueChange(event);
    Update();
  }
  void OnUseCurrentTime(wxCommandEvent& event);
  void OnGribTime(wxCommandEvent& event);
  void OnCurrentTime(wxCommandEvent& event);
  void OnUpdateSpin(wxSpinEvent& event) {
    OnValueChange(event);
    Update();
  }
  void OnBoatFilename(wxCommandEvent& event);
  void OnEditBoat(wxCommandEvent& event) { EditBoat(); }
  void OnUpdateIntegratorNewton(wxCommandEvent& event);
  void OnUpdateIntegratorRungeKutta(wxCommandEvent& event);

  void EnableSpin(wxMouseEvent& event) {
    wxDynamicCast(event.GetEventObject(), wxSpinCtrl)->Enable();
    event.Skip();
  }
  void EnableSpinDouble(wxMouseEvent& event) {
    wxDynamicCast(event.GetEventObject(), wxSpinCtrlDouble)->Enable();
    event.Skip();
  }
  void OnStartFromBoat(wxCommandEvent& event);
  void OnStartFromPosition(wxCommandEvent& event);
  void OnAvoidCyclones(wxCommandEvent& event);
  void OnUseMotor(wxCommandEvent& event);
  void OnAddDegreeStep(wxCommandEvent& event);
  void OnRemoveDegreeStep(wxCommandEvent& event);
  void OnClearDegreeSteps(wxCommandEvent& event);
  void OnGenerateDegreeSteps(wxCommandEvent& event);
  void OnClose(wxCommandEvent& event) { Hide(); }

private:
  void UpdateCycloneControls();

  void SetStartDateTime(wxDateTime datetime);

  WeatherRouting& m_WeatherRouting;
  bool m_bBlockUpdate;

  std::vector<wxObject*> m_edited_controls;
};

#endif
