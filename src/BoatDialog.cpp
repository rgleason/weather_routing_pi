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

#include <wx/wx.h>
#include <wx/dcgraph.h>

#include <stdlib.h>
#include <math.h>
#include <time.h>

#include "weather_routing_pi.h"
#include "Utilities.h"
#include "Boat.h"
#include "BoatDialog.h"
#include "RouteMapOverlay.h"
#include "WeatherRouting.h"

wxString dummy_polar =
    _T("\
TWA	6	10	14	20\n\
45	5.5	6.6	7.3	7.6\n\
60	5.8	7.0	7.6	7.8\n\
90	6.2	7.3	7.8	8.1\n\
135	5.2	6.4	7.4	7.9\n\
150	4.3	5.4	6.5	7.3\n\
");

enum { spFILENAME };

// for plotting
static const int wind_speeds[] = {0,  2,  4,  6,  8,  10, 12, 15, 18, 21,
                                  24, 28, 32, 36, 40, 45, 50, 55, 60};
static const int num_wind_speeds = (sizeof wind_speeds) / (sizeof *wind_speeds);

// Generate gradient colors for wind speeds
static wxColor GetWindSpeedColor(unsigned int windSpeedIndex,
                                 unsigned int totalWindSpeeds) {
  if (totalWindSpeeds <= 1) {
    return wxColor(255, 0, 0);  // Default red for single wind speed
  }

  // Create a gradient from blue (low wind) to red (high wind)
  double ratio = (double)windSpeedIndex / (double)(totalWindSpeeds - 1);

  // Blue to green to yellow to red gradient
  int r, g, b;
  if (ratio < 0.33) {
    // Blue to green
    double localRatio = ratio / 0.33;
    r = 0;
    g = (int)(255 * localRatio);
    b = (int)(255 * (1.0 - localRatio));
  } else if (ratio < 0.66) {
    // Green to yellow
    double localRatio = (ratio - 0.33) / 0.33;
    r = (int)(255 * localRatio);
    g = 255;
    b = 0;
  } else {
    // Yellow to red
    double localRatio = (ratio - 0.66) / 0.34;
    r = 255;
    g = (int)(255 * (1.0 - localRatio));
    b = 0;
  }

  return wxColor(r, g, b);
}

BoatDialog::BoatDialog(WeatherRouting& weatherrouting)
#ifndef __WXOSX__
    : BoatDialogBase(&weatherrouting),
#else
    : BoatDialogBase(&weatherrouting, wxID_ANY, _("Boat"), wxDefaultPosition,
                     wxDefaultSize,
                     wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER | wxSTAY_ON_TOP),
#endif
      m_WeatherRouting(weatherrouting),
      m_PlotScale(0),
      m_HoveredWindSpeedIndex(-1),
      m_ShowHoverInfo(false),
      m_CursorValid(false),
      m_CursorWindAngle(0),
      m_CursorWindSpeed(0),
      m_CursorBoatSpeed(0),
      m_CursorVMG(0),
      m_CursorVMGAngle(0),
      m_CrossOverRegenerate(false),
      m_CrossOverGenerationThread(NULL) {
  // for small screens: don't let boat dialog be larger than screen
  int w, h;
  wxDisplaySize(&w, &h);
  w = wxMin(w, GetMinWidth());
  h = wxMin(h - 32, GetMinHeight());
  SetMinSize(wxSize(w, h));
  SetSize(wxSize(w, h));

  m_lPolars->InsertColumn(spFILENAME, _("Filename"));

  wxFileConfig* pConf = GetOCPNConfigObject();
  pConf->SetPath(_T( "/PlugIns/WeatherRouting/BoatDialog" ));

#ifdef __OCPN__ANDROID__
  wxSize sz = ::wxGetDisplaySize();
  SetSize(0, 0, sz.x, sz.y - 40);
#else
  // hack to adjust items
  SetSize(wxSize(w, h));
#endif
}

BoatDialog::~BoatDialog() {
  wxFileConfig* pConf = GetOCPNConfigObject();
  pConf->SetPath(_T( "/PlugIns/WeatherRouting/BoatDialog" ));
}

void BoatDialog::LoadPolar(const wxString& filename) {
  m_boatpath = filename;

  SetTitle(m_boatpath);
  wxString error = m_Boat.OpenXML(m_boatpath, false);
  RepopulatePolars();

  /* select first polar if it exists */
  if (m_lPolars->GetItemCount())
    m_lPolars->SetItemState(0, wxLIST_STATE_SELECTED, wxLIST_STATE_SELECTED);

  if (error.size()) {
    wxMessageDialog md(this, error, _("OpenCPN Weather Routing Plugin"),
                       wxICON_ERROR | wxOK);
    md.ShowModal();
  }

  //    m_lBoatPlans->SetItemState(m_SelectedSailPlan, wxLIST_STATE_SELECTED,
  //    wxLIST_STATE_SELECTED);

  UpdateVMG();
}

void BoatDialog::OnMouseEventsPolarPlot(wxMouseEvent& event) {
  if (event.Leaving()) {
    m_HoveredWindSpeedIndex = -1;
    m_ShowHoverInfo = false;
    m_CursorValid = false;
    m_PlotWindow->Refresh();
    UpdateCursorInfo();  // Reset cursor info display
    return;
  }

  long index = SelectedPolar();
  if (index < 0) {
    m_CursorValid = false;
    return;
  }

  wxPoint p = event.GetPosition();
  int w, h;
  m_PlotWindow->GetSize(&w, &h);

  int plottype = m_cPlotType->GetSelection();
  if (plottype == 0) {  // polar chart
    if (!m_PlotScale) {
      m_CursorValid = false;
      return;
    }

    // Convert mouse position to polar coordinates
    bool full = m_cbFullPlot->GetValue();
    int cx = full ? w / 2 : 0;
    double x = (double)p.x - cx;
    double y = (double)p.y - h / 2;

    double mouse_distance = sqrt(x * x + y * y) / m_PlotScale;
    double mouse_angle = rad2posdeg(atan2(x, -y));

    // Store cursor position and calculate interpolated values
    m_CursorPosition = p;
    m_CursorValid = true;
    m_CursorWindAngle = mouse_angle;

    Polar& polar = m_Boat.Polars[index];
    int selection = m_cPlotVariable->GetSelection();

    // Interpolate between wind speeds to get values at cursor position
    double interpolated_wind_speed = 0;
    double interpolated_boat_speed = 0;
    bool found_interpolation = false;

    // Find the two closest wind speeds for interpolation
    for (unsigned int VWi = 0; VWi < polar.wind_speeds.size() - 1; VWi++) {
      double VW1 = polar.wind_speeds[VWi].tws;
      double VW2 = polar.wind_speeds[VWi + 1].tws;

      double stw1 = 0, stw2 = 0;
      if (selection < 2) {
        stw1 = polar.Speed(mouse_angle, VW1);
        stw2 = polar.Speed(mouse_angle, VW2);
      } else {
        stw1 = polar.SpeedAtApparentWindSpeed(mouse_angle, VW1);
        stw2 = polar.SpeedAtApparentWindSpeed(mouse_angle, VW2);
      }

      if (std::isnan(stw1) || std::isnan(stw2)) continue;

      // Check if mouse distance is between these two curves
      if (mouse_distance >= stw1 && mouse_distance <= stw2) {
        // Linear interpolation
        double ratio = (mouse_distance - stw1) / (stw2 - stw1);
        interpolated_wind_speed = VW1 + ratio * (VW2 - VW1);
        interpolated_boat_speed = mouse_distance;
        found_interpolation = true;
        break;
      }
    }

    if (found_interpolation) {
      m_CursorWindSpeed = interpolated_wind_speed;
      m_CursorBoatSpeed = interpolated_boat_speed;
      m_CursorVMG = interpolated_boat_speed * cos(deg2rad(mouse_angle));

      // Calculate optimal VMG angle for current wind speed
      if (interpolated_wind_speed > 0) {
        SailingVMG vmg = polar.GetVMGTrueWind(interpolated_wind_speed);

        // Find the VMG angle closest to the cursor position
        double min_angle_diff = 360.0;
        m_CursorVMGAngle = NAN;

        for (int i = 0; i < 4; i++) {
          if (!std::isnan(vmg.values[i])) {
            double angle_diff = fabs(mouse_angle - vmg.values[i]);
            if (angle_diff > 180)
              angle_diff = 360 - angle_diff;  // Handle wrap-around

            if (angle_diff < min_angle_diff) {
              min_angle_diff = angle_diff;
              m_CursorVMGAngle = vmg.values[i];
            }
          }
        }
      } else {
        m_CursorVMGAngle = NAN;
      }
    } else {
      // If no interpolation found between curves, extrapolate wind speed based
      // on cursor position Find the two closest wind speed curves to bracket
      // the cursor position
      double lower_wind_speed = 0, upper_wind_speed = 0;
      double lower_boat_speed = 0, upper_boat_speed = 0;
      bool found_lower = false, found_upper = false;

      for (unsigned int VWi = 0; VWi < polar.wind_speeds.size(); VWi++) {
        double VW = polar.wind_speeds[VWi].tws;
        double stw = 0;
        if (selection < 2)
          stw = polar.Speed(mouse_angle, VW);
        else
          stw = polar.SpeedAtApparentWindSpeed(mouse_angle, VW);

        if (std::isnan(stw)) continue;

        if (stw <= mouse_distance) {
          // This curve is inside the cursor position
          if (!found_lower || stw > lower_boat_speed) {
            lower_wind_speed = VW;
            lower_boat_speed = stw;
            found_lower = true;
          }
        } else {
          // This curve is outside the cursor position
          if (!found_upper || stw < upper_boat_speed) {
            upper_wind_speed = VW;
            upper_boat_speed = stw;
            found_upper = true;
          }
        }
      }

      // Interpolate or extrapolate wind speed based on cursor position
      if (found_lower && found_upper) {
        // Interpolate between the two bracketing curves
        double ratio = (mouse_distance - lower_boat_speed) /
                       (upper_boat_speed - lower_boat_speed);
        m_CursorWindSpeed =
            lower_wind_speed + ratio * (upper_wind_speed - lower_wind_speed);
      } else if (found_lower) {
        // Cursor is outside all curves - extrapolate from the outermost curve
        if (polar.wind_speeds.size() >= 2) {
          // Use the trend from the two outermost curves for extrapolation
          double outer_wind1 =
              polar.wind_speeds[polar.wind_speeds.size() - 2].tws;
          double outer_wind2 =
              polar.wind_speeds[polar.wind_speeds.size() - 1].tws;
          double outer_stw1 = polar.Speed(mouse_angle, outer_wind1);
          double outer_stw2 = polar.Speed(mouse_angle, outer_wind2);

          if (!std::isnan(outer_stw1) && !std::isnan(outer_stw2) &&
              outer_stw2 != outer_stw1) {
            double wind_per_speed =
                (outer_wind2 - outer_wind1) / (outer_stw2 - outer_stw1);
            m_CursorWindSpeed =
                lower_wind_speed +
                (mouse_distance - lower_boat_speed) * wind_per_speed;
          } else {
            m_CursorWindSpeed = lower_wind_speed;
          }
        } else {
          m_CursorWindSpeed = lower_wind_speed;
        }
      } else if (found_upper) {
        // Cursor is inside all curves - extrapolate from the innermost curve
        if (polar.wind_speeds.size() >= 2) {
          // Use the trend from the two innermost curves for extrapolation
          double inner_wind1 = polar.wind_speeds[0].tws;
          double inner_wind2 = polar.wind_speeds[1].tws;
          double inner_stw1 = polar.Speed(mouse_angle, inner_wind1);
          double inner_stw2 = polar.Speed(mouse_angle, inner_wind2);

          if (!std::isnan(inner_stw1) && !std::isnan(inner_stw2) &&
              inner_stw2 != inner_stw1) {
            double wind_per_speed =
                (inner_wind2 - inner_wind1) / (inner_stw2 - inner_stw1);
            m_CursorWindSpeed =
                upper_wind_speed -
                (upper_boat_speed - mouse_distance) * wind_per_speed;
            // Ensure we don't get negative wind speeds
            if (m_CursorWindSpeed < 0) m_CursorWindSpeed = 0;
          } else {
            m_CursorWindSpeed = upper_wind_speed;
          }
        } else {
          m_CursorWindSpeed = upper_wind_speed;
        }
      } else {
        // No valid curves found - fallback
        m_CursorWindSpeed = 0;
      }

      m_CursorBoatSpeed = mouse_distance;
      m_CursorVMG = mouse_distance * cos(deg2rad(mouse_angle));

      // Calculate VMG angle for the interpolated/extrapolated wind speed
      if (m_CursorWindSpeed > 0) {
        SailingVMG vmg = polar.GetVMGTrueWind(m_CursorWindSpeed);

        // Find the VMG angle closest to the cursor position
        double min_angle_diff = 360.0;
        m_CursorVMGAngle = NAN;

        for (int i = 0; i < 4; i++) {
          if (!std::isnan(vmg.values[i])) {
            double angle_diff = fabs(mouse_angle - vmg.values[i]);
            if (angle_diff > 180)
              angle_diff = 360 - angle_diff;  // Handle wrap-around

            if (angle_diff < min_angle_diff) {
              min_angle_diff = angle_diff;
              m_CursorVMGAngle = vmg.values[i];
            }
          }
        }
      } else {
        m_CursorVMGAngle = NAN;
      }
    }

    // Find closest wind speed line for highlighting
    int closest_wind_index = -1;
    double min_distance_diff = 1e6;

    for (unsigned int VWi = 0; VWi < polar.wind_speeds.size(); VWi++) {
      double VW = polar.wind_speeds[VWi].tws;

      double stw = 0;
      if (selection < 2)
        stw = polar.Speed(mouse_angle, VW);
      else
        stw = polar.SpeedAtApparentWindSpeed(mouse_angle, VW);

      if (std::isnan(stw)) continue;

      double distance_diff = fabs(mouse_distance - stw);
      if (distance_diff < min_distance_diff &&
          distance_diff < 0.5) {  // within 0.5 knot tolerance
        min_distance_diff = distance_diff;
        closest_wind_index = VWi;
      }
    }

    bool need_refresh = false;
    if (closest_wind_index != m_HoveredWindSpeedIndex) {
      m_HoveredWindSpeedIndex = closest_wind_index;
      m_ShowHoverInfo = (closest_wind_index >= 0);
      need_refresh = true;
    }

    // Always refresh to update cursor position
    m_PlotWindow->Refresh();

    // Always update cursor information display
    UpdateCursorInfo();
  } else {
    m_CursorValid = false;
    UpdateCursorInfo();
  }
}

void BoatDialog::OnPaintPlot(wxPaintEvent& event) {
  wxWindow* window = m_PlotWindow;

  wxPaintDC dc(window);
  dc.SetBackground(*wxWHITE_BRUSH);
  dc.Clear();
  dc.SetBackgroundMode(wxTRANSPARENT);

  long index = SelectedPolar();
  if (index < 0) {
    dc.SetTextForeground(*wxBLACK);
    wxString str = _("Select a polar to view plot");
    int sw, sh;
    dc.GetTextExtent(str, &sw, &sh);
    dc.DrawText(str, 0, 0);
    return;
  }

  int plottype = m_cPlotType->GetSelection();
  int w, h;
  m_PlotWindow->GetSize(&w, &h);
  double maxVB = 0;

  Polar& polar = m_Boat.Polars[index];

  /* plot scale */
  int selection = m_cPlotVariable->GetSelection();

  for (unsigned int VWi = 0; VWi < polar.wind_speeds.size(); VWi++) {
    double windspeed = polar.wind_speeds[VWi].tws;
    for (unsigned int Wi = 0; Wi < polar.degree_steps.size(); Wi++) {
      double W = polar.degree_steps[Wi];
      double stw = 0;
      if (selection < 2)
        stw = polar.Speed(W, windspeed);
      else
        stw = polar.SpeedAtApparentWindSpeed(W, windspeed);

      if (stw > maxVB) maxVB = stw;
    }
  }

  dc.SetPen(wxPen(wxColor(0, 0, 0)));
  dc.SetBrush(*wxTRANSPARENT_BRUSH);
  dc.SetTextForeground(wxColour(0, 55, 75));

  if (maxVB <= 0) maxVB = 1; /* avoid lock */
  double Vstep = ceil(maxVB / 5);
  maxVB += Vstep;

  bool full = m_cbFullPlot->GetValue();

  // Optimize scale based on full plot vs half plot
  if (full) {
    // Full plot needs square aspect ratio (both port and starboard)
    m_PlotScale = (w < h ? w : h) / 1.8 / (maxVB + 1);
  } else {
    // Half plot can use rectangular aspect ratio (starboard side only)
    // Use width more efficiently since we only show 0-180 degrees
    double scale_from_width =
        w / 1.2 / (maxVB + 1);  // Less margin since we use full width
    double scale_from_height = h / 1.8 / (maxVB + 1);  // Same margin as before
    m_PlotScale = (scale_from_width < scale_from_height ? scale_from_width
                                                        : scale_from_height);
  }

  int xc = full ? w / 2 : 0;

  if (plottype == 0) {
    /* polar circles */
    for (double V = Vstep; V <= maxVB; V += Vstep) {
      dc.DrawCircle(xc, h / 2, V * m_PlotScale);
      dc.DrawText(wxString::Format(_T("%.0f"), V), xc,
                  h / 2 + (int)V * m_PlotScale);
    }
  } else {
    for (double V = Vstep; V <= maxVB; V += Vstep) {
      int y = h - 2 * V * m_PlotScale;
      dc.DrawLine(0, y, w, y);
      dc.DrawText(wxString::Format(_T("%.0f"), V), 0, y);
    }
  }

  dc.SetTextForeground(wxColour(0, 0, 155));

  if (plottype == 0) {
    /* polar meridians */
    for (double ctw = 0; ctw < DEGREES; ctw += 15) {
      double x = maxVB * m_PlotScale * sin(deg2rad(ctw));
      double y = maxVB * m_PlotScale * cos(deg2rad(ctw));
      if (ctw < 180) dc.DrawLine(xc - x, h / 2 + y, xc + x, h / 2 - y);

      wxString str = wxString::Format(_T("%.0f"), ctw);
      int sw, sh;
      dc.GetTextExtent(str, &sw, &sh);
      dc.DrawText(str, xc + .9 * x - sw / 2, h / 2 - .9 * y - sh / 2);
    }
  } else {
    for (int s = 0; s < num_wind_speeds; s++) {
      double windspeed = wind_speeds[s];

      double x = s * w / num_wind_speeds;
      dc.DrawLine(x, 0, x, h);

      wxString str = wxString::Format(_T("%.0f"), windspeed);
      int sw, sh;
      dc.GetTextExtent(str, &sw, &sh);
      dc.DrawText(str, x, 0);
    }
  }

  int cx = (full ? w / 2 : 0), cy = h / 2;

  /* boat speeds */
  if (plottype == 0) {
    for (unsigned int VWi = 0; VWi < polar.wind_speeds.size(); VWi++) {
      // Set different color for each wind speed line
      wxColor windColor = GetWindSpeedColor(VWi, polar.wind_speeds.size());

      // Highlight hovered wind speed line
      int penWidth = 2;
      if (m_ShowHoverInfo && (int)VWi == m_HoveredWindSpeedIndex) {
        penWidth = 4;  // Make hovered line thicker
      }

      dc.SetPen(wxPen(windColor, penWidth));
      double VW = 0, aws = 0;
      switch (selection) {
        case 0:
        case 1:
          VW = polar.wind_speeds[VWi].tws;
          break;
          // use grid vw for va steps
        case 2:
        case 3:
          aws = polar.wind_speeds[VWi].tws;
          break;
      }

      bool lastvalid = false;
      int lx = 0, ly = 0;
      //            for(unsigned int Wi = 0; Wi<polar.degree_steps.size()+full;
      //            Wi++) {
      //              double W =
      //              polar.degree_steps[Wi%polar.degree_steps.size()];
      double W0 = polar.degree_steps[0];
      double Wn = polar.degree_steps[polar.degree_steps.size() - 1];
      double Wd = Wn - W0, Ws = Wd / floor(Wd);
      for (double W = W0; W <= Wn; W += Ws) {
        double stw = 0;
        switch (selection) {
          case 0:
          case 1:
            stw = polar.Speed(W, VW);
            break;
          case 2:
          case 3:
            stw = polar.SpeedAtApparentWindSpeed(W, aws);
            VW = Polar::VelocityTrueWind(aws, stw, W);
            break;
        }

        if (std::isnan(stw)) {
          lastvalid = false;
          continue;
        }

        double a = 0;

        switch (selection) {
          case 0:
          case 2:
            a = W;
            break;
          case 1:
          case 3:
            a = Polar::DirectionApparentWind(stw, W, VW);
            break;
        }

        int px, py;
        px = m_PlotScale * stw * sin(deg2rad(a)) + cx;
        py = -m_PlotScale * stw * cos(deg2rad(a)) + cy;

        if (lastvalid) {
          dc.DrawLine(lx, ly, px, py);
          if (full) dc.DrawLine(2 * cx - lx, ly, 2 * cx - px, py);

          //            dc.DrawArc(lx, ly, px, py, cx, cy);
        }

        lx = px, ly = py;
        lastvalid = true;
      }
    }
  } else {
    for (unsigned int Wi = 0; Wi < polar.degree_steps.size(); Wi++) {
      // Set different color for each wind angle line
      wxColor windColor = GetWindSpeedColor(Wi, polar.degree_steps.size());
      dc.SetPen(wxPen(windColor, 2));

      double W = polar.degree_steps[Wi], stw = 0;

      bool lastvalid = false;
      int lx = 0, ly = 0;
      for (unsigned int VWi = 0; VWi < polar.wind_speeds.size(); VWi++) {
        double windspeed = polar.wind_speeds[VWi].tws;
        double VW = 0, aws = 0;
        switch (selection) {
          case 0:
          case 1:
            VW = windspeed;
            break;
            // use grid vw for va steps
          case 2:
          case 3:
            aws = windspeed;
            break;
        }

        switch (selection) {
          case 0:
          case 1:
            stw = polar.Speed(W, VW);
            break;
          case 2:
          case 3:
            stw = polar.SpeedAtApparentWindSpeed(W, aws);
            VW = Polar::VelocityTrueWind(aws, stw, W);
            break;
        }

        if (std::isnan(stw)) {
          lastvalid = false;
          continue;
        }

#if 0
                double a = 0;

                switch(selection) {
                case 0: case 2: a = W; break;
                case 1: case 3: a = Polar::DirectionApparentWind(stw, W, VW); break;
                }
#endif

        int px, py;
        int s;
        for (s = 0; s < num_wind_speeds - 1; s++)
          if (wind_speeds[s] > windspeed) break;
        {  // interpolate into non-linear windspeed space
          double x = windspeed, x1 = wind_speeds[s], x2 = wind_speeds[s + 1];
          double y1 = s * w / num_wind_speeds;
          double y2 = (s + 1) * w / num_wind_speeds;

          px = x2 - x1 ? (y2 - y1) * (x - x1) / (x2 - x1) + y1 : y1;
        }
        py = h - 2 * stw * m_PlotScale;

        if (lastvalid) {
          dc.DrawLine(lx, ly, px, py);
        }

        lx = px, ly = py;
        lastvalid = true;
      }
    }
  }

  /* vmg */
  wxPoint lastp[4];
  bool lastpvalid[4] = {false, false, false, false};
  for (unsigned int VWi = 0; VWi < polar.wind_speeds.size(); VWi++) {
    double aws, VW = polar.wind_speeds[VWi].tws;
    double windspeed = polar.wind_speeds[VWi].tws;
    SailingVMG vmg = polar.GetVMGTrueWind(VW);

    for (int i = 0; i < 4; i++) {
      if (i % 2 == 1 && !full) continue;

      if (i < 2)
        dc.SetPen(wxPen(wxColor(255, 0, 255), 2));
      else
        dc.SetPen(wxPen(wxColor(0, 255, 255), 2));

      double W = vmg.values[i];
      if (std::isnan(W)) continue;

      // Calculate boat speed at VMG angle
      double stw = 0;
      switch (selection) {
        case 0:
        case 1:
          stw = polar.Speed(W, VW);
          break;
        case 2:
        case 3:
          aws = windspeed;
          stw = polar.SpeedAtApparentWindSpeed(W, aws);
          break;
      }

      // Skip if speed calculation failed
      if (std::isnan(stw) || stw <= 0) continue;

      // Calculate display angle
      double a = 0;
      switch (selection) {
        case 0:
        case 2:
          a = W;
          break;
        case 1:
        case 3:
          a = Polar::DirectionApparentWind(stw, W, VW);
          break;
      }

      wxPoint p;
      if (plottype == 0) {
        p.x = m_PlotScale * stw * sin(deg2rad(a)) + cx;
        p.y = -m_PlotScale * stw * cos(deg2rad(a)) + cy;
      } else {
        int s;
        for (s = 0; s < num_wind_speeds - 1; s++)
          if (wind_speeds[s] > windspeed) break;
        {  // interpolate into non-linear windspeed space
          double x = windspeed, x1 = wind_speeds[s], x2 = wind_speeds[s + 1];
          double y1 = s * w / num_wind_speeds;
          double y2 = (s + 1) * w / num_wind_speeds;

          p.x = x2 - x1 ? (y2 - y1) * (x - x1) / (x2 - x1) + y1 : y1;
        }
        p.y = h - 2 * stw * m_PlotScale;
      }

      if (lastpvalid[i]) dc.DrawLine(lastp[i], p);
      lastp[i] = p;
      lastpvalid[i] = true;
    }
  }

  // Draw cursor indicator and additional information
  if (m_CursorValid && plottype == 0) {
    bool full = m_cbFullPlot->GetValue();
    int cx = full ? w / 2 : 0;
    int cy = h / 2;

    // Draw cursor cross with better contrast - use dark color with white
    // outline
    dc.SetBrush(*wxTRANSPARENT_BRUSH);

    // Use actual mouse position for cursor cross to ensure it follows mouse
    // exactly
    double cursor_x = m_CursorPosition.x;
    double cursor_y = m_CursorPosition.y;

    // Draw cross at cursor position with better visibility
    int cross_size = 8;

    // Draw white outline for contrast
    dc.SetPen(wxPen(wxColor(255, 255, 255), 3));  // Thick white outline
    dc.DrawLine(cursor_x - cross_size, cursor_y, cursor_x + cross_size,
                cursor_y);
    dc.DrawLine(cursor_x, cursor_y - cross_size, cursor_x,
                cursor_y + cross_size);

    // Draw dark cross on top
    dc.SetPen(wxPen(wxColor(0, 0, 0), 2));  // Black cross
    dc.DrawLine(cursor_x - cross_size, cursor_y, cursor_x + cross_size,
                cursor_y);
    dc.DrawLine(cursor_x, cursor_y - cross_size, cursor_x,
                cursor_y + cross_size);

    // Draw a circle around the cross for better visibility
    dc.SetPen(wxPen(wxColor(255, 255, 255), 2));  // White outline
    dc.DrawCircle(cursor_x, cursor_y, cross_size + 2);
    dc.SetPen(wxPen(wxColor(0, 0, 0), 1));  // Black inner circle
    dc.DrawCircle(cursor_x, cursor_y, cross_size + 2);

    // Draw VMG angle indicators for current cursor wind speed
    if (m_CursorWindSpeed > 0) {
      SailingVMG vmg = polar.GetVMGTrueWind(m_CursorWindSpeed);

      // Draw VMG angle lines with better contrast
      for (int i = 0; i < 4; i++) {
        if (i % 2 == 1 && !full) continue;  // Skip port side if not full plot

        double W = vmg.values[i];
        if (std::isnan(W)) continue;

        double stw = polar.Speed(W, m_CursorWindSpeed);
        if (std::isnan(stw)) continue;

        // Draw line from center to VMG point with white outline for contrast
        double vmg_x = m_PlotScale * stw * sin(deg2rad(W)) + cx;
        double vmg_y = -m_PlotScale * stw * cos(deg2rad(W)) + cy;

        // Draw thick white outline
        dc.SetPen(wxPen(wxColor(255, 255, 255), 4));
        dc.DrawLine(cx, cy, vmg_x, vmg_y);

        // Draw dark red line on top for good contrast
        dc.SetPen(wxPen(wxColor(128, 0, 0), 2));
        dc.DrawLine(cx, cy, vmg_x, vmg_y);

        // Draw VMG point with better contrast
        // White outline circle
        dc.SetPen(wxPen(wxColor(255, 255, 255), 2));
        dc.SetBrush(wxBrush(wxColor(255, 255, 255)));
        dc.DrawCircle(vmg_x, vmg_y, 5);

        // Dark red inner circle
        dc.SetPen(wxPen(wxColor(128, 0, 0), 1));
        dc.SetBrush(wxBrush(wxColor(128, 0, 0)));
        dc.DrawCircle(vmg_x, vmg_y, 3);

        dc.SetBrush(*wxTRANSPARENT_BRUSH);
      }
    }
  }
}

static int CalcPolarPoints(wxPoint p0, wxPoint p1) {
  return floor(fabs((double)p0.x - p1.x) / 5) + 1;
}

void BoatDialog::OnPaintCrossOverChart(wxPaintEvent& event) {
  wxWindow* window = m_CrossOverChart;

  wxPaintDC dc(window);

  dc.SetBackground(*wxWHITE_BRUSH);
  dc.Clear();
  dc.SetBackgroundMode(wxTRANSPARENT);

  long index = SelectedPolar();
  bool polar = !m_cPlotType->GetSelection();

  int w, h;
  m_CrossOverChart->GetSize(&w, &h);

  dc.SetPen(wxPen(wxColor(0, 0, 0)));
  dc.SetBrush(*wxTRANSPARENT_BRUSH);
  dc.SetTextForeground(wxColour(0, 55, 75));

  bool full = m_cbFullPlot->GetValue();
  double scale = 0;
  int xc = full ? w / 2 : 0;
  if (polar) scale = wxMin(full ? w / 2 : w, h / 2) / 40.0;

  for (double VW = 0; VW < 40; VW += 10) {
    if (polar) {
      dc.DrawCircle(xc, h / 2, VW * scale);
      dc.DrawText(wxString::Format(_T("%.0f"), VW), xc,
                  h / 2 + (int)VW * scale);
    } else {
      int y = h - VW * h / 40;
      dc.DrawLine(0, y, w, y);
      dc.DrawText(wxString::Format(_T("%.0f"), VW), 0, y);
    }
  }

  for (double H = 0; H < 180; H += 10) {
    if (polar) {
      double x = scale * sin(deg2rad(H));
      double y = scale * cos(deg2rad(H));
      if (H < 180) dc.DrawLine(xc - x, h / 2 + y, xc + x, h / 2 - y);

      wxString str = wxString::Format(_T("%.0f"), H);
      int sw, sh;
      dc.GetTextExtent(str, &sw, &sh);
      dc.DrawText(str, xc + .9 * x - sw / 2, h / 2 - .9 * y - sh / 2);
    } else {
      int x = H * w / 180;
      dc.DrawLine(x, 0, x, h);
      dc.DrawText(wxString::Format(_T("%.0f"), H), x, 0);
    }
  }

  wxColour colors[] = {*wxRED,  *wxGREEN,  *wxBLUE,
                       *wxCYAN, *wxYELLOW, wxColour(255, 0, 255)};
  int c = 0;
  for (int i = 0; i < (int)m_Boat.Polars.size(); i++) {
    bool bold = i == index;
    wxColour col(colors[c].Red(), colors[c].Green(), colors[c].Blue(),
                 bold ? 230 : 90);
#if wxUSE_GRAPHICS_CONTEXT
    wxGCDC gdc(dc);
    gdc.SetPen(*wxTRANSPARENT_PEN);
    gdc.SetBrush(col);
#else
    dc.SetPen(*wxTRANSPARENT_PEN);
    if (bold) dc.SetBrush(*wxBLACK);
    dc.SetBrush(col);
#endif
    if (++c == (sizeof colors) / (sizeof *colors)) c = 0;

    bool tri = true;
    TESStesselator* tess = m_Boat.Polars[i].CrossOverRegion.Tesselate(tri);

    if (!tess) continue;

    const float* verts = tessGetVertices(tess);
    //        const int* vinds = tessGetVertexIndices(tess);
    const int* elems = tessGetElements(tess);
    //        const int nverts = tessGetVertexCount(tess);
    const int nelems = tessGetElementCount(tess);

    // Draw polygons.
    for (int i = 0; i < nelems; ++i) {
      if (tri) {
        const int* p = &elems[i * 3];
        wxPoint points[3];
        for (unsigned j = 0; j < 3 && p[j] != TESS_UNDEF; ++j) {
          double H = verts[p[j] * 2 + 0];
          double VW = verts[p[j] * 2 + 1];
          points[j] = wxPoint(H * w / 180, h - VW * h / 40);
        }
        if (polar) {
          int count[3] = {CalcPolarPoints(points[0], points[1]),
                          CalcPolarPoints(points[1], points[2]),
                          CalcPolarPoints(points[2], points[0])};
          wxPoint* pts = new wxPoint[count[0] + count[1] + count[2]];
          int c = 0;
          for (int j = 0; j < 3; j++) {
            int jp1 = j + 1 == 3 ? 0 : j + 1;
            for (int k = 0; k < count[j]; k++) {
              double d = (double)k / count[j];
              double px = points[j].x * (1 - d) + points[jp1].x * d;
              double py = points[j].y * (1 - d) + points[jp1].y * d;
              double H = px / w * 180;
              double VW = (h - py) / h * 40;
              pts[c++] = wxPoint(xc + scale * VW * sin(deg2rad(H)),
                                 h / 2 - scale * VW * cos(deg2rad(H)));
            }
          }
#if wxUSE_GRAPHICS_CONTEXT
          gdc.DrawPolygon(c, pts);
#else
          dc.DrawPolygon(c, pts);
#endif
          if (full) {
            for (int j = 0; j < c; j++) pts[j].x = 2 * xc - pts[j].x;
#if wxUSE_GRAPHICS_CONTEXT
            gdc.DrawPolygon(c, pts);
#else
            dc.DrawPolygon(c, pts);
#endif
          }
          delete[] pts;
        } else {
#if wxUSE_GRAPHICS_CONTEXT
          gdc.DrawPolygon(3, points);
#else
          dc.DrawPolygon(3, points);
#endif
        }
      } else {
        int b = elems[i * 2];
        int n = elems[i * 2 + 1];

        wxPoint pl;
        for (int j = 0; j <= n; j++) {
          int k = j < n ? j : 0;
          float H = verts[2 * (b + k) + 0], VW = verts[2 * (b + k) + 1];
          wxPoint p0;
          if (polar)
            p0 = wxPoint(xc + scale * VW * sin(deg2rad(H)),
                         h / 2 - scale * VW * cos(deg2rad(H)));
          else
            p0 = wxPoint(H * w / 180, h - VW * h / 40);

          if (j > 0) dc.DrawLine(pl, p0);
          pl = p0;
        }
      }
    }

    tessDeleteTess(tess);
  }
}

void BoatDialog::OnOverlapPercentage(wxSpinEvent& event) {
  long i = SelectedPolar();
  if (i != -1)
    m_Boat.Polars[i].m_crossoverpercentage =
        m_sOverlapPercentage->GetValue() / 100.0;
  GenerateCrossOverChart();
}

void BoatDialog::OnOpenBoat(wxCommandEvent& event) {
  wxFileConfig* pConf = GetOCPNConfigObject();
  pConf->SetPath(_T( "/PlugIns/WeatherRouting/BoatDialog" ));

  wxString path;
  pConf->Read(_T ( "BoatPath" ), &path,
              weather_routing_pi::StandardPath() + _T("boats"));

  wxFileDialog openDialog(
      this, _("Select Boat"), path, wxT(""),
      wxT("Boat polar (*.xml)|*.XML;*.xml|All files (*.*)|*.*"), wxFD_OPEN);

  if (openDialog.ShowModal() == wxID_OK) {
    pConf->SetPath(_T( "/PlugIns/WeatherRouting/BoatDialog" ));
    pConf->Write(_T ( "BoatPath" ), openDialog.GetDirectory());

    wxString filename = openDialog.GetPath();
    wxString error = m_Boat.OpenXML(filename);
    if (error.empty()) {
      RepopulatePolars();
    } else {
      wxMessageDialog md(this, error, _("OpenCPN Weather Routing Plugin"),
                         wxICON_ERROR | wxOK);
      md.ShowModal();
      return;
    }

    UpdateVMG();
    RefreshPlots();
  }
}

void BoatDialog::SaveBoat() {
  // wait for crossover generation to comple
  while (m_CrossOverGenerationThread) {
    wxYield();
    wxThread::Sleep(10);
  }

  if (m_boatpath.empty()) {
    wxFileConfig* pConf = GetOCPNConfigObject();
    pConf->SetPath(_T( "/PlugIns/WeatherRouting/BoatDialog" ));

    wxString path;
    pConf->Read(_T ( "BoatPath" ), &path,
                weather_routing_pi::StandardPath() + _T("boats"));

    wxFileDialog saveDialog(
        this, _("Select Boat"), path, wxT(""),
        wxT("Boat files (*.xml)|*.XML;*.xml|All files (*.*)|*.*"),
        wxFD_SAVE | wxFD_OVERWRITE_PROMPT);

    if (saveDialog.ShowModal() == wxID_OK) {
      pConf->SetPath(_T( "/PlugIns/WeatherRouting/BoatDialog" ));
      pConf->Write(_T ( "BoatPath" ), saveDialog.GetDirectory());

      wxString filename =
          wxFileDialog::AppendExtension(saveDialog.GetPath(), _T("*.xml"));
      m_boatpath = filename;
      SetTitle(m_boatpath);
    } else
      return;
  }

  wxString error = m_Boat.SaveXML(m_boatpath);
  if (error.empty()) {
    m_WeatherRouting.m_ConfigurationDialog.SetBoatFilename(m_boatpath);
    /* update any configurations that use this boat */
    m_WeatherRouting.UpdateBoatFilename(m_boatpath);
    Update();

    Hide();
  } else {
    wxMessageDialog md(this, error, _("OpenCPN Weather Routing Plugin"),
                       wxICON_ERROR | wxOK);
    md.ShowModal();
  }
}

void BoatDialog::OnSaveAsBoat(wxCommandEvent& event) {
  m_boatpath.clear();
  SaveBoat();
}

void BoatDialog::OnClose(wxCommandEvent& event) { EndModal(wxID_CANCEL); }

void BoatDialog::OnPolarSelected() {
  int i = SelectedPolar();
  m_bEditPolar->Enable(i != -1);
  m_bRemovePolar->Enable(i != -1);

  // not needed if modal    m_EditPolarDialog.SetPolarIndex(i);

  m_sOverlapPercentage->Enable(i != -1);
  if (i != -1)
    m_sOverlapPercentage->SetValue(m_Boat.Polars[i].m_crossoverpercentage *
                                   100);

  RefreshPlots();
  UpdateVMG();
  UpdateBestVMGInfo();  // Update best VMG info when polar selection changes
}

void BoatDialog::OnUpdatePlot() {
  m_cbFullPlot->Enable(!m_cPlotType->GetSelection());
  RefreshPlots();
}

void BoatDialog::OnUpPolar(wxCommandEvent& event) {
  long index = SelectedPolar();
  if (index < 1) return;

#ifndef __OCPN__ANDROID__
  m_Boat.Polars.insert(m_Boat.Polars.begin() + index - 1,
                       m_Boat.Polars.at(index));
  m_Boat.Polars.erase(m_Boat.Polars.begin() + index + 1);
#endif
  RepopulatePolars();

  m_lPolars->SetItemState(index - 1, wxLIST_STATE_SELECTED,
                          wxLIST_STATE_SELECTED);
}

void BoatDialog::OnDownPolar(wxCommandEvent& event) {
  long index = SelectedPolar();
  if (index < 0 || index + 1 >= (long)m_Boat.Polars.size()) return;

#ifndef __OCPN__ANDROID__
  m_Boat.Polars.insert(m_Boat.Polars.begin() + index + 2,
                       m_Boat.Polars.at(index));
  m_Boat.Polars.erase(m_Boat.Polars.begin() + index);
#endif
  RepopulatePolars();

  m_lPolars->SetItemState(index + 1, wxLIST_STATE_SELECTED,
                          wxLIST_STATE_SELECTED);
}

void BoatDialog::OnEditPolar(wxCommandEvent& event) {
  // m_EditPolarDialog.Show();

  int i = SelectedPolar();
  if (i == -1) return;

  EditPolarDialog dlg(this);

  dlg.SetPolarIndex(i);
  wxString filename = m_Boat.Polars[i].FileName;
  if (dlg.ShowModal() == wxID_SAVE) {
    if (!m_Boat.Polars[i].Save(filename))
      wxMessageBox(_("Failed to save") + _T(": ") + filename,
                   _("OpenCPN Weather Routing Plugin"), wxICON_ERROR | wxOK);
  } else {
    wxString message;
    if (!m_Boat.Polars[i].Open(filename, message))
      wxMessageBox(
          _("Failed to revert") + _T(": ") + filename + _T("\n") + message,
          _("OpenCPN Weather Routing Plugin"), wxICON_ERROR | wxOK);
  }

  GenerateCrossOverChart();
  RefreshPlots();
}

void BoatDialog::OnAddPolar(wxCommandEvent& event) {
  wxFileConfig* pConf = GetOCPNConfigObject();
  pConf->SetPath(_T( "/PlugIns/WeatherRouting/BoatDialog" ));

  wxString path;
  pConf->Read(_T ( "PolarPath" ), &path,
              weather_routing_pi::StandardPath() + _T("polars"));

  wxFileDialog openDialog(
      this, _("Select Polar File"), path, wxT(""),
      wxT("CSV, POL, TXT (*.csv, *.pol, "
          "*.txt)|*.CSV;*.csv;*.csv.gz;*.csv.bz2;*.POL;*.pol;*.pol.gz;*.pol."
          "bz2;*.TXT;*.txt;*.txt.gz;*.txt.bz2|All files (*.*)|*.*"),
      wxFD_OPEN | wxFD_MULTIPLE);

  if (openDialog.ShowModal() != wxID_OK) return;

  pConf->Write(_T ( "PolarPath" ), openDialog.GetDirectory());

  wxArrayString paths;
  openDialog.GetPaths(paths);

  bool generate = false, existed = true;
  for (unsigned int i = 0; i < paths.GetCount(); i++) {
    wxString filename = paths[i], message;
    Polar polar;
    bool success;

    for (unsigned int j = 0; j < m_Boat.Polars.size(); j++)
      if (m_Boat.Polars[j].FileName == filename) goto skip;

    existed = wxFileName::Exists(filename);

    // write dummy file
    if (!existed) {
      wxFile file;
      if (file.Open(filename, wxFile::write)) file.Write(dummy_polar);
    }

    success = polar.Open(filename, message);
    if (success) {
      m_Boat.Polars.push_back(polar);
      RepopulatePolars();
      m_lPolars->SetItemState(m_Boat.Polars.size() - 1, wxLIST_STATE_SELECTED,
                              wxLIST_STATE_SELECTED);
      generate = true;
    }

    if (!message.IsEmpty()) {
      wxMessageDialog md(this, message, _("OpenCPN Weather Routing Plugin"),
                         success ? wxICON_WARNING : wxICON_ERROR | wxOK);
      md.ShowModal();
    }
  skip:;
  }

  if (generate) GenerateCrossOverChart();

  if (!existed) OnEditPolar(event);
}

void BoatDialog::OnRemovePolar(wxCommandEvent& event) {
  long index = -1, lastindex = -1, count = 0;

  while ((index = m_lPolars->GetNextItem(index, wxLIST_NEXT_ALL,
                                         wxLIST_STATE_SELECTED)) != -1) {
#ifndef __OCPN__ANDROID__
    m_Boat.Polars.erase(m_Boat.Polars.begin() + index - count++);
#endif
    lastindex = index;
  }

  if (lastindex == -1) return;

  RepopulatePolars();

  lastindex -= count;
  if (lastindex == (int)m_Boat.Polars.size()) lastindex--;

  m_lPolars->SetItemState(lastindex, wxLIST_STATE_SELECTED,
                          wxLIST_STATE_SELECTED);
  GenerateCrossOverChart();
  m_bRemovePolar->Enable(lastindex != -1);
}

static void status(void* arg, int p, int s) {
  if (s == 0) p = s = 1;

  wxThreadEvent event(wxEVT_THREAD, 100 * p / s);
  ((wxEvtHandler*)arg)->AddPendingEvent(event);
}

class CrossOverGenerationThread : public wxThread {
public:
  CrossOverGenerationThread(Boat& boat, BoatDialog& dlg)
      : wxThread(wxTHREAD_JOINABLE), m_Boat(boat), m_BoatDialog(dlg) {
    Create();
  }

  void* Entry() {
    m_Boat.GenerateCrossOverChart(&m_BoatDialog, status);
    return 0;
  }

  Boat m_Boat;
  BoatDialog& m_BoatDialog;
};

void BoatDialog::GenerateCrossOverChart() {
  if (m_CrossOverGenerationThread) {
    m_CrossOverRegenerate = true;  // regenerate again when done
    return;
  }

  m_gCrossOverChart->Enable();

  m_CrossOverGenerationThread = new CrossOverGenerationThread(m_Boat, *this);

  Connect(wxEVT_THREAD, (wxEventFunction)&BoatDialog::OnEvtThread);
  m_CrossOverGenerationThread->Run();
}

void BoatDialog::OnEvtThread(wxThreadEvent& event) {
  int id = event.GetId();
  m_gCrossOverChart->SetValue(id);
  if (id < 100) return;

  m_gCrossOverChart->Disable();

  m_CrossOverGenerationThread->Wait();
  Boat& tboat = m_CrossOverGenerationThread->m_Boat;
  for (unsigned int i = 0; i < m_Boat.Polars.size() && i < tboat.Polars.size();
       i++)
    m_Boat.Polars[i].CrossOverRegion = tboat.Polars[i].CrossOverRegion;
  delete m_CrossOverGenerationThread;
  m_CrossOverGenerationThread = NULL;
  RefreshPlots();

  if (m_CrossOverRegenerate) {
    m_CrossOverRegenerate = false;
    GenerateCrossOverChart();
  }
}

void BoatDialog::RepopulatePolars() {
  m_lPolars->DeleteAllItems();
#if 0
    if(m_Boat.Polars.size() == 0) {
        Polar generic_polar;
        wxString message, generic_polar_path = GetPluginDataDir("weather_routing_pi")
            + _T("plugins/weather_routing_pi/data/polars/60ft_mono.pol");
        bool success = generic_polar.Open(generic_polar_path, message);
        if(success)
            m_Boat.Polars.push_back(generic_polar);
        if(message.size())
            wxLogMessage(wxT("weather_routing_pi: ") + wxString(success ? _T("warning") : _T("error")) +
                         _T(" loading generic polar \"") + generic_polar_path + _T("\""));
    }
#endif
  for (unsigned int i = 0; i < m_Boat.Polars.size(); i++) {
    wxListItem info;
    info.SetId(i);
    info.SetData(i);
    long idx = m_lPolars->InsertItem(info);
    Polar& polar = m_Boat.Polars[i];
    m_lPolars->SetItem(idx, spFILENAME,
                       wxFileName(polar.FileName).GetFullName());
    m_lPolars->SetColumnWidth(spFILENAME, wxLIST_AUTOSIZE);
  }

  //        m_lPolars->SetColumnWidth(spFILENAME, wxLIST_AUTOSIZE);
  //    m_lPolars->SetColumnWidth(spFILENAME, 80);

  int enable = m_Boat.Polars.size();
  m_bRemovePolar->Enable(enable);
}

wxString BoatDialog::FormatVMG(double W, double VW) {
  long index = SelectedPolar();
  Polar& polar = m_Boat.Polars[index];
  if (std::isnan(W)) return _("wind speed out of range");
  PolarSpeedStatus error;
  double A = positive_degrees(
      Polar::DirectionApparentWind(polar.Speed(W, VW, &error, true), W, VW));
  wxString tr = _("True"), ap = _("Apparent");
  return wxString::Format("%.1f %s %.1f %s", W, tr, A, ap);
}

void BoatDialog::UpdateVMG() {
  long index = SelectedPolar();
  if (index < 0) return;

  int windspeed = m_sVMGWindSpeed->GetValue();
  Polar& polar = m_Boat.Polars[index];
  SailingVMG vmg = /*m_cVMGTrueApparent->GetSelection()*/ 0
                       ? polar.GetVMGApparentWind(windspeed)
                       : polar.GetVMGTrueWind(windspeed);

  // Update the original VMG display labels (for backward compatibility)
  m_stBestCourseUpWindPortTack->SetLabel(
      FormatVMG(vmg.values[SailingVMG::PORT_UPWIND], windspeed));
  m_stBestCourseUpWindStarboardTack->SetLabel(
      FormatVMG(vmg.values[SailingVMG::STARBOARD_UPWIND], windspeed));
  m_stBestCourseDownWindPortTack->SetLabel(
      FormatVMG(vmg.values[SailingVMG::PORT_DOWNWIND], windspeed));
  m_stBestCourseDownWindStarboardTack->SetLabel(
      FormatVMG(vmg.values[SailingVMG::STARBOARD_DOWNWIND], windspeed));

  // Update the new best VMG info panel
  UpdateBestVMGInfo();
}

void BoatDialog::UpdateCursorInfo() {
  if (!m_stCursorWindAngle) return;  // Panel not created yet

  if (m_CursorValid) {
    m_stCursorWindAngle->SetLabel(
        wxString::Format(_("%.1f°"), m_CursorWindAngle));
    m_stCursorWindSpeed->SetLabel(
        wxString::Format(_("%.1f kts"), m_CursorWindSpeed));
    m_stCursorBoatSpeed->SetLabel(
        wxString::Format(_("%.1f kts"), m_CursorBoatSpeed));
    m_stCursorVMG->SetLabel(wxString::Format(_("%.1f kts"), m_CursorVMG));
    if (m_stCursorVMGAngle) {
      // Show the actual cursor angle (course angle)
      m_stCursorVMGAngle->SetLabel(
          wxString::Format(_("%.1f°"), m_CursorWindAngle));
    }
  } else {
    m_stCursorWindAngle->SetLabel(_("N/A"));
    m_stCursorWindSpeed->SetLabel(_("N/A"));
    m_stCursorBoatSpeed->SetLabel(_("N/A"));
    m_stCursorVMG->SetLabel(_("N/A"));
    if (m_stCursorVMGAngle) {
      m_stCursorVMGAngle->SetLabel(_("N/A"));
    }
  }

  // Always update the best VMG information
  UpdateBestVMGInfo();
}

void BoatDialog::UpdateBestVMGInfo() {
  if (!m_stBestVMGWindSpeed) return;  // Panel not created yet

  long index = SelectedPolar();
  if (index < 0) {
    // No polar selected, clear all fields
    m_stBestVMGWindSpeed->SetLabel(_("N/A"));
    m_stBestVMGUpwindAngle->SetLabel(_("N/A"));
    m_stBestVMGUpwindSpeed->SetLabel(_("N/A"));
    m_stBestVMGUpwindVMG->SetLabel(_("N/A"));
    m_stBestVMGDownwindAngle->SetLabel(_("N/A"));
    m_stBestVMGDownwindSpeed->SetLabel(_("N/A"));
    m_stBestVMGDownwindVMG->SetLabel(_("N/A"));
    return;
  }

  // Determine wind speed to use for best VMG calculation
  double referenceWindSpeed;
  if (m_CursorValid && m_CursorWindSpeed > 0) {
    // Use wind speed from cursor position
    referenceWindSpeed = m_CursorWindSpeed;
  } else {
    // Use wind speed from VMG control
    referenceWindSpeed = m_sVMGWindSpeed->GetValue();
  }

  m_stBestVMGWindSpeed->SetLabel(
      wxString::Format(_("%.1f kts"), referenceWindSpeed));

  Polar& polar = m_Boat.Polars[index];
  SailingVMG vmg = polar.GetVMGTrueWind(referenceWindSpeed);

  // Upwind VMG (use starboard upwind as primary, port as backup)
  double upwindAngle = vmg.values[SailingVMG::STARBOARD_UPWIND];
  if (std::isnan(upwindAngle)) {
    upwindAngle = vmg.values[SailingVMG::PORT_UPWIND];
  }

  if (!std::isnan(upwindAngle)) {
    double upwindSpeed = polar.Speed(upwindAngle, referenceWindSpeed);
    double upwindVMG = upwindSpeed * cos(deg2rad(upwindAngle));

    m_stBestVMGUpwindAngle->SetLabel(wxString::Format(_("%.1f°"), upwindAngle));
    m_stBestVMGUpwindSpeed->SetLabel(
        wxString::Format(_("%.1f kts"), upwindSpeed));
    m_stBestVMGUpwindVMG->SetLabel(wxString::Format(_("%.1f kts"), upwindVMG));
  } else {
    m_stBestVMGUpwindAngle->SetLabel(_("N/A"));
    m_stBestVMGUpwindSpeed->SetLabel(_("N/A"));
    m_stBestVMGUpwindVMG->SetLabel(_("N/A"));
  }

  // Downwind VMG (use starboard downwind as primary, port as backup)
  double downwindAngle = vmg.values[SailingVMG::STARBOARD_DOWNWIND];
  if (std::isnan(downwindAngle)) {
    downwindAngle = vmg.values[SailingVMG::PORT_DOWNWIND];
  }

  if (!std::isnan(downwindAngle)) {
    double downwindSpeed = polar.Speed(downwindAngle, referenceWindSpeed);
    double downwindVMG = downwindSpeed * cos(deg2rad(180.0 - downwindAngle));

    m_stBestVMGDownwindAngle->SetLabel(
        wxString::Format(_("%.1f°"), downwindAngle));
    m_stBestVMGDownwindSpeed->SetLabel(
        wxString::Format(_("%.1f kts"), downwindSpeed));
    m_stBestVMGDownwindVMG->SetLabel(
        wxString::Format(_("%.1f kts"), downwindVMG));
  } else {
    m_stBestVMGDownwindAngle->SetLabel(_("N/A"));
    m_stBestVMGDownwindSpeed->SetLabel(_("N/A"));
    m_stBestVMGDownwindVMG->SetLabel(_("N/A"));
  }
}
