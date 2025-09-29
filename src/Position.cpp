/***************************************************************************
 *   Copyright (C) 2015 by OpenCPN development team                        *
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

#include "Position.h"
#include "RouteMap.h"
#include "Utilities.h"

#include "georef.h"
#include "ocpn_plugin.h"

/* sufficient for routemap uses only.. is this faster than below? if not, remove
 * it */
int ComputeQuadrantFast(Position* p, Position* q) {
  int quadrant;
  if (q->lat < p->lat)
    quadrant = 0;
  else
    quadrant = 2;

  if (p->lon < q->lon) quadrant++;

  return quadrant;
}

#if 0
  /* works for all ranges */
  static int ComputeQuadrant(Position *p, Position *q)
  {
      int quadrant;
      if(q->lat < p->lat)
          quadrant = 0;
      else
          quadrant = 2;
  
      double diff = p->lon - q->lon;
      while(diff < -180) diff += 360;
      while(diff >= 180) diff -= 360;
      
      if(diff < 0)
          quadrant++;
  
      return quadrant;
  }
#endif

#define EPSILON (2e-11)

Position::Position(double latitude, double longitude, Position* p,
                   double pheading, double pbearing, int polar_idx,
                   int tack_count, int jibe_count, int sail_plan_change_count,
                   DataMask data_mask, bool data_deficient)
    : RoutePoint(latitude, longitude, polar_idx, tack_count, jibe_count,
                 sail_plan_change_count, data_mask, data_deficient),
      parent_heading(pheading),
      parent_bearing(pbearing),
      parent(p),
      propagated(false),
      copied(false),
      propagation_error(PROPAGATION_NO_ERROR) {
  lat = EPSILON * std::round(lat / EPSILON);
  lon = EPSILON * std::round(lon / EPSILON);
}

Position::Position(const Position* p)
    : RoutePoint(p->lat, p->lon, p->polar, p->tacks, p->jibes,
                 p->sail_plan_changes, p->data_mask, p->grib_is_data_deficient),
      parent_heading(p->parent_heading),
      parent_bearing(p->parent_bearing),
      parent(p->parent),
      propagated(p->propagated),
      copied(true),
      propagation_error(p->propagation_error) {}

SkipPosition* Position::BuildSkipList() {
  /* build skip list of positions, skipping over strings of positions in
     the same quadrant */
  SkipPosition* skippoints = nullptr;
  Position* p = this;
  int firstquadrant, lastquadrant = -1, quadrant;
  do {
    Position* q = p->next;
    quadrant = ComputeQuadrantFast(p, q);

    if (lastquadrant == -1)
      firstquadrant = lastquadrant = quadrant;
    else if (quadrant != lastquadrant) {
      SkipPosition* rs = new SkipPosition(p, quadrant);
      if (skippoints) {
        rs->prev = skippoints->prev;
        rs->next = skippoints;
        skippoints->prev->next = rs;
        skippoints->prev = rs;
      } else {
        skippoints = rs;
        rs->prev = rs->next = rs;
      }
      lastquadrant = quadrant;
    }
    p = q;
  } while (p != this);

  if (!skippoints) {
    SkipPosition* rs = new SkipPosition(p, quadrant);
    rs->prev = rs->next = rs;
    skippoints = rs;
  } else if (quadrant != firstquadrant) {
    SkipPosition* rs = new SkipPosition(p, firstquadrant);

    rs->prev = skippoints->prev;
    rs->next = skippoints;
    skippoints->prev->next = rs;
    skippoints->prev = rs;

    skippoints = rs;
  }
  return skippoints;
}

void DeletePoints(Position* point) {
  Position* p = point;
  do {
    Position* dp = p;
    p = p->next;
    delete dp;
  } while (p != point);
}

/**
 * Performs a single Runge-Kutta integration step for route propagation.
 *
 * Takes a position and calculates the next position after a time step using
 * current winds and boat polars. This function is used as part of the
 * 4th order Runge-Kutta integration.
 *
 * @param p Current position
 * @param timeseconds Time step in seconds
 * @param cog Course over ground (degrees)
 * @param dist Distance to travel (nm)
 * @param twa True Wind Angle (degrees)
 * @param configuration Route configuration parameters
 * @param grib GRIB weather data
 * @param time Current time
 * @param newpolar Index of polar to use
 * @param rk_cog [out] New bearing over ground (degrees)
 * @param rk_dist [out] New distance traveled (nm)
 * @param data_mask [out] Mask indicating data sources used
 *
 * @return true if step was successful, false if step failed
 */
bool Position::rk_step(double timeseconds, double cog, double dist, double twa,
                       RouteMapConfiguration& configuration,
                       WR_GribRecordSet* grib, const wxDateTime& time,
                       int newpolar, double& rk_cog, double& rk_dist,
                       DataMask& data_mask) {
  double k1_lat, k1_lon;
  ll_gc_ll(lat, lon, cog, dist, &k1_lat, &k1_lon);

  WeatherData weather_data(this);
  Position rk(k1_lat, k1_lon,
              parent);  // parent so deficient data can find parent
  if (!weather_data.ReadWeatherDataAndCheckConstraints(
          configuration, &rk, data_mask, propagation_error, false /*end*/)) {
    return false;
  }
  double ctw =
      weather_data.twdOverWater + twa; /* rotated relative to true wind */

  BoatData boat_data;
  if (!boat_data.GetBoatSpeedForPolar(configuration, weather_data, timeseconds,
                                      newpolar, twa, ctw, data_mask,
                                      true /* check bounds */, "rk_step")) {
    return false;
  }
  rk_cog = boat_data.cog;
  rk_dist = boat_data.dist;
  return true;
}

/* propagate to the end position in the configuration, and return the number of
 * seconds it takes */
double Position::PropagateToEnd(RouteMapConfiguration& cf, double& H,
                                DataMask& data_mask) {
  return PropagateToPoint(cf.EndLat, cf.EndLon, cf, H, data_mask, true);
}

bool Position::Propagate(IsoRouteList& routelist,
                         RouteMapConfiguration& configuration) {
  /* already propagated from this position, don't need to again */
  if (propagated) {
    propagation_error = PROPAGATION_ALREADY_PROPAGATED;
    return false;
  }

  propagated = true;

  Position* points = nullptr;
  /* through all angles relative to wind */
  int count = 0;
  DataMask data_mask = DataMask::NONE;
  WeatherData weather_data(this);
  if (!weather_data.ReadWeatherDataAndCheckConstraints(
          configuration, this, data_mask, propagation_error, false /*end*/)) {
    return false;
  }

  bool first_avoid = true;
  Position* rp;

  double bearing1 = NAN, bearing2 = NAN;
  if (parent && configuration.MaxSearchAngle < 180) {
    bearing1 = heading_resolve(parent_bearing - configuration.MaxSearchAngle);
    bearing2 = heading_resolve(parent_bearing + configuration.MaxSearchAngle);
  }

  for (auto it = configuration.DegreeSteps.begin();
       it != configuration.DegreeSteps.end(); it++) {
    double timeseconds = configuration.UsedDeltaTime;
    double twa = heading_resolve(*it);
    double ctw =
        weather_data.twdOverWater + twa; /* rotated relative to true wind */

    // Do no waste time exploring directions outside the configured search
    // angle.
    if (!std::isnan(bearing1)) {
      double bearing3 = heading_resolve(ctw);
      if ((bearing1 > bearing2 && bearing3 > bearing2 && bearing3 < bearing1) ||
          (bearing1 < bearing2 &&
           (bearing3 > bearing2 || bearing3 < bearing1))) {
        if (first_avoid) {
          /* add a position behind the lines to ensure our route intersects
          with the previous one to nicely merge the resulting graph */
          first_avoid = false;
          rp = new Position(this);
          double dp = .95;
          rp->lat = (1 - dp) * lat + dp * parent->lat;
          rp->lon = (1 - dp) * lon + dp * parent->lon;
          rp->propagated =
              true;  // not a "real" position so we don't propagate it either.
          goto add_position;
        } else {
          continue;
        }
      }
    }

    {
      BoatData boat_data;
      int newpolar = -1;
      if (!boat_data.GetBestPolarAndBoatSpeed(
              configuration, weather_data, twa, ctw, parent_heading, data_mask,
              this->polar, newpolar, timeseconds)) {
        continue;
      }

      // {dlat, dlon} represent the destination coordinates for a route point
      // after propagation.
      double dlat, dlon;
      if (configuration.Integrator == RouteMapConfiguration::RUNGE_KUTTA) {
        double k2_dist, k2_BG, k3_dist, k3_BG, k4_dist, k4_BG;
        // a lot more experimentation is needed here, maybe use grib for the
        // right time??
        wxDateTime rk_time_2 =
            configuration.time + wxTimeSpan::Seconds(timeseconds / 2);
        wxDateTime rk_time =
            configuration.time + wxTimeSpan::Seconds(timeseconds);
        if (!rk_step(timeseconds, boat_data.cog, boat_data.dist / 2, twa,
                     configuration, configuration.grib, rk_time_2, newpolar,
                     k2_BG, k2_dist, data_mask) ||
            !rk_step(timeseconds, boat_data.cog, k2_dist / 2,
                     twa + k2_BG - boat_data.cog, configuration,
                     configuration.grib, rk_time_2, newpolar, k3_BG, k3_dist,
                     data_mask) ||
            !rk_step(timeseconds, boat_data.cog, k3_dist,
                     twa + k3_BG - boat_data.cog, configuration,
                     configuration.grib, rk_time, newpolar, k4_BG, k4_dist,
                     data_mask)) {
          continue;
        }

        ll_gc_ll(lat, lon, boat_data.cog,
                 boat_data.dist / 6 + k2_dist / 3 + k3_dist / 3 + k4_dist / 6,
                 &dlat, &dlon);
      } else /* newtons method */
#if 1
        ll_gc_ll(lat, lon, heading_resolve(boat_data.cog), boat_data.dist,
                 &dlat, &dlon);
#else
      {
        double d = boat_data.dist / 60;
        dlat = lat + d * cos(deg2rad(boat_data.cog));
        dlon = lon + d * sin(deg2rad(boat_data.cog));
        dlon = heading_resolve(dlon);
      }
#endif

      if (configuration.positive_longitudes && dlon < 0) dlon += 360;
      if (!ConstraintChecker::CheckMaxCourseAngleConstraint(configuration, dlat,
                                                            dlon)) {
        continue;
      }
      if (!ConstraintChecker::CheckMaxDivertedCourse(configuration, dlat,
                                                     dlon)) {
        continue;
      }

      /* quick test first to avoid slower calculation */
      if (!ConstraintChecker::CheckMaxApparentWindConstraint(
              configuration, boat_data.stw, twa, weather_data.twsOverWater,
              propagation_error)) {
        continue;
      }

      if (configuration.DetectLand || configuration.DetectBoundary) {
        double dlat1, dlon1;
        double bearing, dist2end;
        double dist2test;

        // it's not an error if there's boundaries after we reach destination
        ll_gc_ll_reverse(lat, lon, configuration.EndLat, configuration.EndLon,
                         &bearing, &dist2end);
        if (dist2end < boat_data.dist) {
          dist2test = dist2end;
          ll_gc_ll(lat, lon, heading_resolve(boat_data.cog), dist2test, &dlat1,
                   &dlon1);
        } else {
          dist2test = boat_data.dist;
          dlat1 = dlat;
          dlon1 = dlon;
        }

        /* landfall test */
        if (!ConstraintChecker::CheckLandConstraint(
                configuration, lat, lon, dlat1, dlon1, boat_data.cog)) {
          configuration.land_crossing = true;
          continue;
        }

        /* Boundary test */
        if (configuration.DetectBoundary) {
          if (EntersBoundary(dlat1, dlon1)) {
            configuration.boundary_crossing = true;
            continue;
          }
        }
      }
      /* crosses cyclone track(s)? */
      if (!ConstraintChecker::CheckCycloneTrackConstraint(configuration, lat,
                                                          lon, dlat, dlon)) {
        continue;
      }

      rp = new Position(dlat, dlon, this, twa, ctw, newpolar,
                        tacks + boat_data.tacked, jibes + boat_data.jibed,
                        sail_plan_changes + boat_data.sail_plan_changed,
                        data_mask, configuration.grib_is_data_deficient);
    }
  add_position:
    if (points) {
      rp->prev = points->prev;
      rp->next = points;
      points->prev->next = rp;
      points->prev = rp;
    } else {
      rp->prev = rp->next = rp;
      points = rp;
    }
    count++;
  }

  if (count < 3) { /* would get eliminated anyway, but save the extra steps */
    if (count) DeletePoints(points);
    propagation_error = PROPAGATION_ANGLE_ERROR;
    return false;
  }

  IsoRoute* nr = new IsoRoute(points->BuildSkipList());
  routelist.push_back(nr);
  return true;
}

double Position::Distance(const Position* p) const {
  return DistGreatCircle(lat, lon, p->lat, p->lon);
}

int Position::SailChanges() const {
  if (!parent) return 0;

  return (polar != parent->polar) + parent->SailChanges();
}

wxString Position::GetErrorText(PropagationError error) {
  static wxString error_texts[] = {_("No error"),
                                   _("Already propagated"),
                                   _("Exceeded maximum swell"),
                                   _("Exceeded maximum latitude"),
                                   _("Wind data retrieval failed"),
                                   _("Exceeded maximum wind speed"),
                                   _("Exceeded wind vs current threshold"),
                                   _("Angle outside search limits"),
                                   _("Polar constraints not met"),
                                   _("Boat speed computation failed"),
                                   _("Exceeded maximum apparent wind"),
                                   _("Land intersection detected"),
                                   _("Boundary intersection detected"),
                                   _("Cyclone track crossing detected"),
                                   _("No valid angles found")};

  if (error >= 0 && error <= PROPAGATION_ANGLE_ERROR)
    return error_texts[error];
  else
    return _("Unknown error");
}

void Position::ResetErrorTracking() {
  propagation_error = PROPAGATION_NO_ERROR;
}

wxString Position::GetErrorInfo() const {
  if (propagation_error == PROPAGATION_NO_ERROR) return wxEmptyString;

  return wxString::Format("%s", GetErrorText(propagation_error));
}

wxString Position::GetDetailedErrorInfo() const {
  wxString info;

  if (propagation_error != PROPAGATION_NO_ERROR) {
    wxString txt = _("Position propagation failed");
    info += wxString::Format("%s: %s\n", txt, GetErrorText(propagation_error));

    // Add position coordinates
    wxString ptxt = _("Position");
    info += wxString::Format("  %s: %.6f, %.6f\n", ptxt, lat, lon);

    // Add wind and current data if available
    if (parent) {
      wxString s1 = _("Heading from parent"), s2 = _("Bearing from parent");
      info += wxString::Format("  %s: %.1f\u00B0\n", s1, parent_heading);
      info += wxString::Format("  %s: %.1f\u00B0\n", s2, parent_bearing);
    }
  }

  return info;
}

SkipPosition::SkipPosition(Position* p, int q) : point(p), quadrant(q) {}

void SkipPosition::Remove() {
  prev->next = next;
  next->prev = prev;
  delete this;
}

/* copy a skip list along with it's position list to new lists */
SkipPosition* SkipPosition::Copy() {
  SkipPosition* s = this;
  if (!s) return s;

  SkipPosition *fs, *ns = nullptr;
  Position *fp, *np = nullptr;
  Position* p = s->point;
  do {
    Position* nsp = nullptr;
    do { /* copy all positions between skip positions */
      Position* nnp = new Position(p);
      if (!nsp) nsp = nnp;
      if (np) {
        np->next = nnp;
        nnp->prev = np;
        np = nnp;
      } else {
        fp = np = nnp;
        np->prev = np->next = np;
      }
      p = p->next;
    } while (p != s->next->point);

    SkipPosition* nns = new SkipPosition(nsp, s->quadrant);
    if (ns) {
      ns->next = nns;
      nns->prev = ns;
      ns = nns;
    } else {
      fs = ns = nns;
      ns->prev = ns->next = nns;
    }
    s = s->next;
  } while (s != this);

  fs->prev = ns;
  ns->next = fs;

  fp->prev = np;
  np->next = fp;
  return fs;
}