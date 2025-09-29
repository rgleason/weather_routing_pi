/***************************************************************************
 *
 * Project:  OpenCPN Weather Routing plugin
 * Author:   Sean D'Epagnier
 *
 ***************************************************************************
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
 ***************************************************************************
 */

#include <wx/wx.h>
#include <wx/filename.h>
#include <vector>

#include "tinyxml.h"

#include "weather_routing_pi.h"
#include "Utilities.h"
#include "Boat.h"

Boat::Boat() {}

Boat::~Boat() {}

wxString Boat::OpenXML(wxString filename, bool shortcut) {
  wxDateTime last_filetime = wxFileName(filename).GetModificationTime();
  /* shortcut if already loaded, and boat wasn't modified */
  if (shortcut && m_last_filename == filename && m_last_filetime.IsValid() &&
      m_last_filetime == last_filetime)
    return _T("");

  bool cleared = false;
  Polars.clear();

  if (!wxFileName::FileExists(filename)) return _("Boat file does not exist.");

  TiXmlDocument doc;
  if (!doc.LoadFile(filename.mb_str()))
    return _("Failed to load file: ") + filename + _T("\n") +
           _("It should an xml file describing a boat, not a polar; try "
             "editing the boat.");

  TiXmlHandle root(doc.RootElement());
  if (strcmp(doc.RootElement()->Value(), "OpenCPNWeatherRoutingBoat"))
    return _("Invalid xml file (no OCPWeatherRoutingBoat node): " + filename);

  bool generateContours = false;

  for (TiXmlElement* e = root.FirstChild().Element(); e;
       e = e->NextSiblingElement()) {
    if (!strcmp(e->Value(), "Polar")) {
      if (!cleared) {
        Polars.clear();
        cleared = true;
      }

      Polar polar;  //(wxString::FromUTF8(e->Attribute("Name")));

      polar.FileName = wxString::FromUTF8(e->Attribute("FileName"));
      polar.FileName.Replace(_T("/"), wxFileName::GetPathSeparator());
      if (!wxFileName::FileExists(polar.FileName))
        polar.FileName = weather_routing_pi::StandardPath() + _T("polars") +
                         wxFileName::GetPathSeparator() + polar.FileName;

      const char* str = e->Attribute("CrossOverContours");
      if (str) {
        wxFile contours(str);
        if (contours.IsOpened()) {
          int size = contours.Length();
          char* data = new char[size + 1];
          if (contours.Read(data, size) == size) {
            data[size] = 0;  // null terminate
            polar.CrossOverRegion = PolygonRegion(data);
          }
          delete[] data;
        }
      } else
        generateContours = true;

      // Load StandaloneRegion if available
      const char* standaloneStr = e->Attribute("StandaloneContours");
      if (standaloneStr) {
        wxFile standaloneContours(standaloneStr);
        if (standaloneContours.IsOpened()) {
          int size = standaloneContours.Length();
          char* data = new char[size + 1];
          if (standaloneContours.Read(data, size) == size) {
            data[size] = 0;  // null terminate
            polar.StandaloneRegion = PolygonRegion(data);
          }
          delete[] data;
        }
      } else
        generateContours = true;

      wxString message;
      if (!polar.Open(polar.FileName, message)) return message;

      polar.m_crossoverpercentage =
          AttributeDouble(e, "CrossOverPercentage", 0.0) / 100.0;

      Polars.push_back(polar);
    }
  }

  if (generateContours) {
    GenerateCrossOverChart();
    SaveXML(filename);
  }

  m_last_filename = filename;
  m_last_filetime = last_filetime;
  return _T("");
}

wxString Boat::SaveXML(wxString filename) {
  TiXmlDocument doc;
  TiXmlDeclaration* decl = new TiXmlDeclaration("1.0", "utf-8", "");
  doc.LinkEndChild(decl);
  TiXmlElement* root = new TiXmlElement("OpenCPNWeatherRoutingBoat");

  doc.LinkEndChild(root);
  char version[24];
  sprintf(version, "%d.%d", PLUGIN_VERSION_MAJOR, PLUGIN_VERSION_MINOR);
  root->SetAttribute("version", version);
  root->SetAttribute("creator", "Opencpn Weather Routing plugin");

  for (unsigned int i = 0; i < Polars.size(); i++) {
    TiXmlElement* e = new TiXmlElement("Polar");
    Polar& polar = Polars[i];
    //        e->SetAttribute("Name", polar.Name.mb_str());

    e->SetAttribute("FileName", polar.FileName.mb_str());
    if (!polar.CrossOverRegion.Empty()) {
      wxString ContoursPath = weather_routing_pi::StandardPath() +
                              _T("contours") + wxFileName::GetPathSeparator();
      if (!wxDirExists(ContoursPath)) wxMkdir(ContoursPath);

      wxString ContoursFileName = polar.FileName;
      ContoursFileName.Replace(wxFileName::GetPathSeparator(), _T("_"));
      if (!wxFileName::GetVolumeSeparator().IsEmpty())
        ContoursFileName.Replace(wxFileName::GetVolumeSeparator(), _T("_"));
      ContoursFileName = ContoursPath + ContoursFileName + _T(".contours");
      wxFile file;
      if (file.Open(ContoursFileName, wxFile::write)) {
        e->SetAttribute("CrossOverContours", ContoursFileName.mb_str());
        file.Write(polar.CrossOverRegion.toString());
      } else {
        delete e;
        return _("Failed to open for writing: ") + ContoursFileName;
      }
    }

    // Save StandaloneRegion if it exists
    if (!polar.StandaloneRegion.Empty()) {
      wxString ContoursPath = weather_routing_pi::StandardPath() +
                              _T("contours") + wxFileName::GetPathSeparator();
      if (!wxDirExists(ContoursPath)) wxMkdir(ContoursPath);

      wxString StandaloneFileName = polar.FileName;
      StandaloneFileName.Replace(wxFileName::GetPathSeparator(), _T("_"));
      if (!wxFileName::GetVolumeSeparator().IsEmpty())
        StandaloneFileName.Replace(wxFileName::GetVolumeSeparator(), _T("_"));
      StandaloneFileName =
          ContoursPath + StandaloneFileName + _T(".standalone");
      wxFile file;
      if (file.Open(StandaloneFileName, wxFile::write)) {
        e->SetAttribute("StandaloneContours", StandaloneFileName.mb_str());
        file.Write(polar.StandaloneRegion.toString());
      } else {
        delete e;
        return _("Failed to open for writing: ") + StandaloneFileName;
      }
    }

    e->SetDoubleAttribute("CrossOverPercentage",
                          100.0 * polar.m_crossoverpercentage);

    root->LinkEndChild(e);
  }

  if (!doc.SaveFile(filename.mb_str()))
    return _("Failed saving file: ") + filename;

  return wxString();
}

static Point FindNextSegmentPoint(std::list<Segment>& segments, Point p,
                                  bool greater) {
  Point best;
  bool nobest = true;
  for (std::list<Segment>::iterator it = segments.begin(); it != segments.end();
       it++)
    if (it->p[0].x == p.x)
      if ((greater && (it->p[0].y > p.y) && (nobest || it->p[0].y < best.y)) ||
          (!greater && (it->p[0].y < p.y) && (nobest || it->p[0].y > best.y))) {
        best = it->p[0];
        nobest = false;
      }
  if (nobest) wxLogMessage("warning, failed to find matching segment");
  return best;
}

bool Boat::PolarCanSail(int p, float H, float VW) {
  const float maxVW = 40;
  if (VW == 0 || VW == maxVW) return false;

  PolarSpeedStatus error;
  double speed = Polars[p].Speed(H, VW, &error, true);

  // Check if the polar has valid data for these conditions and produces
  // positive speed
  return (error == POLAR_SPEED_SUCCESS && speed > 0);
}

void Boat::GenerateStandaloneRegion(int p, void* arg,
                                    void (*status)(void*, int, int)) {
  const int maxVW = 40;
  const int stepi = 8;
  float step = 1.0f / stepi;

  bool buffer[2][maxVW * stepi + 1];
  int bi = 0;
  std::list<Segment> segments;

  for (float H = 0; H <= 180; H += step) {
    for (int VWi = 0; VWi <= maxVW * stepi; VWi++) {
      float VW = VWi * step;
      buffer[bi][VWi] = PolarCanSail(p, H, VW);

      if (VWi > 0 && H > 0) {
        bool q[4] = {buffer[!bi][VWi - 1], buffer[bi][VWi - 1],
                     buffer[!bi][VWi], buffer[bi][VWi]};
        GenerateSegments(H, VW, step, q, segments, p);
      }
    }
    bi = !bi;
  }

  /* insert wrapping segments for 0 and 180 */
  std::list<Segment> wrapped_segments;
  for (std::list<Segment>::iterator it = segments.begin(); it != segments.end();
       it++) {
    if (it->p[1].x == 0) {
      // find next segment starting at 0 with greater y
      wrapped_segments.push_back(
          Segment(it->p[1], FindNextSegmentPoint(segments, it->p[1], false)));
    } else if (it->p[1].x == 180) {
      // find next segment starting at 180 with lesser y
      wrapped_segments.push_back(
          Segment(it->p[1], FindNextSegmentPoint(segments, it->p[1], true)));
    }
  }

  segments.splice(segments.end(), wrapped_segments);
  Polars[p].StandaloneRegion = PolygonRegion(segments);
  Polars[p].StandaloneRegion.Simplify(1e-1f);
}

bool Boat::FastestPolar(int p, float H, float VW) {
  const float maxVW = 40;
  if (VW == 0 || VW == maxVW) return false;
  PolarSpeedStatus error;
  double speed = Polars[p].Speed(H, VW, &error, true) *
                 (1 + Polars[p].m_crossoverpercentage);
  for (int i = 0; i < (int)Polars.size(); i++) {
    if (i == p) continue;
    if (Polars[i].Speed(H, VW, &error, true) > speed) return false;
  }

  return speed > 0;
}

void Boat::GenerateCrossOverChart(void* arg, void (*status)(void*, int, int)) {
  const int maxVW = 40;
  const int stepi = 8;
  float step = 1.0f / stepi;

  for (int p = 0; p < (int)Polars.size(); p++) {
    if (status)
      status(arg, p * 2, Polars.size() * 2);  // Updated progress calculation

    // Generate CrossOver Region (optimal conditions)
    bool buffer[2][maxVW * stepi + 1];
    int bi = 0;
    std::list<Segment> segments;
    for (float H = 0; H <= 180; H += step) {
      for (int VWi = 0; VWi <= maxVW * stepi; VWi++) {
        float VW = VWi * step;
        buffer[bi][VWi] = FastestPolar(p, H, VW);

        if (VWi > 0 && H > 0) {
          bool q[4] = {buffer[!bi][VWi - 1], buffer[bi][VWi - 1],
                       buffer[!bi][VWi], buffer[bi][VWi]};
          GenerateSegments(H, VW, step, q, segments, p);
        }
      }
      bi = !bi;
    }

    /* insert wrapping segments for 0 and 180 */
    std::list<Segment> wrapped_segments;
    for (std::list<Segment>::iterator it = segments.begin();
         it != segments.end(); it++) {
      if (it->p[1].x == 0) {
        // find next segment starting at 0 with greater y
        wrapped_segments.push_back(
            Segment(it->p[1], FindNextSegmentPoint(segments, it->p[1], false)));
      } else if (it->p[1].x == 180) {
        // find next segment starting at 180 with lesser y
        wrapped_segments.push_back(
            Segment(it->p[1], FindNextSegmentPoint(segments, it->p[1], true)));
      }
    }

    segments.splice(segments.end(), wrapped_segments);
    Polars[p].CrossOverRegion = PolygonRegion(segments);
    Polars[p].CrossOverRegion.Simplify(1e-1f);

    if (status)
      status(arg, p * 2 + 1,
             Polars.size() * 2);  // Updated progress calculation

    // Generate Standalone Region (all operational conditions)
    GenerateStandaloneRegion(
        p, arg, nullptr);  // Don't pass status to avoid double callbacks
  }
  if (status)
    status(arg, Polars.size() * 2,
           Polars.size() * 2);  // Updated progress calculation
}

Point Boat::Interp(const Point& p0, const Point& p1, int q, bool q0, bool q1) {
  Point p01((p0.x + p1.x) / 2, (p0.y + p1.y) / 2);
  if (fabsf(p0.x - p1.x) < 1e-2 && fabsf(p0.y - p1.y) < 1e-2) return p01;

  if (q0 != q1) {
    bool q01 = FastestPolar(q, p01.x, p01.y);
    if (q01 == q0)
      return Interp(p01, p1, q, q01, q1);
    else
      return Interp(p0, p01, q, q0, q01);
  }

  return p0;
}

void Boat::NewSegment(Point& p0, Point& p1, std::list<Segment>& segments) {
  segments.push_back(Segment(p0, p1));
}

Point Intersection(Point a0, Point a1, Point b0, Point b1) {
  double ax = a1.x - a0.x, ay = a1.y - a0.y;
  double bx = b0.x - b1.x, by = b0.y - b1.y;
  double cx = a0.x - b0.x, cy = a0.y - b0.y;

  double denom = ay * bx - ax * by;
  double recip = 1 / denom;
  double na = (by * cx - bx * cy) * recip;

  return Point(a0.x + ax * na, a0.y + ay * na);
}
#define SIMPLE
void Boat::GenerateSegments(float H, float VW, float step, bool q[4],
                            std::list<Segment>& segments, int p) {
  float H0 = H - step;
  float VW0 = VW - step;

  Point p0(H0, VW0), p1(H, VW0), p2(H0, VW), p3(H, VW);

  /*        2 --- e --- 3
            |           |
            b     c     d
            |           |
            0 --- a --- 1         */

  //    Point c((H0+H)/2, (VW0+VW)/2);

  Point a[4];
  a[0] = Interp(p0, p1, p, q[0], q[1]);
  a[1] = Interp(p0, p2, p, q[0], q[2]);
  a[2] = Interp(p1, p3, p, q[1], q[3]);
  a[3] = Interp(p2, p3, p, q[2], q[3]);
  // Point c = Intersection(a[0], a[3], a[1], a[2]);
  if (q[0]) {
    if (q[1]) {
      if (q[2]) {
        if (!q[3]) {
#ifdef SIMPLE
          NewSegment(a[2], a[3], segments);
#else
          NewSegment(a[2], c, segments);
          NewSegment(c, a[3], segments);
#endif
        }
      } else {
        if (q[3]) {
#ifdef SIMPLE
          NewSegment(a[3], a[1], segments);
#else
          NewSegment(a[3], c, segments);
          NewSegment(c, a[1], segments);
#endif
        } else
          NewSegment(a[2], a[1], segments);
      }
    } else {
      if (q[2]) {
        if (q[3]) {
#ifdef SIMPLE
          NewSegment(a[0], a[2], segments);
#else
          NewSegment(a[0], c, segments);
          NewSegment(c, a[2], segments);
#endif
        } else
          NewSegment(a[0], a[3], segments);
      } else {
#ifdef SIMPLE
        NewSegment(a[0], a[1], segments);
#else
        NewSegment(a[0], c, segments);
        NewSegment(c, a[1], segments);
#endif
        if (q[3]) {
#ifdef SIMPLE
          NewSegment(a[3], a[2], segments);
#else
          NewSegment(a[3], c, segments);
          NewSegment(c, a[2], segments);
#endif
        }
      }
    }
  } else {
    if (q[1]) {
      if (q[2]) {
#ifdef SIMPLE
        NewSegment(a[1], a[0], segments);
#else
        NewSegment(a[1], c, segments);
        NewSegment(c, a[0], segments);
#endif
        if (!q[3]) {
#ifdef SIMPLE
          NewSegment(a[2], a[3], segments);
#else
          NewSegment(a[2], c, segments);
          NewSegment(c, a[3], segments);
#endif
        }
      } else {
        if (q[3])
          NewSegment(a[3], a[0], segments);
        else {
#ifdef SIMPLE
          NewSegment(a[2], a[0], segments);
#else
          NewSegment(a[2], c, segments);
          NewSegment(c, a[0], segments);
#endif
        }
      }
    } else {
      if (q[2]) {
        if (q[3])
          NewSegment(a[1], a[2], segments);
        else {
#ifdef SIMPLE
          NewSegment(a[1], a[3], segments);
#else
          NewSegment(a[1], c, segments);
          NewSegment(c, a[3], segments);
#endif
        }
      } else {
        if (q[3]) {
#ifdef SIMPLE
          NewSegment(a[3], a[2], segments);
#else
          NewSegment(a[3], c, segments);
          NewSegment(c, a[2], segments);
#endif
        }
      }
    }
  }
}

int Boat::FindBestPolarForCondition(int curpolar, double tws, double twa,
                                    double swell, bool optimize_tacking,
                                    PolarSpeedStatus* status) {
  // First, try with the current polar. If it's still valid, we can use it.
  if (curpolar >= 0 && Polars[curpolar].InsideCrossOverContour(
                           twa, tws, optimize_tacking, status))
    return curpolar;

  // The current polar must change; select the first polar we can use
  for (int i = 0; i < (int)Polars.size(); i++)
    if (i != curpolar &&
        Polars[i].InsideCrossOverContour(twa, tws, optimize_tacking, status))
      return i;

  // If we've reached here, no polar was found using standard checks.
  // However, this does not mean there is no sail configuration that can be
  // used. Examples of such cases are when the wind is too light or the angle is
  // too low or too high. In these cases, we try to find the best available
  // polar.

  PolarSpeedStatus initialStatus = status ? *status : POLAR_SPEED_SUCCESS;

  // Second attempt: Find the best compromise
  int bestPolar = -1;
  double bestScore = -1;

  // Second pass: find the best compromise based on the specific condition
  for (int i = 0; i < (int)Polars.size(); i++) {
    Polar& polar = Polars[i];

    // Skip polars with no data
    if (polar.degree_steps.empty() || polar.wind_speeds.empty()) continue;

    // Get min and max values for this polar
    double minWindSpeed = polar.wind_speeds[0].tws;
    double maxWindSpeed = polar.wind_speeds[polar.wind_speeds.size() - 1].tws;
    double minAngle = polar.degree_steps[0];
    double maxAngle = polar.degree_steps[polar.degree_steps.size() - 1];

    // Check if this polar is valid for specific conditions
    bool headingInRange = (twa >= minAngle && twa <= maxAngle);
    bool windInRange = (tws >= minWindSpeed && tws <= maxWindSpeed);

    // Calculate score based on the specific condition
    double score = 0.0;
    bool isCompatible = false;

    switch (initialStatus) {
      case POLAR_SPEED_WIND_TOO_LIGHT: {
        // Check if heading is in range for this polar
        if (!headingInRange) {
          continue;  // Skip polars that don't cover our heading
        }

        // Calculate score based on how close minimum wind is to our actual wind
        double windRatio = tws / minWindSpeed;
        score = windRatio;  // Higher score as we get closer to minimum wind

        // We're looking for polars with wind range closest to our current wind
        isCompatible = true;
        break;
      }

      case POLAR_SPEED_ANGLE_TOO_LOW: {
        // For upwind sailing (heading too close to wind)
        if (!windInRange) {
          continue;  // Skip polars that don't cover our wind speed
        }

        // Score based on how close this polar can get to the wind (lower min
        // angle is better)
        score =
            1.0 -
            (minAngle /
             45.0);  // Highest score for polars that can sail closest to wind

        // If we're trying to sail closer than this polar's minimum angle
        double angleDiff = std::abs(twa - minAngle);
        if (twa < minAngle) {
          // Extra points for polars whose min angle is closest to our desired
          // heading
          score += 0.5 * (1.0 - (angleDiff / 45.0));
        } else {
          // If our heading is within range, this is ideal
          score += 0.5;
        }

        isCompatible = true;
        break;
      }

      case POLAR_SPEED_ANGLE_TOO_HIGH: {
        // For downwind sailing (heading too far downwind)
        if (!windInRange) {
          continue;  // Skip polars that don't cover our wind speed
        }

        // Score based on how far downwind this polar can go (higher max angle
        // is better)
        score =
            maxAngle /
            180.0;  // Highest score for polars that can sail furthest downwind

        // If we're trying to sail wider than this polar's maximum angle
        double angleDiff = std::abs(twa - maxAngle);
        if (twa > maxAngle) {
          // Extra points for polars whose max angle is closest to our desired
          // heading
          score += 0.5 * (1.0 - (angleDiff / 45.0));
        } else {
          // If our heading is within range, this is ideal
          score += 0.5;
        }

        isCompatible = true;
        break;
      }

      default: {
        // For other cases, use standard contour check with specific status
        PolarSpeedStatus polarStatus;
        if (polar.InsideCrossOverContour(twa, tws, optimize_tacking,
                                         &polarStatus)) {
          isCompatible = true;
          score = 1.0;
        } else if (polarStatus == initialStatus) {
          // If polar fails for the same reason as our initial status,
          // it's at least compatible with the type of problem we're trying to
          // solve
          isCompatible = true;
          score = 0.5;
        }
        break;
      }
    }

    if (!isCompatible) continue;

    // Prefer the current polar if it's suitable (with a slight bonus)
    if (i == curpolar) {
      score *= 1.1;
    }

    // Select the polar with the best score
    if (score > bestScore) {
      bestScore = score;
      bestPolar = i;
    }
  }
  // Return the best compromise polar (or -1 if none found)
  return bestPolar;
}
