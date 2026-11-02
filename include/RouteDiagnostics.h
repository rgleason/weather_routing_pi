// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <string>
#include <vector>
#include <wx/string.h>
#include "supercpn/weather_routing/Types.h"

namespace weather_routing {
enum class DiagnosticSeverity { Error, Warning, Notice };
struct RouteDiagnostic {
  std::string code;
  DiagnosticSeverity severity{DiagnosticSeverity::Notice};
  wxString title, detail, action, engine;
  bool setup{false};
  std::uint64_t occurrences{1};
};
struct RouteDiagnosticReport {
  wxString context, technical;
  std::vector<RouteDiagnostic> items;
  bool hasResult{false}, complete{false}, cancelled{false};
  supercpn::weather_routing::RoutingStatus status{
      supercpn::weather_routing::RoutingStatus::InternalError};
  void Add(RouteDiagnostic item);
  unsigned Count(DiagnosticSeverity severity) const;
  bool HasErrors() const { return Count(DiagnosticSeverity::Error) != 0; }
  wxString Summary() const;
  wxString Text() const;
};

RouteDiagnosticReport DescribeRoutingResult(
    const supercpn::weather_routing::RoutingResult& result,
    bool waveLimitEnabled = true);
RouteDiagnosticReport CheckWindSourceSelection(bool useGrib,
    bool climatologySelected, bool climatologyProviderAvailable);
RouteDiagnosticReport CheckForecastWindCoverage(
    const supercpn::weather_routing::ParameterCoverage& coverage,
    supercpn::weather_routing::GeoPoint start,
    supercpn::weather_routing::GeoPoint destination,
    supercpn::weather_routing::TimePoint departure,
    bool usableFallback);
RouteDiagnosticReport ReconcileRouteDiagnostics(RouteDiagnosticReport report,
    const wxString& error, const wxString& failureReason,
    const wxString& weatherError, bool memoryExhausted);
wxString RoutingStatusTitle(supercpn::weather_routing::RoutingStatus status);
wxString DiagnosticSeverityTitle(DiagnosticSeverity severity);
wxString DiagnosticTimeUtc(supercpn::weather_routing::TimePoint time);
}  // namespace weather_routing
