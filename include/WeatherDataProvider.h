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

#ifndef _WEATHER_ROUTING_DATA_PROVIDER_H_
#define _WEATHER_ROUTING_DATA_PROVIDER_H_

#include <wx/wx.h>

#include <functional>
#include <memory>

#include "GribRecord.h"
#include "GribRecordSet.h"
#include "Boat.h"
#include "ClimatologyThreadGuard.h"

struct RouteMapConfiguration;
class RoutePoint;
struct climatology_wind_atlas;

class WeatherDataProvider {
public:
  virtual ~WeatherDataProvider() = default;

  /**
   * Initialize configured climatology services before route workers start.
   * Some climatology plugin versions lazily create wxWidgets controls on the
   * first query, which must happen on the main thread.
   */
  static bool PrepareClimatologyForWorkers(
      const RouteMapConfiguration& configuration);

  /** Reset preparation state when climatology callback pointers change. */
  static void ResetClimatologyPreparation();

  /**
   * Return whether a climatology callback may be invoked on this thread.
   * Main-thread calls are always allowed; worker calls require preflight.
   */
  static bool CanInvokeClimatology(ClimatologyService service);

  static double GetWeatherParameter(
      RouteMapConfiguration& configuration, double lat, double lon,
      const wxString& requestKey, int gribIndex, double returnOnEmpty = NAN,
      std::function<double(double)> postProcessFn = nullptr);
  /**
   * Return the swell height at the specified lat/long location.
   * @param configuration Route configuration with GRIB data
   * @param lat Latitude in degrees
   * @param lon Longitude in degrees
   * @return the swell height in meters. 0 if no data is available.
   */
  static double GetSwell(RouteMapConfiguration& configuration, double lat,
                         double lon);
  static double GetWaveDirection(RouteMapConfiguration& configuration,
                                 double lat, double lon);
  static double GetWavePeriod(RouteMapConfiguration& configuration, double lat,
                              double lon);

  static double GetGust(RouteMapConfiguration& configuration, double lat,
                        double lon);

  static double GetCloudCover(RouteMapConfiguration& configuration, double lat,
                              double lon);
  static double GetRainfall(RouteMapConfiguration& configuration, double lat,
                            double lon);
  static double GetAirTemperature(RouteMapConfiguration& configuration,
                                  double lat, double lon);
  static double GetSeaTemperature(RouteMapConfiguration& configuration,
                                  double lat, double lon);
  static double GetCAPE(RouteMapConfiguration& configuration, double lat,
                        double lon);
  static double GetRelativeHumidity(RouteMapConfiguration& configuration,
                                    double lat, double lon);
  static double GetAirPressure(RouteMapConfiguration& configuration, double lat,
                               double lon);
  static double GetReflectivity(RouteMapConfiguration& configuration,
                                double lat, double lon);

  static void GroundToWaterFrame(double groundDir, double groundMag,
                                 double currentDir, double currentMag,
                                 double& waterDir, double& waterMag);
  static bool GetGribWind(RouteMapConfiguration& configuration, double lat,
                          double lon, double& twdOverGround,
                          double& twsOverGround);
  static bool GetCurrent(RouteMapConfiguration& configuration, double lat,
                         double lon, double& currentDir, double& currentSpeed,
                         int& data_mask);

  static void TransformToGroundFrame(double directionWater,
                                     double magnitudeWater, double currentDir,
                                     double currentSpeed,
                                     double& directionGround,
                                     double& magnitudeGround);

  static bool ReadWindAndCurrents(RouteMapConfiguration& configuration,
                                  RoutePoint* p,
                                  /* normal data */
                                  double& twdOverGround, double& twsOverGround,
                                  double& twdOverWater, double& twsOverWater,
                                  double& currentDir, double& currentSpeed,
                                  climatology_wind_atlas& atlas,
                                  int& data_mask);
};

class WR_GribRecordSet {
public:
  WR_GribRecordSet(unsigned int id) : m_Reference_Time(-1), m_ID(id) {
    for (int i = 0; i < Idx_COUNT; i++) {
      m_GribRecordPtrArray[i] = 0;
      m_GribRecordUnref[i] = false;
    }
  }

  virtual ~WR_GribRecordSet() { RemoveGribRecords(); }

  /* copy and paste by plugins, keep functions in header */
  void SetUnRefGribRecord(int i, GribRecord* pGR) {
    assert(i >= 0 && i < Idx_COUNT);
    if (m_GribRecordUnref[i] == true) {
      delete m_GribRecordPtrArray[i];
    }
    m_GribRecordPtrArray[i] = pGR;
    m_GribRecordUnref[i] = true;
  }

  void RemoveGribRecords() {
    for (int i = 0; i < Idx_COUNT; i++) {
      if (m_GribRecordUnref[i] == true) {
        delete m_GribRecordPtrArray[i];
      }
    }
  }

  time_t m_Reference_Time;
  unsigned int m_ID;

  GribRecord* m_GribRecordPtrArray[Idx_COUNT];

private:
  // grib records files are stored and owned by reader mapGribRecords
  // interpolated grib are not, keep track of them
  bool m_GribRecordUnref[Idx_COUNT];
};

// ------
class Shared_GribRecordSet {
public:
  Shared_GribRecordSet(WR_GribRecordSet* ptr = 0)
      : m_data(ptr) {}
  explicit Shared_GribRecordSet(
      std::shared_ptr<WR_GribRecordSet> ptr)
      : m_data(std::move(ptr)) {}

  void SetGribRecordSet(WR_GribRecordSet* ptr) {
    m_data.reset(ptr);
  }
  void SetSharedGribRecordSet(std::shared_ptr<WR_GribRecordSet> ptr) {
    m_data = std::move(ptr);
  }

  WR_GribRecordSet* GetGribRecordSet() const {
    return m_data.get();
  }
  std::shared_ptr<WR_GribRecordSet> GetSharedGribRecordSet() const {
    return m_data;
  }

  bool operator==(const Shared_GribRecordSet& other) const {
    return m_data == other.m_data;
  }

private:
  // std::shared_ptr has thread-safe ownership accounting.  Published GRIB
  // frames are immutable, so UI-thread cache eviction and worker reads may
  // safely overlap without wxRefCounter races.
  std::shared_ptr<WR_GribRecordSet> m_data;
};

#endif
