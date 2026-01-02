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
 ***************************************************************************/

#include <wx/wx.h>

#include <map>

#include "IsoRoute.h"
#include "Position.h"
#include "RouteMap.h"

void DeleteSkipPoints(SkipPosition* skippoints) {
  SkipPosition* s = skippoints;
  do {
    SkipPosition* ds = s;
    s = s->next;
    delete ds;
  } while (s != skippoints);
}


// Whenever an IsoRoute is deleted, remove all references to it from
// every IsoRouteList
// that may contain it. For example, after deleting route2 in Merge or
// Normalize, scan all lists and erase the pointer
void RemoveIsoRouteFromList(IsoRouteList& list, IsoRoute* route) {
  for (auto it = list.begin(); it != list.end();) {
    if (*it == route) {
      it = list.erase(it);
    } else {
      ++it;
    }
  }
}

/* find closest position in the routemap */
Position* IsoRoute::ClosestPosition(double lat, double lon, double* dist) {
  double mindist = INFINITY;

  /* first find closest skip position */
  SkipPosition* s = skippoints;
  Position* minpos = s->point;
#if 1
  do {
    Position* p = s->point;

    double dlat = lat - p->lat, dlon = lon - p->lon;
    double dist = dlat * dlat + dlon * dlon;

    if (dist < mindist) {
      minpos = p;
      mindist = dist;
    }

    const Position* q = s->next->point;
    switch (s->quadrant) {
      case 0:
        if ((lon > p->lon && lat > p->lat) || (lon < q->lon && lat < q->lat))
          goto skip;
        break;
      case 1:
        if ((lon < p->lon && lat > p->lat) || (lon > q->lon && lat < q->lat))
          goto skip;
        break;
      case 2:
        if ((lat < p->lat && lon > p->lon) || (lat > q->lat && lon < q->lon))
          goto skip;
        break;
      case 3:
        if ((lat < p->lat && lon < p->lon) || (lat > q->lat && lon > q->lon))
          goto skip;
        break;
    }

    {
      const Position* e = s->next->point;
      for (p = p->next; p != e; p = p->next) {
        double dlat = lat - p->lat, dlon = lon - p->lon;
        double dist = dlat * dlat + dlon * dlon;

        if (dist < mindist) {
          minpos = p;
          mindist = dist;
        }
      }
    }
  skip:
    s = s->next;
  } while (s != skippoints);

#else
  // this is a lot easier to understand but not as fast as above
  Position* p = s->point;
  do {
    double dlat = lat - p->lat, dlon = lon - p->lon;
    double dist = dlat * dlat + dlon * dlon;

    if (dist < mindist) {
      minpos = p;
      mindist = dist;
    }
    p = p->next;
  } while (p != s->point);
#endif

  /* now try children */
  for (IsoRouteList::iterator it = children.begin(); it != children.end();
       it++) {
    double dist;
    Position* p = (*it)->ClosestPosition(lat, lon, &dist);
    if (/*p &&*/ dist < mindist) {
      minpos = p;
      mindist = dist;
    }
  }

  if (dist) *dist = mindist;

  return minpos;
}

void IsoRoute::ResetDrawnFlag() {
  Position* pos = skippoints->point;
  do {
    pos->drawn = false;
    pos = pos->next;
  } while (pos != skippoints->point);

  for (IsoRouteList::iterator cit = children.begin(); cit != children.end();
       cit++)
    (*cit)->ResetDrawnFlag();
}

bool IsoRoute::Propagate(IsoRouteList& routelist,
                         RouteMapConfiguration& configuration,
                         bool& fatal_error) {
  Position* p = skippoints->point;
  bool ret = false;
  fatal_error = false;  // initialize the output parameter
  if (p) {
    do {
      bool local_fatal_error = false;
      if (p->Propagate(routelist, configuration, local_fatal_error)) {
        ret = true;
      }
      if (local_fatal_error) {
        fatal_error = true;  // propagate the error up
        break;
      }
      p = p->next;
    } while (p != skippoints->point);
  }
  return ret;
}

void IsoRoute::PropagateToEnd(RouteMapConfiguration& configuration,
                              double& mindt, Position*& endp, double& minH,
                              bool& mintacked, bool& minjibed,
                              bool& minsail_plan_changed,
                              DataMask& mindata_mask) {
  Position* p = skippoints->point;
  // TODO: it does not look like this function is used anywhere.
  // If it is used, one problem is that it does not check for the
  // case when there is a sailplan change.
  do {
    double H;
    DataMask data_mask = DataMask::NONE;
    double dt = p->PropagateToEnd(configuration, H, data_mask);

    /* did we tack thru the wind? apply penalty */
    bool tacked = false;
    if (!std::isnan(dt) && p->parent_heading * H < 0 &&
        fabs(p->parent_heading - H) < 180) {
      tacked = true;
      dt += configuration.TackingTime;
#if 0        
              if(configuration.MaxTacks >= 0 && p->tacks >= configuration.MaxTacks)
                  dt = NAN;
#endif
    }
    /* did we jibe? */
    bool jibed = false;
    if (!std::isnan(dt) && p->parent_heading * H > 0 &&
        fabs(p->parent_heading - H) > 180) {
      jibed = true;
      dt += configuration.JibingTime;
    }

    if (!std::isnan(dt) && dt < mindt) {
      mindt = dt;
      minH = H;
      endp = p;
      mintacked = tacked;
      minjibed = jibed;
      mindata_mask = data_mask;
    }
    p = p->next;
  } while (p != skippoints->point);

  for (IsoRouteList::iterator cit = children.begin(); cit != children.end();
       cit++)
    (*cit)->PropagateToEnd(configuration, mindt, endp, minH, mintacked,
                           minjibed, minsail_plan_changed, mindata_mask);
}

int IsoRoute::SkipCount() {
  SkipPosition* s = skippoints;
  int count = 0;
  if (s) do {
      count++;
      s = s->next;
    } while (s != skippoints);
  return count;
}

int IsoRoute::Count() {
  Position* p = skippoints->point;
  int count = 0;
  if (p) do {
      count++;
      p = p->next;
    } while (p != skippoints->point);
  return count;
}

void IsoRoute::UpdateStatistics(int& routes, int& invroutes, int& skippositions,
                                int& positions) {
  invroutes += children.size();
  routes += children.size() + 1;

  for (IsoRouteList::iterator it = children.begin(); it != children.end(); it++)
    skippositions += (*it)->SkipCount();
  skippositions += SkipCount();

  for (IsoRouteList::iterator it = children.begin(); it != children.end(); it++)
    positions += (*it)->Count();
  positions += Count();
}

IsoChron::~IsoChron() {
  for (IsoRouteList::iterator it = routes.begin(); it != routes.end(); ++it)
    delete *it;
}

void IsoChron::PropagateIntoList(IsoRouteList& routelist,
                                 RouteMapConfiguration& configuration) {
  for (IsoRouteList::iterator it = routes.begin(); it != routes.end(); ++it) {
    bool propagated = false;

    IsoRoute* x = nullptr;
    /* if anchoring is allowed, then we can propagate a second time,
       so copy the list before clearing the propagate flag,
       when depth data is implemented we will need to flag positions as
       propagated if they are too deep to anchor here. */
    if (configuration.Anchoring) x = new IsoRoute(*it);

    /* build up a list of iso regions for each point
       in the current iso */
    bool fatal_error = false;
    if ((*it)->Propagate(routelist, configuration, fatal_error))
      propagated = true;
    if (fatal_error) break;

    if (!configuration.Anchoring) x = new IsoRoute(*it);

    for (IsoRouteList::iterator cit = (*it)->children.begin();
         cit != (*it)->children.end(); cit++) {
      IsoRoute* y;
      if (configuration.Anchoring)
        y = new IsoRoute(*cit, x);
      else
        y = nullptr;
      if ((*cit)->Propagate(routelist, configuration, fatal_error)) {
        if (!configuration.Anchoring) y = new IsoRoute(*cit, x);
        x->children.push_back(y); /* copy child */
        propagated = true;
      } else {
        delete y;
      }
      if (fatal_error) break;
    }

    /* if any propagation occurred even for children, then we clone this route
       this prevents backtracking, otherwise, we don't need this route
       (it's a dead end) */
    if (propagated)
      routelist.push_front(x);  // slightly faster
    else
      delete x; /* didn't need it */    
  }
}

bool IsoChron::Contains(Position& p) {
  for (IsoRouteList::iterator it = routes.begin(); it != routes.end(); ++it)
    switch ((*it)->Contains(p, true)) {
      case -1:  // treat too close to call as not contained
      case 0:
        continue;
      default:
        return true;
    }
  return false;
}

bool IsoChron::Contains(double lat, double lon) {
  Position p(lat, lon);
  return Contains(p);
}

Position* IsoChron::ClosestPosition(double lat, double lon, wxDateTime* t,
                                    double* d) {
  Position* minpos = nullptr;
  double mindist = INFINITY;
  wxDateTime mint;
  for (IsoRouteList::iterator it = routes.begin(); it != routes.end(); ++it) {
    double dist;
    Position* pos = (*it)->ClosestPosition(lat, lon, &dist);
    if (pos && dist < mindist) {
      minpos = pos;
      mindist = dist;
      mint = time;
    }
  }
  if (d) *d = mindist;
  if (t) *t = mint;
  return minpos;
}

void IsoChron::ResetDrawnFlag() {
  for (IsoRouteList::iterator it = routes.begin(); it != routes.end(); ++it)
    (*it)->ResetDrawnFlag();
}

IsoRoute::IsoRoute(SkipPosition* s, int dir)
    : skippoints(s), direction(dir), parent(nullptr) {
  /* make sure the skip points start at the minimum
   latitude so we know we are on the outside */
  MinimizeLat();
}

/* DeepCopy constructor */
IsoRoute::IsoRoute(IsoRoute* r, IsoRoute* p)
    : skippoints(r->skippoints->DeepCopy()), direction(r->direction), parent(p) {}

IsoRoute::~IsoRoute() {

  for (IsoRouteList::iterator it = children.begin(); it != children.end(); ++it)
    delete *it;

  if (!skippoints) return;

  DeletePoints(skippoints->point);
  DeleteSkipPoints(skippoints);
}

void IsoRoute::Print() {
  if (!skippoints)
    printf("Empty IsoRoute\n");
  else {
    Position* p = skippoints->point;
    do {
      printf("%.10f %.10f\n", p->lon, p->lat);
      p = p->next;
    } while (p != skippoints->point);
    printf("\n");
  }
}

void IsoRoute::PrintSkip() {
  if (!skippoints)
    printf("Empty IsoRoute\n");
  else {
    SkipPosition* s = skippoints;
    do {
      printf("%.10f %.10f\n", s->point->lon, s->point->lat);
      s = s->next;
    } while (s != skippoints);
    printf("\n");
  }
}

void IsoRoute::MinimizeLat() {
  SkipPosition *min = skippoints, *cur = skippoints;
  do {
    if (cur->point->lat < min->point->lat) min = cur;
    cur = cur->next;
  } while (cur != skippoints);
  skippoints = min;
}

/* find intersection of two line segments
   if no intersection return 0, otherwise, 1 if the
   second line crosses from right to left, or -1 for left to right

   In the case that it is too close to determine, we find which endpoint
   is the problematic point (and will be deleted from the graph)
   -2: first point first seg
   -3: second point first seg
   2: first point second seg
   3: second point second seg

   Truth equations to calculate intersection (x, y)
   (y-y1) * (x2-x1) = (y2-y1) * (x-x1)
   (y-y3) * (x4-x3) = (y4-y3) * (x-x3)
*/
static inline int TestIntersectionXY(double x1, double y1, double x2, double y2,
                                     double x3, double y3, double x4,
                                     double y4) {
  double ax = x2 - x1, ay = y2 - y1;
  double bx = x3 - x4, by = y3 - y4;
  double cx = x1 - x3, cy = y1 - y3;

  double denom = ay * bx - ax * by;

#undef EPS
#undef EPS2
#define EPS 2e-16
#define EPS2 2e-8          // should be half the exponent of EPS
  if (fabs(denom) < EPS) { /* parallel or really close to parallel */
#define EPS3 1e-5
    if (fabs(ax) < EPS3 &&
        fabs(ay) < EPS3) /* first segment is a zero segment */
      return -2;

    if (fabs(bx) < EPS3 &&
        fabs(by) < EPS3) /* second segment is a zero segment */
      return 2;

/* we already know from initial test we are overlapping,
for parallel line segments, there is no way to tell
which direction the intersection occurs */
#define PEPS 2e-14
    if (fabs((y1 * ax - ay * x1) * bx - (y3 * bx - by * x3) * ax) > PEPS)
      return 0; /* different intercepts, no intersection */

    /* can invalidate a point on either segment for overlapping parallel,
    we will always choose second segment */
    double dx = x2 - x3, dy = y2 - y3;
    double da = ax * ax + bx * bx, db = cx * cx + cy * cy,
           dc = dx * dx + dy * dy;
    if (db <= da && dc <= da) /* point 3 is between 1 and 2 */
      return 2;
    return 3;
  }

  double recip = 1 / denom;
  double na = (by * cx - bx * cy) * recip;
  if (na < -EPS2 || na > 1 + EPS2) return 0;

  double nb = (ax * cy - ay * cx) * recip;
  if (nb < -EPS2 || nb > 1 + EPS2) return 0;

  /* too close to call.. floating point loses bits with arithmetic so
  in this case we must avoid potential false guesses */
  if (na < EPS2) return -2;
  if (na > 1 - EPS2) return -3;
  if (nb < EPS2) return 2;
  if (nb > 1 - EPS2) return 3;

  return denom < 0 ? -1 : 1;
}

/* how many times do we cross this route going from this point to infinity,
return -1 if inconclusive */
int IsoRoute::IntersectionCount(Position& pos) {
  int numintsct = 0;
  double lat = pos.lat, lon = pos.lon;

  SkipPosition* s1 = skippoints;

  double s1plon = s1->point->lon;
  int state1 = (lon < s1plon);
  do {
    SkipPosition* s2 = s1->next;
    double s2plon = s2->point->lon;
    int state0 = state1;
    state1 = (lon < s2plon);
    if (state0 != state1) {
      double s1plat = s1->point->lat, s2plat = s2->point->lat;
      int state = (lat < s1plat) + (lat < s2plat);

      switch (state) {
        case 1: /* must test every point in this case as point falls in
                   boundaries of skip list */
        {
          Position* p1 = s1->point;
          double p1lon = p1->lon;
          int pstate1 = lon < p1lon;
          do {
            Position* p2 = p1->next;
            double p2lon = p2->lon;
            int pstate0 = pstate1;
            pstate1 = lon < p2lon;

#if 1
            if (lon == p1lon && lon == p2lon)
              printf("degenerate case not handled properly\n");
#endif

            if (pstate0 != pstate1) {
              double p1lat = p1->lat, p2lat = p2->lat;
              state = (lat < p1lat) + (lat < p2lat);
              switch (state) {
                case 1: /* must perform exact intersection test */
                {
                  double p1lon = p1->lon;
#if 0
                        int dir = TestIntersectionXY(p1lon, p1lat, p2lon, p2lat, lon, lat, lon, 91);
                        switch(dir) {
                        case -2: case -3: case 2: case 3: return -1;
                        case 1: case -1: goto intersects;
                        }
#else
                  double m1 = (lat - p1lat) * (p2lon - p1lon);
                  double m2 = (lon - p1lon) * (p2lat - p1lat);

                  if (s1->quadrant & 1) {
                    if (m1 < m2) goto intersects;
                  } else if (m1 > m2)
                    goto intersects;
#endif
                } break;
                case 2: /* must intersect, we are below */
                  goto intersects;
              }
            }
            p1 = p2;
          } while (p1 != s2->point);
        } break;
        case 2: /* must intersect, we are below skip segment out of bounds */
        intersects:
          numintsct++;
      }
    }

    s1 = s2;
  } while (s1 != skippoints);

  return numintsct;
}

/* determine if a route contains a position
0 for outside, 1 for inside, -1 for inconclusive (on border or really close)
*/
int IsoRoute::Contains(Position& pos, bool test_children) {
  int numintsct = IntersectionCount(pos);
  if (numintsct == -1) return -1;

  if (test_children)
    for (IsoRouteList::iterator it = children.begin(); it != children.end();
         it++) {
      int cnumintsct = (*it)->Contains(pos, test_children);
      if (cnumintsct == -1) return -1;
      numintsct += cnumintsct;
    }

  return numintsct & 1; /* odd */
}

/* This function is very slow, and should probably be removed
or replaced with something else.. see how often it is called */
bool IsoRoute::CompletelyContained(IsoRoute* r) {
  Position* pos = r->skippoints->point;
  do {
    if (Contains(*pos, false) != 1) return false;
    pos = pos->next;
  } while (pos != r->skippoints->point);
  return true;
}

/* Determine if one route contains another,
only test first point, but if it fails try other points */
bool IsoRoute::ContainsRoute(IsoRoute* r) {
  Position* pos = r->skippoints->point;
  do {
    switch (Contains(*pos, false)) {
      case 0:
        return false;
      case 1:
        return true;
    }

    pos = pos->next;
  } while (pos !=
           r->skippoints
               ->point); /* avoid deadlock.. lets hope we dont do this often */
  //    printf("bad contains route\n");
  return true; /* probably good to say it is contained in this unlikely case */
}

/* remove points which are right next to eachother on the graph to speed
computation time */
void IsoRoute::ReduceClosePoints() {
  const double eps = 2e-5; /* resolution of 2 meters should be sufficient */
  Position* p = skippoints->point;
  while (p != skippoints->point->prev) {
    Position* n = p->next;
    double dlat = p->lat - n->lat, dlon = p->lon - n->lon;
    if (fabs(dlat) < eps && fabs(dlon) < eps) {
      p->next = n->next;
      n->next->prev = p;
      delete n;
    } else
      p = n;
  }

  DeleteSkipPoints(skippoints);
  skippoints = p->BuildSkipList();

  for (IsoRouteList::iterator it = children.begin(); it != children.end(); it++)
    (*it)->ReduceClosePoints();
}

/* apply current to given route, and return if it changed at all */
#if 0
bool IsoRoute::ApplyCurrents(GribRecordSet *grib, wxDateTime time, RouteMapConfiguration &configuration)
{
if(!skippoints)
    return false;

bool ret = false;
Position *p = skippoints->point;
double timeseconds = configuration.UsedDeltaTime;
do {
    double currentDir, currentSpeed;
    if(configuration.Currents && Current(grib, configuration.ClimatologyType,
                                         time, p->lat, p->lon, currentDir, currentSpeed)) {
        /* drift distance over ground */
        double dist = currentSpeed * timeseconds / 3600.0;
        if(dist)
            ret = true;
        ll_gc_ll(p->lat, p->lon, currentDir, dist, &p->lat, &p->lon);
    }

    p = p->next;
} while(p != skippoints->point);

/* if we moved we need to rebuild the skip list */
if(ret) {
    Position *points = skippoints->point;
    DeleteSkipPoints(skippoints);
    skippoints = points->BuildSkipList();
}

return ret;
}
#endif

enum { MINLON, MAXLON, MINLAT, MAXLAT };
/* return false if longitude is possibly invalid
could cache these bounds to avoid recomputing all the time */
void IsoRoute::FindIsoRouteBounds(double bounds[4]) {
  SkipPosition* maxlat = skippoints;
  Position* p = skippoints->point;
  bounds[MINLAT] = bounds[MAXLAT] = p->lat;
  bounds[MINLON] = bounds[MAXLON] = p->lon;

  SkipPosition* s = skippoints->next;
  while (s != skippoints) {
    p = s->point;
    bounds[MINLAT] = wxMin(p->lat, bounds[MINLAT]);
    bounds[MAXLAT] = wxMax(p->lat, bounds[MAXLAT]);
    bounds[MINLON] = wxMin(p->lon, bounds[MINLON]);
    bounds[MAXLON] = wxMax(p->lon, bounds[MAXLON]);

    if (p->lat == bounds[MAXLAT]) maxlat = s;
    s = s->next;
  }
  skippoints = maxlat; /* set to max lat for merging to keep outside */
}

#if 0
bool checkskiplist(SkipPosition* s) {
  /* build skip list of positions, skipping over strings of positions in
   the same quadrant */
  SkipPosition* skippoints = s;
  Position* p = s->point;
  do {
    do {
      Position* q = p->next;
      int quadrant = ComputeQuadrantFast(p, q);

      if (quadrant != s->quadrant) return false;
      p = q;
    } while (p != s->next->point);
    s = s->next;
  } while (s != skippoints);
  return true;
}
#endif

/* remove and delete a position given it's last skip position,
we need to update the skip list if this point falls on a skip position*/
void IsoRoute::RemovePosition(SkipPosition* s, Position* p) {
  p->next->prev = p->prev;
  p->prev->next = p->next;

  if (s->point == p) {
    if (s == s->next) {
      delete s;
      skippoints = nullptr;
    } else {
      /* rebuild skip list */
      Position* points = skippoints->point;
      if (p == points) points = points->next;
      DeleteSkipPoints(skippoints);
      skippoints = points->BuildSkipList();
      /* make sure the skip points start at the minimum
         latitude so we know we are on the outside */
      MinimizeLat();
    }
  }
  delete p;
}

inline void SwapSegments(Position* p, Position* q, Position* r, Position* s) {
  p->next = s;
  s->prev = p;
  r->next = q;
  q->prev = r;
}

inline void SwapSkipSegments(SkipPosition* sp, SkipPosition* sq,
                             SkipPosition* sr, SkipPosition* ss) {
  sp->next = ss;
  ss->prev = sp;
  sr->next = sq;
  sq->prev = sr;
}

inline void InsertSkipPosition(SkipPosition* sp, SkipPosition* sn, Position* p,
                               int quadrant) {
  SkipPosition* s = new SkipPosition(p, quadrant);
  s->prev = sp;
  sp->next = s;
  s->next = sn;
  sn->prev = s;
}

/* given positions p and s in skip list between sp and ss, fix stuff adding
removing or shifting skip positions to make things valid after this merge */
/*inline*/ void FixSkipList(SkipPosition* sp, SkipPosition* ss, Position* p,
                            Position* s, int rquadrant, SkipPosition*& spend,
                            SkipPosition*& ssend) {
  int quadrant = ComputeQuadrantFast(p, s);
  if (sp->point == p) {
    sp->quadrant = quadrant; /* reuse p with this quadrant */

    if (quadrant == sp->prev->quadrant && sp != ss) {
      sp->point = sp->prev->point;
      if (sp->prev == spend) spend = sp;
      if (sp->prev == ssend) ssend = sp;
      if (ss == sp->prev) {
        if (ssend == ss) ssend = sp;
        ss = sp;
      }
      sp->prev->Remove();
    }
    /* DUPLICATE START */
    if (quadrant == rquadrant) {
      if (rquadrant == ss->quadrant) goto remove;
    } else if (ss->point == s) {
      if (quadrant == ss->quadrant) goto remove;
    } else {
      if (rquadrant == ss->quadrant)
        ss->point = s; /* shift ss to s */
      else
        InsertSkipPosition(sp, ss, s, rquadrant);
    }
    /* DUPLICATE END */
  } else if (sp->quadrant == quadrant) {
    if (quadrant ==
        rquadrant) { /* this is never hit..  can we remove this test? */
      if (rquadrant == ss->quadrant) goto remove;
    } else if (ss->point == s) {
      if (quadrant == ss->quadrant) {
      remove:
        if (sp == ss) printf("sp == ss.. this is bad\n");
        if (ss == spend) spend = ss->next;
        if (ss == ssend) ssend = ss->next;
        ss->Remove();
      }
    } else {
      if (rquadrant == ss->quadrant)
        ss->point = s; /* shift ss to s */
      else
        InsertSkipPosition(sp, ss, s, rquadrant);
    }
  } else {
    if (quadrant == rquadrant) {
      if (rquadrant == ss->quadrant)
        ss->point = p; /* shift ss to p */
      else
        InsertSkipPosition(sp, ss, p, quadrant);
    } else if (ss->point == s) {
      if (quadrant == ss->quadrant)
        ss->point = p; /* shift ss to p */
      else
        InsertSkipPosition(sp, ss, p, quadrant);
    } else {
      InsertSkipPosition(sp, ss, p, quadrant);
      if (rquadrant == ss->quadrant)
        ss->point = s; /* shift ss to s */
      else
        InsertSkipPosition(sp->next, ss, s, rquadrant);
    }
  }
}

bool UpdateEnd(SkipPosition* spend, SkipPosition* sr) {
  SkipPosition* nsr = sr;
  do {
    if (nsr == spend) return true;
    nsr = nsr->next;
  } while (nsr != sr);
  return false;
}

#define COMPUTE_MIN_MAX(quadrant, A, B, N) \
  switch (quadrant) {                      \
    default:                               \
      min##N##x = B##x;                    \
      max##N##x = A##x;                    \
      min##N##y = B##y, max##N##y = A##y;  \
      break;                               \
    case 1:                                \
      min##N##x = A##x;                    \
      max##N##x = B##x;                    \
      min##N##y = B##y, max##N##y = A##y;  \
      break;                               \
    case 2:                                \
      min##N##x = B##x;                    \
      max##N##x = A##x;                    \
      min##N##y = A##y, max##N##y = B##y;  \
      break;                               \
    case 3:                                \
      min##N##x = A##x;                    \
      max##N##x = B##x;                    \
      min##N##y = A##y, max##N##y = B##y;  \
      break;                               \
  }

#define COMPUTE_STATE(state, S, N)    \
  state = 0;                          \
  if (S##x >= min##N##x) state += 4;  \
  if (S##x > max##N##x) state += 4;   \
  if (S##y >= min##N##y) state += 12; \
  if (S##y > max##N##y) state += 12;

/* 0 1    0  4  8
2 3   12 16 20
     24 28 32 */
#define UPDATE_STATE(state, quadrant, skip, S, N)     \
  switch (state + quadrant) {                         \
    case 1:                                           \
      if (S##x >= min##N##x) {                        \
        skip##c1 : if (S##x > max##N##x) state = 8;   \
        else state = 4;                               \
      } /*f*/                                         \
    case 0:                                           \
      goto skip;                                      \
    case 3:                                           \
      if (S##x >= min##N##x) {                        \
        if (S##y >= min##N##y) break;                 \
        goto skip##c1;                                \
      } /*f*/                                         \
    case 2:                                           \
      if (S##y >= min##N##y) {                        \
        if (S##y > max##N##y)                         \
          state = 24;                                 \
        else                                          \
          state = 12;                                 \
      }                                               \
      goto skip;                                      \
                                                      \
    case 6:                                           \
      if (S##y >= min##N##y) break; /*f*/             \
    case 4:                                           \
      if (S##x < min##N##x) state = 0;                \
      goto skip;                                      \
    case 7:                                           \
      if (S##y >= min##N##y) break; /*f*/             \
    case 5:                                           \
      if (S##x > max##N##x) state = 8;                \
      goto skip;                                      \
                                                      \
    case 8:                                           \
      if (S##x <= max##N##x) {                        \
        skip##c8 : if (S##x < min##N##x) state = 0;   \
        else state = 4;                               \
      } /*f*/                                         \
    case 9:                                           \
      goto skip;                                      \
    case 10:                                          \
      if (S##x <= max##N##x) {                        \
        if (S##y >= min##N##y) break;                 \
        goto skip##c8;                                \
      } /*f*/                                         \
    case 11:                                          \
      if (S##y >= min##N##y) {                        \
        if (S##y > max##N##y)                         \
          state = 32;                                 \
        else                                          \
          state = 20;                                 \
      }                                               \
      goto skip;                                      \
                                                      \
    case 13:                                          \
      if (S##x >= min##N##x) break; /*f*/             \
    case 12:                                          \
      if (S##y < min##N##y) state = 0;                \
      goto skip;                                      \
    case 15:                                          \
      if (S##x >= min##N##x) break; /*f*/             \
    case 14:                                          \
      if (S##y > max##N##y) state = 24;               \
      goto skip;                                      \
      /* 16-19 fall through */                        \
    case 20:                                          \
      if (S##x <= max##N##x) break; /*f*/             \
    case 21:                                          \
      if (S##y < min##N##y) state = 8;                \
      goto skip;                                      \
    case 22:                                          \
      if (S##x <= max##N##x) break; /*f*/             \
    case 23:                                          \
      if (S##y > max##N##y) state = 32;               \
      goto skip;                                      \
                                                      \
    case 25:                                          \
      if (S##x >= min##N##x) {                        \
        if (S##y <= max##N##y) break;                 \
        goto skip##c27;                               \
      } /*f*/                                         \
    case 24:                                          \
      if (S##y <= max##N##y) {                        \
        if (S##y < min##N##y)                         \
          state = 0;                                  \
        else                                          \
          state = 12;                                 \
      }                                               \
      goto skip;                                      \
    case 27:                                          \
      if (S##x >= min##N##x) {                        \
        skip##c27 : if (S##x > max##N##x) state = 32; \
        else state = 28;                              \
      } /*f*/                                         \
    case 26:                                          \
      goto skip;                                      \
                                                      \
    case 28:                                          \
      if (S##y <= max##N##y) break; /*f*/             \
    case 30:                                          \
      if (S##x < min##N##x) state = 24;               \
      goto skip;                                      \
    case 29:                                          \
      if (S##y <= max##N##y) break; /*f*/             \
    case 31:                                          \
      if (S##x > max##N##x) state = 32;               \
      goto skip;                                      \
                                                      \
    case 32:                                          \
      if (S##x <= max##N##x) {                        \
        if (S##y <= max##N##y) break;                 \
        goto skip##c34;                               \
      } /*f*/                                         \
    case 33:                                          \
      if (S##y <= max##N##y) {                        \
        if (S##y < min##N##y)                         \
          state = 8;                                  \
        else                                          \
          state = 20;                                 \
      }                                               \
      goto skip;                                      \
    case 34:                                          \
      if (S##x <= max##N##x) {                        \
        skip##c34 : if (S##x < min##N##x) state = 24; \
        else state = 28;                              \
      } /*f*/                                         \
    case 35:                                          \
      goto skip;                                      \
  }

/* This function is the heart of the route map algorithm.
Essentially search for intersecting line segments, and flip them correctly
while maintaining a skip list.
*/
bool Normalize(IsoRouteList& rl, IsoRoute* route1, IsoRoute* route2, int level,
               bool inverted_regions) {
  bool normalizing;

reset:
  SkipPosition *spend = route1->skippoints, *ssend = route2->skippoints;

  if (!spend || spend->prev == spend->next) { /* less than 3 items */
    delete route1;
    // Remove route1 from all lists that may contain it
    RemoveIsoRouteFromList(rl, route1);
    RemoveIsoRouteFromList(route1->children, route1);
    if (route1 != route2) rl.push_back(route2);
    return true;
  }

  if (route1 == route2) {
    normalizing = true;
  } else {
    if (!ssend || ssend->prev == ssend->next) { /* less than 3 items */
      delete route2;
      if (spend) rl.push_back(route1);
      return true;
    }

    normalizing = false;
  }

  SkipPosition* sp = spend;
startnormalizing:
  do {
    SkipPosition* sq = sp->next;
    SkipPosition *sr, *ss;
    if (normalizing)
      ss = sp;
    else
      ss = ssend;

    Position *p = sp->point, *q = sq->point;
    double px = p->lon, qx = q->lon, py = p->lat, qy = q->lat;

    double minx, maxx, miny, maxy;
    COMPUTE_MIN_MAX(sp->quadrant, p, q, )

    Position *r, *s = ss->point;

    int dir;
    double rx, ry;
    double sx = s->lon, sy = s->lat;

    int state, rstate, pstate;
    COMPUTE_STATE(state, s, )

    int nr;
    Position *pstart, *pend, *rstart, *rend;

    do {
      sr = ss;
      ss = sr->next;

      s = ss->point;
      sx = s->lon, sy = s->lat;

      UPDATE_STATE(state, sr->quadrant, skip, s, )

      nr = 0;
      if (normalizing) {
        if (sp == sr) {
          nr = 1; /* only occurs during normalizing (first round) */
          /* normalizing and overlapping round.. don't bother to calculate
           * smaller bounds */
          pstart = sp->point;
          pend = sq->point;

          rstart = sr->point;
          rend = ss->point;
          goto skip_bounds_compute;
        } else if (sq == sr)
          nr = 2; /* only occurs normalizing (second round) */
        else if (ss == sp)
          nr = 3; /* only occurs normalizing (last round) */
      }

#if 1 /* this is only slightly faster, barely can measure a difference */
      /* compute bounds for these skip segments */
      double minrx, maxrx, minry, maxry;
      rx = sr->point->lon, ry = sr->point->lat;
      COMPUTE_MIN_MAX(sr->quadrant, r, s, r)

      pstart = pend = nullptr;
      q = sp->point;
      qx = q->lon, qy = q->lat;
      COMPUTE_STATE(pstate, q, r)
      do {
        p = q;
        q = q->next;
        qx = q->lon, qy = q->lat;
        UPDATE_STATE(pstate, sp->quadrant, skipp, q, r)
        if (!pstart) pstart = p;
        pend = q;
        COMPUTE_STATE(pstate, q, r)
        goto startingp;
      skipp:
        if (pstart) break; /* have start, must be done */
      startingp:;
      } while (q != sq->point);
      p = pstart;
      if (!pstart) goto done;
      //    if(pstart == pend)  // this is never hit in practice
      //      goto done;

      rstart = rend = nullptr;
      s = sr->point;
      rstate = state; /* still valid from before */
      do {
        r = s;
        s = s->next;
        sx = s->lon, sy = s->lat;
        UPDATE_STATE(rstate, sr->quadrant, skipr, s, )
        if (!rstart) rstart = r;
        rend = s;
        COMPUTE_STATE(rstate, s, )
        goto startingr;
      skipr:
        if (rstart) break; /* have start, must be done */
      startingr:;
      } while (s != ss->point);

      if (!rstart) goto done;
#else
      pstart = sp->point;
      pend = sq->point;

      rstart = sr->point;
      rend = ss->point;
#endif
    skip_bounds_compute:

      p = pstart;
      do {
        q = p->next;

        switch (nr) {
          case 1:
            s = q;
            if (s == rend) goto done;
            s = s->next;
            break;
          case 2:
            s = rstart;
            if (s == q) s = s->next;
            break;
          case 3:
            s = rstart;
            if (rend == p) rend = rend->prev;
            break;
          default:
            s = rstart;
        }

        if (s == rend) goto done;

        px = p->lon, py = p->lat;
        qx = q->lon, qy = q->lat;

        double minpqx, maxpqx, minpqy, maxpqy;
        COMPUTE_MIN_MAX(sp->quadrant, p, q, pq)

        sx = s->lon, sy = s->lat;
        COMPUTE_STATE(state, s, pq);
        do {
          r = s;
          s = r->next;

          sx = s->lon, sy = s->lat;
          UPDATE_STATE(state, sr->quadrant, skippr, s, pq);

          rx = r->lon, ry = r->lat;
          dir = TestIntersectionXY(px, py, qx, qy, rx, ry, sx, sy);
          switch (dir) {
            case -2:
              route1->skippoints = spend, route2->skippoints = ssend;
              route1->RemovePosition(sp, p);
              goto reset;
            case -3:
              route1->skippoints = spend, route2->skippoints = ssend;
              route1->RemovePosition(sq, q);
              goto reset;
            case 2:
              route1->skippoints = spend, route2->skippoints = ssend;
              route2->RemovePosition(sr, r);
              goto reset;
            case 3:
              route1->skippoints = spend, route2->skippoints = ssend;
              route2->RemovePosition(ss, s);
              goto reset;
            case -1:
            case 1:
              if (!normalizing) { /* sanity check for merging */
                if (dir == -1) {
                  if (route1->direction != 1 || route2->direction != -1)
                    /* we intersected at the wrong side, skip this intersection
                       and continue to find the intersection we want,  this
                       occurs when a line segment passes completely through a
                       region. We could possibly merge here anyway but the
                       result would be less correct.  */
                    goto skipmerge;
                } else
                  /* inverted invalid test */
                  if (route1->direction == 1 && route2->direction == -1)
                    goto skipmerge;
              } else {
                if (level == 0 && dir == -1 && route1->direction == 1)
                  goto skipmerge;
              }

              SwapSegments(p, q, r, s);         /* update position list */
              SwapSkipSegments(sp, sq, sr, ss); /* update skip lists */

              /* now update skip list properly */
              Position* orig_sppoint = sp->point;
              if (sp->quadrant != sr->quadrant) {
                int rquadrant = sr->quadrant, pquadrant = sp->quadrant;
                FixSkipList(sp, ss, p, s, rquadrant, spend, ssend);
                FixSkipList(sr, sq, r, q, pquadrant, spend, ssend);
              }

              if (normalizing) {
                /* did the end end up in the subroute? move it back out */
                if (UpdateEnd(spend, sr)) spend = sp->next;
                if (UpdateEnd(ssend, sr)) ssend = sp->next;

                if (level == 0) {
                  /* slight numerical error, or outer inversion */
                  if (dir != route1->direction || sr->next->next == sr) {
                    DeletePoints(r);
                    DeleteSkipPoints(sr);
                  } else {
                    IsoRoute* x = new IsoRoute(sr, dir);
                    IsoRouteList sub;
                    Normalize(sub, x, x, level + 1, inverted_regions);
                    if (inverted_regions) {
                      for (IsoRouteList::iterator it = sub.begin();
                           it != sub.end(); ++it) {
                        if (!(*it)->children.empty()) {
                          printf("grandchild detected\n");
                          delete *it;
                        } else if (route1->direction == (*it)->direction) {
                          rl.push_back(*it); /* sibling */
                        } else if ((*it)->Count() < 24) {
                          //                      printf("too small to be a
                          //                      useful child: %d\n",
                          //                      (*it)->Count());
                          delete *it;
                        } else if (!(route1->skippoints = spend,
                                     route1->CompletelyContained(*it))) {
                          //                      printf("not correct to be
                          //                      child: %d\n", (*it)->Count());
                          delete *it;
                        } else { /* different direction contained.. it is a
                                    child */
                          /* we should merge it with the other children here */
                          //                      printf("Child route: %d\n",
                          //                      (*it)->Count());
                          IsoRoute* child = *it;
                          child->parent = route1;
                          route1->children.push_back(child);
                        }
                      }
                    } else { // no inverted regions mode
                      for (IsoRouteList::iterator it = sub.begin();
                           it != sub.end(); ++it) {
                        if (route1->direction == (*it)->direction) {
                          rl.push_back(*it); /* sibling */
                        } else
                          delete *it; /* inversion */
                      }
                    }
                  }
                } else { /* all subregions are siblings for inner levels */

                  if (sr->next->next ==
                      sr) { /* slight numerical error, or outer inversion */
                    DeletePoints(r);
                    DeleteSkipPoints(sr);
                  } else {
                    IsoRoute* x = new IsoRoute(sr, dir);
                    IsoRouteList sub;
                    Normalize(sub, x, x, level + 1, inverted_regions);
                    rl.splice(rl.end(), sub);
                  }
                }
              } else { /* merging */
                for (IsoRouteList::iterator it = route2->children.begin();
                     it != route2->children.end(); it++)
                  (*it)->parent = route1;

                /* merge children (append is currently incorrect)
                   the children need to be merged, and any overlapping regions
                   incremented so they don't get removed if contained */
                int sc1 = route1->children.size();
                int sc2 = route2->children.size();
                if (sc1 && sc2) printf("both have children: %d %d\n", sc1, sc2);

                route1->children.splice(route1->children.end(),
                                        route2->children);
                route2->skippoints = nullptr; /* all points are now in route1 */
                delete route2;
                // Remove route1 from all lists that may contain it
                RemoveIsoRouteFromList(rl, route2);
                RemoveIsoRouteFromList(route1->children, route2);
                route2 = route1;
                ssend = spend;
                spend = sr->next; /* after old sq we are done.. this is known */
                /* continue from here and begin to normalize */
#if 0 /* these only needed if we could jump back in too a more optimal spot \
     than startnormalizing */
        /*  could in theory somehow skip to p for this round instead of starting
            at sp->point.. but I doubt it would speed things up that much. */
        sr = sp, ss = sr->next;
        pend = rend = ss->point;
#endif
                normalizing = true;
              }

              if (sp->point != orig_sppoint) {
                /* it is possible we are no longer on the outside
                   because of the skip list getting contracted
                   so we must minimize the latitude at the start of the skiplist
                 */
                route1->skippoints = sp;
                route1->MinimizeLat();
                goto reset;
              }

              goto startnormalizing;
          }
        skipmerge:
          COMPUTE_STATE(state, s, pq);
        skippr:;
        } while (s != rend);
        p = q;
      } while (p != pend);
    done:
      COMPUTE_STATE(state, s, )
    skip:;
    } while (ss != ssend);
    sp = sq;
  } while (sp != spend);

  if (normalizing) {
    route1->skippoints = spend;

    /* make sure the skip points start at the minimum
       latitude so we know we are on the outside */
    route1->MinimizeLat();
    rl.push_back(route1);
    return true;
  }
  return false;
}

/* take two routes that may overlap, and combine into a list of non-overlapping
 * routes */
bool Merge(IsoRouteList& rl, IsoRoute* route1, IsoRoute* route2, int level,
           bool inverted_regions) {
  if (route1->direction == -1 && route2->direction == -1) {
    printf("cannot merge two inverted routes\n");
    exit(1);
  }

  /* quick test to make sure we could possibly intersect with bounds */
  double bounds1[4], bounds2[4];
  route1->FindIsoRouteBounds(bounds1);
  route2->FindIsoRouteBounds(bounds2);
  if (bounds1[MINLAT] > bounds2[MAXLAT] || bounds1[MAXLAT] < bounds2[MINLAT] ||
      bounds1[MINLON] > bounds2[MAXLON] || bounds1[MAXLON] < bounds2[MINLON])
    return false;

  /* make sure route1 is on the outside */
  if (route2->skippoints->point->lat > route1->skippoints->point->lat) {
    IsoRoute* t = route1;
    route1 = route2;
    route2 = t;
  }

  if (Normalize(rl, route1, route2, level, inverted_regions)) return true;

  /* no intersection found, test if the second route is completely
   inside the first */
  if (route1->ContainsRoute(route2)) {
    if (inverted_regions) {
      if (route1->direction == 1 && route2->direction == 1) {
        /* if both region have children, they should get merged
           correctly here instead of this */
        // int sc1 = route1->children.size();
        // int sc2 = route2->children.size();
        // if(sc1 && sc2)
        // printf("both have children contains: %d %d\n", sc1, sc2);

        /* remove all of route2's children, route1 clears them
           (unless they interected with route1 children which we don't handle
           yet */
        for (IsoRouteList::iterator it2 = route2->children.begin();
             it2 != route2->children.end(); it2++)
          delete *it2;
          route2->children.clear();
        /* now determine if route2 affects any of route1's children,
           if there are any intersections, it should mask away that area.
           once completely merged, all the masks are removed and children
           remain */
        IsoRouteList childrenmask;   /* non-inverted */
        IsoRouteList mergedchildren; /* inverted */
        childrenmask.push_back(route2);
        while (!childrenmask.empty()) {
          IsoRoute* r1 = childrenmask.front();
          childrenmask.pop_front();
          while (!route1->children.empty()) {
            IsoRoute* r2 = route1->children.front();
            route1->children.pop_front();
            IsoRouteList childrl; /* see if there is a merge */

            if (Merge(childrl, r1, r2, 1, true)) {
              for (IsoRouteList::iterator cit = childrl.begin();
                   cit != childrl.end(); cit++)
                if ((*cit)->direction == route1->direction)
                  childrenmask.push_back(*cit);
                else {
                  IsoRoute* child = *cit;
                  child->parent = route1;
                  route1->children.push_back(child);
                }
              goto remerge_children;
            } else
              mergedchildren.push_back(r2);
          }
          delete r1; /* all children have been tried, so done with this mask */

        remerge_children:
          route1->children.splice(route1->children.end(), mergedchildren);
        }
      } else if (route1->direction == -1 && route2->direction == -1) {
        delete route1; /* keep smaller region if both inverted */
        // Remove route1 from all lists that may contain it
        RemoveIsoRouteFromList(rl, route1);
        RemoveIsoRouteFromList(route1->children, route1);
        route1 = route2;
      } else if (route1->direction == 1 && route2->direction == -1) {
        delete route2;
        // Remove route1 from all lists that may contain it
        RemoveIsoRouteFromList(rl, route2);
        RemoveIsoRouteFromList(route1->children, route2);
      } else {
        /* this is a child route with a normal route completely inside..
           a contrived situation it is, should not get here often */
        //                printf("contrived delete: %d, %d\n", route1->Count(),
        //                route2->Count());
        delete route2;
        // Remove route1 from all lists that may contain it
        RemoveIsoRouteFromList(rl, route2);
        RemoveIsoRouteFromList(route1->children, route2);
      }
    } else {         // no inverted regions mode
      delete route2; /* it covers a sub area, delete it */
      // Remove route1 from all lists that may contain it
      RemoveIsoRouteFromList(rl, route2);
      RemoveIsoRouteFromList(route1->children, route2);
    }
    rl.push_back(route1); /* no need to normalize */
    return true;
  }

  /* routes close enough to pass initial rectangle test but no
   actual intersection or overlap occurs so no merge takes places */
  return false;
}

typedef wxWeakRef<Shared_GribRecordSet> Shared_GribRecordSetRef;
extern std::map<time_t, Shared_GribRecordSetRef> grib_key;
extern wxMutex s_key_mutex;

IsoChron::IsoChron(IsoRouteList r, wxDateTime t, double d,
                   Shared_GribRecordSet& g, bool grib_is_data_deficient)
    : routes(r),
      time(t),
      delta(d),
      m_SharedGrib(g),
      m_Grib(0),
      m_Grib_is_data_deficient(grib_is_data_deficient) {
  m_Grib = m_SharedGrib.GetGribRecordSet();
  if (m_Grib) {
    wxMutexLocker lock(s_key_mutex);
    grib_key[m_Grib->m_Reference_Time] = &m_SharedGrib;
  }
}
