#include "supercpn/weather_routing/Providers.h"

#include <algorithm>
#include <cmath>

#include "supercpn/weather_routing/Engine.h"

namespace supercpn::weather_routing {
namespace {
bool envelopeContains(const GeoEnvelope& area, GeoPoint point) {
  const bool latitude =
      point.latitude >= area.south && point.latitude <= area.north;
  if (!latitude) return false;
  const double lon = normalizeLongitude(point.longitude);
  if (area.west <= area.east) return lon >= area.west && lon <= area.east;
  return lon >= area.west || lon <= area.east;
}

double orientation(GeoPoint a, GeoPoint b, GeoPoint c) {
  const double bx = normalizeLongitude(b.longitude - a.longitude);
  const double cx = normalizeLongitude(c.longitude - a.longitude);
  return bx * (c.latitude - a.latitude) - (b.latitude - a.latitude) * cx;
}

bool segmentsIntersect(GeoPoint a, GeoPoint b, GeoPoint c, GeoPoint d) {
  const double o1 = orientation(a, b, c);
  const double o2 = orientation(a, b, d);
  const double o3 = orientation(c, d, a);
  const double o4 = orientation(c, d, b);
  constexpr double epsilon = 1e-12;
  return ((o1 > epsilon && o2 < -epsilon) || (o1 < -epsilon && o2 > epsilon)) &&
         ((o3 > epsilon && o4 < -epsilon) || (o3 < -epsilon && o4 > epsilon));
}

double pointToSegmentDistanceNm(GeoPoint point, GeoPoint start, GeoPoint end) {
  const double referenceLatitude =
      (point.latitude + start.latitude + end.latitude) / 3.0;
  const double longitudeScale =
      60.0 * std::max(0.01, std::cos(referenceLatitude *
                                     3.14159265358979323846 / 180.0));
  const double ax =
      normalizeLongitude(start.longitude - point.longitude) * longitudeScale;
  const double ay = (start.latitude - point.latitude) * 60.0;
  const double bx =
      normalizeLongitude(end.longitude - point.longitude) * longitudeScale;
  const double by = (end.latitude - point.latitude) * 60.0;
  const double dx = bx - ax;
  const double dy = by - ay;
  const double lengthSquared = dx * dx + dy * dy;
  if (lengthSquared <= 1e-18) return std::hypot(ax, ay);
  const double projection =
      std::clamp(-(ax * dx + ay * dy) / lengthSquared, 0.0, 1.0);
  return std::hypot(ax + projection * dx, ay + projection * dy);
}

constexpr double kBoundaryLatitudeBandDegrees = 0.02;
constexpr std::size_t kBoundaryLatitudeBandCount = 9001;
constexpr double kBoundaryLongitudeBandDegrees = 0.02;
constexpr std::size_t kBoundaryLongitudeBandCount = 18001;

std::size_t latitudeBand(double latitude) {
  const double clamped = std::clamp(latitude, -90.0, 90.0);
  return std::min(kBoundaryLatitudeBandCount - 1,
                  static_cast<std::size_t>(std::floor(
                      (clamped + 90.0) / kBoundaryLatitudeBandDegrees)));
}

std::size_t longitudeBand(double longitude) {
  const double clamped = std::clamp(longitude, -180.0, 180.0);
  return std::min(kBoundaryLongitudeBandCount - 1,
                  static_cast<std::size_t>(std::floor(
                      (clamped + 180.0) / kBoundaryLongitudeBandDegrees)));
}

std::uint32_t boundaryCellKey(std::size_t latitude, std::size_t longitude) {
  return static_cast<std::uint32_t>(latitude * kBoundaryLongitudeBandCount +
                                    longitude);
}

template <typename Callback>
void forLongitudeBandRange(double westUnwrapped, double eastUnwrapped,
                           Callback callback) {
  const double width = std::max(0.0, eastUnwrapped - westUnwrapped);
  if (width >= 360.0) {
    callback(std::size_t{0}, kBoundaryLongitudeBandCount - 1);
    return;
  }
  const double west = normalizeLongitude(westUnwrapped);
  const double east = west + width;
  callback(longitudeBand(west), longitudeBand(std::min(180.0, east)));
  if (east > 180.0)
    callback(longitudeBand(-180.0), longitudeBand(east - 360.0));
}
}  // namespace

UniformWeatherProvider::UniformWeatherProvider(Configuration configuration)
    : configuration_(std::move(configuration)) {}

ParameterCoverage UniformWeatherProvider::windCoverage() const {
  return {configuration_.windTowardKnots.has_value(), configuration_.begins,
          configuration_.ends, configuration_.area, configuration_.identity};
}
ParameterCoverage UniformWeatherProvider::currentCoverage() const {
  return {configuration_.currentTowardKnots.has_value(), configuration_.begins,
          configuration_.ends, configuration_.area, configuration_.identity};
}
ParameterCoverage UniformWeatherProvider::waveCoverage() const {
  return {configuration_.wave.has_value(), configuration_.begins,
          configuration_.ends, configuration_.area, configuration_.identity};
}

bool UniformWeatherProvider::covered(GeoPoint position, TimePoint time) const {
  return envelopeContains(configuration_.area, position) &&
         (!configuration_.begins || time >= *configuration_.begins) &&
         (!configuration_.ends || time <= *configuration_.ends);
}

WindSample UniformWeatherProvider::wind(GeoPoint position,
                                        TimePoint time) const {
  WindSample result;
  if (!configuration_.windTowardKnots || !covered(position, time))
    return result;
  result.available = true;
  result.velocity = *configuration_.windTowardKnots;
  result.metadata = {configuration_.source,
                     configuration_.identity,
                     configuration_.identity,
                     {},
                     time,
                     {},
                     {}};
  return result;
}

CurrentSample UniformWeatherProvider::current(GeoPoint position,
                                              TimePoint time) const {
  CurrentSample result;
  if (!configuration_.currentTowardKnots || !covered(position, time))
    return result;
  result.available = true;
  result.velocity = *configuration_.currentTowardKnots;
  result.metadata = {configuration_.source,
                     configuration_.identity,
                     configuration_.identity,
                     {},
                     time,
                     {},
                     {}};
  return result;
}

WaveSample UniformWeatherProvider::waves(GeoPoint position,
                                         TimePoint time) const {
  if (!configuration_.wave || !covered(position, time)) return {};
  WaveSample result = *configuration_.wave;
  result.available = true;
  result.metadata = {configuration_.source,
                     configuration_.identity,
                     configuration_.identity,
                     {},
                     time,
                     {},
                     {}};
  return result;
}

PolygonBoundaryProvider::PolygonBoundaryProvider(
    std::vector<std::vector<GeoPoint>> polygons, std::string identity)
    : polygons_(std::move(polygons)),
      edgeLatitudeBands_(kBoundaryLatitudeBandCount),
      identity_(std::move(identity)) {
  std::size_t edgeCount = 0;
  for (const auto& polygon : polygons_) edgeCount += polygon.size();
  edges_.reserve(edgeCount);
  for (std::size_t index = 0; index < polygons_.size(); ++index) {
    const auto& polygon = polygons_[index];
    if (polygon.size() < 2) continue;
    for (std::size_t vertex = 0; vertex < polygon.size(); ++vertex) {
      const GeoPoint edgeStart = polygon[vertex];
      const GeoPoint edgeEnd = polygon[(vertex + 1) % polygon.size()];
      const std::size_t edgeIndex = edges_.size();
      edges_.push_back({edgeStart, edgeEnd, index});
      const double edgeSouth = std::min(edgeStart.latitude, edgeEnd.latitude);
      const double edgeNorth = std::max(edgeStart.latitude, edgeEnd.latitude);
      const std::size_t firstLatitude = latitudeBand(edgeSouth);
      const std::size_t lastLatitude = latitudeBand(edgeNorth);
      for (std::size_t band = firstLatitude; band <= lastLatitude; ++band)
        edgeLatitudeBands_[band].push_back(edgeIndex);

      const double edgeWest = normalizeLongitude(edgeStart.longitude);
      const double edgeEast =
          edgeWest +
          normalizeLongitude(edgeEnd.longitude - edgeStart.longitude);
      const double west = std::min(edgeWest, edgeEast);
      const double east = std::max(edgeWest, edgeEast);
      forLongitudeBandRange(
          west, east,
          [&](std::size_t firstLongitude, std::size_t lastLongitude) {
            for (std::size_t latitude = firstLatitude; latitude <= lastLatitude;
                 ++latitude) {
              for (std::size_t longitude = firstLongitude;
                   longitude <= lastLongitude; ++longitude) {
                edgeCells_[boundaryCellKey(latitude, longitude)].push_back(
                    edgeIndex);
              }
            }
          });
    }
  }
}

std::vector<std::size_t> PolygonBoundaryProvider::edgeCandidatesForArea(
    double south, double north, double westUnwrapped,
    double eastUnwrapped) const {
  const std::size_t firstLatitude = latitudeBand(std::min(south, north));
  const std::size_t lastLatitude = latitudeBand(std::max(south, north));
  std::vector<std::size_t> candidates;
  forLongitudeBandRange(
      westUnwrapped, eastUnwrapped,
      [&](std::size_t firstLongitude, std::size_t lastLongitude) {
        for (std::size_t latitude = firstLatitude; latitude <= lastLatitude;
             ++latitude) {
          for (std::size_t longitude = firstLongitude;
               longitude <= lastLongitude; ++longitude) {
            const auto found =
                edgeCells_.find(boundaryCellKey(latitude, longitude));
            if (found != edgeCells_.end())
              candidates.insert(candidates.end(), found->second.begin(),
                                found->second.end());
          }
        }
      });
  std::sort(candidates.begin(), candidates.end());
  candidates.erase(std::unique(candidates.begin(), candidates.end()),
                   candidates.end());
  return candidates;
}

bool PolygonBoundaryProvider::pointForbidden(GeoPoint point) const {
  if (polygons_.empty()) return false;
  std::vector<unsigned char> parity(polygons_.size());
  const auto& candidates = edgeLatitudeBands_[latitudeBand(point.latitude)];
  for (const std::size_t edgeIndex : candidates) {
    const BoundaryEdge& edge = edges_[edgeIndex];
    const double startLongitude =
        normalizeLongitude(edge.start.longitude - point.longitude);
    const double endLongitude =
        normalizeLongitude(edge.end.longitude - point.longitude);
    if ((edge.start.latitude > point.latitude) ==
        (edge.end.latitude > point.latitude))
      continue;
    if (0.0 < (endLongitude - startLongitude) *
                      (point.latitude - edge.start.latitude) /
                      (edge.end.latitude - edge.start.latitude) +
                  startLongitude)
      parity[edge.polygon] ^= 1U;
  }
  return std::any_of(parity.begin(), parity.end(),
                     [](unsigned char inside) { return inside != 0U; });
}

bool PolygonBoundaryProvider::segmentForbidden(GeoPoint start, GeoPoint end,
                                               double safetyMarginNm) const {
  if (pointForbidden(start)) return true;
  return segmentFromKnownSafeForbidden(start, end, safetyMarginNm);
}

bool PolygonBoundaryProvider::segmentFromKnownSafeForbidden(
    GeoPoint start, GeoPoint end, double safetyMarginNm) const {
  const double latitudeMargin = std::max(0.0, safetyMarginNm) / 60.0;
  const double maximumLatitude = std::min(
      89.9, std::max(std::abs(start.latitude), std::abs(end.latitude)) +
                latitudeMargin);
  const double longitudeMargin = std::min(
      180.0, latitudeMargin /
                 std::max(0.01, std::cos(maximumLatitude *
                                         3.14159265358979323846 / 180.0)));
  const double startLongitude = normalizeLongitude(start.longitude);
  const double endLongitude =
      startLongitude + normalizeLongitude(end.longitude - start.longitude);
  const double queryWest =
      std::min(startLongitude, endLongitude) - longitudeMargin;
  const double queryEast =
      std::max(startLongitude, endLongitude) + longitudeMargin;
  const double querySouth =
      std::min(start.latitude, end.latitude) - latitudeMargin;
  const double queryNorth =
      std::max(start.latitude, end.latitude) + latitudeMargin;
  const auto candidates =
      edgeCandidatesForArea(querySouth, queryNorth, queryWest, queryEast);
  const double leg = distanceNm(start, end);
  const unsigned samples =
      safetyMarginNm > 0.0
          ? std::max(2U, static_cast<unsigned>(
                             std::ceil(leg / std::max(0.1, safetyMarginNm))))
          : 0U;
  const double bearing = samples > 0 ? initialBearingDegrees(start, end) : 0.0;
  for (const std::size_t edgeIndex : candidates) {
    const BoundaryEdge& edge = edges_[edgeIndex];
    if (segmentsIntersect(start, end, edge.start, edge.end)) return true;
  }
  if (safetyMarginNm > 0.0) {
    // The caller has independently established that start is outside the
    // forbidden polygon. Do not make a safe harbour/coastal start impossible
    // merely because it lies inside the configured stand-off buffer. Every
    // interior sample and endpoint still carry the full margin, so only a
    // segment which clears the shoreline can leave that point; an approach to
    // the same point remains forbidden.
    for (unsigned i = 1; i <= samples; ++i) {
      const GeoPoint point =
          destinationPoint(start, bearing, leg * i / samples);
      for (const std::size_t edgeIndex : candidates)
        if (pointToSegmentDistanceNm(point, edges_[edgeIndex].start,
                                     edges_[edgeIndex].end) < safetyMarginNm)
          return true;
    }
  }
  return false;
}

double PolygonBoundaryProvider::distanceToForbiddenNm(GeoPoint point) const {
  if (pointForbidden(point)) return 0.0;
  double best = std::numeric_limits<double>::infinity();
  // Clearance is distance to the boundary, not to its vertices. A long, nearly
  // straight shoreline can place the closest vertices many miles away while
  // the vessel is only metres off the intervening edge. Besides reporting the
  // wrong clearance, vertex distance breaks the one-way coastal-egress rule.
  for (const BoundaryEdge& edge : edges_)
    best = std::min(best,
                    pointToSegmentDistanceNm(point, edge.start, edge.end));
  return best;
}

}  // namespace supercpn::weather_routing
