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
 ***************************************************************************/

#include <wx/wx.h>
#include <wx/stdpaths.h>

#include <stdlib.h>
#include <math.h>
#include <cmath>  // For std::isnan
#include <limits>
#include <time.h>

#include "Utilities.h"
#include "Boat.h"
#include "RouteMapOverlay.h"
#include "WeatherRouting.h"
#include "AndroidDialogHeader.h"
#include "wx28compat.h"

//---------------------------------------------------------------------------------------
//          Weather Routing Dialog Implementation
//---------------------------------------------------------------------------------------

PlotDialog::PlotDialog(WeatherRouting& weatherrouting)
#ifndef __WXOSX__
    : PlotDialogBase(&weatherrouting),
#else
    : PlotDialogBase(&weatherrouting, wxID_ANY, _("Weather Route Plot"),
                     wxDefaultPosition, wxDefaultSize,
                     wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER | wxSTAY_ON_TOP),
#endif
      m_WeatherRouting(weatherrouting) {
  // wxQt does not reliably honour the initial slider position at creation.
  m_sScale->SetRange(1, 100);
  m_sScale->SetValue(100);
  m_rbCurrentRoute->SetValue(true);
#ifdef __OCPN__ANDROID__
  wxSizer* original = GetSizer();
  original->Detach(m_PlotWindow);
  auto* root = new wxBoxSizer(wxVERTICAL);
  root->Add(m_PlotWindow, 1, wxEXPAND | wxALL, 8);
  auto* controls = new wxScrolledWindow(this, wxID_ANY);
  controls->SetScrollRate(0, 24);
  controls->SetMinSize(wxSize(0, 450));
  WR_EnableAndroidScrolling(controls);
  auto* controlPanel = new wxPanel(controls, wxID_ANY);
  auto* fields = new wxFlexGridSizer(0, 1, 8, 8);
  fields->AddGrowableCol(0);
  auto move = [controlPanel](wxWindow* control) {
    if (auto* sizer = control->GetContainingSizer()) sizer->Detach(control);
    control->Reparent(controlPanel);
    control->SetMinSize(wxSize(0, 62));
    wxFont font = control->GetFont();
    font.SetPointSize(16);
    control->SetFont(font);
  };
  for (auto* control : std::vector<wxWindow*>{m_staticText138, m_sPosition,
         m_staticText139, m_sScale, m_cVariable1, m_cVariable2, m_cVariable3,
         m_stMousePosition1, m_stMousePosition2, m_stMousePosition3,
         m_rbCurrentRoute, m_rbCursorRoute}) move(control);
  auto* sliders = new wxFlexGridSizer(0, 2, 8, 8);
  sliders->AddGrowableCol(1);
  m_staticText138->SetMinSize(wxSize(180, 72));
  m_staticText139->SetMinSize(wxSize(180, 72));
  m_staticText138->SetLabel(_("Position"));
  m_staticText139->SetLabel(_("Zoom"));
  for (auto* slider : {m_sPosition, m_sScale}) {
    WR_EnableAndroidSlider(slider, [this]() { m_PlotWindow->Refresh(); });
    slider->GetHandle()->setStyleSheet(
        "QSlider::groove:horizontal { height: 8px; background: #d5e1e7; } "
        "QSlider::handle:horizontal { width: 32px; margin: -14px 0; "
        "background: #215d79; border-radius: 16px; }");
  }
  sliders->Add(m_staticText138, 0, wxALIGN_CENTER_VERTICAL);
  sliders->Add(m_sPosition, 1, wxEXPAND);
  sliders->Add(m_staticText139, 0, wxALIGN_CENTER_VERTICAL);
  sliders->Add(m_sScale, 1, wxEXPAND);
  const std::pair<wxChoice*, wxStaticText*> variables[] = {
      {m_cVariable1, m_stMousePosition1}, {m_cVariable2, m_stMousePosition2},
      {m_cVariable3, m_stMousePosition3}};
  for (const auto& variable : variables) {
    WR_StyleAndroidCombo(variable.first);
    variable.first->GetHandle()->setStyleSheet(
        "QComboBox { font-size: 16pt; min-height: 62px; }");
    variable.second->GetHandle()->setStyleSheet("QLabel { font-size: 16pt; }");
    variable.second->SetMinSize(wxSize(100, 62));
    fields->Add(variable.first, 1, wxEXPAND);
    fields->Add(variable.second, 0, wxEXPAND);
  }
  auto* selection = new wxBoxSizer(wxHORIZONTAL);
  for (auto* radio : {m_rbCurrentRoute, m_rbCursorRoute}) {
    radio->GetHandle()->setStyleSheet(
        "QRadioButton { font-size: 16pt; min-height: 62px; } "
        "QRadioButton::indicator { width: 26px; height: 26px; }");
    selection->Add(radio, 1, wxEXPAND);
  }
  auto* layout = new wxBoxSizer(wxVERTICAL);
  m_androidSampleTime = new wxStaticText(controlPanel, wxID_ANY,
      _("Tap the plot to inspect a route sample."));
  m_androidSampleTime->GetHandle()->setStyleSheet("QLabel { font-size: 16pt; }");
  m_androidSampleTime->SetMinSize(wxSize(0, 62));
  layout->Add(m_androidSampleTime, 0, wxEXPAND | wxALL, 8);
  layout->Add(fields, 0, wxEXPAND | wxALL, 8);
  layout->Add(selection, 0, wxEXPAND | wxALL, 8);
  layout->Add(sliders, 0, wxEXPAND | wxALL, 8);
  controlPanel->SetSizer(layout);
  auto* controlsWidth = new wxBoxSizer(wxHORIZONTAL);
  controlsWidth->Add(controlPanel, 1, wxEXPAND);
  controlsWidth->AddSpacer(42);
  controls->SetSizer(controlsWidth);
  root->Add(controls, 0, wxEXPAND);
  m_sdbSizer4->Detach(m_sdbSizer4OK);
  m_sdbSizer4OK->Destroy();
  SetSizer(root, false);
  delete original;
  WR_AddAndroidDoneHeader(this, _("Route plot"));
  m_rbCursorRoute->Bind(wxEVT_RADIOBUTTON,
      &PlotDialog::OnUpdateRoute, this);
  wxSize sz = ::wxGetDisplaySize();
  SetSize(0, 0, sz.x, sz.y * 88 / 100);
#endif
}

PlotDialog::~PlotDialog() {}

#ifdef __OCPN__ANDROID__
void PlotDialog::ResetAndroidReadout() {
  for (auto* label : {m_stMousePosition1, m_stMousePosition2, m_stMousePosition3})
    label->SetLabel(_("N/A"));
  if (m_androidSampleTime)
    m_androidSampleTime->SetLabel(_("Tap the plot to inspect a route sample."));
}
#endif

void PlotDialog::OnMouseEventsPlot(wxMouseEvent& event) {
  wxStaticText* stMousePosition[3] = {m_stMousePosition1, m_stMousePosition2,
                                      m_stMousePosition3};
  if (event.Leaving()) {
#ifndef __OCPN__ANDROID__
    for (int i = 0; i < 3; i++) stMousePosition[i]->SetLabel(_("N/A"));
#endif
    return;
  }

  int w, h;
  m_PlotWindow->GetSize(&w, &h);
#ifdef __OCPN__ANDROID__
  // Keep all three scales outside the data area and reserve a time-axis row.
  w = wxMax(1, w - 190);
  h = wxMax(1, h - 56);
#endif

  wxPoint p = event.GetPosition();
#ifdef __OCPN__ANDROID__
  p.x -= 190;
  p.y = wxMax(0, p.y - 36);
  if (m_PlotData.empty() || !m_StartTime.IsValid() || w <= 0) {
    ResetAndroidReadout();
    return;
  }
  const double position = m_sPosition->GetValue() / 100.0;
  const double scale = 100.0 / wxMax(1, m_sScale->GetValue());
  const double fraction = wxMax(0.0, wxMin(1.0,
      ((static_cast<double>(p.x) / w - position) / scale) + position));
  const double sampleTime = fraction * (m_maxtime - m_mintime) + m_mintime;
  PlotData* nearest = nullptr;
  double nearestDistance = std::numeric_limits<double>::infinity();
  for (auto& sample : m_PlotData) {
    if (!sample.time.IsValid()) continue;
    const double distance = std::abs(
        (sample.time - m_StartTime).GetSeconds().ToDouble() - sampleTime);
    if (distance < nearestDistance) {
      nearest = &sample;
      nearestDistance = distance;
    }
  }
  if (!nearest) {
    ResetAndroidReadout();
    return;
  }
  m_androidSampleTime->SetLabel(m_WeatherRouting.m_SettingsDialog.FormatTime(
      nearest->time, _("%Y-%m-%d %H:%M")));
  wxChoice* variables[] = {m_cVariable1, m_cVariable2, m_cVariable3};
  for (int i = 0; i < 3; ++i) {
    const double value = GetValue(*nearest,
        GetVariableEnumFromIndex(variables[i]->GetSelection()));
    stMousePosition[i]->SetLabel(std::isfinite(value)
        ? wxString::Format("%.1f", value) : _("Unavailable"));
  }
  return;
#endif

#if 0
    double position = m_sPosition->GetValue() / 100.0;
    double scale = 100.0 / m_sScale->GetValue();

    double time = (((double)p.x/w - position) / scale + position) * (m_maxtime - m_mintime) + m_mintime;
    wxDateTime datetime = m_StartTime + wxTimeSpan(0, 0, time);
#endif

  for (int i = 0; i < 3; i++) {
    double value = (1.0 - (double)p.y / h) * (m_maxvalue[i] - m_minvalue[i]) +
                   m_minvalue[i];
    stMousePosition[i]->SetLabel(std::isfinite(value)
        ? wxString::Format(_T(" %.1f"), value) : _("Unavailable"));
  }
}

double PlotDialog::GetValue(PlotData& data, Variable variable) {
  switch (variable) {
    case SPEED_OVER_GROUND:
      return data.sog;
    case COURSE_OVER_GROUND:
      return positive_degrees(data.cog);

    case SPEED_THROUGH_WATER:
      return data.stw;
    case COURSE_THROUGH_WATER:
      return positive_degrees(data.ctw);

    case TRUE_WIND_SPEED_OVER_WATER:
      // This would be the same as the TWS reading on the boat, if the
      // estimate is accurate.
      // The TWS reading from the vane and anemometer provides the apparent wind
      // speed, which is then corrected for the boat's speed through the water
      // to get the TWS over water.
      return data.twsOverWater;
    case TRUE_WIND_ANGLE_OVER_WATER:
      // Find the angle between the direction the boat is traveling through
      // water (Course Through Water or CTW) and the direction from which the
      // true wind is coming (True Wind Direction over water or TWD-Water).
      return heading_resolve(data.ctw - data.twdOverWater);
    case TRUE_WIND_DIRECTION_OVER_WATER:
      return positive_degrees(data.twdOverWater);

    case TRUE_WIND_SPEED_OVER_GROUND:
      return data.twsOverGround;
    case TRUE_WIND_ANGLE_OVER_GROUND:
      return heading_resolve(data.cog - data.twdOverGround);
    case TRUE_WIND_DIRECTION_OVER_GROUND:
      return positive_degrees(data.twdOverGround);

    case APPARENT_WIND_SPEED_OVER_WATER:
      return Polar::VelocityApparentWind(
          data.stw, GetValue(data, TRUE_WIND_ANGLE_OVER_WATER),
          data.twsOverWater);
    case APPARENT_WIND_ANGLE_OVER_WATER: {
      return Polar::DirectionApparentWind(
          GetValue(data, APPARENT_WIND_SPEED_OVER_WATER), data.stw,
          GetValue(data, TRUE_WIND_ANGLE_OVER_WATER), data.twsOverWater);
      case WIND_GUST:
        return data.VW_GUST;
    }
    case CURRENT_VELOCITY:
      return data.currentSpeed;
    case CURRENT_DIRECTION:
      return positive_degrees(data.currentDir);
    case SIG_WAVE_HEIGHT:
      return data.WVHT;
    case WAVE_DIRECTION:
      return positive_degrees(data.WVDIR);
    case WAVE_RELATIVE_DIRECTION:
      return positive_degrees(data.WVREL);
    case WAVE_PERIOD:
      return data.WVPER;
    case TACKS:
      return data.tacks;
    case JIBES:
      return data.jibes;
    case SAIL_PLAN_CHANGES:
      return data.sail_plan_changes;
    case CLOUD_COVER:
      return data.cloud_cover;
    case RAINFALL:
      return data.rain_mm_per_hour;
    case AIR_TEMPERATURE:
      return data.air_temp;
    case SEA_SURFACE_TEMPERATURE:
      return data.sea_surface_temp;
    case CAPE:
      return data.cape;
    case RELATIVE_HUMIDITY:
      return data.relative_humidity;
    case AIR_PRESSURE:
      return data.air_pressure;
    case REFLECTIVITY:
      return data.reflectivity;
  }
  return NAN;
}

/**
 * Categories of navigation and weather data
 *
 * This enum represents general categories that group the more specific
 * variables from the Variable enum. Used for processing and displaying
 * related types of data.
 */
enum Type {
  /** Vessel speed values (SOG, STW) */
  SPEED,
  /** Vessel course/heading values (COG, CTW) */
  COURSE,
  /** Wind speed values (true, apparent, gusts) */
  WIND_SPEED,
  /** Wind direction values (angles relative to vessel) */
  WIND_DIRECTION,
  /** Water current speed */
  CURRENT_SPEED,
  /** Water current direction */
  CURRENT_DIRECTION,
  /** Significant wave height */
  WAVE_HEIGHT,
  /** Tacking and Jibing maneuvers count */
  TACKS_AND_JIBES,
  /** Absolute wind directions (meteorological) */
  WIND_COURSE,
  /** Environmental data (cloud cover, rainfall, temperature, humidity) */
  ENVIRONMENTAL,
  /** Invalid or undefined type */
  INVALID
};
int PlotDialog::GetType(int var) {
  switch (var) {
    case SPEED_OVER_GROUND:
    case SPEED_THROUGH_WATER:
      return SPEED;
    case COURSE_OVER_GROUND:
    case COURSE_THROUGH_WATER:
      return COURSE;
    case TRUE_WIND_SPEED_OVER_WATER:
    case TRUE_WIND_SPEED_OVER_GROUND:
    case APPARENT_WIND_SPEED_OVER_WATER:
    case WIND_GUST:
      return WIND_SPEED;
    case TRUE_WIND_ANGLE_OVER_WATER:
    case TRUE_WIND_ANGLE_OVER_GROUND:
    case APPARENT_WIND_ANGLE_OVER_WATER:
      return WIND_DIRECTION;
    case TRUE_WIND_DIRECTION_OVER_WATER:
    case TRUE_WIND_DIRECTION_OVER_GROUND:
      return WIND_COURSE;
    case CURRENT_VELOCITY:
      return CURRENT_SPEED;
    case CURRENT_DIRECTION:
      return CURRENT_DIRECTION;
    case SIG_WAVE_HEIGHT:
      return WAVE_HEIGHT;
    case WAVE_DIRECTION:
    case WAVE_RELATIVE_DIRECTION:
      return COURSE;
    case WAVE_PERIOD:
      return ENVIRONMENTAL;
    case TACKS:
    case JIBES:
    case SAIL_PLAN_CHANGES:
      return TACKS_AND_JIBES;
    case CLOUD_COVER:
    case RAINFALL:
    case AIR_TEMPERATURE:
    case SEA_SURFACE_TEMPERATURE:
    case CAPE:
    case RELATIVE_HUMIDITY:
    case AIR_PRESSURE:
    case REFLECTIVITY:
      return ENVIRONMENTAL;
  }
  return INVALID;
}

void PlotDialog::GetScale() {
  wxChoice* cVariable[3] = {m_cVariable1, m_cVariable2, m_cVariable3};
  for (int i = 0; i < 3; i++) {
    bool first = true;
    for (std::list<PlotData>::iterator it = m_PlotData.begin();
         it != m_PlotData.end(); it++) {
      Variable variable =
          GetVariableEnumFromIndex(cVariable[i]->GetSelection());
      double value = GetValue(*it, variable);
      if (first) {
        m_StartTime = (*it).time;
        m_mintime = m_maxtime = 0.;
        m_minvalue[i] = m_maxvalue[i] = value;
        first = false;
      } else {
        // Make sure both datetimes are valid before performing subtraction
        if ((*it).time.IsValid() && m_StartTime.IsValid()) {
          double time = ((*it).time - m_StartTime).GetSeconds().ToDouble();
          m_mintime = MIN(time, m_mintime);
          m_maxtime = MAX(time, m_maxtime);
        } else {
          // Skip this point if either datetime is invalid
          continue;
        }
        m_minvalue[i] = MIN(value, m_minvalue[i]);
        m_maxvalue[i] = MAX(value, m_maxvalue[i]);
      }
    }
  }

  // force same scales for comparable datatypes
  for (int i = 0; i < 2; i++) {
    for (int j = i + 1; j < 3; j++) {
      // Use the same scale if variables are of the same type
      if (GetType(GetVariableEnumFromIndex(cVariable[i]->GetSelection())) ==
          GetType(GetVariableEnumFromIndex(cVariable[j]->GetSelection()))) {
        m_minvalue[i] = m_minvalue[j] = wxMin(m_minvalue[i], m_minvalue[j]);
        m_maxvalue[i] = m_maxvalue[j] = wxMax(m_maxvalue[i], m_maxvalue[j]);
      }
    }
  }
}

static wxString ReadableTime(int seconds) {
#ifdef __OCPN__ANDROID__
  if (seconds >= 86400)
    return wxString::Format(_T("%dd %dh"), seconds / 86400,
                            (seconds / 3600) % 24);
  return wxString::Format(_T("%dh %02dm"), seconds / 3600,
                          (seconds / 60) % 60);
#endif
  if (seconds < 60) return wxString::Format(_T("%02ds"), seconds);

  if (seconds < 3600)
    return wxString::Format(_T("%02d.%02d"), seconds / 60, seconds % 60);

  if (seconds < 86400)
    return wxString::Format(_T("%02d:%02d"), seconds / 3600,
                            (seconds / 60) % 60);

  return wxString::Format(_T("%dd %02dh"), seconds / 86400, seconds / 3600);
}

void PlotDialog::OnPaintPlot(wxPaintEvent& event) {
  wxWindow* window = m_PlotWindow;

  double position = m_sPosition->GetValue() / 100.0;
  double scale = 100.0 / wxMax(1, m_sScale->GetValue());

  wxPaintDC dc(window);
#ifdef __OCPN__ANDROID__
  wxFont axisFont = window->GetFont();
  axisFont.SetPointSize(13);
  dc.SetFont(axisFont);
#endif
  dc.Clear();
#ifdef __OCPN__ANDROID__
  if (m_PlotData.empty()) {
    dc.DrawText(m_rbCursorRoute->GetValue()
        ? _("Choose a point on the chart route to display its cursor plot.")
        : _("Select a computed routing to display a plot."), 20, 20);
    return;
  }
#endif
  dc.SetBackgroundMode(wxTRANSPARENT);

  int w, h;
  m_PlotWindow->GetSize(&w, &h);
#ifdef __OCPN__ANDROID__
  // Keep all three scales outside the data area and reserve a time-axis row.
  dc.SetDeviceOrigin(190, 36);
  w = wxMax(1, w - 190);
  h = wxMax(1, h - 56);
#endif

  dc.SetBrush(*wxTRANSPARENT_BRUSH);

  wxChoice* cVariable[3] = {m_cVariable1, m_cVariable2, m_cVariable3};
  wxColour colors[3] = {wxColour(200, 0, 0), wxColour(0, 200, 0),
                        wxColour(0, 0, 200)};
#ifdef __OCPN__ANDROID__
  dc.SetClippingRegion(0, 0, w, h);
#endif
  for (int i = 0; i < 3; i++) {
    dc.SetPen(wxPen(colors[i], 3));

    int lx = 0, ly = 0;
    bool first = true;
    for (std::list<PlotData>::iterator it = m_PlotData.begin();
         it != m_PlotData.end(); it++) {
      // Validate timestamps before subtraction
      if (!(*it).time.IsValid() || !m_StartTime.IsValid()) {
        continue;  // Skip invalid points
      }

      double time = ((*it).time - m_StartTime).GetSeconds().ToDouble();
      Variable variable =
          GetVariableEnumFromIndex(cVariable[i]->GetSelection());
      double value = GetValue(*it, variable);

      // Verify calculated values are valid (not NaN)
      if (!std::isfinite(time) || !std::isfinite(value) || m_maxtime == m_mintime ||
          m_maxvalue[i] == m_minvalue[i]) {
        first = true;
        continue;  // Skip invalid points
      }

      int x =
          w *
          (scale * ((time - m_mintime) / (m_maxtime - m_mintime) - position) +
           position);
      int y =
          h * (1 - (value - m_minvalue[i]) / (m_maxvalue[i] - m_minvalue[i]));
      if (first)
        first = false;
      else
        dc.DrawLine(lx, ly, x, y);
      lx = x, ly = y;
    }
  }

  // Cursor Customization
  // ----------------------------------------------------------------
  // Draw a cursor on the graph to show
  // the current time position by the GRIB file
  wxDateTime gribTime =
      m_WeatherRouting.m_ConfigurationDialog.m_GribTimelineTime;

  // Check for valid timestamps and avoid division by zero
  if (gribTime.IsValid() && m_StartTime.IsValid() && m_maxtime > m_mintime) {
    double cursorTime = (gribTime - m_StartTime).GetSeconds().ToDouble();
    if (!std::isnan(cursorTime) && cursorTime <= m_maxtime &&
        cursorTime >= m_mintime) {
      int x_cursor =
          w * (scale * ((cursorTime - m_mintime) / (m_maxtime - m_mintime) -
                        position) +
               position);

      wxColor orange(255, 165, 0);
      wxPen cursorPen(orange, 3, wxPENSTYLE_DOT);
      dc.SetPen(cursorPen);
      dc.DrawLine(x_cursor, 0, x_cursor, h);
    }
  }
  // ----------------------------------------------------------------

#ifdef __OCPN__ANDROID__
  dc.DestroyClippingRegion();
#endif
  dc.SetTextForeground(*wxBLACK);
  dc.SetPen(wxPen(*wxBLACK, 1, wxPENSTYLE_DOT));

#ifdef __OCPN__ANDROID__
  const double steps = 6;
#else
  const double steps = 10;
#endif
  bool grid = true;
  for (double i = 1 / steps; i < 1 - 1 / steps; i += 1 / steps) {
    int x = i * w, y = i * h;
    if (grid) {
      dc.DrawLine(x, 0, x, h);
      dc.DrawLine(0, y, w, y);
    }

    wxString time = ReadableTime(((i - position) / scale + position) *
                                     (m_maxtime - m_mintime) +
                                 m_mintime);
    wxSize s = dc.GetTextExtent(time);
    dc.DrawText(time, x - s.x / 2,
#ifdef __OCPN__ANDROID__
                -32
#else
                0
#endif
    );
  }

  int x =
#ifdef __OCPN__ANDROID__
      -185;
#else
      0;
#endif
  for (int ci = 0; ci < 3; ci++) {
    wxColour c = colors[ci];
    dc.SetTextForeground(
        wxColour(c.Red() * 3 / 4, c.Green() * 3 / 4, c.Blue() * 3 / 4));
    int maxx = 0;
    for (double i = 1 / steps; i < 1 - 1 / steps; i += 1 / steps) {
      wxString value = wxString::Format(
          _T("%.1f"),
          (1 - i) * (m_maxvalue[ci] - m_minvalue[ci]) + m_minvalue[ci]);
#ifdef __OCPN__ANDROID__
      if (!std::isfinite(m_minvalue[ci]) || !std::isfinite(m_maxvalue[ci])) value = "-";
#endif
      wxSize s = dc.GetTextExtent(value);
      int y = i * h;
      dc.DrawText(value, x, y - s.y / 2);

      if (s.x > maxx) maxx = s.x;
    }

    x +=
#ifdef __OCPN__ANDROID__
        60;
#else
        maxx + 5;
#endif
  }
}

void PlotDialog::OnUpdateRoute(wxCommandEvent& event) {
#ifdef __OCPN__ANDROID__
  ResetAndroidReadout();
#endif
  SetRouteMapOverlay(m_WeatherRouting.FirstCurrentRouteMap());
}

void PlotDialog::SetRouteMapOverlay(RouteMapOverlay* routemapoverlay) {
#ifdef __OCPN__ANDROID__
  if (m_androidPlotRoute != routemapoverlay ||
      m_androidCursorRoute != m_rbCursorRoute->GetValue())
    ResetAndroidReadout();
  m_androidPlotRoute = routemapoverlay;
  m_androidCursorRoute = m_rbCursorRoute->GetValue();
#endif
  if (!routemapoverlay)
    m_PlotData.clear();
  else
    m_PlotData = routemapoverlay->GetPlotData(m_rbCursorRoute->GetValue());
  GetScale();
#ifdef __OCPN__ANDROID__
  if (m_PlotData.empty()) ResetAndroidReadout();
  if (routemapoverlay && !m_PlotData.empty() && m_StartTime.IsValid()) {
    const wxDateTime end = m_rbCursorRoute->GetValue()
        ? routemapoverlay->GetLastCursorTime() : routemapoverlay->EndTime();
    if (end.IsValid())
      m_maxtime = wxMax(m_maxtime,
          (end - m_StartTime).GetSeconds().ToDouble());
  }
#endif
  m_PlotWindow->Refresh();
}

void PlotDialog::OnUpdateUI(wxUpdateUIEvent& event) {
  SetRouteMapOverlay(m_WeatherRouting.FirstCurrentRouteMap());
}
