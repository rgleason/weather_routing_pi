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
#include <wx/timer.h>

#include "WeatherRoutingUI.h"

// Forward declaration for Windows-only class
#ifdef __WXMSW__
class AddressSpaceMonitor;
#endif

/**
 * @brief Settings dialog for Weather Routing plugin configuration.
 *
 * @details Provides UI for configuring routing parameters, display options,
 * and on Windows, real-time address space monitoring with visual gauge and
 * alerts.
 *
 * The dialog dynamically reorganizes the memory monitor UI layout and manages
 * a 2-second timer for updating memory statistics while visible.
 */
class SettingsDialog : public SettingsDialogBase {
public:
  /**
   * @brief Constructs the Settings dialog.
   * @param parent Parent window for the dialog.
   */
  SettingsDialog(wxWindow* parent);

  /**
   * @brief Destructor - ensures proper cleanup of timers and event handlers.
   *
   * @details Stops the memory update timer, disconnects all event handlers,
   * and clears references to prevent dangling pointers.
   */
  ~SettingsDialog();

  void LoadSettings();  ///< Loads all plugin settings from wxFileConfig
  void SaveSettings();  ///< Saves all plugin settings to wxFileConfig

  // ========== Display Settings Event Handlers ==========
  void OnUpdateColor(wxColourPickerEvent& event) {
    OnUpdate();
  }  ///< Color picker changed
  void OnUpdateSpin(wxSpinEvent& event) { OnUpdate(); }  ///< Spin control changed
  void OnUpdate(wxCommandEvent& event) { OnUpdate(); }  ///< Generic UI update
  void OnUpdate();  ///< Applies display changes to chart
  void OnUpdateColumns(wxCommandEvent& event);  ///< Column visibility changed
  void OnHelp(wxCommandEvent& event);           ///< Show help dialog

  static const wxString column_names[];

private:
#ifdef __WXMSW__
  // ========== Windows-Only: Address Space Monitoring ==========

   /**
   * @name Memory Monitor UI Components
   * @{
   */
  wxTimer m_memoryUpdateTimer;  ///< 2-second timer for gauge updates
  wxStaticText*
      m_staticTextMemoryStats;  ///< Dynamic text: "XX% (X.XX GB / X.X GB)"
  wxBoxSizer*
      m_usageSizer;  ///< Horizontal sizer for "Usage:" label + stats text
  /** @} */

  /**
   * @name Memory Monitor Event Handlers
   * @{
   */
  void OnMemoryUpdateTimer(wxTimerEvent& event);  ///< Called every 2 seconds
  void OnThresholdChanged(
      wxSpinDoubleEvent& event);  ///< User changed alert threshold
  void OnSuppressAlertChanged(
      wxCommandEvent& event);  ///< User toggled alert suppression
  void OnLogUsageChanged(
      wxCommandEvent& event);  ///< User toggled usage logging
  /** @} */

  /**
   * @name Memory Monitor Helper Methods
   * @{
   */
  void UpdateMemoryGauge();   ///< Updates gauge and text label with current
                              ///< memory usage
  void LoadMemorySettings();  ///< Loads threshold, alert, and logging
                              ///< preferences from config
  void SaveMemorySettings();  ///< Saves current settings to config file
  AddressSpaceMonitor*  GetMonitor();  ///< Retrieves monitor instance from plugin
  /** @} */
#endif
};

#endif

