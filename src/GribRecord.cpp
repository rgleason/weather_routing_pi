/**********************************************************************
zyGrib: meteorological GRIB file viewer
Copyright (C) 2008 - Jacques Zaninetti - http://www.zygrib.org

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program.  If not, see <http://www.gnu.org/licenses/>.
***********************************************************************/
/**
 * \file
 * \implements \ref GribRecord.h
 */
#include <stdlib.h>
#include <cstring>  // memcpy
#include <vector>
#include <algorithm>
#include <memory>
#include <limits>
#include <stdexcept>

#include <cstdio>
#include <cstdlib>
#include <cmath>

// #include <QDateTime>

#include "GribRecord.h"

static constexpr double kPi = 3.141592653589793238462643383279502884;

// interpolate two angles in range +- 180 or +-PI, with resulting angle in the
// same range
static double interp_angle(double a0, double a1, double d, double p) {
  if (a0 - a1 > p)
    a0 -= 2 * p;
  else if (a1 - a0 > p)
    a1 -= 2 * p;
  double a = (1 - d) * a0 + d * a1;
  if (a < (p == 180. ? 0. : -p)) a += 2 * p;
  return a;
}

//-------------------------------------------------------------------------------
GribRecord::GribRecord()
{
  // sensible zero/empty initialization for all members used elsewhere
  id = 0;
  ok = false;
  knownData = false;
  waveData = false;
  IsDuplicated = false;
  eof = false;
  dataKey.clear();
  std::memset(strRefDate, 0, sizeof(strRefDate));
  std::memset(strCurDate, 0, sizeof(strCurDate));
  dataCenterModel = 0;
  m_bfilled = false;
  editionNumber = 0;
  idCenter = 0;
  idModel = 0;
  idGrid = 0;
  dataType = 0;
  levelType = 0;
  levelValue = 0;
  hasBMS = false;
  refyear = refmonth = refday = refhour = refminute = 0;
  periodP1 = periodP2 = 0;
  timeRange = 0;
  periodsec = 0;
  refDate = curDate = 0;
  NV = PV = gridType = 0;
  Ni = Nj = 0;
  La1 = Lo1 = La2 = Lo2 = 0.0;
  latMin = lonMin = latMax = lonMax = 0.0;
  Di = Dj = 0.0;
  resolFlags = scanFlags = 0;
  hasDiDj = false;
  isEarthSpheric = false;
  isUeastVnorth = false;
  isScanIpositive = false;
  isScanJpositive = false;
  isAdjacentI = false;
  BMSsize = 0;
  data = nullptr;
  BMSbits = nullptr;
}


//-------------------------------------------------------------------------------
void GribRecord::print() {
  printf(
      "%d: idCenter=%d idModel=%d idGrid=%d dataType=%d levelType=%d "
      "levelValue=%d hr=%f\n",
      id, idCenter, idModel, idGrid, dataType, levelType, levelValue,
      (curDate - refDate) / 3600.0);
}

//-------------------------------------------------------------------------------
// Copy constructor (deep copy)
//-------------------------------------------------------------------------------
void GribRecord::copyMetadata(const GribRecord& rec) {
  // copy simple/scalar members
  id = rec.id;
  ok = rec.ok;
  knownData = rec.knownData;
  waveData = rec.waveData;
  IsDuplicated = rec.IsDuplicated;
  eof = rec.eof;
  dataKey = rec.dataKey;
  std::memcpy(strRefDate, rec.strRefDate, sizeof(strRefDate));
  std::memcpy(strCurDate, rec.strCurDate, sizeof(strCurDate));
  dataCenterModel = rec.dataCenterModel;
  m_bfilled = rec.m_bfilled;
  editionNumber = rec.editionNumber;
  idCenter = rec.idCenter;
  idModel = rec.idModel;
  idGrid = rec.idGrid;
  dataType = rec.dataType;
  levelType = rec.levelType;
  levelValue = rec.levelValue;
  hasBMS = rec.hasBMS;
  refyear = rec.refyear;
  refmonth = rec.refmonth;
  refday = rec.refday;
  refhour = rec.refhour;
  refminute = rec.refminute;
  periodP1 = rec.periodP1;
  periodP2 = rec.periodP2;
  timeRange = rec.timeRange;
  periodsec = rec.periodsec;
  refDate = rec.refDate;
  curDate = rec.curDate;
  NV = rec.NV;
  PV = rec.PV;
  gridType = rec.gridType;
  Ni = rec.Ni;
  Nj = rec.Nj;
  La1 = rec.La1;
  Lo1 = rec.Lo1;
  La2 = rec.La2;
  Lo2 = rec.Lo2;
  latMin = rec.latMin;
  lonMin = rec.lonMin;
  latMax = rec.latMax;
  lonMax = rec.lonMax;
  Di = rec.Di;
  Dj = rec.Dj;
  resolFlags = rec.resolFlags;
  scanFlags = rec.scanFlags;
  hasDiDj = rec.hasDiDj;
  isEarthSpheric = rec.isEarthSpheric;
  isUeastVnorth = rec.isUeastVnorth;
  isScanIpositive = rec.isScanIpositive;
  isScanJpositive = rec.isScanJpositive;
  isAdjacentI = rec.isAdjacentI;
  BMSsize = rec.BMSsize;

 }

GribRecord::GribRecord(const GribRecord& rec) : GribRecord() {
  copyMetadata(rec);
  IsDuplicated = true;
  const std::size_t n = rec.data ? rec.dataCount() : 0;
  if (rec.data && !n)
    throw std::length_error("Invalid GRIB grid dimensions");
  std::unique_ptr<double[]> values;
  std::unique_ptr<zuchar[]> bitmap;
  if (n) {
    values.reset(new double[n]);
    std::memcpy(values.get(), rec.data, n * sizeof(double));
  }
  if (rec.BMSbits && rec.BMSsize) {
    bitmap.reset(new zuchar[rec.BMSsize]);
    std::memcpy(bitmap.get(), rec.BMSbits, rec.BMSsize);
  }
  data = values.release();
  BMSbits = bitmap.release();
}

//-------------------------------------------------------------------------------
// Move constructor
//-------------------------------------------------------------------------------
GribRecord::GribRecord(GribRecord &&other) noexcept
    : GribRecord() {
  // Steal resources
  swap(other);
  // leave other in a safe-to-destroy state
  other.data = nullptr;
  other.BMSbits = nullptr;
  other.BMSsize = 0;
  other.Ni = other.Nj = 0;
  other.IsDuplicated = false;
}

//-------------------------------------------------------------------------------
/* Copy assignment: allocate copies first, then replace existing pointers.
   This provides strong exception safety: if allocation fails, the object is
   unchanged.
*/
//-------------------------------------------------------------------------------
GribRecord &GribRecord::operator=(const GribRecord &rec) {
  if (this != &rec) {
    GribRecord copy(rec);
    copy.IsDuplicated = rec.IsDuplicated;
    swap(copy);
  }
  return *this;
}

//-------------------------------------------------------------------------------
// Move assignment
//-------------------------------------------------------------------------------
GribRecord &GribRecord::operator=(GribRecord &&other) noexcept {
  if (this != &other) {
    // free current resources
    delete[] data;
    delete[] BMSbits;

    // Move scalars
    id = other.id;
    ok = other.ok;
    knownData = other.knownData;
    waveData = other.waveData;
    IsDuplicated = other.IsDuplicated;
    eof = other.eof;
    dataKey = std::move(other.dataKey);
    std::memcpy(strRefDate, other.strRefDate, sizeof(strRefDate));
    std::memcpy(strCurDate, other.strCurDate, sizeof(strCurDate));
    dataCenterModel = other.dataCenterModel;
    m_bfilled = other.m_bfilled;
    editionNumber = other.editionNumber;
    idCenter = other.idCenter;
    idModel = other.idModel;
    idGrid = other.idGrid;
    dataType = other.dataType;
    levelType = other.levelType;
    levelValue = other.levelValue;
    hasBMS = other.hasBMS;
    refyear = other.refyear;
    refmonth = other.refmonth;
    refday = other.refday;
    refhour = other.refhour;
    refminute = other.refminute;
    periodP1 = other.periodP1;
    periodP2 = other.periodP2;
    timeRange = other.timeRange;
    periodsec = other.periodsec;
    refDate = other.refDate;
    curDate = other.curDate;
    NV = other.NV;
    PV = other.PV;
    gridType = other.gridType;
    Ni = other.Ni;
    Nj = other.Nj;
    La1 = other.La1;
    Lo1 = other.Lo1;
    La2 = other.La2;
    Lo2 = other.Lo2;
    latMin = other.latMin;
    lonMin = other.lonMin;
    latMax = other.latMax;
    lonMax = other.lonMax;
    Di = other.Di;
    Dj = other.Dj;
    resolFlags = other.resolFlags;
    scanFlags = other.scanFlags;
    hasDiDj = other.hasDiDj;
    isEarthSpheric = other.isEarthSpheric;
    isUeastVnorth = other.isUeastVnorth;
    isScanIpositive = other.isScanIpositive;
    isScanJpositive = other.isScanJpositive;
    isAdjacentI = other.isAdjacentI;

    // Steal buffers
    data = other.data;
    BMSbits = other.BMSbits;
    BMSsize = other.BMSsize;

    // Leave other in safe-to-destroy state
    other.data = nullptr;
    other.BMSbits = nullptr;
    other.BMSsize = 0;
    other.Ni = other.Nj = 0;
    other.IsDuplicated = false;
  }
  return *this;
}

//-------------------------------------------------------------------------------
void GribRecord::swap(GribRecord &other) noexcept {
  using std::swap;
  swap(id, other.id);
  swap(ok, other.ok);
  swap(knownData, other.knownData);
  swap(waveData, other.waveData);
  swap(IsDuplicated, other.IsDuplicated);
  swap(eof, other.eof);
  dataKey.swap(other.dataKey);
  char tmpRef[32];
  std::memcpy(tmpRef, strRefDate, sizeof(strRefDate));
  std::memcpy(strRefDate, other.strRefDate, sizeof(strRefDate));
  std::memcpy(other.strRefDate, tmpRef, sizeof(strRefDate));
  std::memcpy(tmpRef, strCurDate, sizeof(strCurDate));
  std::memcpy(strCurDate, other.strCurDate, sizeof(strCurDate));
  std::memcpy(other.strCurDate, tmpRef, sizeof(strCurDate));
  swap(dataCenterModel, other.dataCenterModel);
  swap(m_bfilled, other.m_bfilled);
  swap(editionNumber, other.editionNumber);
  swap(idCenter, other.idCenter);
  swap(idModel, other.idModel);
  swap(idGrid, other.idGrid);
  swap(dataType, other.dataType);
  swap(levelType, other.levelType);
  swap(levelValue, other.levelValue);
  swap(hasBMS, other.hasBMS);
  swap(refyear, other.refyear);
  swap(refmonth, other.refmonth);
  swap(refday, other.refday);
  swap(refhour, other.refhour);
  swap(refminute, other.refminute);
  swap(periodP1, other.periodP1);
  swap(periodP2, other.periodP2);
  swap(timeRange, other.timeRange);
  swap(periodsec, other.periodsec);
  swap(refDate, other.refDate);
  swap(curDate, other.curDate);
  swap(NV, other.NV);
  swap(PV, other.PV);
  swap(gridType, other.gridType);
  swap(Ni, other.Ni);
  swap(Nj, other.Nj);
  swap(La1, other.La1);
  swap(Lo1, other.Lo1);
  swap(La2, other.La2);
  swap(Lo2, other.Lo2);
  swap(latMin, other.latMin);
  swap(lonMin, other.lonMin);
  swap(latMax, other.latMax);
  swap(lonMax, other.lonMax);
  swap(Di, other.Di);
  swap(Dj, other.Dj);
  swap(resolFlags, other.resolFlags);
  swap(scanFlags, other.scanFlags);
  swap(hasDiDj, other.hasDiDj);
  swap(isEarthSpheric, other.isEarthSpheric);
  swap(isUeastVnorth, other.isUeastVnorth);
  swap(isScanIpositive, other.isScanIpositive);
  swap(isScanJpositive, other.isScanJpositive);
  swap(isAdjacentI, other.isAdjacentI);
  swap(BMSsize, other.BMSsize);
  swap(BMSbits, other.BMSbits);
  swap(data, other.data);
}

//-------------------------------------------------------------------------------
GribRecord::~GribRecord() {
  if (data) {
    delete[] data;
    data = nullptr;
  }
  if (BMSbits) {
    delete[] BMSbits;
    BMSbits = nullptr;
  }

  // if (dataType==GRB_TEMP) printf("record destroyed %s   %d\n",
  // dataKey.mb_str(), (int)curDate/3600);
}

//-------------------------------------------------------------------------------
//-------------------------------------------------------------------------------

bool GribRecord::GetInterpolatedParameters(
    const GribRecord &rec1, const GribRecord &rec2, double &La1, double &Lo1,
    double &La2, double &Lo2, double &Di, double &Dj, int &im1, int &jm1,
    int &im2, int &jm2, int &Ni, int &Nj, int &rec1offi, int &rec1offj,
    int &rec2offi, int &rec2offj) {
  if (!rec1.isOk() || !rec2.isOk() || !rec1.validGrid() || !rec2.validGrid())
    return false;
  // This aligned-grid routine historically supports eastward grids. Reversed
  // longitude grids can still be queried spatially; reject them here safely.
  if (rec1.Di <= 0 || rec2.Di <= 0) return false;

  /* make sure Dj both have same sign */
  if (rec1.getDj() * rec2.getDj() <= 0) return false;

  Di = std::max(rec1.getDi(), rec2.getDi());
  Dj = rec1.getDj() > 0 ? std::max(rec1.getDj(), rec2.getDj())
                        : std::min(rec1.getDj(), rec2.getDj());

  /* get overlapping region */
  if (Dj > 0)
    La1 = std::max(rec1.La1, rec2.La1), La2 = std::min(rec1.La2, rec2.La2);
  else
    La1 = std::min(rec1.La1, rec2.La1), La2 = std::max(rec1.La2, rec2.La2);

  Lo1 = std::max(rec1.Lo1, rec2.Lo1), Lo2 = std::min(rec1.Lo2, rec2.Lo2);

  // align gribs on integer boundaries
  int i, j;
  // shut up compiler warning 'may be used uninitialized'
  // rec2.Dj / rec1.Dj > 0
  // XXX Is it true  for rec2.Di / rec1.Di ?
  double rec1offdi = 0, rec2offdi = 0;
  double rec1offdj = 0., rec2offdj = 0.;

  double iiters = rec2.Di / rec1.Di;
  const double iratio = std::max(iiters, 1.0 / iiters);
  const double jratio = std::max(std::abs(rec2.Dj / rec1.Dj),
                                std::abs(rec1.Dj / rec2.Dj));
  if (!std::isfinite(iratio) || !std::isfinite(jratio) ||
      iratio > std::numeric_limits<int>::max() ||
      jratio > std::numeric_limits<int>::max() ||
      std::abs(iratio - std::round(iratio)) > 1e-8 ||
      std::abs(jratio - std::round(jratio)) > 1e-8)
    return false;
  if (iiters < 1) {
    iiters = 1 / iiters;
    im1 = 1, im2 = iiters;
  } else
    im1 = iiters, im2 = 1;

  const int ilimit = static_cast<int>(std::min(iiters, double(std::max(rec1.Ni,rec2.Ni))));
  for (i = 0; i < ilimit; i++) {
    rec1offdi = (Lo1 - rec1.Lo1) / rec1.Di;
    rec2offdi = (Lo1 - rec2.Lo1) / rec2.Di;
    if (rec1offdi == floor(rec1offdi) && rec2offdi == floor(rec2offdi)) break;

    Lo1 += std::min(rec1.Di, rec2.Di);
  }
  if (i == ilimit)  // failed to align, would need spacial interpolation to work
    return false;

  double jiters = rec2.Dj / rec1.Dj;
  if (jiters < 1) {
    jiters = 1 / jiters;
    jm1 = 1, jm2 = jiters;
  } else
    jm1 = jiters, jm2 = 1;

  const int jlimit = static_cast<int>(std::min(jiters, double(std::max(rec1.Nj,rec2.Nj))));
  for (j = 0; j < jlimit; j++) {
    rec1offdj = (La1 - rec1.La1) / rec1.Dj;
    rec2offdj = (La1 - rec2.La1) / rec2.Dj;
    if (rec1offdj == floor(rec1offdj) && rec2offdj == floor(rec2offdj)) break;

    La1 += Dj < 0 ? std::max(rec1.getDj(), rec2.getDj())
                  : std::min(rec1.getDj(), rec2.getDj());
  }
  if (j == jlimit)  // failed to align
    return false;

  /* no overlap */
  if (La1 * Dj > La2 * Dj || Lo1 > Lo2) return false;

  /* compute integer sizes for data array */
  const double nx = (Lo2 - Lo1) / Di + 1;
  const double ny = (La2 - La1) / Dj + 1;
  const double limit = std::numeric_limits<int>::max();
  if (!std::isfinite(nx) || !std::isfinite(ny) || nx < 1 || ny < 1 ||
      nx > limit || ny > limit || nx * ny > limit)
    return false;
  Ni = static_cast<int>(nx); Nj = static_cast<int>(ny);

  /* back-compute final La2 and Lo2 to fit this integer boundary */
  Lo2 = Lo1 + (Ni - 1) * Di, La2 = La1 + (Nj - 1) * Dj;

  if (!std::isfinite(rec1offdi) || !std::isfinite(rec2offdi) ||
      !std::isfinite(rec1offdj) || !std::isfinite(rec2offdj) ||
      rec1offdi < 0 || rec2offdi < 0 || rec1offdj < 0 || rec2offdj < 0 ||
      rec1offdi > limit || rec2offdi > limit ||
      rec1offdj > limit || rec2offdj > limit) return false;
  rec1offi = rec1offdi, rec2offi = rec2offdi;
  rec1offj = rec1offdj, rec2offj = rec2offdj;

  // Validate the last sampled point before either inner loop. All later
  // index arithmetic fits int because each complete grid fits that range.
  if (std::uint64_t(rec1offi) + std::uint64_t(Ni - 1) * im1 >= rec1.Ni ||
      std::uint64_t(rec2offi) + std::uint64_t(Ni - 1) * im2 >= rec2.Ni ||
      std::uint64_t(rec1offj) + std::uint64_t(Nj - 1) * jm1 >= rec1.Nj ||
      std::uint64_t(rec2offj) + std::uint64_t(Nj - 1) * jm2 >= rec2.Nj)
    return false;

  return true;
}

bool GribRecord::GetSpatialInterpolationGrid(
    const GribRecord &rec1, const GribRecord &rec2, double &La1, double &Lo1,
    double &La2, double &Lo2, double &Di, double &Dj, int &Ni, int &Nj) {
  if (!rec1.isOk() || !rec2.isOk() || !rec1.validGrid() || !rec2.validGrid() ||
      rec1.getDj() * rec2.getDj() <= 0)
    return false;

  Di = std::max(std::abs(rec1.getDi()), std::abs(rec2.getDi()));
  const double absDj =
      std::max(std::abs(rec1.getDj()), std::abs(rec2.getDj()));
  if (Di <= 0.0 || absDj <= 0.0) return false;
  Dj = rec1.getDj() > 0.0 ? absDj : -absDj;

  const double lonMin = std::max(rec1.getLonMin(), rec2.getLonMin());
  const double lonMax = std::min(rec1.getLonMax(), rec2.getLonMax());
  const double latMin = std::max(rec1.getLatMin(), rec2.getLatMin());
  const double latMax = std::min(rec1.getLatMax(), rec2.getLatMax());
  if (lonMin > lonMax || latMin > latMax) return false;

  const double nx = std::floor((lonMax - lonMin) / Di + 1e-9) + 1;
  const double ny = std::floor((latMax - latMin) / std::abs(Dj) + 1e-9) + 1;
  const double limit = std::numeric_limits<int>::max();
  if (!std::isfinite(nx) || !std::isfinite(ny) || nx < 1 || ny < 1 ||
      nx > limit || ny > limit || nx * ny > limit ||
      nx * ny > std::min(rec1.dataCount(), rec2.dataCount())) return false;
  Ni = static_cast<int>(nx); Nj = static_cast<int>(ny);
  Lo1 = lonMin; Lo2 = Lo1 + (Ni - 1) * Di;
  La1 = Dj > 0 ? latMin : latMax; La2 = La1 + (Nj - 1) * Dj;
  return Ni > 0 && Nj > 0;
}

GribRecord *GribRecord::SpatiallyInterpolatedRecord(
    const GribRecord &rec1, const GribRecord &rec2, double d, bool dir) {
  double La1, Lo1, La2, Lo2, Di, Dj;
  int Ni, Nj;
  if (!GetSpatialInterpolationGrid(rec1, rec2, La1, Lo1, La2, Lo2, Di, Dj,
                                   Ni, Nj))
    return nullptr;

  std::unique_ptr<double[]> owner(new double[Ni * Nj]);
  double* values = owner.get();
  for (int j = 0; j < Nj; ++j) {
    const double lat = La1 + j * Dj;
    for (int i = 0; i < Ni; ++i) {
      const double lon = Lo1 + i * Di;
      const double first = rec1.getInterpolatedValue(lon, lat, true, dir);
      const double second = rec2.getInterpolatedValue(lon, lat, true, dir);
      const int index = j * Ni + i;
      if (first == GRIB_NOTDEF || second == GRIB_NOTDEF) {
        values[index] = GRIB_NOTDEF;
      } else if (dir) {
        values[index] = interp_angle(first, second, d, 180.0);
      } else {
        values[index] = (1.0 - d) * first + d * second;
      }
    }
  }

  std::unique_ptr<GribRecord> result(new GribRecord);
  result->copyMetadata(rec1);
  result->Di = Di;
  result->Dj = Dj;
  result->Ni = Ni;
  result->Nj = Nj;
  result->La1 = La1;
  result->La2 = La2;
  result->Lo1 = Lo1;
  result->Lo2 = Lo2;
  result->latMin = std::min(La1, La2);
  result->latMax = std::max(La1, La2);
  result->lonMin = Lo1;
  result->lonMax = Lo2;
  result->data = owner.release();
  result->BMSsize = 0;
  result->hasBMS = false;
  result->BMSbits = nullptr;
  result->m_bfilled = false;
  return result.release();
}

GribRecord *GribRecord::SpatiallyInterpolated2DRecord(
    GribRecord *&rety, const GribRecord &rec1x, const GribRecord &rec1y,
    const GribRecord &rec2x, const GribRecord &rec2y, double d) {
  rety = nullptr;
  if (!std::isfinite(d) || !rec1x.sameGrid(rec1y) || !rec2x.sameGrid(rec2y) ||
      !rec1y.validGrid() || !rec2y.validGrid()) return nullptr;
  double La1, Lo1, La2, Lo2, Di, Dj;
  int Ni, Nj;
  if (!GetSpatialInterpolationGrid(rec1x, rec2x, La1, Lo1, La2, Lo2, Di, Dj,
                                   Ni, Nj) ||
      !rec1y.isOk() || !rec2y.isOk())
    return nullptr;

  std::unique_ptr<double[]> ownerX(new double[Ni * Nj]);
  std::unique_ptr<double[]> ownerY(new double[Ni * Nj]);
  double* valuesX = ownerX.get(); double* valuesY = ownerY.get();
  for (int j = 0; j < Nj; ++j) {
    const double lat = La1 + j * Dj;
    for (int i = 0; i < Ni; ++i) {
      const double lon = Lo1 + i * Di;
      const double firstX = rec1x.getInterpolatedValue(lon, lat, true);
      const double firstY = rec1y.getInterpolatedValue(lon, lat, true);
      const double secondX = rec2x.getInterpolatedValue(lon, lat, true);
      const double secondY = rec2y.getInterpolatedValue(lon, lat, true);
      const int index = j * Ni + i;
      if (firstX == GRIB_NOTDEF || firstY == GRIB_NOTDEF ||
          secondX == GRIB_NOTDEF || secondY == GRIB_NOTDEF) {
        valuesX[index] = valuesY[index] = GRIB_NOTDEF;
        continue;
      }
      const double firstMagnitude = std::hypot(firstX, firstY);
      const double secondMagnitude = std::hypot(secondX, secondY);
      const double magnitude =
          (1.0 - d) * firstMagnitude + d * secondMagnitude;
      double firstAngle = std::atan2(firstY, firstX);
      double secondAngle = std::atan2(secondY, secondX);
      if (firstAngle - secondAngle > kPi)
        firstAngle -= 2.0 * kPi;
      else if (secondAngle - firstAngle > kPi)
        secondAngle -= 2.0 * kPi;
      const double angle = (1.0 - d) * firstAngle + d * secondAngle;
      valuesX[index] = magnitude * std::cos(angle);
      valuesY[index] = magnitude * std::sin(angle);
    }
  }

  std::unique_ptr<GribRecord> resultX(new GribRecord), resultY(new GribRecord);
  resultX->copyMetadata(rec1x); resultY->copyMetadata(rec1y);
  for (GribRecord *result : {resultX.get(), resultY.get()}) {
    result->Di = Di;
    result->Dj = Dj;
    result->Ni = Ni;
    result->Nj = Nj;
    result->La1 = La1;
    result->La2 = La2;
    result->Lo1 = Lo1;
    result->Lo2 = Lo2;
    result->latMin = std::min(La1, La2);
    result->latMax = std::max(La1, La2);
    result->lonMin = Lo1;
    result->lonMax = Lo2;
    result->BMSsize = 0;
    result->hasBMS = false;
    result->BMSbits = nullptr;
    result->m_bfilled = false;
  }
  resultX->data = ownerX.release();
  resultY->data = ownerY.release();
  rety = resultY.release();
  return resultX.release();
}

//-------------------------------------------------------------------------------
// Constructeur de interpolate
//-------------------------------------------------------------------------------
GribRecord *GribRecord::InterpolatedRecord(const GribRecord &rec1,
                                           const GribRecord &rec2, double d,
                                           bool dir) {
  if (!std::isfinite(d)) return nullptr;
  double La1, Lo1, La2, Lo2, Di, Dj;
  int im1, jm1, im2, jm2;
  int Ni, Nj, rec1offi, rec1offj, rec2offi, rec2offj;
  if (!GetInterpolatedParameters(rec1, rec2, La1, Lo1, La2, Lo2, Di, Dj, im1,
                                 jm1, im2, jm2, Ni, Nj, rec1offi, rec1offj,
                                 rec2offi, rec2offj))
    return SpatiallyInterpolatedRecord(rec1, rec2, d, dir);

  // recopie les champs de bits
  int size = Ni * Nj;
  std::unique_ptr<double[]> values(new double[size]);
  double* data = values.get();

  const unsigned bitmapSize = (size + 7u) / 8u;
  const bool hasBitmap = rec1.hasBMS || rec2.hasBMS;
  std::unique_ptr<zuchar[]> bitmap(hasBitmap ? new zuchar[bitmapSize]() : nullptr);
  zuchar* BMSbits = bitmap.get();

  // Traverse the row-major arrays contiguously.
  for (int j = 0; j < Nj; j++)
    for (int i = 0; i < Ni; i++) {
      int in = j * Ni + i;
      int i1 = (j * jm1 + rec1offj) * rec1.Ni + i * im1 + rec1offi;
      int i2 = (j * jm2 + rec2offj) * rec2.Ni + i * im2 + rec2offi;
      double data1 = rec1.data[i1], data2 = rec2.data[i2];
      if (data1 == GRIB_NOTDEF || data2 == GRIB_NOTDEF)
        data[in] = GRIB_NOTDEF;
      else {
        if (!dir)
          data[in] = (1 - d) * data1 + d * data2;
        else
          data[in] = interp_angle(data1, data2, d, 180.);
      }

      if (BMSbits) {
        const bool b1 = rec1.hasValue(i * im1 + rec1offi, j * jm1 + rec1offj);
        const bool b2 = rec2.hasValue(i * im2 + rec2offi, j * jm2 + rec2offj);
        if (b1 && b2)
          BMSbits[in >> 3] |= 128u >> (in & 7);
        else {
          BMSbits[in >> 3] &= ~(128u >> (in & 7));
          data[in] = GRIB_NOTDEF;
        }
      }
    }

  /* should maybe update strCurDate ? */

  std::unique_ptr<GribRecord> ret(new GribRecord);
  ret->copyMetadata(rec1);

  ret->Di = Di, ret->Dj = Dj;
  ret->Ni = Ni, ret->Nj = Nj;

  ret->La1 = La1, ret->La2 = La2;
  ret->Lo1 = Lo1, ret->Lo2 = Lo2;

  ret->data = values.release();
  ret->BMSbits = bitmap.release();
  ret->BMSsize = hasBitmap ? bitmapSize : 0;
  ret->hasBMS = hasBitmap;
  ret->isAdjacentI = true;

  ret->latMin = std::min(La1, La2), ret->latMax = std::max(La1, La2);
  ret->lonMin = Lo1, ret->lonMax = Lo2;

  ret->m_bfilled = false;

  return ret.release();
}

/* for interpolation for x and y records, we must do them together because
   otherwise we end up with a vector interpolation which is not what we want..
   instead we want to interpolate from the polar magnitude, and angles */
GribRecord *GribRecord::Interpolated2DRecord(
    GribRecord *&rety, const GribRecord &rec1x, const GribRecord &rec1y,
    const GribRecord &rec2x, const GribRecord &rec2y, double d) {
  double La1, Lo1, La2, Lo2, Di, Dj;
  int im1, jm1, im2, jm2;
  int Ni, Nj, rec1offi, rec1offj, rec2offi, rec2offj;

  rety = nullptr;
  if (!std::isfinite(d) || !rec1x.sameGrid(rec1y) || !rec2x.sameGrid(rec2y) ||
      !rec1y.validGrid() || !rec2y.validGrid() || !rec1y.ok || !rec2y.ok)
    return nullptr;
  if (!GetInterpolatedParameters(rec1x, rec2x, La1, Lo1, La2, Lo2, Di, Dj, im1,
                                 jm1, im2, jm2, Ni, Nj, rec1offi, rec1offj,
                                 rec2offi, rec2offj))
    return SpatiallyInterpolated2DRecord(rety, rec1x, rec1y, rec2x, rec2y, d);

  // recopie les champs de bits
  int size = Ni * Nj;
  std::unique_ptr<double[]> valuesX(new double[size]);
  std::unique_ptr<double[]> valuesY(new double[size]);
  double* datax = valuesX.get(); double* datay = valuesY.get();
  // Traverse all four input arrays and both outputs contiguously.
  for (int j = 0; j < Nj; j++) {
    for (int i = 0; i < Ni; i++) {
      int in = j * Ni + i;
      int i1 = (j * jm1 + rec1offj) * rec1x.Ni + i * im1 + rec1offi;
      int i2 = (j * jm2 + rec2offj) * rec2x.Ni + i * im2 + rec2offi;
      double data1x = rec1x.data[i1], data1y = rec1y.data[i1];
      double data2x = rec2x.data[i2], data2y = rec2y.data[i2];
      // Decoders already expand missing bitmap cells into GRIB_NOTDEF.
      if (data1x == GRIB_NOTDEF || data1y == GRIB_NOTDEF ||
          data2x == GRIB_NOTDEF || data2y == GRIB_NOTDEF) {
        datax[in] = GRIB_NOTDEF;
        datay[in] = GRIB_NOTDEF;
      } else {
        double data1m = sqrt(pow(data1x, 2) + pow(data1y, 2));
        double data2m = sqrt(pow(data2x, 2) + pow(data2y, 2));
        double datam = (1 - d) * data1m + d * data2m;

        double data1a = atan2(data1y, data1x);
        double data2a = atan2(data2y, data2x);
        if (data1a - data2a > kPi)
          data1a -= 2 * kPi;
        else if (data2a - data1a > kPi)
          data2a -= 2 * kPi;
        double dataa = (1 - d) * data1a + d * data2a;

        datax[in] = datam * cos(dataa);
        datay[in] = datam * sin(dataa);
      }
    }
  }

  /* should maybe update strCurDate ? */

  std::unique_ptr<GribRecord> ret(new GribRecord);
  ret->copyMetadata(rec1x);

  ret->Di = Di, ret->Dj = Dj;
  ret->Ni = Ni, ret->Nj = Nj;

  ret->La1 = La1, ret->La2 = La2;
  ret->Lo1 = Lo1, ret->Lo2 = Lo2;

  ret->data = valuesX.release();
  ret->BMSsize = 0;
  ret->BMSbits = nullptr;
  ret->hasBMS = false;  // I don't think wind or current ever use BMS correct?

  ret->latMin = std::min(La1, La2), ret->latMax = std::max(La1, La2);
  ret->lonMin = Lo1, ret->lonMax = Lo2;

  std::unique_ptr<GribRecord> y(new GribRecord);
  y->copyMetadata(*ret);
  y->dataType = rec1y.dataType;
  y->dataKey = makeKey(y->dataType, y->levelType, y->levelValue);
  y->data = valuesY.release();
  y->BMSbits = nullptr;
  y->hasBMS = false;
  rety = y.release();

  return ret.release();
}

GribRecord *GribRecord::MagnitudeRecord(const GribRecord &rec1,
                                        const GribRecord &rec2) {
  GribRecord *rec = new GribRecord(rec1);

  /* generate a record which is the combined magnitude of two records */
  if (rec1.data && rec2.data && rec1.Ni == rec2.Ni && rec1.Nj == rec2.Nj) {
    const std::size_t size = rec1.dataCount();
    for (int i = 0; i < size; i++)
      if (rec1.data[i] == GRIB_NOTDEF || rec2.data[i] == GRIB_NOTDEF)
        rec->data[i] = GRIB_NOTDEF;
      else
        rec->data[i] = sqrt(pow(rec1.data[i], 2) + pow(rec2.data[i], 2));
  } else
    rec->ok = false;

  if (rec1.BMSbits != nullptr && rec2.BMSbits != nullptr) {
    if (rec1.BMSsize == rec2.BMSsize) {
      int size = rec1.BMSsize;
      for (int i = 0; i < size; i++)
        rec->BMSbits[i] = rec1.BMSbits[i] & rec2.BMSbits[i];
    } else
      rec->ok = false;
  }

  return rec;
}

void GribRecord::Polar2UV(GribRecord *pDIR, GribRecord *pSPEED) {
  if (!pDIR || !pSPEED) return;
  if (pDIR->data && pSPEED->data && pDIR->Ni == pSPEED->Ni &&
      pDIR->Nj == pSPEED->Nj) {
    const std::size_t size = pDIR->dataCount();
    for (int i = 0; i < size; i++) {
      if (pDIR->data[i] != GRIB_NOTDEF && pSPEED->data[i] != GRIB_NOTDEF) {
        double dir = pDIR->data[i];
        double speed = pSPEED->data[i];
        pDIR->data[i] = -speed * sin(dir * kPi / 180.);
        pSPEED->data[i] = -speed * cos(dir * kPi / 180.);
      }
    }
    if (pDIR->dataType == GRB_WIND_DIR) {
      pDIR->dataType = GRB_WIND_VX;
      pSPEED->dataType = GRB_WIND_VY;
    } else {
      pDIR->dataType = GRB_UOGRD;
      pSPEED->dataType = GRB_VOGRD;
    }
  }
}

void GribRecord::Substract(const GribRecord &rec, bool pos) {
  // for now only substract records of same size
  if (rec.data == 0 || !rec.isOk()) return;

  if (data == 0 || !isOk()) return;

  if (Ni != rec.Ni || Nj != rec.Nj) return;

  const std::size_t size = dataCount();
  for (zuint i = 0; i < size; i++) {
    if (rec.data[i] == GRIB_NOTDEF) continue;
    if (data[i] == GRIB_NOTDEF) {
      data[i] = -rec.data[i];
      if (BMSbits != 0) {
        if (BMSsize > (i >> 3)) {
          BMSbits[i >> 3] |= 128u >> (i & 7);
        }
      }
    } else
      data[i] -= rec.data[i];
    if (data[i] < 0. && pos) {
      // data type should be positive...
      data[i] = 0.;
    }
  }
}

//------------------------------------------------------------------------------
void GribRecord::Average(const GribRecord &rec) {
  // for now only average records of same size
  // this : 6-12
  // rec  : 6-9
  // compute average 9-12
  //
  // this : 0-12
  // rec  : 0-11
  // compute average 11-12

  if (rec.data == 0 || !rec.isOk()) return;

  if (data == 0 || !isOk()) return;

  if (Ni != rec.Ni || Nj != rec.Nj) return;

  if (getPeriodP1() != rec.getPeriodP1()) return;

  double d2 = double(periodP2) - periodP1;
  double d1 = double(rec.periodP2) - rec.periodP1;

  if (d2 <= d1) return;

  const std::size_t size = dataCount();
  double diff = d2 - d1;
  for (zuint i = 0; i < size; i++) {
    if (rec.data[i] == GRIB_NOTDEF) continue;
    if (data[i] == GRIB_NOTDEF) continue;

    data[i] = (data[i] * d2 - rec.data[i] * d1) / diff;
  }
}

//-------------------------------------------------------------------------------
void GribRecord::setDataType(const zuchar t) {
  dataType = t;
  dataKey = makeKey(dataType, levelType, levelValue);
}
//------------------------------------------------------------------------------
std::string GribRecord::makeKey(int dataType, int levelType, int levelValue) {
  char key[64];
  std::snprintf(key, sizeof(key), "%d-%d-%d", dataType, levelType, levelValue);
  return std::string(key);
}
//-----------------------------------------
// GribRecord::~GribRecord() {
//  if (data) {
//    delete[] data;
//    data = nullptr;
//  }
//  if (BMSbits) {
//    delete[] BMSbits;
//    BMSbits = nullptr;
//  }

  // if (dataType==GRB_TEMP) printf("record destroyed %s   %d\n",
  // dataKey.mb_str(), (int)curDate/3600);
//}

//-------------------------------------------------------------------------------
void GribRecord::multiplyAllData(double k) {
  if (data == 0 || !isOk() || !dataCount()) return;

  for (zuint j = 0; j < Nj; j++) {
    for (zuint i = 0; i < Ni; i++) {
      if (isDefined(i, j)) {
        data[j * Ni + i] *= k;
      }
    }
  }
}

//----------------------------------------------
void GribRecord::setRecordCurrentDate(time_t t) {
  curDate = t;
  struct tm date;
#ifdef _WIN32
  const bool valid = gmtime_s(&date, &t) == 0;
#else
  const bool valid = gmtime_r(&t, &date) != nullptr;
#endif
  if (!valid) { strCurDate[0] = '\0'; return; }
  std::snprintf(strCurDate, sizeof(strCurDate), "%04d-%02d-%02d %02d:%02d",
                date.tm_year + 1900, date.tm_mon + 1, date.tm_mday,
                date.tm_hour, date.tm_min);
}

//----------------------------------------------
static bool isleapyear(zuint y) {
  return ((y % 4 == 0) && (y % 100 != 0)) || (y % 400 == 0);
}

time_t GribRecord::makeDate(zuint year, zuint month, zuint day, zuint hour,
                            zuint min, zuint sec) {
  static const unsigned monthDays[] = {31,28,31,30,31,30,31,31,30,31,30,31};
  static const unsigned daysBeforeMonth[] = {0,31,59,90,120,151,181,212,243,273,304,334};
  if (year < 1970 || year > 2200 || month < 1 || month > 12 || day < 1 ||
      day > monthDays[month-1] + (month == 2 && isleapyear(year)) ||
      hour > 23 || min > 59) return time_t(-1);
  // The GRIB readers pass the whole forecast offset in sec, not just a
  // clock second. Keep that established calling convention.
  const unsigned y = year - 1;
  const unsigned leapDays = y/4 - y/100 + y/400 - (1969/4 - 1969/100 + 1969/400);
  const std::uint64_t days = std::uint64_t(year - 1970) * 365 + leapDays +
      daysBeforeMonth[month-1] + (month > 2 && isleapyear(year)) + day - 1;
  const std::uint64_t seconds = days * 86400 + hour * 3600 + min * 60 + sec;
  if (seconds > std::uint64_t(std::numeric_limits<time_t>::max())) return time_t(-1);
  return static_cast<time_t>(seconds);
}

//===============================================================================================

double GribRecord::getInterpolatedValue(double px, double py,
                                        bool numericalInterpolation,
                                        bool dir) const {
  // The finite grid-coordinate checks below validate the query and spacing
  // before conversion. Only ownership/count need checking here.
  if (!ok || !data || !dataCount()) return GRIB_NOTDEF;

  // Validate in grid coordinates once. The common interior path needs no
  // longitude seam calculation or repeated geographic coverage checks.
  if (!isYInMap(py)) return GRIB_NOTDEF;
  double pi = (px - Lo1) / Di;
  const double pj = (py - La1) / Dj;
  bool wraps = false;
  if (!(pi >= 0 && pi <= Ni - 1.0)) {
    wraps = std::abs(std::abs(Di) * Ni - 360.0) <= 1e-7;
    if (!(wraps && pi >= 0 && pi <= Ni)) {
      pi = (px + 360.0 - Lo1) / Di;
      if (!(pi >= 0 && pi < Ni))
        pi = (px - 360.0 - Lo1) / Di;
    }
    if (wraps && pi == Ni) pi = 0;
    if (!wraps && pi > Ni - 1.0) return GRIB_NOTDEF;
  }
  // Ordered bounds comparisons also reject NaN and infinity before casts.
  if (!(pi >= 0 && pi < Ni && pj >= 0 && pj < Nj)) return GRIB_NOTDEF;
  int i0 = static_cast<int>(pi);  // point 00
  int j0 = static_cast<int>(pj);

  unsigned int i1 = i0 + 1, j1 = j0 + 1;

  if (i1 >= Ni) {
    wraps = std::abs(std::abs(Di) * Ni - 360.0) <= 1e-7;
    i1 = wraps ? 0 : i0;
  }

  if (j1 >= Nj) j1 = j0;

  // distances to 00
  double dx = pi - i0;
  double dy = pj - j0;

  if (!numericalInterpolation) {
    if (dx >= 0.5) i0 = i1;
    if (dy >= 0.5) j0 = j1;

    return getValue(i0,j0);
  }

  // Both GRIB decoders expand missing bitmap cells to GRIB_NOTDEF.
  // Read that canonical data representation, avoiding a second bitmap scan.
  const double x00 = getValue(i0, j0), x01 = getValue(i0, j1);
  const double x10 = getValue(i1, j0), x11 = getValue(i1, j1);
  dx = (3.0 - 2.0 * dx) * dx * dx;  // pseudo hermite interpolation
  dy = (3.0 - 2.0 * dy) * dy * dy;

  double xa, xb, xc, kx, ky;
  // Triangle :
  //   xa  xb
  //   xc
  // kx = distance(xa,x)
  // ky = distance(xa,y)
  if (x00 != GRIB_NOTDEF && x01 != GRIB_NOTDEF &&
      x10 != GRIB_NOTDEF && x11 != GRIB_NOTDEF) {
    if (!dir) {
      double x1 = (1.0 - dx) * x00 + dx * x10;
      double x2 = (1.0 - dx) * x01 + dx * x11;
      return (1.0 - dy) * x1 + dy * x2;
    } else {
      double x1 = interp_angle(x00, x01, dx, 180.);
      double x2 = interp_angle(x10, x11, dx, 180.);
      return interp_angle(x1, x2, dy, 180.);
    }
  }

  const int nbval = (x00 != GRIB_NOTDEF) + (x01 != GRIB_NOTDEF) +
                    (x10 != GRIB_NOTDEF) + (x11 != GRIB_NOTDEF);
  if (nbval < 3) return GRIB_NOTDEF;

  // interpolation with only three points is too hazardous for angles
  if (dir) return GRIB_NOTDEF;

  // here nbval==3, check the corner without data
  if (x00 == GRIB_NOTDEF) {
    // printf("! h00  %f %f\n", dx,dy);
    xa = x11;  // A = point 11
    xb = x01;  // B = point 01
    xc = x10;  // C = point 10
    kx = 1 - dx;
    ky = 1 - dy;
  } else if (x01 == GRIB_NOTDEF) {
    // printf("! h01  %f %f\n", dx,dy);
    xa = x10;  // A = point 10
    xb = x11;  // B = point 11
    xc = x00;  // C = point 00
    kx = dy;
    ky = 1 - dx;
  } else if (x10 == GRIB_NOTDEF) {
    // printf("! h10  %f %f\n", dx,dy);
    xa = x01;  // A = point 01
    xb = x00;  // B = point 00
    xc = x11;  // C = point 11
    kx = 1 - dy;
    ky = dx;
  } else {
    // printf("! h11  %f %f\n", dx,dy);
    xa = x00;  // A = point 00
    xb = x10;  // B = point 10
    xc = x01;  // C = point 01
    kx = dx;
    ky = dy;
  }

  double k = kx + ky;
  if (k < 0 || k > 1) return GRIB_NOTDEF;

  if (k == 0) return xa;

  // axes interpolation
  double vx = k * xb + (1 - k) * xa;
  double vy = k * xc + (1 - k) * xa;
  // diagonal interpolation
  double k2 = kx / k;
  return k2 * vx + (1 - k2) * vy;
}

bool GribRecord::getInterpolatedValues(double &M, double &A,
                                       const GribRecord *GRX,
                                       const GribRecord *GRY, double px,
                                       double py, bool numericalInterpolation) {
  if (!GRX || !GRY) return false;

  if (!GRX->ok || !GRY->ok || !GRX->data || !GRY->data ||
      !GRX->dataCount() || !GRX->sameGrid(*GRY))
    return false;

  // Validate in grid coordinates once. The common interior path needs no
  // longitude seam calculation or repeated geographic coverage checks.
  if (!GRX->isYInMap(py)) return false;
  double pi = (px - GRX->Lo1) / GRX->Di;
  const double pj = (py - GRX->La1) / GRX->Dj;
  bool wraps = false;
  if (!(pi >= 0 && pi <= GRX->Ni - 1.0)) {
    wraps = std::abs(std::abs(GRX->Di) * GRX->Ni - 360.0) <= 1e-7;
    if (!(wraps && pi >= 0 && pi <= GRX->Ni)) {
      pi = (px + 360.0 - GRX->Lo1) / GRX->Di;
      if (!(pi >= 0 && pi < GRX->Ni))
        pi = (px - 360.0 - GRX->Lo1) / GRX->Di;
    }
    if (wraps && pi == GRX->Ni) pi = 0;
    if (!wraps && pi > GRX->Ni - 1.0) return false;
  }
  // Ordered bounds comparisons also reject NaN and infinity before casts.
  if (!(pi >= 0 && pi < GRX->Ni && pj >= 0 && pj < GRX->Nj)) return false;
  int i0 = static_cast<int>(pi);  // point 00
  int j0 = static_cast<int>(pj);

  unsigned int i1 = i0 + 1, j1 = j0 + 1;
  if (i1 >= GRX->Ni) {
    wraps = std::abs(std::abs(GRX->Di) * GRX->Ni - 360.0) <= 1e-7;
    i1 = wraps ? 0 : i0;
  }

  if (j1 >= GRX->Nj) j1 = j0;

  // distances to 00
  double dx = pi - i0;
  double dy = pj - j0;

  if (!numericalInterpolation) {
    double vx, vy;
    if (dx >= 0.5) i0 = i1;
    if (dy >= 0.5) j0 = j1;

    vx = GRX->getValue(i0,j0);
    vy = GRY->getValue(i0,j0);
    if (vx == GRIB_NOTDEF || vy == GRIB_NOTDEF) return false;

    M = sqrt(vx * vx + vy * vy);
    A = atan2(-vx, -vy) * 180 / kPi;
    return true;
  }

  const double x00x = GRX->getValue(i0, j0), x00y = GRY->getValue(i0, j0);
  const double x01x = GRX->getValue(i0, j1), x01y = GRY->getValue(i0, j1);
  const double x10x = GRX->getValue(i1, j0), x10y = GRY->getValue(i1, j0);
  const double x11x = GRX->getValue(i1, j1), x11y = GRY->getValue(i1, j1);
  if (x00x == GRIB_NOTDEF || x01x == GRIB_NOTDEF || x10x == GRIB_NOTDEF ||
      x11x == GRIB_NOTDEF || x00y == GRIB_NOTDEF || x01y == GRIB_NOTDEF ||
      x10y == GRIB_NOTDEF || x11y == GRIB_NOTDEF) return false;

  dx = (3.0 - 2.0 * dx) * dx * dx;  // pseudo hermite interpolation
  dy = (3.0 - 2.0 * dy) * dy * dy;

  // Triangle :
  //   xa  xb
  //   xc
  // kx = distance(xa,x)
  // ky = distance(xa,y)
  {
    double x00m = sqrt(x00x * x00x + x00y * x00y), x00a = atan2(x00x, x00y);

    double x01m = sqrt(x01x * x01x + x01y * x01y), x01a = atan2(x01x, x01y);

    double x10m = sqrt(x10x * x10x + x10y * x10y), x10a = atan2(x10x, x10y);

    double x11m = sqrt(x11x * x11x + x11y * x11y), x11a = atan2(x11x, x11y);

    double x0m = (1 - dx) * x00m + dx * x10m,
           x0a = interp_angle(x00a, x10a, dx, kPi);

    double x1m = (1 - dx) * x01m + dx * x11m,
           x1a = interp_angle(x01a, x11a, dx, kPi);

    M = (1 - dy) * x0m + dy * x1m;
    A = interp_angle(x0a, x1a, dy, kPi);
    A *= 180 / kPi;  // degrees
    A += 180;

    return true;
  }

  return false;  // TODO: make this work in the cases of only 3 points
#if 0
        double xa, xb, xc, kx, ky;
        // here nbval==3, check the corner without data
        if (!h00) {
            //printf("! h00  %f %f\n", dx,dy);
            xa = getValue(i1, j1);   // A = point 11
            xb = getValue(i0, j1);   // B = point 01
            xc = getValue(i1, j0);   // C = point 10
            kx = 1-dx;
            ky = 1-dy;
        }
        else if (!h01) {
            //printf("! h01  %f %f\n", dx,dy);
            xa = getValue(i1, j0);     // A = point 10
            xb = getValue(i1, j1);   // B = point 11
            xc = getValue(i0, j0);     // C = point 00
            kx = dy;
            ky = 1-dx;
        }
        else if (!h10) {
            //printf("! h10  %f %f\n", dx,dy);
            xa = getValue(i0, j1);     // A = point 01
            xb = getValue(i0, j0);       // B = point 00
            xc = getValue(i1, j1);     // C = point 11
            kx = 1-dy;
            ky = dx;
        }
        else {
            //printf("! h11  %f %f\n", dx,dy);
            xa = getValue(i0, j0);  // A = point 00
            xb = getValue(i1, j0);  // B = point 10
            xc = getValue(i0, j1);  // C = point 01
            kx = dx;
            ky = dy;
        }
    }
    double k = kx + ky;
    if (k<0 || k>1) {
        val = GRIB_NOTDEF;
    }
    else if (k == 0) {
        val = xa;
    }
    else {
        // axes interpolation
        double vx = k*xb + (1-k)*xa;
        double vy = k*xc + (1-k)*xa;
        // diagonal interpolation
        double k2 = kx / k;
        val =  k2*vx + (1-k2) * vy;
    }
    return val;
#endif
}
