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

#include "GribRecord.h"
#include "GribRecordSet.h"
#include "Boat.h"
#include "RoutePoint.h"

struct RouteMapConfiguration;
class RoutePoint;
struct climatology_wind_atlas;

class WeatherDataProvider {
public:
  virtual ~WeatherDataProvider() = default;

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
                         DataMask& data_mask);

  static void TransformToGroundFrame(double directionWater,
                                     double magnitudeWater, double currentDir,
                                     double currentSpeed,
                                     double& directionGround,
                                     double& magnitudeGround);

  static bool ReadWindAndCurrents(RouteMapConfiguration& configuration,
                                  const RoutePoint* p,
                                  /* normal data */
                                  double& twdOverGround, double& twsOverGround,
                                  double& twdOverWater, double& twsOverWater,
                                  double& currentDir, double& currentSpeed,
                                  climatology_wind_atlas& atlas,
                                  DataMask& data_mask);
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
class Shared_GribRecordSetData : public wxRefCounter {
public:
  Shared_GribRecordSetData(WR_GribRecordSet* gribset = 0)
      : m_GribRecordSet(gribset) {}
  Shared_GribRecordSetData(const Shared_GribRecordSetData& data)
      : m_GribRecordSet(data.m_GribRecordSet) {}

  void SetGribRecordSet(WR_GribRecordSet* gribset) {
    m_GribRecordSet = gribset;
  }
  WR_GribRecordSet* GetGribRecordSet() const { return m_GribRecordSet; }

  ~Shared_GribRecordSetData();

protected:
  WR_GribRecordSet* m_GribRecordSet;
};

class Shared_GribRecordSet : public wxTrackable {
public:
  // initializes this, assigning to the
  // internal data pointer a new instance of Shared_GribRecordSetData
  Shared_GribRecordSet(WR_GribRecordSet* ptr = 0)
      : m_data(new Shared_GribRecordSetData(ptr)) {}
  Shared_GribRecordSet& operator=(const Shared_GribRecordSet& tocopy) {
    // shallow copy: this is just a fast copy of pointers; the real
    // memory-consuming data which typically is stored inside
    m_data = tocopy.m_data;
    return *this;
  }

  void SetGribRecordSet(WR_GribRecordSet* ptr) {
    // make sure changes to this class do not affect other instances
    // currently sharing our same refcounted data:
    UnShare();
    m_data->SetGribRecordSet(ptr);
  }

  WR_GribRecordSet* GetGribRecordSet() const {
    return m_data->GetGribRecordSet();
  }

  bool operator==(const Shared_GribRecordSet& other) const {
    if (m_data.get() == other.m_data.get())
      return true;  // this instance and the 'other' one share the same data...
    return (m_data->GetGribRecordSet() == other.m_data->GetGribRecordSet());
  }

  wxObjectDataPtr<Shared_GribRecordSetData> m_data;

protected:
  void UnShare() {
    if (m_data->GetRefCount() == 1) return;
    m_data.reset(new Shared_GribRecordSetData(*m_data));
  }
};

#endif
