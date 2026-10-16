// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "Fixtures.h"
namespace fixture {
class InstrumentedWeather final : public wr::WeatherProvider {
public:
  std::shared_ptr<const wr::WeatherProvider> source;
  bool waterRelative{}, cacheEnabled{};
  mutable std::uint64_t calls{}, hits{};
  mutable std::uint64_t queryNanoseconds{};
  bool profileEnabled{};
  using Key = std::tuple<double, double, std::int64_t>;
  mutable std::map<Key, wr::EnvironmentalSnapshot> cache;
  mutable std::map<Key, unsigned> mask;
  explicit InstrumentedWeather(std::shared_ptr<const wr::WeatherProvider> p,
                               bool relative, bool cached, bool timed = false)
      : source(std::move(p)),
        waterRelative(relative),
        cacheEnabled(cached),
        profileEnabled(timed) {}
  wr::ParameterCoverage windCoverage() const override {
    return source->windCoverage();
  }
  wr::ParameterCoverage currentCoverage() const override {
    return source->currentCoverage();
  }
  wr::ParameterCoverage waveCoverage() const override {
    return source->waveCoverage();
  }
  Key key(wr::GeoPoint p, wr::TimePoint t) const {
    return {p.latitude, p.longitude, t.time_since_epoch().count()};
  }
  void bound() const {
    if (cache.size() >= 100000) {
      mask.erase(cache.begin()->first);
      cache.erase(cache.begin());
    }
  }
  wr::WindSample wind(wr::GeoPoint p, wr::TimePoint t) const override {
    QueryTimer timer(queryNanoseconds, profileEnabled);
    ++calls;
    const auto k = key(p, t);
    if (cacheEnabled && (mask[k] & 1)) {
      ++hits;
      return cache.at(k).wind;
    }
    auto result = source->wind(p, t);
    if (waterRelative) {
      auto c = source->current(p, t);
      if (c.available) {
        result.velocity.eastKnots -= c.velocity.eastKnots;
        result.velocity.northKnots -= c.velocity.northKnots;
      }
    }
    if (cacheEnabled) {
      bound();
      cache[k].wind = result;
      mask[k] |= 1;
    }
    return result;
  }
  wr::CurrentSample current(wr::GeoPoint p, wr::TimePoint t) const override {
    QueryTimer timer(queryNanoseconds, profileEnabled);
    ++calls;
    const auto k = key(p, t);
    if (cacheEnabled && (mask[k] & 2)) {
      ++hits;
      return cache.at(k).current;
    }
    auto result = source->current(p, t);
    if (cacheEnabled) {
      bound();
      cache[k].current = result;
      mask[k] |= 2;
    }
    return result;
  }
  wr::WaveSample waves(wr::GeoPoint p, wr::TimePoint t) const override {
    QueryTimer timer(queryNanoseconds, profileEnabled);
    ++calls;
    const auto k = key(p, t);
    if (cacheEnabled && (mask[k] & 4)) {
      ++hits;
      return cache.at(k).waves;
    }
    auto result = source->waves(p, t);
    if (cacheEnabled) {
      bound();
      cache[k].waves = result;
      mask[k] |= 4;
    }
    return result;
  }
  std::string identity() const override { return source->identity(); }
};

class InstrumentedPerformance final : public wr::VesselPerformanceModel {
public:
  wr::PolarPerformanceModel source;
  mutable std::uint64_t calls{};
  mutable std::uint64_t queryNanoseconds{};
  bool profileEnabled{};
  explicit InstrumentedPerformance(const wr::VesselConfiguration& v,
                                   bool timed = false)
      : source(v), profileEnabled(timed) {}
  bool valid(std::string* reason) const override {
    return source.valid(reason);
  }
  std::optional<double> bestSailingSpeedAt(wr::GeoPoint p, wr::TimePoint t,
                                           double s, const wr::WaveSample& w,
                                           double a, double b) const override {
    QueryTimer timer(queryNanoseconds, profileEnabled);
    ++calls;
    return source.bestSailingSpeedAt(p, t, s, w, a, b);
  }
  std::vector<wr::PerformanceCandidate> candidates(
      double s, double a, const wr::WaveSample& w, wr::PropulsionMode m,
      wr::Duration d) const override {
    QueryTimer timer(queryNanoseconds, profileEnabled);
    ++calls;
    return source.candidates(s, a, w, m, d);
  }
  wr::PerformanceCandidate evaluate(wr::PropulsionMode m, wr::ProfileRole r,
                                    const std::string& id, double s, double a,
                                    const wr::WaveSample& w) const override {
    QueryTimer timer(queryNanoseconds, profileEnabled);
    ++calls;
    return source.evaluate(m, r, id, s, a, w);
  }
};
}  // namespace fixture
