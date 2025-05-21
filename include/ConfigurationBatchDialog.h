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

#ifndef _WEATHER_ROUTING_BATCH_DIALOG_H_
#define _WEATHER_ROUTING_BATCH_DIALOG_H_

#include <wx/fileconf.h>

#include "WeatherRoutingUI.h"

struct RouteMapConfiguration;
class WeatherRouting;
class weather_routing_pi;
class piDC;

struct BatchDestination {
  BatchDestination(wxString n) : Name(n) {}
  wxString Name;
};

struct BatchSource : public BatchDestination {
  BatchSource(wxString n) : BatchDestination(n) {}
  std::list<BatchDestination*> destinations;
};

class ConfigurationBatchDialog : public ConfigurationBatchDialogBase {
public:
  ConfigurationBatchDialog(WeatherRouting* parent);
  ~ConfigurationBatchDialog() { ClearSources(); }

  void Render(piDC& dc, PlugIn_ViewPort& vp);
  void AddSource(wxString name);
  void RemoveSource(wxString name);
  void ClearSources();

  void Reset();

  std::vector<BatchSource*> sources;

protected:
  void OnOnce(wxCommandEvent& event);
  void OnDaily(wxCommandEvent& event);
  void OnWeekly(wxCommandEvent& event);
  void OnMonthly(wxCommandEvent& event);
  void OnSources(wxCommandEvent& event);
  void OnDestinations(wxCommandEvent& event);
  void OnRemoveSource(wxCommandEvent& event);
  void OnClearSources(wxCommandEvent& event);
  void OnConnect(wxCommandEvent& event);
  void OnDisconnectAll(wxCommandEvent& event);
  void OnAddBoat(wxCommandEvent& event);
  void OnRemoveBoat(wxCommandEvent& event);
  void On100(wxCommandEvent& event);
  void On80to120(wxCommandEvent& event);
  /**
   * Opens a batch configuration file.
   *
   * Displays a file open dialog allowing the user to select a batch
   * configuration file, then loads the selected file. This updates the batch
   * dialog with the source and destination positions, boat configurations, and
   * other batch processing parameters.
   *
   * @param event The command event (unused)
   */
  void OnOpen(wxCommandEvent& event);
  /**
   * Saves the current batch configuration to a file.
   *
   * Displays a file save dialog allowing the user to specify where to save the
   * current batch configuration settings, then writes the configuration to the
   * selected file.
   *
   * @param event The command event (unused)
   */
  void OnSave(wxCommandEvent& event);
  void OnReset(wxCommandEvent& event);
  void OnInformation(wxCommandEvent& event);
  /**
   * Closes the batch configuration dialog and applies changes.
   *
   * Hides the batch configuration dialog and apply the changes. This
   * method is called when the user clicks the OK button.
   *
   * @param event The command event (unused)
   */
  void OnClose(wxCommandEvent& event);
  void OnGenerate(wxCommandEvent& event);

  wxString m_boatFileName;

  WeatherRouting& m_WeatherRouting;
};

#endif
