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

#include "Utilities.h"
#include "RouteMapOverlay.h"
#include "WeatherRouting.h"

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
#ifdef __OCPN__ANDROID__
  wxSize sz = ::wxGetDisplaySize();
  SetSize(0, 0, sz.x, sz.y - 40);
#endif
}

PlotDialog::~PlotDialog() {}

void PlotDialog::OnMouseEventsPlot(wxMouseEvent& event) {
  wxStaticText* stMousePosition[3] = {m_stMousePosition1, m_stMousePosition2,
                                      m_stMousePosition3};
  if (event.Leaving()) {
    for (int i = 0; i < 3; i++) stMousePosition[i]->SetLabel(_("N/A"));
    return;
  }

  int w, h;
  m_PlotWindow->GetSize(&w, &h);

  wxPoint p = event.GetPosition();

#if 0
    double position = m_sPosition->GetValue() / 100.0;
    double scale = 100.0 / m_sScale->GetValue();

    double time = (((double)p.x/w - position) / scale + position) * (m_maxtime - m_mintime) + m_mintime;
    wxDateTime datetime = m_StartTime + wxTimeSpan(0, 0, time);
#endif

  for (int i = 0; i < 3; i++) {
    double value = (1.0 - (double)p.y / h) * (m_maxvalue[i] - m_minvalue[i]) +
                   m_minvalue[i];
    stMousePosition[i]->SetLabel(wxString::Format(_T(" %.1f"), value));
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
    case HEADING:
      return positive_degrees(data.hdg);

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
    case WAVE_REL:
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
    case HEADING:
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
    case WAVE_REL:
    case WAVE_PERIOD:
      return WAVE_HEIGHT;  // Group all wave parameters together
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
      if (GetType(cVariable[i]->GetSelection()) ==
          GetType(cVariable[j]->GetSelection())) {
        m_minvalue[i] = m_minvalue[j] = wxMin(m_minvalue[i], m_minvalue[j]);
        m_maxvalue[i] = m_maxvalue[j] = wxMax(m_maxvalue[i], m_maxvalue[j]);
      }
    }
  }
}

static wxString ReadableTime(int seconds) {
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
  double scale = 100.0 / m_sScale->GetValue();

  wxPaintDC dc(window);
  dc.Clear();
  dc.SetBackgroundMode(wxTRANSPARENT);

  int w, h;
  m_PlotWindow->GetSize(&w, &h);

  dc.SetBrush(*wxTRANSPARENT_BRUSH);

  wxChoice* cVariable[3] = {m_cVariable1, m_cVariable2, m_cVariable3};
  wxColour colors[3] = {wxColour(200, 0, 0), wxColour(0, 200, 0),
                        wxColour(0, 0, 200)};
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
      if (std::isnan(time) || std::isnan(value) || m_maxtime == m_mintime ||
          m_maxvalue[i] == m_minvalue[i]) {
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

  dc.SetTextForeground(*wxBLACK);
  dc.SetPen(wxPen(*wxBLACK, 1, wxPENSTYLE_DOT));

  const double steps = 10;  // Note that the loop only has 8 steps
  bool grid = true;
  double i = 1.0 / steps;
  while (i <
         1.0 - 1.0 / steps -
             0.5 * 1.0 / steps) {  // Avoid creating an extra step, due to rounding errors
    int x = i * w, y = i * h;
    if (grid) {
      dc.DrawLine(x, 0, x, h);
      dc.DrawLine(0, y, w, y);
    }

    wxString time = ReadableTime(((i - position) / scale + position) *
                                     (m_maxtime - m_mintime) +
                                 m_mintime);
    wxSize s = dc.GetTextExtent(time);
    dc.DrawText(time, x - s.x / 2, 0);
    i += 1.0 / steps;
  }

  int x = 0;
  for (int ci = 0; ci < 3; ci++) {
    wxColour c = colors[ci];
    dc.SetTextForeground(
        wxColour(c.Red() * 3 / 4, c.Green() * 3 / 4, c.Blue() * 3 / 4));
    int maxx = 0;
    i = 1.0 / steps;
    while (i < 1.0 - 1.0 / steps - 0.5 * 1.0 / steps) {  // This loop also has only 8 steps
      wxString value = wxString::Format(
          _T("%.1f"),
          (1 - i) * (m_maxvalue[ci] - m_minvalue[ci]) + m_minvalue[ci]);
      wxSize s = dc.GetTextExtent(value);
      int y = i * h;
      dc.DrawText(value, x, y - s.y / 2);

      if (s.x > maxx) maxx = s.x;
      i += 1.0 / steps;
    }

    x += maxx + 5;
  }
}

void PlotDialog::OnUpdateRoute(wxCommandEvent& event) {
  SetRouteMapOverlay(m_WeatherRouting.FirstCurrentRouteMap());
}

void PlotDialog::SetRouteMapOverlay(RouteMapOverlay* routemapoverlay) {
  if (!routemapoverlay)
    m_PlotData.clear();
  else
    m_PlotData = routemapoverlay->GetPlotData(m_rbCursorRoute->GetValue());
  GetScale();
  m_PlotWindow->Refresh();
}

void PlotDialog::OnUpdateUI(wxUpdateUIEvent& event) {
  SetRouteMapOverlay(m_WeatherRouting.FirstCurrentRouteMap());
}
