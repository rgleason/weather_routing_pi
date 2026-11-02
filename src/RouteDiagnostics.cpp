// SPDX-License-Identifier: GPL-3.0-or-later
#include "RouteDiagnostics.h"
#include "supercpn/weather_routing/Engine.h"

#include <algorithm>
#include <cmath>
#include <set>
#include <wx/datetime.h>
#include <wx/intl.h>

namespace weather_routing {
namespace wr = supercpn::weather_routing;
namespace {
bool Complete(wr::RoutingStatus status) {
  return status == wr::RoutingStatus::Complete ||
      status == wr::RoutingStatus::CompleteUsingReverseRecovery ||
      status == wr::RoutingStatus::CompleteUsingFrontierRecovery ||
      status == wr::RoutingStatus::CompleteUsingGraphFallback;
}
wxString DurationText(long long seconds) {
  return wxString::Format(_("%lld h %02lld min"), seconds / 3600,
                          (seconds % 3600) / 60);
}
wxString PointText(wr::GeoPoint p) {
  return wxString::Format("%.5f, %.5f", p.latitude, p.longitude);
}
wxString StatusAction(wr::RoutingStatus status) {
  switch (status) {
    case wr::RoutingStatus::WindForecastRequired:
      return _("Open Configuration > Basic > Data Source. Check the selected wind source, provider and coverage for the route and UTC time.");
    case wr::RoutingStatus::CurrentDataRequired:
      return _("Supply current data covering the route and time, or deliberately review the Currents setting.");
    case wr::RoutingStatus::WaveDataRequired:
      return _("Supply wave-height data covering the route and time. Review Require wave coverage if you intend to proceed with unchecked sections.");
    case wr::RoutingStatus::WeatherCoverageInsufficient:
      return _("Review the required data listed below, including charted depth when a minimum depth is configured.");
    case wr::RoutingStatus::InvalidPolar:
    case wr::RoutingStatus::InvalidVesselConfiguration:
      return _("Open the boat and polar settings. Check referenced files and usable performance for the expected conditions.");
    case wr::RoutingStatus::InvalidStart:
    case wr::RoutingStatus::InvalidDestination:
      return _("Inspect the named endpoint, selected shoreline model, exclusion areas and depth requirement. Choose an appropriate routing position.");
    case wr::RoutingStatus::ResourceLimitReached:
    case wr::RoutingStatus::SearchIncomplete:
      return _("Review the stop reason and search allowance after checking the inputs. This does not establish that a route is impossible.");
    case wr::RoutingStatus::NoFeasibleRoute:
      return _("Review source coverage, endpoint checks, constraints and the individual engine results in Details.");
    case wr::RoutingStatus::ValidationFailure:
      return _("Inspect the final-check reason. A rejected candidate is not a completed route; retain the diagnostic report if the failure is unexpected.");
    case wr::RoutingStatus::InternalError:
      return _("Copy the diagnostic report for investigation.");
    default: return {};
  }
}
void RequiredAction(RouteDiagnosticReport& report, wr::RequiredUserAction action) {
  wxString title, next;
  switch (action) {
    case wr::RequiredUserAction::LoadSailingPolar:
      title = _("A valid sailing polar is required"); next = _("Select a valid polar in the boat settings."); break;
    case wr::RequiredUserAction::ConfigureVesselPerformance:
      title = _("Boat performance needs configuration"); next = _("Review the boat and propulsion settings."); break;
    case wr::RequiredUserAction::LoadWeatherGrib:
    case wr::RequiredUserAction::GenerateWeatherGrib:
      title = _("Wind forecast coverage is required"); next = StatusAction(wr::RoutingStatus::WindForecastRequired); break;
    case wr::RequiredUserAction::ConfirmClimatologyWindFallback:
      title = _("Climatology fallback needs review"); next = _("Review the climatology source and fallback policy."); break;
    case wr::RequiredUserAction::LoadCurrentPredictionDataset:
    case wr::RequiredUserAction::ConfirmRoutingWithoutCurrent:
      title = _("Current data or the current-data policy needs review"); next = StatusAction(wr::RoutingStatus::CurrentDataRequired); break;
    case wr::RequiredUserAction::ConfirmRoutingWithoutWaveData:
      title = _("Required wave-height coverage is unavailable"); next = StatusAction(wr::RoutingStatus::WaveDataRequired); break;
    case wr::RequiredUserAction::LoadDepthData:
      title = _("Required charted depth data are unavailable"); next = _("Load usable charted depth coverage for the endpoints and passage. Check that this host supports depth checks."); break;
  }
  report.Add({"preflight." + std::to_string(static_cast<int>(action)),
      DiagnosticSeverity::Error, title, {}, next});
}
void Warning(RouteDiagnosticReport& report, const wr::RoutingWarning& warning) {
  wxString title, next;
  auto severity = DiagnosticSeverity::Warning;
  switch (warning.code) {
    case wr::RoutingWarningCode::ClimatologyWindUsed:
      title = _("Climatology wind was used"); next = _("Climatology describes historical conditions rather than a forecast for this passage."); break;
    case wr::RoutingWarningCode::WindCoverageIncomplete:
      title = _("Wind forecast coverage is incomplete"); next = _("Check forecast coverage and the availability of the permitted fallback source."); break;
    case wr::RoutingWarningCode::XtdCurrentUsed:
      title = _("An alternative current source was used"); next = _("Check the recorded provider; a statistical current source is not the same as a passage forecast."); break;
    case wr::RoutingWarningCode::CurrentAssumedZero:
      title = _("Zero current was assumed where data were unavailable"); next = _("Review current coverage; passage times and track may be affected."); break;
    case wr::RoutingWarningCode::CurrentCoverageIncomplete:
      title = _("Current coverage is incomplete"); next = _("Review the affected sections and permitted current fallback."); break;
    case wr::RoutingWarningCode::WaveDataMissing:
    case wr::RoutingWarningCode::WaveCoverageIncomplete:
      title = _("Wave-height coverage is incomplete"); next = _("The wave-height limit could not be checked in those sections. Missing wave heights do not mean calm seas."); break;
    case wr::RoutingWarningCode::WaveHeightEstimated:
      title = _("Some wave heights were estimated"); next = _("Nearby connected-water estimates are not a guaranteed upper bound."); break;
    case wr::RoutingWarningCode::EstimatedPolar:
      title = _("Estimated polar performance was used"); next = _("Review the measured polar range and the selected low- and high-wind policies."); break;
    case wr::RoutingWarningCode::SearchPruned:
      title = _("The search was bounded and pruned"); severity = DiagnosticSeverity::Notice;
      next = _("The validated result does not establish global optimality or an exhaustive search."); break;
    case wr::RoutingWarningCode::SearchIncomplete:
      title = _("Part of the requested search was incomplete"); next = _("Review the stop reason; compare only completed validated candidates."); break;
    case wr::RoutingWarningCode::EnsembleMemberFailed:
      title = _("Some ensemble evaluations did not meet the target"); next = _("Review achieved feasibility and member failures."); break;
    case wr::RoutingWarningCode::CoastalEndpointLeeway:
      title = _("An endpoint used reduced shore-buffer clearance"); next = _("Inspect the approach or departure. Endpoint access does not permit crossing modelled land."); break;
  }
  report.Add({"warning." + std::to_string(static_cast<int>(warning.code)),
      severity, title, wxString::FromUTF8(warning.message.c_str()), next});
}
// Report actual delivered-route use, independently of warnings emitted during
// exploratory search. Merge only adjacent affected legs, without bridging gaps.
template <typename Predicate>
void Usage(RouteDiagnosticReport& report, const wr::RoutingResult& result,
           const std::string& code, const wxString& title,
           const wxString& consequence, Predicate affected) {
  long long seconds = 0;
  wxString intervals;
  unsigned ranges = 0;
  for (std::size_t i = 0; i < result.legs.size(); ++i) {
    const auto& first = result.legs[i];
    if (!affected(first)) continue;
    auto end = first.endTime;
    auto endPoint = first.end;
    seconds += std::max<long long>(0, (first.endTime - first.startTime).count());
    while (i + 1 < result.legs.size() && affected(result.legs[i + 1]) &&
           result.legs[i + 1].startTime == end &&
           result.legs[i + 1].start == endPoint) {
      const auto& leg = result.legs[++i];
      seconds += std::max<long long>(0, (leg.endTime - leg.startTime).count());
      end = leg.endTime;
      endPoint = leg.end;
    }
    ++ranges;
    // Keep the interactive/support summary bounded for very fragmented data.
    if (ranges <= 20)
      intervals += wxString::Format("\n%s to %s UTC; %s to %s",
          DiagnosticTimeUtc(first.startTime), DiagnosticTimeUtc(end),
          PointText(first.start), PointText(endPoint));
  }
  if (!ranges) return;
  wxString detail = wxString::Format(_("Affected duration: %s."),
                                     DurationText(seconds));
  if (result.metrics.elapsed.count() > 0)
    detail += wxString::Format(_(" %.1f%% of passage time."),
        100.0 * seconds / result.metrics.elapsed.count());
  detail += intervals;
  if (ranges > 20)
    detail += wxString::Format(_("\n%u further intervals omitted from this summary."),
                               ranges - 20);
  report.Add({code, DiagnosticSeverity::Warning, title, detail, consequence});
}
}  // namespace

wxString DiagnosticTimeUtc(wr::TimePoint time) {
  return wxDateTime(static_cast<time_t>(time.time_since_epoch().count()))
      .Format("%Y-%m-%d %H:%M:%S", wxDateTime::UTC);
}
wxString DiagnosticSeverityTitle(DiagnosticSeverity severity) {
  switch (severity) {
    case DiagnosticSeverity::Error: return _("Error");
    case DiagnosticSeverity::Warning: return _("Warning");
    case DiagnosticSeverity::Notice: return _("Note");
  }
  return {};
}
wxString RoutingStatusTitle(wr::RoutingStatus status) {
  switch (status) {
    case wr::RoutingStatus::Complete:
    case wr::RoutingStatus::CompleteUsingReverseRecovery:
    case wr::RoutingStatus::CompleteUsingFrontierRecovery:
    case wr::RoutingStatus::CompleteUsingGraphFallback: return _("Complete");
    case wr::RoutingStatus::NoFeasibleRoute: return _("No validated complete route found");
    case wr::RoutingStatus::SearchIncomplete: return _("Search stopped before completion");
    case wr::RoutingStatus::ResourceLimitReached: return _("Calculation allowance or memory limit reached");
    case wr::RoutingStatus::WindForecastRequired: return _("Usable wind data are required");
    case wr::RoutingStatus::CurrentDataRequired: return _("Required current data are unavailable");
    case wr::RoutingStatus::WaveDataRequired: return _("Required wave-height coverage is unavailable");
    case wr::RoutingStatus::WeatherCoverageInsufficient: return _("Required data coverage is unavailable");
    case wr::RoutingStatus::InvalidPolar: return _("Boat polar is unavailable or invalid");
    case wr::RoutingStatus::InvalidVesselConfiguration: return _("Route or boat settings need attention");
    case wr::RoutingStatus::InvalidStart: return _("Start position cannot be used");
    case wr::RoutingStatus::InvalidDestination: return _("Destination position cannot be used");
    case wr::RoutingStatus::ValidationFailure: return _("Candidate route failed its final check");
    case wr::RoutingStatus::Cancelled: return _("Stopped by user");
    case wr::RoutingStatus::InternalError: return _("Unexpected calculation error");
  }
  return _("Calculation needs attention");
}
void RouteDiagnosticReport::Add(RouteDiagnostic item) {
  const auto same = [&](const auto& old) {
    return old.code == item.code && old.engine == item.engine &&
           old.detail == item.detail;
  };
  if (std::none_of(items.begin(), items.end(), same))
    items.push_back(std::move(item));
}
unsigned RouteDiagnosticReport::Count(DiagnosticSeverity severity) const {
  return static_cast<unsigned>(std::count_if(items.begin(), items.end(),
      [severity](const auto& item) { return item.severity == severity; }));
}
wxString RouteDiagnosticReport::Summary() const {
  if (cancelled) return _("Stopped by user");
  const auto first = std::find_if(items.begin(), items.end(), [](const auto& item) {
    return item.severity == DiagnosticSeverity::Error;
  });
  if (first != items.end()) return first->title;
  if (!hasResult) return {};
  wxString summary = RoutingStatusTitle(status);
  const auto warnings = Count(DiagnosticSeverity::Warning);
  if (complete && warnings)
    summary += warnings == 1 ? _(" (1 warning)")
                             : wxString::Format(_(" (%u warnings)"), warnings);
  return summary;
}
wxString RouteDiagnosticReport::Text() const {
  wxString text = context;
  if (!text.empty()) text += "\n\n";
  if (!Summary().empty()) text += Summary() + "\n";
  // Stable priority order, while preserving context within each severity.
  for (const auto severity : {DiagnosticSeverity::Error,
                             DiagnosticSeverity::Warning,
                             DiagnosticSeverity::Notice}) {
    for (const auto& item : items) {
      if (item.severity != severity) continue;
      text += "\n" + DiagnosticSeverityTitle(severity) + ": " + item.title;
      if (!item.engine.empty()) text += " [" + item.engine + "]";
      if (item.occurrences > 1)
        text += wxString::Format(_(" (%llu occurrences; first request shown)"),
            static_cast<unsigned long long>(item.occurrences));
      if (!item.detail.empty()) text += "\n" + item.detail;
      if (!item.action.empty()) text += "\n" + _("Next step: ") + item.action;
      text += "\n";
    }
  }
  if (!technical.empty()) text += "\n" + _("Technical details") + "\n" + technical;
  return text;
}
RouteDiagnosticReport ReconcileRouteDiagnostics(RouteDiagnosticReport report,
    const wxString& error, const wxString& failureReason,
    const wxString& weatherError, bool memoryExhausted) {
  const auto originalSummary = report.Summary();
  if (!error.empty() && std::none_of(report.items.begin(), report.items.end(),
      [&](const auto& item) { return item.title == error && item.severity == DiagnosticSeverity::Error; }))
    report.Add({"runtime.error", DiagnosticSeverity::Error, error, {},
                _("Review the route setup and the details below.")});
  if (!failureReason.empty() && (!report.hasResult || failureReason != originalSummary)) {
    report.Add({"runtime.failure", DiagnosticSeverity::Error,
        failureReason, {}, _("Inspect the route and the final-check details before using the result.")});
  }
  if (!weatherError.empty())
    report.Add({"runtime.weather", DiagnosticSeverity::Error,
        _("Weather service could not supply required data"), weatherError,
        _("Check the selected weather source, coverage and route time.")});
  if (report.HasErrors()) {
    report.complete = false;
    if (report.hasResult && Complete(report.status)) report.status = wr::RoutingStatus::ValidationFailure;
  }
  if (memoryExhausted) {
    report.complete = false;
    report.cancelled = false;
    report.status = wr::RoutingStatus::ResourceLimitReached;
    report.Add({"runtime.memory", DiagnosticSeverity::Error,
                _("Memory allocation failed"), failureReason});
    const auto memory = std::find_if(report.items.begin(), report.items.end(),
        [](const auto& item) { return item.code == "runtime.memory"; });
    if (memory != report.items.end()) std::rotate(report.items.begin(), memory, memory + 1);
  }
  return report;
}

RouteDiagnosticReport CheckWindSourceSelection(bool useGrib,
    bool climatologySelected, bool climatologyProviderAvailable) {
  RouteDiagnosticReport report;
  if (!useGrib && !(climatologySelected && climatologyProviderAvailable)) {
    report.Add({"setup.wind_source", DiagnosticSeverity::Error,
        _("No usable wind source is selected"),
        climatologySelected
            ? _("GRIB is not selected for this route and the selected climatology wind provider is unavailable. Wind displayed on the chart is not enabled for this calculation.")
            : _("Neither GRIB nor climatology wind is selected for this route. Wind displayed on the chart does not enable it for routing."),
        _("Open Configuration > Basic > Data Source and select Grib, or configure an available climatology wind source."),
        {}, true});
  } else if (useGrib && climatologySelected && !climatologyProviderAvailable) {
    report.Add({"setup.climatology_unavailable", DiagnosticSeverity::Warning,
        _("Climatology fallback is unavailable"),
        _("A climatology wind mode is selected, but its provider is unavailable. This calculation needs usable GRIB wind wherever the route goes."),
        _("Review the climatology provider and forecast coverage."), {}, true});
  }
  return report;
}
RouteDiagnosticReport CheckForecastWindCoverage(
    const wr::ParameterCoverage& coverage, wr::GeoPoint start,
    wr::GeoPoint destination, wr::TimePoint departure, bool usableFallback) {
  RouteDiagnosticReport report;
  const auto severity = usableFallback ? DiagnosticSeverity::Warning
                                      : DiagnosticSeverity::Error;
  const wxString next = usableFallback
      ? _("Climatology may be needed. The finished result will report actual use. Obtain a forecast covering the passage if you want forecast wind throughout.")
      : _("Load a wind forecast covering the departure, passage and approach, with room for detours, or deliberately configure an available climatology source.");
  if (!coverage.available) {
    report.Add({"coverage.wind_unavailable", severity,
        _("GRIB wind is unavailable at the requested departure time"),
        _("The provider did not supply a usable wind frame at ") +
            DiagnosticTimeUtc(departure) + _(" UTC. This alone does not identify whether a file, time range, parameter or provider reply is missing."),
        next, {}, true});
    return report;
  }
  const auto contains = [&](wr::GeoPoint point) {
    const double lon = std::fmod(std::fmod(point.longitude + 180.0, 360.0) + 360.0, 360.0) - 180.0;
    const bool longitude = coverage.area.west <= coverage.area.east
        ? lon >= coverage.area.west && lon <= coverage.area.east
        : lon >= coverage.area.west || lon <= coverage.area.east;
    return point.latitude >= coverage.area.south &&
        point.latitude <= coverage.area.north && longitude;
  };
  for (const bool isStart : {true, false}) {
    const auto point = isStart ? start : destination;
    if (contains(point)) continue;
    report.Add({isStart ? "coverage.start_outside_wind" : "coverage.destination_outside_wind",
        severity, isStart ? _("Departure is outside the GRIB wind grid")
                          : _("Destination is outside the GRIB wind grid"),
        PointText(point) + wxString::Format(
            _("; departure-frame wind bounds: latitude %.5f to %.5f, longitude %.5f to %.5f."),
            coverage.area.south, coverage.area.north, coverage.area.west, coverage.area.east),
        next, {}, true});
  }
  if ((coverage.begins && departure < *coverage.begins) ||
      (coverage.ends && departure > *coverage.ends))
    report.Add({"coverage.departure_time", severity,
        _("Departure is outside the wind forecast time range"),
        DiagnosticTimeUtc(departure) + " UTC", next, {}, true});
  if (coverage.ends)
    report.Add({"coverage.forecast_end", DiagnosticSeverity::Notice,
        _("Wind forecast has a limited time range"),
        _("Forecast ends at ") + DiagnosticTimeUtc(*coverage.ends) +
            _(" UTC. A passage continuing beyond this needs further data or a permitted usable fallback."),
        {}, {}, true});
  else
    report.Add({"coverage.time_unknown", DiagnosticSeverity::Notice,
        _("The provider has not supplied the full forecast time range"),
        _("The departure frame does not establish coverage for the whole passage. Weather availability is checked during calculation and final validation."),
        {}, {}, true});
  return report;
}
RouteDiagnosticReport DescribeRoutingResult(const wr::RoutingResult& result,
                                           bool waveLimitEnabled) {
  RouteDiagnosticReport report;
  report.hasResult = true;
  report.status = result.status;
  if (Complete(result.status) && (!result.validation.passed || result.legs.empty()))
    report.status = wr::RoutingStatus::ValidationFailure;
  report.complete = Complete(report.status);
  report.cancelled = result.status == wr::RoutingStatus::Cancelled;
  if (!report.complete && !report.cancelled && !result.attemptFailures.empty()) {
    const auto first = result.attemptFailures.front().status;
    report.status = std::all_of(result.attemptFailures.begin(), result.attemptFailures.end(),
        [first](const auto& attempt) { return attempt.status == first; })
        ? first : wr::RoutingStatus::NoFeasibleRoute;
  }
  if (!report.complete && !report.cancelled) {
    if (result.preflight && result.attemptFailures.empty())
      for (const auto action : result.preflight->requiredActions)
        RequiredAction(report, action);
    if (!report.HasErrors())
      report.Add({"result." + wr::toString(report.status),
          DiagnosticSeverity::Error, RoutingStatusTitle(report.status),
          wxString::FromUTF8(result.message.c_str()), StatusAction(report.status)});
  }
  if (report.cancelled)
    report.Add({"result.cancelled", DiagnosticSeverity::Notice,
                _("Stopped by user"), {}, _("Recompute when ready.")});
  for (const auto& warning : result.warnings) Warning(report, warning);

  if (report.complete) {
    // Remove generic source-use warnings in favour of measured delivered-route
    // intervals below. A selected fallback policy is not proof it was used.
    std::erase_if(report.items, [](const auto& item) {
      for (const auto code : {wr::RoutingWarningCode::ClimatologyWindUsed,
          wr::RoutingWarningCode::CurrentAssumedZero,
          wr::RoutingWarningCode::WaveDataMissing,
          wr::RoutingWarningCode::WaveHeightEstimated,
          wr::RoutingWarningCode::WaveCoverageIncomplete})
        if (item.code == "warning." + std::to_string(static_cast<int>(code))) return true;
      return false;
    });
    Usage(report, result, "usage.climatology_wind",
        _("Climatology wind was used"),
        _("Climatology describes historical conditions rather than a forecast for this passage. Review forecast coverage for these sections."),
        [](const auto& leg) { return leg.windSource.source == wr::EnvironmentalSource::Climatology; });
    Usage(report, result, "usage.climatology_current",
        _("Climatology current was used"),
        _("Climatology describes historical current conditions rather than a prediction for this passage."),
        [](const auto& leg) { return leg.currentSource.source == wr::EnvironmentalSource::XtdCurrentPrediction &&
            leg.currentSource.modelIdentity == "OpenCPN climatology current"; });
    Usage(report, result, "usage.alternative_current",
        _("Alternative current data were used"),
        _("Review the recorded current provider and its coverage."),
        [](const auto& leg) { return leg.currentSource.source == wr::EnvironmentalSource::XtdCurrentPrediction &&
            leg.currentSource.modelIdentity != "OpenCPN climatology current"; });
    Usage(report, result, "usage.zero_current",
        _("Zero current was assumed where data were unavailable"),
        _("Review current coverage; passage times and track may be affected."),
        [](const auto& leg) { return leg.currentSource.source == wr::EnvironmentalSource::NoDataAssumedZero &&
            leg.currentSource.datasetIdentity != "current-disabled"; });
    Usage(report, result, "usage.missing_waves",
        _("Wave-height data are missing"),
        waveLimitEnabled
            ? _("The wave-height limit could not be checked there. Missing wave heights do not mean calm seas.")
            : _("Missing wave heights do not mean calm seas. Wave-based comfort information is incomplete in those sections."),
        [](const auto& leg) { return !leg.waves.available; });
    Usage(report, result, "usage.estimated_waves",
        _("Some wave heights were estimated"),
        _("The maximum of nearby connected-water cells is not a guaranteed upper bound."),
        [](const auto& leg) { return leg.waves.nearbyHeightEstimate; });
    Usage(report, result, "usage.waiting",
        _("Stationary waiting or anchoring was assumed"),
        _("Verify anchoring suitability at each position; the calculation does not establish it."),
        [](const auto& leg) { return leg.stationaryWait; });
  }
  if (report.complete && !result.attemptFailures.empty())
    report.Add({"sequence.incomplete", DiagnosticSeverity::Warning,
        _("Some selected engines did not produce a complete route"),
        wxString::Format(_("%u unsuccessful engine attempts; a validated result is available."),
                         static_cast<unsigned>(result.attemptFailures.size())),
        _("Review the individual engine results below. Comparisons apply to the completed validated candidates.")});
  report.technical = wxString::FromUTF8(wr::toString(result.status).c_str()) +
      "\n" + wxString::FromUTF8(result.message.c_str());
  if (!result.legs.empty())
    report.technical += "\n" + _("Delivered route departure: ") +
        DiagnosticTimeUtc(result.legs.front().startTime) + " UTC; " +
        PointText(result.legs.front().start) + "\n" + _("Delivered route endpoint: ") +
        DiagnosticTimeUtc(result.legs.back().endTime) + " UTC; " + PointText(result.legs.back().end);
  if (!result.validation.failureReason.empty())
    report.technical += "\n" + _("Final check: ") +
        wxString::FromUTF8(result.validation.failureReason.c_str());
  const auto& attemptHistory = result.engineAttempts.empty()
      ? result.attemptFailures : result.engineAttempts;
  for (const auto& attempt : attemptHistory)
    report.technical += "\n" + wxString::FromUTF8(attempt.engine.c_str()) +
        ": " + RoutingStatusTitle(attempt.status) + "\n" +
        wxString::FromUTF8(attempt.detail.c_str());
  for (const auto& reason : result.diagnostics.stageStopReasons)
    report.technical += "\n" + wxString::FromUTF8(reason.c_str());
  for (const auto& reason : result.diagnostics.resourceLimitEvents)
    report.technical += "\n" + wxString::FromUTF8(reason.c_str());
  std::set<std::string> sources;
  for (const auto& leg : result.legs) {
    for (const auto* source : {&leg.windSource, &leg.currentSource, &leg.waveSource}) {
      if (source->datasetIdentity.empty() && source->modelIdentity.empty()) continue;
      const auto identity = source->datasetIdentity + "; " + source->modelIdentity;
      if (sources.size() < 20 && sources.insert(identity).second)
        report.technical += "\n" + _("Source: ") + wxString::FromUTF8(identity.c_str());
    }
  }
  report.technical += wxString::Format(
      _("\nGenerated states: %llu; retained states: %llu; validation samples: %llu."),
      static_cast<unsigned long long>(result.diagnostics.generatedStates),
      static_cast<unsigned long long>(result.diagnostics.retainedStates),
      static_cast<unsigned long long>(result.diagnostics.validationSamples));
  return report;
}
}  // namespace weather_routing
