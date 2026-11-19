#include "OffshoreHazards.h"

#include <algorithm>
#include <cmath>
#include <numbers>
#include <set>
#include <stdexcept>

#include <json/json.h>

namespace weather_routing {
namespace {

constexpr double kEarthRadiusNm = 3440.065;  // Matches the routing geometry.
constexpr double kRadiansPerDegree = std::numbers::pi / 180.0;
constexpr double kAngularTolerance = 1e-12;
using Vector = std::array<double, 3>;

Vector UnitPosition(double latitude, double longitude) {
  const double lat = latitude * kRadiansPerDegree;
  const double lon = std::remainder(longitude, 360.0) * kRadiansPerDegree;
  const double horizontal = std::cos(lat);
  return {horizontal * std::cos(lon), horizontal * std::sin(lon),
          std::sin(lat)};
}

double Dot(const Vector& a, const Vector& b) {
  return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
}

Vector Cross(const Vector& a, const Vector& b) {
  return {a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2],
          a[0] * b[1] - a[1] * b[0]};
}

// The bundled JSON reader can accept an unrecognised token after a root
// object even in strict mode. Locate the end of the object before parsing.
bool HasSingleRootObject(std::string_view json) {
  constexpr std::string_view whitespace = " \t\r\n";
  auto i = json.find_first_not_of(whitespace);
  if (i == std::string_view::npos || json[i] != '{') return false;
  std::size_t depth = 0;
  bool quoted = false;
  for (; i < json.size(); ++i) {
    const char c = json[i];
    if (quoted) {
      if (c == '\\') ++i;
      else if (c == '"') quoted = false;
    } else if (c == '"') {
      quoted = true;
    } else if (c == '{') {
      ++depth;
    } else if (c == '}' && --depth == 0) {
      return json.find_first_not_of(whitespace, i + 1) == std::string_view::npos;
    }
  }
  return false;
}

std::string Text(const Json::Value& object, const char* key) {
  const auto& value = object[key];
  if (!value.isString())
    throw std::runtime_error(std::string("Missing offshore hazard field ") + key);
  auto text = value.asString();
  if (text.empty() || text.size() > 512 ||
      std::any_of(text.begin(), text.end(), [](unsigned char c) { return c < 32; }))
    throw std::runtime_error(std::string("Invalid offshore hazard field ") + key);
  return text;
}

double Number(const Json::Value& object, const char* key, double minimum,
              double maximum) {
  const auto& value = object[key];
  if (!value.isNumeric())
    throw std::runtime_error(std::string("Missing offshore hazard field ") + key);
  const double number = value.asDouble();
  if (!std::isfinite(number) || number < minimum || number > maximum)
    throw std::runtime_error(std::string("Invalid offshore hazard field ") + key);
  return number;
}

}  // namespace

std::shared_ptr<const OffshoreHazards> OffshoreHazards::Parse(
    std::string_view json) {
  if (json.empty() || json.size() > kMaxFileBytes)
    throw std::runtime_error("Invalid offshore hazard file size");

  if (!HasSingleRootObject(json))
    throw std::runtime_error("Invalid offshore hazard document");

  Json::CharReaderBuilder builder;
  Json::CharReaderBuilder::strictMode(&builder.settings_);
  builder.settings_["collectComments"] = false;
  builder.settings_["stackLimit"] = 16;
  const std::unique_ptr<Json::CharReader> reader(builder.newCharReader());
  Json::Value root;
  if (!reader->parse(json.data(), json.data() + json.size(), &root, nullptr) ||
      !root.isObject() || !root["schemaVersion"].isInt() ||
      root["schemaVersion"].asInt() != 1 || !root["hazards"].isArray() ||
      root["hazards"].empty())
    throw std::runtime_error("Invalid offshore hazard document or schema version");

  auto result = std::make_shared<OffshoreHazards>();
  result->name_ = Text(root, "dataset");
  result->revision_ = Text(root, "revision");
  std::set<std::string> ids;
  for (const auto& item : root["hazards"]) {
    if (!item.isObject() || item.size() != 6)
      throw std::runtime_error("Invalid offshore hazard exclusion record");
    Hazard hazard{Text(item, "id"), Text(item, "source"),
                  Number(item, "lat", -90.0, 90.0),
                  Number(item, "lon", -180.0, 180.0),
                  Number(item, "radiusNm", 0.0, 180.0),
                  Number(item, "uncertaintyNm", 0.0, 180.0)};
    if (hazard.radiusNm <= 0.0 || !ids.insert(hazard.id).second)
      throw std::runtime_error("Zero offshore exclusion radius or duplicate ID");
    const auto centre = UnitPosition(hazard.latitude, hazard.longitude);
    result->maximumRadiusNm_ = std::max(
        result->maximumRadiusNm_, hazard.radiusNm + hazard.uncertaintyNm);
    result->hazards_.push_back({std::move(hazard), centre});
  }
  std::stable_sort(result->hazards_.begin(), result->hazards_.end(),
                   [](const auto& a, const auto& b) {
                     return a.hazard.latitude < b.hazard.latitude;
                   });
  return result;
}

OffshoreHazards::Check OffshoreHazards::Intersect(
    double lat1, double lon1, double lat2, double lon2,
    double clearanceNm) const {
  if (hazards_.empty()) return {};
  if (!std::isfinite(lat1) || !std::isfinite(lat2) ||
      !std::isfinite(lon1) || !std::isfinite(lon2) ||
      !std::isfinite(clearanceNm) || std::abs(lat1) > 90.0 ||
      std::abs(lat2) > 90.0)
    return {nullptr, true};

  const double margin = std::max(0.0, clearanceNm);
  const double deltaLat = std::abs(lat2 - lat1);
  const double deltaLon = std::abs(std::remainder(lon2 - lon1, 360.0));
  // A meridian plus a parallel gives an upper bound on the great-circle arc.
  // The bound allows inexpensive latitude filtering even for an arc that bows
  // outside the interval between its endpoint latitudes.
  const double halo = (maximumRadiusNm_ + margin) /
                          (kEarthRadiusNm * kRadiansPerDegree) +
                      (deltaLat + deltaLon) / 2.0 + 1e-8;
  const auto first = std::lower_bound(
      hazards_.begin(), hazards_.end(), std::min(lat1, lat2) - halo,
      [](const auto& entry, double lat) { return entry.hazard.latitude < lat; });
  const double north = std::max(lat1, lat2) + halo;
  if (first == hazards_.end() || first->hazard.latitude > north) return {};

  const auto a = UnitPosition(lat1, lon1);
  const auto b = UnitPosition(lat2, lon2);
  auto normal = Cross(a, b);
  const double length = std::sqrt(Dot(normal, normal));
  const double alignment = Dot(a, b);
  // A nearly antipodal pair has an ambiguous minor arc.
  if (length <= kAngularTolerance && alignment < 0.0)
    return {nullptr, true};

  const bool point = length <= kAngularTolerance;
  Vector forwardAtStart{}, backAtEnd{};
  if (!point) {
    for (double& component : normal) component /= length;
    forwardAtStart = Cross(normal, a);
    backAtEnd = Cross(b, normal);
  }
  const Vector chordMidpoint{a[0] + b[0], a[1] + b[1], a[2] + b[2]};
  const double midpointLength = std::sqrt(Dot(chordMidpoint, chordMidpoint));
  const double halfArc = std::atan2(length, alignment) / 2.0;
  const double reach = std::min(
      std::numbers::pi,
      halfArc + (maximumRadiusNm_ + margin) / kEarthRadiusNm + kAngularTolerance);
  const double midpointThreshold = midpointLength * std::cos(reach) - 1e-14;

  for (auto entry = first;
       entry != hazards_.end() && entry->hazard.latitude <= north; ++entry) {
    const auto& hazard = entry->hazard;
    const auto& centre = entry->centre;
    if (Dot(chordMidpoint, centre) < midpointThreshold) continue;
    const double radius = (hazard.radiusNm + hazard.uncertaintyNm + margin) /
                              kEarthRadiusNm +
                          kAngularTolerance;
    const double endpointThreshold = std::cos(std::min(radius, std::numbers::pi));
    if (Dot(a, centre) >= endpointThreshold - 1e-14 ||
        Dot(b, centre) >= endpointThreshold - 1e-14)
      return {&hazard};

    // Project the centre onto the great-circle plane. Both dot products
    // require its nearest projection to lie on the finite minor arc.
    if (!point &&
        Dot(forwardAtStart, centre) >= -kAngularTolerance &&
        Dot(backAtEnd, centre) >= -kAngularTolerance &&
        (radius >= std::numbers::pi / 2.0 ||
         std::abs(Dot(normal, centre)) <= std::sin(radius)))
      return {&hazard};
  }
  return {};
}

}  // namespace weather_routing
