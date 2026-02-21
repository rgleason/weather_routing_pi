//==============================================================
// FILE: WeatherRoute.h
// CLASS: WeatherRoute
// PURPOSE: New UI
// Routing model, computation engine, structured errors
//==============================================================

#ifndef _WEATHER_ROUTING_WEATHERROUTE_H_
#define _WEATHER_ROUTING_WEATHERROUTE_H_

#pragma once
#include <wx/string.h>
#include <wx/datetime.h>
#include <vector>

// Forward declarations
class GRIBData;
class Polar;
class IsoChron;
class IsoRoute;
class RoutePoint;
class Position;

//==============================================================
// 1. ERROR CODE ENUM (public)
//==============================================================
enum RouteErrorCode {
  ERR_NONE = 0,

  // Input validation
  ERR_INVALID_START,
  ERR_INVALID_END,
  ERR_IDENTICAL_POINTS,
  ERR_CONFIG,

  // GRIB
  ERR_NO_GRIB,
  ERR_GRIB_COVERAGE,
  ERR_GRIB_TIME,
  ERR_GRIB_WIND,
  ERR_GRIB_CURRENT,

  // Polar
  ERR_POLAR_NONE,
  ERR_POLAR_INVALID,
  ERR_POLAR_RANGE,

  // Isochrones
  ERR_ISOCHRONE_FAIL,
  ERR_ISOCHRONE_EMPTY,
  ERR_UNREACHABLE,

  // Route construction
  ERR_ROUTE_BUILD,
  ERR_ROUTE_INVALID,

  // Threading
  ERR_ABORTED
};

//==============================================================
// 2. ROUTE STATE ENUM (public)
//==============================================================
enum class RouteState {
  Idle,
  Computing,
  Completed,
  Failed,
  Aborted,
  InvalidInput
};

//==============================================================
// 3. STRUCTURED ERROR (public)
//==============================================================
struct RouteError {
  bool hasError = false;

  wxString userMessage;      // short, human-readable
  wxString detailMessage;    // multi-line, technical
  wxString failureStage;     // subsystem: GRIB, Polar, Isochrones, etc.
  int errorCode = ERR_NONE;  // stable programmatic identifier
};

//==============================================================
// 4. WeatherRoute CLASS DECLARATION
//==============================================================
class WeatherRoute {
public:
  WeatherRoute();


  //----------------------------------------------------------
  // PUBLIC API
  //----------------------------------------------------------
  RouteState Update(bool stateOnly);
  wxString ComputeStateString() const;

  // Structured error helpers
  void ClearError();
  void SetError(int code, const wxString& user, const wxString& detail,
                const wxString& stage);

  // Existing UI string helpers (unchanged)
  // Legacy UI compatibility
  wxString StartTypeString() const;
  wxString StartString() const;
  wxString StartTimeString() const;
  wxString EndString() const;
  wxString EndTimeString() const;
  wxString DurationString() const;
  wxString DistanceString() const;
  wxString StateString() const;
  
  //----------------------------------------------------------
  // STATE + ERROR
  //----------------------------------------------------------
  RouteState state = RouteState::Idle;
  RouteError lastError;

private:
  //----------------------------------------------------------
  // PRIVATE COMPUTATION
  //----------------------------------------------------------
  bool HasValidEndpoints() const;
  bool ComputeIsochrones();
  bool BuildRoute();
  bool PropagateNode(const IsoRoute& src, double speed, IsoRoute& dst);

private:
  //----------------------------------------------------------
  // PRIVATE MEMBERS
  //----------------------------------------------------------
  GRIBData* m_Grib = nullptr;
  Polar* m_Polar = nullptr;

  double start_lat = 0.0;
  double start_lon = 0.0;
  double end_lat = 0.0;
  double end_lon = 0.0;

  wxDateTime start_time;
  wxDateTime end_time;

  double distance_nm = 0.0;

  int start_type = 0;  // START_MANUAL, START_BOAT, START_POSITION
  Position* start_position = nullptr;
  Position* end_position = nullptr;

  std::vector<IsoChron*> m_IsoChronList;
  std::vector<RoutePoint> m_RoutePoints;

  int m_MaxSteps = 200;
  int m_TimeStep = 3600;  // seconds
};
#endif
