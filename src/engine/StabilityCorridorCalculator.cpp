/***************************************************************************
 *   Copyright (C) 2026 by OpenCPN contributors                            *
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 3 of the License, or     *
 *   (at your option) any later version.                                   *
 ***************************************************************************/

#include "weather_routing_engine/StabilityCorridor.h"

#include <wx/stopwatch.h>

#include <algorithm>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <limits>
#include <map>
#include <numbers>
#include <set>
#include <sstream>

namespace {

static const double kEarthNmPerDegree = 60.0;
static const int kResamplePoints = 64;

struct XYPoint {
  double x;
  double y;
};

struct Projection {
  double referenceLat;
  double lonScale;

  explicit Projection(double latitude)
      : referenceLat(latitude),
        lonScale(kEarthNmPerDegree *
                 std::max(0.05,
                          std::cos(latitude * std::numbers::pi_v<double> /
                                   180.0))) {}

  XYPoint ToXY(const weather_routing_engine::StabilityPoint& point) const {
    return {point.lon * lonScale, point.lat * kEarthNmPerDegree};
  }

  weather_routing_engine::StabilityPoint ToLatLon(double x, double y) const {
    return weather_routing_engine::StabilityPoint(y / kEarthNmPerDegree,
                                                  x / lonScale);
  }
};

double Distance(const XYPoint& a, const XYPoint& b) {
  return std::hypot(a.x - b.x, a.y - b.y);
}

std::vector<weather_routing_engine::StabilityPoint> Resample(
    const weather_routing_engine::StabilityRoute& route,
    const Projection& projection) {
  std::vector<weather_routing_engine::StabilityPoint> output;
  if (route.points.empty()) return output;
  if (route.points.size() == 1) {
    output.assign(kResamplePoints, route.points.front());
    return output;
  }

  std::vector<double> cumulative(route.points.size(), 0.0);
  for (size_t i = 1; i < route.points.size(); ++i)
    cumulative[i] =
        cumulative[i - 1] + Distance(projection.ToXY(route.points[i - 1]),
                                     projection.ToXY(route.points[i]));
  const double total = cumulative.back();
  if (total <= 1e-9) {
    output.assign(kResamplePoints, route.points.front());
    return output;
  }

  size_t segment = 1;
  for (int sample = 0; sample < kResamplePoints; ++sample) {
    const double target = total * sample / (kResamplePoints - 1);
    while (segment + 1 < cumulative.size() && cumulative[segment] < target)
      ++segment;
    const double length = cumulative[segment] - cumulative[segment - 1];
    const double fraction =
        length > 1e-9 ? (target - cumulative[segment - 1]) / length : 0.0;
    const XYPoint a = projection.ToXY(route.points[segment - 1]);
    const XYPoint b = projection.ToXY(route.points[segment]);
    output.push_back(projection.ToLatLon(a.x + fraction * (b.x - a.x),
                                         a.y + fraction * (b.y - a.y)));
  }
  return output;
}

struct RouteDistance {
  double average;
  double maximum;
  bool connectorsSafe;
};

RouteDistance CompareRoutes(
    const std::vector<weather_routing_engine::StabilityPoint>& a,
    const std::vector<weather_routing_engine::StabilityPoint>& b,
    const Projection& projection,
    const weather_routing_engine::StabilityCorridorCalculator::
        SegmentSafetyCheck& safety) {
  RouteDistance result = {0.0, 0.0, true};
  const size_t count = std::min(a.size(), b.size());
  if (!count) {
    result.average = result.maximum = std::numeric_limits<double>::infinity();
    result.connectorsSafe = false;
    return result;
  }
  for (size_t i = 0; i < count; ++i) {
    const double distance =
        Distance(projection.ToXY(a[i]), projection.ToXY(b[i]));
    result.average += distance;
    result.maximum = std::max(result.maximum, distance);
    if (safety && !safety(a[i], b[i])) result.connectorsSafe = false;
  }
  result.average /= count;
  return result;
}

double PointSegmentDistance(const XYPoint& point, const XYPoint& a,
                            const XYPoint& b) {
  const double dx = b.x - a.x;
  const double dy = b.y - a.y;
  const double length2 = dx * dx + dy * dy;
  if (length2 <= 1e-12) return Distance(point, a);
  const double t = std::max(
      0.0,
      std::min(1.0, ((point.x - a.x) * dx + (point.y - a.y) * dy) / length2));
  return std::hypot(point.x - (a.x + t * dx), point.y - (a.y + t * dy));
}

typedef std::pair<int, int> CellKey;

std::set<CellKey> RasterizeRoute(
    const weather_routing_engine::StabilityRoute& route,
    const Projection& projection, double resolution, double influenceRadius) {
  std::set<CellKey> cells;
  if (route.points.empty()) return cells;
  const int radiusCells =
      std::max(1, static_cast<int>(std::ceil(influenceRadius / resolution)));

  if (route.points.size() == 1) {
    const XYPoint point = projection.ToXY(route.points.front());
    cells.insert(CellKey(static_cast<int>(std::floor(point.x / resolution)),
                         static_cast<int>(std::floor(point.y / resolution))));
    return cells;
  }

  for (size_t segment = 1; segment < route.points.size(); ++segment) {
    const XYPoint a = projection.ToXY(route.points[segment - 1]);
    const XYPoint b = projection.ToXY(route.points[segment]);
    const int minX =
        static_cast<int>(std::floor(std::min(a.x, b.x) / resolution)) -
        radiusCells;
    const int maxX =
        static_cast<int>(std::floor(std::max(a.x, b.x) / resolution)) +
        radiusCells;
    const int minY =
        static_cast<int>(std::floor(std::min(a.y, b.y) / resolution)) -
        radiusCells;
    const int maxY =
        static_cast<int>(std::floor(std::max(a.y, b.y) / resolution)) +
        radiusCells;
    for (int y = minY; y <= maxY; ++y) {
      for (int x = minX; x <= maxX; ++x) {
        const XYPoint center = {(x + 0.5) * resolution, (y + 0.5) * resolution};
        if (PointSegmentDistance(center, a, b) <=
            influenceRadius + resolution * 0.71)
          cells.insert(CellKey(x, y));
      }
    }
  }
  return cells;
}

bool CellIsSafe(
    const CellKey& key, const Projection& projection, double resolution,
    const weather_routing_engine::StabilityCorridorCalculator::CellSafetyCheck&
        safety) {
  if (!safety) return true;
  const weather_routing_engine::StabilityPoint southwest =
      projection.ToLatLon(key.first * resolution, key.second * resolution);
  const weather_routing_engine::StabilityPoint northeast = projection.ToLatLon(
      (key.first + 1) * resolution, (key.second + 1) * resolution);
  return safety(southwest.lat, southwest.lon, northeast.lat, northeast.lon);
}

weather_routing_engine::StabilityCell MakeCell(const CellKey& key,
                                               const Projection& projection,
                                               double resolution,
                                               double agreement) {
  weather_routing_engine::StabilityCell cell;
  cell.x = key.first;
  cell.y = key.second;
  weather_routing_engine::StabilityPoint southwest =
      projection.ToLatLon(key.first * resolution, key.second * resolution);
  weather_routing_engine::StabilityPoint northeast = projection.ToLatLon(
      (key.first + 1) * resolution, (key.second + 1) * resolution);
  cell.minLat = std::min(southwest.lat, northeast.lat);
  cell.maxLat = std::max(southwest.lat, northeast.lat);
  cell.minLon = std::min(southwest.lon, northeast.lon);
  cell.maxLon = std::max(southwest.lon, northeast.lon);
  cell.agreement = agreement;
  return cell;
}

std::string EscapeJson(const wxString& value) {
  std::string input(value.ToUTF8().data());
  std::ostringstream output;
  for (unsigned char c : input) {
    switch (c) {
      case '\\':
        output << "\\\\";
        break;
      case '"':
        output << "\\\"";
        break;
      case '\n':
        output << "\\n";
        break;
      case '\r':
        output << "\\r";
        break;
      case '\t':
        output << "\\t";
        break;
      default:
        if (c < 0x20)
          output << "\\u" << std::hex << std::setw(4) << std::setfill('0')
                 << static_cast<int>(c) << std::dec;
        else
          output << c;
    }
  }
  return output.str();
}

}  // namespace

namespace weather_routing_engine {

StabilityCorridorOptions::StabilityCorridorOptions()
    : minimumRoutes(3),
      maxEtaPenaltyMinutes(120.0),
      gridResolutionNm(0.5),
      innerAgreementThreshold(0.7),
      outerAgreementThreshold(0.4),
      clusterRoutes(true),
      clusterDistanceNm(2.5),
      routeInfluenceRadiusNm(0.75) {}

StabilityCorridorResult StabilityCorridorCalculator::Calculate(
    const std::vector<StabilityRoute>& routes,
    const StabilityCorridorOptions& requestedOptions,
    const SegmentSafetyCheck& connectorSafetyCheck,
    const CellSafetyCheck& cellSafetyCheck) {
  wxStopWatch timer;
  StabilityCorridorResult result;
  result.inputRoutes = static_cast<int>(routes.size());

  StabilityCorridorOptions options = requestedOptions;
  options.minimumRoutes = std::max(1, options.minimumRoutes);
  options.gridResolutionNm = std::max(0.05, options.gridResolutionNm);
  options.routeInfluenceRadiusNm =
      std::max(options.gridResolutionNm, options.routeInfluenceRadiusNm);
  options.outerAgreementThreshold =
      std::max(0.0, std::min(1.0, options.outerAgreementThreshold));
  options.innerAgreementThreshold =
      std::max(options.outerAgreementThreshold,
               std::min(1.0, options.innerAgreementThreshold));

  std::vector<size_t> valid;
  long bestElapsed = std::numeric_limits<long>::max();
  double referenceLatSum = 0.0;
  size_t referencePoints = 0;
  for (size_t i = 0; i < routes.size(); ++i) {
    if (!routes[i].complete || !routes[i].finalValidationPass ||
        routes[i].points.size() < 2 || routes[i].elapsedSeconds < 0) {
      ++result.excludedRoutes;
      continue;
    }
    bestElapsed = std::min(bestElapsed, routes[i].elapsedSeconds);
    valid.push_back(i);
    for (const StabilityPoint& point : routes[i].points) {
      referenceLatSum += point.lat;
      ++referencePoints;
    }
  }

  if (bestElapsed != std::numeric_limits<long>::max() &&
      options.maxEtaPenaltyMinutes >= 0.0) {
    const long maximumElapsed =
        bestElapsed + static_cast<long>(options.maxEtaPenaltyMinutes * 60.0);
    valid.erase(
        std::remove_if(valid.begin(), valid.end(),
                       [&](size_t index) {
                         if (routes[index].elapsedSeconds <= maximumElapsed)
                           return false;
                         ++result.excludedRoutes;
                         ++result.etaExcludedRoutes;
                         return true;
                       }),
        valid.end());
  }
  result.validRoutes = static_cast<int>(valid.size());
  if (valid.size() < static_cast<size_t>(options.minimumRoutes)) {
    result.failureReason = "fewer than minimum completed validated routes";
    result.calculationTimeMs = timer.Time();
    return result;
  }

  const Projection projection(
      referencePoints ? referenceLatSum / referencePoints : 0.0);
  std::vector<std::vector<StabilityPoint> > resampled(routes.size());
  for (size_t index : valid)
    resampled[index] = Resample(routes[index], projection);

  std::map<std::pair<size_t, size_t>, RouteDistance> distances;
  for (size_t a = 0; a < valid.size(); ++a) {
    for (size_t b = a + 1; b < valid.size(); ++b) {
      distances[std::make_pair(valid[a], valid[b])] =
          CompareRoutes(resampled[valid[a]], resampled[valid[b]], projection,
                        connectorSafetyCheck);
    }
  }
  const auto distanceFor = [&](size_t a, size_t b) -> RouteDistance {
    if (a == b) return {0.0, 0.0, true};
    if (a > b) std::swap(a, b);
    return distances[std::make_pair(a, b)];
  };

  std::vector<std::vector<size_t> > clusters;
  for (size_t routeIndex : valid) {
    bool assigned = false;
    if (options.clusterRoutes) {
      for (std::vector<size_t>& cluster : clusters) {
        bool compatible = true;
        for (size_t member : cluster) {
          RouteDistance distance = distanceFor(routeIndex, member);
          if (!distance.connectorsSafe ||
              distance.average > options.clusterDistanceNm) {
            compatible = false;
            break;
          }
        }
        if (compatible) {
          cluster.push_back(routeIndex);
          assigned = true;
          break;
        }
      }
    } else if (!clusters.empty()) {
      clusters.front().push_back(routeIndex);
      assigned = true;
    }
    if (!assigned) clusters.push_back(std::vector<size_t>(1, routeIndex));
  }

  int familyId = 0;
  for (const std::vector<size_t>& cluster : clusters) {
    if (cluster.size() < static_cast<size_t>(options.minimumRoutes)) continue;
    RouteFamily family;
    family.id = familyId++;
    family.routeIndices = cluster;

    double bestAggregate = std::numeric_limits<double>::infinity();
    for (size_t candidate : cluster) {
      double aggregate = 0.0;
      for (size_t other : cluster)
        aggregate += distanceFor(candidate, other).average;
      if (aggregate < bestAggregate) {
        bestAggregate = aggregate;
        family.representativeRouteIndex = candidate;
      }
    }

    std::vector<double> widths;
    for (int sample = 0; sample < kResamplePoints; ++sample) {
      double sampleMaximum = 0.0;
      for (size_t a = 0; a < cluster.size(); ++a)
        for (size_t b = a + 1; b < cluster.size(); ++b)
          sampleMaximum = std::max(
              sampleMaximum,
              Distance(projection.ToXY(resampled[cluster[a]][sample]),
                       projection.ToXY(resampled[cluster[b]][sample])));
      widths.push_back(sampleMaximum);
    }
    std::sort(widths.begin(), widths.end());
    family.medianWidthNm = widths[widths.size() / 2];
    family.maximumWidthNm = widths.back();

    long earliestEta = std::numeric_limits<long>::max();
    long latestEta = std::numeric_limits<long>::min();
    for (size_t index : cluster) {
      if (!routes[index].eta.IsValid()) continue;
      const long eta = routes[index].eta.GetTicks();
      earliestEta = std::min(earliestEta, eta);
      latestEta = std::max(latestEta, eta);
    }
    if (earliestEta != std::numeric_limits<long>::max())
      family.etaSpreadMinutes = (latestEta - earliestEta) / 60.0;

    std::map<CellKey, int> usage;
    for (size_t index : cluster) {
      const std::set<CellKey> cells =
          RasterizeRoute(routes[index], projection, options.gridResolutionNm,
                         options.routeInfluenceRadiusNm);
      for (const CellKey& cell : cells) ++usage[cell];
    }
    result.rasterCellsUsed += static_cast<int>(usage.size());
    for (const auto& entry : usage) {
      const double agreement =
          static_cast<double>(entry.second) / cluster.size();
      if (agreement + 1e-12 < options.outerAgreementThreshold) continue;
      if (!CellIsSafe(entry.first, projection, options.gridResolutionNm,
                      cellSafetyCheck)) {
        ++result.unsafeCellsExcluded;
        continue;
      }
      const StabilityCell cell = MakeCell(entry.first, projection,
                                          options.gridResolutionNm, agreement);
      family.outerCells.push_back(cell);
      if (agreement + 1e-12 >= options.innerAgreementThreshold)
        family.innerCells.push_back(cell);
    }
    if (!family.outerCells.empty()) result.families.push_back(family);
  }

  if (result.families.empty()) {
    result.failureReason = "no route family met agreement and safety criteria";
    result.calculationTimeMs = timer.Time();
    return result;
  }
  result.success = true;
  result.calculationTimeMs = timer.Time();
  return result;
}

int StabilityCorridorCalculator::FindFamilyForRoute(
    const StabilityCorridorResult& result, size_t routeIndex) {
  for (const RouteFamily& family : result.families)
    if (std::find(family.routeIndices.begin(), family.routeIndices.end(),
                  routeIndex) != family.routeIndices.end())
      return family.id;
  return -1;
}

bool WriteStabilityCorridorGeoJson(const wxString& path,
                                   const std::vector<StabilityRoute>& routes,
                                   const StabilityCorridorResult& result,
                                   wxString& error) {
  std::ofstream output(path.mb_str(), std::ios::out | std::ios::trunc);
  if (!output.good()) {
    error = wxString::Format("cannot open stability GeoJSON: %s", path);
    return false;
  }
  output << std::fixed << std::setprecision(8);
  output << "{\n  \"type\": \"FeatureCollection\",\n  \"features\": [\n";
  bool firstFeature = true;
  const auto featureSeparator = [&]() {
    if (!firstFeature) output << ",\n";
    firstFeature = false;
  };
  const auto writeCell = [&](const StabilityCell& cell,
                             const RouteFamily& family, const char* band) {
    featureSeparator();
    output << "    {\"type\":\"Feature\",\"properties\":{"
           << "\"familyId\":" << family.id << ",\"band\":\"" << band
           << "\",\"agreement\":" << cell.agreement
           << ",\"routes\":" << family.routeIndices.size()
           << "},\"geometry\":{\"type\":\"Polygon\",\"coordinates\":[[["
           << cell.minLon << ',' << cell.minLat << "],[" << cell.maxLon << ','
           << cell.minLat << "],[" << cell.maxLon << ',' << cell.maxLat << "],["
           << cell.minLon << ',' << cell.maxLat << "],[" << cell.minLon << ','
           << cell.minLat << "]] ]}}";
  };

  for (const RouteFamily& family : result.families) {
    for (const StabilityCell& cell : family.outerCells)
      writeCell(cell, family, "outer");
    for (const StabilityCell& cell : family.innerCells)
      writeCell(cell, family, "inner");
    if (family.representativeRouteIndex >= routes.size()) continue;
    featureSeparator();
    const StabilityRoute& route = routes[family.representativeRouteIndex];
    output << "    {\"type\":\"Feature\",\"properties\":{\"familyId\":"
           << family.id << ",\"band\":\"medoid\",\"candidateId\":\""
           << EscapeJson(route.id)
           << "\"},\"geometry\":{\"type\":\"LineString\",\"coordinates\":[";
    for (size_t i = 0; i < route.points.size(); ++i) {
      if (i) output << ',';
      output << '[' << route.points[i].lon << ',' << route.points[i].lat << ']';
    }
    output << "]}}";
  }
  output << "\n  ]\n}\n";
  if (!output.good()) {
    error = wxString::Format("failed writing stability GeoJSON: %s", path);
    return false;
  }
  return true;
}

}  // namespace weather_routing_engine
