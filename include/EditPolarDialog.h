/***************************************************************************
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
 **************************************************************************/

#ifndef _WEATHER_ROUTING_EDITPOLAR_DIALOG_H_
#define _WEATHER_ROUTING_EDITPOLAR_DIALOG_H_

#include "WeatherRoutingUI.h"
#include <array>
#include <vector>

class BoatDialog;

class Polar;
class EditPolarDialog : public EditPolarDialogBase {
public:
  EditPolarDialog(wxWindow* parent);

  void SetPolarIndex(int i);
#ifdef __OCPN__ANDROID__
  bool Saved() const { return m_androidSaved; }
#endif

private:
  void OnPolarGridChanged(wxGridEvent& event);
  void OnAddTrueWindAngle(wxCommandEvent& event);
  void OnRemoveTrueWindAngle(wxCommandEvent& event);
  void OnAddTrueWindSpeed(wxCommandEvent& event);
  void OnRemoveTrueWindSpeed(wxCommandEvent& event);
  void OnAddMeasurement(wxCommandEvent& event);
  void OnRemoveMeasurement(wxCommandEvent& event);
  void OnRemoveAllMeasurements(wxCommandEvent& event);
  void OnGeneratePolar(wxCommandEvent& event);
  void OnSave(wxCommandEvent& event) {
#ifdef __OCPN__ANDROID__
    if (!ValidateAndroidPolar()) return;
    m_androidSaved = true;
#endif
    EndModal(wxID_SAVE);
  }

  void RebuildTrueWindAngles();
  void RebuildTrueWindSpeeds();
  void RebuildGrid();
  Polar* GetPolar();

#ifdef __OCPN__ANDROID__
  bool ValidateAndroidSpeeds();
  bool ValidateAndroidPolar();
  void RefreshAndroidSpeeds();
  void RefreshAndroidMeasurements();
  void ShowAndroidMeasurement();
  bool m_androidSaved{false};
  int m_androidDisplayedAngle{-1};
  wxChoice* m_androidAngle{nullptr};
  wxChoice* m_androidDimensionAngle{nullptr};
  wxChoice* m_androidDimensionWind{nullptr};
  wxScrolledWindow* m_androidSpeeds{nullptr};
  wxBoxSizer* m_androidSpeedFields{nullptr};
  wxChoice* m_androidMeasurementPicker{nullptr};
  wxStaticText* m_androidMeasurementDetails{nullptr};
  std::vector<std::array<double, 3>> m_androidMeasurements;
#endif
  int polarindex{-1};
  BoatDialog* m_BoatDialog;
};

#endif
