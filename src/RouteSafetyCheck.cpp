/* Copyright (C) 2026 OpenCPN contributors. GPL v3 or later. */
#include "RouteSafetyCheck.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <utility>
#include <wx/intl.h>

#include "ChartLongitude.h"

namespace weather_routing {
namespace {
constexpr double kPi = 3.14159265358979323846;
constexpr double kEarthRadiusNm = 3440.065;
constexpr double kChunkNm = .25;
constexpr std::size_t kMaximumChunks = 200000;
constexpr std::size_t kMaximumPendingRetries = 100;
double Mercator(double lat) {
  return std::log(std::tan(kPi / 4 + lat * kPi / 360));
}
double RhumbDistance(const SafetyCheckWaypoint& a, const SafetyCheckWaypoint& b) {
  const double dphi = (b.lat - a.lat) * kPi / 180;
  const double dpsi = Mercator(b.lat) - Mercator(a.lat);
  const double q = std::abs(dpsi) > 1e-12 ? dphi / dpsi : std::cos(a.lat * kPi / 180);
  const double dlambda = CanonicalChartLongitude(b.lon - a.lon) * kPi / 180;
  return std::hypot(dphi, q * dlambda) * kEarthRadiusNm;
}
SafetyCheckWaypoint At(const SafetyCheckWaypoint& a,
                      const SafetyCheckWaypoint& b, double f) {
  SafetyCheckWaypoint p;
  // Rhumb distance is proportional to latitude change. Interpolate distance,
  // then use the Mercator ratio for longitude so high-latitude chunks also
  // respect the query-length bound and remain on the original rhumb line.
  p.lat = a.lat + f * (b.lat - a.lat);
  const double dpsi = Mercator(b.lat) - Mercator(a.lat);
  const double longitude_fraction = std::abs(dpsi) > 1e-12 ?
      (Mercator(p.lat) - Mercator(a.lat)) / dpsi : f;
  p.lon = CanonicalChartLongitude(a.lon + longitude_fraction * CanonicalChartLongitude(b.lon - a.lon));
  return p;
}
bool Valid(const SafetyCheckWaypoint& p) {
  return std::isfinite(p.lat) && std::abs(p.lat) < 90 &&
         std::isfinite(p.lon) && std::abs(p.lon) <= 540;
}
template <std::size_t N> std::string Text(const char (&value)[N]) {
  return std::string(value, std::find(value, value + N, '\0'));
}
}  // namespace

bool SafetyCheckWaypoint::operator==(const SafetyCheckWaypoint& other) const {
  return guid == other.guid && name == other.name &&
         lat == other.lat && lon == other.lon;
}

RouteSafetyCheck::RouteSafetyCheck(std::vector<SafetyCheckWaypoint> waypoints,
    PlugInSegmentSafetyOptions options, Query query)
    : waypoints_(std::move(waypoints)), options_(options), query_(std::move(query)) {
  if (waypoints_.size() < 2 || !query_ ||
      !std::all_of(waypoints_.begin(), waypoints_.end(), Valid) ||
      !std::isfinite(options_.minimum_depth_m) || options_.minimum_depth_m < 0 ||
      !std::isfinite(options_.safety_margin_nm) || options_.safety_margin_nm < 0) {
    error_ = wxTRANSLATE("Invalid route coordinates or check settings.");
    return;
  }
  options_.struct_size = sizeof(options_);
  options_.check_land = 1;
  options_.check_depth = options_.minimum_depth_m > 0;
  options_.allow_gshhs_fallback = 0;
  options_.force_authoritative_fine_validation = 1;
  for (std::size_t i = 1; i < waypoints_.size(); ++i) {
    const double distance = RhumbDistance(waypoints_[i - 1], waypoints_[i]);
    const double count = std::max(1., std::ceil(distance / kChunkNm));
    if (!std::isfinite(count) || count > kMaximumChunks - total_) {
      error_ = wxTRANSLATE("Route is too large for one check. Check shorter sections.");
      return;
    }
    chunks_.push_back(static_cast<std::size_t>(count));
    total_ += chunks_.back();
  }
  state_ = RouteCheckState::Running;
}

void RouteSafetyCheck::Advance() {
  if (state_ != RouteCheckState::Running) return;
  const double from = static_cast<double>(chunk_) / chunks_[leg_];
  const double to = static_cast<double>(chunk_ + 1) / chunks_[leg_];
  const auto a = At(waypoints_[leg_], waypoints_[leg_ + 1], from);
  const auto b = At(waypoints_[leg_], waypoints_[leg_ + 1], to);
  PlugInSegmentSafetyResult result{};
  result.struct_size = sizeof(result);
  result.status = PI_SEGMENT_SAFETY_ERROR;
  const bool answered = query_(a.lat, a.lon, b.lat, b.lon, &options_, &result);
  if (answered && (result.status == PI_SEGMENT_SAFETY_PENDING_DATA ||
                   result.diagnostic_reason == PI_SEGMENT_SAFETY_DIAG_PENDING_DATA)) {
    if (++pending_retries_ <= kMaximumPendingRetries) return;
    // A pending diagnostic cannot certify a section, even if a provider has
    // inconsistently left its status as SAFE.
    result.status = PI_SEGMENT_SAFETY_PENDING_DATA;
  }
  pending_retries_ = 0;
  if (!answered || result.status < PI_SEGMENT_SAFETY_SAFE ||
      result.status > PI_SEGMENT_SAFETY_PENDING_DATA) {
    result.status = PI_SEGMENT_SAFETY_ERROR;
    std::snprintf(result.message, sizeof(result.message), "%s", "Chart-safety query failed.");
  } else if (result.status == PI_SEGMENT_SAFETY_SAFE &&
             (result.used_fallback || result.source == PI_SEGMENT_SAFETY_SOURCE_NONE ||
              result.source == PI_SEGMENT_SAFETY_SOURCE_GSHHS_FALLBACK)) {
    result.status = PI_SEGMENT_SAFETY_NO_DATA;
    std::snprintf(result.message, sizeof(result.message), "%s", "No chart-backed result is available.");
  }
  ++checked_;
  if (result.status != PI_SEGMENT_SAFETY_SAFE) {
    if (IsUnverified(result.status)) ++unverified_; else ++hazards_;
    const auto midpoint = At(waypoints_[leg_], waypoints_[leg_ + 1], (from + to) / 2);
    RouteCheckFinding finding;
    finding.leg = leg_ + 1;
    finding.status = result.status;
    finding.source = result.source;
    finding.lat = midpoint.lat;
    finding.lon = midpoint.lon;
    if (result.hit_sample_count > 0 && std::isfinite(result.hit_sample_lat) &&
        std::abs(result.hit_sample_lat) < 90 && std::isfinite(result.hit_sample_lon)) {
      finding.lat = result.hit_sample_lat;
      finding.lon = CanonicalChartLongitude(result.hit_sample_lon);
    }
    finding.from_fraction = from;
    finding.to_fraction = to;
    finding.has_depth = result.has_depth && std::isfinite(result.min_depth_m);
    finding.minimum_depth_m = result.min_depth_m;
    finding.chart_path = Text(result.chart_path);
    finding.object = Text(result.hit_object);
    finding.message = Text(result.message);
    // Coalesce contiguous affected chunks; never merge across a clear gap.
    if (!findings_.empty() && findings_.back().leg == finding.leg &&
        findings_.back().status == finding.status &&
        findings_.back().source == finding.source &&
        findings_.back().chart_path == finding.chart_path &&
        findings_.back().object == finding.object &&
        findings_.back().to_fraction == from &&
        findings_.back().has_depth == finding.has_depth) {
      auto& last = findings_.back();
      last.to_fraction = to;
      if (finding.has_depth) last.minimum_depth_m = std::min(last.minimum_depth_m, finding.minimum_depth_m);
    } else {
      findings_.push_back(std::move(finding));
    }
  }
  if (++chunk_ == chunks_[leg_]) { chunk_ = 0; ++leg_; }
  if (checked_ == total_) state_ = RouteCheckState::Complete;
}

void RouteSafetyCheck::Cancel() {
  if (state_ == RouteCheckState::Running) state_ = RouteCheckState::Cancelled;
}
void RouteSafetyCheck::Invalidate(const std::string& reason) {
  state_ = RouteCheckState::Outdated;
  error_ = reason;
}
bool RouteSafetyCheck::Matches(const std::vector<SafetyCheckWaypoint>& waypoints) const {
  return waypoints_ == waypoints;
}
bool RouteSafetyCheck::Clear() const {
  return state_ == RouteCheckState::Complete && hazards_ == 0 && unverified_ == 0;
}
bool RouteSafetyCheck::IsUnverified(int status) {
  return status == PI_SEGMENT_SAFETY_NO_DATA || status == PI_SEGMENT_SAFETY_ERROR ||
         status == PI_SEGMENT_SAFETY_UNKNOWN_DEPTH || status == PI_SEGMENT_SAFETY_PENDING_DATA;
}
const char* RouteSafetyCheck::Reason(int status) {
  switch (status) {
    case PI_SEGMENT_SAFETY_CROSSES_LAND: return wxTRANSLATE("Land crossing");
    case PI_SEGMENT_SAFETY_WITHIN_LAND_MARGIN: return wxTRANSLATE("Within land clearance");
    case PI_SEGMENT_SAFETY_UNSAFE_AREA: return wxTRANSLATE("Charted hazard");
    case PI_SEGMENT_SAFETY_DRYING_AREA: return wxTRANSLATE("Drying area");
    case PI_SEGMENT_SAFETY_TOO_SHALLOW: return wxTRANSLATE("Insufficient depth");
    case PI_SEGMENT_SAFETY_UNKNOWN_DEPTH: return wxTRANSLATE("Depth unavailable");
    case PI_SEGMENT_SAFETY_PENDING_DATA: return wxTRANSLATE("Charts still loading");
    case PI_SEGMENT_SAFETY_NO_DATA: return wxTRANSLATE("Chart coverage unavailable");
    case PI_SEGMENT_SAFETY_ERROR: return wxTRANSLATE("Check could not be completed");
    default: return wxTRANSLATE("No hazard found");
  }
}
}  // namespace weather_routing
