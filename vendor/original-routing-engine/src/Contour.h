// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "original_routing/Engine.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <list>
#include <limits>
#include <vector>

namespace original_routing::detail {
class Position;
class SkipPosition;
class IsoRoute;
struct Trace;
using IsoRouteList = std::list<IsoRoute*>;
enum { MINLAT, MAXLAT, MINLON, MAXLON };

struct Stop {
  wr::RoutingStatus status;
  const char* reason;
};

// O(1) allocation accounting and ownership even if cancellation interrupts
// a contour splice. Bulk teardown visits objects individually, never follows
// a partially updated linked list. Free registry slots are reused.
template <class T>
struct Registry {
  std::vector<T*> objects;
  std::vector<std::size_t> free;
  std::size_t count{};
  std::size_t add(T* p) {
    std::size_t n;
    if (free.empty()) {
      n = objects.size();
      if (free.capacity() < n + 1)
        free.reserve(std::max<std::size_t>(16, (n + 1) * 2));
      objects.push_back(p);
    } else {
      n = free.back();
      free.pop_back();
      objects[n] = p;
    }
    ++count;
    return n;
  }
  void remove(std::size_t n) {
    objects[n] = nullptr;
    --count;
    free.push_back(n);
  }
};

struct Arena {
  const wr::RoutingRequest& request;
  const Options& options;
  std::chrono::steady_clock::time_point deadline;
  Registry<Position> positions;
  Registry<SkipPosition> skips;
  Registry<IsoRoute> routes;
  Registry<Trace> traces;
  std::uint64_t operations{};
  bool cleaning{};
  Arena(const wr::RoutingRequest& r, const Options& o)
      : request(r),
        options(o),
        deadline(std::chrono::steady_clock::now() + o.maximumRuntime) {}
  ~Arena();
  void checkpoint();
  void allocation();
};

struct Trace {
  Arena& arena;
  std::size_t slot;
  Trace* parent;
  unsigned refs{1};
  wr::GeoPoint point;
  wr::TimePoint time;
  double heading{}, angle{};
  wr::Duration modeDuration{}, motorTime{};
  double fuel{};
  unsigned profile{};
  bool tack{}, gybe{}, modeChange{};
  Trace(Arena&, Trace*, wr::GeoPoint, wr::TimePoint);
  ~Trace();
  void retain() { ++refs; }
  void release();
};

class Position {
public:
  Arena& arena;
  std::size_t slot;
  double lat{}, lon{};
  Trace* trace{};
  Position *prev{}, *next{};
  bool propagated{}, drawn{}, copied{};
  Position(Arena&, double, double, Trace*);
  explicit Position(const Position*);
  ~Position();
  SkipPosition* BuildSkipList();
};
class SkipPosition {
public:
  Arena& arena;
  std::size_t slot;
  Position* point;
  int quadrant;
  SkipPosition *prev{}, *next{};
  SkipPosition(Position*, int);
  ~SkipPosition();
  void Remove();
  SkipPosition* Copy();
};
class IsoRoute {
public:
  Arena& arena;
  std::size_t slot;
  SkipPosition* skippoints;
  int direction;
  IsoRoute* parent;
  IsoRouteList children;
  explicit IsoRoute(SkipPosition*, int = 1);
  IsoRoute(IsoRoute*, IsoRoute* = nullptr);
  ~IsoRoute();
  void Print();
  void PrintSkip();
  void MinimizeLat();
  int IntersectionCount(Position&);
  int Contains(Position&, bool);
  bool CompletelyContained(IsoRoute*);
  bool ContainsRoute(IsoRoute*);
  void ReduceClosePoints();
  void FindIsoRouteBounds(double[4]);
  void RemovePosition(SkipPosition*, Position*);
  Position* ClosestPosition(double, double, double* = nullptr);
  void ResetDrawnFlag();
  int SkipCount();
  int Count();
  void UpdateStatistics(int&, int&, int&, int&);
};
int ComputeQuadrantFast(Position*, Position*);
void DeletePoints(Position*);
void DeleteSkipPoints(SkipPosition*);
bool Merge(IsoRouteList&, IsoRoute*, IsoRoute*, int, bool);
}  // namespace original_routing::detail
