// SPDX-License-Identifier: GPL-3.0-or-later
// Adapted from the existing offline benchmark provider in
// benchmarks/weather-routing/environment.cpp (OpenCPN workspace).
#pragma once
#include "Fixtures.h"
#include <eccodes.h>
#include <cstdio>
#include <ctime>
#include <stdexcept>
#include <memory>
namespace fixture {
struct Field {
  int ni{}, nj{};
  double lat{}, lon{}, di{}, dj{};
  std::vector<double> values;
  double at(wr::GeoPoint p) const {
    if (ni < 2 || nj < 2 || di == 0 || dj == 0) return NAN;
    double x = wr::normalizeLongitude(p.longitude - lon) / di,
           y = (p.latitude - lat) / dj;
    if (x < 0 && ni * di >= 359) x += 360 / di;
    if (x < 0 || y < 0 || x > ni - 1 || y > nj - 1) return NAN;
    int i = std::min(int(x), ni - 2), k = std::min(int(y), nj - 2);
    double a = x - i, b = y - k;
    double v[4] = {values[k * ni + i], values[k * ni + i + 1],
                   values[(k + 1) * ni + i], values[(k + 1) * ni + i + 1]};
    for (double z : v)
      if (!std::isfinite(z) || std::abs(z) > 1e10) return NAN;
    return (v[0] * (1 - a) + v[1] * a) * (1 - b) +
           (v[2] * (1 - a) + v[3] * a) * b;
  }
};
class GribWeather final : public wr::WeatherProvider {
  std::map<long long, Field> fields[4];
  double sample(int n, wr::GeoPoint p, wr::TimePoint t) const {
    auto& f = fields[n];
    auto hi = f.lower_bound(t.time_since_epoch().count());
    if (hi == f.end()) return NAN;
    double b = hi->second.at(p);
    if (hi->first == t.time_since_epoch().count()) return b;
    if (hi == f.begin()) return NAN;
    auto lo = std::prev(hi);
    double a = lo->second.at(p);
    return a + (b - a) * double(t.time_since_epoch().count() - lo->first) /
                   (hi->first - lo->first);
  }
  wr::ParameterCoverage coverage(int n) const {
    wr::ParameterCoverage c;
    c.available = !fields[n].empty();
    c.area = {-180, -90, 180, 90};
    if (c.available) {
      c.begins = wr::TimePoint(wr::Duration(fields[n].begin()->first));
      c.ends = wr::TimePoint(wr::Duration(fields[n].rbegin()->first));
      const auto& f = fields[n].begin()->second;
      double lastLat = f.lat + (f.nj - 1) * f.dj,
             lastLon = f.lon + (f.ni - 1) * f.di;
      c.area = {wr::normalizeLongitude(std::min(f.lon, lastLon)),
                std::min(f.lat, lastLat),
                wr::normalizeLongitude(std::max(f.lon, lastLon)),
                std::max(f.lat, lastLat)};
    }
    return c;
  }

public:
  explicit GribWeather(const std::string& path) {
    struct CloseFile {
      void operator()(FILE* file) const { fclose(file); }
    };
    std::unique_ptr<FILE, CloseFile> fp(fopen(path.c_str(), "rb"));
    if (!fp) throw std::runtime_error("Cannot open GRIB");
    int err = 0;
    codes_handle* h;
    auto number = [](codes_handle* h, const char* key) {
      double v;
      if (codes_get_double(h, key, &v))
        throw std::runtime_error(std::string("Missing GRIB key ") + key);
      return v;
    };
    while ((h = codes_handle_new_from_file(nullptr, fp.get(), PRODUCT_GRIB,
                                           &err))) {
      std::unique_ptr<codes_handle, decltype(&codes_handle_delete)> handle(
          h, codes_handle_delete);
      char name[100]{};
      size_t len = sizeof(name);
      if (codes_get_string(h, "shortName", name, &len))
        throw std::runtime_error("GRIB shortName unavailable");
      int n = std::string(name) == "10u"   ? 0
              : std::string(name) == "10v" ? 1
                                           : -1;
      long ed = 0, param = 0;
      codes_get_long(h, "edition", &ed);
      codes_get_long(h, "indicatorOfParameter", &param);
      if (ed == 1 && param == 49) n = 2;
      if (ed == 1 && param == 50) n = 3;
      if (n >= 0) {
        Field f;
        f.ni = number(h, "Ni");
        f.nj = number(h, "Nj");
        f.lat = number(h, "latitudeOfFirstGridPointInDegrees");
        f.lon = number(h, "longitudeOfFirstGridPointInDegrees");
        f.di = number(h, "iDirectionIncrementInDegrees") *
               (number(h, "iScansNegatively") ? -1 : 1);
        f.dj = number(h, "jDirectionIncrementInDegrees") *
               (number(h, "jScansPositively") ? 1 : -1);
        if (number(h, "jPointsAreConsecutive") ||
            (ed == 2 && number(h, "alternativeRowScanning")))
          throw std::runtime_error("Unsupported GRIB scan mode");
        size_t count = 0;
        if (codes_get_size(h, "values", &count) || f.ni < 2 || f.nj < 2 ||
            count != std::uint64_t(f.ni) * f.nj)
          throw std::runtime_error("Unsupported or inconsistent GRIB grid");
        f.values.resize(count);
        if (codes_get_double_array(h, "values", f.values.data(), &count))
          throw std::runtime_error("GRIB values unavailable");
        double missing = number(h, "missingValue");
        for (double& v : f.values)
          if (v == missing)
            v = NAN;
          else
            v *= 1.9438444924406;
        int date = number(h, "validityDate"), time = number(h, "validityTime");
        std::tm tm{};
        tm.tm_year = date / 10000 - 1900;
        tm.tm_mon = date / 100 % 100 - 1;
        tm.tm_mday = date % 100;
        tm.tm_hour = time / 100;
        tm.tm_min = time % 100;
        fields[n].emplace(timegm(&tm), std::move(f));
      }
    }
    if (err && err != CODES_END_OF_FILE)
      throw std::runtime_error("GRIB decoding error");
    if (fields[0].empty() || fields[1].empty())
      throw std::runtime_error("GRIB has no 10m wind");
  }
  wr::ParameterCoverage windCoverage() const override { return coverage(0); }
  wr::ParameterCoverage currentCoverage() const override { return coverage(2); }
  wr::ParameterCoverage waveCoverage() const override { return {}; }
  wr::WindSample wind(wr::GeoPoint p, wr::TimePoint t) const override {
    double u = sample(0, p, t), v = sample(1, p, t);
    return {std::isfinite(u) && std::isfinite(v),
            {u, v},
            {wr::EnvironmentalSource::GribForecast}};
  }
  wr::CurrentSample current(wr::GeoPoint p, wr::TimePoint t) const override {
    double u = sample(2, p, t), v = sample(3, p, t);
    return {std::isfinite(u) && std::isfinite(v),
            {u, v},
            {wr::EnvironmentalSource::GribForecast}};
  }
  wr::WaveSample waves(wr::GeoPoint, wr::TimePoint) const override {
    return {};
  }
  std::string identity() const override { return "shared-offline-grib"; }
};
}  // namespace fixture
