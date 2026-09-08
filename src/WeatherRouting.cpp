/***************************************************************************
 *   Copyright (C) 2016 by Sean D'Epagnier                                 *
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
 ***************************************************************************/

#include <wx/wx.h>
#include <wx/aui/aui.h>
#include <wx/imaglist.h>
#include <wx/progdlg.h>
#include <wx/dir.h>
#include <wx/spinctrl.h>

#include <json/json.h>

#include <stdlib.h>
#include <math.h>
#include <time.h>

#include <wx/glcanvas.h>

#include "tinyxml.h"

#include "Utilities.h"
#include "Boat.h"
#include "BoatDialog.h"
#include "RouteMapOverlay.h"
#include "ModernNativeRoute.h"
#include "MultiLegRouteOutput.h"
#include "RouteWaypointExtractor.h"
#include "weather_routing_pi.h"
#include "WeatherRouting.h"
#include "ChartSafetyDefaults.h"
#include "AboutDialog.h"
#include "ConstraintChecker.h"
#include "ChartSafetyAtlas.h"
#include "ChartSafetyHost.h"
#include "ChartSafetyPolicy.h"
#include "OceanPrewarmPolicy.h"
#include "ReachabilityPrewarmPolicy.h"
#include "RouteDisplayPolicy.h"
#include "DepartureScheduler.h"
#include "RoutingResourcePolicy.h"
#include "WeatherDataProvider.h"
#include "StabilityRouteAdapter.h"
#include "headless/HeadlessRouteRunner.h"
#include "headless/HeadlessRouteMonitor.h"
#include "georef.h"
#include "icons.h"
#include "navobj_util.h"
#include "ocpn_plugin.h"

#include <algorithm>
#include <cmath>
#include <functional>
#include <limits>
#include <set>
#include <vector>

static const int MAX_DEPARTURE_OPTIMIZATION_CANDIDATES = 73;
static const int DEFERRED_ROUTING_MULTILEG_SEQUENCE = 1;
static const int DEFERRED_ROUTING_MULTILEG_OPTIMIZATION = 2;
static const int DEFERRED_ROUTING_COMPUTE_CURRENT = 3;
static const int DEFERRED_ROUTING_COMPUTE_ALL = 4;
static const int UI_TIMING_COMPLETED_ROUTES_PER_TICK = 4;
static const int UI_TIMING_ACTIVE_SERVICE_INTERVAL_MS = 2;
static const long UI_TIMING_LOG_THRESHOLD_MS = 100;
static bool s_loggedDetectLandGshhsWarning = false;
static std::set<wxString> s_chartSafetySharedPrewarmScopes;
static std::set<wxString> s_chartSafetyPreparedScoutScopes;

namespace {

constexpr int kDefaultWeatherRoutingWidthDip = 1260;
constexpr int kDefaultWeatherRoutingHeightDip = 500;
constexpr int kWeatherRoutingScreenMarginDip = 40;

wxSize DefaultWeatherRoutingDialogSize(wxWindow* window) {
  const wxSize best = window->GetBestSize();
  const wxSize preferred = window->FromDIP(
      wxSize(kDefaultWeatherRoutingWidthDip, kDefaultWeatherRoutingHeightDip));
  const int margin = window->FromDIP(kWeatherRoutingScreenMarginDip);
  const wxSize display = wxGetClientDisplayRect().GetSize();
  const wxSize available(std::max(1, display.x - margin),
                         std::max(1, display.y - margin));
  return wxSize(std::min(available.x, std::max(best.x, preferred.x)),
                std::min(available.y, std::max(best.y, preferred.y)));
}

class ScopedRoutePreparation {
public:
  explicit ScopedRoutePreparation(int& depth) : depth_(depth) { ++depth_; }
  ~ScopedRoutePreparation() { --depth_; }

  ScopedRoutePreparation(const ScopedRoutePreparation&) = delete;
  ScopedRoutePreparation& operator=(const ScopedRoutePreparation&) = delete;

private:
  int& depth_;
};

wxString NewDepartureOptimizationGroupId() {
  // FormatISOCombined() has only one-second resolution. A second optimisation
  // started quickly enough could otherwise reuse the retained weather cache
  // from its predecessor, including after the selected GRIB changed.
  static unsigned long long sequence = 0;
  return wxString::Format("departure-%lld-%llu",
                          wxGetUTCTimeMillis().GetValue(), ++sequence);
}

bool InitializeHeadlessGribFromEnv(wxString* error) {
  const char* configuredFile = getenv("WR_HEADLESS_GRIB_FILE");
  const wxString gribFile =
      configuredFile ? wxString(configuredFile) : wxString();
  if (gribFile.IsEmpty()) return true;
  if (!wxFileExists(gribFile)) {
    if (error)
      *error = wxString::Format("GRIB file does not exist: %s", gribFile);
    return false;
  }

  // GRIB_VALUES_REQUEST lazily creates the GRIB control object.  Opening the
  // file then uses the GRIB plugin's existing public JSON message interface;
  // Weather Routing neither parses the file nor depends on GRIB internals.
  Json::Value initialize;
  SendPluginMessage("GRIB_VALUES_REQUEST",
                    Json::FastWriter().write(initialize));
  Json::Value open;
  open["grib_file"] = std::string(gribFile.ToUTF8());
  SendPluginMessage("GRIB_APPLY_JSON_CONFIG", Json::FastWriter().write(open));
  SendPluginMessage("GRIB_TIMELINE_REQUEST", "");
  wxYieldIfNeeded();
  wxLogMessage("WR_HEADLESS_GRIB initialized file=\"%s\"", gribFile);
  return true;
}

class RouteSimplificationDialog : public wxDialog {
public:
  typedef std::function<RouteSimplificationResult(
      const RouteSimplificationOptions&)>
      PreviewFunction;

  RouteSimplificationDialog(wxWindow* parent, const PreviewFunction& preview)
      : wxDialog(parent, wxID_ANY, _("Simplify weather route"),
                 wxDefaultPosition, wxDefaultSize,
                 wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER),
        m_preview(preview) {
    wxBoxSizer* top = new wxBoxSizer(wxVERTICAL);
    wxFlexGridSizer* settings = new wxFlexGridSizer(0, 2, 8, 8);
    settings->AddGrowableCol(1);

    settings->Add(new wxStaticText(this, wxID_ANY, _("Preset")), 0,
                  wxALIGN_CENTER_VERTICAL);
    m_preset = new wxChoice(this, wxID_ANY);
    m_preset->Append(_("Navigation"));
    m_preset->Append(_("Compact GPX"));
    m_preset->Append(_("Custom"));
    m_preset->SetSelection(0);
    settings->Add(m_preset, 1, wxEXPAND);

    settings->Add(
        new wxStaticText(this, wxID_ANY, _("Maximum cross-track error")), 0,
        wxALIGN_CENTER_VERTICAL);
    wxBoxSizer* error_row = new wxBoxSizer(wxHORIZONTAL);
    m_crossTrack = new wxSpinCtrlDouble(this, wxID_ANY);
    m_crossTrack->SetRange(0.01, 5.0);
    m_crossTrack->SetIncrement(0.05);
    m_crossTrack->SetDigits(2);
    error_row->Add(m_crossTrack, 1, wxRIGHT, 6);
    error_row->Add(new wxStaticText(this, wxID_ANY, _("NM")), 0,
                   wxALIGN_CENTER_VERTICAL);
    settings->Add(error_row, 1, wxEXPAND);

    settings->Add(
        new wxStaticText(this, wxID_ANY, _("Maximum estimated ETA penalty")), 0,
        wxALIGN_CENTER_VERTICAL);
    wxBoxSizer* eta_row = new wxBoxSizer(wxHORIZONTAL);
    m_etaPenalty = new wxSpinCtrlDouble(this, wxID_ANY);
    m_etaPenalty->SetRange(0.0, 180.0);
    m_etaPenalty->SetIncrement(1.0);
    m_etaPenalty->SetDigits(0);
    eta_row->Add(m_etaPenalty, 1, wxRIGHT, 6);
    eta_row->Add(new wxStaticText(this, wxID_ANY, _("minutes")), 0,
                 wxALIGN_CENTER_VERTICAL);
    settings->Add(eta_row, 1, wxEXPAND);
    top->Add(settings, 0, wxEXPAND | wxALL, 12);

    m_summary = new wxStaticText(
        this, wxID_ANY,
        _("Select Preview to calculate a weather-feasible, chart-safe route."));
    m_summary->Wrap(430);
    top->Add(m_summary, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 12);

    wxStaticText* note = new wxStaticText(
        this, wxID_ANY,
        _("The computed weather route and saved tracks remain unchanged."));
    top->Add(note, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 12);

    wxBoxSizer* buttons = new wxBoxSizer(wxHORIZONTAL);
    m_previewButton = new wxButton(this, wxID_ANY, _("Preview"));
    m_useButton = new wxButton(this, wxID_APPLY, _("Use Simplified Route"));
    m_useButton->Enable(false);
    buttons->Add(m_previewButton, 0, wxRIGHT, 8);
    buttons->AddStretchSpacer();
    buttons->Add(m_useButton, 0, wxRIGHT, 8);
    buttons->Add(new wxButton(this, wxID_CANCEL, _("Cancel")), 0);
    top->Add(buttons, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 12);

    SetSizerAndFit(top);
    SetMinSize(wxSize(500, GetSize().GetHeight()));
    ApplyPreset(0);

    m_preset->Bind(wxEVT_CHOICE, &RouteSimplificationDialog::OnPreset, this);
    m_crossTrack->Bind(wxEVT_SPINCTRLDOUBLE,
                       &RouteSimplificationDialog::OnSettingsChanged, this);
    m_etaPenalty->Bind(wxEVT_SPINCTRLDOUBLE,
                       &RouteSimplificationDialog::OnSettingsChanged, this);
    m_previewButton->Bind(wxEVT_BUTTON, &RouteSimplificationDialog::OnPreview,
                          this);
    m_useButton->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) {
      if (m_result.success) EndModal(wxID_APPLY);
    });
  }

  const RouteSimplificationOptions& Options() const { return m_options; }
  const RouteSimplificationResult& Result() const { return m_result; }

private:
  void ApplyPreset(int selection) {
    if (selection == 0) {
      m_crossTrack->SetValue(0.10);
      m_etaPenalty->SetValue(5.0);
    } else if (selection == 1) {
      m_crossTrack->SetValue(0.25);
      m_etaPenalty->SetValue(15.0);
    }
    InvalidatePreview();
  }

  void InvalidatePreview() {
    m_useButton->Enable(false);
    m_result = RouteSimplificationResult();
    m_summary->SetLabel(
        _("Select Preview to calculate a weather-feasible, chart-safe route."));
    Layout();
  }

  void OnPreset(wxCommandEvent&) { ApplyPreset(m_preset->GetSelection()); }

  void OnSettingsChanged(wxCommandEvent&) {
    if (m_preset->GetSelection() != 2) m_preset->SetSelection(2);
    InvalidatePreview();
  }

  void OnPreview(wxCommandEvent&) {
    m_options.max_cross_track_error_nm = m_crossTrack->GetValue();
    m_options.max_eta_penalty_minutes = m_etaPenalty->GetValue();
    m_previewButton->Enable(false);
    m_summary->SetLabel(
        _("Checking route geometry, weather and chart safety..."));
    Layout();
    Update();

    m_result = m_preview(m_options);
    if (!m_result.success) {
      m_summary->SetLabel(wxString::Format(
          _("No simplification available.\n%s"), m_result.failure_reason));
      m_useButton->Enable(false);
    } else {
      const long eta_seconds = wxRound(m_result.estimated_eta_change_seconds);
      const wxString eta = wxString::Format(
          "%s%ld:%02ld", eta_seconds >= 0 ? "+" : "-",
          std::labs(eta_seconds) / 60, std::labs(eta_seconds) % 60);
      m_summary->SetLabel(wxString::Format(
          _("Original points: %d\nSimplified points: %d\n"
            "Maximum deviation: %.2f NM\nEstimated ETA change: %s\n"
            "Chart safety: Pass"),
          m_result.original_points, m_result.simplified_points,
          m_result.max_deviation_nm, eta));
      m_useButton->Enable(m_result.simplified_points <
                          m_result.original_points);
    }
    m_previewButton->Enable(true);
    m_summary->Wrap(430);
    Fit();
    Layout();
  }

  PreviewFunction m_preview;
  wxChoice* m_preset;
  wxSpinCtrlDouble* m_crossTrack;
  wxSpinCtrlDouble* m_etaPenalty;
  wxStaticText* m_summary;
  wxButton* m_previewButton;
  wxButton* m_useButton;
  RouteSimplificationOptions m_options;
  RouteSimplificationResult m_result;
};

}  // namespace

static wxString EnvString(const char* name) {
  const char* value = getenv(name);
  return value ? wxString::FromUTF8(value) : wxString();
}

static long EnvLong(const char* name, long default_value) {
  const char* value = getenv(name);
  if (!value || !*value) return default_value;
  char* end = nullptr;
  long parsed = strtol(value, &end, 10);
  return end && *end == '\0' ? parsed : default_value;
}

static double EnvDouble(const char* name, double default_value) {
  const char* value = getenv(name);
  if (!value || !*value) return default_value;
  char* end = nullptr;
  double parsed = strtod(value, &end);
  return end && *end == '\0' ? parsed : default_value;
}

static void FinishHeadlessRouteTestProcess(int exit_code = 0) {
  weather_routing::chart_safety_host::FlushCache();
  wxLog::FlushActive();
  const wxString no_exit = EnvString("WR_HEADLESS_NO_EXIT");
  if (no_exit.IsSameAs("resident")) return;
  if (!no_exit.IsSameAs("1")) {
    wxLogMessage("WR_HEADLESS_ROUTE_TEST process_exit code=%d", exit_code);
    wxLog::FlushActive();
    _Exit(exit_code);
  }
  wxTheApp->ExitMainLoop();
}

static bool TextMatchesFilter(const wxString& text, const wxString& filter) {
  return filter.IsEmpty() || text.Lower().Find(filter.Lower()) != wxNOT_FOUND;
}

static void EnsureRuntimePosition(const wxString& name, double lat,
                                  double lon) {
  for (auto& position : RouteMap::Positions) {
    if (position.GUID.IsEmpty() && position.Name == name) {
      position.lat = lat;
      position.lon = lon;
      wxLogMessage(
          "WR_HEADLESS_ROUTE_TEST runtime_position_reused name=\"%s\" "
          "lat=%.6f lon=%.6f.",
          name, lat, lon);
      return;
    }
  }

  RouteMap::Positions.push_back(RouteMapPosition(name, lat, lon));
  wxLogMessage(
      "WR_HEADLESS_ROUTE_TEST runtime_position_added name=\"%s\" "
      "lat=%.6f lon=%.6f.",
      name, lat, lon);
}

static void ReadExperimentalChartSafetySettings(bool& use_chart_safety,
                                                bool& enforce_chart_safety) {
  // The standard-plugin first-run policy is stock-safe and opt-in. Explicit
  // saved choices continue to win, and the availability guard below disables
  // both options on an unchanged host.
  use_chart_safety =
      weather_routing::chart_safety_defaults::kCheckLoadedCharts;
  enforce_chart_safety =
      weather_routing::chart_safety_defaults::kRequireChartDepthChecks;
  wxFileConfig* pConf = GetOCPNConfigObject();
  pConf->SetPath(_T( "/PlugIns/WeatherRouting" ));
  pConf->Read(
      _T("UseExperimentalChartSafety"), &use_chart_safety,
      weather_routing::chart_safety_defaults::kCheckLoadedCharts);
  pConf->Read(_T("EnforceExperimentalChartSafety"), &enforce_chart_safety,
              weather_routing::chart_safety_defaults::kRequireChartDepthChecks);
  wxString use_override = EnvString("WR_HEADLESS_CHART_SAFETY_USE");
  wxString enforce_override = EnvString("WR_HEADLESS_CHART_SAFETY_ENFORCE");
  if (!use_override.IsEmpty())
    use_chart_safety =
        use_override.IsSameAs("1") || use_override.IsSameAs("true", false);
  if (!enforce_override.IsEmpty())
    enforce_chart_safety = enforce_override.IsSameAs("1") ||
                           enforce_override.IsSameAs("true", false);

  // Persisted preferences may have been written while the same profile was
  // used with an enhanced OpenCPN build.  They must not make the stock-host
  // plugin unusable: without the optional dynamically resolved service the
  // effective runtime mode is the standard GSHHS land checker.
  if (!weather_routing::chart_safety_host::Available()) {
    use_chart_safety = false;
    enforce_chart_safety = false;
  }
}

static void ApplyAuthoritativeChartSearchPolicy(
    RouteMapConfiguration& configuration) {
  bool use_chart_safety = false;
  bool enforce_chart_safety = false;
  ReadExperimentalChartSafetySettings(use_chart_safety,
                                      enforce_chart_safety);
  configuration.chart_safety_runtime_available = use_chart_safety;
  configuration.chart_safety_runtime_enforced = enforce_chart_safety;
  configuration.UseChartSafetyForPropagation =
      weather_routing::ShouldUseAuthoritativeChartSearch(
          configuration.DetectLand, use_chart_safety, enforce_chart_safety,
          configuration.chart_safety_scout_preview);
}

static void ApplyHeadlessRouteSafetyOverrides(
    RouteMapConfiguration& configuration, const wxString& context) {
  wxString detect_land_override = EnvString("WR_HEADLESS_DETECT_LAND");
  if (!detect_land_override.IsEmpty()) {
    configuration.DetectLand = detect_land_override.IsSameAs("1") ||
                               detect_land_override.IsSameAs("true", false);
    wxLogMessage("WR_HEADLESS_ROUTE_TEST override context=%s DetectLand=%d.",
                 context, configuration.DetectLand ? 1 : 0);
  }

  wxString margin_override = EnvString("WR_HEADLESS_SAFETY_MARGIN_NM");
  if (!margin_override.IsEmpty()) {
    double margin_nm = EnvDouble("WR_HEADLESS_SAFETY_MARGIN_NM",
                                 configuration.SafetyMarginLand);
    if (std::isfinite(margin_nm)) {
      configuration.SafetyMarginLand = wxMax(0.0, margin_nm);
      wxLogMessage(
          "WR_HEADLESS_ROUTE_TEST override context=%s "
          "SafetyMarginLand=%.3f.",
          context, configuration.SafetyMarginLand);
    } else {
      wxLogMessage(
          "WR_HEADLESS_ROUTE_TEST warning invalid "
          "WR_HEADLESS_SAFETY_MARGIN_NM=\"%s\"; keeping %.3f.",
          margin_override, configuration.SafetyMarginLand);
    }
  }
}

static const int kDefaultMaxChartSafetyMissingTileRetries = 16;
// Scouts stop at a deterministic generated-state limit configured by the
// native adapter. This watchdog is only a deadlock/stall escape hatch. If it
// fires, a meaningful partial frontier is retained only as a broad spatial
// preparation hint. It never becomes route-safety evidence.
// The scout is only a chart-cache hint.  Keep its synchronous wait short: a
// long wait delays the authoritative solve, and yielding the wx event loop
// here is unsafe because callers already run from GUI event handlers.
static const long kChartSafetyScoutWatchdogMs = 2000;

static const char* RouteStartTypeName(
    RouteMapConfiguration::StartDataType type) {
  switch (type) {
    case RouteMapConfiguration::START_FROM_BOAT:
      return "boat";
    case RouteMapConfiguration::START_FROM_WAYPOINT:
      return "waypoint";
    case RouteMapConfiguration::START_FROM_POSITION:
    default:
      return "position";
  }
}

static const char* RouteEndTypeName(RouteMapConfiguration::EndDataType type) {
  switch (type) {
    case RouteMapConfiguration::END_AT_WAYPOINT:
      return "waypoint";
    case RouteMapConfiguration::END_AT_POSITION:
    default:
      return "position";
  }
}

static int ReadExperimentalChartSafetyMissingTileMaxRetries() {
  long retries = kDefaultMaxChartSafetyMissingTileRetries;
  wxFileConfig* pConf = GetOCPNConfigObject();
  pConf->SetPath(_T( "/PlugIns/WeatherRouting" ));
  pConf->Read(_T("ExperimentalChartSafetyMissingTileMaxRetries"), &retries,
              static_cast<long>(kDefaultMaxChartSafetyMissingTileRetries));
  return static_cast<int>(wxMax(1L, wxMin(64L, retries)));
}

static PlugInSegmentSafetyOptions ChartSafetyRouteMaskOptions(
    const RouteMapConfiguration& configuration) {
  PlugInSegmentSafetyOptions options = {};
  options.struct_size = sizeof(options);
  options.safety_margin_nm = wxMax(0.0, configuration.SafetyMarginLand);
  options.check_land = 1;
  options.allow_gshhs_fallback = 0;
  weather_routing::ApplyMinimumDepthPolicy(options,
                                           configuration.MinimumDepthMeters);
  return options;
}

static PlugInSegmentSafetyOptions ChartSafetySearchRouteMaskOptions(
    const RouteMapConfiguration& configuration) {
  PlugInSegmentSafetyOptions options =
      ChartSafetyRouteMaskOptions(configuration);
  // The native engine deliberately adds a very small guard band while
  // exploring so a state accepted at a raster-cell boundary cannot become a
  // final-route margin failure through rounding.  Prewarm that exact mask as
  // well as the configured final-validation mask.
  if (options.safety_margin_nm > 0.0) options.safety_margin_nm += 0.01;
  return options;
}

static bool EndpointMeetsMinimumDepth(
    const RouteMapConfiguration& configuration, double latitude,
    double longitude, const wxString& endpoint_name, wxString* failure_reason) {
  if (configuration.MinimumDepthMeters <= 0.0) return true;

  PlugInSegmentSafetyOptions options = {};
  options.struct_size = sizeof(options);
  options.safety_margin_nm = 0.0;
  options.check_land = 1;
  options.allow_gshhs_fallback = 0;
  weather_routing::ApplyMinimumDepthPolicy(
      options, configuration.MinimumDepthMeters);

  PlugInSegmentSafetyResult result = {};
  result.struct_size = sizeof(result);
  bool queried = weather_routing::chart_safety_host::CheckSegment(
      latitude, longitude, latitude, longitude, &options, &result);
  for (int retry = 0;
       queried && result.status == PI_SEGMENT_SAFETY_PENDING_DATA && retry < 4;
       ++retry) {
    PlugInSegmentSafetyRequestServiceResult service = {};
    service.struct_size = sizeof(service);
    weather_routing::chart_safety_host::ServicePendingRequests(
        8, 250, &service);
    result = {};
    result.struct_size = sizeof(result);
    queried = weather_routing::chart_safety_host::CheckSegment(
        latitude, longitude, latitude, longitude, &options, &result);
  }
  if (queried && result.status == PI_SEGMENT_SAFETY_SAFE) return true;

  if (failure_reason) {
    if (queried && result.status == PI_SEGMENT_SAFETY_TOO_SHALLOW &&
        result.has_depth && std::isfinite(result.hit_depth_m)) {
      *failure_reason = wxString::Format(
          _("%s charted depth %.1f m is below the configured minimum %.1f m"),
          endpoint_name, result.hit_depth_m,
          configuration.MinimumDepthMeters);
    } else if (queried &&
               result.status == PI_SEGMENT_SAFETY_UNKNOWN_DEPTH) {
      *failure_reason = wxString::Format(
          _("%s has no charted depth proving the configured minimum %.1f m"),
          endpoint_name, configuration.MinimumDepthMeters);
    } else {
      *failure_reason = wxString::Format(
          _("%s does not satisfy the configured minimum depth %.1f m"),
          endpoint_name, configuration.MinimumDepthMeters);
    }
  }
  wxLogMessage(
      "WR_MINIMUM_DEPTH_ENDPOINT_REJECTED route=\"%s -> %s\" "
      "endpoint=\"%s\" lat=%.8f lon=%.8f minimum_depth_m=%.3f "
      "queried=%d status=%d has_depth=%d hit_depth_m=%.3f message=\"%s\"",
      configuration.Start, configuration.End, endpoint_name, latitude,
      longitude, configuration.MinimumDepthMeters, queried ? 1 : 0,
      queried ? result.status : PI_SEGMENT_SAFETY_ERROR,
      queried ? result.has_depth : 0,
      queried ? result.hit_depth_m : std::numeric_limits<double>::quiet_NaN(),
      queried ? wxString::FromUTF8(result.message) : wxString("unavailable"));
  return false;
}

static bool PrewarmChartSafetyHazardSnapshot(
    const std::vector<RouteMapConfiguration>& configurations,
    const wxString& context, bool enable_fast_path) {
  if (configurations.empty()) return false;
  double min_lat = 90.0;
  double max_lat = -90.0;
  double min_lon = 180.0;
  double max_lon = -180.0;
  double maximum_padding_nm = 40.0;
  for (std::vector<RouteMapConfiguration>::const_iterator it =
           configurations.begin();
       it != configurations.end(); ++it) {
    min_lat = wxMin(min_lat, wxMin(it->StartLat, it->EndLat));
    max_lat = wxMax(max_lat, wxMax(it->StartLat, it->EndLat));
    min_lon = wxMin(min_lon, wxMin(it->StartLon, it->EndLon));
    max_lon = wxMax(max_lon, wxMax(it->StartLon, it->EndLon));
    maximum_padding_nm =
        wxMax(maximum_padding_nm,
              0.45 * DistGreatCircle_Plugin(it->StartLat, it->StartLon,
                                            it->EndLat, it->EndLon));
  }
  const double latitude_padding = maximum_padding_nm / 60.0;
  const double maximum_latitude =
      wxMin(89.0, wxMax(fabs(min_lat), fabs(max_lat)) + latitude_padding);
  const double longitude_padding =
      maximum_padding_nm /
      (60.0 * wxMax(0.1, fabs(cos(maximum_latitude * M_PI / 180.0))));
  min_lat = wxMax(-89.0, min_lat - latitude_padding);
  max_lat = wxMin(89.0, max_lat + latitude_padding);
  min_lon = wxMax(-180.0, min_lon - longitude_padding);
  max_lon = wxMin(180.0, max_lon + longitude_padding);

  PlugInSegmentSafetyOptions options =
      ChartSafetyRouteMaskOptions(configurations.front());
  PlugInSegmentSafetyResult result = {};
  result.struct_size = sizeof(result);
  wxString shadow_only_value;
  const bool shadow_only =
      wxGetEnv("WR_HAZARD_SNAPSHOT_SHADOW_ONLY", &shadow_only_value) &&
      shadow_only_value != "0";
  const bool effective_fast_path = enable_fast_path && !shadow_only;
  const bool ok = weather_routing::chart_safety_host::PrewarmHazardSnapshot(
      min_lat, min_lon, max_lat, max_lon, effective_fast_path ? 1 : 0, 1,
      &options, &result);
  wxLogMessage(
      "WR_HAZARD_SNAPSHOT_PREWARM context=%s routes=%lu ok=%d "
      "fast_path=%d shadow_only=%d area=[%.6f..%.6f,%.6f..%.6f] "
      "charts=%d supported=%d hazard_rings=%d elapsed_ms=%d message=\"%s\"",
      context, static_cast<unsigned long>(configurations.size()), ok ? 1 : 0,
      effective_fast_path ? 1 : 0, shadow_only ? 1 : 0, min_lat, max_lat,
      min_lon, max_lon, result.candidate_chart_count, result.s57_chart_count,
      result.land_ring_count, result.cache_build_ms, result.message);
  // A successfully constructed snapshot may still contain only chart types
  // which cannot provide deterministic immutable SAFE certificates (notably
  // CM93 composites).  Callers should retain the authoritative route-mask
  // prewarm unless at least one supported vector chart was captured.
  return ok && result.s57_chart_count > 0;
}

static wxString ChartSafetyRouteFamilyKey(
    const RouteMapConfiguration& configuration) {
  PlugInSegmentSafetyOptions options =
      ChartSafetyRouteMaskOptions(configuration);
  wxString key = wxString::Format(
      "route-shape:%.7f:%.7f:%.7f:%.7f:sm%.4f:land%d:depth%d:md%.3f:"
      "dt%.0f:boat=%s",
      configuration.StartLat, configuration.StartLon, configuration.EndLat,
      configuration.EndLon, options.safety_margin_nm, options.check_land,
      options.check_depth, options.minimum_depth_m, configuration.DeltaTime,
      configuration.boatFileName);
  key += wxString::Format(":propagation%d",
                          configuration.UseChartSafetyForPropagation ? 1 : 0);
  return key;
}

static wxString ChartSafetySharedPrewarmScopeKey(
    const RouteMapConfiguration& configuration) {
  wxString key = ChartSafetyRouteFamilyKey(configuration);
  if (configuration.DepartureTimeOptimizationCandidate &&
      !configuration.DepartureTimeOptimizationGroupId.IsEmpty()) {
    // Tile payloads are still shared by the host cache, but a scout is part
    // of route semantics: it derives the narrowly bounded start/destination
    // margin reach from that departure's own weather-driven lineage. Reusing
    // the nominal scout for every offset made a hard 08:00 candidate differ
    // from the same route calculated alone.
    key += ":optimization=" + configuration.DepartureTimeOptimizationGroupId;
    key += wxString::Format(
        ":candidate=%+d:departure=%s",
        configuration.DepartureTimeOptimizationOffsetMinutes,
        configuration.StartTime.IsValid()
            ? configuration.StartTime.FormatISOCombined()
            : wxString("invalid"));
  } else if (configuration.TimeMode ==
                 RouteMapConfiguration::ROUTE_BY_ARRIVAL_TIME &&
             configuration.PlannedArrivalTime.IsValid()) {
    key +=
        ":planned-arrival=" +
        configuration.PlannedArrivalTime.FormatISOCombined();
  } else if (configuration.StartTime.IsValid()) {
    key += ":departure=" + configuration.StartTime.FormatISOCombined();
  }
  if (configuration.IsMultiLegGenerated)
    key += wxString::Format(":leg-%d", configuration.MultiLegLegIndex);
  return key;
}

static wxString ChartSafetyScoutEnvelopeGroupKey(
    const RouteMapConfiguration& configuration) {
  wxString key = ChartSafetyRouteFamilyKey(configuration);
  if (configuration.DepartureTimeOptimizationCandidate &&
      !configuration.DepartureTimeOptimizationGroupId.IsEmpty()) {
    // Static land/depth evidence is shared by the complete departure sweep.
    // The scouts themselves remain candidate-specific, so their distinct
    // weather/current frontiers are unioned rather than reused.
    key += ":optimization-family=" +
           configuration.DepartureTimeOptimizationGroupId;
  } else if (configuration.TimeMode ==
                 RouteMapConfiguration::ROUTE_BY_ARRIVAL_TIME &&
             configuration.PlannedArrivalTime.IsValid()) {
    key += ":planned-arrival-family=" +
           configuration.PlannedArrivalTime.FormatISOCombined();
  } else if (configuration.IsMultiLegGenerated) {
    key += wxString::Format(":leg-family-%d", configuration.MultiLegLegIndex);
  }
  return key;
}

static void PrewarmExperimentalChartSafetyForConfiguration(
    const RouteMapConfiguration& configuration, const wxString& context,
    const std::function<void(const wxString&, const wxString&, int, int)>&
        progress =
            std::function<void(const wxString&, const wxString&, int, int)>()) {
  if (!configuration.DetectLand) return;

  bool use_chart_safety = false;
  bool enforce_chart_safety = false;
  ReadExperimentalChartSafetySettings(use_chart_safety, enforce_chart_safety);
  if (!use_chart_safety) return;

  wxString shared_scope = ChartSafetySharedPrewarmScopeKey(configuration);
  if (s_chartSafetySharedPrewarmScopes.find(shared_scope) !=
      s_chartSafetySharedPrewarmScopes.end()) {
    wxLogMessage(
        "WR_ROUTE_MASK_PREWARM_SHARED_REUSE context=%s "
        "route=\"%s to %s\" candidate_offset=%d retry=%d scope=%s",
        context, configuration.Start, configuration.End,
        configuration.DepartureTimeOptimizationOffsetMinutes,
        configuration.chart_safety_missing_tile_retry_count, shared_scope);
    if (progress) {
      progress(
          _("Chart safety grid ready"),
          wxString::Format(_("%s: reusing shared chart safety grid"), context),
          -1, -1);
    }
    return;
  }

  if (progress) {
    progress(_("Building chart safety grid"),
             wxString::Format(_("%s: %s to %s"), context, configuration.Start,
                              configuration.End),
             -1, -1);
  }

  // This is only a fallback for a scout which could not reach the destination.
  // Prewarm route-shaped search hints, not a latitude/longitude rectangle.
  // This is cache preparation only: it neither constrains the solver nor
  // changes the authoritative safety result.  Long ocean passages use a
  // sparse fan of narrow bowed alternatives instead of an enormous filled
  // band.  The outer alternatives scale to 15% of passage length (with an
  // ocean-scale cap), so several-thousand-mile passages cover realistic
  // synoptic diversions while tile count and SSD traffic remain linear.
  double direct_course = 0.0;
  double direct_distance_nm = 0.0;
  ll_gc_ll_reverse(configuration.StartLat, configuration.StartLon,
                   configuration.EndLat, configuration.EndLon, &direct_course,
                   &direct_distance_nm);
  const weather_routing::OceanPrewarmPlan ocean_prewarm =
      weather_routing::BuildOceanPrewarmPlan(direct_distance_nm);
  const bool ocean_passage = ocean_prewarm.enabled;
  const bool direct_crosses_land = PlugIn_GSHHS_CrossesLand(
      configuration.StartLat, configuration.StartLon, configuration.EndLat,
      configuration.EndLon);
  double prewarm_margin_nm =
      ocean_passage
          ? ocean_prewarm.raster_margin_nm
          : wxMin(12.0, wxMax(4.0, direct_distance_nm * 0.05));
  if (direct_crosses_land && !ocean_passage) prewarm_margin_nm = 4.0;
  const double ocean_outer_diversion_nm =
      ocean_prewarm.outer_diversion_nm;
  const double ocean_inner_diversion_nm =
      ocean_prewarm.inner_diversion_nm;
  const int direct_steps =
      wxMax(1, wxMin(64, static_cast<int>(
                              std::ceil(direct_distance_nm / 10.0))));
  std::vector<double> corridor_latitudes;
  std::vector<double> corridor_longitudes;
  std::vector<int> corridor_point_counts;
  double maximum_diversion_nm = 0.0;
  const int maximum_corridors =
      ocean_passage ? ocean_prewarm.corridor_count : 3;
  corridor_latitudes.reserve((direct_steps + 1) * maximum_corridors);
  corridor_longitudes.reserve((direct_steps + 1) * maximum_corridors);
  corridor_point_counts.reserve(maximum_corridors);

  auto append_corridor = [&](double signed_diversion_nm,
                             bool require_gshhs_clear) {
    std::vector<double> latitudes;
    std::vector<double> longitudes;
    latitudes.reserve(direct_steps + 1);
    longitudes.reserve(direct_steps + 1);
    for (int step = 0; step <= direct_steps; ++step) {
      const double fraction = static_cast<double>(step) / direct_steps;
      double latitude = configuration.StartLat;
      double longitude = configuration.StartLon;
      if (step > 0) {
        ll_gc_ll(configuration.StartLat, configuration.StartLon, direct_course,
                 direct_distance_nm * fraction, &latitude, &longitude);
      }
      if (signed_diversion_nm != 0.0 && step > 0 && step < direct_steps) {
        const double bow = std::fabs(signed_diversion_nm) *
                           std::sin(std::acos(-1.0) * fraction);
        double offset_latitude = latitude;
        double offset_longitude = longitude;
        ll_gc_ll(latitude, longitude,
                 direct_course +
                     (signed_diversion_nm < 0.0 ? -90.0 : 90.0),
                 bow,
                 &offset_latitude, &offset_longitude);
        latitude = offset_latitude;
        longitude = offset_longitude;
      }
      latitudes.push_back(latitude);
      longitudes.push_back(longitude);
    }
    if (require_gshhs_clear) {
      for (int step = 1; step <= direct_steps; ++step) {
        if (PlugIn_GSHHS_CrossesLand(
                latitudes[step - 1], longitudes[step - 1], latitudes[step],
                longitudes[step]))
          return false;
      }
    }
    corridor_latitudes.insert(corridor_latitudes.end(), latitudes.begin(),
                              latitudes.end());
    corridor_longitudes.insert(corridor_longitudes.end(), longitudes.begin(),
                               longitudes.end());
    corridor_point_counts.push_back(direct_steps + 1);
    maximum_diversion_nm =
        wxMax(maximum_diversion_nm, std::fabs(signed_diversion_nm));
    return true;
  };

  if (ocean_passage) {
    append_corridor(0.0, false);
    append_corridor(-ocean_inner_diversion_nm, false);
    append_corridor(ocean_inner_diversion_nm, false);
    append_corridor(-ocean_outer_diversion_nm, false);
    append_corridor(ocean_outer_diversion_nm, false);
  } else if (!direct_crosses_land) {
    append_corridor(0.0, false);
  } else {
    const double factors[] = {0.10, 0.20, 0.30, 0.45};
    for (double factor : factors) {
      const double diversion =
          wxMin(120.0, wxMax(12.0, direct_distance_nm * factor));
      if (append_corridor(-diversion, true) &&
          corridor_point_counts.size() >= 2)
        break;
      if (append_corridor(diversion, true) &&
          corridor_point_counts.size() >= 2)
        break;
    }
  }

  PlugInSegmentSafetyResult result = {};
  result.struct_size = sizeof(result);
  PlugInSegmentSafetyResult search_result = {};
  search_result.struct_size = sizeof(search_result);
  bool prewarm_ok = false;
  wxString prewarm_mode = _("corridor");
  PlugInSegmentSafetyOptions options =
      ChartSafetyRouteMaskOptions(configuration);
  const bool modern_native = ModernNativeRouteEnabled(configuration);
  if (!modern_native)
    PrewarmChartSafetyHazardSnapshot(
        std::vector<RouteMapConfiguration>(1, configuration), context, true);
  if (modern_native)
    wxLogMessage(
        "WR_ROUTE_MASK_PREWARM_REQUIRED context=%s route=\"%s to %s\" "
        "engine=modern-native "
        "policy=plugin-owned-raw-tiles-and-derived-masks",
        context, configuration.Start, configuration.End);
  if (corridor_point_counts.empty()) {
    wxLogMessage(
        "WR_ROUTE_MASK_PREWARM_DEFERRED context=%s route=\"%s to %s\" "
        "direct_crosses_land=1 gshhs_alternative_corridors=0 "
        "policy=scout-shape-plus-on-demand-authoritative",
        context, configuration.Start, configuration.End);
    return;
  }
  prewarm_ok = weather_routing::chart_safety_host::
      PrewarmRouteMaskForPolylinesWithTileHalo(
          corridor_latitudes.data(), corridor_longitudes.data(),
          corridor_point_counts.data(),
          static_cast<int>(corridor_point_counts.size()), prewarm_margin_nm, 1,
          &options, &result);
  PlugInSegmentSafetyOptions search_options =
      ChartSafetySearchRouteMaskOptions(configuration);
  const bool search_mask_differs = std::fabs(search_options.safety_margin_nm -
                                             options.safety_margin_nm) > 1e-9;
  const bool search_prewarm_ok =
      !search_mask_differs ||
      weather_routing::chart_safety_host::
          PrewarmRouteMaskForPolylinesWithTileHalo(
              corridor_latitudes.data(), corridor_longitudes.data(),
              corridor_point_counts.data(),
              static_cast<int>(corridor_point_counts.size()),
              prewarm_margin_nm, 1, &search_options, &search_result);
  prewarm_ok = prewarm_ok && search_prewarm_ok;
  if (enforce_chart_safety)
    prewarm_mode =
        ocean_passage
            ? _("ocean-alternative-corridors")
            : (direct_crosses_land ? _("GSHHS-clear alternative corridors")
                                   : _("route-shaped-search-corridor"));
  if (!prewarm_ok) {
    wxLogMessage(
        "WeatherRouting Detect Land: chart safety prewarm failed context=%s "
        "route=\"%s to %s\".",
        context, configuration.Start, configuration.End);
    return;
  }
  s_chartSafetySharedPrewarmScopes.insert(shared_scope);

  wxString message = wxString::Format(
      "WeatherRouting Detect Land: chart safety prewarm context=%s "
      "route=\"%s to %s\" start=(%.6f,%.6f) end=(%.6f,%.6f) "
      "mode=%s safety_margin_nm=%.3f prewarm_margin_nm=%.3f "
      "corridors=%d ocean_diversion_nm=%.1f enforce=%d ",
      context, configuration.Start, configuration.End, configuration.StartLat,
      configuration.StartLon, configuration.EndLat, configuration.EndLon,
      prewarm_mode, configuration.SafetyMarginLand, prewarm_margin_nm,
      static_cast<int>(corridor_point_counts.size()), maximum_diversion_nm,
      enforce_chart_safety ? 1 : 0);
  message += wxString::Format(
      "tile_builds=%d tile_hits=%d build_ms=%d cells=%d land=%d water=%d "
      "drying=%d unknown=%d point_queries=%d point_cache_hits=%d "
      "grid_cache_size=%d grid_cache_evictions=%d.",
      result.grid_cache_misses, result.grid_cache_hits, result.grid_build_ms,
      result.grid_cells_total, result.grid_cells_land, result.grid_cells_water,
      result.grid_cells_drying, result.grid_cells_unknown,
      result.point_cache_misses, result.point_cache_hits,
      result.grid_cache_size, result.grid_cache_evictions);
  if (search_mask_differs)
    message += wxString::Format(
        " search_margin_nm=%.3f search_tile_builds=%d search_tile_hits=%d "
        "search_build_ms=%d.",
        search_options.safety_margin_nm, search_result.grid_cache_misses,
        search_result.grid_cache_hits, search_result.grid_build_ms);
  wxLogMessage("%s", message.c_str());
  if (progress) {
    progress(
        _("Chart safety grid ready"),
        wxString::Format(_("%s: built %d tiles, reused %d, elapsed %.1f s"),
                         context, result.grid_cache_misses,
                         result.grid_cache_hits, result.grid_build_ms / 1000.0),
        -1, -1);
  }
}

wxString GetRouteNameForGuid(const wxString& routeGuid) {
  std::unique_ptr<PlugIn_Route> route = GetRoute_Plugin(routeGuid);
  if (route && !route->m_NameString.IsEmpty()) return route->m_NameString;
  return routeGuid;
}

/**
 * Used for NEflag argument to toSDMM_Plugin function from ocpn_plugin.h.
 * @todo Should probably be declared there instead, and probably be a boolean.
 */
enum NEflag {
  LAT = 1,
  LON = 2,
};

/**
 * Used for precision argument to toSDMM_Plugin function from ocpn_plugin.h.
 **/
enum Precision {
  LO = 0,
  HI = 1,
};

/* XPM */
static const char* eye[] = {"20 20 7 1",
                            ". c none",
                            "# c #000000",
                            "a c #333333",
                            "b c #666666",
                            "c c #999999",
                            "d c #cccccc",
                            "e c #ffffff",
                            "....................",
                            "....................",
                            "....................",
                            "....................",
                            ".......######.......",
                            ".....#aabccb#a#.....",
                            "....#deeeddeebcb#...",
                            "..#aeeeec##aceaec#..",
                            ".#bedaeee####dbcec#.",
                            "#aeedbdabc###bcceea#",
                            ".#bedad######abcec#.",
                            "..#be#d######dadb#..",
                            "...#abac####abba#...",
                            ".....##acbaca##.....",
                            ".......######.......",
                            "....................",
                            "....................",
                            "....................",
                            "....................",
                            "...................."};

WeatherRoute::WeatherRoute() : routemapoverlay(new RouteMapOverlay) {}
WeatherRoute::~WeatherRoute() { delete routemapoverlay; }

const wxString WeatherRouting::column_names[NUM_COLS] = {_("Visible"),
                                                         _("Boat"),
                                                         _("Start Type"),
                                                         _("Start"),
                                                         _("Start Time"),
                                                         _("End"),
                                                         _("End Time"),
                                                         _("Time"),
                                                         _("Distance"),
                                                         _("Avg Speed"),
                                                         _("Max Speed"),
                                                         _("Avg Speed Ground"),
                                                         _("Max Speed Ground"),
                                                         _("Avg Wind"),
                                                         _("Max Wind"),
                                                         _("Max Wind Gust"),
                                                         _("Avg Current"),
                                                         _("Max Current"),
                                                         _("Avg Swell"),
                                                         _("Max Swell"),
                                                         _("Upwind Percentage"),
                                                         _("Port Starboard"),
                                                         _("Tacks"),
                                                         _("Jibes"),
                                                         _("Sail Plan Changes"),
                                                         _("Comfort"),
                                                         _("Wx"),
                                                         _("State")};

static int sortcol, sortorder = 1;
// sort callback. Sort by body.
#if wxCHECK_VERSION(2, 9, 0)
int wxCALLBACK SortWeatherRoutes(wxIntPtr item1, wxIntPtr item2, wxIntPtr list)
#else
int wxCALLBACK SortWeatherRoutes(long item1, long item2, long list)
#endif
{
  wxListCtrl* lc = (wxListCtrl*)list;

  wxListItem it1, it2;

  it1.SetId(lc->FindItem(-1, item1));
  it1.SetColumn(sortcol);

  it2.SetId(lc->FindItem(-1, item2));
  it2.SetColumn(sortcol);

  lc->GetItem(it1);
  lc->GetItem(it2);

  return sortorder * it1.GetText().Cmp(it2.GetText());
}

struct WeatherRouting::HeadlessRouteTestState {
  enum class Kind { SingleRoute, MultiLeg };

  Kind kind{Kind::SingleRoute};
  long timeoutMs{0};
  wxLongLong startedMs{0};
  RouteMapOverlay* selectedRoute{nullptr};
  bool departureOptimization{false};
  bool scenarioLoaded{false};
  weather_routing_engine::RoutingScenario scenario;
  wxString scenarioOutputPath;
  wxString selectedGroup;
};

WeatherRouting::WeatherRouting(wxWindow* parent, weather_routing_pi& plugin)
    : WeatherRoutingBase(parent, wxID_ANY, _("WeatherRouting")),
      m_panel(NULL),
      m_ConfigurationDialog(*this),
      m_ConfigurationBatchDialog(this),
      m_CursorPositionDialog(this),
      // CUSTOMIZATION
      m_RoutePositionDialog(this),
      m_BoatDialog(*this),
      m_SettingsDialog(this),
      m_StabilityCorridorKeepPreference(false),
      m_UpdatingStabilityRouteSelection(false),
      m_StatisticsDialog(this),
      m_ReportDialog(*this),
      m_PlotDialog(*this),
      m_FilterRoutesDialog(this),
      m_bRunning(false),
      m_RoutePreparationDepth(0),
      m_RoutesToRun(0),
      m_bSkipUpdateCurrentItems(false),
      m_ActiveMultiLegCurrentLegIndex(0),
      m_ActiveMultiLegSequence(false),
      m_ApplyingMultiLegGroupSettings(false),
      m_ActiveMultiLegOptimizationCandidateIndex(-1),
      m_ActiveMultiLegOptimizationLegIndex(0),
      m_AppliedMultiLegOptimizationCandidateIndex(-1),
      m_ActiveMultiLegDepartureOptimization(false),
      m_DeferredRoutingStartMode(0),
      m_DeferredRoutingStartPending(false),
      m_ChartSafetyComputeProgressActive(false),
      m_ChartSafetyComputeProgressAll(false),
      m_ChartSafetyComputeProgressTotalRoutes(0),
      m_ChartSafetyComputeProgressStartedRoutes(0),
      m_ChartSafetyComputeProgressCompletedRoutes(0),
      m_RoutingProgressDialog(NULL),
      m_RoutingProgressStage(NULL),
      m_RoutingProgressDetail(NULL),
      m_RoutingProgressTiming(NULL),
      m_RoutingProgressGauge(NULL),
      m_RoutingProgressFinished(false),
      m_bShowConfiguration(false),
      m_bShowConfigurationBatch(false),
      m_bShowRoutePosition(false),
      m_bShowSettings(false),
      m_bShowStatistics(false),
      m_bShowReport(false),
      m_bShowPlot(false),
      m_bShowFilter(false),
      m_mChartAwarenessSettings(NULL),
      m_mStabilityCorridorView(NULL),
      m_weather_routing_pi(plugin),
      m_positionOnRoute(nullptr),
      m_RoutingTablePanel(nullptr) {
  wxFileConfig* pConf = GetOCPNConfigObject();
  pConf->SetPath(_T( "/Plugins/WeatherRouting" ));

  m_mConfiguration->AppendSeparator();
  m_mChartAwarenessSettings = new wxMenuItem(m_mConfiguration, wxID_ANY,
                                             _("Chart Awareness Settings..."),
                                             wxEmptyString, wxITEM_NORMAL);
  m_mConfiguration->Append(m_mChartAwarenessSettings);
  m_mConfiguration->Bind(
      wxEVT_COMMAND_MENU_SELECTED,
      wxCommandEventHandler(WeatherRouting::OnChartAwarenessSettings), this,
      m_mChartAwarenessSettings->GetId());

  m_mView->AppendSeparator();
  m_mStabilityCorridorView =
      new wxMenuItem(m_mView, wxID_ANY, _("Show stability corridor"),
                     wxEmptyString, wxITEM_CHECK);
  m_mView->Append(m_mStabilityCorridorView);
  m_mStabilityCorridorView->Enable(false);
  m_mView->Bind(wxEVT_COMMAND_MENU_SELECTED,
                wxCommandEventHandler(WeatherRouting::OnViewStabilityCorridor),
                this, m_mStabilityCorridorView->GetId());

  wxIcon icon;
  icon.CopyFromBitmap(*_img_WeatherRouting);
  m_ConfigurationDialog.SetIcon(icon);
  m_ConfigurationBatchDialog.SetIcon(icon);
  m_BoatDialog.SetIcon(icon);
  m_SettingsDialog.SetIcon(icon);
  m_StatisticsDialog.SetIcon(icon);
  m_ReportDialog.SetIcon(icon);
  m_PlotDialog.SetIcon(icon);
  m_FilterRoutesDialog.SetIcon(icon);

  m_default_configuration_path = weather_routing_pi::StandardPath() +
                                 _T("WeatherRoutingConfiguration.xml");
  const wxString bundledConfiguration = GetPluginDataDir(PLUGIN_PACKAGE_NAME) +
                                        _T("/data/") +
                                        _T("WeatherRoutingConfiguration.xml");
  if (!wxFileName::FileExists(m_default_configuration_path) &&
      wxFileName::FileExists(bundledConfiguration))
    wxCopyFile(bundledConfiguration, m_default_configuration_path);

  bool forceCopyBoats = false;
  bool forceCopyPolars = false;
  wxString boatsdir = weather_routing_pi::StandardPath() +
                      wxFileName::GetPathSeparator() + _T("boats");
  if (!wxFileName::DirExists(boatsdir)) forceCopyBoats = true;
  wxString polarsdir = weather_routing_pi::StandardPath() +
                       wxFileName::GetPathSeparator() + _T("polars");
  if (!wxFileName::DirExists(polarsdir)) forceCopyPolars = true;

  /* ensure the directories exist */
  wxFileName fn;
  fn.Mkdir(weather_routing_pi::StandardPath(), wxS_DIR_DEFAULT,
           wxPATH_MKDIR_FULL);
  fn.Mkdir(boatsdir, wxS_DIR_DEFAULT, wxPATH_MKDIR_FULL);
  fn.Mkdir(polarsdir, wxS_DIR_DEFAULT, wxPATH_MKDIR_FULL);

  /* if the boats or polars directories did not previously exist, populate them
   */
  if (forceCopyBoats)
    CopyDataFiles(GetPluginDataDir(PLUGIN_PACKAGE_NAME) + _T("/data/boats"),
                  boatsdir);
  if (forceCopyPolars)
    CopyDataFiles(GetPluginDataDir(PLUGIN_PACKAGE_NAME) + _T("/data/polars"),
                  polarsdir);

  int confVersion;
  pConf->Read(_T ( "ConfigVersion" ), &confVersion, 0);

#ifndef __OCPN__ANDROID__
  if (confVersion < PLUGIN_VERSION_MAJOR * 100 + PLUGIN_VERSION_MINOR) {
    wxString title = _("New or updated data available");
    wxString message =
        _("A new version of WeatherRouting has been installed.\n\n"
          "\"Import new boats and polars\" will overwrite the standard boats\n"
          "and polars with newer data. If you have modified this data and not\n"
          "changed the names, your modifications will be overwritten, so be\n"
          "sure to backup your changes. If you have added new polars or boats\n"
          "with exclusive names, they will be kept untouched.\n\n");
    message += _(
        "Import example configurations will overwrite your route\n"
        "configurations with a sample set showing you how WeatherRouting\n"
        "works. Backup your existing configurations if you need.\n\n"
        "Pressing \"OK\" will apply the selected changes, pressing \"Cancel\"\n"
        "will do nothing and you will be asked again on the next launch.");

    wxString confDlgChoices[3] = {_("Import new boats and polars"),
                                  _("Import example configurations")};

    wxMultiChoiceDialog confDlg(this, message, title, 2, confDlgChoices);
    /* check on by default if user starts WR for the first time */
    if (confVersion == 0) {
      wxArrayInt sel;
      sel.Add(0);
      sel.Add(1);
      confDlg.SetSelections(sel);
    }

    if (confDlg.ShowModal() == wxID_OK) {
      wxArrayInt result = confDlg.GetSelections();
      for (size_t i = 0; i < result.GetCount(); i++) {
        if (result[i] == 0) {
          CopyDataFiles(
              GetPluginDataDir(PLUGIN_PACKAGE_NAME) + _T("/data/boats"),
              boatsdir);
          CopyDataFiles(
              GetPluginDataDir(PLUGIN_PACKAGE_NAME) + _T("/data/polars"),
              polarsdir);
        } else if (result[i] == 1) {
          wxString cfg = GetPluginDataDir(PLUGIN_PACKAGE_NAME) + _T("/data/") +
                         _T("WeatherRoutingConfiguration.xml");
          if (wxFileName::FileExists(cfg))
            wxCopyFile(cfg, m_default_configuration_path);
        }
      }
      pConf->SetPath(_T( "/PlugIns/WeatherRouting" ));  // path can change after
                                                        // modal dialog
      pConf->Write(_T ( "ConfigVersion" ),
                   PLUGIN_VERSION_MAJOR * 100 + PLUGIN_VERSION_MINOR);
    }
  }
#endif
  m_SettingsDialog.LoadSettings();
  m_ConfigurationDialog.RefreshTimeZoneControls();

  pConf->SetPath(
      _T( "/PlugIns/WeatherRouting" ));  // path can change after modal dialog
  pConf->Read(_T("KeepStabilityCorridorAfterResults"),
              &m_StabilityCorridorKeepPreference, false);
  pConf->Read(_T ( "DisableColPane" ), &m_disable_colpane, false);
#ifdef __OCPN__ANDROID__
  m_disable_colpane = true;
#endif

  wxBoxSizer* bSizer;
  bSizer = new wxBoxSizer(wxVERTICAL);
  this->SetSizer(bSizer);
  if (!m_disable_colpane) {
    m_colpane = new wxCollapsiblePane(this, wxID_ANY, _("Weather Routing"),
                                      wxDefaultPosition, wxDefaultSize,
                                      wxCP_NO_TLW_RESIZE);
    bSizer->Add(m_colpane, 1, wxEXPAND | wxALL, 5);
    m_colpaneWindow = m_colpane->GetPane();
    wxSizer* paneSz = new wxBoxSizer(wxVERTICAL);
    m_colpaneWindow->SetSizer(paneSz);
    m_panel = new WeatherRoutingPanel(m_colpaneWindow);
    paneSz->Add(m_panel, 1, wxEXPAND, 0);
    paneSz->SetSizeHints(m_colpaneWindow);
  } else {
    m_colpane = NULL;
    m_colpaneWindow = this;
    m_panel = new WeatherRoutingPanel(m_colpaneWindow);
    bSizer->Add(m_panel, 1, wxEXPAND, 0);
  }
  bSizer->SetSizeHints(this);

  m_panel->m_lPositions->InsertColumn(POSITION_NAME, _("Name"));
  m_panel->m_lPositions->InsertColumn(POSITION_LAT, _("Lat"));
  m_panel->m_lPositions->InsertColumn(POSITION_LON, _("Lon"));
  m_panel->m_lWeatherRoutes->SetToolTip(
      _("Wx reports wind sources used by the accepted route. Orange route "
        "wind barbs mark climatology-sourced legs."));

  wxImageList* imglist = new wxImageList(20, 20, true, 1);
  imglist->Add(wxBitmap(eye));
  m_panel->m_lWeatherRoutes->AssignImageList(imglist, wxIMAGE_LIST_SMALL);

  UpdateColumns();

  if (m_colpane) m_colpane->Expand();

  if (EnvString("WR_HEADLESS_SCENARIO").IsEmpty()) {
    OpenXML(m_default_configuration_path, false);
  } else {
    wxLogMessage(
        "WR_HEADLESS_SCENARIO self_contained=1 "
        "saved_configuration_load_skipped=1");
  }

  wxPoint p = GetPosition();
  pConf->Read(_T ( "DialogX" ), &p.x, p.x);
  pConf->Read(_T ( "DialogY" ), &p.y, p.y);

  m_size = DefaultWeatherRoutingDialogSize(this);
  pConf->Read(_T("DialogWidth"), &m_size.x, m_size.x);
  pConf->Read(_T("DialogHeight"), &m_size.y, m_size.y);
#ifdef __OCPN__ANDROID__
  wxSize sz = ::wxGetDisplaySize();
  m_size.x = sz.x * 3 / 5;
  m_size.y = sz.y * 2 / 5;
  int y = 2 * sz.y / 3 - 40;
  if (m_size.y > y) m_size.y = y;
#endif
  SetSize(p.x, p.y, m_size.x, m_size.y);

  pConf->Read(_T ( "DialogSplit" ), &sashpos, 0);

  /* periodically check for updates from computation thread */
  m_tCompute.Connect(wxEVT_TIMER,
                     wxTimerEventHandler(WeatherRouting::OnComputationTimer),
                     NULL, this);

  m_tHideConfiguration.Connect(
      wxEVT_TIMER,
      wxTimerEventHandler(WeatherRouting::OnHideConfigurationTimer), NULL,
      this);
  m_tRoutingProgress.Connect(
      wxEVT_TIMER, wxTimerEventHandler(WeatherRouting::OnRoutingProgressTimer),
      NULL, this);
  m_tDeferredRoutingStart.Connect(
      wxEVT_TIMER, wxTimerEventHandler(WeatherRouting::OnDeferredRoutingStart),
      NULL, this);
  m_tHeadlessRouteTest.Connect(
      wxEVT_TIMER, wxTimerEventHandler(WeatherRouting::OnHeadlessRouteTestTimer),
      NULL, this);

  m_tAutoSaveXML.Connect(
      wxEVT_TIMER, wxTimerEventHandler(WeatherRouting::OnAutoSaveXMLTimer),
      NULL, this);

  Connect(wxEVT_IDLE, wxTimerEventHandler(WeatherRouting::OnRenderedTimer),
          NULL, this);

  SetEnableConfigurationMenu();

  // Connect Events
  if (m_colpane)
    m_colpane->Connect(
        wxEVT_COLLAPSIBLEPANE_CHANGED,
        wxCollapsiblePaneEventHandler(WeatherRouting::OnCollPaneChanged), NULL,
        this);
  m_panel->m_lPositions->Connect(
      wxEVT_LEFT_DCLICK,
      wxMouseEventHandler(WeatherRouting::OnEditPositionClick), NULL, this);
  m_panel->m_lPositions->Connect(
      wxEVT_COMMAND_LIST_KEY_DOWN,
      wxListEventHandler(WeatherRouting::OnPositionKeyDown), NULL, this);
  m_panel->m_lWeatherRoutes->Connect(
      wxEVT_LEFT_DCLICK,
      wxMouseEventHandler(WeatherRouting::OnEditConfigurationClick), NULL,
      this);
  m_panel->m_lWeatherRoutes->Connect(
      wxEVT_LEFT_DOWN,
      wxMouseEventHandler(WeatherRouting::OnWeatherRoutesListLeftDown), NULL,
      this);
  m_panel->m_lWeatherRoutes->Connect(
      wxEVT_LEFT_UP, wxMouseEventHandler(WeatherRouting::OnLeftUp), NULL, this);
  m_panel->m_lWeatherRoutes->Connect(
      wxEVT_RIGHT_UP, wxMouseEventHandler(WeatherRouting::OnRightUp), NULL,
      this);
  m_panel->m_lPositions->Connect(
      wxEVT_LEFT_DOWN, wxMouseEventHandler(WeatherRouting::OnLeftDown), NULL,
      this);
  m_panel->m_lPositions->Connect(
      wxEVT_LEFT_UP, wxMouseEventHandler(WeatherRouting::OnLeftUp), NULL, this);
  m_panel->m_lWeatherRoutes->Connect(
      wxEVT_COMMAND_LIST_COL_CLICK,
      wxListEventHandler(WeatherRouting::OnWeatherRouteSort), NULL, this);
  m_panel->m_lWeatherRoutes->Connect(
      wxEVT_COMMAND_LIST_ITEM_DESELECTED,
      wxListEventHandler(WeatherRouting::OnWeatherRouteSelected), NULL, this);
  m_panel->m_lWeatherRoutes->Connect(
      wxEVT_COMMAND_LIST_ITEM_SELECTED,
      wxListEventHandler(WeatherRouting::OnWeatherRouteSelected), NULL, this);
  m_panel->m_lWeatherRoutes->Connect(
      wxEVT_COMMAND_LIST_KEY_DOWN,
      wxListEventHandler(WeatherRouting::OnWeatherRouteKeyDown), NULL, this);
  m_panel->m_bCompute->Connect(wxEVT_COMMAND_BUTTON_CLICKED,
                               wxCommandEventHandler(WeatherRouting::OnCompute),
                               NULL, this);
  m_panel->m_bSaveAsTrack->Connect(
      wxEVT_COMMAND_BUTTON_CLICKED,
      wxCommandEventHandler(WeatherRouting::OnSaveAsTrack), NULL, this);
  m_panel->m_bSimplifyRoute->Connect(
      wxEVT_COMMAND_BUTTON_CLICKED,
      wxCommandEventHandler(WeatherRouting::OnSimplifyRoute), NULL, this);
  m_panel->m_bSaveAsRoute->Connect(
      wxEVT_COMMAND_BUTTON_CLICKED,
      wxCommandEventHandler(WeatherRouting::OnSaveAsRoute), NULL, this);
  m_panel->m_bExportRoute->Connect(
      wxEVT_COMMAND_BUTTON_CLICKED,
      wxCommandEventHandler(WeatherRouting::OnExportRouteAsGPX), NULL, this);

#ifdef __OCPN__ANDROID__
  GetHandle()->setAttribute(Qt::WA_AcceptTouchEvents);
  GetHandle()->grabGesture(Qt::PanGesture);
  GetHandle()->setStyleSheet(qtStyleSheet);

  GetHandle()->setAttribute(Qt::WA_AcceptTouchEvents);
  GetHandle()->grabGesture(Qt::PanGesture);
  Connect(
      wxEVT_QT_PANGESTURE,
      (wxObjectEventFunction)(wxEventFunction)&WeatherRouting::OnEvtPanGesture,
      NULL, this);
  m_tDownTimer.Connect(wxEVT_TIMER,
                       wxTimerEventHandler(WeatherRouting::OnDownTimer), NULL,
                       this);
#endif
}

WeatherRouting::~WeatherRouting() {
  // Disconnect Events
  if (m_mStabilityCorridorView) {
    m_mView->Unbind(wxEVT_COMMAND_MENU_SELECTED,
                    &WeatherRouting::OnViewStabilityCorridor, this,
                    m_mStabilityCorridorView->GetId());
  }
  if (m_mChartAwarenessSettings) {
    m_mConfiguration->Unbind(wxEVT_COMMAND_MENU_SELECTED,
                             &WeatherRouting::OnChartAwarenessSettings, this,
                             m_mChartAwarenessSettings->GetId());
  }
  if (m_colpane)
    m_colpane->Disconnect(
        wxEVT_COLLAPSIBLEPANE_CHANGED,
        wxCollapsiblePaneEventHandler(WeatherRouting::OnCollPaneChanged), NULL,
        this);
  m_panel->m_lPositions->Disconnect(
      wxEVT_LEFT_DCLICK,
      wxMouseEventHandler(WeatherRouting::OnEditPositionClick), NULL, this);
  m_panel->m_lPositions->Disconnect(
      wxEVT_COMMAND_LIST_KEY_DOWN,
      wxListEventHandler(WeatherRouting::OnPositionKeyDown), NULL, this);
  m_panel->m_lWeatherRoutes->Disconnect(
      wxEVT_LEFT_DCLICK,
      wxMouseEventHandler(WeatherRouting::OnEditConfigurationClick), NULL,
      this);
  m_panel->m_lWeatherRoutes->Disconnect(
      wxEVT_LEFT_DOWN,
      wxMouseEventHandler(WeatherRouting::OnWeatherRoutesListLeftDown), NULL,
      this);
  m_panel->m_lWeatherRoutes->Disconnect(
      wxEVT_LEFT_UP, wxMouseEventHandler(WeatherRouting::OnLeftUp), NULL, this);
  m_panel->m_lWeatherRoutes->Disconnect(
      wxEVT_RIGHT_UP, wxMouseEventHandler(WeatherRouting::OnRightUp), NULL,
      this);
  m_panel->m_lWeatherRoutes->Disconnect(
      wxEVT_COMMAND_LIST_COL_CLICK,
      wxListEventHandler(WeatherRouting::OnWeatherRouteSort), NULL, this);
  m_panel->m_lWeatherRoutes->Disconnect(
      wxEVT_COMMAND_LIST_ITEM_DESELECTED,
      wxListEventHandler(WeatherRouting::OnWeatherRouteSelected), NULL, this);
  m_panel->m_lWeatherRoutes->Disconnect(
      wxEVT_COMMAND_LIST_ITEM_SELECTED,
      wxListEventHandler(WeatherRouting::OnWeatherRouteSelected), NULL, this);
  m_panel->m_lWeatherRoutes->Disconnect(
      wxEVT_COMMAND_LIST_KEY_DOWN,
      wxListEventHandler(WeatherRouting::OnWeatherRouteKeyDown), NULL, this);
  m_panel->m_bCompute->Disconnect(
      wxEVT_COMMAND_BUTTON_CLICKED,
      wxCommandEventHandler(WeatherRouting::OnCompute), NULL, this);
  m_panel->m_bSaveAsTrack->Disconnect(
      wxEVT_COMMAND_BUTTON_CLICKED,
      wxCommandEventHandler(WeatherRouting::OnSaveAsTrack), NULL, this);
  m_panel->m_bSimplifyRoute->Disconnect(
      wxEVT_COMMAND_BUTTON_CLICKED,
      wxCommandEventHandler(WeatherRouting::OnSimplifyRoute), NULL, this);
  m_panel->m_bSaveAsRoute->Disconnect(
      wxEVT_COMMAND_BUTTON_CLICKED,
      wxCommandEventHandler(WeatherRouting::OnSaveAsRoute), NULL, this);
  m_panel->m_bExportRoute->Disconnect(
      wxEVT_COMMAND_BUTTON_CLICKED,
      wxCommandEventHandler(WeatherRouting::OnExportRouteAsGPX), NULL, this);

  m_tAutoSaveXML.Disconnect(
      wxEVT_TIMER, wxTimerEventHandler(WeatherRouting::OnAutoSaveXMLTimer),
      NULL, this);
  m_tRoutingProgress.Disconnect(
      wxEVT_TIMER, wxTimerEventHandler(WeatherRouting::OnRoutingProgressTimer),
      NULL, this);
  m_tDeferredRoutingStart.Disconnect(
      wxEVT_TIMER, wxTimerEventHandler(WeatherRouting::OnDeferredRoutingStart),
      NULL, this);
  if (m_tHeadlessRouteTest.IsRunning()) m_tHeadlessRouteTest.Stop();
  m_HeadlessRouteTestState.reset();
  m_tHeadlessRouteTest.Disconnect(
      wxEVT_TIMER, wxTimerEventHandler(WeatherRouting::OnHeadlessRouteTestTimer),
      NULL, this);

  StopAll();
  if (m_RoutingProgressDialog) {
    m_RoutingProgressDialog->Destroy();
    m_RoutingProgressDialog = NULL;
    m_RoutingProgressStage = NULL;
    m_RoutingProgressDetail = NULL;
    m_RoutingProgressTiming = NULL;
    m_RoutingProgressGauge = NULL;
  }

  const bool headless_route_test =
      !EnvString("WR_HEADLESS_ROUTE_TEST").IsEmpty();
  if (!headless_route_test) {
    m_SettingsDialog.SaveSettings();
  } else {
    wxLogMessage(
        "WR_HEADLESS_ROUTE_TEST shutdown: skipping interactive settings dialog "
        "save to avoid dereferencing destroyed controls during headless app "
        "teardown.");
  }

  if (!headless_route_test) {
    wxFileConfig* pConf = GetOCPNConfigObject();
    pConf->SetPath(_T( "/PlugIns/WeatherRouting" ));

    wxPoint p = GetPosition();
    pConf->Write(_T ( "DialogX" ), p.x);
    pConf->Write(_T ( "DialogY" ), p.y);

    pConf->Write(_T ( "DialogWidth" ), m_size.x);
    pConf->Write(_T ( "DialogHeight" ), m_size.y);
    pConf->Write(_T ( "DialogSplit" ), m_panel->m_splitter1->GetSashPosition());

    SaveXML(m_FileName.GetFullPath());
  } else {
    wxLogMessage(
        "WR_HEADLESS_ROUTE_TEST shutdown: skipping Weather Routing dialog/XML "
        "settings save because OpenCPN config services may already be "
        "tearing down.");
  }

  // Clean up routing table panel if it exists
  if (m_RoutingTablePanel) {
    m_RoutingTablePanel->SetRouteMap(nullptr);
    wxAuiManager* pauimgr = ::GetFrameAuiManager();
    pauimgr->DetachPane(m_RoutingTablePanel);
    m_RoutingTablePanel->Destroy();
    m_RoutingTablePanel = nullptr;
  }

  for (std::list<WeatherRoute*>::iterator it = m_WeatherRoutes.begin();
       it != m_WeatherRoutes.end(); it++)
    delete *it;
  delete m_panel;
  delete m_colpane;
}

void WeatherRouting::RequestGribTimelineFrame(
    RouteMapOverlay* routeMapOverlay, const wxDateTime& time) {
  if (!routeMapOverlay || !time.IsValid()) return;

  const wxString requestToken = wxString::Format(
      "wr-%llu", static_cast<unsigned long long>(m_NextGribRequestToken++));
  {
    std::lock_guard<std::mutex> lock(m_GribRequestMutex);
    m_PendingGribRequests.emplace(
        requestToken,
        PendingGribRequest{routeMapOverlay,
                           static_cast<std::int64_t>(time.GetTicks())});
  }

  /*
   * OpenCPN currently delivers plugin messages synchronously.  The token makes
   * the ownership explicit and prevents a nested or unrelated timeline reply
   * from being published to the wrong departure worker.  If a provider does
   * not answer in this call, fail the request closed rather than retaining a
   * raw route pointer beyond its guaranteed lifetime.
   */
  routeMapOverlay->RequestGrib(time, requestToken);

  bool unanswered = false;
  {
    std::lock_guard<std::mutex> lock(m_GribRequestMutex);
    unanswered = m_PendingGribRequests.erase(requestToken) != 0;
  }
  if (unanswered) {
    routeMapOverlay->Lock();
    routeMapOverlay->SetNewGrib(static_cast<GribRecordSet*>(nullptr),
                                static_cast<std::int64_t>(time.GetTicks()));
    routeMapOverlay->Unlock();
    wxLogWarning(
        "WR_GRIB_BROKER no synchronous response token=%s timeline=%s",
        requestToken, time.FormatISOCombined());
  }
}

void WeatherRouting::HandleGribTimelineFrame(const wxString& requestToken,
                                             GribRecordSet* frame) {
  RouteMapOverlay* routeMapOverlay = nullptr;
  std::int64_t expectedTimelineKey = -1;
  {
    std::lock_guard<std::mutex> lock(m_GribRequestMutex);
    const auto found = m_PendingGribRequests.find(requestToken);
    if (found == m_PendingGribRequests.end()) {
      wxLogWarning("WR_GRIB_BROKER ignored unmatched response token=%s",
                   requestToken);
      return;
    }
    routeMapOverlay = found->second.route_map_overlay;
    expectedTimelineKey = found->second.timeline_key;
    m_PendingGribRequests.erase(found);
  }

  routeMapOverlay->Lock();
  routeMapOverlay->SetNewGrib(frame, expectedTimelineKey);
  routeMapOverlay->Unlock();
  wxLogMessage(
      "WR_GRIB_BROKER delivered token=%s timeline_key=%lld valid=%d",
      requestToken, static_cast<long long>(expectedTimelineKey),
      frame != nullptr ? 1 : 0);
}

#ifdef __OCPN__ANDROID__
void WeatherRouting::OnEvtPanGesture(wxQT_PanGestureEvent& event) {
  switch (event.GetState()) {
    case GestureStarted:
      m_startPos = GetPosition();
      m_startMouse = event.GetCursorPos();  // g_mouse_pos_screen;
      break;
    default: {
      wxPoint pos = event.GetCursorPos();
      int x = wxMax(0, pos.x + m_startPos.x - m_startMouse.x);
      int y = wxMax(0, pos.y + m_startPos.y - m_startMouse.y);
      int xmax = ::wxGetDisplaySize().x - GetSize().x;
      x = wxMin(x, xmax);
      int ymax =
          ::wxGetDisplaySize().y - GetSize().y;  // Some fluff at the bottom
      y = wxMin(y, ymax);

      Move(x, y);
      m_tDownTimer.Stop();
    } break;
  }
}
#endif

void WeatherRouting::OnLeftDown(wxMouseEvent& event) {
  m_tDownTimer.Start(1200, true);
  m_downPos = event.GetPosition();
  event.Skip();
}

void WeatherRouting::OnLeftUp(wxMouseEvent& event) { m_tDownTimer.Stop(); }

void WeatherRouting::OnDownTimer(wxTimerEvent&) {
  int flags = wxLIST_HITTEST_NOWHERE | wxLIST_HITTEST_ONITEM;
  if (m_panel->m_lWeatherRoutes->HitTest(m_downPos, flags) != wxNOT_FOUND)
    m_panel->m_lWeatherRoutes->PopupMenu(m_mContextMenu, m_downPos);
}

void WeatherRouting::OnRightUp(wxMouseEvent& event) {
  m_panel->m_lWeatherRoutes->PopupMenu(m_mContextMenu, event.GetPosition());
}

void WeatherRouting::CopyDataFiles(wxString from, wxString to) {
  if (from[from.Len() - 1] != '\\' && from[from.Len() - 1] != '/')
    from += wxFILE_SEP_PATH;
  if (to[to.Len() - 1] != '\\' && to[to.Len() - 1] != '/')
    to += wxFILE_SEP_PATH;

  if (!wxDirExists(to))
    wxFileName::Mkdir(to, wxS_DIR_DEFAULT, wxPATH_MKDIR_FULL);

  wxDir dir(from);
  wxString next = wxEmptyString;
  bool b = dir.GetFirst(&next);
  while (b) {
    const wxString fileFrom = from + next;
    const wxString fileTo = to + next;
    if (wxDirExists(fileFrom))
      CopyDataFiles(fileFrom, fileTo);
    else {
      wxLogMessage(
          _T("WeatherRouting copy file: " + fileFrom + _T(" to ") + fileTo));
      wxCopyFile(fileFrom, fileTo);
    }
    b = dir.GetNext(&next);
  }
}

void WeatherRouting::Render(piDC& dc, PlugIn_ViewPort& vp) {
  static int previousLocationFormat = -1;
  static wxString previousDistanceUnit;
  static wxString previousSpeedUnit;

  if (vp.bValid == false) return;

  const int locationFormat = GetLatLonFormat();
  const wxString distanceUnit = getUsrDistanceUnit_Plugin();
  const wxString speedUnit = getUsrSpeedUnit_Plugin();
  const bool locationFormatChanged = locationFormat != previousLocationFormat;
  const bool unitsChanged =
      distanceUnit != previousDistanceUnit || speedUnit != previousSpeedUnit;
  previousLocationFormat = locationFormat;
  previousDistanceUnit = distanceUnit;
  previousSpeedUnit = speedUnit;

  if (unitsChanged) UpdateColumns();

  // polling is bad
  bool work = false;
  for (auto& it : RouteMap::Positions) {
    if (it.GUID.IsEmpty()) continue;

    PlugIn_Waypoint waypoint;
    double lat = it.lat;
    double lon = it.lon;

    if (!GetSingleWaypoint(it.GUID, &waypoint)) continue;
    const bool waypointChanged = lat != waypoint.m_lat ||
                                 lon != waypoint.m_lon ||
                                 !waypoint.m_MarkName.IsSameAs(it.Name);
    if (!locationFormatChanged && !waypointChanged) continue;

    long index = m_panel->m_lPositions->FindItem(0, it.ID);
    if (index < 0) {
      // corrupted data
      continue;
    }

    wxString name = waypoint.m_MarkName;
    lat = waypoint.m_lat;
    lon = waypoint.m_lon;
    it.Name = name;
    it.lat = lat;
    it.lon = lon;

    // XXX FIXME there's already this name, update m_ConfigurationDialog source
    m_panel->m_lPositions->SetItem(index, POSITION_NAME, name);
    m_panel->m_lPositions->SetColumnWidth(POSITION_NAME, wxLIST_AUTOSIZE);
    m_panel->m_lPositions->SetItem(
        index, POSITION_LAT, toSDMM_PlugIn(NEflag::LAT, lat, Precision::HI));
    m_panel->m_lPositions->SetColumnWidth(POSITION_LAT, wxLIST_AUTOSIZE);
    m_panel->m_lPositions->SetItem(
        index, POSITION_LON, toSDMM_PlugIn(NEflag::LON, lon, Precision::HI));
    m_panel->m_lPositions->SetColumnWidth(POSITION_LON, wxLIST_AUTOSIZE);
    work = work || waypointChanged;
  }
  if (work) {
    UpdateConfigurations();
    Reset();
  }

  if (locationFormatChanged || unitsChanged) GetParent()->Refresh();

  if (!dc.GetDC()) {
#ifndef __OCPN__ANDROID__
    glPushAttrib(GL_LINE_BIT | GL_ENABLE_BIT | GL_HINT_BIT);  // Save state
    glEnable(GL_LINE_SMOOTH);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glHint(GL_LINE_SMOOTH_HINT, GL_NICEST);
#endif
    glEnable(GL_BLEND);
  }

  wxDateTime time = m_ConfigurationDialog.m_GribTimelineTime;
  if (!time.IsValid()) time = wxDateTime::UNow();

  RenderStabilityCorridor(dc, vp);

  // A hidden table does not need timeline repaint work. In particular, do not
  // let a retained AUI pane dereference route data while routes are replaced.
  if (m_RoutingTablePanel) {
    wxAuiManager* pauimgr = ::GetFrameAuiManager();
    if (pauimgr) {
      wxAuiPaneInfo& pane = pauimgr->GetPane(m_RoutingTablePanel);
      if (pane.IsOk() && pane.IsShown())
        m_RoutingTablePanel->UpdateTimeHighlight(time);
    }
  }

  for (int i = 0; i < m_panel->m_lWeatherRoutes->GetItemCount(); i++) {
    WeatherRoute* weatherroute = reinterpret_cast<WeatherRoute*>(
        wxUIntToPtr(m_panel->m_lWeatherRoutes->GetItemData(i)));
    if (weatherroute->routemapoverlay->m_bEndRouteVisible) {
      weatherroute->routemapoverlay->Render(time, m_SettingsDialog, dc, vp,
                                            true);
    }
  }

  std::list<RouteMapOverlay*> currentroutemaps = CurrentRouteMaps();
  for (std::list<RouteMapOverlay*>::iterator it = currentroutemaps.begin();
       it != currentroutemaps.end(); it++) {
    (*it)->Render(time, m_SettingsDialog, dc, vp, false, m_positionOnRoute);

    if (it == currentroutemaps.begin() &&
        m_SettingsDialog.m_cbDisplayWindBarbs->GetValue())
      (*it)->RenderWindBarbs(dc, vp);
    if (it == currentroutemaps.begin() &&
        m_SettingsDialog.m_cbDisplayCurrent->GetValue())
      (*it)->RenderCurrent(dc, vp);
  }

  m_ConfigurationBatchDialog.Render(dc, vp);

#ifndef __OCPN__ANDROID__
  if (!dc.GetDC()) glPopAttrib();
#endif
}

void WeatherRouting::UpdateDisplaySettings() {
  for (int i = 0; i < m_panel->m_lWeatherRoutes->GetItemCount(); i++) {
    WeatherRoute* weatherroute = reinterpret_cast<WeatherRoute*>(
        wxUIntToPtr(m_panel->m_lWeatherRoutes->GetItemData(i)));
    weatherroute->routemapoverlay->m_UpdateOverlay = true;
  }

  GetParent()->Refresh();
}

void WeatherRouting::AddPosition(double lat, double lon) {
  wxTextEntryDialog pd(this, _("Enter Name"), _("New Position"));
  if (pd.ShowModal() == wxID_OK) AddPosition(lat, lon, pd.GetValue());
}

void WeatherRouting::AddPosition(double lat, double lon, wxString name) {
  for (auto& it : RouteMap::Positions) {
    if (it.GUID.IsEmpty() && it.Name == name) {
      wxMessageDialog mdlg(this, _("This name already exists, replace?\n"),
                           _("Weather Routing"), wxYES | wxNO | wxICON_WARNING);
      if (mdlg.ShowModal() == wxID_YES) {
        long index = m_panel->m_lPositions->FindItem(0, it.ID);
        assert(index >= 0);

        it.lat = lat;
        it.lon = lon;
        m_panel->m_lPositions->SetItem(
            index, POSITION_LAT,
            toSDMM_PlugIn(NEflag::LAT, lat, Precision::HI));
        m_panel->m_lPositions->SetColumnWidth(POSITION_LAT, wxLIST_AUTOSIZE);
        m_panel->m_lPositions->SetItem(
            index, POSITION_LON,
            toSDMM_PlugIn(NEflag::LON, lon, Precision::HI));
        m_panel->m_lPositions->SetColumnWidth(POSITION_LON, wxLIST_AUTOSIZE);
        UpdateConfigurations();
      }
      return;
    }
  }

  RouteMapPosition p(name, lat, lon);
  RouteMap::Positions.push_back(p);
  UpdateConfigurations();

  wxListItem item;
  long index = m_panel->m_lPositions->InsertItem(
      m_panel->m_lPositions->GetItemCount(), item);

  m_panel->m_lPositions->SetItem(index, POSITION_NAME, name);
  m_panel->m_lPositions->SetColumnWidth(POSITION_NAME, wxLIST_AUTOSIZE);
  m_panel->m_lPositions->SetItem(
      index, POSITION_LAT, toSDMM_PlugIn(NEflag::LAT, lat, Precision::HI));
  m_panel->m_lPositions->SetColumnWidth(POSITION_LAT, wxLIST_AUTOSIZE);
  m_panel->m_lPositions->SetItem(
      index, POSITION_LON, toSDMM_PlugIn(NEflag::LON, lon, Precision::HI));
  m_panel->m_lPositions->SetColumnWidth(POSITION_LON, wxLIST_AUTOSIZE);

  m_panel->m_lPositions->SetItemData(index, p.ID);
  m_ConfigurationDialog.AddSource(name);
  m_ConfigurationBatchDialog.AddSource(name);
  m_tAutoSaveXML.Start(5000, true);  // Schedule auto-save in 5 seconds
}

void WeatherRouting::AddPosition(double lat, double lon, wxString name,
                                 wxString GUID) {
  if (GUID.IsEmpty()) return AddPosition(lat, lon, name);

  for (auto& it : RouteMap::Positions) {
    if (it.GUID.IsEmpty()) continue;

    if (it.GUID.IsSameAs(GUID)) {
      // wxMessageDialog mdlg(this, _("This name already exists,
      // replace?\n"),_("Weather Routing"), wxYES | wxNO | wxICON_WARNING);
      long index = m_panel->m_lPositions->FindItem(0, it.ID);

      it.Name = name;
      it.lat = lat;
      it.lon = lon;
      if (index >= 0) {
        m_panel->m_lPositions->SetItem(index, POSITION_NAME, name);
        m_panel->m_lPositions->SetColumnWidth(POSITION_NAME, wxLIST_AUTOSIZE);
        m_panel->m_lPositions->SetItem(
            index, POSITION_LAT,
            toSDMM_PlugIn(NEflag::LAT, lat, Precision::HI));
        m_panel->m_lPositions->SetColumnWidth(POSITION_LAT, wxLIST_AUTOSIZE);
        m_panel->m_lPositions->SetItem(
            index, POSITION_LON,
            toSDMM_PlugIn(NEflag::LON, lon, Precision::HI));
        m_panel->m_lPositions->SetColumnWidth(POSITION_LON, wxLIST_AUTOSIZE);
      }
      UpdateConfigurations();
      m_tAutoSaveXML.Start(5000, true);  // Schedule auto-save in 5 seconds
      return;
    }
  }

  RouteMapPosition p(name, lat, lon, GUID);
  RouteMap::Positions.push_back(p);
  UpdateConfigurations();

  wxListItem item;
  long index = m_panel->m_lPositions->InsertItem(
      m_panel->m_lPositions->GetItemCount(), item);
  m_panel->m_lPositions->SetItem(index, POSITION_NAME, name);
  m_panel->m_lPositions->SetColumnWidth(POSITION_NAME, wxLIST_AUTOSIZE);

  m_panel->m_lPositions->SetItem(
      index, POSITION_LAT, toSDMM_PlugIn(NEflag::LAT, lat, Precision::HI));
  m_panel->m_lPositions->SetColumnWidth(POSITION_LAT, wxLIST_AUTOSIZE);
  m_panel->m_lPositions->SetItem(
      index, POSITION_LON, toSDMM_PlugIn(NEflag::LON, lon, Precision::HI));
  m_panel->m_lPositions->SetColumnWidth(POSITION_LON, wxLIST_AUTOSIZE);
  m_panel->m_lPositions->SetItemData(index, p.ID);

  m_ConfigurationDialog.AddSource(name);
  m_ConfigurationBatchDialog.AddSource(name);
}

void WeatherRouting::AddRoute(wxString& GUID) {
  RouteMapConfiguration configuration;
  if (FirstCurrentRouteMap())
    configuration = FirstCurrentRouteMap()->GetConfiguration();
  else
    configuration = DefaultConfiguration();

  configuration.RouteGUID = GUID;
  configuration.StartTime = wxDateTime::Now();
  configuration.DeltaTime = 3600;

  if (!AddConfiguration(configuration)) return;
#if 0
    for(int i=0; i<m_panel->m_lWeatherRoutes->GetItemCount(); i++) {
        WeatherRoute *weatherroute =
            reinterpret_cast<WeatherRoute*>(wxUIntToPtr(m_panel->m_lWeatherRoutes->GetItemData(i)));
        if(weatherroute->routemapoverlay->m_bEndRouteVisible)
            weatherroute->routemapoverlay->Render(time, m_SettingsDialog, dc, vp, true);
    }
#endif
  if (!IsShown()) Show(true);

  m_tAutoSaveXML.Start(5000, true);  // Schedule auto-save in 5 seconds
}

bool WeatherRouting::CreateMultiLegConfigurationsFromRoute(
    const wxString& routeGuid) {
  std::vector<RouteWaypointInfo> waypoints;
  wxString error;
  if (!ExtractOpenCPNRouteWaypoints(routeGuid, waypoints, error)) {
    wxLogMessage(
        "WeatherRouting multi-leg leg creation failed: route=%s "
        "error=%s",
        routeGuid, error);
    wxMessageBox(error, _("Weather Routing"), wxOK | wxICON_WARNING, this);
    return false;
  }

  RouteMapConfiguration base;
  if (FirstCurrentRouteMap())
    base = FirstCurrentRouteMap()->GetConfiguration();
  else
    base = DefaultConfiguration();

  base.RouteGUID.Clear();
  base.DepartureTimeOptimizationEnabled = false;
  base.DepartureTimeOptimizationCandidate = false;
  base.DepartureTimeOptimizationGroupId.Clear();
  base.DepartureTimeOptimizationOffsetMinutes = 0;
  base.IsMultiLegGenerated = false;
  base.MultiLegGroupId.Clear();
  base.MultiLegParentRouteGUID.Clear();
  base.MultiLegParentRouteName.Clear();
  base.MultiLegLegIndex = 0;
  base.MultiLegLegCount = 0;

  wxString routeName = GetRouteNameForGuid(routeGuid);
  wxString groupId = wxString::Format(_T("multileg-%s-%s"), routeGuid,
                                      wxDateTime::UNow().FormatISOCombined());
  size_t legCount = waypoints.size() - 1;
  int added = 0;

  wxLogMessage(
      "WeatherRouting multi-leg leg creation: route=%s name=%s legs=%lu",
      routeGuid, routeName, static_cast<unsigned long>(legCount));

  for (size_t i = 0; i < legCount; ++i) {
    const RouteWaypointInfo& from = waypoints[i];
    const RouteWaypointInfo& to = waypoints[i + 1];

    RouteMapConfiguration leg = base;
    leg.RouteGUID.Clear();
    leg.StartType = RouteMapConfiguration::START_FROM_WAYPOINT;
    leg.EndType = RouteMapConfiguration::END_AT_WAYPOINT;
    leg.Start = from.name;
    leg.StartGUID = from.guid;
    leg.StartLat = from.lat;
    leg.StartLon = from.lon;
    leg.End = to.name;
    leg.EndGUID = to.guid;
    leg.EndLat = to.lat;
    leg.EndLon = to.lon;
    leg.DepartureTimeOptimizationEnabled = false;
    leg.DepartureTimeOptimizationCandidate = false;
    leg.DepartureTimeOptimizationGroupId.Clear();
    leg.DepartureTimeOptimizationOffsetMinutes = 0;
    leg.IsMultiLegGenerated = true;
    leg.MultiLegGroupId = groupId;
    leg.MultiLegParentRouteGUID = routeGuid;
    leg.MultiLegParentRouteName = routeName;
    leg.MultiLegLegIndex = i + 1;
    leg.MultiLegLegCount = legCount;

    wxString legName = wxString::Format(
        "Multi-leg %s Leg %lu/%lu: %s to %s", routeName,
        static_cast<unsigned long>(i + 1), static_cast<unsigned long>(legCount),
        from.name, to.name);
    wxLogMessage(
        "WeatherRouting multi-leg leg created: route=%s leg=%lu/%lu name=%s "
        "start=%s start_guid=%s end=%s end_guid=%s",
        routeGuid, static_cast<unsigned long>(i + 1),
        static_cast<unsigned long>(legCount), legName, leg.Start, leg.StartGUID,
        leg.End, leg.EndGUID);

    if (AddConfiguration(leg)) ++added;
  }

  if (!added) {
    wxString message = _("No Weather Routing leg configurations were created.");
    wxLogMessage(
        "WeatherRouting multi-leg leg creation failed: route=%s "
        "error=%s",
        routeGuid, message);
    wxMessageBox(message, _("Weather Routing"), wxOK | wxICON_WARNING, this);
    return false;
  }

  if (!IsShown()) Show(true);
  BeginMultiLegGroupSettingsEdit(groupId);
  m_tAutoSaveXML.Start(5000, true);
  return true;
}

std::vector<RouteMapOverlay*> WeatherRouting::GetMultiLegGroupRoutes(
    const wxString& groupId) {
  std::vector<RouteMapOverlay*> routes;
  if (groupId.IsEmpty()) return routes;

  for (auto weatherroute : m_WeatherRoutes) {
    RouteMapOverlay* routemap = weatherroute->routemapoverlay;
    RouteMapConfiguration configuration = routemap->GetConfiguration();
    if (configuration.IsMultiLegGenerated &&
        configuration.MultiLegGroupId == groupId)
      routes.push_back(routemap);
  }

  std::sort(routes.begin(), routes.end(),
            [](RouteMapOverlay* a, RouteMapOverlay* b) {
              RouteMapConfiguration ca = a->GetConfiguration();
              RouteMapConfiguration cb = b->GetConfiguration();
              return ca.MultiLegLegIndex < cb.MultiLegLegIndex;
            });
  return routes;
}

void WeatherRouting::SelectMultiLegGroup(const wxString& groupId) {
  if (!m_panel || groupId.IsEmpty()) return;

  m_bSkipUpdateCurrentItems = true;
  for (long i = 0; i < m_panel->m_lWeatherRoutes->GetItemCount(); ++i) {
    m_panel->m_lWeatherRoutes->SetItemState(i, 0, wxLIST_STATE_SELECTED);

    WeatherRoute* weatherroute = reinterpret_cast<WeatherRoute*>(
        wxUIntToPtr(m_panel->m_lWeatherRoutes->GetItemData(i)));
    if (!weatherroute || !weatherroute->routemapoverlay) continue;

    RouteMapConfiguration configuration =
        weatherroute->routemapoverlay->GetConfiguration();
    if (configuration.IsMultiLegGenerated &&
        configuration.MultiLegGroupId == groupId)
      m_panel->m_lWeatherRoutes->SetItemState(i, wxLIST_STATE_SELECTED,
                                              wxLIST_STATE_SELECTED);
  }
  m_bSkipUpdateCurrentItems = false;

  OnWeatherRouteSelected();
}

void WeatherRouting::PreserveMultiLegLegFields(
    RouteMapOverlay* routemapoverlay,
    RouteMapConfiguration& configuration) const {
  auto it = m_MultiLegLegSnapshots.find(routemapoverlay);
  if (it == m_MultiLegLegSnapshots.end()) return;

  const RouteMapConfiguration& original = it->second;
  configuration.RouteGUID.Clear();
  configuration.StartType = original.StartType;
  configuration.Start = original.Start;
  configuration.StartGUID = original.StartGUID;
  configuration.StartLat = original.StartLat;
  configuration.StartLon = original.StartLon;
  configuration.EndType = original.EndType;
  configuration.End = original.End;
  configuration.EndGUID = original.EndGUID;
  configuration.EndLat = original.EndLat;
  configuration.EndLon = original.EndLon;
  configuration.IsMultiLegGenerated = true;
  configuration.MultiLegGroupId = original.MultiLegGroupId;
  configuration.MultiLegParentRouteGUID = original.MultiLegParentRouteGUID;
  configuration.MultiLegParentRouteName = original.MultiLegParentRouteName;
  configuration.MultiLegLegIndex = original.MultiLegLegIndex;
  configuration.MultiLegLegCount = original.MultiLegLegCount;
  configuration.DepartureTimeOptimizationEnabled = false;
  configuration.DepartureTimeOptimizationCandidate = false;
  configuration.DepartureTimeOptimizationGroupId.Clear();
  configuration.DepartureTimeOptimizationOffsetMinutes = 0;
}

void WeatherRouting::BeginMultiLegGroupSettingsEdit(const wxString& groupId) {
  std::vector<RouteMapOverlay*> routes = GetMultiLegGroupRoutes(groupId);
  if (routes.empty()) return;

  m_ApplyingMultiLegGroupSettings = true;
  m_MultiLegSettingsGroupId = groupId;
  m_MultiLegLegSnapshots.clear();

  for (auto route : routes)
    m_MultiLegLegSnapshots[route] = route->GetConfiguration();

  SelectMultiLegGroup(groupId);
  if (!IsShown()) Show(true);

  wxLogMessage(
      "WeatherRouting multi-leg group settings edit started: group=%s "
      "legs=%lu",
      groupId, static_cast<unsigned long>(routes.size()));

  wxMessageBox(
      _("Edit shared settings for this multi-leg passage. Start and end "
        "waypoints are preserved separately for each leg. The generated legs "
        "will be reset when settings are changed."),
      _("Weather Routing"), wxOK | wxICON_INFORMATION, this);
  m_ConfigurationDialog.Show();
  m_ConfigurationDialog.Raise();
}

bool WeatherRouting::EditMultiLegGroupSettings(RouteMapOverlay* selectedRoute) {
  if (!selectedRoute) return false;

  RouteMapConfiguration selected = selectedRoute->GetConfiguration();
  if (!selected.IsMultiLegGenerated || selected.MultiLegGroupId.IsEmpty()) {
    wxMessageBox(_("Select a generated multi-leg route row first."),
                 _("Weather Routing"), wxOK | wxICON_WARNING, this);
    return false;
  }

  if (m_ActiveMultiLegSequence &&
      selected.MultiLegGroupId == m_ActiveMultiLegGroupId) {
    wxMessageBox(
        _("Stop the active multi-leg sequence before editing group settings."),
        _("Weather Routing"), wxOK | wxICON_WARNING, this);
    return false;
  }

  std::vector<RouteMapOverlay*> routes =
      GetMultiLegGroupRoutes(selected.MultiLegGroupId);
  for (auto route : routes) {
    if (RouteMapIsWaitingOrRunning(route)) {
      wxMessageBox(_("Stop the selected multi-leg routes before editing group "
                     "settings."),
                   _("Weather Routing"), wxOK | wxICON_WARNING, this);
      return false;
    }
  }

  BeginMultiLegGroupSettingsEdit(selected.MultiLegGroupId);
  return true;
}

void WeatherRouting::ShowRoutingStatus(RouteMapOverlay* selectedRoute) {
  if (!selectedRoute) {
    wxMessageBox(_("Select a weather route row first."), _("Weather Routing"),
                 wxOK | wxICON_WARNING, this);
    return;
  }

  auto appendRouteStatus = [&](wxString& text, RouteMapOverlay* route) {
    if (!route) return;
    RouteMapConfiguration configuration = route->GetConfiguration();

    WeatherRoute display;
    display.routemapoverlay = route;
    display.Update(this);

    wxString state = display.State;
    if (route->Finished() && !route->ReachedDestination()) {
      wxString reason = SafeMultiLegFailureReason(route);
      if (!reason.IsEmpty() && state.Find(reason) == wxNOT_FOUND) {
        if (!state.IsEmpty()) state += _T(": ");
        state += reason;
      }
    }

    if (configuration.IsMultiLegGenerated) {
      text += wxString::Format(_("Leg %d/%d: %s to %s\n"),
                               configuration.MultiLegLegIndex,
                               configuration.MultiLegLegCount,
                               configuration.Start, configuration.End);
    } else {
      text += wxString::Format(_("%s to %s\n"), configuration.Start,
                               configuration.End);
    }
    text += wxString::Format(_("State: %s\n"), state);
    text += wxString::Format(_("Start: %s\n"), display.StartTime);
    text += wxString::Format(_("End: %s\n"), display.EndTime);
    if (configuration.TimeMode ==
        RouteMapConfiguration::ROUTE_BY_ARRIVAL_TIME) {
      wxDateTime plannedArrival = configuration.PlannedArrivalTime;
      const wxString plannedArrivalText =
          plannedArrival.IsValid()
              ? m_SettingsDialog.FormatTime(plannedArrival, _T("%x %H:%M"))
              : _("N/A");
      text += wxString::Format(_("Planned arrival: %s\n"),
                               plannedArrivalText);
      if (route->Finished()) {
        const long marginMinutes =
            configuration.ArrivalPlanningScheduleMarginSeconds / 60;
        text += wxString::Format(
            _("Arrival safety margin: %ld min; schedule margin: %ld min\n"),
            static_cast<long>(configuration.ArrivalSafetyMarginMinutes),
            marginMinutes);
        text += wxString::Format(
            _("Arrival-planning routes evaluated: %d; feasible: %d\n"),
            configuration.ArrivalPlanningEvaluatedRoutes,
            configuration.ArrivalPlanningFeasibleRoutes);
      }
    }
    text += wxString::Format(_("Time: %s\n"), display.Time);
    text += wxString::Format(_("Distance: %s\n"), display.Distance);
    text += wxString::Format(_("Weather: %s\n"),
                             display.WeatherSourceDetail);
  };

  RouteMapConfiguration selected = selectedRoute->GetConfiguration();
  wxString message;
  if (selected.IsMultiLegGenerated && !selected.MultiLegGroupId.IsEmpty()) {
    std::vector<RouteMapOverlay*> routes =
        GetMultiLegGroupRoutes(selected.MultiLegGroupId);
    message = wxString::Format(_("Multi-leg routing status: %s\n\n"),
                               selected.MultiLegParentRouteName);
    for (size_t i = 0; i < routes.size(); ++i) {
      appendRouteStatus(message, routes[i]);
      if (i + 1 < routes.size()) message += _T("\n");
    }
  } else {
    message = _("Routing status\n\n");
    appendRouteStatus(message, selectedRoute);
  }

  wxMessageBox(message, _("Weather Routing Status"), wxOK | wxICON_INFORMATION,
               this);
}

bool WeatherRouting::RouteMapIsWaitingOrRunning(
    RouteMapOverlay* routemapoverlay) const {
  if (!routemapoverlay) return false;
  if (routemapoverlay->Running()) return true;
  if (std::find(m_RunningRouteMaps.begin(), m_RunningRouteMaps.end(),
                routemapoverlay) != m_RunningRouteMaps.end())
    return true;
  if (std::find(m_WaitingRouteMaps.begin(), m_WaitingRouteMaps.end(),
                routemapoverlay) != m_WaitingRouteMaps.end())
    return true;
  return false;
}

bool WeatherRouting::RouteMapIsManaged(RouteMapOverlay* routemapoverlay) const {
  if (!routemapoverlay) return false;
  for (auto weatherroute : m_WeatherRoutes)
    if (weatherroute && weatherroute->routemapoverlay == routemapoverlay)
      return true;
  return false;
}

void WeatherRouting::ShowRoutingProgress(const wxString& title) {
  static const int kProgressTextWidth = 540;
  if (!m_RoutingProgressDialog) {
    m_RoutingProgressDialog =
        new wxDialog(this, wxID_ANY, title, wxDefaultPosition, wxDefaultSize,
                     wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER);
    wxBoxSizer* topSizer = new wxBoxSizer(wxVERTICAL);
    m_RoutingProgressStage =
        new wxStaticText(m_RoutingProgressDialog, wxID_ANY, wxEmptyString);
    m_RoutingProgressStage->SetFont(m_RoutingProgressStage->GetFont().Bold());
    topSizer->Add(m_RoutingProgressStage, 0, wxALL | wxEXPAND, 8);
    m_RoutingProgressDetail =
        new wxStaticText(m_RoutingProgressDialog, wxID_ANY, wxEmptyString,
                         wxDefaultPosition, wxSize(kProgressTextWidth, -1));
    m_RoutingProgressDetail->Wrap(kProgressTextWidth);
    topSizer->Add(m_RoutingProgressDetail, 0,
                  wxLEFT | wxRIGHT | wxBOTTOM | wxEXPAND, 8);
    m_RoutingProgressTiming =
        new wxStaticText(m_RoutingProgressDialog, wxID_ANY, wxEmptyString,
                         wxDefaultPosition, wxSize(kProgressTextWidth, -1));
    m_RoutingProgressTiming->Wrap(kProgressTextWidth);
    topSizer->Add(m_RoutingProgressTiming, 0,
                  wxLEFT | wxRIGHT | wxBOTTOM | wxEXPAND, 8);
    m_RoutingProgressGauge =
        new wxGauge(m_RoutingProgressDialog, wxID_ANY, 100);
    topSizer->Add(m_RoutingProgressGauge, 0,
                  wxLEFT | wxRIGHT | wxBOTTOM | wxEXPAND, 8);
    wxStaticText* note = new wxStaticText(
        m_RoutingProgressDialog, wxID_ANY,
        _("Use the Weather Routing Stop button to cancel active route "
          "computations."));
    note->Wrap(kProgressTextWidth);
    topSizer->Add(note, 0, wxLEFT | wxRIGHT | wxBOTTOM | wxEXPAND, 8);
    m_RoutingProgressDialog->SetSizerAndFit(topSizer);
    m_RoutingProgressDialog->Bind(wxEVT_CLOSE_WINDOW,
                                  [this](wxCloseEvent& event) {
                                    if (m_DeferredRoutingStartPending)
                                      CancelDeferredRoutingStart();
                                    if (m_tRoutingProgress.IsRunning())
                                      m_tRoutingProgress.Stop();
                                    if (m_RoutingProgressDialog)
                                      m_RoutingProgressDialog->Hide();
                                  });
  } else {
    m_RoutingProgressDialog->SetTitle(title);
  }

  m_RoutingProgressStartTime = wxDateTime::Now();
  m_RoutingProgressStageStartTime = m_RoutingProgressStartTime;
  m_RoutingProgressCurrentStage.Clear();
  m_RoutingProgressPreviousStage.Clear();
  m_RoutingProgressPreviousStageDuration = wxTimeSpan(0);
  m_RoutingProgressFinished = false;
  m_RoutingProgressDialog->Show();
  m_RoutingProgressDialog->Raise();
  RefreshRoutingProgressTiming();
  PaintRoutingProgressNow();
  m_tRoutingProgress.Start(1000);
}

void WeatherRouting::UpdateRoutingProgress(const wxString& stage,
                                           const wxString& detail, int value,
                                           int range) {
  if (!m_RoutingProgressDialog) return;
  wxDateTime now = wxDateTime::Now();
  if (!stage.IsEmpty() && stage != m_RoutingProgressCurrentStage) {
    if (!m_RoutingProgressCurrentStage.IsEmpty() &&
        m_RoutingProgressStageStartTime.IsValid()) {
      m_RoutingProgressPreviousStage = m_RoutingProgressCurrentStage;
      m_RoutingProgressPreviousStageDuration =
          now - m_RoutingProgressStageStartTime;
    }
    m_RoutingProgressCurrentStage = stage;
    m_RoutingProgressStageStartTime = now;
  }
  if (range > 0) m_RoutingProgressGauge->SetRange(range);
  if (value >= 0)
    m_RoutingProgressGauge->SetValue(
        wxMin(value, m_RoutingProgressGauge->GetRange()));
  if (!stage.IsEmpty()) m_RoutingProgressStage->SetLabel(stage);

  m_RoutingProgressDetail->SetLabel(detail);
  m_RoutingProgressDetail->Wrap(540);
  RefreshRoutingProgressTiming();
  wxLogMessage("WR_PROGRESS stage=\"%s\" detail=\"%s\" value=%d range=%d",
               stage, detail, value, range);
  PaintRoutingProgressNow();
}

void WeatherRouting::FinishRoutingProgress(const wxString& stage,
                                           const wxString& detail) {
  UpdateRoutingProgress(stage, detail);
  m_RoutingProgressFinished = true;
  RefreshRoutingProgressTiming();
  if (m_tRoutingProgress.IsRunning()) m_tRoutingProgress.Stop();
}

void WeatherRouting::CloseRoutingProgress() {
  if (m_tRoutingProgress.IsRunning()) m_tRoutingProgress.Stop();
  if (m_RoutingProgressDialog) m_RoutingProgressDialog->Hide();
}

void WeatherRouting::OnRoutingProgressTimer(wxTimerEvent&) {
  RefreshRoutingProgressTiming();
}

void WeatherRouting::RefreshRoutingProgressTiming() {
  if (!m_RoutingProgressDialog || !m_RoutingProgressTiming) return;
  wxDateTime now = wxDateTime::Now();
  wxString timing;
  if (m_RoutingProgressStageStartTime.IsValid()) {
    wxTimeSpan stageElapsed = now - m_RoutingProgressStageStartTime;
    timing += wxString::Format(_("Stage elapsed: %s"),
                               stageElapsed.Format(_T("%H:%M:%S")));
  }
  if (m_RoutingProgressStartTime.IsValid()) {
    wxTimeSpan totalElapsed = now - m_RoutingProgressStartTime;
    if (!timing.IsEmpty()) timing += _T("\n");
    timing +=
        wxString::Format(m_RoutingProgressFinished ? _("Total time: %s")
                                                   : _("Total elapsed: %s"),
                         totalElapsed.Format(_T("%H:%M:%S")));
  }
  if (!m_RoutingProgressPreviousStage.IsEmpty()) {
    if (!timing.IsEmpty()) timing += _T("\n");
    timing += wxString::Format(
        _("Previous stage: %s, %s"), m_RoutingProgressPreviousStage,
        m_RoutingProgressPreviousStageDuration.Format(_T("%H:%M:%S")));
  }
  m_RoutingProgressTiming->SetLabel(timing);
  m_RoutingProgressTiming->Wrap(540);
  m_RoutingProgressDialog->Layout();
}

void WeatherRouting::PaintRoutingProgressNow() {
  if (!m_RoutingProgressDialog || !m_RoutingProgressDialog->IsShown()) return;

  static bool painting = false;
  if (painting) return;
  painting = true;

  m_RoutingProgressDialog->Layout();
  m_RoutingProgressDialog->Fit();
  if (m_RoutingProgressStage) {
    m_RoutingProgressStage->Refresh();
    m_RoutingProgressStage->Update();
  }
  if (m_RoutingProgressDetail) {
    m_RoutingProgressDetail->Refresh();
    m_RoutingProgressDetail->Update();
  }
  if (m_RoutingProgressTiming) {
    m_RoutingProgressTiming->Refresh();
    m_RoutingProgressTiming->Update();
  }
  if (m_RoutingProgressGauge) {
    m_RoutingProgressGauge->Refresh();
    m_RoutingProgressGauge->Update();
  }
  m_RoutingProgressDialog->Refresh();
  m_RoutingProgressDialog->Update();

  painting = false;
}

bool WeatherRouting::ShouldShowChartSafetyComputeProgress(
    const std::list<RouteMapOverlay*>& routemapoverlays) const {
  bool use_experimental_chart_safety = false;
  bool enforce_experimental_chart_safety = false;
  ReadExperimentalChartSafetySettings(use_experimental_chart_safety,
                                      enforce_experimental_chart_safety);
  if (!use_experimental_chart_safety || !enforce_experimental_chart_safety)
    return false;

  for (auto routemapoverlay : routemapoverlays) {
    if (!routemapoverlay) continue;
    RouteMapConfiguration configuration = routemapoverlay->GetConfiguration();
    if (configuration.DetectLand) return true;
  }
  return false;
}

void WeatherRouting::BeginChartSafetyComputeProgress(
    bool computeAll, const std::list<RouteMapOverlay*>& routemapoverlays) {
  m_ChartSafetyComputeProgressActive = true;
  m_ChartSafetyComputeProgressAll = computeAll;
  m_ChartSafetyComputeProgressTotalRoutes =
      wxMax(1, static_cast<int>(routemapoverlays.size()));
  m_ChartSafetyComputeProgressStartedRoutes = 0;
  m_ChartSafetyComputeProgressCompletedRoutes = 0;

  ShowRoutingProgress(_("Weather Routing Progress"));
  wxString stage = computeAll ? _("Preparing routes") : _("Preparing route");
  wxString detail =
      computeAll
          ? wxString::Format(_("Preparing %d weather routes with chart-backed "
                               "safety checks."),
                             m_ChartSafetyComputeProgressTotalRoutes)
          : _("Preparing weather route with chart-backed safety checks.");
  UpdateRoutingProgress(stage, detail, 0,
                        m_ChartSafetyComputeProgressTotalRoutes);
  wxLogMessage("WR_PROGRESS mode=%s stage=\"%s\" routes=%d chart_enforcement=1",
               computeAll ? "compute-all" : "single", stage,
               m_ChartSafetyComputeProgressTotalRoutes);
}

void WeatherRouting::UpdateChartSafetyComputeProgress(
    const wxString& stage, RouteMapOverlay* routemapoverlay, int value,
    int range) {
  if (!m_ChartSafetyComputeProgressActive || !m_RoutingProgressDialog ||
      !m_RoutingProgressDialog->IsShown())
    return;

  wxString routeName;
  if (routemapoverlay) {
    RouteMapConfiguration configuration = routemapoverlay->GetConfiguration();
    routeName =
        wxString::Format(_("%s to %s"), configuration.Start, configuration.End);
  } else {
    routeName = _("weather route");
  }

  int progressRange =
      range > 0 ? range : wxMax(1, m_ChartSafetyComputeProgressTotalRoutes);
  int progressValue = value >= 0 ? value : -1;
  wxString detail;
  if (m_ChartSafetyComputeProgressAll) {
    int routeNumber =
        wxMin(m_ChartSafetyComputeProgressCompletedRoutes + 1, progressRange);
    detail = wxString::Format(_("Route %d of %d: %s"), routeNumber,
                              progressRange, routeName);
  } else {
    detail = wxString::Format(_("%s"), routeName);
  }
  UpdateRoutingProgress(stage, detail, progressValue, progressRange);

  long elapsedMs = -1;
  if (m_RoutingProgressStartTime.IsValid())
    elapsedMs = (wxDateTime::Now() - m_RoutingProgressStartTime)
                    .GetMilliseconds()
                    .ToLong();
  wxLogMessage(
      "WR_PROGRESS mode=%s route=\"%s\" stage=\"%s\" elapsed_ms=%ld "
      "chart_enforcement=1",
      m_ChartSafetyComputeProgressAll ? "compute-all" : "single", routeName,
      stage, elapsedMs);
}

void WeatherRouting::FinishChartSafetyComputeProgressIfDone() {
  if (!m_ChartSafetyComputeProgressActive) return;
  if (!m_RunningRouteMaps.empty() || !m_WaitingRouteMaps.empty()) return;

  wxString stage = m_ChartSafetyComputeProgressAll ? _("All routes complete")
                                                   : _("Route complete");
  wxString detail =
      m_ChartSafetyComputeProgressAll
          ? wxString::Format(_("Completed %d weather route computations."),
                             m_ChartSafetyComputeProgressCompletedRoutes)
          : _("Weather route computation finished.");
  FinishRoutingProgress(stage, detail);
  wxLogMessage(
      "WR_PROGRESS mode=%s stage=\"%s\" completed_routes=%d "
      "chart_enforcement=1",
      m_ChartSafetyComputeProgressAll ? "compute-all" : "single", stage,
      m_ChartSafetyComputeProgressCompletedRoutes);
  m_ChartSafetyComputeProgressActive = false;
  m_ChartSafetyComputeProgressAll = false;
  m_ChartSafetyComputeProgressTotalRoutes = 0;
  m_ChartSafetyComputeProgressStartedRoutes = 0;
  m_ChartSafetyComputeProgressCompletedRoutes = 0;
}

wxString WeatherRouting::SafeMultiLegFailureReason(
    RouteMapOverlay* routemapoverlay) const {
  if (!routemapoverlay) return _("route is unavailable");
  if (!RouteMapIsManaged(routemapoverlay))
    return _("route is no longer active");
  if (routemapoverlay->Running()) return _("leg is still running");
  if (!routemapoverlay->Finished()) return _("leg was stopped");

  wxString reason;
  wxString explicitReason = routemapoverlay->GetFailureReason();
  if (!explicitReason.IsEmpty()) return explicitReason;

  RouteMapConfiguration configuration = routemapoverlay->GetConfiguration();
  if (configuration.grib_is_data_deficient) reason += _("data deficient");

  WeatherForecastStatus forecastStatus =
      routemapoverlay->GetWeatherForecastStatus();
  if (forecastStatus != WEATHER_FORECAST_SUCCESS) {
    if (!reason.IsEmpty()) reason += _T("; ");
    reason += RouteMap::GetWeatherForecastStatusMessage(forecastStatus);
  }

  wxString weatherStatus = routemapoverlay->GetWeatherForecastError();
  if (!weatherStatus.IsEmpty()) {
    if (!reason.IsEmpty()) reason += _T("; ");
    reason += weatherStatus;
  }

  wxString gribStatus = routemapoverlay->GetGribError();
  if (!gribStatus.IsEmpty()) {
    if (!reason.IsEmpty()) reason += _T("; ");
    reason += gribStatus;
  }

  PolarSpeedStatus polarStatus = routemapoverlay->GetPolarStatus();
  if (polarStatus != POLAR_SPEED_SUCCESS) {
    if (!reason.IsEmpty()) reason += _T("; ");
    reason += _("Polar: ");
    reason += Polar::GetPolarStatusMessage(polarStatus);
  }

  if (routemapoverlay->LandCrossing()) {
    if (!reason.IsEmpty()) reason += _T("; ");
    reason += _("land crossing");
  }

  if (routemapoverlay->BoundaryCrossing()) {
    if (!reason.IsEmpty()) reason += _T("; ");
    reason += _("boundary crossing");
  }

  if (routemapoverlay->Finished() && !routemapoverlay->ReachedDestination()) {
    if (!reason.IsEmpty()) reason += _T("; ");
    reason += _("leg did not reach destination");
  }

  if (routemapoverlay->Finished() && routemapoverlay->ReachedDestination() &&
      !routemapoverlay->EndTime().IsValid()) {
    if (!reason.IsEmpty()) reason += _T("; ");
    reason += _("leg completed without a valid ETA");
  }

  if (reason.IsEmpty()) reason = _("leg failed");
  return reason;
}

void WeatherRouting::CancelMultiLegSequence() {
  if (m_DeferredRoutingStartPending &&
      m_DeferredRoutingStartMode == DEFERRED_ROUTING_MULTILEG_SEQUENCE)
    CancelDeferredRoutingStart();
  if (!m_ActiveMultiLegSequence) return;
  wxLogMessage("WeatherRouting multi-leg sequence cancelled: group=%s",
               m_ActiveMultiLegGroupId);
  m_ActiveMultiLegSequence = false;
  m_ActiveMultiLegGroupId.Clear();
  m_ActiveMultiLegCurrentLegIndex = 0;
  if (!m_ActiveMultiLegDepartureOptimization) CloseRoutingProgress();
}

bool WeatherRouting::StartMultiLegSequenceLeg(
    RouteMapOverlay* routemapoverlay) {
  if (!routemapoverlay) return false;

  RouteMapConfiguration configuration = routemapoverlay->GetConfiguration();
  wxLogMessage(
      "WeatherRouting multi-leg sequence starting leg %d/%d: group=%s "
      "start=%s end=%s departure=%s",
      configuration.MultiLegLegIndex, configuration.MultiLegLegCount,
      configuration.MultiLegGroupId, configuration.Start, configuration.End,
      configuration.StartTime.FormatISOCombined());
  UpdateRoutingProgress(
      _("Computing multi-leg route"),
      wxString::Format(_("Computing leg %d of %d: %s to %s"),
                       configuration.MultiLegLegIndex,
                       configuration.MultiLegLegCount, configuration.Start,
                       configuration.End),
      configuration.MultiLegLegIndex - 1, configuration.MultiLegLegCount);

  if (RouteMapIsWaitingOrRunning(routemapoverlay)) {
    Stop(routemapoverlay);
    m_RunningRouteMaps.remove(routemapoverlay);
    m_WaitingRouteMaps.remove(routemapoverlay);
  }
  routemapoverlay->Reset();
  UpdateRouteMap(routemapoverlay);
  Start(routemapoverlay);

  if (!RouteMapIsWaitingOrRunning(routemapoverlay)) {
    UpdateRouteMap(routemapoverlay);
    wxLogMessage(
        "WeatherRouting multi-leg sequence could not start leg %d/%d: "
        "group=%s",
        configuration.MultiLegLegIndex, configuration.MultiLegLegCount,
        configuration.MultiLegGroupId);
    return false;
  }

  m_panel->m_gProgress->SetRange(m_RoutesToRun);
  return true;
}

void WeatherRouting::ScheduleDeferredRoutingStart(int mode,
                                                  const wxString& groupId) {
  if (m_DeferredRoutingStartPending) {
    wxLogMessage(
        "WeatherRouting deferred routing start ignored: mode=%d group=%s "
        "pending_mode=%d pending_group=%s",
        mode, groupId, m_DeferredRoutingStartMode,
        m_DeferredRoutingStartGroupId);
    return;
  }

  m_DeferredRoutingStartMode = mode;
  m_DeferredRoutingStartGroupId = groupId;
  m_DeferredRoutingStartPending = true;
  wxLogMessage(
      "WeatherRouting deferred routing start scheduled: mode=%d "
      "group=%s",
      mode, groupId);
  m_tDeferredRoutingStart.StartOnce(100);
}

void WeatherRouting::CancelDeferredRoutingStart() {
  if (!m_DeferredRoutingStartPending) return;
  wxLogMessage(
      "WeatherRouting deferred routing start cancelled: mode=%d "
      "group=%s",
      m_DeferredRoutingStartMode, m_DeferredRoutingStartGroupId);
  bool normalComputePending =
      m_DeferredRoutingStartMode == DEFERRED_ROUTING_COMPUTE_CURRENT ||
      m_DeferredRoutingStartMode == DEFERRED_ROUTING_COMPUTE_ALL;
  if (m_tDeferredRoutingStart.IsRunning()) m_tDeferredRoutingStart.Stop();
  m_DeferredRoutingStartPending = false;
  m_DeferredRoutingStartMode = 0;
  m_DeferredRoutingStartGroupId.Clear();
  if (normalComputePending) {
    m_ChartSafetyComputeProgressActive = false;
    m_ChartSafetyComputeProgressAll = false;
    m_ChartSafetyComputeProgressTotalRoutes = 0;
    m_ChartSafetyComputeProgressStartedRoutes = 0;
    m_ChartSafetyComputeProgressCompletedRoutes = 0;
  }
}

void WeatherRouting::OnDeferredRoutingStart(wxTimerEvent&) {
  if (!m_DeferredRoutingStartPending) return;

  int mode = m_DeferredRoutingStartMode;
  wxString groupId = m_DeferredRoutingStartGroupId;
  m_DeferredRoutingStartPending = false;
  m_DeferredRoutingStartMode = 0;
  m_DeferredRoutingStartGroupId.Clear();

  wxLogMessage(
      "WeatherRouting deferred routing start running: mode=%d "
      "group=%s",
      mode, groupId);

  if (mode == DEFERRED_ROUTING_MULTILEG_SEQUENCE)
    ComputeMultiLegSequenceNow(groupId);
  else if (mode == DEFERRED_ROUTING_MULTILEG_OPTIMIZATION)
    ComputeMultiLegDepartureOptimizationNow(groupId);
  else if (mode == DEFERRED_ROUTING_COMPUTE_CURRENT)
    StartCurrentRouteComputations();
  else if (mode == DEFERRED_ROUTING_COMPUTE_ALL)
    StartAllRouteComputations();
}

bool WeatherRouting::ComputeMultiLegSequence(RouteMapOverlay* selectedRoute) {
  if (!selectedRoute) return false;

  RouteMapConfiguration selected = selectedRoute->GetConfiguration();
  if (!selected.IsMultiLegGenerated || selected.MultiLegGroupId.IsEmpty()) {
    wxMessageBox(_("Select a generated multi-leg route row first."),
                 _("Weather Routing"), wxOK | wxICON_WARNING, this);
    return false;
  }

  if (m_DeferredRoutingStartPending) {
    wxMessageBox(_("A multi-leg routing start is already pending."),
                 _("Weather Routing"), wxOK | wxICON_WARNING, this);
    return false;
  }

  std::vector<RouteMapOverlay*> routes =
      GetMultiLegGroupRoutes(selected.MultiLegGroupId);
  if ((int)routes.size() != selected.MultiLegLegCount || routes.empty()) {
    wxString message =
        _("The selected multi-leg group is incomplete or unavailable.");
    wxLogMessage("WeatherRouting multi-leg sequence failed: group=%s error=%s",
                 selected.MultiLegGroupId, message);
    wxMessageBox(message, _("Weather Routing"), wxOK | wxICON_WARNING, this);
    return false;
  }

  RouteMapConfiguration first = routes.front()->GetConfiguration();
  CancelMultiLegSequence();
  ShowRoutingProgress(_("Weather Routing Progress"));
  UpdateRoutingProgress(
      _("Preparing routes"),
      wxString::Format(_("Preparing multi-leg route: %s (%d legs)"),
                       first.MultiLegParentRouteName, first.MultiLegLegCount),
      0, first.MultiLegLegCount);
  ScheduleDeferredRoutingStart(DEFERRED_ROUTING_MULTILEG_SEQUENCE,
                               selected.MultiLegGroupId);
  return true;
}

bool WeatherRouting::ComputeMultiLegSequenceNow(const wxString& groupId) {
  std::vector<RouteMapOverlay*> routes = GetMultiLegGroupRoutes(groupId);
  if (routes.empty()) {
    FinishRoutingProgress(_("Multi-leg route failed"),
                          _("The selected multi-leg group is unavailable."));
    return false;
  }

  RouteMapConfiguration selected = routes.front()->GetConfiguration();
  if ((int)routes.size() != selected.MultiLegLegCount) {
    FinishRoutingProgress(
        _("Multi-leg route failed"),
        _("The selected multi-leg group is incomplete or unavailable."));
    return false;
  }

  CancelMultiLegSequence();

  for (auto route : routes) {
    RouteMapConfiguration configuration = route->GetConfiguration();
    if (RouteMapIsWaitingOrRunning(route)) {
      Stop(route);
      m_RunningRouteMaps.remove(route);
      m_WaitingRouteMaps.remove(route);
    }
    route->Reset();
    route->SetConfiguration(configuration);
    UpdateRouteMap(route);
  }

  RouteMapConfiguration first = routes.front()->GetConfiguration();
  m_ActiveMultiLegGroupId = first.MultiLegGroupId;
  m_ActiveMultiLegCurrentLegIndex = 1;
  m_ActiveMultiLegSequence = true;
  UpdateRoutingProgress(
      _("Preparing routes"),
      wxString::Format(_("Preparing multi-leg route: %s (%d legs)"),
                       first.MultiLegParentRouteName, first.MultiLegLegCount),
      0, first.MultiLegLegCount);

  wxLogMessage(
      "WeatherRouting multi-leg sequence starting: group=%s route=%s legs=%d",
      first.MultiLegGroupId, first.MultiLegParentRouteName,
      first.MultiLegLegCount);

  if (!StartMultiLegSequenceLeg(routes.front())) {
    CancelMultiLegSequence();
    return false;
  }

  UpdateComputeState();
  return true;
}

void WeatherRouting::AdvanceMultiLegSequence(RouteMapOverlay* completedRoute) {
  if (!m_ActiveMultiLegSequence || !completedRoute) return;
  if (!RouteMapIsManaged(completedRoute)) {
    wxLogMessage(
        "WeatherRouting multi-leg sequence stopped: group=%s route pointer is "
        "no longer active",
        m_ActiveMultiLegGroupId);
    CancelMultiLegSequence();
    return;
  }

  RouteMapConfiguration completed = completedRoute->GetConfiguration();
  if (!completed.IsMultiLegGenerated ||
      completed.MultiLegGroupId != m_ActiveMultiLegGroupId ||
      completed.MultiLegLegIndex != m_ActiveMultiLegCurrentLegIndex)
    return;

  if (!completedRoute->Finished()) return;

  bool reachedDestination = completedRoute->ReachedDestination();
  wxDateTime eta = completedRoute->EndTime();
  if (reachedDestination && eta.IsValid()) {
    RouteMapConfiguration validationConfig = completedRoute->GetConfiguration();
    if (!completedRoute->ValidatePlottedDestinationRouteLand(
            validationConfig)) {
      reachedDestination = false;
      completedRoute->SetConfigurationPreserveResult(validationConfig);
      UpdateRouteMap(completedRoute);
    }
  }
  wxLogMessage(
      "WeatherRouting multi-leg sequence leg state: group=%s leg=%d/%d "
      "route=%p finished=%d reached=%d eta_valid=%d",
      completed.MultiLegGroupId, completed.MultiLegLegIndex,
      completed.MultiLegLegCount, completedRoute, completedRoute->Finished(),
      reachedDestination, eta.IsValid());

  if (!reachedDestination || !eta.IsValid()) {
    wxString reason = SafeMultiLegFailureReason(completedRoute);
    FinishRoutingProgress(
        _("Multi-leg route failed"),
        wxString::Format(_("Leg %d of %d failed: %s to %s\n%s"),
                         completed.MultiLegLegIndex, completed.MultiLegLegCount,
                         completed.Start, completed.End, reason));
    wxLogMessage(
        "WeatherRouting multi-leg sequence stopped at leg %d/%d: group=%s "
        "%s -> %s start_time=%s reached=%d eta_valid=%d reason=%s",
        completed.MultiLegLegIndex, completed.MultiLegLegCount,
        completed.MultiLegGroupId, completed.Start, completed.End,
        completed.StartTime.FormatISOCombined(), reachedDestination,
        eta.IsValid(), reason);
    m_ActiveMultiLegSequence = false;
    m_ActiveMultiLegGroupId.Clear();
    m_ActiveMultiLegCurrentLegIndex = 0;
    return;
  }

  wxLogMessage(
      "WeatherRouting multi-leg sequence completed leg %d/%d: group=%s "
      "eta=%s",
      completed.MultiLegLegIndex, completed.MultiLegLegCount,
      completed.MultiLegGroupId, eta.FormatISOCombined());

  if (completed.MultiLegLegIndex >= completed.MultiLegLegCount) {
    FinishRoutingProgress(
        _("Multi-leg route complete"),
        wxString::Format(_("Final ETA: %s"), eta.FormatISOCombined()));
    wxLogMessage(
        "WeatherRouting multi-leg sequence complete: group=%s final_eta=%s",
        completed.MultiLegGroupId, eta.FormatISOCombined());
    m_ActiveMultiLegSequence = false;
    m_ActiveMultiLegGroupId.Clear();
    m_ActiveMultiLegCurrentLegIndex = 0;
    return;
  }

  std::vector<RouteMapOverlay*> routes =
      GetMultiLegGroupRoutes(completed.MultiLegGroupId);
  RouteMapOverlay* nextRoute = NULL;
  for (auto route : routes) {
    RouteMapConfiguration configuration = route->GetConfiguration();
    if (configuration.MultiLegLegIndex == completed.MultiLegLegIndex + 1) {
      nextRoute = route;
      break;
    }
  }

  if (!nextRoute) {
    wxLogMessage(
        "WeatherRouting multi-leg sequence stopped: missing leg %d in "
        "group=%s",
        completed.MultiLegLegIndex + 1, completed.MultiLegGroupId);
    CancelMultiLegSequence();
    return;
  }

  RouteMapConfiguration next = nextRoute->GetConfiguration();
  next.StartTime = eta;
  next.UseCurrentTime = false;
  nextRoute->SetConfiguration(next);
  UpdateRouteMap(nextRoute);
  m_ActiveMultiLegCurrentLegIndex = next.MultiLegLegIndex;

  if (!StartMultiLegSequenceLeg(nextRoute)) CancelMultiLegSequence();
}

void WeatherRouting::CancelMultiLegDepartureOptimization(
    bool cleanupCandidates) {
  if (m_DeferredRoutingStartPending &&
      m_DeferredRoutingStartMode == DEFERRED_ROUTING_MULTILEG_OPTIMIZATION)
    CancelDeferredRoutingStart();

  if (m_ActiveMultiLegDepartureOptimization)
    wxLogMessage(
        "WeatherRouting multi-leg departure optimisation cancelled: "
        "id=%s",
        m_ActiveMultiLegOptimizationId);

  for (auto& candidate : m_MultiLegOptimizationCandidates) {
    for (auto route : candidate.routes) {
      if (route && RouteMapIsWaitingOrRunning(route)) Stop(route);
    }
  }

  if (cleanupCandidates) {
    std::list<RouteMapOverlay*> routesToDelete;
    for (auto& candidate : m_MultiLegOptimizationCandidates)
      for (auto route : candidate.routes)
        if (route && RouteMapIsManaged(route)) routesToDelete.push_back(route);
    DeleteRouteMaps(routesToDelete);
    m_MultiLegOptimizationCandidates.clear();
  }

  m_ActiveMultiLegDepartureOptimization = false;
  m_ActiveMultiLegOptimizationId.Clear();
  m_MultiLegOptimizationBaseGroupId.Clear();
  m_ActiveMultiLegOptimizationCandidateIndex = -1;
  m_ActiveMultiLegOptimizationLegIndex = 0;
  m_AppliedMultiLegOptimizationCandidateIndex = -1;
  CloseRoutingProgress();
}

void WeatherRouting::DeleteMultiLegOptimizationCandidateRows() {
  std::list<RouteMapOverlay*> routesToDelete;
  for (auto& candidate : m_MultiLegOptimizationCandidates) {
    for (auto route : candidate.routes)
      if (route && RouteMapIsManaged(route)) routesToDelete.push_back(route);
    candidate.routes.clear();
  }
  DeleteRouteMaps(routesToDelete);
}

bool WeatherRouting::HasCompleteMultiLegOptimizationCandidate() const {
  for (const auto& candidate : m_MultiLegOptimizationCandidates)
    if (candidate.complete) return true;
  return false;
}

bool WeatherRouting::ApplyMultiLegOptimizationCandidate(int candidateIndex) {
  if (m_ActiveMultiLegDepartureOptimization) {
    wxMessageBox(_("Wait for the optimisation to finish before applying a "
                   "candidate."),
                 _("Weather Routing"), wxOK | wxICON_WARNING, this);
    return false;
  }
  if (m_AppliedMultiLegOptimizationCandidateIndex >= 0) {
    if (m_AppliedMultiLegOptimizationCandidateIndex == candidateIndex)
      return true;
    wxMessageBox(_("A multi-leg departure candidate has already been applied. "
                   "Run the optimisation again to choose a different result."),
                 _("Weather Routing"), wxOK | wxICON_INFORMATION, this);
    return false;
  }
  if (candidateIndex < 0 ||
      candidateIndex >= (int)m_MultiLegOptimizationCandidates.size())
    return false;

  MultiLegOptimizationCandidate& candidate =
      m_MultiLegOptimizationCandidates[candidateIndex];
  if (!candidate.complete) {
    wxMessageBox(_("Only complete multi-leg candidates can be applied."),
                 _("Weather Routing"), wxOK | wxICON_WARNING, this);
    return false;
  }

  std::vector<RouteMapOverlay*> baseRoutes =
      GetMultiLegGroupRoutes(m_MultiLegOptimizationBaseGroupId);
  if (baseRoutes.size() != candidate.routes.size() || baseRoutes.empty()) {
    wxMessageBox(_("The original multi-leg group is no longer available."),
                 _("Weather Routing"), wxOK | wxICON_WARNING, this);
    return false;
  }

  for (size_t i = 0; i < baseRoutes.size(); ++i) {
    RouteMapOverlay* baseRoute = baseRoutes[i];
    RouteMapOverlay* candidateRoute = candidate.routes[i];
    if (!baseRoute || !candidateRoute || !RouteMapIsManaged(baseRoute) ||
        !RouteMapIsManaged(candidateRoute)) {
      wxLogMessage(
          "WeatherRouting multi-leg departure optimisation apply failed: "
          "candidate=%d leg=%zu base=%p candidate_route=%p",
          candidateIndex, i + 1, baseRoute, candidateRoute);
      wxMessageBox(
          _("The selected multi-leg candidate is no longer available."),
          _("Weather Routing"), wxOK | wxICON_WARNING, this);
      return false;
    }
    RouteMapConfiguration validationConfig = candidateRoute->GetConfiguration();
    if (!candidateRoute->ValidatePlottedDestinationRouteLand(
            validationConfig)) {
      candidate.complete = false;
      candidate.failed = true;
      candidate.failedLegIndex = validationConfig.MultiLegLegIndex > 0
                                     ? validationConfig.MultiLegLegIndex
                                     : static_cast<int>(i + 1);
      candidate.failedLegName = wxString::Format(
          _T("%s to %s"), validationConfig.Start, validationConfig.End);
      candidate.state = _("Failed");
      candidate.reason = candidateRoute->GetFailureReason();
      if (candidate.reason.IsEmpty())
        candidate.reason = _("Chart land crossing in final route");
      candidateRoute->SetConfigurationPreserveResult(validationConfig);
      UpdateRouteMap(candidateRoute);
      wxLogMessage(
          "WeatherRouting multi-leg departure optimisation apply blocked: "
          "candidate=%d leg=%zu reason=%s",
          candidateIndex, i + 1, candidate.reason);
      wxMessageBox(
          _("The selected multi-leg candidate crosses chart land and cannot be "
            "applied."),
          _("Weather Routing"), wxOK | wxICON_WARNING, this);
      return false;
    }
  }

  wxLogMessage(
      "WeatherRouting multi-leg departure optimisation applying: index=%d "
      "offset=%d base_rows=%zu candidate_rows=%zu",
      candidateIndex, candidate.offsetMinutes, baseRoutes.size(),
      candidate.routes.size());

  auto findWeatherRoute = [&](RouteMapOverlay* route) -> WeatherRoute* {
    for (auto weatherroute : m_WeatherRoutes)
      if (weatherroute->routemapoverlay == route) return weatherroute;
    return NULL;
  };

  std::vector<RouteMapConfiguration> originalCandidateConfigs;
  std::vector<bool> originalCandidateFiltered;
  originalCandidateConfigs.reserve(candidate.routes.size());
  originalCandidateFiltered.reserve(candidate.routes.size());
  for (auto route : candidate.routes) {
    originalCandidateConfigs.push_back(route->GetConfiguration());
    WeatherRoute* weatherroute = findWeatherRoute(route);
    originalCandidateFiltered.push_back(weatherroute ? weatherroute->Filtered
                                                     : false);
  }

  std::vector<RouteMapConfiguration> originalBaseConfigs;
  std::vector<bool> originalBaseFiltered;
  originalBaseConfigs.reserve(baseRoutes.size());
  originalBaseFiltered.reserve(baseRoutes.size());
  for (auto route : baseRoutes) {
    originalBaseConfigs.push_back(route->GetConfiguration());
    WeatherRoute* weatherroute = findWeatherRoute(route);
    originalBaseFiltered.push_back(weatherroute ? weatherroute->Filtered
                                                : false);
  }

  std::vector<std::pair<RouteMapOverlay*, bool> > originalOtherCandidateFilter;
  for (size_t i = 0; i < m_MultiLegOptimizationCandidates.size(); ++i) {
    if ((int)i == candidateIndex) continue;
    for (auto route : m_MultiLegOptimizationCandidates[i].routes) {
      WeatherRoute* weatherroute = findWeatherRoute(route);
      originalOtherCandidateFilter.push_back(
          std::make_pair(route, weatherroute ? weatherroute->Filtered : false));
    }
  }

  auto rollbackPromotedCandidate = [&]() {
    for (size_t i = 0; i < candidate.routes.size(); ++i) {
      if (candidate.routes[i])
        candidate.routes[i]->SetConfigurationPreserveResult(
            originalCandidateConfigs[i]);
      WeatherRoute* weatherroute = findWeatherRoute(candidate.routes[i]);
      if (weatherroute) weatherroute->Filtered = originalCandidateFiltered[i];
    }
    for (size_t i = 0; i < baseRoutes.size(); ++i) {
      if (baseRoutes[i])
        baseRoutes[i]->SetConfigurationPreserveResult(originalBaseConfigs[i]);
      WeatherRoute* weatherroute = findWeatherRoute(baseRoutes[i]);
      if (weatherroute) weatherroute->Filtered = originalBaseFiltered[i];
    }
    for (auto entry : originalOtherCandidateFilter) {
      WeatherRoute* weatherroute = findWeatherRoute(entry.first);
      if (weatherroute) weatherroute->Filtered = entry.second;
    }
  };

  auto visibleAppliedRows = [&](const wxString& groupId) {
    int count = 0;
    if (!m_panel || !m_panel->m_lWeatherRoutes) return count;
    for (long i = 0; i < m_panel->m_lWeatherRoutes->GetItemCount(); ++i) {
      WeatherRoute* weatherroute = reinterpret_cast<WeatherRoute*>(
          wxUIntToPtr(m_panel->m_lWeatherRoutes->GetItemData(i)));
      if (!weatherroute || !weatherroute->routemapoverlay) continue;
      RouteMapConfiguration configuration =
          weatherroute->routemapoverlay->GetConfiguration();
      if (configuration.IsMultiLegGenerated &&
          !configuration.DepartureTimeOptimizationCandidate &&
          configuration.MultiLegGroupId == groupId)
        count++;
    }
    return count;
  };

  for (size_t i = 0; i < baseRoutes.size(); ++i) {
    RouteMapOverlay* baseRoute = baseRoutes[i];
    RouteMapOverlay* candidateRoute = candidate.routes[i];

    RouteMapConfiguration baseConfig = baseRoute->GetConfiguration();
    RouteMapConfiguration appliedConfig = candidateRoute->GetConfiguration();
    appliedConfig.RouteGUID.Clear();
    appliedConfig.StartType = baseConfig.StartType;
    appliedConfig.Start = baseConfig.Start;
    appliedConfig.StartGUID = baseConfig.StartGUID;
    appliedConfig.StartLat = baseConfig.StartLat;
    appliedConfig.StartLon = baseConfig.StartLon;
    appliedConfig.EndType = baseConfig.EndType;
    appliedConfig.End = baseConfig.End;
    appliedConfig.EndGUID = baseConfig.EndGUID;
    appliedConfig.EndLat = baseConfig.EndLat;
    appliedConfig.EndLon = baseConfig.EndLon;
    appliedConfig.DepartureTimeOptimizationEnabled =
        baseConfig.DepartureTimeOptimizationEnabled;
    appliedConfig.DepartureTimeOptimizationCandidate = false;
    appliedConfig.DepartureTimeOptimizationGroupId.Clear();
    appliedConfig.DepartureTimeOptimizationOffsetMinutes = 0;
    appliedConfig.IsMultiLegGenerated = true;
    appliedConfig.MultiLegGroupId = baseConfig.MultiLegGroupId;
    appliedConfig.MultiLegParentRouteGUID = baseConfig.MultiLegParentRouteGUID;
    appliedConfig.MultiLegParentRouteName = baseConfig.MultiLegParentRouteName;
    appliedConfig.MultiLegLegIndex = baseConfig.MultiLegLegIndex;
    appliedConfig.MultiLegLegCount = baseConfig.MultiLegLegCount;
    candidateRoute->SetConfigurationPreserveResult(appliedConfig);

    WeatherRoute* candidateWeatherRoute = findWeatherRoute(candidateRoute);
    if (candidateWeatherRoute) candidateWeatherRoute->Filtered = false;
  }

  int retiredBaseRows = 0;
  for (auto route : baseRoutes) {
    if (!route || !RouteMapIsManaged(route)) continue;
    RouteMapConfiguration retiredConfig = route->GetConfiguration();
    retiredConfig.DepartureTimeOptimizationCandidate = true;
    retiredConfig.DepartureTimeOptimizationGroupId =
        m_ActiveMultiLegOptimizationId;
    retiredConfig.MultiLegGroupId =
        retiredConfig.MultiLegGroupId + _T("-replaced");
    route->SetConfigurationPreserveResult(retiredConfig);
    WeatherRoute* weatherroute = findWeatherRoute(route);
    if (weatherroute) weatherroute->Filtered = true;
    retiredBaseRows++;
  }

  int removedTemporaryRows = 0;
  std::list<RouteMapOverlay*> temporaryRoutesToDelete;
  for (size_t i = 0; i < m_MultiLegOptimizationCandidates.size(); ++i) {
    MultiLegOptimizationCandidate& cleanupCandidate =
        m_MultiLegOptimizationCandidates[i];
    if ((int)i == candidateIndex) {
      cleanupCandidate.routes.clear();
      continue;
    }

    for (auto route : cleanupCandidate.routes) {
      if (route && RouteMapIsManaged(route)) {
        WeatherRoute* weatherroute = findWeatherRoute(route);
        if (weatherroute) weatherroute->Filtered = true;
        temporaryRoutesToDelete.push_back(route);
        removedTemporaryRows++;
      }
    }
  }

  wxLogMessage(
      "WeatherRouting multi-leg departure optimisation applied: index=%d "
      "offset=%d promoted_rows=%zu removed_temporary_rows=%d "
      "retired_base_rows=%d",
      candidateIndex, candidate.offsetMinutes, baseRoutes.size(),
      removedTemporaryRows, retiredBaseRows);

  RebuildList();
  int visibleRows = visibleAppliedRows(m_MultiLegOptimizationBaseGroupId);
  std::vector<RouteMapOverlay*> appliedRoutes =
      GetMultiLegGroupRoutes(m_MultiLegOptimizationBaseGroupId);
  if (appliedRoutes.size() != baseRoutes.size() ||
      visibleRows != (int)baseRoutes.size()) {
    rollbackPromotedCandidate();
    RebuildList();
    wxLogMessage(
        "WeatherRouting multi-leg departure optimisation post-apply "
        "verification failed: index=%d applied_group_rows=%zu visible_rows=%d "
        "expected=%zu",
        candidateIndex, appliedRoutes.size(), visibleRows, baseRoutes.size());
    wxMessageBox(
        _("The selected multi-leg candidate was applied, but the route list "
          "did "
          "not refresh as expected. The candidate rows were not deleted."),
        _("Weather Routing"), wxOK | wxICON_WARNING, this);
    return false;
  }

  if (!temporaryRoutesToDelete.empty()) {
    DeleteRouteMaps(temporaryRoutesToDelete);
    for (size_t i = 0; i < m_MultiLegOptimizationCandidates.size(); ++i)
      if ((int)i != candidateIndex)
        m_MultiLegOptimizationCandidates[i].routes.clear();
  }

  for (size_t i = 0; i < m_MultiLegOptimizationCandidates.size(); ++i)
    m_MultiLegOptimizationCandidates[i].applied = false;
  candidate.applied = true;
  m_AppliedMultiLegOptimizationCandidateIndex = candidateIndex;

  SelectMultiLegGroup(m_MultiLegOptimizationBaseGroupId);
  UpdateDialogs();
  GetParent()->Refresh();
  m_tAutoSaveXML.Start(5000, true);
  wxLogMessage(
      "WeatherRouting multi-leg departure optimisation apply complete: "
      "weather_route_count=%zu group=%s",
      m_WeatherRoutes.size(), m_MultiLegOptimizationBaseGroupId);
  return true;
}

bool WeatherRouting::ApplyBestMultiLegOptimizationCandidate() {
  int bestIndex = -1;
  long bestSeconds = std::numeric_limits<long>::max();
  for (size_t i = 0; i < m_MultiLegOptimizationCandidates.size(); ++i) {
    const MultiLegOptimizationCandidate& candidate =
        m_MultiLegOptimizationCandidates[i];
    if (!candidate.complete) continue;
    if (candidate.best || (candidate.totalElapsedSeconds >= 0 &&
                           candidate.totalElapsedSeconds < bestSeconds)) {
      bestIndex = i;
      bestSeconds = candidate.totalElapsedSeconds;
      if (candidate.best) break;
    }
  }
  return ApplyMultiLegOptimizationCandidate(bestIndex);
}

bool WeatherRouting::StartMultiLegOptimizationLeg(
    RouteMapOverlay* routemapoverlay) {
  if (!routemapoverlay) return false;

  RouteMapConfiguration configuration = routemapoverlay->GetConfiguration();
  wxLogMessage(
      "WeatherRouting multi-leg departure optimisation starting candidate=%d "
      "leg=%d/%d departure=%s start=%s end=%s",
      configuration.DepartureTimeOptimizationOffsetMinutes,
      configuration.MultiLegLegIndex, configuration.MultiLegLegCount,
      configuration.StartTime.FormatISOCombined(), configuration.Start,
      configuration.End);
  int candidateIndex = m_ActiveMultiLegOptimizationCandidateIndex;
  int candidateCount = (int)m_MultiLegOptimizationCandidates.size();
  int totalSteps = wxMax(1, candidateCount * configuration.MultiLegLegCount);
  int currentStep = wxMax(0, candidateIndex * configuration.MultiLegLegCount +
                                 configuration.MultiLegLegIndex - 1);
  UpdateRoutingProgress(
      _("Computing multi-leg departure optimisation"),
      wxString::Format(
          _("Candidate %d of %d, leg %d of %d: %s to %s\nDeparture: %s"),
          candidateIndex + 1, candidateCount, configuration.MultiLegLegIndex,
          configuration.MultiLegLegCount, configuration.Start,
          configuration.End, configuration.StartTime.FormatISOCombined()),
      currentStep, totalSteps);

  if (RouteMapIsWaitingOrRunning(routemapoverlay)) {
    Stop(routemapoverlay);
    m_RunningRouteMaps.remove(routemapoverlay);
    m_WaitingRouteMaps.remove(routemapoverlay);
  }
  routemapoverlay->Reset();
  UpdateRouteMap(routemapoverlay);
  Start(routemapoverlay);

  if (!RouteMapIsWaitingOrRunning(routemapoverlay)) {
    UpdateRouteMap(routemapoverlay);
    return false;
  }

  m_panel->m_gProgress->SetRange(m_RoutesToRun);
  return true;
}

bool WeatherRouting::StartNextMultiLegOptimizationCandidate() {
  if (!m_ActiveMultiLegDepartureOptimization) return false;

  m_ActiveMultiLegOptimizationCandidateIndex++;
  m_ActiveMultiLegOptimizationLegIndex = 1;

  while (m_ActiveMultiLegOptimizationCandidateIndex <
         (int)m_MultiLegOptimizationCandidates.size()) {
    MultiLegOptimizationCandidate& candidate = m_MultiLegOptimizationCandidates
        [m_ActiveMultiLegOptimizationCandidateIndex];
    if (candidate.failed &&
        (int)candidate.routes.size() != candidate.totalLegs) {
      m_ActiveMultiLegOptimizationCandidateIndex++;
      continue;
    }
    if (candidate.routes.empty()) {
      candidate.failed = true;
      candidate.state = _("Failed");
      candidate.reason = _("No route legs were created for this candidate.");
      m_ActiveMultiLegOptimizationCandidateIndex++;
      continue;
    }

    candidate.running = true;
    candidate.state = _("Computing...");
    candidate.reason.Clear();
    UpdateRoutingProgress(
        _("Computing candidate"),
        wxString::Format(_("Candidate %d of %d: offset %+d min, departure %s"),
                         m_ActiveMultiLegOptimizationCandidateIndex + 1,
                         (int)m_MultiLegOptimizationCandidates.size(),
                         candidate.offsetMinutes,
                         candidate.departureTime.FormatISOCombined()),
        m_ActiveMultiLegOptimizationCandidateIndex * candidate.totalLegs,
        (int)m_MultiLegOptimizationCandidates.size() * candidate.totalLegs);
    wxLogMessage(
        "WeatherRouting multi-leg departure optimisation candidate started: "
        "index=%d offset=%d departure=%s",
        m_ActiveMultiLegOptimizationCandidateIndex, candidate.offsetMinutes,
        candidate.departureTime.FormatISOCombined());

    if (!StartMultiLegOptimizationLeg(candidate.routes.front())) {
      candidate.running = false;
      candidate.failed = true;
      candidate.state = _("Failed");
      candidate.reason = _("Could not start first leg.");
      m_ActiveMultiLegOptimizationCandidateIndex++;
      continue;
    }

    UpdateComputeState();
    return true;
  }

  long bestSeconds = std::numeric_limits<long>::max();
  int bestIndex = -1;
  for (size_t i = 0; i < m_MultiLegOptimizationCandidates.size(); ++i) {
    MultiLegOptimizationCandidate& candidate =
        m_MultiLegOptimizationCandidates[i];
    candidate.best = false;
    if (!candidate.complete) continue;
    if (candidate.totalElapsedSeconds >= 0 &&
        candidate.totalElapsedSeconds < bestSeconds) {
      bestSeconds = candidate.totalElapsedSeconds;
      bestIndex = i;
    }
  }
  if (bestIndex >= 0) m_MultiLegOptimizationCandidates[bestIndex].best = true;

  m_ActiveMultiLegDepartureOptimization = false;
  long completed = 0;
  long failed = 0;
  for (size_t i = 0; i < m_MultiLegOptimizationCandidates.size(); ++i) {
    if (m_MultiLegOptimizationCandidates[i].complete)
      ++completed;
    else if (m_MultiLegOptimizationCandidates[i].failed)
      ++failed;
  }
  wxLogMessage(
      "WeatherRouting multi-leg departure optimisation complete: id=%s "
      "best_index=%d candidates_completed=%ld candidates_failed=%ld "
      "candidate_count=%lu",
      m_ActiveMultiLegOptimizationId, bestIndex, completed, failed,
      static_cast<unsigned long>(m_MultiLegOptimizationCandidates.size()));
  ConstraintChecker::LogSegmentSafetyDiagnostics(
      wxString::Format(_("multi-leg departure optimisation %s"),
                       m_ActiveMultiLegOptimizationId));
  FinishRoutingProgress(
      _("Multi-leg departure optimisation complete"),
      wxString::Format(_("Completed candidates: %ld\nFailed candidates: %ld"),
                       completed, failed));
  return false;
}

void WeatherRouting::AdvanceMultiLegDepartureOptimization(
    RouteMapOverlay* completedRoute) {
  if (!m_ActiveMultiLegDepartureOptimization || !completedRoute) return;
  if (m_ActiveMultiLegOptimizationCandidateIndex < 0 ||
      m_ActiveMultiLegOptimizationCandidateIndex >=
          (int)m_MultiLegOptimizationCandidates.size())
    return;

  MultiLegOptimizationCandidate& candidate = m_MultiLegOptimizationCandidates
      [m_ActiveMultiLegOptimizationCandidateIndex];

  auto routeIt = std::find(candidate.routes.begin(), candidate.routes.end(),
                           completedRoute);
  if (routeIt == candidate.routes.end()) return;

  int legIndex = (int)(routeIt - candidate.routes.begin()) + 1;
  if (legIndex != m_ActiveMultiLegOptimizationLegIndex) return;
  if (!completedRoute->Finished()) return;

  RouteMapConfiguration completed = completedRoute->GetConfiguration();
  wxDateTime eta = completedRoute->EndTime();
  bool completeLeg = completedRoute->ReachedDestination() && eta.IsValid();
  if (completeLeg) {
    RouteMapConfiguration validationConfig = completedRoute->GetConfiguration();
    if (!completedRoute->ValidatePlottedDestinationRouteLand(
            validationConfig)) {
      completeLeg = false;
      completedRoute->SetConfigurationPreserveResult(validationConfig);
      UpdateRouteMap(completedRoute);
    }
  }
  wxLogMessage(
      "WeatherRouting multi-leg departure optimisation leg state: "
      "candidate=%d leg=%d/%d finished=%d reached=%d eta_valid=%d",
      candidate.offsetMinutes, legIndex, candidate.totalLegs,
      completedRoute->Finished(), completedRoute->ReachedDestination(),
      eta.IsValid());

  if (!completeLeg) {
    candidate.running = false;
    candidate.failed = true;
    candidate.complete = false;
    candidate.completedLegs = legIndex > 0 ? legIndex - 1 : 0;
    candidate.failedLegIndex = legIndex;
    candidate.failedLegName =
        wxString::Format(_T("%s to %s"), completed.Start, completed.End);
    candidate.state = _("Failed");
    candidate.reason = SafeMultiLegFailureReason(completedRoute);
    UpdateRoutingProgress(
        _("Candidate failed"),
        wxString::Format(_("Candidate offset %+d min failed at leg %d of %d: "
                           "%s\n%s"),
                         candidate.offsetMinutes, legIndex, candidate.totalLegs,
                         candidate.failedLegName, candidate.reason),
        m_ActiveMultiLegOptimizationCandidateIndex * candidate.totalLegs +
            legIndex,
        (int)m_MultiLegOptimizationCandidates.size() * candidate.totalLegs);
    wxLogMessage(
        "WeatherRouting multi-leg departure optimisation candidate failed: "
        "offset=%d leg=%d/%d reason=%s",
        candidate.offsetMinutes, legIndex, candidate.totalLegs,
        candidate.reason);
    StartNextMultiLegOptimizationCandidate();
    return;
  }

  candidate.completedLegs = legIndex;
  double legDistance = completedRoute->RouteInfo(RouteMapOverlay::DISTANCE);
  if (std::isfinite(legDistance)) candidate.totalDistance += legDistance;

  if (legIndex >= candidate.totalLegs) {
    candidate.running = false;
    candidate.complete = true;
    candidate.failed = false;
    candidate.finalEta = eta;
    wxTimeSpan elapsed = eta - candidate.departureTime;
    candidate.totalElapsedSeconds = elapsed.GetSeconds().ToLong();
    candidate.state = _("Complete");
    candidate.reason.Clear();
    wxLogMessage(
        "WeatherRouting multi-leg departure optimisation candidate complete: "
        "offset=%d final_eta=%s elapsed=%ld",
        candidate.offsetMinutes, eta.FormatISOCombined(),
        candidate.totalElapsedSeconds);
    UpdateRoutingProgress(
        _("Candidate complete"),
        wxString::Format(_("Candidate offset %+d min complete\nFinal ETA: %s"),
                         candidate.offsetMinutes, eta.FormatISOCombined()),
        (m_ActiveMultiLegOptimizationCandidateIndex + 1) * candidate.totalLegs,
        (int)m_MultiLegOptimizationCandidates.size() * candidate.totalLegs);
    StartNextMultiLegOptimizationCandidate();
    return;
  }

  RouteMapOverlay* nextRoute = candidate.routes[legIndex];
  RouteMapConfiguration next = nextRoute->GetConfiguration();
  next.StartTime = eta;
  next.UseCurrentTime = false;
  nextRoute->SetConfiguration(next);
  UpdateRouteMap(nextRoute);
  m_ActiveMultiLegOptimizationLegIndex = legIndex + 1;

  if (!StartMultiLegOptimizationLeg(nextRoute)) {
    candidate.running = false;
    candidate.failed = true;
    candidate.failedLegIndex = legIndex + 1;
    candidate.failedLegName =
        wxString::Format(_T("%s to %s"), next.Start, next.End);
    candidate.state = _("Failed");
    candidate.reason = _("Could not start next leg.");
    StartNextMultiLegOptimizationCandidate();
  }
}

bool WeatherRouting::ComputeMultiLegDepartureOptimization(
    RouteMapOverlay* selectedRoute) {
  if (!selectedRoute) return false;

  RouteMapConfiguration selected = selectedRoute->GetConfiguration();
  if (!selected.IsMultiLegGenerated || selected.MultiLegGroupId.IsEmpty()) {
    wxMessageBox(_("Select a generated multi-leg route row first."),
                 _("Weather Routing"), wxOK | wxICON_WARNING, this);
    return false;
  }

  std::vector<RouteMapOverlay*> baseRoutes =
      GetMultiLegGroupRoutes(selected.MultiLegGroupId);
  if (baseRoutes.empty()) return false;

  RouteMapConfiguration first = baseRoutes.front()->GetConfiguration();
  int rangeMinutes = first.DepartureTimeOptimizationRangeMinutes;
  int stepMinutes = first.DepartureTimeOptimizationStepMinutes;
  if (rangeMinutes < 0) {
    wxMessageBox(_("Departure optimisation range must not be negative."),
                 _("Weather Routing"), wxOK | wxICON_ERROR, this);
    return false;
  }
  if (stepMinutes <= 0) {
    wxMessageBox(_("Departure optimisation step must be greater than zero."),
                 _("Weather Routing"), wxOK | wxICON_ERROR, this);
    return false;
  }

  std::vector<int> offsets;
  for (int offset = -rangeMinutes; offset <= rangeMinutes;
       offset += stepMinutes)
    offsets.push_back(offset);
  offsets.push_back(0);
  std::sort(offsets.begin(), offsets.end());
  offsets.erase(std::unique(offsets.begin(), offsets.end()), offsets.end());
  weather_routing::OrderDepartureOffsets(offsets);

  if (offsets.size() > MAX_DEPARTURE_OPTIMIZATION_CANDIDATES) {
    wxMessageBox(
        wxString::Format(
            _("Departure optimisation would create %d candidate chains. "
              "Increase the step or reduce the range. The current limit is "
              "%d."),
            (int)offsets.size(), MAX_DEPARTURE_OPTIMIZATION_CANDIDATES),
        _("Weather Routing"), wxOK | wxICON_ERROR, this);
    return false;
  }

  if (m_DeferredRoutingStartPending) {
    wxMessageBox(_("A multi-leg routing start is already pending."),
                 _("Weather Routing"), wxOK | wxICON_WARNING, this);
    return false;
  }

  CancelMultiLegSequence();
  CancelMultiLegDepartureOptimization(true);

  ShowRoutingProgress(_("Weather Routing Progress"));
  UpdateRoutingProgress(
      _("Preparing routes"),
      wxString::Format(
          _("Preparing multi-leg departure optimisation: %lu candidates, %lu "
            "legs"),
          static_cast<unsigned long>(offsets.size()),
          static_cast<unsigned long>(baseRoutes.size())),
      0, wxMax(1, (int)(offsets.size() * baseRoutes.size())));
  ScheduleDeferredRoutingStart(DEFERRED_ROUTING_MULTILEG_OPTIMIZATION,
                               selected.MultiLegGroupId);
  return true;
}

bool WeatherRouting::ComputeMultiLegDepartureOptimizationNow(
    const wxString& groupId) {
  std::vector<RouteMapOverlay*> baseRoutes = GetMultiLegGroupRoutes(groupId);
  if (baseRoutes.empty()) {
    FinishRoutingProgress(_("Multi-leg departure optimisation failed"),
                          _("The selected multi-leg group is unavailable."));
    return false;
  }

  RouteMapConfiguration first = baseRoutes.front()->GetConfiguration();
  int rangeMinutes = first.DepartureTimeOptimizationRangeMinutes;
  int stepMinutes = first.DepartureTimeOptimizationStepMinutes;
  if (rangeMinutes < 0 || stepMinutes <= 0) {
    FinishRoutingProgress(
        _("Multi-leg departure optimisation failed"),
        _("Departure optimisation range or step is invalid."));
    return false;
  }

  std::vector<int> offsets;
  for (int offset = -rangeMinutes; offset <= rangeMinutes;
       offset += stepMinutes)
    offsets.push_back(offset);
  offsets.push_back(0);
  std::sort(offsets.begin(), offsets.end());
  offsets.erase(std::unique(offsets.begin(), offsets.end()), offsets.end());
  weather_routing::OrderDepartureOffsets(offsets);

  if (offsets.size() > MAX_DEPARTURE_OPTIMIZATION_CANDIDATES) {
    FinishRoutingProgress(
        _("Multi-leg departure optimisation failed"),
        _("Departure optimisation would create too many candidate chains."));
    return false;
  }

  m_ActiveMultiLegOptimizationId = wxString::Format(
      _T("multileg-opt-%s"), wxDateTime::UNow().FormatISOCombined());
  m_MultiLegOptimizationBaseGroupId = groupId;
  m_MultiLegOptimizationCandidates.clear();
  m_AppliedMultiLegOptimizationCandidateIndex = -1;
  wxDateTime nominalStartTime = first.StartTime;
  UpdateRoutingProgress(
      _("Preparing routes"),
      wxString::Format(
          _("Preparing multi-leg departure optimisation: %lu candidates, %lu "
            "legs"),
          static_cast<unsigned long>(offsets.size()),
          static_cast<unsigned long>(baseRoutes.size())),
      0, wxMax(1, (int)(offsets.size() * baseRoutes.size())));

  for (auto offset : offsets) {
    MultiLegOptimizationCandidate candidate;
    candidate.offsetMinutes = offset;
    candidate.departureTime = nominalStartTime + wxTimeSpan::Minutes(offset);
    candidate.totalElapsedSeconds = -1;
    candidate.totalDistance = 0.0;
    candidate.completedLegs = 0;
    candidate.totalLegs = (int)baseRoutes.size();
    candidate.failedLegIndex = 0;
    candidate.complete = false;
    candidate.failed = false;
    candidate.running = false;
    candidate.best = false;
    candidate.applied = false;
    candidate.state = _("Waiting...");

    wxString candidateGroupId =
        wxString::Format(_T("%s-%+d"), m_ActiveMultiLegOptimizationId, offset);
    for (size_t i = 0; i < baseRoutes.size(); ++i) {
      RouteMapConfiguration leg = baseRoutes[i]->GetConfiguration();
      leg.RouteGUID.Clear();
      leg.DepartureTimeOptimizationEnabled = false;
      leg.DepartureTimeOptimizationCandidate = true;
      leg.DepartureTimeOptimizationNominalStartTime = nominalStartTime;
      leg.DepartureTimeOptimizationOffsetMinutes = offset;
      leg.DepartureTimeOptimizationGroupId = m_ActiveMultiLegOptimizationId;
      leg.MultiLegGroupId = candidateGroupId;
      leg.MultiLegLegIndex = i + 1;
      leg.MultiLegLegCount = baseRoutes.size();
      leg.MultiLegParentRouteName = wxString::Format(
          _("%s departure %+d min"), first.MultiLegParentRouteName, offset);
      leg.StartTime = candidate.departureTime;
      leg.UseCurrentTime = false;
      leg.ChartSafetyPropagationFallbackTried = false;
      ApplyAuthoritativeChartSearchPolicy(leg);
      if (!AddConfiguration(leg)) continue;
      RouteMapOverlay* route = m_WeatherRoutes.back()->routemapoverlay;
      route->LoadBoat();
      candidate.routes.push_back(route);
    }

    if ((int)candidate.routes.size() != candidate.totalLegs) {
      candidate.failed = true;
      candidate.state = _("Failed");
      candidate.reason = _("Could not create all candidate route legs.");
    }
    m_MultiLegOptimizationCandidates.push_back(candidate);
  }

  if (m_MultiLegOptimizationCandidates.empty()) return false;

  std::vector<RouteMapOverlay*> first_leg_candidates;
  for (std::vector<MultiLegOptimizationCandidate>::iterator candidate =
           m_MultiLegOptimizationCandidates.begin();
       candidate != m_MultiLegOptimizationCandidates.end(); ++candidate) {
    if (!candidate->routes.empty() && candidate->routes.front())
      first_leg_candidates.push_back(candidate->routes.front());
  }
  PrepareChartSafetyScoutEnvelopes(first_leg_candidates,
                                   _("multi-leg departure first-leg scouts"));

  m_ActiveMultiLegDepartureOptimization = true;
  m_ActiveMultiLegOptimizationCandidateIndex = -1;
  m_ActiveMultiLegOptimizationLegIndex = 0;

  wxLogMessage(
      "WeatherRouting multi-leg departure optimisation starting: id=%s "
      "candidates=%lu legs=%lu",
      m_ActiveMultiLegOptimizationId,
      static_cast<unsigned long>(m_MultiLegOptimizationCandidates.size()),
      static_cast<unsigned long>(baseRoutes.size()));

  StartNextMultiLegOptimizationCandidate();
  UpdateComputeState();
  ShowMultiLegDepartureOptimizationResults();
  return true;
}

void WeatherRouting::OnHeadlessRouteTestTimer(wxTimerEvent&) {
  if (!m_HeadlessRouteTestState) {
    m_tHeadlessRouteTest.Stop();
    return;
  }

  const long elapsed_ms =
      (wxGetUTCTimeMillis() - m_HeadlessRouteTestState->startedMs).ToLong();
  bool active = !m_RunningRouteMaps.empty() || !m_WaitingRouteMaps.empty();
  if (m_HeadlessRouteTestState->kind ==
          HeadlessRouteTestState::Kind::SingleRoute &&
      !active && m_HeadlessRouteTestState->departureOptimization) {
    for (auto route : m_DepartureOptimizationRoutes) {
      if (route && !route->Finished()) {
        active = true;
        break;
      }
    }
  } else if (m_HeadlessRouteTestState->kind ==
             HeadlessRouteTestState::Kind::MultiLeg) {
    active = active || m_ActiveMultiLegSequence ||
             m_ActiveMultiLegDepartureOptimization;
  }

  const weather_routing_headless::HeadlessRouteMonitorDecision decision =
      weather_routing_headless::EvaluateHeadlessRouteMonitor(
          active, elapsed_ms, m_HeadlessRouteTestState->timeoutMs);
  if (decision ==
      weather_routing_headless::HeadlessRouteMonitorDecision::Continue)
    return;

  m_tHeadlessRouteTest.Stop();
  const HeadlessRouteTestState::Kind kind = m_HeadlessRouteTestState->kind;
  const bool timed_out =
      decision == weather_routing_headless::HeadlessRouteMonitorDecision::Timeout;
  if (kind == HeadlessRouteTestState::Kind::SingleRoute)
    CompleteHeadlessSingleRouteTest(timed_out, elapsed_ms);
  else
    CompleteHeadlessMultiLegTest(timed_out, elapsed_ms);
}

void WeatherRouting::ClearExternalPlanningScenario() {
  m_HeadlessRouteTestState.reset();
}

void WeatherRouting::CompleteHeadlessSingleRouteTest(bool timed_out,
                                                     long elapsed_ms) {
  RouteMapOverlay* selected_route = m_HeadlessRouteTestState->selectedRoute;
  RouteMapConfiguration selected_config =
      selected_route ? selected_route->GetConfiguration()
                     : RouteMapConfiguration();
  if (timed_out) {
    wxLogMessage(
        "WR_HEADLESS_ROUTE_TEST timeout route=\"%s to %s\" "
        "elapsed_ms=%ld running=%lu waiting=%lu.",
        selected_config.Start, selected_config.End, elapsed_ms,
        static_cast<unsigned long>(m_RunningRouteMaps.size()),
        static_cast<unsigned long>(m_WaitingRouteMaps.size()));
    StopAll();
  }

  int complete = 0;
  int failed = 0;
  int running = 0;
  int waiting = 0;
  std::vector<RouteMapOverlay*> routes;
  if (m_HeadlessRouteTestState->departureOptimization) {
    for (auto route : m_DepartureOptimizationRoutes) routes.push_back(route);
  } else if (selected_route) {
    routes.push_back(selected_route);
  }
  for (size_t i = 0; i < routes.size(); ++i) {
    RouteMapOverlay* route = routes[i];
    if (!route) continue;
    RouteMapConfiguration configuration = route->GetConfiguration();
    bool is_running = false;
    bool is_waiting = false;
    for (auto running_route : m_RunningRouteMaps)
      if (running_route == route) is_running = true;
    for (auto waiting_route : m_WaitingRouteMaps)
      if (waiting_route == route) is_waiting = true;
    const bool is_complete = route->Finished() && route->ReachedDestination();
    const bool is_failed = route->Finished() && !route->ReachedDestination();
    if (is_complete) complete++;
    if (is_failed) failed++;
    if (is_running) running++;
    if (is_waiting) waiting++;
    const wxString state = is_complete  ? _("Complete")
                           : is_failed  ? _("Failed")
                           : is_running ? _("Running")
                           : is_waiting ? _("Waiting")
                                        : _("Not running");
    long elapsed_seconds = -1;
    if (route->EndTime().IsValid() && configuration.StartTime.IsValid()) {
      const wxTimeSpan elapsed = route->EndTime() - configuration.StartTime;
      elapsed_seconds = elapsed.GetSeconds().ToLong();
    }
    const double distance_nm = route->RouteInfo(RouteMapOverlay::DISTANCE);
    wxLogMessage(
        "WR_HEADLESS_ROUTE_TEST route_result index=%lu offset=%d "
        "start=\"%s\" end=\"%s\" complete=%d failed=%d running=%d "
        "waiting=%d state=\"%s\" end_time=\"%s\" elapsed=%ld "
        "distance=%.3f reason=\"%s\".",
        static_cast<unsigned long>(i),
        configuration.DepartureTimeOptimizationOffsetMinutes,
        configuration.Start, configuration.End, is_complete ? 1 : 0,
        is_failed ? 1 : 0, is_running ? 1 : 0, is_waiting ? 1 : 0, state,
        route->EndTime().IsValid() ? route->EndTime().FormatISOCombined()
                                   : wxString("invalid"),
        elapsed_seconds, distance_nm, route->GetFailureReason());
  }

  wxLogMessage(
      "WR_HEADLESS_ROUTE_TEST end route=\"%s to %s\" elapsed_ms=%ld "
      "timed_out=%d routes=%lu complete=%d failed=%d running=%d "
      "waiting=%d.",
      selected_config.Start, selected_config.End, elapsed_ms,
      timed_out ? 1 : 0, static_cast<unsigned long>(routes.size()), complete,
      failed, running, waiting);

  const wxString result_status = timed_out      ? _("timeout")
                                 : complete > 0 ? _("complete")
                                 : failed > 0   ? _("failed")
                                                : _("unknown");
  const wxString result_failure =
      timed_out ? _("timeout")
                : (complete > 0 ? wxString() : _("no_completed_routes"));
  if (m_HeadlessRouteTestState->scenarioLoaded &&
      !m_HeadlessRouteTestState->scenarioOutputPath.IsEmpty()) {
    wxString write_error;
    if (!weather_routing_headless::HeadlessRouteRunner::WriteSingleRouteResult(
            m_HeadlessRouteTestState->scenarioOutputPath,
            &m_HeadlessRouteTestState->scenario, result_status,
            result_failure, routes, write_error)) {
      wxLogMessage("WR_HEADLESS_SCENARIO warning output=\"%s\" reason=\"%s\".",
                   m_HeadlessRouteTestState->scenarioOutputPath, write_error);
    }
  }

  if (m_tCompute.IsRunning()) m_tCompute.Stop();
  if (m_tRoutingProgress.IsRunning()) m_tRoutingProgress.Stop();
  if (m_tDeferredRoutingStart.IsRunning()) m_tDeferredRoutingStart.Stop();
  FinishHeadlessRouteTestProcess(timed_out ? 3 : 0);
}

void WeatherRouting::CompleteHeadlessMultiLegTest(bool timed_out,
                                                  long elapsed_ms) {
  if (timed_out) {
    wxLogMessage(
        "WR_HEADLESS_ROUTE_TEST timeout group=%s elapsed_ms=%ld running=%lu "
        "waiting=%lu multileg=%d multileg_opt=%d.",
        m_HeadlessRouteTestState->selectedGroup, elapsed_ms,
        static_cast<unsigned long>(m_RunningRouteMaps.size()),
        static_cast<unsigned long>(m_WaitingRouteMaps.size()),
        m_ActiveMultiLegSequence ? 1 : 0,
        m_ActiveMultiLegDepartureOptimization ? 1 : 0);
    StopAll();
    CancelMultiLegSequence();
    CancelMultiLegDepartureOptimization(false);
  }

  int complete = 0;
  int failed = 0;
  int running = 0;
  int waiting = 0;
  for (size_t i = 0; i < m_MultiLegOptimizationCandidates.size(); ++i) {
    const MultiLegOptimizationCandidate& candidate =
        m_MultiLegOptimizationCandidates[i];
    if (candidate.complete) complete++;
    if (candidate.failed) failed++;
    if (candidate.running) running++;
    if (!candidate.complete && !candidate.failed && !candidate.running)
      waiting++;
    wxLogMessage(
        "WR_HEADLESS_ROUTE_TEST candidate index=%lu offset=%d departure=%s "
        "state=\"%s\" complete=%d failed=%d running=%d legs=%d/%d "
        "final_eta=%s total_elapsed=%ld distance=%.3f reason=\"%s\".",
        static_cast<unsigned long>(i), candidate.offsetMinutes,
        candidate.departureTime.IsValid()
            ? candidate.departureTime.FormatISOCombined()
            : wxString("invalid"),
        candidate.state, candidate.complete ? 1 : 0, candidate.failed ? 1 : 0,
        candidate.running ? 1 : 0, candidate.completedLegs, candidate.totalLegs,
        candidate.finalEta.IsValid() ? candidate.finalEta.FormatISOCombined()
                                     : wxString("invalid"),
        candidate.totalElapsedSeconds, candidate.totalDistance,
        candidate.reason);
  }

  wxLogMessage(
      "WR_HEADLESS_ROUTE_TEST end group=%s elapsed_ms=%ld timed_out=%d "
      "candidates=%lu complete=%d failed=%d running=%d waiting=%d.",
      m_HeadlessRouteTestState->selectedGroup, elapsed_ms, timed_out ? 1 : 0,
      static_cast<unsigned long>(m_MultiLegOptimizationCandidates.size()),
      complete, failed, running, waiting);

  if (m_tCompute.IsRunning()) m_tCompute.Stop();
  if (m_tRoutingProgress.IsRunning()) m_tRoutingProgress.Stop();
  if (m_tDeferredRoutingStart.IsRunning()) m_tDeferredRoutingStart.Stop();
  FinishHeadlessRouteTestProcess(timed_out ? 3 : 0);
}

void WeatherRouting::RunHeadlessRouteTestFromEnv() {
  // A scenario run is an automated test transaction, not an editing session.
  // Configuration mutations below can otherwise arm the normal five-second
  // autosave timer and display a modal error over the running benchmark.
  m_tAutoSaveXML.Stop();
  wxString mode = EnvString("WR_HEADLESS_ROUTE_TEST");
  weather_routing_engine::RoutingScenario scenario;
  wxString scenario_path;
  wxString scenario_output_path;
  wxString scenario_error;
  bool scenario_loaded = false;
  if (!EnvString("WR_HEADLESS_SCENARIO").IsEmpty()) {
    scenario_loaded =
        weather_routing_headless::HeadlessRouteRunner::LoadScenarioFromEnv(
            scenario, scenario_path, scenario_output_path, scenario_error);
    if (!scenario_loaded) {
      wxLogMessage("WR_HEADLESS_SCENARIO abort path=\"%s\" reason=\"%s\".",
                   EnvString("WR_HEADLESS_SCENARIO"), scenario_error);
      FinishHeadlessRouteTestProcess(2);
      return;
    }
    if (mode.IsEmpty() || mode.IsSameAs("scenario", false))
      mode = scenario.departureOptimization.enabled ? _("departure-opt")
                                                    : _("single");
    wxString safety_mode = scenario.safety.mode.Lower();
    if (safety_mode == "none" || safety_mode == "gshhs") {
      wxSetEnv("WR_HEADLESS_CHART_SAFETY_USE", "0");
      wxSetEnv("WR_HEADLESS_CHART_SAFETY_ENFORCE", "0");
    } else if (safety_mode == "chart") {
      wxSetEnv("WR_HEADLESS_CHART_SAFETY_USE", "1");
      wxSetEnv("WR_HEADLESS_CHART_SAFETY_ENFORCE",
               scenario.safety.enforce ? "1" : "0");
    }
    if (scenario.safety.hasPersistentCertifiedCacheEnabled)
      m_weather_routing_pi.SetUsePersistentChartSafeCache(
          scenario.safety.persistentCertifiedCacheEnabled, false);
    wxString write_error;
    if (!weather_routing_headless::HeadlessRouteRunner::WriteStartedResult(
            scenario_output_path, scenario, write_error)) {
      wxLogMessage("WR_HEADLESS_SCENARIO warning output=\"%s\" reason=\"%s\".",
                   scenario_output_path, write_error);
    }
    wxLogMessage(
        "WR_HEADLESS_SCENARIO loaded path=\"%s\" output=\"%s\" name=\"%s\" "
        "mode=%s.",
        scenario_path, scenario_output_path, scenario.name, mode);
  }
  if (mode.IsEmpty()) mode = _("multileg-opt");
  wxString match = EnvString("WR_HEADLESS_MATCH");
  wxString group_filter = EnvString("WR_HEADLESS_GROUP");
  long timeout_ms = EnvLong("WR_HEADLESS_TIMEOUT_MS", 30L * 60L * 1000L);
  if (timeout_ms < 1000) timeout_ms = 1000;

  auto write_scenario_result = [&](const wxString& status,
                                   const wxString& failureReason,
                                   const std::vector<RouteMapOverlay*>&
                                       routes) {
    if (!scenario_loaded || scenario_output_path.IsEmpty()) return;
    wxString write_error;
    if (!weather_routing_headless::HeadlessRouteRunner::WriteSingleRouteResult(
            scenario_output_path, &scenario, status, failureReason, routes,
            write_error)) {
      wxLogMessage("WR_HEADLESS_SCENARIO warning output=\"%s\" reason=\"%s\".",
                   scenario_output_path, write_error);
    }
  };

  wxString gribInitializationError;
  if (!InitializeHeadlessGribFromEnv(&gribInitializationError)) {
    wxLogMessage("WR_HEADLESS_GRIB failed reason=\"%s\"",
                 gribInitializationError);
    write_scenario_result("failed", gribInitializationError, {});
    FinishHeadlessRouteTestProcess(2);
    return;
  }

  auto apply_reverse_reachability_options =
      [&](RouteMapConfiguration& configuration) {
        if (scenario_loaded) {
          configuration.UseReverseReachabilityRecovery =
              scenario.reverseReachability.enabled;
          if (scenario.reverseReachability.hasTargetTime)
            configuration.PlannedArrivalTime =
                scenario.reverseReachability.targetTime;
          if (scenario.reverseReachability.hasSearchBackIsochrones)
            configuration.ReverseReachabilitySearchBackIsochrones =
                wxMax(1, scenario.reverseReachability.searchBackIsochrones);
          if (scenario.reverseReachability.hasHorizonHours)
            configuration.ReverseReachabilityHorizonHours =
                wxMax(0.0, scenario.reverseReachability.horizonHours);
          if (scenario.reverseReachability.hasDiagnostics)
            configuration.ReverseReachabilityDiagnostics =
                scenario.reverseReachability.diagnostics;
        }
        wxString env_reverse = EnvString("WR_HEADLESS_REVERSE_REACHABILITY");
        if (!env_reverse.IsEmpty()) {
          configuration.UseReverseReachabilityRecovery =
              env_reverse.IsSameAs("1") || env_reverse.IsSameAs("true", false);
        }
        wxString env_reverse_back =
            EnvString("WR_HEADLESS_REVERSE_SEARCH_BACK_ISOCHRONES");
        if (!env_reverse_back.IsEmpty()) {
          long value = 0;
          if (env_reverse_back.ToLong(&value))
            configuration.ReverseReachabilitySearchBackIsochrones =
                wxMax(1L, value);
        }
        wxString env_reverse_diag =
            EnvString("WR_HEADLESS_REVERSE_DIAGNOSTICS");
        if (!env_reverse_diag.IsEmpty()) {
          configuration.ReverseReachabilityDiagnostics =
              env_reverse_diag.IsSameAs("1") ||
              env_reverse_diag.IsSameAs("true", false);
        }
      };
  auto apply_arrival_planning_options =
      [&](RouteMapConfiguration& configuration) {
        wxString plannedArrival =
            EnvString("WR_HEADLESS_PLANNED_ARRIVAL_TIME");
        if (plannedArrival.IsEmpty()) return false;

        wxString normalized = plannedArrival;
        const bool explicitUtc = normalized.EndsWith("Z");
        if (explicitUtc) normalized.RemoveLast();
        wxDateTime parsed;
        if (!parsed.ParseISOCombined(normalized, 'T')) {
          wxLogMessage(
              "WR_HEADLESS_ROUTE_TEST warning invalid "
              "WR_HEADLESS_PLANNED_ARRIVAL_TIME=\"%s\".",
              plannedArrival);
          return false;
        }
        if (explicitUtc) parsed.MakeFromTimezone(wxDateTime::UTC);
        configuration.TimeMode =
            RouteMapConfiguration::ROUTE_BY_ARRIVAL_TIME;
        configuration.PlannedArrivalTime = parsed;
        configuration.ArrivalSearchHorizonMinutes =
            wxMax(60L, EnvLong("WR_HEADLESS_ARRIVAL_HORIZON_MINUTES",
                               configuration.ArrivalSearchHorizonMinutes));
        configuration.ArrivalSafetyMarginMinutes =
            wxMax(0L, EnvLong("WR_HEADLESS_ARRIVAL_SAFETY_MARGIN_MINUTES",
                              configuration.ArrivalSafetyMarginMinutes));
        configuration.DepartureTimeOptimizationEnabled = false;
        configuration.UseCurrentTime = false;
        return true;
      };

  bool use_chart_safety = false;
  bool enforce_chart_safety = false;
  ReadExperimentalChartSafetySettings(use_chart_safety, enforce_chart_safety);

  wxLogMessage(
      "WR_HEADLESS_ROUTE_TEST start mode=%s match=\"%s\" group=\"%s\" "
      "timeout_ms=%ld routes=%lu chart_safety{use=%d enforce=%d}.",
      mode, match, group_filter, timeout_ms,
      static_cast<unsigned long>(m_WeatherRoutes.size()),
      use_chart_safety ? 1 : 0, enforce_chart_safety ? 1 : 0);

  if (mode.IsSameAs("single", false) || mode.IsSameAs("route", false) ||
      mode.IsSameAs("single-opt", false) ||
      mode.IsSameAs("departure-opt", false)) {
    RouteMapOverlay* selected_route = NULL;
    if (!scenario_loaded) {
      for (auto weatherroute : m_WeatherRoutes) {
        if (!weatherroute || !weatherroute->routemapoverlay) continue;
        RouteMapConfiguration configuration =
            weatherroute->routemapoverlay->GetConfiguration();
        if (configuration.IsMultiLegGenerated ||
            configuration.DepartureTimeOptimizationCandidate)
          continue;
        wxString haystack =
            wxString::Format(_("%s %s %s"), configuration.RouteGUID,
                             configuration.Start, configuration.End);
        bool text_matches = TextMatchesFilter(haystack, match);
        wxLogMessage(
            "WR_HEADLESS_ROUTE_TEST candidate_route start=\"%s\" end=\"%s\" "
            "guid=\"%s\" text_matches=%d departure_opt=%d.",
            configuration.Start, configuration.End, configuration.RouteGUID,
            text_matches ? 1 : 0,
            configuration.DepartureTimeOptimizationEnabled ? 1 : 0);
        if (text_matches) {
          selected_route = weatherroute->routemapoverlay;
          break;
        }
      }
    }

    if (!selected_route) {
      double start_lat = scenario_loaded
                             ? scenario.start.lat
                             : EnvDouble("WR_HEADLESS_START_LAT", NAN);
      double start_lon = scenario_loaded
                             ? scenario.start.lon
                             : EnvDouble("WR_HEADLESS_START_LON", NAN);
      double end_lat = scenario_loaded ? scenario.end.lat
                                       : EnvDouble("WR_HEADLESS_END_LAT", NAN);
      double end_lon = scenario_loaded ? scenario.end.lon
                                       : EnvDouble("WR_HEADLESS_END_LON", NAN);
      if (!std::isnan(start_lat) && !std::isnan(start_lon) &&
          !std::isnan(end_lat) && !std::isnan(end_lon)) {
        RouteMapConfiguration configuration = DefaultConfiguration();
        if (!scenario_loaded && !m_WeatherRoutes.empty() &&
            m_WeatherRoutes.front() &&
            m_WeatherRoutes.front()->routemapoverlay) {
          configuration =
              m_WeatherRoutes.front()->routemapoverlay->GetConfiguration();
        }
        if (scenario_loaded) {
          // Headless scenario files are self-contained.  Inheriting an
          // unrelated saved GUI route made GRIB-only benchmarks silently
          // initialize climatology (and any other stale route settings).
          configuration.ClimatologyType = RouteMapConfiguration::DISABLED;
          configuration.AllowDataDeficient = false;
          configuration.AvoidCycloneTracks = false;
        }
        configuration.RouteGUID.Clear();
        configuration.StartType = RouteMapConfiguration::START_FROM_POSITION;
        configuration.EndType = RouteMapConfiguration::END_AT_POSITION;
        configuration.Start =
            scenario_loaded ? scenario.start.name
                            : (EnvString("WR_HEADLESS_START_NAME").IsEmpty()
                                   ? _("Headless start")
                                   : EnvString("WR_HEADLESS_START_NAME"));
        configuration.End = scenario_loaded
                                ? scenario.end.name
                                : (EnvString("WR_HEADLESS_END_NAME").IsEmpty()
                                       ? _("Headless end")
                                       : EnvString("WR_HEADLESS_END_NAME"));
        configuration.StartGUID.Clear();
        configuration.EndGUID.Clear();
        configuration.StartLat = start_lat;
        configuration.StartLon = start_lon;
        configuration.EndLat = end_lat;
        configuration.EndLon = end_lon;
        ApplyHeadlessRouteSafetyOverrides(configuration, _("created_route"));
        apply_reverse_reachability_options(configuration);
        if (scenario_loaded && scenario.startTime.IsValid()) {
          configuration.StartTime = scenario.startTime;
          configuration.UseCurrentTime = false;
        }
        wxString headless_start_time = EnvString("WR_HEADLESS_START_TIME");
        if (!scenario_loaded && !headless_start_time.IsEmpty()) {
          wxDateTime parsed_start_time;
          if (parsed_start_time.ParseISOCombined(headless_start_time, 'T')) {
            configuration.StartTime = parsed_start_time;
          } else {
            wxLogMessage(
                "WR_HEADLESS_ROUTE_TEST warning invalid WR_HEADLESS_START_TIME="
                "\"%s\"; using configuration start time \"%s\".",
                headless_start_time,
                configuration.StartTime.IsValid()
                    ? configuration.StartTime.FormatISOCombined()
                    : _("invalid"));
          }
        }
        EnsureRuntimePosition(configuration.Start, start_lat, start_lon);
        EnsureRuntimePosition(configuration.End, end_lat, end_lon);
        configuration.IsMultiLegGenerated = false;
        configuration.MultiLegGroupId.Clear();
        configuration.MultiLegParentRouteGUID.Clear();
        configuration.MultiLegParentRouteName.Clear();
        configuration.MultiLegLegIndex = 0;
        configuration.MultiLegLegCount = 0;
        configuration.DepartureTimeOptimizationCandidate = false;
        configuration.DepartureTimeOptimizationGroupId.Clear();
        configuration.DepartureTimeOptimizationEnabled =
            scenario_loaded ? scenario.departureOptimization.enabled
                            : (mode.IsSameAs("single-opt", false) ||
                               mode.IsSameAs("departure-opt", false));
        if (scenario_loaded) {
          configuration.DepartureTimeOptimizationRangeMinutes =
              wxMax(scenario.departureOptimization.beforeMinutes,
                    scenario.departureOptimization.afterMinutes);
          configuration.DepartureTimeOptimizationStepMinutes =
              scenario.departureOptimization.stepMinutes;
          configuration.DepartureTimeOptimizationConcurrentRoutes =
              scenario.departureOptimization.concurrentRoutes;
        } else {
          configuration.DepartureTimeOptimizationRangeMinutes =
              EnvLong("WR_HEADLESS_OPT_RANGE_MIN", 240);
          configuration.DepartureTimeOptimizationStepMinutes =
              EnvLong("WR_HEADLESS_OPT_STEP_MIN", 60);
          configuration.DepartureTimeOptimizationConcurrentRoutes = std::max(
              0L, std::min(
                      static_cast<long>(
                          weather_routing::
                              kMaximumParallelDepartureCandidates),
                      EnvLong("WR_HEADLESS_OPT_CONCURRENT_ROUTES", 0)));
        }
        if (scenario_loaded && scenario.environment.hasUseCurrents)
          configuration.Currents = scenario.environment.useCurrents;
        if (scenario_loaded) {
          if (scenario.environment.hasUseGrib)
            configuration.UseGrib = scenario.environment.useGrib;
          if (scenario.environment.hasAllowClimatologyFallback) {
            configuration.ClimatologyType =
                scenario.environment.allowClimatologyFallback
                    ? RouteMapConfiguration::MOST_LIKELY
                    : RouteMapConfiguration::DISABLED;
          }
          if (scenario.route.hasBoatFile) {
            wxString boat_file = scenario.route.boatFile;
            if (boat_file.StartsWith("~/"))
              boat_file = wxFileName::GetHomeDir() + boat_file.Mid(1);
            configuration.boatFileName = boat_file;
          }
          if (scenario.route.hasRoutingEffortPercent)
            configuration.RoutingEffortPercent =
                weather_routing::NormalizeRoutingEffortPercent(
                    scenario.route.routingEffortPercent);
          if (scenario.route.hasTimeStepSeconds)
            configuration.DeltaTime = wxMax(1, scenario.route.timeStepSeconds);
          if (scenario.route.hasHeadingFromDegrees)
            configuration.FromDegree = scenario.route.headingFromDegrees;
          if (scenario.route.hasHeadingToDegrees)
            configuration.ToDegree = scenario.route.headingToDegrees;
          if (scenario.route.hasHeadingStepDegrees)
            configuration.ByDegrees =
                wxMax(0.1, scenario.route.headingStepDegrees);
          if (scenario.route.hasMaxDivertedCourseDegrees)
            configuration.MaxDivertedCourse =
                scenario.route.maxDivertedCourseDegrees;
          if (scenario.route.hasMaxCourseAngleDegrees)
            configuration.MaxCourseAngle =
                scenario.route.maxCourseAngleDegrees;
          if (scenario.route.hasMaxSearchAngleDegrees)
            configuration.MaxSearchAngle =
                scenario.route.maxSearchAngleDegrees;
          if (scenario.route.hasMaxTrueWindKnots)
            configuration.MaxTrueWindKnots = scenario.route.maxTrueWindKnots;
          if (scenario.route.hasMaxApparentWindKnots)
            configuration.MaxApparentWindKnots =
                scenario.route.maxApparentWindKnots;
          if (scenario.route.hasOptimizeTacking)
            configuration.OptimizeTacking = scenario.route.optimizeTacking;
          if (scenario.route.hasUpwindEfficiency)
            configuration.UpwindEfficiency = scenario.route.upwindEfficiency;
          if (scenario.route.hasDownwindEfficiency)
            configuration.DownwindEfficiency =
                scenario.route.downwindEfficiency;
          if (scenario.route.hasNightEfficiency)
            configuration.NightCumulativeEfficiency =
                scenario.route.nightEfficiency;
          if (scenario.route.hasUseMotor)
            configuration.UseMotor = scenario.route.useMotor;
          if (scenario.route.hasMotorSpeedThresholdKnots)
            configuration.MotorSpeedThreshold =
                scenario.route.motorSpeedThresholdKnots;
          if (scenario.route.hasMotorSpeedKnots)
            configuration.MotorSpeed = scenario.route.motorSpeedKnots;
          wxString safety_mode = scenario.safety.mode.Lower();
          if (safety_mode == "none") {
            configuration.DetectLand = false;
            wxSetEnv("WR_HEADLESS_CHART_SAFETY_USE", "0");
            wxSetEnv("WR_HEADLESS_CHART_SAFETY_ENFORCE", "0");
          } else if (safety_mode == "gshhs") {
            configuration.DetectLand = true;
            wxSetEnv("WR_HEADLESS_CHART_SAFETY_USE", "0");
            wxSetEnv("WR_HEADLESS_CHART_SAFETY_ENFORCE", "0");
          } else if (safety_mode == "chart") {
            configuration.DetectLand = true;
            wxSetEnv("WR_HEADLESS_CHART_SAFETY_USE", "1");
            wxSetEnv("WR_HEADLESS_CHART_SAFETY_ENFORCE",
                     scenario.safety.enforce ? "1" : "0");
          }
          if (scenario.safety.hasLandMarginNm)
            configuration.SafetyMarginLand =
                wxMax(0.0, scenario.safety.landMarginNm);
          if (scenario.safety.hasMinimumDepthM)
            configuration.MinimumDepthMeters =
                wxMax(0.0, scenario.safety.minimumDepthM);
          if (scenario.safety.hasPersistentCertifiedCacheEnabled)
            m_weather_routing_pi.SetUsePersistentChartSafeCache(
                scenario.safety.persistentCertifiedCacheEnabled, false);
        }
        apply_arrival_planning_options(configuration);
        if (!AddConfiguration(configuration)) {
          wxLogMessage(
              "WR_HEADLESS_ROUTE_TEST abort reason=create_route_failed "
              "start=(%.6f,%.6f) end=(%.6f,%.6f).",
              start_lat, start_lon, end_lat, end_lon);
          write_scenario_result("failed", "create_route_failed", {});
          FinishHeadlessRouteTestProcess(2);
          return;
        }
        selected_route = m_WeatherRoutes.back()->routemapoverlay;
        selected_route->LoadBoat();
        wxLogMessage(
            "WR_HEADLESS_ROUTE_TEST created_route start=\"%s\" end=\"%s\" "
            "start=(%.6f,%.6f) end=(%.6f,%.6f) departure_opt=%d.",
            configuration.Start, configuration.End, start_lat, start_lon,
            end_lat, end_lon,
            configuration.DepartureTimeOptimizationEnabled ? 1 : 0);
      } else {
        wxLogMessage(
            "WR_HEADLESS_ROUTE_TEST abort reason=no_matching_route "
            "match=\"%s\". Provide WR_HEADLESS_START_LAT/LON and "
            "WR_HEADLESS_END_LAT/LON to create a runtime route.",
            match);
        write_scenario_result("failed", "no_matching_route", {});
        FinishHeadlessRouteTestProcess(2);
        return;
      }
    }

    RouteMapConfiguration selected_config = selected_route->GetConfiguration();
    bool selected_config_changed = false;
    double original_safety_margin = selected_config.SafetyMarginLand;
    ApplyHeadlessRouteSafetyOverrides(selected_config, _("selected_route"));
    apply_reverse_reachability_options(selected_config);
    if (apply_arrival_planning_options(selected_config))
      selected_config_changed = true;
    if (selected_config.SafetyMarginLand != original_safety_margin)
      selected_config_changed = true;
    wxString headless_start_time = EnvString("WR_HEADLESS_START_TIME");
    if (!headless_start_time.IsEmpty()) {
      wxDateTime parsed_start_time;
      if (parsed_start_time.ParseISOCombined(headless_start_time, 'T')) {
        selected_config.StartTime = parsed_start_time;
        selected_config.UseCurrentTime = false;
        selected_config_changed = true;
      } else {
        wxLogMessage(
            "WR_HEADLESS_ROUTE_TEST warning invalid WR_HEADLESS_START_TIME="
            "\"%s\" for selected route; keeping \"%s\".",
            headless_start_time,
            selected_config.StartTime.IsValid()
                ? selected_config.StartTime.FormatISOCombined()
                : _("invalid"));
      }
    }
    if (scenario_loaded && scenario.startTime.IsValid()) {
      selected_config.StartTime = scenario.startTime;
      selected_config.UseCurrentTime = false;
      selected_config_changed = true;
    }
    if (mode.IsSameAs("single-opt", false) ||
        mode.IsSameAs("departure-opt", false)) {
      selected_config.DepartureTimeOptimizationEnabled = true;
      selected_config.DepartureTimeOptimizationRangeMinutes =
          EnvLong("WR_HEADLESS_OPT_RANGE_MIN",
                  selected_config.DepartureTimeOptimizationRangeMinutes);
      selected_config.DepartureTimeOptimizationStepMinutes =
          EnvLong("WR_HEADLESS_OPT_STEP_MIN",
                  selected_config.DepartureTimeOptimizationStepMinutes);
      selected_config_changed = true;
    }
    wxString departure_opt_override = EnvString("WR_HEADLESS_DEPARTURE_OPT");
    if (!departure_opt_override.IsEmpty()) {
      selected_config.DepartureTimeOptimizationEnabled =
          departure_opt_override.IsSameAs("1") ||
          departure_opt_override.IsSameAs("true", false);
      selected_config_changed = true;
      wxLogMessage(
          "WR_HEADLESS_ROUTE_TEST override context=selected_route "
          "DepartureTimeOptimizationEnabled=%d.",
          selected_config.DepartureTimeOptimizationEnabled ? 1 : 0);
    }
    if (selected_config_changed) {
      wxLogMessage(
          "WR_HEADLESS_ROUTE_TEST setup_step=set_selected_configuration "
          "phase=begin");
      selected_route->SetConfiguration(selected_config);
      wxLogMessage(
          "WR_HEADLESS_ROUTE_TEST setup_step=set_selected_configuration "
          "phase=end");
    }
    bool departure_opt =
        selected_config.TimeMode !=
            RouteMapConfiguration::ROUTE_BY_ARRIVAL_TIME &&
        (mode.IsSameAs("single-opt", false) ||
         mode.IsSameAs("departure-opt", false) ||
         selected_config.DepartureTimeOptimizationEnabled);
    bool started = false;
    if (departure_opt) {
      started = ComputeDepartureTimeOptimization(selected_route);
    } else {
      selected_config.ChartSafetyPropagationFallbackTried = false;
      ApplyAuthoritativeChartSearchPolicy(selected_config);
      selected_config.chart_safety_missing_tile_retry_count = 0;
      selected_config.chart_safety_missing_tile_rejections = 0;
      selected_config.chart_safety_missing_tile_first_lat_tile = 0;
      selected_config.chart_safety_missing_tile_first_lon_tile = 0;
      selected_config.chart_safety_missing_tile_first_min_lat = NAN;
      selected_config.chart_safety_missing_tile_first_min_lon = NAN;
      selected_config.chart_safety_missing_tile_min_lat = NAN;
      selected_config.chart_safety_missing_tile_max_lat = NAN;
      selected_config.chart_safety_missing_tile_min_lon = NAN;
      selected_config.chart_safety_missing_tile_max_lon = NAN;
      wxLogMessage(
          "WR_HEADLESS_ROUTE_TEST setup_step=set_propagation_configuration "
          "phase=begin");
      selected_route->SetConfiguration(selected_config);
      wxLogMessage(
          "WR_HEADLESS_ROUTE_TEST setup_step=set_propagation_configuration "
          "phase=end");
      wxLogMessage(
          "WR_HEADLESS_ROUTE_TEST setup_step=start_route phase=begin");
      Start(selected_route);
      wxLogMessage(
          "WR_HEADLESS_ROUTE_TEST setup_step=start_route phase=end");
      started = true;
    }

    if (!started) {
      wxLogMessage(
          "WR_HEADLESS_ROUTE_TEST abort route=\"%s to %s\" "
          "reason=start_failed.",
          selected_config.Start, selected_config.End);
      write_scenario_result("failed", "start_failed", {selected_route});
      FinishHeadlessRouteTestProcess(2);
      return;
    }

    UpdateComputeState();
    m_HeadlessRouteTestState = std::make_unique<HeadlessRouteTestState>();
    m_HeadlessRouteTestState->kind =
        HeadlessRouteTestState::Kind::SingleRoute;
    m_HeadlessRouteTestState->timeoutMs = timeout_ms;
    m_HeadlessRouteTestState->startedMs = wxGetUTCTimeMillis();
    m_HeadlessRouteTestState->selectedRoute = selected_route;
    m_HeadlessRouteTestState->departureOptimization = departure_opt;
    m_HeadlessRouteTestState->scenarioLoaded = scenario_loaded;
    m_HeadlessRouteTestState->scenario = scenario;
    m_HeadlessRouteTestState->scenarioOutputPath = scenario_output_path;
    m_tHeadlessRouteTest.Start(50);
    wxLogMessage(
        "WR_HEADLESS_ROUTE_TEST monitor_started kind=single interval_ms=50 "
        "timeout_ms=%ld.",
        timeout_ms);
    return;
  }

  wxString selected_group;
  std::vector<wxString> seen_groups;
  for (auto weatherroute : m_WeatherRoutes) {
    if (!weatherroute || !weatherroute->routemapoverlay) continue;
    RouteMapConfiguration configuration =
        weatherroute->routemapoverlay->GetConfiguration();
    if (!configuration.IsMultiLegGenerated ||
        configuration.MultiLegGroupId.IsEmpty())
      continue;

    bool seen = false;
    for (const auto& group : seen_groups) {
      if (group == configuration.MultiLegGroupId) {
        seen = true;
        break;
      }
    }
    if (seen) continue;
    seen_groups.push_back(configuration.MultiLegGroupId);

    wxString haystack =
        wxString::Format(_("%s %s %s %s"), configuration.MultiLegGroupId,
                         configuration.MultiLegParentRouteName,
                         configuration.Start, configuration.End);
    bool group_matches =
        group_filter.IsEmpty() || configuration.MultiLegGroupId == group_filter;
    bool text_matches = TextMatchesFilter(haystack, match);
    std::vector<RouteMapOverlay*> group_routes =
        GetMultiLegGroupRoutes(configuration.MultiLegGroupId);
    wxLogMessage(
        "WR_HEADLESS_ROUTE_TEST candidate_group group=%s parent=\"%s\" "
        "legs_found=%lu expected=%d start=\"%s\" end=\"%s\" "
        "group_matches=%d text_matches=%d.",
        configuration.MultiLegGroupId, configuration.MultiLegParentRouteName,
        static_cast<unsigned long>(group_routes.size()),
        configuration.MultiLegLegCount, configuration.Start, configuration.End,
        group_matches ? 1 : 0, text_matches ? 1 : 0);

    if (group_matches && text_matches && !group_routes.empty() &&
        (int)group_routes.size() == configuration.MultiLegLegCount) {
      selected_group = configuration.MultiLegGroupId;
      break;
    }
  }

  if (selected_group.IsEmpty()) {
    wxLogMessage(
        "WR_HEADLESS_ROUTE_TEST abort reason=no_matching_multileg_group "
        "groups_seen=%lu.",
        static_cast<unsigned long>(seen_groups.size()));
    FinishHeadlessRouteTestProcess(2);
    return;
  }

  bool started = false;
  if (mode.IsSameAs("multileg", false) || mode.IsSameAs("sequence", false))
    started = ComputeMultiLegSequenceNow(selected_group);
  else
    started = ComputeMultiLegDepartureOptimizationNow(selected_group);

  if (!started) {
    wxLogMessage("WR_HEADLESS_ROUTE_TEST abort group=%s reason=start_failed.",
                 selected_group);
    FinishHeadlessRouteTestProcess(2);
    return;
  }

  m_HeadlessRouteTestState = std::make_unique<HeadlessRouteTestState>();
  m_HeadlessRouteTestState->kind = HeadlessRouteTestState::Kind::MultiLeg;
  m_HeadlessRouteTestState->timeoutMs = timeout_ms;
  m_HeadlessRouteTestState->startedMs = wxGetUTCTimeMillis();
  m_HeadlessRouteTestState->selectedGroup = selected_group;
  m_tHeadlessRouteTest.Start(50);
  wxLogMessage(
      "WR_HEADLESS_ROUTE_TEST monitor_started kind=multileg interval_ms=50 "
      "timeout_ms=%ld.",
      timeout_ms);
}

void WeatherRouting::CursorRouteChanged() {
  if (m_PlotDialog.IsShown() && m_PlotDialog.m_rbCursorRoute->GetValue())
    m_PlotDialog.SetRouteMapOverlay(FirstCurrentRouteMap());
}

void WeatherRouting::UpdateColumns() {
  m_panel->m_lWeatherRoutes->DeleteAllColumns();

  for (int i = 0; i < NUM_COLS; i++) {
    if (m_SettingsDialog.m_cblFields->IsChecked(i)) {
      columns[i] = m_panel->m_lWeatherRoutes->GetColumnCount();
      wxString name = _(column_names[i]);

      if (i == STARTTIME || i == ENDTIME) {
        name += _T(" (") +
                (m_SettingsDialog.UseLocalTimeZone()
                     ? m_SettingsDialog.DisplayTimeZone()
                     : wxString("UTC")) +
                _T(")");
      } else if (i == DISTANCE) {
        name += _T(" (") + getUsrDistanceUnit_Plugin() + _T(")");
      } else if (i == AVGSPEED || i == MAXSPEED || i == AVGSPEEDGROUND ||
                 i == MAXSPEEDGROUND || i == AVGWIND || i == MAXWIND ||
                 i == MAXWINDGUST || i == AVGCURRENT || i == MAXCURRENT) {
        name += _T(" (") + getUsrSpeedUnit_Plugin() + _T(")");
      } else if (i == AVGSWELL || i == MAXSWELL) {
        name += _T(" (m)");
      }

      m_panel->m_lWeatherRoutes->InsertColumn(columns[i], name);
      m_panel->m_lWeatherRoutes->SetColumnWidth(columns[i], wxLIST_AUTOSIZE);
    } else
      columns[i] = -1;
  }

  std::list<WeatherRoute*>::iterator it = m_WeatherRoutes.begin();
  for (int i = 0; i < m_panel->m_lWeatherRoutes->GetItemCount(); i++, it++) {
    m_panel->m_lWeatherRoutes->SetItemPtrData(i, (wxUIntPtr)*it);
    (*it)->Update(
        this);  // update utc/local switch to strings of start/end time
    UpdateItem(i);
  }

  OnWeatherRouteSelected();  // update utc/local switch if configuration dialog
                             // is visible
}

static void CursorPositionDialogMessage(CursorPositionDialog& dlg,
                                        wxString msg) {
  dlg.m_stPosition->SetLabel(msg);
  dlg.m_stPosition->Fit();
  dlg.m_stTime->SetLabel("");
  dlg.m_stPolar->SetLabel("");
  dlg.m_stSailChanges->SetLabel("");
  dlg.m_stTacks->SetLabel("");
  dlg.m_stJibes->SetLabel("");
  dlg.m_stSailPlanChanges->SetLabel("");
  dlg.m_stWeatherData->SetLabel("");
  dlg.Fit();
}

static void RoutePositionDialogMessage(RoutePositionDialog& dlg, wxString msg) {
  dlg.m_stPosition->SetLabel(msg);
  dlg.m_stPosition->Fit();
  dlg.m_stTime->SetLabel("");
  dlg.m_stPolar->SetLabel("");
  dlg.m_stSailChanges->SetLabel("");
  dlg.m_stTacks->SetLabel("");
  dlg.m_stJibes->SetLabel("");
  dlg.m_stSailPlanChanges->SetLabel("");
  dlg.m_stWeatherData->SetLabel("");
  dlg.Fit();
}

void WeatherRouting::UpdateCursorPositionDialog() {
  CursorPositionDialog& dlg = m_CursorPositionDialog;
  if (!dlg.IsShown()) return;

  std::list<RouteMapOverlay*> currentroutemaps = CurrentRouteMaps();
  if (currentroutemaps.size() != 1) {
    CursorPositionDialogMessage(dlg, _("Select exactly 1 configuration"));
    return;
  }

  RouteMapOverlay* rmo = currentroutemaps.front();
  Position* p = rmo->GetLastCursorPosition();
  if (!p) {
    CursorPositionDialogMessage(dlg, _("Cursor outside computed route map"));
    return;
  }
  dlg.m_stTime->SetLabel(m_SettingsDialog.FormatTime(
      rmo->GetLastCursorTime(), _T("%x %H:%M")));

  RouteMapConfiguration configuration = rmo->GetConfiguration();
  auto latStr = toSDMM_PlugIn(NEflag::LAT, p->lat, Precision::HI);
  auto lonStr =
      toSDMM_PlugIn(NEflag::LON, heading_resolve(p->lon), Precision::HI);
  dlg.m_stPosition->SetLabel(latStr + " " + lonStr);

  if (p->polar == -1)
    dlg.m_stPolar->SetLabel(wxEmptyString);
  else {
    wxFileName fn = configuration.boat.Polars[p->polar].FileName;
    dlg.m_stPolar->SetLabel(fn.GetFullName());
  }

  dlg.m_stSailChanges->SetLabel(wxString::Format(_T("%d"), p->SailChanges()));

  dlg.m_stTacks->SetLabel(wxString::Format(_T("%d"), p->tacks));
  dlg.m_stJibes->SetLabel(wxString::Format(_T("%d"), p->jibes));
  dlg.m_stSailPlanChanges->SetLabel(
      wxString::Format(_T("%d"), p->sail_plan_changes));

  wxString weatherdata;
  wxString grib = _("Grib") + _T(" ");
  wxString climatology = _("Climatology") + _T(" ");
  wxString data_deficient = _("Data Deficient") + _T(" ");
  wxString wind = _("Wind") + _T(" ");
  wxString current = _("Current") + _T(" ");

  if (p->data_mask & Position::GRIB_WIND) weatherdata += grib + wind;
  if (p->data_mask & Position::CLIMATOLOGY_WIND)
    weatherdata += climatology + wind;
  if (p->data_mask & Position::DATA_DEFICIENT_WIND)
    weatherdata += data_deficient + wind;
  if (p->data_mask & Position::GRIB_CURRENT) weatherdata += grib + current;
  if (p->data_mask & Position::CLIMATOLOGY_CURRENT)
    weatherdata += climatology + current;
  if (p->data_mask & Position::DATA_DEFICIENT_CURRENT)
    weatherdata += data_deficient + current;

  dlg.m_stWeatherData->SetLabel(weatherdata);
  dlg.Fit();
}

void WeatherRouting::UpdateRoutePositionDialog() {
  /* New method to display information on the weather route
   * (like time, duration, position, wind, speed, etc.)
   * based on cursor position of the user.
   * This is complementary with the plot chart.
   */

  RoutePositionDialog& dlg = m_RoutePositionDialog;
  if (!dlg.IsShown()) return;

  m_positionOnRoute = nullptr;
  std::list<RouteMapOverlay*> currentroutemaps = CurrentRouteMaps();
  if (currentroutemaps.size() != 1) {
    RoutePositionDialogMessage(dlg, _("Select exactly 1 configuration"));
    return;
  }

  RouteMapOverlay* rmo = currentroutemaps.front();
  RouteMapConfiguration configuration = rmo->GetConfiguration();

  // CUSTOMIZATION
  // -------------------------
  // Get access to the closest point between the cursor location
  // and the weather routing computed point.
  PlotData data;
  Position* closestPosition = rmo->getClosestRoutePositionFromCursor(
      m_weather_routing_pi.m_cursor_lat, m_weather_routing_pi.m_cursor_lon,
      data);

  // Store position to display it
  m_positionOnRoute = closestPosition;
  if (!closestPosition && data.time.IsValid()) {
    m_positionOnRoute = &m_savedPosition;
    m_savedPosition = data;
  }

  if (!m_positionOnRoute) {
    RoutePositionDialogMessage(dlg, _("Cursor outside computed route map"));
    return;
  }

  // TRIP DURATION
  wxDateTime cursorTime = data.time;
  dlg.m_stTime->SetLabel(
      m_SettingsDialog.FormatTime(cursorTime, _T("%x %H:%M")));

  wxString duration = calculateTimeDelta(configuration.StartTime, cursorTime);
  dlg.m_stDuration->SetLabel(duration);

  // POSITION
  auto latStr = toSDMM_PlugIn(NEflag::LAT, data.lat, Precision::HI);
  auto lonStr =
      toSDMM_PlugIn(NEflag::LON, heading_resolve(data.lon), Precision::HI);
  dlg.m_stPosition->SetLabel(latStr + _T(" ") + lonStr);

  // POLAR
  if (data.polar == -1)
    dlg.m_stPolar->SetLabel(wxEmptyString);
  else {
    wxFileName fn = configuration.boat.Polars[data.polar].FileName;
    dlg.m_stPolar->SetLabel(fn.GetFullName());
  }

  // TACKS
  dlg.m_stTacks->SetLabel(wxString::Format(_T("%d"), data.tacks));
  // JIBES
  dlg.m_stJibes->SetLabel(wxString::Format(_T("%d"), data.jibes));

  // BOAT SPEED
  if (std::abs(data.stw - data.sog) > 0.1) {
    dlg.m_stBoatSpeed->SetLabel(wxString::Format(
        _T("%.1f knts (SOW), %.1f knts (SOG)"), data.stw, data.sog));
  } else {
    dlg.m_stBoatSpeed->SetLabel(wxString::Format(_T("%.1f knts"), data.stw));
  }

  // BEARING
  if (std::abs(data.ctw - data.cog) >= 5) {
    dlg.m_stBoatCourse->SetLabel(wxString::Format(
        _T("%.0f \u00B0T (COW), %.0f \u00B0T (COG)"),
        positive_degrees(data.ctw), positive_degrees(data.cog)));
  } else {
    dlg.m_stBoatCourse->SetLabel(
        wxString::Format(_T("%.0f \u00B0T"), positive_degrees(data.ctw)));
  }

  // WIND SPEED
  // RouteInfo(RouteMapOverlay::COMFORT);
  dlg.m_stTWS->SetLabel(wxString::Format(_T("%.0f knts"), data.twsOverWater));

  // WIND: TRUE WIND ANGLE
  // For wind direction, specify if it is
  // coming from starboard or port side.
  double windDirection = heading_resolve(data.ctw - data.twdOverWater);
  wxString windDirectionLabel;
  if (windDirection <= 0)
    windDirectionLabel =
        wxString::Format(_T("%.0f\u00B0 starboard"), fabs(windDirection));
  else
    windDirectionLabel =
        wxString::Format(_T("%.0f\u00B0 port"), fabs(windDirection));
  dlg.m_stTWA->SetLabel(windDirectionLabel);

  // WIND: APPARENT WIND SPEED
  float apparentWindSpeed =
      Polar::VelocityApparentWind(data.stw, windDirection, data.twsOverWater);
  dlg.m_stAWS->SetLabel(wxString::Format(_T("%.0f knts"), apparentWindSpeed));

  // WIND: APPARENT WIND SPEED
  float apparentWindDirection = Polar::DirectionApparentWind(
      apparentWindSpeed, data.stw, windDirection, data.twsOverWater);
  wxString apparentWindDirectionLabel;
  if (apparentWindDirection <= 0)
    apparentWindDirectionLabel = wxString::Format(_T("%.0f\u00B0 starboard"),
                                                  fabs(apparentWindDirection));
  else
    apparentWindDirectionLabel =
        wxString::Format(_T("%.0f\u00B0 port"), fabs(apparentWindDirection));
  dlg.m_stAWA->SetLabel(apparentWindDirectionLabel);

  // WAVES
  dlg.m_stWaves->SetLabel(wxString::Format(_T("%.0f m"), data.WVHT));

  // WIND GUST
  dlg.m_stWindGust->SetLabel(wxString::Format(_T("%.0f knts"), data.VW_GUST));

  // CLIMATOLOGY DATA
  wxString weatherdata;
  wxString grib = _("Grib") + _T(" ");
  wxString climatology = _("Climatology") + _T(" ");
  wxString data_deficient = _("Data Deficient") + _T(" ");
  wxString wind = _("Wind") + _T(" ");
  wxString current = _("Current") + _T(" ");
  if (closestPosition) {
    // SAIL CHANGES
    dlg.m_stSailChanges->SetLabel(
        wxString::Format(_T("%d"), closestPosition->SailChanges()));

    if (closestPosition->data_mask & Position::GRIB_WIND)
      weatherdata += grib + wind;
    if (closestPosition->data_mask & Position::CLIMATOLOGY_WIND)
      weatherdata += climatology + wind;
    if (closestPosition->data_mask & Position::DATA_DEFICIENT_WIND)
      weatherdata += data_deficient + wind;
    if (closestPosition->data_mask & Position::GRIB_CURRENT)
      weatherdata += grib + current;
    if (closestPosition->data_mask & Position::CLIMATOLOGY_CURRENT)
      weatherdata += climatology + current;
    if (closestPosition->data_mask & Position::DATA_DEFICIENT_CURRENT)
      weatherdata += data_deficient + current;

    dlg.m_stWeatherData->SetLabel(weatherdata);
  }
  dlg.Fit();
}

void WeatherRouting::OnNewPosition(wxCommandEvent& event) {
  NewPositionDialog dlg(this);
  if (dlg.ShowModal() == wxID_OK) {
    double lat = 0, lon = 0, lat_minutes = 0, lon_minutes = 0;

    wxString latitude_degrees = dlg.m_tLatitudeDegrees->GetValue();
    wxString latitude_minutes = dlg.m_tLatitudeMinutes->GetValue();
    latitude_degrees.ToDouble(&lat);
    latitude_minutes.ToDouble(&lat_minutes);
    lat_minutes = fabs(lat_minutes);
    if (lat < 0) lat_minutes = -lat_minutes;
    lat += lat_minutes / 60;

    wxString longitude_degrees = dlg.m_tLongitudeDegrees->GetValue();
    wxString longitude_minutes = dlg.m_tLongitudeMinutes->GetValue();
    longitude_degrees.ToDouble(&lon);
    longitude_minutes.ToDouble(&lon_minutes);
    lon_minutes = fabs(lon_minutes);
    if (lon < 0) lon_minutes = -lon_minutes;
    lon += lon_minutes / 60;

    AddPosition(lat, lon, dlg.m_tName->GetValue());
  }
}

void WeatherRouting::OnUpdateBoat(wxCommandEvent& event) {
  double lat = m_weather_routing_pi.m_boat_lat;
  double lon = m_weather_routing_pi.m_boat_lon;

  long index = 0;
  for (std::list<RouteMapPosition>::iterator it = RouteMap::Positions.begin();
       it != RouteMap::Positions.end(); it++, index++)
    if ((*it).Name == _("Boat")) {
      m_panel->m_lPositions->SetItem(
          index, POSITION_LAT, toSDMM_PlugIn(NEflag::LAT, lat, Precision::HI));
      m_panel->m_lPositions->SetItem(
          index, POSITION_LON, toSDMM_PlugIn(NEflag::LON, lon, Precision::HI));

      (*it).lat = lat, (*it).lon = lon;
      UpdateConfigurations();
      return;
    }

  AddPosition(lat, lon, _("Boat"));
}

#if 0 /* wx widgets is shit, can only allow users \
         to edit the first column, so this doesn't work */
void WeatherRouting::OnListLabelEdit( wxListEvent& event )
{
    long index = event.GetIndex();
    int col = event.GetColumn();

    long i = 0;
    for(std::list<RouteMapPosition>::iterator it = RouteMap::Positions.begin();
        it != RouteMap::Positions.end(); it++, i++)
        if(i == index) {
            if(col == POSITION_NAME) {
                (*it).Name = event.GetText();
            } else {
                double value;
                event.GetText().ToDouble(&value);
                if(col == POSITION_LAT)
                    (*it).lat = value;
                else if(col == POSITION_LON)
                    (*it).lon = value;

                m_lPositions->SetItem(index, col, wxString::Format(_T("%.5f"), value));
                UpdateConfigurations();
            }
        }
}
#endif

void WeatherRouting::OnEditPosition() {
  const long index = m_panel->m_lPositions->GetNextItem(-1, wxLIST_NEXT_ALL,
                                                        wxLIST_STATE_SELECTED);
  if (index < 0) return;

  const long id = m_panel->m_lPositions->GetItemData(index);
  auto selected = RouteMap::Positions.end();
  for (auto it = RouteMap::Positions.begin(); it != RouteMap::Positions.end();
       ++it) {
    if (it->ID == id) {
      selected = it;
      break;
    }
  }
  if (selected == RouteMap::Positions.end()) return;
  if (!selected->GUID.IsEmpty()) {
    wxMessageBox(_("This position is linked to an OpenCPN waypoint. Edit the "
                   "waypoint in OpenCPN; Weather Routing will follow it."),
                 _("Weather Routing"), wxOK | wxICON_INFORMATION, this);
    return;
  }

  NewPositionDialog dlg(this);
  dlg.m_tName->SetValue(selected->Name);
  double degrees = 0.0;
  double minutes = std::modf(selected->lat, &degrees);
  dlg.m_tLatitudeDegrees->SetValue(wxString::Format("%.0f", degrees));
  dlg.m_tLatitudeMinutes->SetValue(
      wxString::Format("%.4f", std::fabs(minutes * 60.0)));
  minutes = std::modf(selected->lon, &degrees);
  dlg.m_tLongitudeDegrees->SetValue(wxString::Format("%.0f", degrees));
  dlg.m_tLongitudeMinutes->SetValue(
      wxString::Format("%.4f", std::fabs(minutes * 60.0)));
  if (dlg.ShowModal() != wxID_OK) return;

  double latDegrees = 0.0, latMinutes = 0.0;
  double lonDegrees = 0.0, lonMinutes = 0.0;
  if (!dlg.m_tLatitudeDegrees->GetValue().ToDouble(&latDegrees) ||
      !dlg.m_tLatitudeMinutes->GetValue().ToDouble(&latMinutes) ||
      !dlg.m_tLongitudeDegrees->GetValue().ToDouble(&lonDegrees) ||
      !dlg.m_tLongitudeMinutes->GetValue().ToDouble(&lonMinutes)) {
    wxMessageBox(_("Latitude and longitude must be numeric."),
                 _("Weather Routing"), wxOK | wxICON_ERROR, this);
    return;
  }
  const bool latNegative =
      latDegrees < 0 || dlg.m_tLatitudeDegrees->GetValue().StartsWith("-");
  const bool lonNegative =
      lonDegrees < 0 || dlg.m_tLongitudeDegrees->GetValue().StartsWith("-");
  latMinutes = std::fabs(latMinutes) * (latNegative ? -1.0 : 1.0);
  lonMinutes = std::fabs(lonMinutes) * (lonNegative ? -1.0 : 1.0);
  const double lat = latDegrees + latMinutes / 60.0;
  const double lon = lonDegrees + lonMinutes / 60.0;
  wxString name = dlg.m_tName->GetValue();
  name.Trim(true).Trim(false);
  if (name.IsEmpty() || lat < -90.0 || lat > 90.0 || lon < -180.0 ||
      lon > 180.0 || std::fabs(latMinutes) >= 60.0 ||
      std::fabs(lonMinutes) >= 60.0) {
    wxMessageBox(_("Enter a name and valid latitude/longitude."),
                 _("Weather Routing"), wxOK | wxICON_ERROR, this);
    return;
  }
  for (const auto& position : RouteMap::Positions) {
    if (position.ID != id && position.GUID.IsEmpty() && position.Name == name) {
      wxMessageBox(_("Another manual position already has this name."),
                   _("Weather Routing"), wxOK | wxICON_ERROR, this);
      return;
    }
  }

  const wxString oldName = selected->Name;
  selected->Name = name;
  selected->lat = lat;
  selected->lon = lon;
  m_panel->m_lPositions->SetItem(index, POSITION_NAME, name);
  m_panel->m_lPositions->SetItem(
      index, POSITION_LAT, toSDMM_PlugIn(NEflag::LAT, lat, Precision::HI));
  m_panel->m_lPositions->SetItem(
      index, POSITION_LON, toSDMM_PlugIn(NEflag::LON, lon, Precision::HI));
  if (oldName != name) {
    for (int row = 0; row < m_panel->m_lWeatherRoutes->GetItemCount(); ++row) {
      WeatherRoute* route = reinterpret_cast<WeatherRoute*>(
          wxUIntToPtr(m_panel->m_lWeatherRoutes->GetItemData(row)));
      RouteMapConfiguration configuration =
          route->routemapoverlay->GetConfiguration();
      bool changed = false;
      if (configuration.Start == oldName && configuration.StartGUID.IsEmpty()) {
        configuration.Start = name;
        changed = true;
      }
      if (configuration.End == oldName && configuration.EndGUID.IsEmpty()) {
        configuration.End = name;
        changed = true;
      }
      if (changed) route->routemapoverlay->SetConfiguration(configuration);
    }
    m_ConfigurationDialog.RenameSource(oldName, name);
    m_ConfigurationBatchDialog.RenameSource(oldName, name);
  }
  UpdateConfigurations();
  m_tAutoSaveXML.Start(5000, true);
}

void WeatherRouting::OnDeletePosition(wxCommandEvent& event) {
  long index = m_panel->m_lPositions->GetNextItem(-1, wxLIST_NEXT_ALL,
                                                  wxLIST_STATE_SELECTED);
  if (index < 0) return;

  wxListItem item;
  item.SetId(index);
  item.SetColumn(0);
  item.SetMask(
      wxLIST_MASK_TEXT);  // Note use of the mask, somehow it's required for
                          // this to work correctly on windows
  m_panel->m_lPositions->GetItem(item);

  long ID = m_panel->m_lPositions->GetItemData(index);
  assert(ID >= 0);

  for (std::list<RouteMapPosition>::iterator it = RouteMap::Positions.begin();
       it != RouteMap::Positions.end(); it++) {
    if ((*it).ID == ID) {
      wxString name = (*it).Name;
      m_ConfigurationDialog.RemoveSource(name);
      m_ConfigurationBatchDialog.RemoveSource(name);
      RouteMap::Positions.erase(it);
      break;
    }
  }
  m_panel->m_lPositions->DeleteItem(index);

  UpdateConfigurations();
  m_tAutoSaveXML.Start(5000, true);  // Schedule auto-save in 5 seconds
}

void WeatherRouting::OnDeleteAllPositions(wxCommandEvent& event) {
  RouteMap::Positions.clear();
  m_ConfigurationDialog.ClearSources();
  m_ConfigurationBatchDialog.ClearSources();
  m_panel->m_lPositions->DeleteAllItems();
  m_tAutoSaveXML.Start(5000, true);  // Schedule auto-save in 5 seconds
}

void WeatherRouting::OnPositionKeyDown(wxListEvent& event) {
  switch (event.GetKeyCode()) {
    case WXK_DELETE: {
      wxCommandEvent event;
      OnDeletePosition(event);
    } break;
    case WXK_RETURN:
    case WXK_NUMPAD_ENTER:
      OnEditPosition();
      break;
    default:
      event.Skip();
  }
}

void WeatherRouting::OnEditConfiguration() {
  std::list<RouteMapOverlay*> routemapoverlays = CurrentRouteMaps(true);
  if (routemapoverlays.empty()) return;

  m_ApplyingMultiLegGroupSettings = false;
  m_MultiLegSettingsGroupId.Clear();
  m_MultiLegLegSnapshots.clear();

  m_ConfigurationDialog.Show();

#if 0
    /* if boat filename doesn't exist open boat dialog immediately */
    wxString boatfilename = m_ConfigurationDialog.m_fpBoat->GetPath();
    if(!boatfilename.empty() && !wxFileName::FileExists(boatfilename))
        m_ConfigurationDialog.EditBoat();
#endif
}

void WeatherRouting::OnGoTo(wxCommandEvent& event) {
  std::list<RouteMapOverlay*> currentroutemaps = CurrentRouteMaps(true);
  if (currentroutemaps.empty()) return;

  double avg_lat = 0, avg_lonx = 0, avg_lony = 0, total = 0;
  for (std::list<RouteMapOverlay*>::iterator it = currentroutemaps.begin();
       it != currentroutemaps.end(); it++) {
    RouteMapConfiguration configuration = (*it)->GetConfiguration();
    if (std::isnan(configuration.StartLat)) continue;
    avg_lat += configuration.StartLat + configuration.EndLat;
    avg_lonx = cos(deg2rad(configuration.StartLon)) +
               cos(deg2rad(configuration.EndLon));
    avg_lony = sin(deg2rad(configuration.StartLon)) +
               sin(deg2rad(configuration.EndLon));

    total += 2;
  }

  avg_lat /= total, avg_lonx /= total, avg_lony /= total;
  double avg_lon = rad2deg(atan2(avg_lony, avg_lonx));

  double max_distance = 0;
  for (std::list<RouteMapOverlay*>::iterator it = currentroutemaps.begin();
       it != currentroutemaps.end(); it++) {
    RouteMapConfiguration configuration = (*it)->GetConfiguration();
    if (std::isnan(configuration.StartLat)) continue;
    double distance;
    DistanceBearingMercator_Plugin(avg_lat, avg_lon, configuration.StartLat,
                                   configuration.StartLon, NULL, &distance);
    max_distance = wxMax(distance, max_distance);
    DistanceBearingMercator_Plugin(avg_lat, avg_lon, configuration.EndLat,
                                   configuration.EndLon, NULL, &distance);
    max_distance = wxMax(distance, max_distance);
  }

  if (max_distance > 1e-4)
    JumpToPosition(avg_lat, avg_lon, .125 / max_distance);
  else {
    wxMessageDialog mdlg(this, _("Cannot goto invalid route(s)."),
                         _("Weather Routing"), wxOK | wxICON_ERROR);
    mdlg.ShowModal();
  }
}

void WeatherRouting::OnDelete(wxCommandEvent& event) {
  //  Stop all computations to avoid thread corruption.
  //  Probably could do better to stop only the computation of selected
  //  configuration But stopping all is safer. Sess:
  //  https://github.com/rgleason/weather_routing_pi/issues/103
  StopAll();

  long index = m_panel->m_lWeatherRoutes->GetNextItem(-1, wxLIST_NEXT_ALL,
                                                      wxLIST_STATE_SELECTED);
  if (index < 0) return;

  DeleteRouteMaps(CurrentRouteMaps());

  /* select map just after the first one selected */
  int cnt = m_panel->m_lWeatherRoutes->GetItemCount();
  m_panel->m_lWeatherRoutes->SetItemState(index == cnt ? index - 1 : index,
                                          wxLIST_STATE_SELECTED,
                                          wxLIST_STATE_SELECTED);
  GetParent()->Refresh();

  m_tAutoSaveXML.Start(5000, true);  // Schedule auto-save in 5 seconds
}

void WeatherRouting::OnDeleteAll(wxCommandEvent& event) {
  std::list<RouteMapOverlay*> allroutemapoverlays;
  for (int i = 0; i < m_panel->m_lWeatherRoutes->GetItemCount(); i++) {
    WeatherRoute* weatherroute = reinterpret_cast<WeatherRoute*>(
        wxUIntToPtr(m_panel->m_lWeatherRoutes->GetItemData(i)));
    allroutemapoverlays.push_back(weatherroute->routemapoverlay);
  }

  DeleteRouteMaps(allroutemapoverlays);

  GetParent()->Refresh();

  m_tAutoSaveXML.Start(5000, true);  // Schedule auto-save in 5 seconds
}

void WeatherRouting::OnWeatherRouteSort(wxListEvent& event) {
  sortcol = event.GetColumn();
  sortorder = -sortorder;

  if (sortcol == 0) {
    for (int index = 0; index < m_panel->m_lWeatherRoutes->GetItemCount();
         index++) {
      WeatherRoute* weatherroute = reinterpret_cast<WeatherRoute*>(
          wxUIntToPtr(m_panel->m_lWeatherRoutes->GetItemData(index)));
      weatherroute->routemapoverlay->m_bEndRouteVisible = sortorder == 1;
      UpdateItem(index);
    }
    RequestRefresh(GetParent());
  } else {
#if wxCHECK_VERSION(2, 9, 0)
    m_panel->m_lWeatherRoutes->SortItems(SortWeatherRoutes,
                                         (wxIntPtr)m_panel->m_lWeatherRoutes);
#else
    m_panel->m_lWeatherRoutes->SortItems(SortWeatherRoutes,
                                         (long)m_panel->m_lWeatherRoutes);
#endif
  }
}

void WeatherRouting::OnWeatherRouteSelected() {
  GetParent()->Refresh();

  // Get the list of currently selected routes.
  std::list<RouteMapOverlay*> currentroutemaps = CurrentRouteMaps();
  ValidateStabilityCorridorSelection(currentroutemaps);
  std::list<RouteMapConfiguration> currentconfigurations;
  for (std::list<RouteMapOverlay*>::iterator it = currentroutemaps.begin();
       it != currentroutemaps.end(); it++) {
    (*it)->SetCursorLatLon(m_weather_routing_pi.m_cursor_lat,
                           m_weather_routing_pi.m_cursor_lon);
    currentconfigurations.push_back((*it)->GetConfiguration());
  }

  if (currentroutemaps.empty())
    m_tHideConfiguration.Start(25, true);
  else {
    m_tHideConfiguration.Stop();
    m_bSkipUpdateCurrentItems = true;
    m_ConfigurationDialog.SetConfigurations(currentconfigurations);
    m_bSkipUpdateCurrentItems = false;
  }

  UpdateDialogs();

  // Keep the table's route reference synchronized even while its AUI pane is
  // hidden. An empty selection must clear the previous route reference.
  if (m_RoutingTablePanel) {
    m_RoutingTablePanel->SetRouteMap(
        currentroutemaps.empty() ? nullptr : currentroutemaps.front());
  }

  SetEnableConfigurationMenu();
}

void WeatherRouting::OnWeatherRouteKeyDown(wxListEvent& event) {
  switch (event.GetKeyCode()) {
    case WXK_DELETE: {
      wxCommandEvent event;
      OnDelete(event);
    } break;
    default:
      event.Skip();
  }
}

void WeatherRouting::OnWeatherRoutesListLeftDown(wxMouseEvent& event) {
  OnLeftDown(event);
  wxPoint pos = event.GetPosition();
  int flags = 0;
  long index = m_panel->m_lWeatherRoutes->HitTest(pos, flags);

  // Do we have the Visibility column?
  if (columns[VISIBLE] >= 0) {
    int minx = 0,
        maxx = m_panel->m_lWeatherRoutes->GetColumnWidth(columns[VISIBLE]);

    //    Clicking Visibility column?
    if (index >= 0 && event.GetX() >= minx && event.GetX() < maxx) {
      // Process the clicked item
      WeatherRoute* weatherroute = reinterpret_cast<WeatherRoute*>(
          wxUIntToPtr(m_panel->m_lWeatherRoutes->GetItemData(index)));
      weatherroute->routemapoverlay->m_bEndRouteVisible =
          !weatherroute->routemapoverlay->m_bEndRouteVisible;
      UpdateItem(index);
      RequestRefresh(GetParent());
    }
  }

  // Allow wx to process...
  event.Skip();
}

void WeatherRouting::UpdateComputeState() {
  m_panel->m_gProgress->SetRange(m_RoutesToRun);

  if (m_bRunning) return;

  m_bRunning = true;
  m_panel->m_gProgress->SetValue(0);

  m_mCompute->Enable();
  m_panel->m_bCompute->Enable();
  m_StartTime = wxDateTime::Now();
  m_tCompute.Start(1, true);
}

class DepartureTimeOptimizationResultsDialog : public wxDialog {
public:
  DepartureTimeOptimizationResultsDialog(
      WeatherRouting* parent, const std::list<RouteMapOverlay*>& routemaps,
      const wxDateTime& nominalStartTime)
      : wxDialog(parent, wxID_ANY, _("Departure Time Optimisation Results"),
                 wxDefaultPosition, wxSize(1100, 420),
                 wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER),
        m_WeatherRouting(parent),
        m_RouteMaps(routemaps),
        m_NominalStartTime(nominalStartTime),
        m_AutoRefreshTimer(this),
        m_AutoRefreshCount(0),
        m_UpdatingSelection(false),
        m_CloseHandled(false) {
    wxBoxSizer* topSizer = new wxBoxSizer(wxVERTICAL);
    m_List = new wxListCtrl(this, wxID_ANY, wxDefaultPosition, wxDefaultSize,
                            wxLC_REPORT | wxLC_SINGLE_SEL);

    wxString columns[] = {
        _("Best"),        _("Offset"),   _("Departure"), _("ETA"),
        _("Elapsed"),     _("Distance"), _("Avg Speed"), _("Avg SOG"),
        _("Max SOG"),     _("Avg Wind"), _("Max Wind"),  _("Avg Current"),
        _("Max Current"), _("Tacks"),    _("Comfort"),   _("Wx"),
        _("State")};
    for (unsigned int i = 0; i < WXSIZEOF(columns); i++)
      m_List->InsertColumn(i, columns[i]);
    m_List->SetToolTip(
        _("Wx reports wind sources used by the accepted route. Orange route "
          "wind barbs mark climatology-sourced legs."));

    topSizer->Add(m_List, 1, wxEXPAND | wxALL, 5);

    wxBoxSizer* corridorSizer = new wxBoxSizer(wxHORIZONTAL);
    m_ShowCorridor = new wxCheckBox(
        this, wxID_ANY, _("Show stability corridor for selected route"));
    m_ShowCorridor->SetToolTip(
        _("Show route-agreement bands for the selected completed departure "
          "candidate. This is descriptive, not a safety guarantee."));
    m_ShowCorridor->Enable(false);
    corridorSizer->Add(m_ShowCorridor, 0, wxALL | wxALIGN_CENTER_VERTICAL, 5);
    m_KeepCorridor = new wxCheckBox(
        this, wxID_ANY, _("Keep corridor on chart when results close"));
    m_KeepCorridor->SetToolTip(
        _("Keep the displayed corridor with its selected weather route until "
          "another route is selected or the overlay is hidden."));
    m_KeepCorridor->SetValue(
        m_WeatherRouting->StabilityCorridorKeepPreference());
    m_KeepCorridor->Enable(false);
    corridorSizer->Add(m_KeepCorridor, 0, wxALL | wxALIGN_CENTER_VERTICAL, 5);
    m_CorridorStatus =
        new wxStaticText(this, wxID_ANY, _("Waiting for completed routes..."));
    corridorSizer->Add(m_CorridorStatus, 1, wxALL | wxALIGN_CENTER_VERTICAL, 5);
    topSizer->Add(corridorSizer, 0, wxEXPAND);

    wxBoxSizer* buttonSizer = new wxBoxSizer(wxHORIZONTAL);
    wxButton* refresh = new wxButton(this, wxID_REFRESH, _("Refresh"));
    buttonSizer->Add(refresh, 0, wxALL, 5);
    buttonSizer->AddStretchSpacer();
    wxButton* close = new wxButton(this, wxID_CLOSE, _("Close"));
    buttonSizer->Add(close, 0, wxALL, 5);
    topSizer->Add(buttonSizer, 0, wxEXPAND);

    SetSizer(topSizer);
    refresh->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) {
      Populate();
      UpdateAutoRefresh();
    });
    close->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) {
      StopAutoRefresh();
      CloseCorridor("results_closed");
      EndModal(wxID_OK);
    });
    m_ShowCorridor->Bind(wxEVT_CHECKBOX,
                         [this](wxCommandEvent&) { UpdateCorridor(); });
    m_KeepCorridor->Bind(wxEVT_CHECKBOX, [this](wxCommandEvent&) {
      m_WeatherRouting->SetStabilityCorridorKeepPreference(
          m_KeepCorridor->GetValue());
    });
    m_List->Bind(wxEVT_LIST_ITEM_SELECTED,
                 [this](wxListEvent&) { UpdateCorridor(); });
    m_List->Bind(wxEVT_LIST_ITEM_DESELECTED,
                 [this](wxListEvent&) { UpdateCorridor(); });
    Bind(wxEVT_TIMER, &DepartureTimeOptimizationResultsDialog::OnAutoRefresh,
         this);
    Bind(wxEVT_CLOSE_WINDOW, [this](wxCloseEvent&) {
      StopAutoRefresh();
      CloseCorridor("results_closed");
      EndModal(wxID_OK);
    });
    Populate();
    UpdateAutoRefresh();
  }

  ~DepartureTimeOptimizationResultsDialog() override {
    StopAutoRefresh();
    CloseCorridor("results_destroyed");
  }

private:
  static const int AUTO_REFRESH_INTERVAL_MS = 1000;
  static const int AUTO_REFRESH_MAX_COUNT = 3600;

  WeatherRoute* FindWeatherRoute(RouteMapOverlay* routemap) {
    for (auto it = m_WeatherRouting->m_WeatherRoutes.begin();
         it != m_WeatherRouting->m_WeatherRoutes.end(); it++)
      if ((*it)->routemapoverlay == routemap) return *it;
    return NULL;
  }

  bool RouteMapIsInList(RouteMapOverlay* routemap,
                        const std::list<RouteMapOverlay*>& routemaps) const {
    return std::find(routemaps.begin(), routemaps.end(), routemap) !=
           routemaps.end();
  }

  bool RouteStillPending(RouteMapOverlay* routemap) {
    if (!m_WeatherRouting || !FindWeatherRoute(routemap)) return false;
    if (RouteMapIsInList(routemap, m_WeatherRouting->m_WaitingRouteMaps))
      return true;
    if (RouteMapIsInList(routemap, m_WeatherRouting->m_RunningRouteMaps))
      return true;
    return routemap->Running();
  }

  bool HasPendingRoutes() {
    for (auto routemap : m_RouteMaps)
      if (RouteStillPending(routemap)) return true;
    return false;
  }

  void StopAutoRefresh() {
    if (m_AutoRefreshTimer.IsRunning()) m_AutoRefreshTimer.Stop();
  }

  void CloseCorridor(const wxString& reason) {
    if (m_CloseHandled || !m_WeatherRouting) return;
    m_CloseHandled = true;
    m_WeatherRouting->CloseStabilityCorridorResults(
        m_ShowCorridor->GetValue() && m_KeepCorridor->GetValue(), reason);
  }

  void UpdateAutoRefresh() {
    if (HasPendingRoutes() && m_AutoRefreshCount < AUTO_REFRESH_MAX_COUNT) {
      if (!m_AutoRefreshTimer.IsRunning())
        m_AutoRefreshTimer.Start(AUTO_REFRESH_INTERVAL_MS);
    } else {
      StopAutoRefresh();
    }
  }

  void OnAutoRefresh(wxTimerEvent&) {
    wxStopWatch timer;
    m_AutoRefreshCount++;
    Populate();
    long populateMs = timer.Time();
    UpdateAutoRefresh();
    long totalMs = timer.Time();
    if (totalMs >= UI_TIMING_LOG_THRESHOLD_MS)
      wxLogMessage(
          "WR_UI_TIMING departure_results_autorefresh total_ms=%ld "
          "populate_ms=%ld routes=%lu pending=%d modal=1",
          totalMs, populateMs, static_cast<unsigned long>(m_RouteMaps.size()),
          HasPendingRoutes() ? 1 : 0);
  }

  wxString FormatOffset(int minutes) {
    wxString sign = minutes < 0 ? _T("-") : _T("+");
    int absMinutes = abs(minutes);
    return wxString::Format(_T("%s%d:%02d"), sign, absMinutes / 60,
                            absMinutes % 60);
  }

  wxString FormatElapsedSeconds(long seconds) {
    int days = seconds / 86400;
    seconds %= 86400;
    int hours = seconds / 3600;
    seconds %= 3600;
    int minutes = seconds / 60;
    if (days)
      return wxString::Format(_T("%dd %02d:%02d"), days, hours, minutes);
    return wxString::Format(_T("%02d:%02d"), hours, minutes);
  }

  bool IsFiniteMetricText(wxString value) const {
    value.Trim(true);
    value.Trim(false);
    value.MakeLower();
    return value.Find(_T("nan")) == wxNOT_FOUND &&
           value.Find(_T("inf")) == wxNOT_FOUND;
  }

  wxString MetricOrNA(const wxString& value, bool complete) const {
    if (!complete) return _("N/A");
    if (!IsFiniteMetricText(value)) return _("N/A");
    if (value.IsEmpty()) return _("N/A");
    return value;
  }

  void SetCell(long row, int col, const wxString& value) {
    m_List->SetItem(row, col, value);
    m_List->SetColumnWidth(col, wxLIST_AUTOSIZE_USEHEADER);
  }

  RouteMapOverlay* SelectedRoute() const {
    long selected =
        m_List->GetNextItem(-1, wxLIST_NEXT_ALL, wxLIST_STATE_SELECTED);
    if (selected < 0) return NULL;
    const long routeIndex = m_List->GetItemData(selected);
    if (routeIndex < 0 || routeIndex >= static_cast<long>(m_RouteMaps.size()))
      return NULL;
    auto route = m_RouteMaps.begin();
    std::advance(route, routeIndex);
    return *route;
  }

  void UpdateCorridor() {
    if (m_UpdatingSelection || !m_WeatherRouting) return;
    RouteMapOverlay* selected = SelectedRoute();
    if (!m_ShowCorridor->GetValue()) {
      m_KeepCorridor->Enable(false);
      m_WeatherRouting->HideStabilityCorridor("checkbox_disabled");
      if (m_ShowCorridor->IsEnabled())
        m_CorridorStatus->SetLabel(
            _("Select a completed route to display its stability family."));
      return;
    }
    if (!selected || !selected->Finished() || !selected->ReachedDestination()) {
      m_KeepCorridor->Enable(false);
      m_WeatherRouting->HideStabilityCorridor("selection_not_complete");
      m_CorridorStatus->SetLabel(
          _("A completed route is required to display a stability corridor."));
      return;
    }
    m_CorridorStatus->SetLabel(_("Calculating stability corridor..."));
    Layout();
    Update();
    wxString status;
    if (!m_WeatherRouting->ShowStabilityCorridor(m_RouteMaps, selected,
                                                 &status)) {
      m_KeepCorridor->Enable(false);
      m_CorridorStatus->SetLabel(
          status.IsEmpty() ? _("No stability corridor is available.") : status);
      return;
    }
    m_KeepCorridor->Enable(true);
    m_CorridorStatus->SetLabel(status);
  }

  void Populate() {
    wxStopWatch timer;
    RouteMapOverlay* selectedRoute = SelectedRoute();
    m_UpdatingSelection = true;
    m_List->DeleteAllItems();

    RouteMapOverlay* bestRoute = NULL;
    long bestSeconds = std::numeric_limits<long>::max();
    for (auto routemap : m_RouteMaps) {
      RouteMapConfiguration configuration = routemap->GetConfiguration();
      if (!routemap->Finished() || !routemap->ReachedDestination()) continue;

      wxTimeSpan elapsed = routemap->EndTime() - configuration.StartTime;
      long seconds = elapsed.GetSeconds().ToLong();
      if (seconds >= 0 && seconds < bestSeconds) {
        bestSeconds = seconds;
        bestRoute = routemap;
      }
    }

    size_t routeIndex = 0;
    int completeRoutes = 0;
    for (auto routemap : m_RouteMaps) {
      WeatherRoute* weatherroute = FindWeatherRoute(routemap);
      if (!weatherroute) {
        ++routeIndex;
        continue;
      }
      weatherroute->Update(m_WeatherRouting);

      RouteMapConfiguration configuration = routemap->GetConfiguration();
      long row =
          m_List->InsertItem(m_List->GetItemCount(),
                             routemap == bestRoute ? _("Best") : wxString());
      bool complete = routemap->Finished() && routemap->ReachedDestination();
      if (complete) ++completeRoutes;
      m_List->SetItemData(row, static_cast<long>(routeIndex));
      SetCell(
          row, 1,
          FormatOffset(configuration.DepartureTimeOptimizationOffsetMinutes));
      SetCell(row, 2, weatherroute->StartTime);
      SetCell(row, 3, complete ? weatherroute->EndTime : _("N/A"));

      if (complete) {
        wxTimeSpan elapsed = routemap->EndTime() - configuration.StartTime;
        SetCell(row, 4, FormatElapsedSeconds(elapsed.GetSeconds().ToLong()));
      } else {
        SetCell(row, 4, _("N/A"));
      }

      SetCell(row, 5, MetricOrNA(weatherroute->Distance, complete));
      SetCell(row, 6, MetricOrNA(weatherroute->AvgSpeed, complete));
      SetCell(row, 7, MetricOrNA(weatherroute->AvgSpeedGround, complete));
      SetCell(row, 8, MetricOrNA(weatherroute->MaxSpeedGround, complete));
      SetCell(row, 9, MetricOrNA(weatherroute->AvgWind, complete));
      SetCell(row, 10, MetricOrNA(weatherroute->MaxWind, complete));
      SetCell(row, 11, MetricOrNA(weatherroute->AvgCurrent, complete));
      SetCell(row, 12, MetricOrNA(weatherroute->MaxCurrent, complete));
      SetCell(row, 13, MetricOrNA(weatherroute->Tacks, complete));
      SetCell(row, 14, MetricOrNA(weatherroute->Comfort, complete));
      SetCell(row, 15,
              complete ? weatherroute->WeatherSource : wxString(_("N/A")));
      SetCell(row, 16, weatherroute->State);
      if (routemap == selectedRoute)
        m_List->SetItemState(row, wxLIST_STATE_SELECTED, wxLIST_STATE_SELECTED);
      ++routeIndex;
    }
    const bool pending = HasPendingRoutes();
    const bool available = !pending && completeRoutes >= 3;
    m_ShowCorridor->Enable(available);
    m_KeepCorridor->Enable(available && m_ShowCorridor->GetValue());
    if (!available) {
      if (m_ShowCorridor->GetValue()) m_ShowCorridor->SetValue(false);
      m_WeatherRouting->HideStabilityCorridor(
          pending ? "optimization_running" : "too_few_completed_routes");
      m_CorridorStatus->SetLabel(
          pending ? _("Waiting for completed routes...")
                  : _("No stability corridor: fewer than 3 completed routes."));
    } else if (!selectedRoute) {
      m_CorridorStatus->SetLabel(
          _("Select a completed route to display its stability family."));
    }
    m_UpdatingSelection = false;
    if (available && m_ShowCorridor->GetValue()) UpdateCorridor();
    long totalMs = timer.Time();
    if (totalMs >= UI_TIMING_LOG_THRESHOLD_MS)
      wxLogMessage(
          "WR_UI_TIMING departure_results_populate total_ms=%ld routes=%lu "
          "rows=%ld modal=1",
          totalMs, static_cast<unsigned long>(m_RouteMaps.size()),
          m_List->GetItemCount());
  }

  WeatherRouting* m_WeatherRouting;
  std::list<RouteMapOverlay*> m_RouteMaps;
  wxDateTime m_NominalStartTime;
  wxListCtrl* m_List;
  wxTimer m_AutoRefreshTimer;
  int m_AutoRefreshCount;
  wxCheckBox* m_ShowCorridor;
  wxCheckBox* m_KeepCorridor;
  wxStaticText* m_CorridorStatus;
  bool m_UpdatingSelection;
  bool m_CloseHandled;
};

void WeatherRouting::ShowDepartureTimeOptimizationResults(
    const std::list<RouteMapOverlay*>& routemapoverlays,
    const wxDateTime& nominalStartTime) {
  wxStopWatch timer;
  wxLogMessage(
      "WR_UI_TIMING departure_results_show begin routes=%lu modal=1 "
      "progress_dialog=%d progress_timer=%d deferred_start=%d",
      static_cast<unsigned long>(routemapoverlays.size()),
      m_RoutingProgressDialog && m_RoutingProgressDialog->IsShown() ? 1 : 0,
      m_tRoutingProgress.IsRunning() ? 1 : 0,
      m_DeferredRoutingStartPending ? 1 : 0);
  DepartureTimeOptimizationResultsDialog dialog(this, routemapoverlays,
                                                nominalStartTime);
  long constructMs = timer.Time();
  dialog.ShowModal();
  wxLogMessage(
      "WR_UI_TIMING departure_results_show end total_ms=%ld construct_ms=%ld "
      "routes=%lu modal=1 progress_dialog=%d progress_timer=%d",
      timer.Time(), constructMs,
      static_cast<unsigned long>(routemapoverlays.size()),
      m_RoutingProgressDialog && m_RoutingProgressDialog->IsShown() ? 1 : 0,
      m_tRoutingProgress.IsRunning() ? 1 : 0);
}

bool WeatherRouting::ShowStabilityCorridor(
    const std::list<RouteMapOverlay*>& routes, RouteMapOverlay* selected,
    wxString* status) {
  if (!selected) {
    if (status) *status = _("A completed route is required.");
    HideStabilityCorridor("no_selection");
    return false;
  }

  const std::vector<RouteMapOverlay*> source(routes.begin(), routes.end());
  const auto selectedIt = std::find(source.begin(), source.end(), selected);
  if (selectedIt == source.end()) {
    if (status) *status = _("The selected route is not in this result set.");
    return false;
  }
  const std::vector<weather_routing_engine::StabilityRoute> stabilityRoutes =
      source == m_StabilityCorridorSourceRoutes &&
              m_StabilityCorridorResult.success
          ? m_StabilityCorridorRoutes
          : BuildValidatedStabilityRoutes(source);
  return ShowStabilityCorridorData(
      source, stabilityRoutes, static_cast<size_t>(selectedIt - source.begin()),
      {selected}, status);
}

bool WeatherRouting::ShowMultiLegStabilityCorridor(
    const std::vector<std::vector<RouteMapOverlay*> >& candidates,
    size_t selectedCandidate, wxString* status) {
  if (selectedCandidate >= candidates.size() ||
      candidates[selectedCandidate].empty()) {
    if (status) *status = _("A completed route is required.");
    HideStabilityCorridor("no_multi_leg_selection");
    return false;
  }
  std::vector<RouteMapOverlay*> signature;
  for (const auto& candidate : candidates) {
    signature.insert(signature.end(), candidate.begin(), candidate.end());
    signature.push_back(NULL);
  }
  const std::vector<weather_routing_engine::StabilityRoute> stabilityRoutes =
      signature == m_StabilityCorridorSourceRoutes &&
              m_StabilityCorridorResult.success
          ? m_StabilityCorridorRoutes
          : BuildValidatedMultiLegStabilityRoutes(candidates);
  return ShowStabilityCorridorData(signature, stabilityRoutes,
                                   selectedCandidate,
                                   candidates[selectedCandidate], status);
}

bool WeatherRouting::ShowStabilityCorridorData(
    const std::vector<RouteMapOverlay*>& sourceSignature,
    const std::vector<weather_routing_engine::StabilityRoute>& routes,
    size_t selectedIndex, const std::vector<RouteMapOverlay*>& selectedRoutes,
    wxString* status) {
  if (selectedRoutes.empty() || !selectedRoutes.front()) return false;
  const bool cacheHit = sourceSignature == m_StabilityCorridorSourceRoutes &&
                        m_StabilityCorridorResult.success;
  if (!cacheHit) {
    wxStopWatch timer;
    m_StabilityCorridorSourceRoutes = sourceSignature;
    m_StabilityCorridorRoutes = routes;
    RouteMapConfiguration configuration =
        selectedRoutes.front()->GetConfiguration();
    weather_routing_engine::StabilityCorridorOptions options;
    wxLogMessage(
        "WR_STABILITY_CORRIDOR_START source=gui result_set=%p candidates=%lu "
        "minimum_routes=%d grid_resolution_nm=%.3f inner=%.3f outer=%.3f "
        "cache=miss authoritative_final_validation=1 descriptive_only=1",
        sourceSignature.empty() ? NULL : sourceSignature.front(),
        static_cast<unsigned long>(routes.size()), options.minimumRoutes,
        options.gridResolutionNm, options.innerAgreementThreshold,
        options.outerAgreementThreshold);
    const auto segmentSafety =
        [&](const weather_routing_engine::StabilityPoint& first,
            const weather_routing_engine::StabilityPoint& last) {
          return CheckStabilitySafetySegment(configuration, first, last);
        };
    const auto cellSafety = [&](double minLat, double minLon, double maxLat,
                                double maxLon) {
      return CheckStabilitySafetyCell(configuration, minLat, minLon, maxLat,
                                      maxLon);
    };
    m_StabilityCorridorResult =
        weather_routing_engine::StabilityCorridorCalculator::Calculate(
            m_StabilityCorridorRoutes, options, segmentSafety, cellSafety);
    for (const auto& family : m_StabilityCorridorResult.families)
      wxLogMessage(
          "WR_STABILITY_CORRIDOR_CLUSTER source=gui family=%d routes=%lu "
          "representative=%lu median_width_nm=%.3f max_width_nm=%.3f "
          "eta_spread_min=%.1f",
          family.id, static_cast<unsigned long>(family.routeIndices.size()),
          static_cast<unsigned long>(family.representativeRouteIndex),
          family.medianWidthNm, family.maximumWidthNm, family.etaSpreadMinutes);
    wxLogMessage(
        "WR_STABILITY_CORRIDOR_BUILD source=gui valid=%d excluded=%d "
        "families=%lu cells=%d unsafe_cells_excluded=%d elapsed_ms=%ld",
        m_StabilityCorridorResult.validRoutes,
        m_StabilityCorridorResult.excludedRoutes,
        static_cast<unsigned long>(m_StabilityCorridorResult.families.size()),
        m_StabilityCorridorResult.rasterCellsUsed,
        m_StabilityCorridorResult.unsafeCellsExcluded, timer.Time());
  } else {
    wxLogMessage(
        "WR_STABILITY_CORRIDOR_START source=gui result_set=%p candidates=%lu "
        "cache=hit",
        sourceSignature.empty() ? NULL : sourceSignature.front(),
        static_cast<unsigned long>(routes.size()));
  }

  const int familyId =
      weather_routing_engine::StabilityCorridorCalculator::FindFamilyForRoute(
          m_StabilityCorridorResult, selectedIndex);
  if (!m_StabilityCorridorResult.success || familyId < 0) {
    m_StabilityCorridorLifecycle.Hide();
    UpdateStabilityCorridorMenu();
    const wxString reason = m_StabilityCorridorResult.failureReason.IsEmpty()
                                ? _("The selected route has too few similar "
                                    "completed routes.")
                                : m_StabilityCorridorResult.failureReason;
    if (status) *status = reason;
    wxLogMessage(
        "WR_STABILITY_CORRIDOR_RESULT source=gui status=unavailable "
        "selected_index=%lu family=-1 cache=%s reason=\"%s\"",
        static_cast<unsigned long>(selectedIndex), cacheHit ? "hit" : "miss",
        reason);
    RequestRefresh(GetOCPNCanvasWindow());
    return false;
  }

  const weather_routing_engine::RouteFamily* selectedFamily = NULL;
  for (const auto& family : m_StabilityCorridorResult.families)
    if (family.id == familyId) selectedFamily = &family;
  if (!selectedFamily) return false;

  std::vector<StabilityCorridorLifecycle::RouteToken> routeTokens;
  routeTokens.reserve(selectedRoutes.size());
  for (RouteMapOverlay* route : selectedRoutes) routeTokens.push_back(route);
  m_StabilityCorridorLifecycle.Show(familyId, routeTokens);
  UpdateStabilityCorridorMenu();
  SelectWeatherRoutesForStability(selectedRoutes);
  if (status)
    *status = wxString::Format(
        _("Stability corridor: family %d - %lu of %d validated routes"),
        familyId + 1,
        static_cast<unsigned long>(selectedFamily->routeIndices.size()),
        m_StabilityCorridorResult.validRoutes);
  wxLogMessage(
      "WR_STABILITY_CORRIDOR_RESULT source=gui status=complete family=%d "
      "routes=%lu valid=%d cache=%s elapsed_ms=%ld",
      familyId, static_cast<unsigned long>(selectedFamily->routeIndices.size()),
      m_StabilityCorridorResult.validRoutes, cacheHit ? "hit" : "miss",
      m_StabilityCorridorResult.calculationTimeMs);
  wxLogMessage(
      "WR_STABILITY_CORRIDOR_DISPLAY family=%d selected_route=%p "
      "inner_cells=%lu outer_cells=%lu",
      familyId, selectedRoutes.front(),
      static_cast<unsigned long>(selectedFamily->innerCells.size()),
      static_cast<unsigned long>(selectedFamily->outerCells.size()));
  RequestRefresh(GetOCPNCanvasWindow());
  return true;
}

void WeatherRouting::SelectWeatherRoutesForStability(
    const std::vector<RouteMapOverlay*>& routes) {
  if (routes.empty() || !m_panel || !m_panel->m_lWeatherRoutes) return;
  m_UpdatingStabilityRouteSelection = true;
  long firstSelectedRow = -1;
  for (long row = 0; row < m_panel->m_lWeatherRoutes->GetItemCount(); ++row) {
    WeatherRoute* weatherRoute = reinterpret_cast<WeatherRoute*>(
        wxUIntToPtr(m_panel->m_lWeatherRoutes->GetItemData(row)));
    const bool selected =
        weatherRoute &&
        std::find(routes.begin(), routes.end(),
                  weatherRoute->routemapoverlay) != routes.end();
    if (selected && firstSelectedRow < 0) firstSelectedRow = row;
    m_panel->m_lWeatherRoutes->SetItemState(
        row, selected ? wxLIST_STATE_SELECTED : 0, wxLIST_STATE_SELECTED);
  }
  if (firstSelectedRow < 0) {
    m_UpdatingStabilityRouteSelection = false;
    return;
  }
  m_panel->m_lWeatherRoutes->SetItemState(
      firstSelectedRow, wxLIST_STATE_FOCUSED, wxLIST_STATE_FOCUSED);
  m_panel->m_lWeatherRoutes->EnsureVisible(firstSelectedRow);
  OnWeatherRouteSelected();
  m_UpdatingStabilityRouteSelection = false;
}

void WeatherRouting::HideStabilityCorridor(const wxString& reason) {
  if (m_StabilityCorridorLifecycle.IsVisible())
    wxLogMessage("WR_STABILITY_CORRIDOR_HIDE family=%d reason=\"%s\"",
                 m_StabilityCorridorLifecycle.FamilyId(), reason);
  m_StabilityCorridorLifecycle.Hide();
  UpdateStabilityCorridorMenu();
  RequestRefresh(GetOCPNCanvasWindow());
}

void WeatherRouting::CloseStabilityCorridorResults(bool keepVisible,
                                                   const wxString& reason) {
  const int familyId = m_StabilityCorridorLifecycle.FamilyId();
  m_StabilityCorridorLifecycle.SetPinned(keepVisible);
  if (m_StabilityCorridorLifecycle.ResultsClosed()) {
    wxLogMessage("WR_STABILITY_CORRIDOR_HIDE family=%d reason=\"%s\"", familyId,
                 reason);
    RequestRefresh(GetOCPNCanvasWindow());
  } else if (m_StabilityCorridorLifecycle.IsVisible()) {
    wxLogMessage("WR_STABILITY_CORRIDOR_PIN family=%d reason=\"%s\"",
                 m_StabilityCorridorLifecycle.FamilyId(), reason);
  }
  UpdateStabilityCorridorMenu();
}

void WeatherRouting::SetStabilityCorridorKeepPreference(bool keep) {
  m_StabilityCorridorKeepPreference = keep;
  wxFileConfig* config = GetOCPNConfigObject();
  config->SetPath(_T("/PlugIns/WeatherRouting"));
  config->Write(_T("KeepStabilityCorridorAfterResults"), keep);
  config->Flush();
}

void WeatherRouting::UpdateStabilityCorridorMenu() {
  if (!m_mStabilityCorridorView) return;
  const bool visible = m_StabilityCorridorLifecycle.IsVisible();
  m_mStabilityCorridorView->Check(visible);
  m_mStabilityCorridorView->Enable(visible);
}

void WeatherRouting::OnViewStabilityCorridor(wxCommandEvent& event) {
  if (!event.IsChecked()) HideStabilityCorridor("view_menu_disabled");
}

void WeatherRouting::ValidateStabilityCorridorSelection(
    const std::list<RouteMapOverlay*>& selectedRoutes) {
  if (m_UpdatingStabilityRouteSelection ||
      !m_StabilityCorridorLifecycle.IsVisible())
    return;
  std::vector<StabilityCorridorLifecycle::RouteToken> routeTokens;
  routeTokens.reserve(selectedRoutes.size());
  for (RouteMapOverlay* route : selectedRoutes) routeTokens.push_back(route);
  const int familyId = m_StabilityCorridorLifecycle.FamilyId();
  if (m_StabilityCorridorLifecycle.DisplayedRoutesChanged(routeTokens)) {
    wxLogMessage(
        "WR_STABILITY_CORRIDOR_HIDE family=%d "
        "reason=\"weather_route_selection_changed\"",
        familyId);
    UpdateStabilityCorridorMenu();
    RequestRefresh(GetOCPNCanvasWindow());
  }
}

void WeatherRouting::RenderStabilityCorridor(piDC& dc, PlugIn_ViewPort& vp) {
  if (!m_StabilityCorridorLifecycle.IsVisible()) return;
  const weather_routing_engine::RouteFamily* selectedFamily = NULL;
  for (const auto& family : m_StabilityCorridorResult.families)
    if (family.id == m_StabilityCorridorLifecycle.FamilyId())
      selectedFamily = &family;
  if (!selectedFamily) return;

  const auto drawCells =
      [&](const std::vector<weather_routing_engine::StabilityCell>& cells,
          const wxColour& colour) {
        dc.SetPen(*wxTRANSPARENT_PEN);
        dc.SetBrush(wxBrush(colour));
        for (const auto& cell : cells) {
          wxPoint points[4];
          GetCanvasPixLL(&vp, &points[0], cell.minLat, cell.minLon);
          GetCanvasPixLL(&vp, &points[1], cell.minLat, cell.maxLon);
          GetCanvasPixLL(&vp, &points[2], cell.maxLat, cell.maxLon);
          GetCanvasPixLL(&vp, &points[3], cell.maxLat, cell.minLon);
          dc.DrawPolygon(4, points);
        }
      };
  drawCells(selectedFamily->outerCells, wxColour(50, 155, 210, 45));
  drawCells(selectedFamily->innerCells, wxColour(25, 95, 190, 80));
  dc.SetBrush(*wxTRANSPARENT_BRUSH);
}

class MultiLegDepartureOptimizationResultsDialog : public wxDialog {
public:
  explicit MultiLegDepartureOptimizationResultsDialog(WeatherRouting* parent)
      : wxDialog(parent, wxID_ANY,
                 _("Multi-leg Departure Time Optimisation Results"),
                 wxDefaultPosition, wxSize(1150, 420),
                 wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER),
        m_WeatherRouting(parent),
        m_AutoRefreshTimer(this),
        m_AutoRefreshCount(0),
        m_UpdatingSelection(false),
        m_CloseHandled(false) {
    wxBoxSizer* topSizer = new wxBoxSizer(wxVERTICAL);
    m_List = new wxListCtrl(this, wxID_ANY, wxDefaultPosition, wxDefaultSize,
                            wxLC_REPORT | wxLC_SINGLE_SEL);

    wxString columns[] = {_("Best"),      _("Offset"),     _("Departure"),
                          _("Final ETA"), _("Total Time"), _("Distance"),
                          _("Legs"),      _("State"),      _("Reason")};
    for (unsigned int i = 0; i < WXSIZEOF(columns); i++)
      m_List->InsertColumn(i, columns[i]);

    topSizer->Add(m_List, 1, wxEXPAND | wxALL, 5);

    wxBoxSizer* corridorSizer = new wxBoxSizer(wxHORIZONTAL);
    m_ShowCorridor = new wxCheckBox(
        this, wxID_ANY, _("Show stability corridor for selected route"));
    m_ShowCorridor->SetToolTip(
        _("Show route-agreement bands for the selected completed departure "
          "candidate. This is descriptive, not a safety guarantee."));
    m_ShowCorridor->Enable(false);
    corridorSizer->Add(m_ShowCorridor, 0, wxALL | wxALIGN_CENTER_VERTICAL, 5);
    m_KeepCorridor = new wxCheckBox(
        this, wxID_ANY, _("Keep corridor on chart when results close"));
    m_KeepCorridor->SetToolTip(
        _("Keep the displayed corridor with its selected weather route until "
          "another route is selected or the overlay is hidden."));
    m_KeepCorridor->SetValue(
        m_WeatherRouting->StabilityCorridorKeepPreference());
    m_KeepCorridor->Enable(false);
    corridorSizer->Add(m_KeepCorridor, 0, wxALL | wxALIGN_CENTER_VERTICAL, 5);
    m_CorridorStatus =
        new wxStaticText(this, wxID_ANY, _("Waiting for completed routes..."));
    corridorSizer->Add(m_CorridorStatus, 1, wxALL | wxALIGN_CENTER_VERTICAL, 5);
    topSizer->Add(corridorSizer, 0, wxEXPAND);

    wxBoxSizer* buttonSizer = new wxBoxSizer(wxHORIZONTAL);
    wxButton* refresh = new wxButton(this, wxID_REFRESH, _("Refresh"));
    buttonSizer->Add(refresh, 0, wxALL, 5);
    wxButton* applyBest = new wxButton(this, wxID_ANY, _("Apply Best"));
    buttonSizer->Add(applyBest, 0, wxALL, 5);
    wxButton* applySelected = new wxButton(this, wxID_ANY, _("Apply Selected"));
    buttonSizer->Add(applySelected, 0, wxALL, 5);
    buttonSizer->AddStretchSpacer();
    wxButton* discard = new wxButton(this, wxID_ANY, _("Discard Candidates"));
    buttonSizer->Add(discard, 0, wxALL, 5);
    wxButton* close = new wxButton(this, wxID_CLOSE, _("Close"));
    buttonSizer->Add(close, 0, wxALL, 5);
    topSizer->Add(buttonSizer, 0, wxEXPAND);

    SetSizer(topSizer);
    refresh->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) {
      Populate();
      UpdateAutoRefresh();
    });
    applyBest->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { ApplyBest(); });
    applySelected->Bind(wxEVT_BUTTON,
                        [this](wxCommandEvent&) { ApplySelected(); });
    discard->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { DiscardAndClose(); });
    close->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { CloseDialog(); });
    m_ShowCorridor->Bind(wxEVT_CHECKBOX,
                         [this](wxCommandEvent&) { UpdateCorridor(); });
    m_KeepCorridor->Bind(wxEVT_CHECKBOX, [this](wxCommandEvent&) {
      m_WeatherRouting->SetStabilityCorridorKeepPreference(
          m_KeepCorridor->GetValue());
    });
    m_List->Bind(wxEVT_LIST_ITEM_SELECTED,
                 [this](wxListEvent&) { UpdateCorridor(); });
    m_List->Bind(wxEVT_LIST_ITEM_DESELECTED,
                 [this](wxListEvent&) { UpdateCorridor(); });
    Bind(wxEVT_TIMER,
         &MultiLegDepartureOptimizationResultsDialog::OnAutoRefresh, this);
    Bind(wxEVT_CLOSE_WINDOW, [this](wxCloseEvent&) { CloseDialog(); });
    Populate();
    UpdateAutoRefresh();
  }

  ~MultiLegDepartureOptimizationResultsDialog() override {
    StopAutoRefresh();
    CloseCorridor("multi_leg_results_destroyed");
  }

private:
  static const int AUTO_REFRESH_INTERVAL_MS = 1000;
  static const int AUTO_REFRESH_MAX_COUNT = 7200;

  int SelectedCandidateIndex() const {
    long selected =
        m_List->GetNextItem(-1, wxLIST_NEXT_ALL, wxLIST_STATE_SELECTED);
    return selected == -1 ? -1 : (int)selected;
  }

  bool ApplyBest() {
    if (!m_WeatherRouting) return false;
    m_WeatherRouting->HideStabilityCorridor("multi_leg_candidate_applied");
    bool applied = m_WeatherRouting->ApplyBestMultiLegOptimizationCandidate();
    Populate();
    UpdateAutoRefresh();
    return applied;
  }

  bool ApplySelected() {
    if (!m_WeatherRouting) return false;
    int index = SelectedCandidateIndex();
    if (index < 0) {
      wxMessageBox(_("Select a complete candidate first."),
                   _("Weather Routing"), wxOK | wxICON_WARNING, this);
      return false;
    }
    m_WeatherRouting->HideStabilityCorridor("multi_leg_candidate_applied");
    bool applied = m_WeatherRouting->ApplyMultiLegOptimizationCandidate(index);
    Populate();
    UpdateAutoRefresh();
    return applied;
  }

  void DiscardAndClose() {
    StopAutoRefresh();
    if (m_WeatherRouting) {
      m_WeatherRouting->HideStabilityCorridor("multi_leg_candidates_discarded");
      m_WeatherRouting->CloseMultiLegDepartureOptimizationResults();
    }
    EndModal(wxID_OK);
  }

  void CloseDialog() {
    if (m_WeatherRouting &&
        m_WeatherRouting->MultiLegDepartureOptimizationActive()) {
      wxMessageDialog confirm(
          this,
          _("Multi-leg departure optimisation is still running. Cancel it and "
            "discard temporary candidate legs?"),
          _("Weather Routing"), wxYES_NO | wxNO_DEFAULT | wxICON_QUESTION);
      if (confirm.ShowModal() != wxID_YES) return;
      DiscardAndClose();
      return;
    }

    if (m_WeatherRouting &&
        m_WeatherRouting->AppliedMultiLegOptimizationCandidateIndex() < 0 &&
        m_WeatherRouting->HasCompleteMultiLegOptimizationCandidate()) {
      wxMessageDialog confirm(
          this,
          _("Apply the best complete multi-leg departure candidate before "
            "closing?\n\nYes: apply best candidate\nNo: discard temporary "
            "candidate legs\nCancel: return to results"),
          _("Weather Routing"),
          wxYES_NO | wxCANCEL | wxCANCEL_DEFAULT | wxICON_QUESTION);
      int answer = confirm.ShowModal();
      if (answer == wxID_CANCEL) return;
      if (answer == wxID_YES && !ApplyBest()) return;
    }

    StopAutoRefresh();
    if (m_WeatherRouting) {
      CloseCorridor("multi_leg_results_closed");
      m_WeatherRouting->CloseMultiLegDepartureOptimizationResults();
    }
    EndModal(wxID_OK);
  }

  void StopAutoRefresh() {
    if (m_AutoRefreshTimer.IsRunning()) m_AutoRefreshTimer.Stop();
  }

  void CloseCorridor(const wxString& reason) {
    if (m_CloseHandled || !m_WeatherRouting) return;
    m_CloseHandled = true;
    m_WeatherRouting->CloseStabilityCorridorResults(
        m_ShowCorridor->GetValue() && m_KeepCorridor->GetValue(), reason);
  }

  void UpdateAutoRefresh() {
    if (m_WeatherRouting &&
        m_WeatherRouting->MultiLegDepartureOptimizationActive() &&
        m_AutoRefreshCount < AUTO_REFRESH_MAX_COUNT) {
      if (!m_AutoRefreshTimer.IsRunning())
        m_AutoRefreshTimer.Start(AUTO_REFRESH_INTERVAL_MS);
    } else {
      StopAutoRefresh();
    }
  }

  void OnAutoRefresh(wxTimerEvent&) {
    m_AutoRefreshCount++;
    Populate();
    UpdateAutoRefresh();
  }

  wxString FormatOffset(int minutes) const {
    wxString sign = minutes < 0 ? _T("-") : _T("+");
    int absMinutes = abs(minutes);
    return wxString::Format(_T("%s%d:%02d"), sign, absMinutes / 60,
                            absMinutes % 60);
  }

  wxString FormatElapsedSeconds(long seconds) const {
    if (seconds < 0) return _("N/A");
    int days = seconds / 86400;
    seconds %= 86400;
    int hours = seconds / 3600;
    seconds %= 3600;
    int minutes = seconds / 60;
    if (days)
      return wxString::Format(_T("%dd %02d:%02d"), days, hours, minutes);
    return wxString::Format(_T("%02d:%02d"), hours, minutes);
  }

  wxString FormatTime(wxDateTime time) const {
    if (!time.IsValid()) return _("N/A");
    return m_WeatherRouting
               ? m_WeatherRouting->m_SettingsDialog.FormatTime(
                     time, _T("%x %H:%M"))
               : marine_time::FormatInTimeZone(time, _T("%x %H:%M"), "UTC");
  }

  void SetCell(long row, int col, const wxString& value) {
    m_List->SetItem(row, col, value);
    m_List->SetColumnWidth(col, wxLIST_AUTOSIZE_USEHEADER);
  }

  void UpdateCorridor() {
    if (m_UpdatingSelection || !m_WeatherRouting) return;
    if (!m_ShowCorridor->GetValue()) {
      m_KeepCorridor->Enable(false);
      m_WeatherRouting->HideStabilityCorridor("checkbox_disabled");
      if (m_ShowCorridor->IsEnabled())
        m_CorridorStatus->SetLabel(
            _("Select a completed route to display its stability family."));
      return;
    }
    const int selected = SelectedCandidateIndex();
    const auto& candidates = m_WeatherRouting->MultiLegOptimizationCandidates();
    if (selected < 0 || selected >= static_cast<int>(candidates.size()) ||
        !candidates[selected].complete) {
      m_KeepCorridor->Enable(false);
      m_WeatherRouting->HideStabilityCorridor("selection_not_complete");
      m_CorridorStatus->SetLabel(
          _("A completed route is required to display a stability corridor."));
      return;
    }
    std::vector<std::vector<RouteMapOverlay*> > routes;
    routes.reserve(candidates.size());
    for (const auto& candidate : candidates) routes.push_back(candidate.routes);
    m_CorridorStatus->SetLabel(_("Calculating stability corridor..."));
    Layout();
    Update();
    wxString status;
    if (!m_WeatherRouting->ShowMultiLegStabilityCorridor(
            routes, static_cast<size_t>(selected), &status)) {
      m_KeepCorridor->Enable(false);
      m_CorridorStatus->SetLabel(
          status.IsEmpty() ? _("No stability corridor is available.") : status);
      return;
    }
    m_KeepCorridor->Enable(true);
    m_CorridorStatus->SetLabel(status);
  }

  void Populate() {
    const int selectedCandidate = SelectedCandidateIndex();
    m_UpdatingSelection = true;
    m_List->DeleteAllItems();
    if (!m_WeatherRouting) {
      m_UpdatingSelection = false;
      return;
    }

    const auto& candidates = m_WeatherRouting->MultiLegOptimizationCandidates();
    int completeRoutes = 0;
    for (size_t candidateIndex = 0; candidateIndex < candidates.size();
         ++candidateIndex) {
      const auto& candidate = candidates[candidateIndex];
      if (candidate.complete) ++completeRoutes;
      wxString marker;
      if (candidate.applied)
        marker = _("Applied");
      else if (candidate.best)
        marker = _("Best");
      long row = m_List->InsertItem(m_List->GetItemCount(), marker);
      SetCell(row, 1, FormatOffset(candidate.offsetMinutes));
      SetCell(row, 2, FormatTime(candidate.departureTime));
      SetCell(row, 3,
              candidate.complete ? FormatTime(candidate.finalEta) : _("N/A"));
      SetCell(row, 4, FormatElapsedSeconds(candidate.totalElapsedSeconds));
      SetCell(row, 5,
              candidate.complete
                  ? wxString::Format(_T("%.0f NM"), candidate.totalDistance)
                  : _("N/A"));
      SetCell(row, 6,
              wxString::Format(_T("%d/%d"), candidate.completedLegs,
                               candidate.totalLegs));
      SetCell(row, 7, candidate.state);
      wxString reason = candidate.reason;
      if (!candidate.failedLegName.IsEmpty()) {
        if (!reason.IsEmpty())
          reason = candidate.failedLegName + _T(": ") + reason;
        else
          reason = candidate.failedLegName;
      }
      SetCell(row, 8, reason);
      if (static_cast<int>(candidateIndex) == selectedCandidate)
        m_List->SetItemState(row, wxLIST_STATE_SELECTED, wxLIST_STATE_SELECTED);
    }
    const bool available =
        !m_WeatherRouting->MultiLegDepartureOptimizationActive() &&
        completeRoutes >= 3;
    m_ShowCorridor->Enable(available);
    m_KeepCorridor->Enable(available && m_ShowCorridor->GetValue());
    if (!available) {
      if (m_ShowCorridor->GetValue()) m_ShowCorridor->SetValue(false);
      m_WeatherRouting->HideStabilityCorridor(
          m_WeatherRouting->MultiLegDepartureOptimizationActive()
              ? "optimization_running"
              : "too_few_completed_routes");
      m_CorridorStatus->SetLabel(
          m_WeatherRouting->MultiLegDepartureOptimizationActive()
              ? _("Waiting for completed routes...")
              : _("No stability corridor: fewer than 3 completed routes."));
    } else if (selectedCandidate < 0) {
      m_CorridorStatus->SetLabel(
          _("Select a completed route to display its stability family."));
    }
    m_UpdatingSelection = false;
    if (available && m_ShowCorridor->GetValue()) UpdateCorridor();
  }

  WeatherRouting* m_WeatherRouting;
  wxListCtrl* m_List;
  wxTimer m_AutoRefreshTimer;
  int m_AutoRefreshCount;
  wxCheckBox* m_ShowCorridor;
  wxCheckBox* m_KeepCorridor;
  wxStaticText* m_CorridorStatus;
  bool m_UpdatingSelection;
  bool m_CloseHandled;
};

void WeatherRouting::ShowMultiLegDepartureOptimizationResults() {
  MultiLegDepartureOptimizationResultsDialog dialog(this);
  dialog.ShowModal();
}

bool WeatherRouting::ComputeDepartureTimeOptimization(
    RouteMapOverlay* routemapoverlay) {
  if (!routemapoverlay) return false;

  RouteMapConfiguration base = routemapoverlay->GetConfiguration();
  // Arrival-time routing has its own adaptive, reverse-guided departure
  // search. It must remain one route calculation rather than being expanded
  // into the fixed departure-optimisation batch.
  if (base.TimeMode == RouteMapConfiguration::ROUTE_BY_ARRIVAL_TIME)
    return false;
  if (!base.DepartureTimeOptimizationEnabled) return false;
  if (weather_routing::ShouldPromoteDepartureOptimizationCandidate(
          base.DepartureTimeOptimizationEnabled,
          base.DepartureTimeOptimizationCandidate)) {
    wxLogMessage(
        "WR_DEPARTURE_PROMOTE source=scheduler old_group=\"%s\" "
        "old_offset_minutes=%d start=\"%s\" end=\"%s\".",
        base.DepartureTimeOptimizationGroupId,
        base.DepartureTimeOptimizationOffsetMinutes, base.Start, base.End);
    base.PromoteDepartureTimeOptimizationCandidate();
    routemapoverlay->SetConfiguration(base);
  }

  int rangeMinutes = base.DepartureTimeOptimizationRangeMinutes;
  int stepMinutes = base.DepartureTimeOptimizationStepMinutes;
  if (rangeMinutes < 0) {
    wxMessageDialog mdlg(this,
                         _("Departure optimisation range must not be "
                           "negative."),
                         _("Weather Routing"), wxOK | wxICON_ERROR);
    mdlg.ShowModal();
    return true;
  }
  if (stepMinutes <= 0) {
    wxMessageDialog mdlg(this,
                         _("Departure optimisation step must be greater "
                           "than zero."),
                         _("Weather Routing"), wxOK | wxICON_ERROR);
    mdlg.ShowModal();
    return true;
  }

  std::vector<int> offsets;
  for (int offset = -rangeMinutes; offset <= rangeMinutes;
       offset += stepMinutes)
    offsets.push_back(offset);
  offsets.push_back(0);
  std::sort(offsets.begin(), offsets.end());
  offsets.erase(std::unique(offsets.begin(), offsets.end()), offsets.end());
  weather_routing::OrderDepartureOffsets(offsets);

  if (offsets.size() > MAX_DEPARTURE_OPTIMIZATION_CANDIDATES) {
    wxMessageDialog mdlg(
        this,
        wxString::Format(
            _("Departure optimisation would create %d route calculations. "
              "Increase the step or reduce the range. The current limit is "
              "%d."),
            (int)offsets.size(), MAX_DEPARTURE_OPTIMIZATION_CANDIDATES),
        _("Weather Routing"), wxOK | wxICON_ERROR);
    mdlg.ShowModal();
    return true;
  }

  std::list<RouteMapOverlay*> oldOptimizationRoutes;
  for (auto it = m_WeatherRoutes.begin(); it != m_WeatherRoutes.end(); it++) {
    RouteMapConfiguration configuration =
        (*it)->routemapoverlay->GetConfiguration();
    if (configuration.DepartureTimeOptimizationCandidate)
      oldOptimizationRoutes.push_back((*it)->routemapoverlay);
  }
  for (auto routemap : oldOptimizationRoutes)
    if (routemap) Stop(routemap);
  DeleteRouteMaps(oldOptimizationRoutes);
  m_DepartureOptimizationRoutes.clear();

  wxString groupId = NewDepartureOptimizationGroupId();
  wxDateTime nominalStartTime = base.StartTime;
  std::vector<RouteMapOverlay*> candidate_routes;
  for (auto offset : offsets) {
    RouteMapConfiguration candidate = base;
    candidate.DepartureTimeOptimizationEnabled = false;
    candidate.DepartureTimeOptimizationCandidate = true;
    candidate.DepartureTimeOptimizationNominalStartTime = nominalStartTime;
    candidate.DepartureTimeOptimizationOffsetMinutes = offset;
    candidate.DepartureTimeOptimizationGroupId = groupId;
    candidate.StartTime = nominalStartTime + wxTimeSpan::Minutes(offset);
    // GSHHS scouts describe the union of time-dependent search envelopes.
    // Every deliverable candidate starts against the shared authoritative
    // chart raster; a scout result is never treated as a navigable route.
    candidate.ChartSafetyPropagationFallbackTried = false;
    ApplyAuthoritativeChartSearchPolicy(candidate);
    candidate.chart_safety_missing_tile_retry_count = 0;
    candidate.chart_safety_missing_tile_rejections = 0;
    candidate.chart_safety_missing_tile_first_lat_tile = 0;
    candidate.chart_safety_missing_tile_first_lon_tile = 0;
    candidate.chart_safety_missing_tile_first_min_lat = NAN;
    candidate.chart_safety_missing_tile_first_min_lon = NAN;
    candidate.chart_safety_missing_tile_min_lat = NAN;
    candidate.chart_safety_missing_tile_max_lat = NAN;
    candidate.chart_safety_missing_tile_min_lon = NAN;
    candidate.chart_safety_missing_tile_max_lon = NAN;

    if (!AddConfiguration(candidate)) continue;
    RouteMapOverlay* candidateRoute = m_WeatherRoutes.back()->routemapoverlay;
    candidateRoute->LoadBoat();
    candidateRoute->ResetFinished();
    m_DepartureOptimizationRoutes.push_back(candidateRoute);
    candidate_routes.push_back(candidateRoute);
  }

  const int logical_cpu_count = wxThread::GetCPUCount();
  const bool deterministic_host_service_lane =
      ModernNativeRouteRequiresSerialHostServices(base);
  const bool authoritative_chart_search =
      !candidate_routes.empty() && candidate_routes.front() &&
      candidate_routes.front()
          ->GetConfiguration()
          .UseChartSafetyForPropagation;
  const int effective_workers = weather_routing::EffectiveRouteWorkerLimit(
      m_SettingsDialog.m_sConcurrentThreads->GetValue(), true,
      base.DepartureTimeOptimizationConcurrentRoutes, logical_cpu_count,
      deterministic_host_service_lane, authoritative_chart_search);
  wxLogMessage(
      "WR_DEPARTURE_SCHEDULER group=%s candidates=%lu requested=%d "
      "logical_cpus=%d global_limit=%d effective=%d "
      "deterministic_host_service_lane=%d authoritative_chart_search=%d",
      groupId, static_cast<unsigned long>(candidate_routes.size()),
      base.DepartureTimeOptimizationConcurrentRoutes, logical_cpu_count,
      m_SettingsDialog.m_sConcurrentThreads->GetValue(), effective_workers,
      deterministic_host_service_lane ? 1 : 0,
      authoritative_chart_search ? 1 : 0);
  // Collect every departure's weather/current-driven frontier before starting
  // production workers. Their union prepares one shared static land/depth
  // raster without collapsing the candidates' distinct temporal searches.
  PrepareChartSafetyScoutEnvelopes(candidate_routes,
                                   _("departure optimisation scouts"));
  for (std::vector<RouteMapOverlay*>::iterator route = candidate_routes.begin();
       route != candidate_routes.end(); ++route)
    Start(*route);

  return true;
}

void WeatherRouting::StartCurrentRouteComputations() {
  std::list<RouteMapOverlay*> currentroutemaps = CurrentRouteMaps();
  wxDateTime optimizationNominalStartTime;
  bool showOptimizationResults = false;
  for (auto it = currentroutemaps.begin(); it != currentroutemaps.end(); it++) {
    RouteMapConfiguration configuration = (*it)->GetConfiguration();
    if (ComputeDepartureTimeOptimization(*it)) {
      optimizationNominalStartTime = configuration.StartTime;
      showOptimizationResults = true;
    } else {
      configuration.ChartSafetyPropagationFallbackTried = false;
      ApplyAuthoritativeChartSearchPolicy(configuration);
      configuration.chart_safety_missing_tile_retry_count = 0;
      configuration.chart_safety_missing_tile_rejections = 0;
      configuration.chart_safety_missing_tile_first_lat_tile = 0;
      configuration.chart_safety_missing_tile_first_lon_tile = 0;
      configuration.chart_safety_missing_tile_first_min_lat = NAN;
      configuration.chart_safety_missing_tile_first_min_lon = NAN;
      configuration.chart_safety_missing_tile_min_lat = NAN;
      configuration.chart_safety_missing_tile_max_lat = NAN;
      configuration.chart_safety_missing_tile_min_lon = NAN;
      configuration.chart_safety_missing_tile_max_lon = NAN;
      (*it)->SetConfiguration(configuration);
      Start(*it);
    }
  }
  UpdateComputeState();
  if (showOptimizationResults && !m_DepartureOptimizationRoutes.empty())
    ShowDepartureTimeOptimizationResults(m_DepartureOptimizationRoutes,
                                         optimizationNominalStartTime);
}

void WeatherRouting::StartAllRouteComputations() {
  std::vector<RouteMapOverlay*> all_routes;
  for (int i = 0; i < m_panel->m_lWeatherRoutes->GetItemCount(); i++) {
    WeatherRoute* weatherroute = reinterpret_cast<WeatherRoute*>(
        wxUIntToPtr(m_panel->m_lWeatherRoutes->GetItemData(i)));
    if (!weatherroute || !weatherroute->routemapoverlay) continue;
    RouteMapConfiguration configuration =
        weatherroute->routemapoverlay->GetConfiguration();
    configuration.ChartSafetyPropagationFallbackTried = false;
    ApplyAuthoritativeChartSearchPolicy(configuration);
    configuration.chart_safety_missing_tile_retry_count = 0;
    configuration.chart_safety_missing_tile_rejections = 0;
    configuration.chart_safety_missing_tile_first_lat_tile = 0;
    configuration.chart_safety_missing_tile_first_lon_tile = 0;
    configuration.chart_safety_missing_tile_first_min_lat = NAN;
    configuration.chart_safety_missing_tile_first_min_lon = NAN;
    configuration.chart_safety_missing_tile_min_lat = NAN;
    configuration.chart_safety_missing_tile_max_lat = NAN;
    configuration.chart_safety_missing_tile_min_lon = NAN;
    configuration.chart_safety_missing_tile_max_lon = NAN;
    weatherroute->routemapoverlay->SetConfiguration(configuration);
    all_routes.push_back(weatherroute->routemapoverlay);
  }
  PrepareChartSafetyScoutEnvelopes(all_routes, _("compute all scouts"));
  StartAll();
  UpdateComputeState();
}

void WeatherRouting::OnCompute(wxCommandEvent& event) {
  CancelMultiLegSequence();
  CancelMultiLegDepartureOptimization(true);
  std::list<RouteMapOverlay*> currentroutemaps = CurrentRouteMaps();
  if (ShouldShowChartSafetyComputeProgress(currentroutemaps)) {
    if (m_DeferredRoutingStartPending) {
      wxMessageBox(_("A weather routing start is already pending."),
                   _("Weather Routing"), wxOK | wxICON_WARNING, this);
      return;
    }
    BeginChartSafetyComputeProgress(false, currentroutemaps);
    ScheduleDeferredRoutingStart(DEFERRED_ROUTING_COMPUTE_CURRENT,
                                 wxEmptyString);
    return;
  }
  StartCurrentRouteComputations();
}

void WeatherRouting::OnComputeMultiLegSequence(wxCommandEvent& event) {
  CancelMultiLegDepartureOptimization(true);
  RouteMapOverlay* selected = FirstCurrentRouteMap();
  if (!selected) {
    wxMessageBox(_("Select a generated multi-leg route row first."),
                 _("Weather Routing"), wxOK | wxICON_WARNING, this);
    return;
  }
  ComputeMultiLegSequence(selected);
}

void WeatherRouting::OnOptimizeMultiLegDeparture(wxCommandEvent& event) {
  RouteMapOverlay* selected = FirstCurrentRouteMap();
  if (!selected) {
    wxMessageBox(_("Select a generated multi-leg route row first."),
                 _("Weather Routing"), wxOK | wxICON_WARNING, this);
    return;
  }
  ComputeMultiLegDepartureOptimization(selected);
}

void WeatherRouting::OnEditMultiLegGroupSettings(wxCommandEvent& event) {
  RouteMapOverlay* selected = FirstCurrentRouteMap();
  if (!selected) {
    wxMessageBox(_("Select a generated multi-leg route row first."),
                 _("Weather Routing"), wxOK | wxICON_WARNING, this);
    return;
  }
  EditMultiLegGroupSettings(selected);
}

void WeatherRouting::OnShowRoutingStatus(wxCommandEvent& event) {
  ShowRoutingStatus(FirstCurrentRouteMap());
}

void WeatherRouting::OnComputeAll(wxCommandEvent& event) {
  CancelMultiLegSequence();
  CancelMultiLegDepartureOptimization(true);
  std::list<RouteMapOverlay*> allroutemaps;
  for (int i = 0; i < m_panel->m_lWeatherRoutes->GetItemCount(); i++) {
    WeatherRoute* weatherroute = reinterpret_cast<WeatherRoute*>(
        wxUIntToPtr(m_panel->m_lWeatherRoutes->GetItemData(i)));
    if (weatherroute && weatherroute->routemapoverlay)
      allroutemaps.push_back(weatherroute->routemapoverlay);
  }
  if (ShouldShowChartSafetyComputeProgress(allroutemaps)) {
    if (m_DeferredRoutingStartPending) {
      wxMessageBox(_("A weather routing start is already pending."),
                   _("Weather Routing"), wxOK | wxICON_WARNING, this);
      return;
    }
    BeginChartSafetyComputeProgress(true, allroutemaps);
    ScheduleDeferredRoutingStart(DEFERRED_ROUTING_COMPUTE_ALL, wxEmptyString);
    return;
  }
  StartAllRouteComputations();
}

void WeatherRouting::OnStop(wxCommandEvent& event) {
  CancelDeferredRoutingStart();
  CancelMultiLegSequence();
  CancelMultiLegDepartureOptimization(true);
  std::list<RouteMapOverlay*> currentroutemaps = CurrentRouteMaps();
  for (auto it = currentroutemaps.begin(); it != currentroutemaps.end(); it++)
    Stop(*it);
  UpdateComputeState();
}

#define FAIL(X)  \
  do {           \
    error = X;   \
    goto failed; \
  } while (0)
void WeatherRouting::OnOpen(wxCommandEvent& event) {
  wxString error;
  wxFileDialog openDialog(
      this, _("Select Configuration"), m_FileName.GetPath(),
      m_FileName.GetName(),
      wxT("XML files (*.xml)|*.XML;*.xml|All files (*.*)|*.*"), wxFD_OPEN);

  if (openDialog.ShowModal() == wxID_OK) {
    wxCommandEvent event;
    OnDeleteAllPositions(event);
    OnDeleteAll(event);
    OpenXML(openDialog.GetPath());
  }
}

void WeatherRouting::OnSave(wxCommandEvent& event) {
  if (m_FileName.GetFullPath().IsEmpty()) {
    // No file path set yet, behave like Save As
    OnSaveAs(event);
    return;
  }

  SaveXML(m_FileName.GetFullPath());
  m_tAutoSaveXML
      .Stop();  // Stop any pending auto-save since we just manually saved
}

void WeatherRouting::OnSaveAs(wxCommandEvent& event) {
  wxString error;
  wxFileDialog saveDialog(
      this, _("Select Configuration"), m_FileName.GetPath(),
      m_FileName.GetName(),
      wxT("XML files (*.xml)|*.XML;*.xml|All files (*.*)|*.*"),
      wxFD_SAVE | wxFD_OVERWRITE_PROMPT);

  if (saveDialog.ShowModal() == wxID_OK) {
    // Use wxFileDialog::AppendExtension to ensure the file has the .xml
    // extension
    wxString filename =
        wxFileDialog::AppendExtension(saveDialog.GetPath(), _T("*.xml"));

    SaveXML(filename);
    m_tAutoSaveXML
        .Stop();  // Stop any pending auto-save since we just manually saved
  }
}

void WeatherRouting::OnClose(wxCommandEvent& event) { Hide(); }

void WeatherRouting::OnCollPaneChanged(wxCollapsiblePaneEvent& event) {
  if (m_colpane && m_colpane->IsExpanded())
    SetSize(m_size);
  else if (m_colpane)
    Fit();
  Update();
  Layout();
}

void WeatherRouting::OnSize(wxSizeEvent& event) {
  if (m_colpane && m_colpane->IsExpanded()) {
    Update();
    Layout();
    m_size = GetSize();
  } else {
    if (m_colpane) Fit();
  }
  event.Skip();
}

void WeatherRouting::OnNew(wxCommandEvent& event) {
  RouteMapConfiguration configuration;
  if (FirstCurrentRouteMap())
    configuration = FirstCurrentRouteMap()->GetConfiguration();
  else
    configuration = DefaultConfiguration();

  AddConfiguration(configuration);

  // deselect all
  for (int i = 0; i < m_panel->m_lWeatherRoutes->GetItemCount(); i++)
    m_panel->m_lWeatherRoutes->SetItemState(i, 0, wxLIST_STATE_SELECTED);

  m_panel->m_lWeatherRoutes->SetItemState(
      m_panel->m_lWeatherRoutes->GetItemCount() - 1, wxLIST_STATE_SELECTED,
      wxLIST_STATE_SELECTED);
  OnEditConfiguration();
}

void WeatherRouting::OnBatch(wxCommandEvent& event) {
  if (m_ConfigurationBatchDialog.IsShown()) return;

  m_ConfigurationBatchDialog.Reset();
  m_ConfigurationBatchDialog.Show();
}

void WeatherRouting::GenerateBatch() {
  std::list<RouteMapOverlay*> routemapoverlays = CurrentRouteMaps(true);

  wxProgressDialog* progressdialog = NULL;
  int count = routemapoverlays.size(), c = 0;
  int times = 0;

  wxTimeSpan StartSpan, StartSpacingSpan;
  double days, hours;

  ConfigurationBatchDialog& dlg = m_ConfigurationBatchDialog;
  dlg.m_tStartDays->GetValue().ToDouble(&days);
  StartSpan = wxTimeSpan::Days(days);

  dlg.m_tStartHours->GetValue().ToDouble(&hours);
  StartSpan += wxTimeSpan::Seconds(3600 * hours);

  dlg.m_tStartSpacingDays->GetValue().ToDouble(&days);
  StartSpacingSpan = wxTimeSpan::Days(days);

  dlg.m_tStartSpacingHours->GetValue().ToDouble(&hours);
  StartSpacingSpan += wxTimeSpan::Seconds(3600 * hours);

  if (!StartSpacingSpan.GetSeconds().ToLong()) {
    wxMessageDialog mdlg(this, _("Zero time span forbidden, aborting."),
                         _("Weather Routing"), wxOK | wxICON_ERROR);
    mdlg.ShowModal();
    return;
  }

  wxDateTime StartTime = wxDateTime::Now(), EndTime = StartTime + StartSpan;

  for (wxDateTime start = StartTime; start <= EndTime;
       start += StartSpacingSpan)
    times++;

  int sources = 0;
  for (std::vector<BatchSource*>::iterator it = dlg.sources.begin();
       it != dlg.sources.end(); it++)
    for (std::list<BatchDestination*>::iterator it2 =
             (*it)->destinations.begin();
         it2 != (*it)->destinations.end(); it2++)
      sources++;

  count *= sources;
  count *= dlg.m_lBoats->GetCount();

  if (count > 10) {
    progressdialog = new wxProgressDialog(
        _("Batch configuration"), _("Weather Routing"), count, this,
        wxPD_CAN_ABORT | wxPD_ELAPSED_TIME | wxPD_REMAINING_TIME);
  }

  for (std::list<RouteMapOverlay*>::iterator it = routemapoverlays.begin();
       it != routemapoverlays.end(); it++) {
    RouteMapConfiguration configuration = (*it)->GetConfiguration();

    EndTime = configuration.StartTime + StartSpan;

    for (; configuration.StartTime <= EndTime;
         configuration.StartTime += StartSpacingSpan) {
      for (std::vector<BatchSource*>::iterator it = dlg.sources.begin();
           it != dlg.sources.end(); it++) {
        configuration.Start = (*it)->Name;

        for (std::list<BatchDestination*>::iterator it2 =
                 (*it)->destinations.begin();
             it2 != (*it)->destinations.end(); it2++) {
          configuration.End = (*it2)->Name;

          for (unsigned int boatindex = 0; boatindex < dlg.m_lBoats->GetCount();
               boatindex++) {
            configuration.boatFileName = dlg.m_lBoats->GetString(boatindex);

            for (int windstrength = dlg.m_sWindStrengthMin->GetValue();
                 windstrength <= dlg.m_sWindStrengthMax->GetValue();
                 windstrength += dlg.m_sWindStrengthStep->GetValue()) {
              configuration.WindStrength = windstrength / 100.0;

              AddConfiguration(configuration);
              m_WeatherRoutes.back()->routemapoverlay->LoadBoat();
              configuration =
                  m_WeatherRoutes.back()->routemapoverlay->GetConfiguration();

              if (progressdialog && !progressdialog->Update(c++)) goto abort;
            }
          }
        }
      }
    }
  }
abort:
  DeleteRouteMaps(routemapoverlays);

  delete progressdialog;
}

bool WeatherRouting::Show(bool show) {
  m_weather_routing_pi.ShowMenuItems(show);

  if (show) {
    m_ConfigurationDialog.Show(m_bShowConfiguration);
    m_ConfigurationBatchDialog.Show(m_bShowConfigurationBatch);
    m_SettingsDialog.Show(m_bShowSettings);
    m_StatisticsDialog.Show(m_bShowStatistics);
    m_ReportDialog.Show(m_bShowReport);
    m_PlotDialog.Show(m_bShowPlot);
    m_FilterRoutesDialog.Show(m_bShowFilter);
    m_RoutePositionDialog.Show(m_bShowRoutePosition);
  } else {
    m_bShowConfiguration = m_ConfigurationDialog.IsShown();
    m_ConfigurationDialog.Hide();

    m_bShowConfigurationBatch = m_ConfigurationBatchDialog.IsShown();
    m_ConfigurationBatchDialog.Hide();

    m_bShowSettings = m_SettingsDialog.IsShown();
    m_SettingsDialog.Hide();

    m_bShowStatistics = m_StatisticsDialog.IsShown();
    m_StatisticsDialog.Hide();

    m_bShowReport = m_ReportDialog.IsShown();
    m_ReportDialog.Hide();

    m_bShowPlot = m_PlotDialog.IsShown();
    m_PlotDialog.Hide();

    m_bShowFilter = m_FilterRoutesDialog.IsShown();
    m_FilterRoutesDialog.Hide();

    m_bShowRoutePosition = m_RoutePositionDialog.IsShown();
    m_RoutePositionDialog.Hide();

    // Hide routing table panel if it exists
    if (m_RoutingTablePanel) {
      wxAuiManager* pauimgr = ::GetFrameAuiManager();
      wxAuiPaneInfo& pane = pauimgr->GetPane(m_RoutingTablePanel);
      if (pane.IsOk() && pane.IsShown()) pane.Hide();
      pauimgr->Update();
    }
  }

  return WeatherRoutingBase::Show(show);
}

void WeatherRouting::OnFilter(wxCommandEvent& event) {
  m_FilterRoutesDialog.Show();
}

void WeatherRouting::OnResetAll(wxCommandEvent& event) {
  CancelMultiLegSequence();
  CancelMultiLegDepartureOptimization(true);
  m_StatisticsDialog.SetRunTime(m_RunTime = wxTimeSpan(0));
  Reset();
  UpdateStates();
}

void WeatherRouting::OnSaveAsTrack(wxCommandEvent& event) {
  std::list<RouteMapOverlay*> routemapoverlays = CurrentRouteMaps(true);
  for (std::list<RouteMapOverlay*>::iterator it = routemapoverlays.begin();
       it != routemapoverlays.end(); it++)
    SaveAsTrack(**it);
}

void WeatherRouting::OnSimplifyRoute(wxCommandEvent& event) {
  std::list<RouteMapOverlay*> selected = CurrentRouteMaps(true);
  if (selected.empty()) return;
  std::vector<RouteMapOverlay*> routes(selected.begin(), selected.end());

  for (size_t i = 0; i < routes.size(); ++i) {
    if (!routes[i]->Finished() || !routes[i]->ReachedDestination()) {
      wxMessageDialog dialog(
          this,
          _("Every selected weather-route leg must be complete before the "
            "passage can be simplified."),
          _("Simplify weather route"), wxOK | wxICON_WARNING);
      dialog.ShowModal();
      return;
    }
    if (!ValidateRouteForOutput(*routes[i], _("Simplify route"))) return;

    if (i > 0) {
      const RouteMapConfiguration previous = routes[i - 1]->GetConfiguration();
      const RouteMapConfiguration current = routes[i]->GetConfiguration();
      const double join_distance = DistGreatCircle_Plugin(
          previous.EndLat, previous.EndLon, current.StartLat, current.StartLon);
      if (join_distance > 0.05) {
        wxMessageDialog dialog(
            this,
            _("The selected routes do not form one contiguous multi-waypoint "
              "passage. Select consecutive legs in route order."),
            _("Simplify weather route"), wxOK | wxICON_WARNING);
        dialog.ShowModal();
        return;
      }
      if (previous.DetectLand != current.DetectLand ||
          std::fabs(previous.SafetyMarginLand - current.SafetyMarginLand) >
              1e-6 ||
          std::fabs(previous.MinimumDepthMeters -
                    current.MinimumDepthMeters) > 1e-6) {
        wxMessageDialog dialog(this,
                               _("Selected legs must use the same "
                                 "land-detection, safety-margin and "
                                 "minimum-depth "
                                 "settings before they can be combined."),
                               _("Simplify weather route"),
                               wxOK | wxICON_WARNING);
        dialog.ShowModal();
        return;
      }
    }
  }

  RouteSimplificationDialog dialog(
      this, [this, routes](const RouteSimplificationOptions& options) {
        return SimplifyOutputRoutes(routes, options);
      });
  if (dialog.ShowModal() != wxID_APPLY) return;

  if (routes.size() == 1) {
    SimplifiedRouteState state;
    state.original_fingerprint =
        RouteGeometryFingerprint(FullOutputRoute(*routes.front()));
    state.options = dialog.Options();
    state.result = dialog.Result();
    m_SimplifiedRoutes[routes.front()] = state;
  } else {
    m_SimplifiedRouteGroup = SimplifiedRouteGroupState();
    m_SimplifiedRouteGroup.valid = true;
    m_SimplifiedRouteGroup.routes = routes;
    m_SimplifiedRouteGroup.options = dialog.Options();
    m_SimplifiedRouteGroup.result = dialog.Result();
    for (size_t i = 0; i < routes.size(); ++i)
      m_SimplifiedRouteGroup.fingerprints.push_back(
          RouteGeometryFingerprint(FullOutputRoute(*routes[i])));
  }
  const RouteMapConfiguration first = routes.front()->GetConfiguration();
  const RouteMapConfiguration last = routes.back()->GetConfiguration();
  wxLogMessage(
      "WR_ROUTE_SIMPLIFY stored route=\"%s -> %s\" legs=%lu original_points=%d "
      "simplified_points=%d max_deviation_nm=%.3f eta_change_seconds=%.1f "
      "safety_checks=%d feasibility_checks=%d",
      first.Start, last.End, static_cast<unsigned long>(routes.size()),
      dialog.Result().original_points, dialog.Result().simplified_points,
      dialog.Result().max_deviation_nm,
      dialog.Result().estimated_eta_change_seconds,
      dialog.Result().safety_checks, dialog.Result().feasibility_checks);
}

void WeatherRouting::OnSaveAsRoute(wxCommandEvent& event) {
  std::vector<RouteOutputGroup> groups = SelectedRouteOutputGroups();
  for (size_t i = 0; i < groups.size(); ++i) {
    if (!groups[i].multi_leg) {
      SaveAsRoute(*groups[i].routes.front());
      continue;
    }

    std::vector<PlotData> points;
    bool simplified = false;
    wxString failure_reason;
    if (!PrepareCombinedRouteOutput(groups[i].routes, &points, &simplified,
                                    &failure_reason)) {
      if (!failure_reason.IsEmpty())
        wxMessageBox(failure_reason, _("Save multi-waypoint route"),
                     wxOK | wxICON_WARNING, this);
      continue;
    }
    SaveCombinedRoute(groups[i].routes, points, simplified);
  }
}

void WeatherRouting::OnExportRouteAsGPX(wxCommandEvent& event) {
  std::vector<RouteOutputGroup> groups = SelectedRouteOutputGroups();
  for (size_t i = 0; i < groups.size(); ++i) {
    if (!groups[i].multi_leg) {
      ExportRoute(*groups[i].routes.front());
      continue;
    }

    std::vector<PlotData> points;
    bool simplified = false;
    wxString failure_reason;
    if (!PrepareCombinedRouteOutput(groups[i].routes, &points, &simplified,
                                    &failure_reason)) {
      if (!failure_reason.IsEmpty())
        wxMessageBox(failure_reason, _("Export multi-waypoint route"),
                     wxOK | wxICON_WARNING, this);
      continue;
    }
    ExportCombinedRoute(groups[i].routes, points, simplified);
  }
}

void WeatherRouting::OnSaveAllAsTracks(wxCommandEvent& event) {
  for (int i = 0; i < m_panel->m_lWeatherRoutes->GetItemCount(); i++)
    SaveAsTrack(*reinterpret_cast<WeatherRoute*>(
                     wxUIntToPtr(m_panel->m_lWeatherRoutes->GetItemData(i)))
                     ->routemapoverlay);
}

void WeatherRouting::OnChartAwarenessSettings(wxCommandEvent& event) {
  wxDialog dialog(this, wxID_ANY, _("Chart Awareness Settings"),
                  wxDefaultPosition, wxDefaultSize,
                  wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER);
  wxBoxSizer* top = new wxBoxSizer(wxVERTICAL);

  wxStaticText* capability = new wxStaticText(
      &dialog, wxID_ANY,
      m_weather_routing_pi.HasEnhancedChartSafety()
          ? _("Full chart-aware safety is available and enabled by default. "
              "OpenCPN remains the chart-classification authority; this "
              "plugin caches permission-approved semantic tiles, while "
              "OpenCPN caches certified safe-area proofs.")
          : _("This OpenCPN build does not provide the optional enhanced "
              "chart-safety capability. The plugin remains usable with "
              "standard GSHHS land checks. If chart safety is explicitly "
              "enforced, routing fails closed instead of silently "
              "downgrading."));
  capability->Wrap(470);
  top->Add(capability, 0, wxALL | wxEXPAND, 10);

  wxCheckBox* persistent = new wxCheckBox(
      &dialog, wxID_ANY, _("Keep authoritative chart-safety tiles on disk"));
  persistent->SetValue(m_weather_routing_pi.UsePersistentChartSafeCache());
  top->Add(persistent, 0, wxLEFT | wxRIGHT | wxBOTTOM | wxEXPAND, 10);

  wxStaticBoxSizer* atlas_box = new wxStaticBoxSizer(
      wxVERTICAL, &dialog, _("Optional prebuilt semantic atlas"));
  wxCheckBox* atlas_enabled = new wxCheckBox(
      &dialog, wxID_ANY,
      _("Prebuild selected charts when OpenCPN is idle"));
  atlas_enabled->SetValue(
      m_weather_routing_pi.ChartSafetyAtlasEnabled());
  atlas_enabled->SetToolTip(
      _("This is a performance option. Routes may still request any other "
        "authoritative tile when required."));
  atlas_box->Add(atlas_enabled, 0, wxALL | wxEXPAND, 5);

  wxBoxSizer* quota_row = new wxBoxSizer(wxHORIZONTAL);
  quota_row->Add(new wxStaticText(
                     &dialog, wxID_ANY,
                     _("Maximum semantic tile cache / atlas (MiB):")),
                 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 8);
  wxSpinCtrl* atlas_quota = new wxSpinCtrl(&dialog, wxID_ANY);
  atlas_quota->SetRange(256, 16384);
  atlas_quota->SetIncrement(256);
  atlas_quota->SetValue(
      m_weather_routing_pi.ChartSafetyAtlasMaxDiskMiB());
  quota_row->Add(atlas_quota, 1, wxEXPAND);
  atlas_box->Add(quota_row, 0, wxLEFT | wxRIGHT | wxBOTTOM | wxEXPAND, 5);

  std::vector<weather_routing::ChartSafetyAtlasChart> atlas_charts =
      weather_routing::chart_safety_host::AtlasCharts();
  std::sort(atlas_charts.begin(), atlas_charts.end(),
            [](const auto& first, const auto& second) {
              return first.path < second.path;
            });
  const std::set<std::string>& saved_atlas_paths =
      m_weather_routing_pi.ChartSafetyAtlasSelectedPaths();
  wxCheckBox* atlas_all = new wxCheckBox(
      &dialog, wxID_ANY, _("Prebuild all applicable charts"));
  atlas_all->SetValue(
      m_weather_routing_pi.ChartSafetyAtlasAllCharts());
  atlas_box->Add(atlas_all, 0, wxLEFT | wxRIGHT | wxBOTTOM | wxEXPAND, 5);

  wxArrayString atlas_labels;
  for (const auto& chart : atlas_charts) {
    wxFileName filename(wxString::FromUTF8(chart.path.c_str()));
    atlas_labels.Add(wxString::Format(
        _("%s  (1:%d)"), filename.GetFullName(), chart.chart_scale));
  }
  wxCheckListBox* atlas_list = new wxCheckListBox(
      &dialog, wxID_ANY, wxDefaultPosition, wxSize(540, 180), atlas_labels);
  for (unsigned int index = 0; index < atlas_charts.size(); ++index) {
    const bool checked =
        m_weather_routing_pi.ChartSafetyAtlasAllCharts() ||
        saved_atlas_paths.count(atlas_charts[index].path) != 0;
    atlas_list->Check(index, checked);
  }
  atlas_list->Enable(!atlas_all->GetValue());
  atlas_box->Add(atlas_list, 1, wxLEFT | wxRIGHT | wxBOTTOM | wxEXPAND, 5);

  wxBoxSizer* atlas_actions = new wxBoxSizer(wxHORIZONTAL);
  wxButton* atlas_select_all =
      new wxButton(&dialog, wxID_ANY, _("Select all"));
  wxButton* atlas_select_none =
      new wxButton(&dialog, wxID_ANY, _("Select none"));
  wxButton* atlas_estimate =
      new wxButton(&dialog, wxID_ANY, _("Estimate size"));
  atlas_actions->Add(atlas_select_all, 0, wxRIGHT, 5);
  atlas_actions->Add(atlas_select_none, 0, wxRIGHT, 5);
  atlas_actions->AddStretchSpacer();
  atlas_actions->Add(atlas_estimate, 0);
  atlas_box->Add(atlas_actions, 0, wxLEFT | wxRIGHT | wxBOTTOM | wxEXPAND,
                 5);

  wxStaticText* atlas_size = new wxStaticText(
      &dialog, wxID_ANY, _("Size has not yet been estimated."));
  atlas_size->Wrap(520);
  atlas_box->Add(atlas_size, 0, wxLEFT | wxRIGHT | wxBOTTOM | wxEXPAND, 5);
  wxStaticText* atlas_note = new wxStaticText(
      &dialog, wxID_ANY,
      _("Selection controls proactive generation only. A route outside the "
        "atlas remains eligible and requests authoritative tiles on demand. "
        "The estimate uses the charts' actual coverage polygons."));
  atlas_note->Wrap(520);
  atlas_box->Add(atlas_note, 0, wxLEFT | wxRIGHT | wxBOTTOM | wxEXPAND, 5);
  top->Add(atlas_box, 1, wxLEFT | wxRIGHT | wxBOTTOM | wxEXPAND, 10);

  const auto selected_atlas_paths = [&]() {
    std::set<std::string> selected;
    if (atlas_all->GetValue()) return selected;
    for (unsigned int index = 0; index < atlas_charts.size(); ++index)
      if (atlas_list->IsChecked(index))
        selected.insert(atlas_charts[index].path);
    return selected;
  };
  const auto update_atlas_estimate = [&]() {
    bool coverage_complete = false;
    const auto coverage_tiles =
        weather_routing::chart_safety_host::AtlasCoverageTiles(
            atlas_charts, selected_atlas_paths(), atlas_all->GetValue(),
            2000000, &coverage_complete);
    const weather_routing::ChartSafetyAtlasEstimate estimate =
        weather_routing::EstimateChartSafetyAtlasCoverage(
            atlas_charts, coverage_tiles, selected_atlas_paths(),
            atlas_all->GetValue(), coverage_complete);
    const double compact_mib =
        estimate.compact_bytes / (1024.0 * 1024.0);
    const double recommended_mib =
        estimate.recommended_quota_bytes / (1024.0 * 1024.0);
    const bool fits = estimate.complete &&
                      estimate.recommended_quota_bytes <=
                          static_cast<std::uint64_t>(atlas_quota->GetValue()) *
                              1024ULL * 1024ULL;
    atlas_size->SetLabel(wxString::Format(
        _("%lu of %lu charts; %llu coverage tiles, approximately %.0f MiB "
          "compact (recommended quota %.0f MiB). %s"),
        static_cast<unsigned long>(estimate.selected_charts),
        static_cast<unsigned long>(estimate.available_charts),
        static_cast<unsigned long long>(estimate.upper_bound_tiles),
        compact_mib, recommended_mib,
        !estimate.complete
            ? _("Estimate exceeded its safety limit.")
            : fits ? _("Fits the configured quota.")
                   : _("Does not fit the configured quota.")));
    atlas_size->Wrap(520);
    dialog.Layout();
    dialog.Fit();
  };

  atlas_all->Bind(wxEVT_CHECKBOX, [=](wxCommandEvent&) {
    atlas_list->Enable(!atlas_all->GetValue());
  });
  atlas_select_all->Bind(wxEVT_BUTTON, [=](wxCommandEvent&) {
    atlas_all->SetValue(false);
    atlas_list->Enable(true);
    for (unsigned int index = 0; index < atlas_list->GetCount(); ++index)
      atlas_list->Check(index, true);
  });
  atlas_select_none->Bind(wxEVT_BUTTON, [=](wxCommandEvent&) {
    atlas_all->SetValue(false);
    atlas_list->Enable(true);
    for (unsigned int index = 0; index < atlas_list->GetCount(); ++index)
      atlas_list->Check(index, false);
  });
  atlas_estimate->Bind(wxEVT_BUTTON,
                       [=](wxCommandEvent&) { update_atlas_estimate(); });

  wxBoxSizer* ram_row = new wxBoxSizer(wxHORIZONTAL);
  ram_row->Add(new wxStaticText(&dialog, wxID_ANY,
                                _("Chart-safety RAM cache (MiB):")),
               0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 8);
  wxSpinCtrl* ram = new wxSpinCtrl(&dialog, wxID_ANY);
  ram->SetRange(0, 8192);
  ram->SetIncrement(256);
  ram->SetValue(m_weather_routing_pi.ChartSafetyRamCacheMiB());
  ram->SetToolTip(
      _("0 selects Auto. Auto uses approximately 1/32 of physical RAM, "
        "bounded between 256 and 2048 MiB."));
  ram_row->Add(ram, 1, wxEXPAND);
  top->Add(ram_row, 0, wxLEFT | wxRIGHT | wxBOTTOM | wxEXPAND, 10);

  wxStaticText* effective = new wxStaticText(
      &dialog, wxID_ANY,
      wxString::Format(_("Current effective RAM budget: %d MiB"),
                       m_weather_routing_pi
                           .EffectiveChartSafetyRamCacheMiB()));
  top->Add(effective, 0, wxLEFT | wxRIGHT | wxBOTTOM | wxEXPAND, 10);

  wxButton* clear = new wxButton(
      &dialog, wxID_ANY, _("Clear Persistent Certified Safe-Area Cache"));
  top->Add(clear, 0, wxLEFT | wxRIGHT | wxBOTTOM | wxEXPAND, 10);

  const weather_routing::ChartSafetyCacheStats cache_stats =
      m_weather_routing_pi.ChartSafetyCacheStatistics();
  wxStaticText* stats = new wxStaticText(
      &dialog, wxID_ANY,
      wxString::Format(
          _("Cache: %lu RAM tiles (%.1f MiB), %lu disk tiles "
            "(%.1f of %.0f MiB); hits RAM=%llu disk=%llu, misses=%llu"),
          static_cast<unsigned long>(cache_stats.ram_entries),
          cache_stats.ram_bytes / (1024.0 * 1024.0),
          static_cast<unsigned long>(cache_stats.disk_entries),
          cache_stats.disk_file_bytes / (1024.0 * 1024.0),
          cache_stats.disk_budget_bytes / (1024.0 * 1024.0),
          static_cast<unsigned long long>(cache_stats.ram_hits),
          static_cast<unsigned long long>(cache_stats.disk_hits),
          static_cast<unsigned long long>(cache_stats.misses)));
  stats->Wrap(470);
  top->Add(stats, 0, wxLEFT | wxRIGHT | wxBOTTOM | wxEXPAND, 10);

  wxStaticText* note = new wxStaticText(
      &dialog, wxID_ANY,
      _("Disk caching retains permission-approved semantic hazard/depth "
        "tiles and areas proven safe for matching chart and safety "
        "settings. A chart update selectively rebuilds affected semantic "
        "tiles and invalidates the derived proofs which depend on them."));
  note->Wrap(420);
  top->Add(note, 0, wxLEFT | wxRIGHT | wxBOTTOM | wxEXPAND, 10);

  wxStdDialogButtonSizer* buttons = new wxStdDialogButtonSizer();
  wxButton* ok = new wxButton(&dialog, wxID_OK);
  wxButton* cancel = new wxButton(&dialog, wxID_CANCEL);
  buttons->AddButton(ok);
  buttons->AddButton(cancel);
  buttons->Realize();
  top->Add(buttons, 0, wxALL | wxALIGN_RIGHT, 10);

  clear->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) {
    bool ok = m_weather_routing_pi.ClearChartSafetyCache();
    wxLogMessage("WR_CERT_SAFE_CACHE clear_requested success=%d", ok ? 1 : 0);
    wxMessageBox(
        ok ? _("Persistent certified safe-area cache cleared.")
           : _("Unable to clear persistent certified safe-area cache."),
        _("Chart Awareness Settings"), wxOK | wxICON_INFORMATION, this);
  });

  dialog.SetSizerAndFit(top);
  update_atlas_estimate();
  if (dialog.ShowModal() == wxID_OK) {
    m_weather_routing_pi.SetUsePersistentChartSafeCache(persistent->GetValue());
    m_weather_routing_pi.SetChartSafetyRamCacheMiB(ram->GetValue());
    m_weather_routing_pi.SetChartSafetyAtlasSettings(
        atlas_enabled->GetValue(), atlas_quota->GetValue(),
        atlas_all->GetValue(),
        selected_atlas_paths());
    wxLogMessage("WR_CERT_SAFE_CACHE ui_set enabled=%d",
                 persistent->GetValue() ? 1 : 0);
  }
}

int WeatherRouting::ChartSafetyRamCacheMiB() const {
  return m_weather_routing_pi.ChartSafetyRamCacheMiB();
}

int WeatherRouting::EffectiveChartSafetyRamCacheMiB() const {
  return m_weather_routing_pi.EffectiveChartSafetyRamCacheMiB();
}

bool WeatherRouting::HasEnhancedChartSafety() const {
  return m_weather_routing_pi.HasEnhancedChartSafety();
}

void WeatherRouting::SetChartSafetyRamCacheMiB(int ramMiB) {
  m_weather_routing_pi.SetChartSafetyRamCacheMiB(ramMiB);
}

void WeatherRouting::OnSettings(wxCommandEvent& event) {
  m_SettingsDialog.Show();
}

void WeatherRouting::OnStatistics(wxCommandEvent& event) {
  m_StatisticsDialog.SetRouteMapOverlays(CurrentRouteMaps());
  m_StatisticsDialog.Show();
}

void WeatherRouting::OnReport(wxCommandEvent& event) {
  m_ReportDialog.SetRouteMapOverlays(CurrentRouteMaps());
  m_ReportDialog.Show();
}

void WeatherRouting::OnPlot(wxCommandEvent& event) {
  m_PlotDialog.SetRouteMapOverlay(FirstCurrentRouteMap());
  m_PlotDialog.Show();
}

void WeatherRouting::OnCursorPosition(wxCommandEvent& event) {
  m_CursorPositionDialog.Show(!m_CursorPositionDialog.IsShown());
  UpdateCursorPositionDialog();
}

// CUSTOMIZATION
void WeatherRouting::OnRoutePosition(wxCommandEvent& event) {
  m_RoutePositionDialog.Show(!m_RoutePositionDialog.IsShown());
  UpdateRoutePositionDialog();
}

void WeatherRouting::AddRoutingPanel() {
  std::list<RouteMapOverlay*> currentroutemaps = CurrentRouteMaps(true);
  if (currentroutemaps.empty()) return;

  if (!m_RoutingTablePanel) {
    // Create the panel if it doesn't exist yet
    wxWindow* parent = m_weather_routing_pi.GetParentWindow();

    // Create the panel directly with the updated RoutingTablePanel
    m_RoutingTablePanel =
        new RoutingTablePanel(parent, *this, currentroutemaps.front());

    // Add the panel to the AUI manager
    wxAuiManager* pauimgr = ::GetFrameAuiManager();
    wxAuiPaneInfo pane = wxAuiPaneInfo()
                             .Name(_T("Weather Routing Table"))
                             .Caption(_T("Weather Routing Table"))
                             .CaptionVisible(true)
                             .Float()
                             .FloatingPosition(100, 100)
                             .FloatingSize(700, 400)
                             .Dockable(true)
                             .Movable(true)
                             .CloseButton(true);

    pauimgr->AddPane(m_RoutingTablePanel, pane);

#if OCPN_API_VERSION_MAJOR > 1 || \
    (OCPN_API_VERSION_MAJOR == 1 && OCPN_API_VERSION_MINOR >= 20)
    // Set color scheme using GetAppColorScheme() from the OpenCPN API
    PI_ColorScheme cs = GetAppColorScheme();
    ((RoutingTablePanel*)m_RoutingTablePanel)->SetColorScheme(cs);
#endif

    pauimgr->Update();
  } else {
    // Update data in existing panel
    m_RoutingTablePanel->SetRouteMap(currentroutemaps.front());

    // Show the panel if it's hidden
    wxAuiManager* pauimgr = ::GetFrameAuiManager();
    wxAuiPaneInfo& pane = pauimgr->GetPane(m_RoutingTablePanel);
    if (!pane.IsShown()) {
      pane.Show(true);
      pauimgr->Update();
    }
  }
}

void WeatherRouting::OnWeatherTable(wxCommandEvent& event) {
  AddRoutingPanel();
}

void WeatherRouting::OnManual(wxCommandEvent& event) {
  wxLaunchDefaultBrowser(
      "https://opencpn.org/wiki/dokuwiki/"
      "doku.php?id=opencpn:opencpn_user_manual:plugins:weather:weather_"
      "routing");
}

void WeatherRouting::OnInformation(wxCommandEvent& event) {
  wxString infolocation = GetPluginDataDir(PLUGIN_PACKAGE_NAME) +
                          _T("/data/") + _("WeatherRoutingInformation.html");
  wxLaunchDefaultBrowser(_T("file://") + infolocation);
}

void WeatherRouting::OnAbout(wxCommandEvent& event) {
  AboutDialog dlg(GetParent());
  dlg.ShowModal();
}

void WeatherRouting::OnComputationTimer(wxTimerEvent&) {
  wxStopWatch tickTimer;
  int runningBefore = m_RunningRouteMaps.size();
  int waitingBefore = m_WaitingRouteMaps.size();
  int completedProcessed = 0;
  int gribRequests = 0;
  int chartSafetyRequestsServiced = 0;
  long chartSafetyRequestMs = 0;
  bool completedBatchLimitHit = false;
  long deleteThreadMs = 0;
  long finalValidationMs = 0;
  long updateRouteMs = 0;
  long reportUpdateMs = 0;
  long gribRequestMs = 0;
  long advanceMs = 0;
  long startWaitingMs = 0;
  long refreshDialogsMs = 0;
  std::vector<RouteMapOverlay*> completedRouteMaps;
  for (std::list<RouteMapOverlay*>::iterator it = m_RunningRouteMaps.begin();
       it != m_RunningRouteMaps.end();) {
    RouteMapOverlay* routemapoverlay = *it;
    if (!routemapoverlay->Running()) {
      if (completedProcessed >= UI_TIMING_COMPLETED_ROUTES_PER_TICK) {
        completedBatchLimitHit = true;
        break;
      }
      completedProcessed++;
      wxStopWatch sectionTimer;
      routemapoverlay->DeleteThread();
      deleteThreadMs += sectionTimer.Time();

      RouteMapConfiguration postThreadConfiguration =
          routemapoverlay->GetConfiguration();
      bool chartSafetyUse = false;
      bool chartSafetyEnforce = false;
      ReadExperimentalChartSafetySettings(chartSafetyUse, chartSafetyEnforce);
      if (postThreadConfiguration.DetectLand && chartSafetyUse &&
          chartSafetyEnforce && routemapoverlay->Finished() &&
          routemapoverlay->ReachedDestination() &&
          !routemapoverlay->UsesModernNativeResult()) {
        sectionTimer.Start();
        routemapoverlay->UpdateDestination();
        finalValidationMs += sectionTimer.Time();
        wxLogMessage(
            "FINAL_ROUTE_SAFETY main_thread_destination_update "
            "route=\"%s -> %s\" finished=%d reached=%d elapsed_ms=%ld",
            postThreadConfiguration.Start, postThreadConfiguration.End,
            routemapoverlay->Finished() ? 1 : 0,
            routemapoverlay->ReachedDestination() ? 1 : 0, sectionTimer.Time());
      }

      RouteMapConfiguration completedConfiguration =
          routemapoverlay->GetConfiguration();
      bool missingChartSafetyData =
          completedConfiguration.chart_safety_missing_tile_rejections > 0 &&
          !routemapoverlay->ReachedDestination();
      bool retryChartSafetyUse = false;
      bool retryChartSafetyEnforce = false;
      if (missingChartSafetyData)
        ReadExperimentalChartSafetySettings(retryChartSafetyUse,
                                            retryChartSafetyEnforce);
      bool retryMissingChartSafetyData =
          missingChartSafetyData && retryChartSafetyUse &&
          retryChartSafetyEnforce &&
          completedConfiguration.chart_safety_missing_tile_retry_count <
              ReadExperimentalChartSafetyMissingTileMaxRetries();
      if (retryMissingChartSafetyData) {
        it = m_RunningRouteMaps.erase(it);
        if (RetryRouteAfterMissingChartSafetyTiles(routemapoverlay)) {
          wxLogMessage(
              "WR_GRID_TILE_RETRY_QUEUED route=\"%s to %s\" "
              "waiting=%lu running=%lu.",
              completedConfiguration.Start, completedConfiguration.End,
              static_cast<unsigned long>(m_WaitingRouteMaps.size()),
              static_cast<unsigned long>(m_RunningRouteMaps.size()));
          continue;
        }
        it = m_RunningRouteMaps.insert(it, routemapoverlay);
      } else if (missingChartSafetyData && retryChartSafetyUse &&
                 retryChartSafetyEnforce) {
        RetryRouteAfterMissingChartSafetyTiles(routemapoverlay);
      }

      if (m_ChartSafetyComputeProgressActive && !m_ActiveMultiLegSequence &&
          !m_ActiveMultiLegDepartureOptimization) {
        UpdateChartSafetyComputeProgress(
            _("Validating final route"), routemapoverlay,
            m_ChartSafetyComputeProgressCompletedRoutes,
            wxMax(m_ChartSafetyComputeProgressTotalRoutes,
                  m_ChartSafetyComputeProgressStartedRoutes));
      }
      sectionTimer.Start();
      ValidateCompletedRouteForDisplay(routemapoverlay);
      finalValidationMs += sectionTimer.Time();

      bool retryWithChartSafetyPropagation =
          !routemapoverlay->ReachedDestination() &&
          RetryRouteWithChartSafetyPropagation(routemapoverlay);

      it = m_RunningRouteMaps.erase(it);

      if (retryWithChartSafetyPropagation) {
        Start(routemapoverlay);
        bool retryQueued = RouteMapIsWaitingOrRunning(routemapoverlay);
        wxLogMessage(
            "FINAL_ROUTE_SAFETY chart_propagation_retry_queued "
            "route=\"%s to %s\" queued=%d waiting=%lu running=%lu.",
            completedConfiguration.Start, completedConfiguration.End,
            retryQueued ? 1 : 0,
            static_cast<unsigned long>(m_WaitingRouteMaps.size()),
            static_cast<unsigned long>(m_RunningRouteMaps.size()));
        if (retryQueued) continue;
      }

      RouteMapConfiguration reverseDiagnosticConfiguration =
          routemapoverlay->GetConfiguration();
      if (reverseDiagnosticConfiguration.UseReverseReachabilityRecovery &&
          routemapoverlay->Finished() &&
          !routemapoverlay->ReachedDestination() &&
          !routemapoverlay->UsesModernNativeResult()) {
        wxStopWatch reverseDiagnosticTimer;
        bool connectionFound =
            routemapoverlay->AnalyzeReverseReachabilityForFrontierCollapse(
                _("route failed before reaching destination"));
        finalValidationMs += reverseDiagnosticTimer.Time();
        wxLogMessage(
            "WR_REVERSE_FRONTIER_COLLAPSE_UI route=\"%s -> %s\" "
            "connection_found=%d elapsed_ms=%ld",
            reverseDiagnosticConfiguration.Start,
            reverseDiagnosticConfiguration.End, connectionFound ? 1 : 0,
            reverseDiagnosticTimer.Time());
      }

      m_panel->m_gProgress->SetValue(m_RoutesToRun - m_WaitingRouteMaps.size() -
                                     m_RunningRouteMaps.size());
      sectionTimer.Start();
      UpdateRouteMap(routemapoverlay);
      updateRouteMs += sectionTimer.Time();
      completedRouteMaps.push_back(routemapoverlay);
      if (m_ChartSafetyComputeProgressActive && !m_ActiveMultiLegSequence &&
          !m_ActiveMultiLegDepartureOptimization) {
        m_ChartSafetyComputeProgressCompletedRoutes++;
        UpdateChartSafetyComputeProgress(
            routemapoverlay->ReachedDestination() ? _("Route complete")
                                                  : _("Route failed"),
            routemapoverlay, m_ChartSafetyComputeProgressCompletedRoutes,
            wxMax(m_ChartSafetyComputeProgressTotalRoutes,
                  m_ChartSafetyComputeProgressStartedRoutes));
      }

      /* update report if needed */
      sectionTimer.Start();
      m_ReportDialog.m_bReportStale = true;
      if (m_ReportDialog.IsShown()) {
        std::list<RouteMapOverlay*> routemapoverlays = CurrentRouteMaps();
        for (std::list<RouteMapOverlay*>::iterator it =
                 routemapoverlays.begin();
             it != routemapoverlays.end(); it++)
          if (routemapoverlay == *it) {
            m_ReportDialog.SetRouteMapOverlays(routemapoverlays);
            break;
          }
      }
      reportUpdateMs += sectionTimer.Time();

      continue;
    } else
      it++;

    wxString modernStage, modernDetail;
    if (routemapoverlay->GetModernNativeProgress(modernStage, modernDetail) &&
        m_RoutingProgressDialog && m_RoutingProgressDialog->IsShown())
      UpdateRoutingProgress(modernStage, modernDetail);

    /* Service chart-derived data only on the application thread.  The route
     * worker retains all completed isochrones and recomputes only its
     * provisional current layer when these requests complete. */
    if (routemapoverlay->NeedsChartSafetyData() &&
        !routemapoverlay->Finished()) {
      wxStopWatch chartRequestTimer;
      PlugInSegmentSafetyRequestServiceResult service = {};
      service.struct_size = sizeof(service);
      weather_routing::chart_safety_host::ServicePendingRequests(
          16, 50, &service);
      chartSafetyRequestMs += chartRequestTimer.Time();
      chartSafetyRequestsServiced += service.requests_serviced;
      if (service.pending_after == 0) {
        routemapoverlay->ChartSafetyDataServiced();
        wxLogMessage(
            "WR_GRID_REQUEST_RESUME route=\"%s -> %s\" serviced=%d "
            "built=%d failed=%d elapsed_ms=%d",
            routemapoverlay->GetConfiguration().Start,
            routemapoverlay->GetConfiguration().End, service.requests_serviced,
            service.masks_built, service.requests_failed, service.elapsed_ms);
      }
    }

    /* get a new grib for the route map if needed */
    if (routemapoverlay->NeedsGrib() && !routemapoverlay->Finished()) {
      wxStopWatch sectionTimer;
      RequestGribTimelineFrame(routemapoverlay, routemapoverlay->NewTime());
      gribRequestMs += sectionTimer.Time();
      gribRequests++;
    }
  }

  wxStopWatch sectionTimer;
  for (auto routemapoverlay : completedRouteMaps) {
    AdvanceMultiLegSequence(routemapoverlay);
    AdvanceMultiLegDepartureOptimization(routemapoverlay);
  }
  FinishChartSafetyComputeProgressIfDone();
  advanceMs += sectionTimer.Time();

  bool departure_candidates_active = false;
  bool deterministic_host_service_lane = false;
  bool authoritative_chart_search = false;
  int requested_departure_workers =
      weather_routing::kAutomaticParallelDepartureCandidates;
  for (RouteMapOverlay* route : m_RunningRouteMaps)
    if (route && route->GetConfiguration().DepartureTimeOptimizationCandidate) {
      departure_candidates_active = true;
      deterministic_host_service_lane =
          ModernNativeRouteRequiresSerialHostServices(
              route->GetConfiguration());
      authoritative_chart_search =
          route->GetConfiguration().UseChartSafetyForPropagation;
      requested_departure_workers =
          route->GetConfiguration()
              .DepartureTimeOptimizationConcurrentRoutes;
      break;
    }
  if (!departure_candidates_active)
    for (RouteMapOverlay* route : m_WaitingRouteMaps)
      if (route &&
          route->GetConfiguration().DepartureTimeOptimizationCandidate) {
        departure_candidates_active = true;
        deterministic_host_service_lane =
            ModernNativeRouteRequiresSerialHostServices(
                route->GetConfiguration());
        authoritative_chart_search =
            route->GetConfiguration().UseChartSafetyForPropagation;
        requested_departure_workers =
            route->GetConfiguration()
                .DepartureTimeOptimizationConcurrentRoutes;
        break;
      }
  const int route_worker_limit = weather_routing::EffectiveRouteWorkerLimit(
      m_SettingsDialog.m_sConcurrentThreads->GetValue(),
      departure_candidates_active, requested_departure_workers,
      wxThread::GetCPUCount(), deterministic_host_service_lane,
      authoritative_chart_search);
  if ((int)m_RunningRouteMaps.size() < route_worker_limit &&
      m_WaitingRouteMaps.size()) {
    sectionTimer.Start();
    RouteMapOverlay* routemapoverlay = m_WaitingRouteMaps.front();
    m_WaitingRouteMaps.pop_front();
    wxString error;
    if (routemapoverlay->Start(error))
      m_RunningRouteMaps.push_back(routemapoverlay);
    else {
      wxMessageDialog mdlg(this, _("Failed to start configuration: ") + error,
                           _("Weather Routing"), wxOK | wxICON_ERROR);
      mdlg.ShowModal();
    }

    UpdateRouteMap(routemapoverlay);
    startWaitingMs += sectionTimer.Time();
  }

  static int cycles; /* don't refresh all the time */
  if (++cycles > 50 || !m_RunningRouteMaps.size()) {
    cycles = 0;

    sectionTimer.Start();
    std::list<RouteMapOverlay*> currentroutemaps = CurrentRouteMaps();
    for (std::list<RouteMapOverlay*>::iterator it = currentroutemaps.begin();
         it != currentroutemaps.end(); it++)
      if ((*it)->Updated()) {
        m_StatisticsDialog.SetRunTime(m_RunTime +=
                                      wxDateTime::Now() - m_StartTime);
        if (m_StatisticsDialog.IsShown())
          m_StatisticsDialog.SetRouteMapOverlays(CurrentRouteMaps());
        if (m_PlotDialog.IsShown())
          m_PlotDialog.SetRouteMapOverlay(FirstCurrentRouteMap());

        m_StartTime = wxDateTime::Now();
        GetParent()->Refresh();
        break;
      }
    refreshDialogsMs += sectionTimer.Time();
  }

  long tickMs = tickTimer.Time();
  // A running route normally asks for one GRIB sample on almost every timer
  // tick.  Logging each successful, cached request at MESSAGE level can write
  // tens of records per second and make the diagnostic path a measurable part
  // of routing time.  Retain a periodic service heartbeat while reporting
  // slow and completion events immediately.
  static unsigned routineServiceLogCycles = 0;
  bool periodicServiceLog = false;
  if (gribRequests > 0 || chartSafetyRequestsServiced > 0) {
    periodicServiceLog = ++routineServiceLogCycles >= 40;
    if (periodicServiceLog) routineServiceLogCycles = 0;
  } else {
    routineServiceLogCycles = 0;
  }
  if (tickMs >= UI_TIMING_LOG_THRESHOLD_MS || completedProcessed > 0 ||
      periodicServiceLog || completedBatchLimitHit) {
    wxLogMessage(
        "WR_UI_TIMING OnComputationTimer tick_ms=%ld running_before=%d "
        "waiting_before=%d running_after=%lu waiting_after=%lu "
        "completed_processed=%d completed_batch_limit=%d "
        "delete_thread_ms=%ld final_validation_ms=%ld update_route_ms=%ld "
        "report_update_ms=%ld grib_requests=%d grib_request_ms=%ld "
        "chart_requests=%d chart_request_ms=%ld "
        "advance_ms=%ld start_waiting_ms=%ld refresh_dialogs_ms=%ld "
        "progress_dialog=%d progress_timer=%d deferred_start=%d "
        "multileg=%d multileg_opt=%d",
        tickMs, runningBefore, waitingBefore,
        static_cast<unsigned long>(m_RunningRouteMaps.size()),
        static_cast<unsigned long>(m_WaitingRouteMaps.size()),
        completedProcessed, completedBatchLimitHit ? 1 : 0, deleteThreadMs,
        finalValidationMs, updateRouteMs, reportUpdateMs, gribRequests,
        gribRequestMs, chartSafetyRequestsServiced, chartSafetyRequestMs,
        advanceMs, startWaitingMs, refreshDialogsMs,
        m_RoutingProgressDialog && m_RoutingProgressDialog->IsShown() ? 1 : 0,
        m_tRoutingProgress.IsRunning() ? 1 : 0,
        m_DeferredRoutingStartPending ? 1 : 0, m_ActiveMultiLegSequence ? 1 : 0,
        m_ActiveMultiLegDepartureOptimization ? 1 : 0);
  }

  if (m_RunningRouteMaps.size()) {
    // A native worker blocks while a GRIB or chart request is serviced on the
    // GUI thread.  The historical fixed 25 ms poll imposed up to 40 round
    // trips per second even when a cached request itself took microseconds.
    // Poll promptly after servicing work, then return to the low-frequency
    // idle cadence when no request was present so ordinary GUI work is not
    // needlessly churned.
    const int nextIntervalMs =
        gribRequests > 0 || chartSafetyRequestsServiced > 0
            ? UI_TIMING_ACTIVE_SERVICE_INTERVAL_MS
            : 25;
    m_tCompute.Start(nextIntervalMs, true);
    return;
  }

  StopAll();
}

void WeatherRouting::OnHideConfigurationTimer(wxTimerEvent&) {
  m_ConfigurationDialog.Hide();
}

void WeatherRouting::OnAutoSaveXMLTimer(wxTimerEvent&) {
  if (!EnvString("WR_HEADLESS_ROUTE_TEST").IsEmpty()) return;
  AutoSaveXML();
}

void WeatherRouting::AutoSaveXML() { SaveXML(m_FileName.GetFullPath()); }

void WeatherRouting::OnRenderedTimer(wxTimerEvent&) {
  // don't do it until the window system is up and running
  if (GetClientSize().GetWidth() > 20) {
    if (!sashpos) sashpos = GetClientSize().GetWidth() / 5;
    m_panel->m_splitter1->SetSashPosition(sashpos, true);
    Disconnect(wxEVT_IDLE, wxTimerEventHandler(WeatherRouting::OnRenderedTimer),
               NULL, this);
  }
}

bool WeatherRouting::OpenXML(wxString filename, bool reportfailure) {
  TiXmlDocument doc;
  wxString error;

  wxFileName fn(filename);
  SetTitle(_("WeatherRouting") + wxString(_T(" - ")) + fn.GetFullName());
  m_FileName = fn;

  wxProgressDialog* progressdialog = NULL;
  wxDateTime start = wxDateTime::UNow();

  wxString lastboatFileName;
  Boat lastboat;

  if (!doc.LoadFile(filename.mb_str()))
    FAIL(_("Failed to load file."));
  else {
    TiXmlHandle root(doc.RootElement());

    if (strcmp(root.Element()->Value(), "OpenCPNWeatherRoutingConfiguration"))
      FAIL(_("Invalid xml file"));

    RouteMap::Positions.clear();

    int count = 0;
    for (TiXmlElement* e = root.FirstChild().Element(); e;
         e = e->NextSiblingElement())
      count++;

    int i = 0;
    for (TiXmlElement* e = root.FirstChild().Element(); e;
         e = e->NextSiblingElement(), i++) {
      if (progressdialog) {
        if (!progressdialog->Update(i)) {
          delete progressdialog;
          return true;
        }
      } else {
        wxDateTime now = wxDateTime::UNow();
        /* if it's going to take more than a half second, show a progress dialog
         */
        if ((now - start).GetMilliseconds() > 250 && i < count / 2) {
          progressdialog = new wxProgressDialog(
              _("Load"), _("Weather Routing"), count, this,
              wxPD_CAN_ABORT | wxPD_ELAPSED_TIME | wxPD_REMAINING_TIME);
        }
      }

      if (!strcmp(e->Value(), "Position")) {
        wxString name = wxString::FromUTF8(e->Attribute("Name"));
        wxString GUID = wxString::FromUTF8(e->Attribute("GUID"));
        double lat = AttributeDouble(e, "Latitude", NAN);
        double lon = AttributeDouble(e, "Longitude", NAN);

        if (GUID.IsEmpty())
          for (auto it : RouteMap::Positions) {
            if (it.Name == name) {
              static bool warnonce = true;
              if (warnonce) {
                warnonce = false;
                wxLogMessage(
                    "Weather Routing: duplicate position name \"%s\" in "
                    "configuration file; discarding duplicate.",
                    name);
                if (EnvString("WR_HEADLESS_ROUTE_TEST").IsEmpty()) {
                  wxMessageDialog mdlg(
                      this,
                      wxString::Format(
                          _("File contains duplicate position name \"%s\"; "
                            "discarding duplicate.\n"),
                          name),
                      _("Weather Routing"), wxOK | wxICON_WARNING);
                  mdlg.ShowModal();
                }
              }

              goto skipadd;
            }
          }
        AddPosition(lat, lon, name, GUID);

      skipadd:;
      } else if (!strcmp(e->Value(), "Configuration")) {
        // Ideally the name of the XML element should be "Routings" but it is
        // kept as "Configuration" for backward compatibility.
        RouteMapConfiguration configuration;
        configuration.RouteGUID = wxString::FromUTF8(e->Attribute("GUID"));
        configuration.StartType =
            (RouteMapConfiguration::StartDataType)AttributeInt(
                e, "StartType", RouteMapConfiguration::START_FROM_POSITION);
        configuration.Start = wxString::FromUTF8(e->Attribute("Start"));
        configuration.EndType =
            (RouteMapConfiguration::EndDataType)AttributeInt(
                e, "EndType", RouteMapConfiguration::END_AT_POSITION);
        const char* startGuid = e->Attribute("StartGUID");
        if (startGuid) configuration.StartGUID = wxString::FromUTF8(startGuid);
        const char* endGuid = e->Attribute("EndGUID");
        if (endGuid) configuration.EndGUID = wxString::FromUTF8(endGuid);
        if (configuration.StartType ==
                RouteMapConfiguration::START_FROM_POSITION &&
            !configuration.StartGUID.IsEmpty())
          configuration.StartType = RouteMapConfiguration::START_FROM_WAYPOINT;
        if (configuration.EndType == RouteMapConfiguration::END_AT_POSITION &&
            !configuration.EndGUID.IsEmpty())
          configuration.EndType = RouteMapConfiguration::END_AT_WAYPOINT;
        configuration.UseCurrentTime =
            AttributeBool(e, "UseCurrentTime", false);
        const int routingTimeMode =
            AttributeInt(e, "RoutingTimeMode",
                         RouteMapConfiguration::ROUTE_BY_DEPARTURE_TIME);
        configuration.TimeMode =
            routingTimeMode == RouteMapConfiguration::ROUTE_BY_ARRIVAL_TIME
                ? RouteMapConfiguration::ROUTE_BY_ARRIVAL_TIME
                : RouteMapConfiguration::ROUTE_BY_DEPARTURE_TIME;
        configuration.ArrivalSearchHorizonMinutes =
            std::max(60, AttributeInt(e, "ArrivalSearchHorizonMinutes",
                                      30 * 24 * 60));
        configuration.ArrivalSafetyMarginMinutes =
            std::max(0,
                     AttributeInt(e, "ArrivalSafetyMarginMinutes", 30));
        configuration.DepartureTimeOptimizationEnabled =
            AttributeBool(e, "DepartureTimeOptimizationEnabled", false);
        configuration.DepartureTimeOptimizationRangeMinutes =
            AttributeInt(e, "DepartureTimeOptimizationRangeMinutes", 360);
        configuration.DepartureTimeOptimizationStepMinutes =
            AttributeInt(e, "DepartureTimeOptimizationStepMinutes", 60);
        configuration.DepartureTimeOptimizationConcurrentRoutes = std::max(
            0, std::min(
                   weather_routing::kMaximumParallelDepartureCandidates,
                   AttributeInt(
                       e, "DepartureTimeOptimizationConcurrentRoutes", 0)));
        configuration.RoutingEffortPercent =
            weather_routing::NormalizeRoutingEffortPercent(
                AttributeInt(e, "RoutingEffortPercent",
                             weather_routing::kDefaultRoutingEffortPercent));
        configuration.IsMultiLegGenerated =
            AttributeBool(e, "IsMultiLegGenerated", false);
        const char* multiLegGroupId = e->Attribute("MultiLegGroupId");
        if (multiLegGroupId)
          configuration.MultiLegGroupId = wxString::FromUTF8(multiLegGroupId);
        const char* multiLegParentRouteGUID =
            e->Attribute("MultiLegParentRouteGUID");
        if (multiLegParentRouteGUID)
          configuration.MultiLegParentRouteGUID =
              wxString::FromUTF8(multiLegParentRouteGUID);
        const char* multiLegParentRouteName =
            e->Attribute("MultiLegParentRouteName");
        if (multiLegParentRouteName)
          configuration.MultiLegParentRouteName =
              wxString::FromUTF8(multiLegParentRouteName);
        configuration.MultiLegLegIndex = AttributeInt(e, "MultiLegLegIndex", 0);
        configuration.MultiLegLegCount = AttributeInt(e, "MultiLegLegCount", 0);
        if (configuration.UseCurrentTime) {
          // The current time will be overridden when the route is computed.
          configuration.StartTime = wxDateTime::Now();
        } else {
          wxDateTime date;
          date.ParseISODate(wxString::FromUTF8(e->Attribute("StartDate")));
          wxDateTime time;
          time.ParseISOTime(wxString::FromUTF8(e->Attribute("StartTime")));
          if (date.IsValid()) {
            if (time.IsValid()) {
              date.SetHour(time.GetHour());
              date.SetMinute(time.GetMinute());
              date.SetSecond(time.GetSecond());
            }
            configuration.StartTime = date;
          } else {
            configuration.StartTime = wxDateTime::Now();
          }
        }
        wxDateTime plannedArrivalDate;
        const char* plannedArrivalDateAttribute =
            e->Attribute("PlannedArrivalDate");
        if (plannedArrivalDateAttribute)
          plannedArrivalDate.ParseISODate(
              wxString::FromUTF8(plannedArrivalDateAttribute));
        wxDateTime plannedArrivalClock;
        const char* plannedArrivalTimeAttribute =
            e->Attribute("PlannedArrivalTime");
        if (plannedArrivalTimeAttribute)
          plannedArrivalClock.ParseISOTime(
              wxString::FromUTF8(plannedArrivalTimeAttribute));
        if (plannedArrivalDate.IsValid()) {
          if (plannedArrivalClock.IsValid()) {
            plannedArrivalDate.SetHour(plannedArrivalClock.GetHour());
            plannedArrivalDate.SetMinute(plannedArrivalClock.GetMinute());
            plannedArrivalDate.SetSecond(plannedArrivalClock.GetSecond());
          }
          configuration.PlannedArrivalTime = plannedArrivalDate;
        } else {
          configuration.PlannedArrivalTime =
              configuration.StartTime + wxTimeSpan::Hours(24);
        }

        configuration.End = wxString::FromUTF8(e->Attribute("End"));
        configuration.DeltaTime = AttributeDouble(e, "dt", 0);

        configuration.boatFileName = wxString::FromUTF8(e->Attribute("Boat"));
        if (!wxFileName::FileExists(configuration.boatFileName)) {
          configuration.boatFileName =
              weather_routing_pi::StandardPath() + _T("boats") +
              wxFileName::GetPathSeparator() + configuration.boatFileName;
          if (!wxFileName::FileExists(configuration.boatFileName)) {
            configuration.boatFileName = _T("");
          }
        }

        configuration.Integrator =
            (RouteMapConfiguration::IntegratorType)AttributeInt(e, "Integrator",
                                                                0);

        configuration.MaxDivertedCourse =
            AttributeDouble(e, "MaxDivertedCourse", 90);
        configuration.MaxCourseAngle =
            AttributeDouble(e, "MaxCourseAngle", 180);
        configuration.MaxSearchAngle =
            AttributeDouble(e, "MaxSearchAngle", 120);
        configuration.MaxTrueWindKnots =
            AttributeDouble(e, "MaxTrueWindKnots", 50);
        configuration.MaxApparentWindKnots =
            AttributeDouble(e, "MaxApparentWindKnots", 50);

        configuration.MaxSwellMeters =
            AttributeDouble(e, "MaxSwellMeters", 20.);
        configuration.MaxLatitude = AttributeDouble(e, "MaxLatitude", 90);
        configuration.TackingTime = AttributeDouble(e, "TackingTime", 0);
        configuration.JibingTime = AttributeDouble(e, "JibingTime", 0);
        configuration.SailPlanChangeTime =
            AttributeDouble(e, "SailPlanChangeTime", 0);
        configuration.WindVSCurrent = AttributeDouble(e, "WindVSCurrent", 0);

        configuration.AvoidCycloneTracks =
            AttributeBool(e, "AvoidCycloneTracks", false);
        configuration.CycloneMonths = AttributeInt(e, "CycloneMonths", 2);
        configuration.CycloneDays = AttributeInt(e, "CycloneDays", 0);

        configuration.UseGrib = AttributeBool(e, "UseGrib", true);
        configuration.ClimatologyType =
            (RouteMapConfiguration::ClimatologyDataType)AttributeInt(
                e, "ClimatologyType", RouteMapConfiguration::CUMULATIVE_MAP);
        configuration.AllowDataDeficient =
            AttributeBool(e, "AllowDataDeficient", false);
        configuration.WindStrength = AttributeDouble(e, "WindStrength", 1);

        configuration.UpwindEfficiency =
            AttributeDouble(e, "UpwindEfficiency", 1.);
        configuration.DownwindEfficiency =
            AttributeDouble(e, "DownwindEfficiency", 1.);
        configuration.NightCumulativeEfficiency =
            AttributeDouble(e, "NightCumulativeEfficiency", 1.);

        configuration.DetectLand = AttributeBool(e, "DetectLand", true);
        configuration.SafetyMarginLand =
            AttributeDouble(e, "SafetyMarginLand", 0.);
        configuration.MinimumDepthMeters =
            wxMax(0.0, AttributeDouble(e, "MinimumDepthMeters", 0.));
        configuration.DetectBoundary =
            AttributeBool(e, "DetectBoundary", false);
        configuration.Currents = AttributeBool(e, "Currents", true);
        configuration.OptimizeTacking =
            AttributeBool(e, "OptimizeTacking", false);

        configuration.InvertedRegions =
            AttributeBool(e, "InvertedRegions", false);
        configuration.UseReverseReachabilityRecovery =
            AttributeBool(e, "UseReverseReachabilityRecovery", false);
        configuration.Anchoring = AttributeBool(e, "Anchoring", false);

        configuration.FromDegree = AttributeDouble(e, "FromDegree", 0);
        configuration.ToDegree = AttributeDouble(e, "ToDegree", 180);
        configuration.UseOptimalAngles =
            AttributeBool(e, "UseOptimalAngles", false);
        configuration.ByDegrees = AttributeDouble(e, "ByDegrees", 5.);
        configuration.UseMotor = AttributeBool(e, "UseMotor", false);
        configuration.MotorSpeedThreshold =
            AttributeDouble(e, "MotorSpeedThreshold", 2.0);
        configuration.MotorSpeed = AttributeDouble(e, "MotorSpeed", 5.0);

        if (configuration.boatFileName == lastboatFileName)
          configuration.boat = lastboat;

        AddConfiguration(configuration);

        lastboatFileName = configuration.boatFileName;
        m_WeatherRoutes.back()->routemapoverlay->LoadBoat();
        lastboat =
            m_WeatherRoutes.back()->routemapoverlay->GetConfiguration().boat;
      } else
        FAIL(_("Unrecognized xml node"));
    }
  }

  delete progressdialog;
  return true;
failed:

  delete progressdialog;
  if (reportfailure) {
    wxMessageDialog mdlg(this, error, _("Weather Routing"),
                         wxOK | wxICON_ERROR);
    mdlg.ShowModal();
  }
  return false;
}

void WeatherRouting::SaveXML(wxString filename) {
  wxFileName fn(filename);
  SetTitle(_("WeatherRouting") + wxString(_T(" - ")) + fn.GetFullName());
  m_FileName = fn;

  TiXmlDocument doc;
  TiXmlDeclaration* decl = new TiXmlDeclaration("1.0", "utf-8", "");
  doc.LinkEndChild(decl);

  TiXmlElement* root = new TiXmlElement("OpenCPNWeatherRoutingConfiguration");
  doc.LinkEndChild(root);

  char version[24];
  sprintf(version, "%d.%d", PLUGIN_VERSION_MAJOR, PLUGIN_VERSION_MINOR);
  root->SetAttribute("version", version);
  root->SetAttribute("creator", "Opencpn Weather Routing plugin");

  for (std::list<RouteMapPosition>::iterator it = RouteMap::Positions.begin();
       it != RouteMap::Positions.end(); it++) {
    TiXmlElement* c = new TiXmlElement("Position");

    c->SetAttribute("Name", (*it).Name.mb_str());
    c->SetAttribute("Latitude",
                    wxString::Format(_T("%.6f"), (*it).lat).mb_str());
    c->SetAttribute("Longitude",
                    wxString::Format(_T("%.6f"), (*it).lon).mb_str());
    if (!(*it).GUID.IsEmpty()) c->SetAttribute("GUID", (*it).GUID.mb_str());

    root->LinkEndChild(c);
  }

  for (auto it = m_WeatherRoutes.begin(); it != m_WeatherRoutes.end(); it++) {
    // Ideally the name of the XML element should be "Routings" but it is kept
    // as "Configuration" for backward compatibility.
    TiXmlElement* c = new TiXmlElement("Configuration");

    RouteMapConfiguration configuration =
        (*it)->routemapoverlay->GetConfiguration();
    if (configuration.DepartureTimeOptimizationCandidate) continue;

    if (!configuration.RouteGUID.IsEmpty())
      c->SetAttribute("GUID", configuration.RouteGUID.mb_str());

    c->SetAttribute("StartType", configuration.StartType);
    c->SetAttribute("Start", configuration.Start.mb_str());
    if (!configuration.StartGUID.IsEmpty())
      c->SetAttribute("StartGUID", configuration.StartGUID.mb_str());
    c->SetAttribute("EndType", configuration.EndType);
    c->SetAttribute("UseCurrentTime", configuration.UseCurrentTime);
    c->SetAttribute("RoutingTimeMode",
                    static_cast<int>(configuration.TimeMode));
    c->SetAttribute("ArrivalSearchHorizonMinutes",
                    configuration.ArrivalSearchHorizonMinutes);
    c->SetAttribute("ArrivalSafetyMarginMinutes",
                    configuration.ArrivalSafetyMarginMinutes);
    c->SetAttribute("DepartureTimeOptimizationEnabled",
                    configuration.DepartureTimeOptimizationEnabled);
    c->SetAttribute("DepartureTimeOptimizationRangeMinutes",
                    configuration.DepartureTimeOptimizationRangeMinutes);
    c->SetAttribute("DepartureTimeOptimizationStepMinutes",
                    configuration.DepartureTimeOptimizationStepMinutes);
    c->SetAttribute("DepartureTimeOptimizationConcurrentRoutes",
                    configuration.DepartureTimeOptimizationConcurrentRoutes);
    c->SetAttribute(
        "RoutingEffortPercent",
        weather_routing::NormalizeRoutingEffortPercent(
            configuration.RoutingEffortPercent));
    c->SetAttribute("IsMultiLegGenerated", configuration.IsMultiLegGenerated);
    if (!configuration.MultiLegGroupId.IsEmpty())
      c->SetAttribute("MultiLegGroupId",
                      configuration.MultiLegGroupId.mb_str());
    if (!configuration.MultiLegParentRouteGUID.IsEmpty())
      c->SetAttribute("MultiLegParentRouteGUID",
                      configuration.MultiLegParentRouteGUID.mb_str());
    if (!configuration.MultiLegParentRouteName.IsEmpty())
      c->SetAttribute("MultiLegParentRouteName",
                      configuration.MultiLegParentRouteName.mb_str());
    if (configuration.MultiLegLegIndex)
      c->SetAttribute("MultiLegLegIndex", configuration.MultiLegLegIndex);
    if (configuration.MultiLegLegCount)
      c->SetAttribute("MultiLegLegCount", configuration.MultiLegLegCount);
    if (!configuration.UseCurrentTime) {
      c->SetAttribute("StartDate",
                      configuration.StartTime.FormatISODate().mb_str());
      c->SetAttribute("StartTime",
                      configuration.StartTime.FormatISOTime().mb_str());
    }
    if (configuration.PlannedArrivalTime.IsValid()) {
      c->SetAttribute(
          "PlannedArrivalDate",
          configuration.PlannedArrivalTime.FormatISODate().mb_str());
      c->SetAttribute(
          "PlannedArrivalTime",
          configuration.PlannedArrivalTime.FormatISOTime().mb_str());
    }
    c->SetAttribute("End", configuration.End.mb_str());
    if (!configuration.EndGUID.IsEmpty())
      c->SetAttribute("EndGUID", configuration.EndGUID.mb_str());
    c->SetAttribute("dt", configuration.DeltaTime);

    c->SetAttribute("Boat", configuration.boatFileName.ToUTF8());

    c->SetAttribute("Integrator", configuration.Integrator);

    c->SetAttribute("MaxDivertedCourse", configuration.MaxDivertedCourse);
    c->SetAttribute("MaxCourseAngle", configuration.MaxCourseAngle);
    c->SetAttribute("MaxSearchAngle", configuration.MaxSearchAngle);
    c->SetAttribute("MaxTrueWindKnots", configuration.MaxTrueWindKnots);
    c->SetAttribute("MaxApparentWindKnots", configuration.MaxApparentWindKnots);

    c->SetDoubleAttribute("MaxSwellMeters", configuration.MaxSwellMeters);
    c->SetAttribute("MaxLatitude", configuration.MaxLatitude);
    c->SetAttribute("TackingTime", configuration.TackingTime);
    c->SetAttribute("JibingTime", configuration.JibingTime);
    c->SetAttribute("SailPlanChangeTime", configuration.SailPlanChangeTime);
    c->SetAttribute("WindVSCurrent", configuration.WindVSCurrent);

    c->SetAttribute("AvoidCycloneTracks", configuration.AvoidCycloneTracks);
    c->SetAttribute("CycloneMonths", configuration.CycloneMonths);
    c->SetAttribute("CycloneDays", configuration.CycloneDays);

    c->SetAttribute("UseGrib", configuration.UseGrib);
    c->SetAttribute("ClimatologyType", configuration.ClimatologyType);
    c->SetAttribute("AllowDataDeficient", configuration.AllowDataDeficient);
    c->SetDoubleAttribute("WindStrength", configuration.WindStrength);

    c->SetDoubleAttribute("UpwindEfficiency", configuration.UpwindEfficiency);
    c->SetDoubleAttribute("DownwindEfficiency",
                          configuration.DownwindEfficiency);
    c->SetDoubleAttribute("NightCumulativeEfficiency",
                          configuration.NightCumulativeEfficiency);

    c->SetAttribute("DetectLand", configuration.DetectLand);
    c->SetDoubleAttribute("SafetyMarginLand", configuration.SafetyMarginLand);
    c->SetDoubleAttribute("MinimumDepthMeters",
                          configuration.MinimumDepthMeters);
    c->SetAttribute("DetectBoundary", configuration.DetectBoundary);
    c->SetAttribute("Currents", configuration.Currents);
    c->SetAttribute("OptimizeTacking", configuration.OptimizeTacking);

    c->SetAttribute("InvertedRegions", configuration.InvertedRegions);
    c->SetAttribute("UseReverseReachabilityRecovery",
                    configuration.UseReverseReachabilityRecovery);
    c->SetAttribute("Anchoring", configuration.Anchoring);

    c->SetDoubleAttribute("FromDegree", configuration.FromDegree);
    c->SetDoubleAttribute("ToDegree", configuration.ToDegree);
    c->SetAttribute("UseOptimalAngles", configuration.UseOptimalAngles);
    c->SetDoubleAttribute("ByDegrees", configuration.ByDegrees);
    c->SetAttribute("UseMotor", configuration.UseMotor);
    c->SetDoubleAttribute("MotorSpeedThreshold",
                          configuration.MotorSpeedThreshold);
    c->SetDoubleAttribute("MotorSpeed", configuration.MotorSpeed);

    root->LinkEndChild(c);
  }

  if (!doc.SaveFile(filename.mb_str())) {
    wxMessageDialog mdlg(this, _("Failed to save xml file: ") + filename,
                         _("Weather Routing"), wxOK | wxICON_ERROR);
    mdlg.ShowModal();
  }
}

void WeatherRouting::SetEnableConfigurationMenu() {
  bool current = FirstCurrentRouteMap() != NULL;
  m_mBatch->Enable(current);
  m_mBatch1->Enable(current);
  m_mEdit->Enable(current);
  m_mEdit1->Enable(current);
  m_mEditMultiLegGroupSettings1->Enable(current);
  m_mShowRoutingStatus1->Enable(current);
  m_mGoTo->Enable(current);
  m_mGoTo1->Enable(current);
  m_mDelete->Enable(current);
  m_mDelete1->Enable(current);
  m_mCompute->Enable(current);
  m_mCompute1->Enable(current);
  m_mComputeMultiLegSequence1->Enable(current);
  m_mOptimizeMultiLegDeparture1->Enable(current);
  m_panel->m_bCompute->Enable(current);
  m_mSaveAsTrack->Enable(current);
  m_mSaveAsRoute->Enable(current);
  m_mExportRouteAsGPX->Enable(current);
  m_panel->m_bSaveAsTrack->Enable(current);
  m_panel->m_bSimplifyRoute->Enable(current);
  m_panel->m_bSaveAsRoute->Enable(current);

  m_mStop->Enable(m_WaitingRouteMaps.size() + m_RunningRouteMaps.size() > 0);
  m_mStop1->Enable(m_WaitingRouteMaps.size() + m_RunningRouteMaps.size() > 0);

  bool cnt = m_panel->m_lWeatherRoutes->GetItemCount() > 0;
  m_mDeleteAll->Enable(cnt);
  m_mComputeAll->Enable(cnt);
  m_mComputeAll1->Enable(cnt);
  m_mSaveAllAsTracks->Enable(cnt);
}

void WeatherRouting::UpdateConfigurations() {
  for (int i = 0; i < m_panel->m_lWeatherRoutes->GetItemCount(); i++) {
    WeatherRoute* weatherroute = reinterpret_cast<WeatherRoute*>(
        wxUIntToPtr(m_panel->m_lWeatherRoutes->GetItemData(i)));

    /* get and set configuration to update start/end positions */
    RouteMapConfiguration c = weatherroute->routemapoverlay->GetConfiguration();
    weatherroute->routemapoverlay->SetConfiguration(c);

    weatherroute->Update(this, true);
    UpdateItem(i);
  }
}

void WeatherRouting::UpdateDialogs() {
  std::list<RouteMapOverlay*> routemapoverlays = CurrentRouteMaps();
  if (m_StatisticsDialog.IsShown())
    m_StatisticsDialog.SetRouteMapOverlays(routemapoverlays);

  if (m_ReportDialog.IsShown())
    m_ReportDialog.SetRouteMapOverlays(routemapoverlays);

  if (m_PlotDialog.IsShown())
    m_PlotDialog.SetRouteMapOverlay(FirstCurrentRouteMap());
}

bool WeatherRouting::AddConfiguration(RouteMapConfiguration& configuration) {
  if (!configuration.RouteGUID.IsEmpty()) {
    // use stuff from actual route not whatever was saved
    std::unique_ptr<PlugIn_Route> rte =
        GetRoute_Plugin(configuration.RouteGUID);
    if (rte.get() == nullptr) return false;

    PlugIn_Route* proute = rte.get();
    if (!proute) return false;

    PlugIn_Waypoint* pwp;
    wxPlugin_WaypointListNode* pwpnode = proute->pWaypointList->GetFirst();
    if (!pwpnode) return false;

    pwp = pwpnode->GetData();
    AddPosition(pwp->m_lat, pwp->m_lon, pwp->m_MarkName, pwp->m_GUID);
    configuration.Start = pwp->m_MarkName;
    configuration.StartGUID = pwp->m_GUID;
    configuration.StartLat = pwp->m_lat, configuration.StartLon = pwp->m_lon;
    while (pwpnode->GetNext()) {
      pwpnode = pwpnode->GetNext();
    }

    pwp = pwpnode->GetData();
    AddPosition(pwp->m_lat, pwp->m_lon, pwp->m_MarkName, pwp->m_GUID);
    configuration.End = pwp->m_MarkName;
    configuration.EndGUID = pwp->m_GUID;
    configuration.EndLat = pwp->m_lat, configuration.EndLon = pwp->m_lon;
  }
  WeatherRoute* weatherroute = new WeatherRoute;
  RouteMapOverlay* routemapoverlay = weatherroute->routemapoverlay;
  routemapoverlay->SetConfiguration(configuration);
  routemapoverlay->Reset();
  weatherroute->Update(this);

  m_WeatherRoutes.push_back(weatherroute);

  wxListItem item;
  long index = m_panel->m_lWeatherRoutes->InsertItem(
      m_panel->m_lWeatherRoutes->GetItemCount(), item);

  m_panel->m_lWeatherRoutes->SetItemPtrData(index, (wxUIntPtr)weatherroute);
  UpdateItem(index);

  m_mDeleteAll->Enable();
  m_mComputeAll->Enable();
  m_mSaveAllAsTracks->Enable();
  m_tAutoSaveXML.Start(5000, true);  // Schedule auto-save in 5 seconds
  return true;
}

void WeatherRouting::UpdateRouteMap(RouteMapOverlay* routemapoverlay) {
  for (int i = 0; i < m_panel->m_lWeatherRoutes->GetItemCount(); i++) {
    WeatherRoute* weatherroute = reinterpret_cast<WeatherRoute*>(
        wxUIntToPtr(m_panel->m_lWeatherRoutes->GetItemData(i)));
    if (weatherroute->routemapoverlay == routemapoverlay) {
      weatherroute->Update(this);
      UpdateItem(i);
      return;
    }
  }
}

static bool IsDisplayMetricAvailable(RouteMapOverlay* routemapoverlay) {
  return routemapoverlay && weather_routing::HasPublishableRouteResult(
                                routemapoverlay->Finished(),
                                routemapoverlay->ReachedDestination(),
                                routemapoverlay->EndTime().IsValid());
}

static wxString FormatRouteMetric(RouteMapOverlay* routemapoverlay,
                                  RouteMapOverlay::RouteInfoType metric,
                                  const wxString& format, bool available,
                                  bool convertSpeed = false) {
  if (!available) return _("N/A");
  double value = routemapoverlay->RouteInfo(metric);
  if (!std::isfinite(value)) return _("N/A");
  if (convertSpeed) value = toUsrSpeed_Plugin(value);
  return wxString::Format(format, value);
}

static wxString FormatRouteDistance(RouteMapOverlay* routemapoverlay,
                                    const RouteMapConfiguration& configuration,
                                    bool available) {
  if (!available) return _("N/A");
  double routedDistance = routemapoverlay->RouteInfo(RouteMapOverlay::DISTANCE);
  double directDistance =
      DistGreatCircle_Plugin(configuration.StartLat, configuration.StartLon,
                             configuration.EndLat, configuration.EndLon);
  if (!std::isfinite(routedDistance) || !std::isfinite(directDistance))
    return _("N/A");
  return wxString::Format(_T("%.1f/%.1f"), toUsrDistance_Plugin(routedDistance),
                          toUsrDistance_Plugin(directDistance));
}

static wxString BuildRouteFailureState(RouteMapOverlay* routemapoverlay) {
  if (!routemapoverlay) return _("Failed");

  wxString explicitReason = routemapoverlay->GetFailureReason();
  if (!explicitReason.IsEmpty()) {
    const wxString lowerReason = explicitReason.Lower();
    const bool resourceLimit =
        lowerReason.Find("maximum generated states") != wxNOT_FOUND ||
        lowerReason.Find("maximum retained states") != wxNOT_FOUND ||
        lowerReason.Find("maximum graph labels") != wxNOT_FOUND ||
        lowerReason.Find("resource limit") != wxNOT_FOUND;
    if (resourceLimit) {
      const RouteMapConfiguration configuration =
          routemapoverlay->GetConfiguration();
      return wxString::Format(
          _("Resource limit reached at %d%% effort "
            "(generated %llu/%llu, retained %llu/%llu, graph labels "
            "%llu/%llu): %s"),
          weather_routing::NormalizeRoutingEffortPercent(
              configuration.RoutingEffortPercent),
          static_cast<unsigned long long>(
              configuration.routing_generated_states),
          static_cast<unsigned long long>(
              configuration.routing_generated_state_limit),
          static_cast<unsigned long long>(
              configuration.routing_retained_states),
          static_cast<unsigned long long>(
              configuration.routing_retained_state_limit),
          static_cast<unsigned long long>(configuration.routing_graph_labels),
          static_cast<unsigned long long>(
              configuration.routing_graph_label_limit),
          explicitReason);
    }
    return explicitReason;
  }

  wxString state;
  bool needsComma = false;
  RouteMapConfiguration configuration = routemapoverlay->GetConfiguration();
  if (configuration.grib_is_data_deficient) {
    state += _("Data Deficient");
    needsComma = true;
  }

  WeatherForecastStatus forecastStatus =
      routemapoverlay->GetWeatherForecastStatus();
  if (forecastStatus != WEATHER_FORECAST_SUCCESS) {
    if (needsComma) state += _T(", ");
    state += RouteMap::GetWeatherForecastStatusMessage(forecastStatus);
    needsComma = true;
  }

  wxString weatherStatus = routemapoverlay->GetWeatherForecastError();
  if (!weatherStatus.IsEmpty()) {
    if (needsComma) state += _T(", ");
    state += _("Grib");
    state += ": ";
    state += weatherStatus;
    needsComma = true;
  }

  PolarSpeedStatus polarStatus = routemapoverlay->GetPolarStatus();
  if (polarStatus != POLAR_SPEED_SUCCESS) {
    if (needsComma) state += _T(", ");
    state += _("Polar");
    state += ": ";
    state += Polar::GetPolarStatusMessage(polarStatus);
    needsComma = true;
  }

  wxString gribError = routemapoverlay->GetGribError();
  if (!gribError.IsEmpty()) {
    if (needsComma) state += _T(", ");
    state += gribError;
    needsComma = true;
  }

  if (routemapoverlay->LandCrossing()) {
    if (needsComma) state += _T(", ");
    state += _("Land");
    state += ": ";
    state += _("Failed");
    needsComma = true;
  }

  if (routemapoverlay->BoundaryCrossing()) {
    if (needsComma) state += _T(", ");
    state += _("Boundary");
    state += ": ";
    state += _("Failed");
    needsComma = true;
  }

  if (!routemapoverlay->ReachedDestination()) {
    if (needsComma) state += _T(", ");
    state += _("Did not reach destination");
    needsComma = true;
  }

  if (routemapoverlay->ReachedDestination() &&
      !routemapoverlay->EndTime().IsValid()) {
    if (needsComma) state += _T(", ");
    state += _("No valid ETA");
    needsComma = true;
  }

  if (state.IsEmpty()) state = _("Failed");
  return state;
}

struct FinalRouteWeatherSourceSummary {
  bool grib{false};
  bool climatology{false};
  long climatologySeconds{0};
};

static FinalRouteWeatherSourceSummary FinalRouteWeatherSources(
    RouteMapOverlay* route) {
  FinalRouteWeatherSourceSummary summary;
  if (!route || !route->Finished() || !route->ReachedDestination())
    return summary;

  for (const PlotData& point : route->GetPlotData(false)) {
    const bool grib = point.data_mask & Position::GRIB_WIND;
    const bool climatology = point.data_mask & Position::CLIMATOLOGY_WIND;
    summary.grib = summary.grib || grib;
    summary.climatology = summary.climatology || climatology;
    if (climatology && std::isfinite(point.delta) && point.delta > 0.0)
      summary.climatologySeconds +=
          static_cast<long>(std::llround(point.delta));
  }
  return summary;
}

static wxString CompactWeatherDuration(long seconds) {
  const long minutes = std::max(1L, (seconds + 30L) / 60L);
  if (minutes < 60) return wxString::Format(_("%ldm"), minutes);
  const long hours = minutes / 60;
  const long remainder = minutes % 60;
  if (!remainder) return wxString::Format(_("%ldh"), hours);
  return wxString::Format(_("%ldh%02ld"), hours, remainder);
}

static void FormatFinalRouteWeatherSources(
    const FinalRouteWeatherSourceSummary& summary, wxString& compact,
    wxString& detail) {
  if (summary.climatology) {
    const wxString duration =
        CompactWeatherDuration(summary.climatologySeconds);
    if (summary.grib) {
      compact = wxString::Format(_("GRIB+Clim %s"), duration);
      detail = wxString::Format(
          _("GRIB + climatology (%s using climatology)"), duration);
    } else {
      compact = wxString::Format(_("Clim %s"), duration);
      detail = wxString::Format(_("Climatology (%s)"), duration);
    }
  } else if (summary.grib) {
    compact = _("GRIB");
    detail = _("GRIB forecast only");
  } else {
    compact = _("N/A");
    detail = _("Weather source unavailable");
  }
}

/* we could speed this up more with another flag for when we need to update
   parameters but not computed route information */
void WeatherRoute::Update(WeatherRouting* wr, bool stateonly) {
  if (!stateonly) {
    RouteMapConfiguration configuration = routemapoverlay->GetConfiguration();

    BoatFilename = configuration.boatFileName;
    // Add handling for Start field based on StartType
    if (configuration.StartType == RouteMapConfiguration::START_FROM_BOAT) {
      Start = _("Boat");
    } else {
      Start = configuration.Start;
    }
    StartType =
        configuration.StartType == RouteMapConfiguration::START_FROM_BOAT
            ? _("From Boat")
        : configuration.StartType == RouteMapConfiguration::START_FROM_WAYPOINT
            ? _("From Waypoint")
            : _("From Position");
    UseCurrentTime =
        configuration.TimeMode ==
                RouteMapConfiguration::ROUTE_BY_DEPARTURE_TIME &&
            configuration.UseCurrentTime
            ? _("true")
            : _("false");
    const bool arrivalPlanning =
        configuration.TimeMode ==
            RouteMapConfiguration::ROUTE_BY_ARRIVAL_TIME &&
        routemapoverlay->Running();
    wxDateTime starttime = configuration.StartTime;
    if (arrivalPlanning)
      StartTime = _("Calculating...");
    else
      StartTime = wr->m_SettingsDialog.FormatTime(starttime,
                                                  _T("%x %H:%M"));

    End = configuration.End;

    wxDateTime endtime = routemapoverlay->EndTime();
    const bool metricsAvailable = IsDisplayMetricAvailable(routemapoverlay);
    if (arrivalPlanning && configuration.PlannedArrivalTime.IsValid()) {
      const wxString plannedArrival = wr->m_SettingsDialog.FormatTime(
          configuration.PlannedArrivalTime, _T("%x %H:%M"));
      EndTime = wxString::Format(_("Target %s"), plannedArrival);
    } else if (metricsAvailable) {
      EndTime = wr->m_SettingsDialog.FormatTime(endtime, _T("%x %H:%M"));
    } else {
      EndTime = _T("N/A");
    }

    // REFACTORING
    // I decided to dedicate a function for displaying the difference
    // between two TimeDate as it is usefull in some other part of the code.
    Time = metricsAvailable ? calculateTimeDelta(starttime, endtime) : _("N/A");

    Distance =
        FormatRouteDistance(routemapoverlay, configuration, metricsAvailable);
    AvgSpeed = FormatRouteMetric(routemapoverlay, RouteMapOverlay::AVGSPEED,
                                 _T("%.1f"), metricsAvailable, true);
    MaxSpeed = FormatRouteMetric(routemapoverlay, RouteMapOverlay::MAXSPEED,
                                 _T("%.1f"), metricsAvailable, true);
    AvgSpeedGround =
        FormatRouteMetric(routemapoverlay, RouteMapOverlay::AVGSPEEDGROUND,
                          _T("%.1f"), metricsAvailable, true);
    MaxSpeedGround =
        FormatRouteMetric(routemapoverlay, RouteMapOverlay::MAXSPEEDGROUND,
                          _T("%.1f"), metricsAvailable, true);
    AvgWind = FormatRouteMetric(routemapoverlay, RouteMapOverlay::AVGWIND,
                                _T("%.1f"), metricsAvailable, true);
    MaxWind = FormatRouteMetric(routemapoverlay, RouteMapOverlay::MAXWIND,
                                _T("%.1f"), metricsAvailable, true);
    MaxWindGust =
        FormatRouteMetric(routemapoverlay, RouteMapOverlay::MAXWINDGUST,
                          _T("%.1f"), metricsAvailable, true);
    AvgCurrent = FormatRouteMetric(routemapoverlay, RouteMapOverlay::AVGCURRENT,
                                   _T("%.1f"), metricsAvailable, true);
    MaxCurrent = FormatRouteMetric(routemapoverlay, RouteMapOverlay::MAXCURRENT,
                                   _T("%.1f"), metricsAvailable, true);
    AvgSwell = FormatRouteMetric(routemapoverlay, RouteMapOverlay::AVGSWELL,
                                 _T("%.1f"), metricsAvailable);
    MaxSwell = FormatRouteMetric(routemapoverlay, RouteMapOverlay::MAXSWELL,
                                 _T("%.1f"), metricsAvailable);
    UpwindPercentage =
        FormatRouteMetric(routemapoverlay, RouteMapOverlay::PERCENTAGE_UPWIND,
                          _T("%.1f%%"), metricsAvailable);

    double ps =
        metricsAvailable
            ? routemapoverlay->RouteInfo(RouteMapOverlay::PORT_STARBOARD)
            : NAN;
    PortStarboard = metricsAvailable && std::isfinite(ps)
                        ? wxString::Format(_T("%.0f/%.0f"), ps, 100 - ps)
                        : _("N/A");

    Tacks = FormatRouteMetric(routemapoverlay, RouteMapOverlay::TACKS,
                              _T("%.0f"), metricsAvailable);
    Jibes = FormatRouteMetric(routemapoverlay, RouteMapOverlay::JIBES,
                              _T("%.0f"), metricsAvailable);
    SailPlanChanges =
        FormatRouteMetric(routemapoverlay, RouteMapOverlay::SAIL_PLAN_CHANGES,
                          _T("%.0f"), metricsAvailable);

    if (metricsAvailable) {
      int comfort_level = routemapoverlay->RouteInfo(RouteMapOverlay::COMFORT);
      Comfort = RouteMapOverlay::sailingConditionText(comfort_level);
    } else {
      Comfort = _("N/A");
    }

    FormatFinalRouteWeatherSources(FinalRouteWeatherSources(routemapoverlay),
                                   WeatherSource, WeatherSourceDetail);
  }

  if (!routemapoverlay->Valid()) {
    State = _("Invalid Start/End");
    wxString error = routemapoverlay->GetError();
    wxString weatherError = routemapoverlay->GetWeatherForecastError();
    if (!error.IsEmpty())
      State += ": " + error;
    else if (!weatherError.IsEmpty())
      State += ": " + weatherError;
  } else if (routemapoverlay->Running())
    State = _("Computing...");
  else {
    if (routemapoverlay->Finished()) {
      if (routemapoverlay->ReachedDestination())
        State = _("Complete");
      else
        State = BuildRouteFailureState(routemapoverlay);
    } else {
      for (std::list<RouteMapOverlay*>::iterator it =
               wr->m_WaitingRouteMaps.begin();
           it != wr->m_WaitingRouteMaps.end(); it++)
        if (*it == routemapoverlay) {
          State = _("Waiting...");
          return;
        }
      State = _("Not Computed");
    }
  }
}

void WeatherRouting::UpdateItem(long index, bool stateonly) {
  WeatherRoute* weatherroute = reinterpret_cast<WeatherRoute*>(
      wxUIntToPtr(m_panel->m_lWeatherRoutes->GetItemData(index)));
  if (!weatherroute) return;

  if (!stateonly) {
    if (columns[VISIBLE] >= 0) {
      m_panel->m_lWeatherRoutes->SetItemImage(
          index, weatherroute->routemapoverlay->m_bEndRouteVisible ? 0 : -1);
      m_panel->m_lWeatherRoutes->SetColumnWidth(columns[VISIBLE], 28);
    }

    if (columns[BOAT] >= 0) {
      m_panel->m_lWeatherRoutes->SetItem(
          index, columns[BOAT],
          wxFileName(weatherroute->BoatFilename).GetName());
      m_panel->m_lWeatherRoutes->SetColumnWidth(columns[BOAT], wxLIST_AUTOSIZE);
    }

    if (columns[START_TYPE] >= 0) {
      m_panel->m_lWeatherRoutes->SetItem(index, columns[START_TYPE],
                                         weatherroute->StartType);
      m_panel->m_lWeatherRoutes->SetColumnWidth(columns[START_TYPE],
                                                wxLIST_AUTOSIZE);
    }

    if (columns[START] >= 0) {
      m_panel->m_lWeatherRoutes->SetItem(index, columns[START],
                                         weatherroute->Start);
      m_panel->m_lWeatherRoutes->SetColumnWidth(columns[START],
                                                wxLIST_AUTOSIZE);
    }

    if (columns[STARTTIME] >= 0) {
      m_panel->m_lWeatherRoutes->SetItem(index, columns[STARTTIME],
                                         weatherroute->StartTime);
      m_panel->m_lWeatherRoutes->SetColumnWidth(columns[STARTTIME],
                                                wxLIST_AUTOSIZE);
    }

    if (columns[END] >= 0) {
      m_panel->m_lWeatherRoutes->SetItem(index, columns[END],
                                         weatherroute->End);
      m_panel->m_lWeatherRoutes->SetColumnWidth(columns[END], wxLIST_AUTOSIZE);
    }

    if (columns[ENDTIME] >= 0) {
      m_panel->m_lWeatherRoutes->SetItem(index, columns[ENDTIME],
                                         weatherroute->EndTime);
      m_panel->m_lWeatherRoutes->SetColumnWidth(columns[ENDTIME],
                                                wxLIST_AUTOSIZE);
    }

    if (columns[TIME] >= 0) {
      m_panel->m_lWeatherRoutes->SetItem(index, columns[TIME],
                                         weatherroute->Time);
      m_panel->m_lWeatherRoutes->SetColumnWidth(columns[TIME], wxLIST_AUTOSIZE);
    }

    if (columns[DISTANCE] >= 0) {
      m_panel->m_lWeatherRoutes->SetItem(index, columns[DISTANCE],
                                         weatherroute->Distance);
      m_panel->m_lWeatherRoutes->SetColumnWidth(columns[DISTANCE],
                                                wxLIST_AUTOSIZE);
    }

    if (columns[AVGSPEED] >= 0) {
      m_panel->m_lWeatherRoutes->SetItem(index, columns[AVGSPEED],
                                         weatherroute->AvgSpeed);
      m_panel->m_lWeatherRoutes->SetColumnWidth(columns[AVGSPEED],
                                                wxLIST_AUTOSIZE);
    }

    if (columns[MAXSPEED] >= 0) {
      m_panel->m_lWeatherRoutes->SetItem(index, columns[MAXSPEED],
                                         weatherroute->MaxSpeed);
      m_panel->m_lWeatherRoutes->SetColumnWidth(columns[MAXSPEED],
                                                wxLIST_AUTOSIZE);
    }

    if (columns[AVGSPEEDGROUND] >= 0) {
      m_panel->m_lWeatherRoutes->SetItem(index, columns[AVGSPEEDGROUND],
                                         weatherroute->AvgSpeedGround);
      m_panel->m_lWeatherRoutes->SetColumnWidth(columns[AVGSPEEDGROUND],
                                                wxLIST_AUTOSIZE);
    }

    if (columns[MAXSPEEDGROUND] >= 0) {
      m_panel->m_lWeatherRoutes->SetItem(index, columns[MAXSPEEDGROUND],
                                         weatherroute->MaxSpeedGround);
      m_panel->m_lWeatherRoutes->SetColumnWidth(columns[MAXSPEEDGROUND],
                                                wxLIST_AUTOSIZE);
    }

    if (columns[AVGWIND] >= 0) {
      m_panel->m_lWeatherRoutes->SetItem(index, columns[AVGWIND],
                                         weatherroute->AvgWind);
      m_panel->m_lWeatherRoutes->SetColumnWidth(columns[AVGWIND],
                                                wxLIST_AUTOSIZE);
    }

    if (columns[MAXWIND] >= 0) {
      m_panel->m_lWeatherRoutes->SetItem(index, columns[MAXWIND],
                                         weatherroute->MaxWind);
      m_panel->m_lWeatherRoutes->SetColumnWidth(columns[MAXWIND],
                                                wxLIST_AUTOSIZE);
    }

    if (columns[MAXWINDGUST] >= 0) {
      m_panel->m_lWeatherRoutes->SetItem(index, columns[MAXWINDGUST],
                                         weatherroute->MaxWindGust);
      m_panel->m_lWeatherRoutes->SetColumnWidth(columns[MAXWINDGUST],
                                                wxLIST_AUTOSIZE);
    }

    if (columns[AVGCURRENT] >= 0) {
      m_panel->m_lWeatherRoutes->SetItem(index, columns[AVGCURRENT],
                                         weatherroute->AvgCurrent);
      m_panel->m_lWeatherRoutes->SetColumnWidth(columns[AVGCURRENT],
                                                wxLIST_AUTOSIZE);
    }

    if (columns[MAXCURRENT] >= 0) {
      m_panel->m_lWeatherRoutes->SetItem(index, columns[MAXCURRENT],
                                         weatherroute->MaxCurrent);
      m_panel->m_lWeatherRoutes->SetColumnWidth(columns[MAXCURRENT],
                                                wxLIST_AUTOSIZE);
    }

    if (columns[AVGSWELL] >= 0) {
      m_panel->m_lWeatherRoutes->SetItem(index, columns[AVGSWELL],
                                         weatherroute->AvgSwell);
      m_panel->m_lWeatherRoutes->SetColumnWidth(columns[AVGSWELL],
                                                wxLIST_AUTOSIZE);
    }

    if (columns[MAXSWELL] >= 0) {
      m_panel->m_lWeatherRoutes->SetItem(index, columns[MAXSWELL],
                                         weatherroute->MaxSwell);
      m_panel->m_lWeatherRoutes->SetColumnWidth(columns[MAXSWELL],
                                                wxLIST_AUTOSIZE);
    }

    if (columns[UPWIND_PERCENTAGE] >= 0) {
      m_panel->m_lWeatherRoutes->SetItem(index, columns[UPWIND_PERCENTAGE],
                                         weatherroute->UpwindPercentage);
      m_panel->m_lWeatherRoutes->SetColumnWidth(columns[UPWIND_PERCENTAGE],
                                                wxLIST_AUTOSIZE);
    }

    if (columns[PORT_STARBOARD] >= 0) {
      m_panel->m_lWeatherRoutes->SetItem(index, columns[PORT_STARBOARD],
                                         weatherroute->PortStarboard);
      m_panel->m_lWeatherRoutes->SetColumnWidth(columns[PORT_STARBOARD],
                                                wxLIST_AUTOSIZE);
    }

    if (columns[TACKS] >= 0) {
      m_panel->m_lWeatherRoutes->SetItem(index, columns[TACKS],
                                         weatherroute->Tacks);
      m_panel->m_lWeatherRoutes->SetColumnWidth(columns[TACKS],
                                                wxLIST_AUTOSIZE);
    }

    if (columns[JIBES] >= 0) {
      m_panel->m_lWeatherRoutes->SetItem(index, columns[JIBES],
                                         weatherroute->Jibes);
      m_panel->m_lWeatherRoutes->SetColumnWidth(columns[JIBES],
                                                wxLIST_AUTOSIZE);
    }

    if (columns[SAIL_PLAN_CHANGES] >= 0) {
      m_panel->m_lWeatherRoutes->SetItem(index, columns[SAIL_PLAN_CHANGES],
                                         weatherroute->SailPlanChanges);
      m_panel->m_lWeatherRoutes->SetColumnWidth(columns[SAIL_PLAN_CHANGES],
                                                wxLIST_AUTOSIZE);
    }

    if (columns[COMFORT] >= 0) {
      m_panel->m_lWeatherRoutes->SetItem(index, columns[COMFORT],
                                         weatherroute->Comfort);
      m_panel->m_lWeatherRoutes->SetColumnWidth(columns[COMFORT],
                                                wxLIST_AUTOSIZE);
    }

    if (columns[WEATHER_SOURCE] >= 0) {
      m_panel->m_lWeatherRoutes->SetItem(index, columns[WEATHER_SOURCE],
                                         weatherroute->WeatherSource);
      m_panel->m_lWeatherRoutes->SetColumnWidth(columns[WEATHER_SOURCE],
                                                wxLIST_AUTOSIZE);
    }
  }

  if (columns[STATE] >= 0) {
    m_panel->m_lWeatherRoutes->SetItem(index, columns[STATE],
                                       weatherroute->State);
    m_panel->m_lWeatherRoutes->SetColumnWidth(columns[STATE], wxLIST_AUTOSIZE);
  }
}

// The configuration changed, so stop computation and update the display in the
// list
void WeatherRouting::SetConfigurationRoute(WeatherRoute* weatherroute) {
  if (m_bSkipUpdateCurrentItems) return;

  RouteMapOverlay* rmo = weatherroute->routemapoverlay;
  for (std::list<RouteMapOverlay*>::iterator it = m_RunningRouteMaps.begin();
       it != m_RunningRouteMaps.end(); it++)
    if (*it == rmo && rmo->Running()) rmo->DeleteThread();

  if (m_ApplyingMultiLegGroupSettings && rmo &&
      !m_MultiLegSettingsGroupId.IsEmpty()) {
    RouteMapConfiguration configuration = rmo->GetConfiguration();
    if (configuration.IsMultiLegGenerated &&
        configuration.MultiLegGroupId == m_MultiLegSettingsGroupId) {
      PreserveMultiLegLegFields(rmo, configuration);
      rmo->SetConfiguration(configuration);
      if (!RouteMapIsWaitingOrRunning(rmo)) rmo->Reset();
    }
  }

  weatherroute->Update(this);

  for (long index = 0; index < m_panel->m_lWeatherRoutes->GetItemCount();
       index++) {
    WeatherRoute* wr = reinterpret_cast<WeatherRoute*>(
        wxUIntToPtr(m_panel->m_lWeatherRoutes->GetItemData(index)));
    if (weatherroute == wr) {
      UpdateItem(index);
      break;
    }
  }
}

void WeatherRouting::UpdateBoatFilename(wxString boatFileName) {
  for (long index = 0; index < m_panel->m_lWeatherRoutes->GetItemCount();
       index++) {
    WeatherRoute* weatherroute = reinterpret_cast<WeatherRoute*>(
        wxUIntToPtr(m_panel->m_lWeatherRoutes->GetItemData(index)));

    RouteMapConfiguration c = weatherroute->routemapoverlay->GetConfiguration();
    if (c.boatFileName == boatFileName) {
      RouteMapOverlay* rmo = weatherroute->routemapoverlay;
      rmo->ResetFinished();
      SetConfigurationRoute(weatherroute);
    }
  }
}

void WeatherRouting::UpdateCurrentConfigurations() {
  long index = -1;
  for (;;) {
    index = m_panel->m_lWeatherRoutes->GetNextItem(index, wxLIST_NEXT_ALL,
                                                   wxLIST_STATE_SELECTED);
    if (index == -1) break;
    WeatherRoute* weatherroute = reinterpret_cast<WeatherRoute*>(
        wxUIntToPtr(m_panel->m_lWeatherRoutes->GetItemData(index)));
    SetConfigurationRoute(weatherroute);
  }
}

void WeatherRouting::UpdateStates() {
  for (std::list<WeatherRoute*>::iterator it = m_WeatherRoutes.begin();
       it != m_WeatherRoutes.end(); it++)
    (*it)->Update(this, true);
  for (int i = 0; i < m_panel->m_lWeatherRoutes->GetItemCount(); i++)
    UpdateItem(i, true);
}

std::list<RouteMapOverlay*> WeatherRouting::CurrentRouteMaps(
    bool messagedialog) {
  std::list<RouteMapOverlay*> routemapoverlays;
  long index = -1;
  if (m_panel)
    for (;;) {
      index = m_panel->m_lWeatherRoutes->GetNextItem(index, wxLIST_NEXT_ALL,
                                                     wxLIST_STATE_SELECTED);
      if (index == -1) break;
      routemapoverlays.push_back(
          reinterpret_cast<WeatherRoute*>(
              wxUIntToPtr(m_panel->m_lWeatherRoutes->GetItemData(index)))
              ->routemapoverlay);
    }

  if (messagedialog && routemapoverlays.empty()) {
    wxMessageDialog mdlg(this, _("No Weather Route selected"),
                         _("Weather Routing"), wxOK | wxICON_WARNING);
    mdlg.ShowModal();
  }

  return routemapoverlays;
}

RouteMapOverlay* WeatherRouting::FirstCurrentRouteMap() {
  std::list<RouteMapOverlay*> currentroutemaps = CurrentRouteMaps();
  return currentroutemaps.empty() ? NULL : currentroutemaps.front();
}

void WeatherRouting::RebuildList() {
  m_panel->m_lWeatherRoutes->DeleteAllItems();
  for (std::list<WeatherRoute*>::iterator it = m_WeatherRoutes.begin();
       it != m_WeatherRoutes.end(); it++) {
    if (!(*it)->Filtered) {
      wxListItem item;
      item.SetId(m_panel->m_lWeatherRoutes->GetItemCount());
      item.SetData(*it);
      UpdateItem(m_panel->m_lWeatherRoutes->InsertItem(item));
    }
  }
}

bool WeatherRouting::ValidateCompletedRouteForDisplay(
    RouteMapOverlay* routemapoverlay) {
  wxStopWatch timer;
  if (!routemapoverlay) return true;
  if (!routemapoverlay->Finished() || !routemapoverlay->ReachedDestination())
    return true;

  RouteMapConfiguration configuration = routemapoverlay->GetConfiguration();
  if (!configuration.DetectLand) return true;

  bool use_experimental_chart_safety = false;
  bool enforce_experimental_chart_safety = false;
  ReadExperimentalChartSafetySettings(use_experimental_chart_safety,
                                      enforce_experimental_chart_safety);
  if (!use_experimental_chart_safety || !enforce_experimental_chart_safety)
    return true;

  ConstraintChecker::ResetSegmentSafetyDiagnostics(
      use_experimental_chart_safety, enforce_experimental_chart_safety);
  ConstraintChecker::SetSegmentSafetyDiagnosticContext(wxString::Format(
      _("route_display route=\"%s to %s\" group=%s candidate_offset=%d "
        "leg=%d/%d"),
      configuration.Start, configuration.End, configuration.MultiLegGroupId,
      configuration.DepartureTimeOptimizationOffsetMinutes,
      configuration.MultiLegLegIndex, configuration.MultiLegLegCount));

  bool valid =
      routemapoverlay->UsesModernNativeResult()
          ? routemapoverlay->ValidatePlottedDestinationRouteLand(configuration)
          : routemapoverlay->ValidateDestinationRouteLand(configuration) &&
                routemapoverlay->ValidatePlottedDestinationRouteLand(
                    configuration);
  routemapoverlay->SetConfigurationPreserveResult(configuration);

  long totalMs = timer.Time();
  wxLogMessage(
      "FINAL_ROUTE_SAFETY display_validation route=\"%s -> %s\" pass=%d "
      "finished=%d reached=%d",
      configuration.Start, configuration.End, valid ? 1 : 0,
      routemapoverlay->Finished() ? 1 : 0,
      routemapoverlay->ReachedDestination() ? 1 : 0);
  wxLogMessage(
      "WR_UI_TIMING ValidateCompletedRouteForDisplay total_ms=%ld "
      "route=\"%s -> %s\" chart_validation=1 ui_thread=%d pass=%d "
      "candidate_offset=%d leg=%d/%d progress_dialog=%d",
      totalMs, configuration.Start, configuration.End,
      wxThread::IsMain() ? 1 : 0, valid ? 1 : 0,
      configuration.DepartureTimeOptimizationOffsetMinutes,
      configuration.MultiLegLegIndex, configuration.MultiLegLegCount,
      m_RoutingProgressDialog && m_RoutingProgressDialog->IsShown() ? 1 : 0);
  ConstraintChecker::LogSegmentSafetyDiagnostics(
      _("normal route display validation"));
  return valid;
}

bool WeatherRouting::ValidateRouteForOutput(RouteMapOverlay& routemapoverlay,
                                            const wxString& action) {
  RouteMapConfiguration configuration = routemapoverlay.GetConfiguration();
  if (!configuration.DetectLand) return true;

  bool use_experimental_chart_safety = false;
  bool enforce_experimental_chart_safety = false;
  ReadExperimentalChartSafetySettings(use_experimental_chart_safety,
                                      enforce_experimental_chart_safety);
  if (!use_experimental_chart_safety || !enforce_experimental_chart_safety)
    return true;

  ConstraintChecker::ResetSegmentSafetyDiagnostics(
      use_experimental_chart_safety, enforce_experimental_chart_safety);
  ConstraintChecker::SetSegmentSafetyDiagnosticContext(wxString::Format(
      _("route_output action=\"%s\" route=\"%s to %s\" group=%s leg=%d/%d"),
      action, configuration.Start, configuration.End,
      configuration.MultiLegGroupId, configuration.MultiLegLegIndex,
      configuration.MultiLegLegCount));

  if (routemapoverlay.ValidatePlottedDestinationRouteLand(configuration))
    return true;

  routemapoverlay.SetConfigurationPreserveResult(configuration);
  UpdateRouteMap(&routemapoverlay);
  ConstraintChecker::LogSegmentSafetyDiagnostics(_("route output validation"));

  wxString reason = routemapoverlay.GetFailureReason();
  if (reason.empty()) reason = _("Chart land crossing in final route");
  wxMessageDialog mdlg(
      this,
      wxString::Format(_("%s was blocked because the plotted weather route is "
                         "not chart-safe.\n\n%s"),
                       action, reason),
      _("Weather Routing"), wxOK | wxICON_WARNING);
  mdlg.ShowModal();
  return false;
}

bool WeatherRouting::CollectChartSafetyScoutGeometry(
    RouteMapOverlay* routemapoverlay,
    std::vector<std::pair<double, double> >* geometry,
    std::vector<RouteMapFrontierSegment>* retained_segments,
    bool* reached_destination, const std::function<void(long)>& heartbeat) {
  if (geometry) geometry->clear();
  if (retained_segments) retained_segments->clear();
  if (reached_destination) *reached_destination = false;
  if (!routemapoverlay || !geometry) return false;

  RouteMapConfiguration original = routemapoverlay->GetConfiguration();
  if (!original.DetectLand || !original.RouteGUID.IsEmpty() ||
      original.chart_safety_missing_tile_retry_count > 0) {
    return false;
  }

  bool use_chart_safety = false;
  bool enforce_chart_safety = false;
  ReadExperimentalChartSafetySettings(use_chart_safety, enforce_chart_safety);
  if (!use_chart_safety || !enforce_chart_safety) return false;
  RouteMapConfiguration scout = original;
  // Preserve normal weather/polar/current physics and cheap GSHHS avoidance,
  // but explicitly prevent the scout worker from querying chart route masks.
  scout.DetectLand = true;
  scout.UseChartSafetyForPropagation = false;
  scout.ChartSafetyPropagationFallbackTried = false;
  scout.chart_safety_missing_tile_rejections = 0;
  scout.chart_safety_missing_tile_first_lat_tile = 0;
  scout.chart_safety_missing_tile_first_lon_tile = 0;
  scout.chart_safety_missing_tile_first_min_lat = NAN;
  scout.chart_safety_missing_tile_first_min_lon = NAN;
  scout.chart_safety_missing_tile_min_lat = NAN;
  scout.chart_safety_missing_tile_max_lat = NAN;
  scout.chart_safety_missing_tile_min_lon = NAN;
  scout.chart_safety_missing_tile_max_lon = NAN;
  // A scout is a geometry/prewarm hint only; it is never returned to the user
  // and cannot bypass the authoritative chart-backed solve or final replay.
  // Ten-degree primary headings materially reduce scout work while the normal
  // recovery stages retain five-degree refinement for narrow approaches.
  scout.ByDegrees = wxMax(10.0, scout.ByDegrees);
  scout.chart_safety_scout_preview = true;

  wxLogMessage(
      "WR_SCOUT_ROUTE start route=\"%s to %s\" departure=\"%s\" "
      "candidate_offset=%d leg=%d/%d use_grib=%d delta_time=%.0f.",
      original.Start, original.End,
      original.StartTime.IsValid() ? original.StartTime.FormatISOCombined()
                                   : wxString("invalid"),
      original.DepartureTimeOptimizationOffsetMinutes,
      original.MultiLegLegIndex, original.MultiLegLegCount,
      original.UseGrib ? 1 : 0, original.DeltaTime);

  routemapoverlay->SetConfiguration(scout);
  routemapoverlay->Reset();

  wxString error;
  wxStopWatch timer;
  if (!routemapoverlay->Start(error)) {
    wxLogMessage(
        "WR_SCOUT_ROUTE fail route=\"%s to %s\" status=start_error "
        "error=\"%s\" scout_time_ms=%ld.",
        original.Start, original.End, error, timer.Time());
    routemapoverlay->SetConfiguration(original);
    routemapoverlay->Reset();
    return false;
  }

  bool watchdog_expired = false;
  long next_heartbeat_ms = 250;
  while (routemapoverlay->Running()) {
    if (routemapoverlay->NeedsGrib() && !routemapoverlay->Finished()) {
      RequestGribTimelineFrame(routemapoverlay, routemapoverlay->NewTime());
    }

    const long elapsed_ms = timer.Time();
    if (heartbeat && elapsed_ms >= next_heartbeat_ms) {
      heartbeat(elapsed_ms);
      next_heartbeat_ms = elapsed_ms + 250;
    }

    if (elapsed_ms > kChartSafetyScoutWatchdogMs) {
      watchdog_expired = true;
      routemapoverlay->Stop();
      break;
    }

    wxMilliSleep(20);
  }

  while (routemapoverlay->Running()) {
    wxMilliSleep(20);
  }
  routemapoverlay->DeleteThread();

  if (watchdog_expired) {
    wxLogMessage(
        "WR_SCOUT_ROUTE watchdog route=\"%s to %s\" status=timeout "
        "scout_time_ms=%ld action=retain-partial-frontier-for-prefetch.",
        original.Start, original.End, timer.Time());
  }

  const bool complete =
      routemapoverlay->Finished() && routemapoverlay->ReachedDestination();
  if (retained_segments)
    *retained_segments = routemapoverlay->GetRetainedFrontierSegments();
  geometry->push_back(std::make_pair(original.StartLat, original.StartLon));
  if (complete) {
    std::list<PlotData> scout_plot = routemapoverlay->GetPlotData(false);
    for (std::list<PlotData>::const_iterator it = scout_plot.begin();
         it != scout_plot.end(); ++it) {
      if (!std::isfinite(it->lat) || !std::isfinite(it->lon)) continue;
      if (geometry->empty() || DistGreatCircle_Plugin(geometry->back().first,
                                                      geometry->back().second,
                                                      it->lat, it->lon) > 0.01)
        geometry->push_back(std::make_pair(it->lat, it->lon));
    }
    geometry->push_back(std::make_pair(original.EndLat, original.EndLon));
  } else {
    std::vector<std::pair<double, double> > partial =
        routemapoverlay->GetClosestFrontierGeometry();
    for (std::vector<std::pair<double, double> >::const_iterator it =
             partial.begin();
         it != partial.end(); ++it) {
      if (geometry->empty() ||
          DistGreatCircle_Plugin(geometry->back().first,
                                 geometry->back().second, it->first,
                                 it->second) > 0.01)
        geometry->push_back(*it);
    }
  }

  double progress_nm = 0.0;
  for (size_t i = 1; i < geometry->size(); ++i)
    progress_nm += DistGreatCircle_Plugin(
        (*geometry)[i - 1].first, (*geometry)[i - 1].second,
        (*geometry)[i].first, (*geometry)[i].second);
  const double leg_nm = DistGreatCircle_Plugin(
      original.StartLat, original.StartLon, original.EndLat, original.EndLon);
  const double meaningful_progress_nm = wxMin(10.0, wxMax(2.0, 0.05 * leg_nm));
  if (!complete && progress_nm < meaningful_progress_nm) geometry->clear();

  if (!complete) {
    wxString reason = routemapoverlay->GetFailureReason();
    wxLogMessage(
        "WR_SCOUT_ROUTE partial route=\"%s to %s\" status=no_route "
        "finished=%d reached=%d watchdog_expired=%d scout_time_ms=%ld "
        "points=%lu "
        "progress_nm=%.3f meaningful=%d reason=\"%s\".",
        original.Start, original.End, routemapoverlay->Finished() ? 1 : 0,
        routemapoverlay->ReachedDestination() ? 1 : 0,
        watchdog_expired ? 1 : 0, timer.Time(),
        static_cast<unsigned long>(geometry->size()), progress_nm,
        geometry->empty() ? 0 : 1, reason);
  } else {
    wxLogMessage(
        "WR_SCOUT_ROUTE complete route=\"%s to %s\" scout_time_ms=%ld "
        "scout_status=destination points=%lu retained_segments=%lu "
        "distance_nm=%.3f "
        "used_as_hint_only=1.",
        original.Start, original.End, timer.Time(),
        static_cast<unsigned long>(geometry->size()),
        static_cast<unsigned long>(retained_segments ? retained_segments->size()
                                                     : 0),
        progress_nm);
  }

  routemapoverlay->SetConfiguration(original);
  routemapoverlay->Reset();
  if (reached_destination) *reached_destination = complete;
  return geometry->size() >= 2;
}

static bool ChartSafetyScoutSegmentIsEndpointAdjacent(
    const RouteMapConfiguration& configuration,
    const RouteMapFrontierSegment& segment) {
  const double endpoint_coordinate_tolerance_nm = 0.001;
  const double start_reach =
      wxMax(endpoint_coordinate_tolerance_nm,
            configuration.chart_safety_start_endpoint_reach_nm);
  const double end_reach =
      wxMax(endpoint_coordinate_tolerance_nm,
            configuration.chart_safety_end_endpoint_reach_nm);
  return DistGreatCircle_Plugin(configuration.StartLat, configuration.StartLon,
                                segment.lat1, segment.lon1) <= start_reach ||
         DistGreatCircle_Plugin(configuration.StartLat, configuration.StartLon,
                                segment.lat2, segment.lon2) <= start_reach ||
         DistGreatCircle_Plugin(configuration.EndLat, configuration.EndLon,
                                segment.lat1, segment.lon1) <= end_reach ||
         DistGreatCircle_Plugin(configuration.EndLat, configuration.EndLon,
                                segment.lat2, segment.lon2) <= end_reach;
}

static void SetChartSafetyScoutEndpointReach(
    RouteMapConfiguration* configuration,
    const std::vector<std::pair<double, double> >& selected_points,
    const std::vector<RouteMapFrontierSegment>& retained_segments,
    bool complete) {
  if (!configuration) return;
  configuration->chart_safety_start_endpoint_reach_nm = 0.0;
  configuration->chart_safety_end_endpoint_reach_nm = 0.0;
  if (!complete || selected_points.size() < 2) return;

  const double tolerance_nm = 0.001;
  for (std::vector<RouteMapFrontierSegment>::const_iterator it =
           retained_segments.begin();
       it != retained_segments.end(); ++it) {
    double start1 = DistGreatCircle_Plugin(
        configuration->StartLat, configuration->StartLon, it->lat1, it->lon1);
    double start2 = DistGreatCircle_Plugin(
        configuration->StartLat, configuration->StartLon, it->lat2, it->lon2);
    if (start1 <= tolerance_nm || start2 <= tolerance_nm)
      configuration->chart_safety_start_endpoint_reach_nm =
          wxMax(configuration->chart_safety_start_endpoint_reach_nm,
                wxMax(start1, start2));

    double end1 = DistGreatCircle_Plugin(
        configuration->EndLat, configuration->EndLon, it->lat1, it->lon1);
    double end2 = DistGreatCircle_Plugin(
        configuration->EndLat, configuration->EndLon, it->lat2, it->lon2);
    if (end1 <= tolerance_nm || end2 <= tolerance_nm)
      configuration->chart_safety_end_endpoint_reach_nm = wxMax(
          configuration->chart_safety_end_endpoint_reach_nm, wxMax(end1, end2));
  }

  for (size_t i = 1; i < selected_points.size(); ++i) {
    double reach = DistGreatCircle_Plugin(
        configuration->StartLat, configuration->StartLon,
        selected_points[i].first, selected_points[i].second);
    if (reach > tolerance_nm) {
      configuration->chart_safety_start_endpoint_reach_nm =
          wxMax(configuration->chart_safety_start_endpoint_reach_nm, reach);
      break;
    }
  }
  for (size_t i = selected_points.size() - 1; i > 0; --i) {
    double reach = DistGreatCircle_Plugin(
        configuration->EndLat, configuration->EndLon,
        selected_points[i - 1].first, selected_points[i - 1].second);
    if (reach > tolerance_nm) {
      configuration->chart_safety_end_endpoint_reach_nm =
          wxMax(configuration->chart_safety_end_endpoint_reach_nm, reach);
      break;
    }
  }

  // A coarse scout leg can be several miles long.  Using that full length as
  // a scalar endpoint exception would relax the configured stand-off much
  // farther than needed.  The native engine integrates motion in five-minute
  // slices, so a tightly bounded local reach is sufficient; every relaxed
  // slice must still pass a separate authoritative zero-margin chart check.
  const double maximum_endpoint_reach_nm =
      wxMin(2.0, wxMax(0.5, 2.5 * configuration->SafetyMarginLand));
  configuration->chart_safety_start_endpoint_reach_nm =
      wxMin(configuration->chart_safety_start_endpoint_reach_nm,
            maximum_endpoint_reach_nm);
  configuration->chart_safety_end_endpoint_reach_nm =
      wxMin(configuration->chart_safety_end_endpoint_reach_nm,
            maximum_endpoint_reach_nm);
}

void WeatherRouting::PrepareChartSafetyScoutEnvelopes(
    const std::vector<RouteMapOverlay*>& routemapoverlays,
    const wxString& context) {
  if (routemapoverlays.empty()) return;
  ScopedRoutePreparation route_preparation(m_RoutePreparationDepth);

  bool use_chart_safety = false;
  bool enforce_chart_safety = false;
  ReadExperimentalChartSafetySettings(use_chart_safety, enforce_chart_safety);
  if (!use_chart_safety || !enforce_chart_safety) return;

  std::vector<RouteMapConfiguration> snapshot_configurations;
  bool legacy_full_chart_propagation = false;
  for (std::vector<RouteMapOverlay*>::const_iterator route =
           routemapoverlays.begin();
       route != routemapoverlays.end(); ++route)
    if (*route && (*route)->GetConfiguration().DetectLand) {
      const RouteMapConfiguration configuration = (*route)->GetConfiguration();
      snapshot_configurations.push_back(configuration);
      legacy_full_chart_propagation =
          legacy_full_chart_propagation ||
          (configuration.UseChartSafetyForPropagation &&
           !ModernNativeRouteEnabled(configuration));
    }
  if (legacy_full_chart_propagation)
    PrewarmChartSafetyHazardSnapshot(snapshot_configurations, context, true);

  struct ScoutEnvelope {
    RouteMapConfiguration configuration;
    std::vector<std::pair<double, double> > points;
    std::vector<RouteMapFrontierSegment> retained_segments;
    bool complete;
    wxString scope;
  };
  std::map<wxString, std::vector<ScoutEnvelope> > groups;
  std::map<wxString, ScoutEnvelope> reusable_scouts;
  int scout_total = 0;
  for (std::vector<RouteMapOverlay*>::const_iterator route =
           routemapoverlays.begin();
       route != routemapoverlays.end(); ++route) {
    if (!*route) continue;
    const RouteMapConfiguration configuration = (*route)->GetConfiguration();
    if (!configuration.DetectLand ||
        configuration.chart_safety_missing_tile_retry_count > 0)
      continue;
    const wxString scope = ChartSafetySharedPrewarmScopeKey(configuration);
    if (s_chartSafetySharedPrewarmScopes.find(scope) ==
        s_chartSafetySharedPrewarmScopes.end())
      ++scout_total;
  }
  int scouts_completed = 0;
  wxStopWatch scout_progress_timer;
  for (std::vector<RouteMapOverlay*>::const_iterator route =
           routemapoverlays.begin();
       route != routemapoverlays.end(); ++route) {
    if (!*route) continue;
    RouteMapConfiguration configuration = (*route)->GetConfiguration();
    if (!configuration.DetectLand ||
        configuration.chart_safety_missing_tile_retry_count > 0)
      continue;
    wxString scope = ChartSafetySharedPrewarmScopeKey(configuration);
    if (s_chartSafetySharedPrewarmScopes.find(scope) !=
        s_chartSafetySharedPrewarmScopes.end())
      continue;

    const int scout_number = scouts_completed + 1;
    const wxString departure =
        configuration.StartTime.IsValid()
            ? configuration.StartTime.FormatISOCombined(' ') + _(" UTC")
            : _("unspecified time");
    const wxString scout_stage =
        scout_total > 1 ? _("Surveying chart-safe route alternatives")
                        : _("Surveying chart-safe route corridor");
    if (m_RoutingProgressDialog && m_RoutingProgressDialog->IsShown()) {
      UpdateRoutingProgress(
          scout_stage,
          wxString::Format(
              _("Chart-safety scout %d of %d: %s to %s, departure %s. "
                "Working; each scout may take a few seconds."),
              scout_number, wxMax(1, scout_total), configuration.Start,
              configuration.End, departure),
          scouts_completed, wxMax(1, scout_total));
    }

    std::map<wxString, ScoutEnvelope>::const_iterator reusable =
        reusable_scouts.find(scope);
    if (reusable != reusable_scouts.end()) {
      configuration.chart_safety_start_endpoint_reach_nm =
          reusable->second.configuration.chart_safety_start_endpoint_reach_nm;
      configuration.chart_safety_end_endpoint_reach_nm =
          reusable->second.configuration.chart_safety_end_endpoint_reach_nm;
      (*route)->SetConfiguration(configuration);
      (*route)->Reset();
      wxLogMessage(
          "WR_SCOUT_ROUTE reused scope=%s route=\"%s to %s\" "
          "source=shared-route-family-scout start_reach_nm=%.3f "
          "end_reach_nm=%.3f",
          scope, configuration.Start, configuration.End,
          configuration.chart_safety_start_endpoint_reach_nm,
          configuration.chart_safety_end_endpoint_reach_nm);
      s_chartSafetyPreparedScoutScopes.insert(scope);
      ++scouts_completed;
      if (m_RoutingProgressDialog && m_RoutingProgressDialog->IsShown())
        UpdateRoutingProgress(
            scout_stage,
            wxString::Format(_("Completed %d of %d chart-safety scouts."),
                             scouts_completed, wxMax(1, scout_total)),
            scouts_completed, wxMax(1, scout_total));
      continue;
    }

    ScoutEnvelope envelope;
    envelope.configuration = configuration;
    envelope.complete = false;
    envelope.scope = scope;
    long last_heartbeat_second = -1;
    const std::function<void(long)> heartbeat =
        [this, &last_heartbeat_second](long elapsed_ms) {
          const long elapsed_second = elapsed_ms / 1000;
          if (elapsed_second == last_heartbeat_second) return;
          last_heartbeat_second = elapsed_second;
          // Scout workers are deliberately polled without yielding the event
          // loop: yielding here can re-enter partially constructed routing
          // state. Force only the progress widgets to repaint so the elapsed
          // clock remains visibly alive during this bounded wait.
          RefreshRoutingProgressTiming();
          PaintRoutingProgressNow();
        };
    if (!CollectChartSafetyScoutGeometry(
            *route, &envelope.points, &envelope.retained_segments,
            &envelope.complete, heartbeat)) {
      envelope.points.push_back(
          std::make_pair(configuration.StartLat, configuration.StartLon));
      envelope.points.push_back(
          std::make_pair(configuration.EndLat, configuration.EndLon));
      RouteMapFrontierSegment direct = {
          configuration.StartLat, configuration.StartLon, configuration.EndLat,
          configuration.EndLon};
      envelope.retained_segments.push_back(direct);
      wxLogMessage("WR_SCOUT_ROUTE fallback_direct route=\"%s to %s\" scope=%s",
                   configuration.Start, configuration.End, scope);
    }
    SetChartSafetyScoutEndpointReach(&envelope.configuration, envelope.points,
                                     envelope.retained_segments,
                                     envelope.complete);
    (*route)->SetConfiguration(envelope.configuration);
    (*route)->Reset();
    s_chartSafetyPreparedScoutScopes.insert(scope);
    wxLogMessage(
        "WR_SCOUT_ENDPOINT_REACH route=\"%s to %s\" complete=%d "
        "start_reach_nm=%.3f end_reach_nm=%.3f source=scout-frontier",
        envelope.configuration.Start, envelope.configuration.End,
        envelope.complete ? 1 : 0,
        envelope.configuration.chart_safety_start_endpoint_reach_nm,
        envelope.configuration.chart_safety_end_endpoint_reach_nm);
    reusable_scouts[scope] = envelope;
    groups[ChartSafetyScoutEnvelopeGroupKey(configuration)].push_back(
        envelope);
    ++scouts_completed;
    if (m_RoutingProgressDialog && m_RoutingProgressDialog->IsShown()) {
      wxString detail = wxString::Format(
          _("Completed %d of %d chart-safety scouts."), scouts_completed,
          wxMax(1, scout_total));
      if (scouts_completed < scout_total && scouts_completed > 0) {
        const long elapsed_ms = scout_progress_timer.Time();
        const long remaining_seconds =
            ((elapsed_ms / scouts_completed) *
             (scout_total - scouts_completed) +
             999) /
            1000;
        detail += wxString::Format(
            _(" Approximately %ld seconds of scouting remain."),
            remaining_seconds);
      }
      UpdateRoutingProgress(scout_stage, detail, scouts_completed,
                            wxMax(1, scout_total));
    }
  }

  for (std::map<wxString, std::vector<ScoutEnvelope> >::iterator group =
           groups.begin();
       group != groups.end(); ++group) {
    if (group->second.empty()) continue;
    RouteMapConfiguration representative = group->second.front().configuration;
    // A small spatial dilation plus the fine-tile halo covers interpolation
    // between candidate-specific scout fronts. It is a preparation envelope,
    // not a declaration that GSHHS-certified water is safe.
    const double footprint_dilation_nm = 2.0;
    const int footprint_fine_tile_halo = 1;

    std::vector<double> latitudes;
    std::vector<double> longitudes;
    std::vector<int> point_counts;
    std::vector<double> endpoint_latitudes;
    std::vector<double> endpoint_longitudes;
    std::vector<int> endpoint_point_counts;
    int complete_scouts = 0;
    int partial_scouts = 0;
    int retained_segments = 0;
    int endpoint_segments = 0;
    for (std::vector<ScoutEnvelope>::const_iterator envelope =
             group->second.begin();
         envelope != group->second.end(); ++envelope) {
      if (envelope->complete)
        ++complete_scouts;
      else
        ++partial_scouts;

      std::vector<RouteMapFrontierSegment> segments;
      if (envelope->complete) {
        segments = envelope->retained_segments;
      } else {
        // A partial scout's full retained frontier can span the entire search
        // fan and force thousands of irrelevant CM93 tiles to be rasterised.
        // Keep frontier edges in a generous corridor around the meaningful
        // best-known chain. Missing alternatives remain fail-closed and trigger
        // the existing bounded on-demand tile retry/prewarm path.
        const double route_distance_nm = DistGreatCircle_Plugin(
            envelope->configuration.StartLat,
            envelope->configuration.StartLon,
            envelope->configuration.EndLat,
            envelope->configuration.EndLon);
        const double partial_scout_corridor_nm =
            wxMin(40.0, wxMax(12.0, route_distance_nm * 0.15));
        for (const auto& segment : envelope->retained_segments) {
          bool near_selected_chain = false;
          for (const auto& point : envelope->points) {
            if (DistGreatCircle_Plugin(segment.lat1, segment.lon1, point.first,
                                       point.second) <=
                    partial_scout_corridor_nm ||
                DistGreatCircle_Plugin(segment.lat2, segment.lon2, point.first,
                                       point.second) <=
                    partial_scout_corridor_nm) {
              near_selected_chain = true;
              break;
            }
          }
          if (near_selected_chain) segments.push_back(segment);
        }
      }
      // The destination connection is not always stored as an isochrone
      // parent edge.  Include the selected scout chain as well, while the
      // retained frontier remains the source of alternative-route coverage.
      for (size_t point = 1; point < envelope->points.size(); ++point) {
        RouteMapFrontierSegment selected = {envelope->points[point - 1].first,
                                            envelope->points[point - 1].second,
                                            envelope->points[point].first,
                                            envelope->points[point].second};
        segments.push_back(selected);
      }
      // Always include the direct spine. It may cross coarse GSHHS land; that
      // is precisely why it is only used to fetch authoritative evidence.
      // Including it prevents a partial scout from leaving a gap between its
      // closest frontier and the destination.
      segments.push_back({envelope->configuration.StartLat,
                          envelope->configuration.StartLon,
                          envelope->configuration.EndLat,
                          envelope->configuration.EndLon});

      for (std::vector<RouteMapFrontierSegment>::const_iterator segment =
               segments.begin();
           segment != segments.end(); ++segment) {
        latitudes.push_back(segment->lat1);
        longitudes.push_back(segment->lon1);
        latitudes.push_back(segment->lat2);
        longitudes.push_back(segment->lon2);
        point_counts.push_back(2);
        ++retained_segments;

        bool endpoint_touch = ChartSafetyScoutSegmentIsEndpointAdjacent(
            envelope->configuration, *segment);
        if (endpoint_touch) {
          endpoint_latitudes.push_back(segment->lat1);
          endpoint_longitudes.push_back(segment->lon1);
          endpoint_latitudes.push_back(segment->lat2);
          endpoint_longitudes.push_back(segment->lon2);
          endpoint_point_counts.push_back(2);
          ++endpoint_segments;
        }
      }
    }

    if (m_RoutingProgressDialog && m_RoutingProgressDialog->IsShown()) {
      UpdateRoutingProgress(
          _("Building chart safety grid"),
          wxString::Format(_("Preparing from %lu route scouts"),
                           static_cast<unsigned long>(group->second.size())),
          -1, -1);
    }

    PlugInSegmentSafetyOptions options =
        ChartSafetyRouteMaskOptions(representative);
    PlugInSegmentSafetyOptions search_options =
        ChartSafetySearchRouteMaskOptions(representative);
    const bool search_mask_differs = std::fabs(search_options.safety_margin_nm -
                                               options.safety_margin_nm) > 1e-9;
    PlugInSegmentSafetyResult result = {};
    result.struct_size = sizeof(result);
    wxStopWatch timer;
    const bool prewarm_full_corridor =
        representative.UseChartSafetyForPropagation;

    // Union the scout footprint with a filled geodesic reachability envelope.
    // For the logged maximum path length, every possible route point obeys
    // d(start, point) + d(point, end) <= maximum_path_length. This is only
    // proactive cache coverage: routes outside the budget remain eligible and
    // use the existing fail-closed on-demand expansion path.
    const double direct_distance_nm = DistGreatCircle_Plugin(
        representative.StartLat, representative.StartLon,
        representative.EndLat, representative.EndLon);
    const weather_routing::ReachabilityPrewarmPlan reachability =
        weather_routing::BuildReachabilityPrewarmPlan(direct_distance_nm);
    PlugInSegmentSafetyResult reachability_result = {};
    reachability_result.struct_size = sizeof(reachability_result);
    bool reachability_ok = true;
    if (prewarm_full_corridor && reachability.enabled) {
      if (m_RoutingProgressDialog && m_RoutingProgressDialog->IsShown())
        UpdateRoutingProgress(_("Building chart safety grid"),
                              _("Prewarming wider chart area"), -1, -1);
      PlugInSegmentSafetyOptions reachability_options = options;
      reachability_options.safety_margin_nm =
          std::max(options.safety_margin_nm, search_options.safety_margin_nm);
      reachability_ok = weather_routing::chart_safety_host::
          PrewarmReachabilityEnvelope(
              representative.StartLat, representative.StartLon,
              representative.EndLat, representative.EndLon,
              reachability.maximum_path_length_nm, &reachability_options,
              &reachability_result);
      wxLogMessage(
          "WR_ROUTE_MASK_REACHABILITY_PREWARM context=%s scope=%s ok=%d "
          "direct_nm=%.3f maximum_path_nm=%.3f cross_track_nm=%.3f "
          "safety_margin_nm=%.3f requested_tiles=%d base_built=%d "
          "base_reused=%d build_ms=%d solver_bound=0",
          context, group->first, reachability_ok ? 1 : 0,
          reachability.direct_distance_nm,
          reachability.maximum_path_length_nm,
          reachability.maximum_cross_track_nm,
          reachability_options.safety_margin_nm,
          reachability_result.prewarm_requested_tiles,
          reachability_result.prewarm_base_tiles_built,
          reachability_result.prewarm_base_tiles_reused,
          reachability_result.grid_build_ms);
    }

    if (m_RoutingProgressDialog && m_RoutingProgressDialog->IsShown())
      UpdateRoutingProgress(
          _("Building chart safety grid"),
          wxString::Format(_("Preparing from %lu route scouts"),
                           static_cast<unsigned long>(group->second.size())),
          -1, -1);
    bool ok = !prewarm_full_corridor ||
              weather_routing::chart_safety_host::
                  PrewarmRouteMaskForPolylinesWithTileHalo(
                  latitudes.data(), longitudes.data(), point_counts.data(),
                  (int)point_counts.size(), footprint_dilation_nm,
                  footprint_fine_tile_halo, &options, &result);
    PlugInSegmentSafetyResult search_result = {};
    search_result.struct_size = sizeof(search_result);
    const bool search_ok =
        !prewarm_full_corridor || !search_mask_differs ||
        weather_routing::chart_safety_host::
            PrewarmRouteMaskForPolylinesWithTileHalo(
            latitudes.data(), longitudes.data(), point_counts.data(),
            (int)point_counts.size(), footprint_dilation_nm,
            footprint_fine_tile_halo, &search_options, &search_result);

    PlugInSegmentSafetyOptions endpoint_options = options;
    endpoint_options.safety_margin_nm = 0.0;
    // Endpoint relaxation removes only the configured land margin.  A
    // minimum-depth constraint remains a hard safety condition at both ends.
    weather_routing::ApplyMinimumDepthPolicy(
        endpoint_options, representative.MinimumDepthMeters);
    PlugInSegmentSafetyResult endpoint_result = {};
    endpoint_result.struct_size = sizeof(endpoint_result);
    bool endpoint_ok =
        endpoint_point_counts.empty() ||
        weather_routing::chart_safety_host::
            PrewarmRouteMaskForPolylinesWithTileHalo(
            endpoint_latitudes.data(), endpoint_longitudes.data(),
            endpoint_point_counts.data(), (int)endpoint_point_counts.size(),
            footprint_dilation_nm, footprint_fine_tile_halo, &endpoint_options,
            &endpoint_result);

    // The union includes every selected chain, retained nearby alternatives,
    // a direct spine and a fine-tile halo. Mark each exact candidate scope as
    // prepared even when all scouts are partial; unresolved expansion remains
    // fail-closed and uses the existing bounded on-demand refinement path.
    if (ok && search_ok && endpoint_ok) {
      for (const ScoutEnvelope& envelope : group->second)
        s_chartSafetySharedPrewarmScopes.insert(envelope.scope);
    }
    wxLogMessage(
        "WR_ROUTE_MASK_SCOUT_ENVELOPE context=%s scope=%s candidates=%lu "
        "complete_scouts=%d partial_scouts=%d retained_segments=%d "
        "footprint_polylines=%lu footprint_dilation_nm=%.3f "
        "footprint_fine_tile_halo=%d "
        "endpoint_segments=%d normal_ok=%d search_ok=%d endpoint_ok=%d "
        "requested_tiles=%d base_built=%d base_reused=%d masks_built=%d "
        "masks_reused=%d fine_tiles_avoided=%d build_ms=%d "
        "endpoint_requested_tiles=%d endpoint_base_built=%d "
        "endpoint_base_reused=%d endpoint_masks_built=%d "
        "endpoint_masks_reused=%d endpoint_fine_tiles_avoided=%d "
        "endpoint_build_ms=%d elapsed_ms=%ld",
        context, group->first, static_cast<unsigned long>(group->second.size()),
        complete_scouts, partial_scouts, retained_segments,
        static_cast<unsigned long>(point_counts.size()), footprint_dilation_nm,
        footprint_fine_tile_halo, endpoint_segments, ok ? 1 : 0,
        search_ok ? 1 : 0, endpoint_ok ? 1 : 0, result.prewarm_requested_tiles,
        result.prewarm_base_tiles_built, result.prewarm_base_tiles_reused,
        result.prewarm_masks_built, result.prewarm_masks_reused,
        result.prewarm_fine_tiles_avoided, result.grid_build_ms,
        endpoint_result.prewarm_requested_tiles,
        endpoint_result.prewarm_base_tiles_built,
        endpoint_result.prewarm_base_tiles_reused,
        endpoint_result.prewarm_masks_built,
        endpoint_result.prewarm_masks_reused,
        endpoint_result.prewarm_fine_tiles_avoided,
        endpoint_result.grid_build_ms, timer.Time());
    if (search_mask_differs)
      wxLogMessage(
          "WR_ROUTE_MASK_SCOUT_SEARCH_MASK context=%s scope=%s ok=%d "
          "search_margin_nm=%.3f requested_tiles=%d base_built=%d "
          "base_reused=%d masks_built=%d masks_reused=%d build_ms=%d",
          context, group->first, search_ok ? 1 : 0,
          search_options.safety_margin_nm,
          search_result.prewarm_requested_tiles,
          search_result.prewarm_base_tiles_built,
          search_result.prewarm_base_tiles_reused,
          search_result.prewarm_masks_built, search_result.prewarm_masks_reused,
          search_result.grid_build_ms);
  }
}

bool WeatherRouting::RetryRouteWithChartSafetyPropagation(
    RouteMapOverlay* routemapoverlay) {
  if (!routemapoverlay) return false;

  RouteMapConfiguration configuration = routemapoverlay->GetConfiguration();
  if (!configuration.DetectLand || configuration.UseChartSafetyForPropagation ||
      configuration.ChartSafetyPropagationFallbackTried) {
    return false;
  }

  bool use_chart_safety = false;
  bool enforce_chart_safety = false;
  ReadExperimentalChartSafetySettings(use_chart_safety, enforce_chart_safety);
  if (!use_chart_safety || !enforce_chart_safety) return false;

  wxString reason = routemapoverlay->GetFailureReason();
  const wxString lower_reason = reason.Lower();
  if (lower_reason.Find("cancel") != wxNOT_FOUND ||
      lower_reason.Find("aborted") != wxNOT_FOUND) {
    return false;
  }
  // This is retained for migrated/in-flight legacy configurations which began
  // before the authoritative-first policy was applied. Re-running an exhausted
  // resource-limited cascade cannot enlarge its search budget.
  if (lower_reason.Find("maximum generated states") != wxNOT_FOUND ||
      lower_reason.Find("maximum retained states") != wxNOT_FOUND ||
      lower_reason.Find("maximum graph labels") != wxNOT_FOUND ||
      lower_reason.Find("resource limit") != wxNOT_FOUND) {
    wxLogMessage(
        "FINAL_ROUTE_SAFETY chart_propagation_retry_skipped route=\"%s -> "
        "%s\" reason=\"%s\" policy=bounded-resource-failure",
        configuration.Start, configuration.End, reason);
    return false;
  }

  configuration.UseChartSafetyForPropagation = true;
  configuration.ChartSafetyPropagationFallbackTried = true;
  configuration.chart_safety_missing_tile_rejections = 0;
  configuration.chart_safety_missing_tile_retry_count = 0;
  configuration.chart_safety_missing_tile_first_lat_tile = 0;
  configuration.chart_safety_missing_tile_first_lon_tile = 0;
  configuration.chart_safety_missing_tile_first_min_lat = NAN;
  configuration.chart_safety_missing_tile_first_min_lon = NAN;
  configuration.chart_safety_missing_tile_min_lat = NAN;
  configuration.chart_safety_missing_tile_max_lat = NAN;
  configuration.chart_safety_missing_tile_min_lon = NAN;
  configuration.chart_safety_missing_tile_max_lon = NAN;
  routemapoverlay->SetConfiguration(configuration);
  routemapoverlay->SetFailureReason(
      _("Retrying with chart-backed land checks during propagation"));

  wxLogMessage(
      "FINAL_ROUTE_SAFETY chart_propagation_retry route=\"%s -> %s\" "
      "start_time=%s safety_margin_land_nm=%.3f reason=\"%s\" "
      "policy=legacy-configuration-authoritative-recovery",
      configuration.Start, configuration.End,
      configuration.StartTime.IsValid()
          ? configuration.StartTime.FormatISOCombined()
          : wxString("invalid"),
      configuration.SafetyMarginLand, reason);

  UpdateRoutingProgress(
      _("Retrying with chart-backed land checks"),
      wxString::Format(_("%s to %s"), configuration.Start, configuration.End),
      -1, -1);
  return true;
}

std::vector<PlotData> WeatherRouting::FullOutputRoute(
    RouteMapOverlay& routemapoverlay) const {
  const std::list<PlotData>& plot = routemapoverlay.GetPlotData(false);
  std::vector<PlotData> points(plot.begin(), plot.end());
  Position* destination = routemapoverlay.GetDestination();
  if (!destination) return points;

  const bool already_has_destination =
      !points.empty() &&
      std::fabs(points.back().lat - destination->lat) < 1e-8 &&
      std::fabs(heading_resolve(points.back().lon - destination->lon)) < 1e-8;
  if (already_has_destination) return points;

  PlotData endpoint = PlotData();
  if (!points.empty()) endpoint = points.back();
  endpoint.lat = destination->lat;
  endpoint.lon = heading_resolve(destination->lon);
  endpoint.time = routemapoverlay.EndTime();
  endpoint.polar = destination->polar;
  endpoint.tacks = destination->tacks;
  endpoint.jibes = destination->jibes;
  endpoint.sail_plan_changes = destination->sail_plan_changes;
  points.push_back(endpoint);
  return points;
}

uint64_t WeatherRouting::RouteGeometryFingerprint(
    const std::vector<PlotData>& points) const {
  uint64_t hash = 1469598103934665603ULL;
  const uint64_t prime = 1099511628211ULL;
  const auto quantize = [](double value, double scale) {
    return std::isfinite(value)
               ? static_cast<int64_t>(std::llround(value * scale))
               : std::numeric_limits<int64_t>::min();
  };
  for (size_t i = 0; i < points.size(); ++i) {
    const int64_t values[] = {
        quantize(points[i].lat, 1e7),
        quantize(heading_resolve(points[i].lon), 1e7),
        points[i].time.IsValid()
            ? static_cast<int64_t>(points[i].time.GetTicks())
            : static_cast<int64_t>(0),
        static_cast<int64_t>(points[i].tacks),
        static_cast<int64_t>(points[i].jibes),
        static_cast<int64_t>(points[i].sail_plan_changes),
        static_cast<int64_t>(points[i].polar),
        quantize(points[i].cog, 1000.0),
        quantize(points[i].twsOverWater, 1000.0),
        quantize(points[i].twdOverWater, 1000.0),
        quantize(points[i].currentSpeed, 1000.0),
        quantize(points[i].currentDir, 1000.0)};
    for (size_t value = 0; value < sizeof(values) / sizeof(values[0]);
         ++value) {
      uint64_t bits = static_cast<uint64_t>(values[value]);
      for (int byte = 0; byte < 8; ++byte) {
        hash ^= bits & 0xff;
        hash *= prime;
        bits >>= 8;
      }
    }
  }
  hash ^= static_cast<uint64_t>(points.size());
  hash *= prime;
  return hash;
}

std::vector<PlotData> WeatherRouting::RouteOutputPoints(
    RouteMapOverlay& routemapoverlay, bool* simplified) {
  if (simplified) *simplified = false;
  std::vector<PlotData> full = FullOutputRoute(routemapoverlay);
  std::map<RouteMapOverlay*, SimplifiedRouteState>::iterator state =
      m_SimplifiedRoutes.find(&routemapoverlay);
  if (state == m_SimplifiedRoutes.end()) return full;

  if (state->second.original_fingerprint != RouteGeometryFingerprint(full)) {
    wxLogMessage(
        "WR_ROUTE_SIMPLIFY invalidated route=\"%s -> %s\" "
        "reason=geometry_changed",
        routemapoverlay.GetConfiguration().Start,
        routemapoverlay.GetConfiguration().End);
    m_SimplifiedRoutes.erase(state);
    return full;
  }
  if (simplified) *simplified = true;
  return state->second.result.points;
}

bool WeatherRouting::ValidateSimplifiedOutputRoute(
    RouteMapOverlay& routemapoverlay, const std::vector<PlotData>& points,
    wxString* failure_reason) {
  if (points.size() < 2) {
    if (failure_reason)
      *failure_reason = _("Simplified route has too few points");
    return false;
  }

  RouteMapConfiguration configuration = routemapoverlay.GetConfiguration();
  if (!configuration.DetectLand) return true;

  bool use_chart_safety = false;
  bool enforce_chart_safety = false;
  ReadExperimentalChartSafetySettings(use_chart_safety, enforce_chart_safety);
  ConstraintChecker::ResetSegmentSafetyDiagnostics(use_chart_safety,
                                                   enforce_chart_safety);
  ConstraintChecker::SetSegmentSafetyDiagnosticContext(wxString::Format(
      _("simplified_route route=\"%s to %s\" points=%lu"), configuration.Start,
      configuration.End, static_cast<unsigned long>(points.size())));

  for (size_t i = 1; i < points.size(); ++i) {
    double course = 0.0;
    double distance = 0.0;
    ll_gc_ll_reverse(points[i - 1].lat, points[i - 1].lon, points[i].lat,
                     points[i].lon, &course, &distance);
    wxString reason;
    if (!ConstraintChecker::CheckFinalRouteLandConstraint(
            configuration, points[i - 1].lat, points[i - 1].lon, points[i].lat,
            points[i].lon, course, &reason)) {
      if (failure_reason) {
        *failure_reason =
            wxString::Format(_("Segment %lu is not chart-safe: %s"),
                             static_cast<unsigned long>(i), reason);
      }
      ConstraintChecker::LogSegmentSafetyDiagnostics(
          _("simplified route validation failed"));
      return false;
    }
  }

  ConstraintChecker::LogSegmentSafetyDiagnostics(
      _("simplified route validation passed"));
  wxLogMessage(
      "FINAL_ROUTE_SAFETY simplified_output_validation route=\"%s -> %s\" "
      "pass=1 points=%lu persistent_cache_used_in_final_validation=0",
      configuration.Start, configuration.End,
      static_cast<unsigned long>(points.size()));
  return true;
}

RouteSimplificationResult WeatherRouting::SimplifyOutputRoute(
    RouteMapOverlay& routemapoverlay,
    const RouteSimplificationOptions& options) {
  wxStopWatch timer;
  const std::vector<PlotData> points = FullOutputRoute(routemapoverlay);
  RouteMapConfiguration configuration = routemapoverlay.GetConfiguration();

  bool use_chart_safety = false;
  bool enforce_chart_safety = false;
  ReadExperimentalChartSafetySettings(use_chart_safety, enforce_chart_safety);
  ConstraintChecker::ResetSegmentSafetyDiagnostics(use_chart_safety,
                                                   enforce_chart_safety);
  ConstraintChecker::SetSegmentSafetyDiagnosticContext(
      wxString::Format(_("route_simplification route=\"%s to %s\""),
                       configuration.Start, configuration.End));

  RouteSimplificationResult result = RouteSimplifier::Simplify(
      points, options,
      [&configuration](const PlotData& first, const PlotData& last,
                       wxString* reason) {
        double course = 0.0;
        double distance = 0.0;
        ll_gc_ll_reverse(first.lat, first.lon, last.lat, last.lon, &course,
                         &distance);
        return ConstraintChecker::CheckFinalRouteLandConstraint(
            configuration, first.lat, first.lon, last.lat, last.lon, course,
            reason);
      },
      [&configuration](const PlotData& first, const PlotData& last,
                       double allowed_penalty_seconds,
                       double* eta_change_seconds, wxString* reason) {
        if (!first.time.IsValid() || !last.time.IsValid() ||
            last.time <= first.time) {
          if (reason) *reason = _("Route timing is unavailable");
          return false;
        }

        RouteMapConfiguration segment_configuration = configuration;
        segment_configuration.time = first.time;
        segment_configuration.StartTime = first.time;
        segment_configuration.StartLat = first.lat;
        segment_configuration.StartLon = first.lon;
        segment_configuration.EndLat = last.lat;
        segment_configuration.EndLon = last.lon;
        double segment_distance = 0.0;
        ll_gc_ll_reverse(first.lat, first.lon, last.lat, last.lon,
                         &segment_configuration.StartEndBearing,
                         &segment_distance);
        RoutePoint start(first.lat, first.lon, first.polar, first.tacks,
                         first.jibes, first.sail_plan_changes, first.data_mask,
                         first.grib_is_data_deficient);
        std::vector<RoutePoint*> intermediate_points;
        int data_mask = 0;
        double distance = 0.0;
        double average_speed = 0.0;
        const double duration = start.RhumbLinePropagateToPoint(
            last.lat, last.lon, segment_configuration, intermediate_points,
            data_mask, distance, average_speed, 5.0);
        for (size_t i = 0; i < intermediate_points.size(); ++i)
          delete intermediate_points[i];

        if (!std::isfinite(duration)) {
          if (reason) *reason = _("Direct leg is not weather-feasible");
          return false;
        }
        const double original_duration =
            (last.time - first.time).GetSeconds().ToDouble();
        const double eta_change = duration - original_duration;
        if (eta_change_seconds) *eta_change_seconds = eta_change;
        if (eta_change > allowed_penalty_seconds + 1.0) {
          if (reason) *reason = _("Direct leg exceeds the ETA allowance");
          return false;
        }
        return true;
      });

  wxString final_reason;
  if (result.success && !ValidateSimplifiedOutputRoute(
                            routemapoverlay, result.points, &final_reason)) {
    result.success = false;
    result.failure_reason = final_reason;
    result.points.clear();
    result.simplified_points = 0;
  }
  ConstraintChecker::LogSegmentSafetyDiagnostics(_("route simplification"));
  wxLogMessage(
      "WR_ROUTE_SIMPLIFY preview route=\"%s -> %s\" pass=%d "
      "original_points=%d simplified_points=%d max_deviation_nm=%.3f "
      "eta_change_seconds=%.1f safety_checks=%d feasibility_checks=%d "
      "elapsed_ms=%ld reason=\"%s\"",
      configuration.Start, configuration.End, result.success ? 1 : 0,
      result.original_points, result.simplified_points, result.max_deviation_nm,
      result.estimated_eta_change_seconds, result.safety_checks,
      result.feasibility_checks, timer.Time(), result.failure_reason);
  return result;
}

RouteSimplificationResult WeatherRouting::SimplifyOutputRoutes(
    const std::vector<RouteMapOverlay*>& routes,
    const RouteSimplificationOptions& options) {
  RouteSimplificationResult combined;
  if (routes.empty()) {
    combined.failure_reason = _("No weather routes selected");
    return combined;
  }

  combined.success = true;
  for (size_t leg = 0; leg < routes.size(); ++leg) {
    RouteSimplificationResult result =
        SimplifyOutputRoute(*routes[leg], options);
    if (!result.success) {
      combined.success = false;
      combined.failure_reason = wxString::Format(
          _("Leg %lu could not be simplified: %s"),
          static_cast<unsigned long>(leg + 1), result.failure_reason);
      combined.points.clear();
      combined.simplified_points = 0;
      return combined;
    }

    combined.original_points += result.original_points;
    combined.simplified_points += result.simplified_points;
    combined.max_deviation_nm =
        std::max(combined.max_deviation_nm, result.max_deviation_nm);
    combined.estimated_eta_change_seconds +=
        result.estimated_eta_change_seconds;
    combined.safety_checks += result.safety_checks;
    combined.feasibility_checks += result.feasibility_checks;

    size_t first_point = 0;
    if (!combined.points.empty() && !result.points.empty()) {
      const PlotData& previous = combined.points.back();
      const PlotData& next = result.points.front();
      if (std::fabs(previous.lat - next.lat) < 1e-8 &&
          std::fabs(heading_resolve(previous.lon - next.lon)) < 1e-8) {
        first_point = 1;
        --combined.original_points;
        --combined.simplified_points;
      }
    }
    combined.points.insert(combined.points.end(),
                           result.points.begin() + first_point,
                           result.points.end());
  }
  combined.simplified_points = static_cast<int>(combined.points.size());
  return combined;
}

std::vector<WeatherRouting::RouteOutputGroup>
WeatherRouting::SelectedRouteOutputGroups() {
  std::vector<RouteOutputGroup> groups;
  const std::list<RouteMapOverlay*> selected = CurrentRouteMaps(true);
  std::set<wxString> included_multi_leg_groups;

  for (std::list<RouteMapOverlay*>::const_iterator it = selected.begin();
       it != selected.end(); ++it) {
    RouteMapOverlay* route = *it;
    if (!route) continue;
    const RouteMapConfiguration configuration = route->GetConfiguration();

    RouteOutputGroup output;
    if (configuration.IsMultiLegGenerated) {
      output.multi_leg = true;
      if (configuration.MultiLegGroupId.IsEmpty()) {
        output.routes.push_back(route);
        groups.push_back(output);
        continue;
      }
      if (!included_multi_leg_groups.insert(configuration.MultiLegGroupId)
               .second)
        continue;
      output.routes = GetMultiLegGroupRoutes(configuration.MultiLegGroupId);
    } else {
      output.routes.push_back(route);
    }
    if (!output.routes.empty()) groups.push_back(output);
  }
  return groups;
}

bool WeatherRouting::PrepareCombinedRouteOutput(
    const std::vector<RouteMapOverlay*>& routes, std::vector<PlotData>* points,
    bool* simplified, wxString* failure_reason) {
  if (points) points->clear();
  if (simplified) *simplified = false;
  if (routes.empty()) {
    if (failure_reason)
      *failure_reason = _("No multi-waypoint weather route is available.");
    return false;
  }

  const RouteMapConfiguration first_configuration =
      routes.front()->GetConfiguration();
  if (!first_configuration.IsMultiLegGenerated ||
      first_configuration.MultiLegGroupId.IsEmpty()) {
    if (failure_reason)
      *failure_reason = _("The selected route is not a multi-waypoint route.");
    return false;
  }

  for (size_t i = 0; i < routes.size(); ++i) {
    if (!routes[i]) {
      if (failure_reason)
        *failure_reason = _("The multi-waypoint route contains an unavailable "
                            "leg.");
      return false;
    }
    const RouteMapConfiguration configuration = routes[i]->GetConfiguration();
    if (!configuration.IsMultiLegGenerated ||
        configuration.MultiLegGroupId !=
            first_configuration.MultiLegGroupId) {
      if (failure_reason)
        *failure_reason = _("The selected route legs do not belong to one "
                            "multi-waypoint passage.");
      return false;
    }
    if (!routes[i]->Finished() || !routes[i]->ReachedDestination()) {
      if (failure_reason)
        *failure_reason = wxString::Format(
            _("Leg %lu of the multi-waypoint route has not reached its "
              "destination and cannot be exported."),
            static_cast<unsigned long>(i + 1));
      return false;
    }
    if (!ValidateRouteForOutput(*routes[i], _("Prepare route output"))) {
      if (failure_reason) failure_reason->Clear();
      return false;
    }
  }

  std::vector<MultiLegRouteOutputLeg> legs;
  legs.reserve(routes.size());
  for (size_t i = 0; i < routes.size(); ++i) {
    const RouteMapConfiguration configuration = routes[i]->GetConfiguration();
    MultiLegRouteOutputLeg leg;
    leg.index = configuration.MultiLegLegIndex;
    leg.count = configuration.MultiLegLegCount;
    leg.points = FullOutputRoute(*routes[i]);
    legs.push_back(leg);
  }

  MultiLegRouteOutputResult assembled = MultiLegRouteOutput::Assemble(legs);
  if (!assembled.success) {
    if (failure_reason) *failure_reason = assembled.failure_reason;
    return false;
  }

  // Simplification is optional. Recheck a stored version against the current
  // geometry and chart evidence only after the complete original passage has
  // passed leg-count and continuity validation. If simplification is stale,
  // fall back to the assembled route above.
  if (SelectedSimplifiedGroup(routes)) {
    RouteSimplificationResult current =
        SimplifyOutputRoutes(routes, m_SimplifiedRouteGroup.options);
    if (current.success) {
      if (points) *points = current.points;
      if (simplified) *simplified = true;
      return true;
    }
    wxLogMessage(
        "WR_ROUTE_OUTPUT group_simplification_fallback group=%s reason=\"%s\"",
        first_configuration.MultiLegGroupId, current.failure_reason);
    m_SimplifiedRouteGroup = SimplifiedRouteGroupState();
  }

  if (points) points->swap(assembled.points);
  return true;
}

bool WeatherRouting::SelectedSimplifiedGroup(
    const std::vector<RouteMapOverlay*>& routes,
    std::vector<PlotData>* points) const {
  if (!m_SimplifiedRouteGroup.valid ||
      routes.size() != m_SimplifiedRouteGroup.routes.size())
    return false;
  for (size_t i = 0; i < routes.size(); ++i) {
    if (routes[i] != m_SimplifiedRouteGroup.routes[i] ||
        RouteGeometryFingerprint(FullOutputRoute(*routes[i])) !=
            m_SimplifiedRouteGroup.fingerprints[i])
      return false;
  }
  if (points) *points = m_SimplifiedRouteGroup.result.points;
  return true;
}

void WeatherRouting::SaveCombinedRoute(
    const std::vector<RouteMapOverlay*>& routes,
    const std::vector<PlotData>& points, bool simplified) {
  if (routes.empty() || points.empty()) return;
  for (size_t i = 0; i < routes.size(); ++i)
    if (!ValidateRouteForOutput(*routes[i], _("Save as route"))) return;

  const RouteMapConfiguration first = routes.front()->GetConfiguration();
  const RouteMapConfiguration last = routes.back()->GetConfiguration();
  PlugIn_Route_Ex* route = new PlugIn_Route_Ex();
  route->m_NameString = _("Weather Route") + " (" +
                        m_SettingsDialog.FormatTime(
                            routes.front()->StartTime(), _T("%x %H:%M")) +
                        ")";
  route->m_StartString = first.Start;
  route->m_EndString = last.End;
  route->m_isVisible = true;
  route->m_GUID = GetNewGUID();
  for (size_t i = 0; i < points.size(); ++i) {
    PlugIn_Waypoint_Ex* point = new PlugIn_Waypoint_Ex(
        points[i].lat, heading_resolve(points[i].lon), _T("circle"),
        i + 1 == points.size() ? _("Weather Route Destination")
                               : _("Weather Route Point"));
    // OpenCPN route/track insertion currently expects this extra conversion.
    point->m_CreateTime = points[i].time.ToUTC();
    route->pWaypointList->Append(point);
  }
  AddPlugInRouteEx(route);
  route->pWaypointList->DeleteContents(true);
  route->pWaypointList->Clear();
  delete route;
  GetParent()->Refresh();

  wxLogMessage(
      "WR_ROUTE_OUTPUT save_combined_route route=\"%s -> %s\" legs=%lu "
      "simplified=%d points=%lu",
      first.Start, last.End, static_cast<unsigned long>(routes.size()),
      simplified ? 1 : 0,
      static_cast<unsigned long>(points.size()));
  wxMessageDialog dialog(
      this,
      simplified
          ? _("The simplified multi-waypoint routing has been saved as one "
              "route in the 'Route and Mark' Manager\n")
          : _("The complete multi-waypoint routing has been saved as one "
              "route in the 'Route and Mark' Manager\n"),
      _("Weather Routing"), wxOK);
  dialog.ShowModal();
}

void WeatherRouting::ExportCombinedRoute(
    const std::vector<RouteMapOverlay*>& routes,
    const std::vector<PlotData>& points, bool simplified) {
  if (routes.empty() || points.empty()) return;
  for (size_t i = 0; i < routes.size(); ++i)
    if (!ValidateRouteForOutput(*routes[i], _("Export as GPX"))) return;

  const RouteMapConfiguration first = routes.front()->GetConfiguration();
  const RouteMapConfiguration last = routes.back()->GetConfiguration();
  SimpleRoute route;
  route.m_GUID = GetNewGUID();
  route.m_RouteNameString =
      "WXRoute_" +
      m_SettingsDialog.FormatTime(routes.front()->StartTime(),
                                  _T("%m-%d-%y_%H-%M"), false) +
      "_" + first.Start + "_" + last.End;
  route.m_RouteStartString = first.Start;
  route.m_RouteEndString = last.End;
  route.m_PlannedDeparture = routes.front()->StartTime();

  const wxString suffix = route.m_GUID.AfterLast('-').Truncate(4);
  for (size_t i = 0; i < points.size(); ++i) {
    wxString name;
    if (i + 1 == points.size())
      name = _("Weather Route Destination");
    else
      name =
          wxString::Format("RP-%s-%lu", suffix, static_cast<unsigned long>(i));
    SimpleRoutePoint* point =
        new SimpleRoutePoint(points[i].lat, heading_resolve(points[i].lon),
                             _T("circle"), name, GetNewGUID());
    point->m_CreateTime = points[i].time;
    if (i > 0) {
      point->etd = points[i - 1].time;
      const double seconds =
          (points[i].time - points[i - 1].time).GetSeconds().ToDouble();
      if (seconds > 0.0) {
        const double distance = DistGreatCircle_Plugin(
            points[i].lat, points[i].lon, points[i - 1].lat, points[i - 1].lon);
        point->m_seg_vmg = distance * 3600.0 / seconds;
      }
    }
    route.AddPoint(point);
  }

  SimpleNavObjectXML navobj;
  navobj.CreateNavObjGPXRoute(route);
  wxString directory = weather_routing_pi::StandardPath() +
                       _T("PlannedRoutes") + wxFileName::GetPathSeparator();
  if (!wxDir::Exists(directory)) wxDir::Make(directory);
  wxString base = directory + route.m_RouteNameString;
  wxString path = base + ".gpx";
  for (int suffix_number = 1; wxFileName::Exists(path) && suffix_number < 100;
       ++suffix_number)
    path = wxString::Format("%s(%d).gpx", base, suffix_number);
  const bool saved = navobj.save_file(path.ToStdString().c_str());

  wxLogMessage(
      "WR_ROUTE_OUTPUT export_combined_gpx route=\"%s -> %s\" legs=%lu "
      "simplified=%d points=%lu saved=%d path=\"%s\"",
      first.Start, last.End, static_cast<unsigned long>(routes.size()),
      simplified ? 1 : 0, static_cast<unsigned long>(points.size()),
      saved ? 1 : 0, path);
  if (saved) {
    wxMessageBox(
        (simplified ? _("Simplified multi-waypoint route exported to:\n")
                    : _("Complete multi-waypoint route exported to:\n")) +
            path,
        _("Weather Routing"), wxOK, this);
  } else {
    wxMessageBox(_("GPX route file export failed.\n\n") + path,
                 _("Weather Routing"), wxOK | wxICON_ERROR, this);
  }
}

void WeatherRouting::SaveAsTrack(RouteMapOverlay& routemapoverlay) {
  if (!ValidateRouteForOutput(routemapoverlay, _("Save as track"))) return;

  std::list<PlotData> plotdata = routemapoverlay.GetPlotData(false);

  if (plotdata.empty()) {
    wxMessageDialog mdlg(this, _("Empty routing, nothing to save\n"),
                         _("Weather Routing"), wxOK | wxICON_WARNING);
    mdlg.ShowModal();
    return;
  }

  PlugIn_Track* newPath = new PlugIn_Track;
  newPath->m_NameString = _("Weather Route ") + " (" +
                          m_SettingsDialog.FormatTime(
                              routemapoverlay.StartTime(), _T("%x %H:%M")) +
                          ")";

  // XXX double check time is really end time, not start time off by one.
  RouteMapConfiguration c = routemapoverlay.GetConfiguration();
  newPath->m_StartString = c.Start;
  newPath->m_EndString = c.End;
  newPath->m_GUID = GetNewGUID();

  for (auto const& it : plotdata) {
    PlugIn_Waypoint* newPoint =
        new PlugIn_Waypoint(it.lat, heading_resolve(it.lon), _T("circle"),
                            _("Weather Route Point"));

    newPoint->m_CreateTime = it.time.ToUTC();
    newPath->pWaypointList->Append(newPoint);
  }

  // last point, missing if config didn't succeed
  Position* p = routemapoverlay.GetDestination();
  if (p) {
    PlugIn_Waypoint* newPoint = new PlugIn_Waypoint(
        p->lat, p->lon, _T("circle"), _("Weather Route Destination"));
    newPoint->m_CreateTime = routemapoverlay.EndTime().ToUTC();
    newPath->pWaypointList->Append(newPoint);
  }

  AddPlugInTrack(newPath);
  // not done PlugIn_Track DTOR
  newPath->pWaypointList->DeleteContents(true);
  newPath->pWaypointList->Clear();

  delete newPath;

  GetParent()->Refresh();

  wxMessageDialog mdlg(this,
                       _("Routing has been saved as a track in the 'Route and "
                         "Mark' Manager\n"),
                       _("Weather Routing"), wxOK);
  mdlg.ShowModal();
}

void WeatherRouting::SaveAsRoute(RouteMapOverlay& routemapoverlay) {
  if (!ValidateRouteForOutput(routemapoverlay, _("Save as route"))) return;

  bool simplified = false;
  std::vector<PlotData> plotdata =
      RouteOutputPoints(routemapoverlay, &simplified);

  if (plotdata.empty()) {
    wxMessageDialog mdlg(this, _("Empty routing, nothing to save\n"),
                         _("Weather Routing"), wxOK | wxICON_WARNING);
    mdlg.ShowModal();
    return;
  }
  wxString simplified_failure;
  if (simplified && !ValidateSimplifiedOutputRoute(routemapoverlay, plotdata,
                                                   &simplified_failure)) {
    m_SimplifiedRoutes.erase(&routemapoverlay);
    wxMessageDialog dialog(
        this,
        wxString::Format(_("The simplified route is no longer chart-safe and "
                           "was not saved.\n\n%s"),
                         simplified_failure),
        _("Weather Routing"), wxOK | wxICON_WARNING);
    dialog.ShowModal();
    return;
  }

  PlugIn_Route_Ex* newRoute = new PlugIn_Route_Ex();
  newRoute->m_NameString = _("Weather Route ") + " (" +
                           m_SettingsDialog.FormatTime(
                               routemapoverlay.StartTime(), _T("%x %H:%M")) +
                           ")";

  RouteMapConfiguration c = routemapoverlay.GetConfiguration();
  newRoute->m_StartString = c.Start;
  newRoute->m_EndString = c.End;
  newRoute->m_isVisible = true;
  newRoute->m_GUID = GetNewGUID();

  for (std::vector<PlotData>::const_iterator it = plotdata.begin();
       it != plotdata.end(); ++it) {
    const bool destination_point = it + 1 == plotdata.end();
    PlugIn_Waypoint_Ex* newPoint = new PlugIn_Waypoint_Ex(
        it->lat, heading_resolve(it->lon), _T("circle"),
        destination_point ? _("Weather Route Destination")
                          : _("Weather Route Point"));
    // newPoint->m_PlannedSpeed = it.sog;
    newPoint->m_CreateTime = it->time.ToUTC();
    newRoute->pWaypointList->Append(newPoint);
  }

  AddPlugInRouteEx(newRoute);
  // Clean up waypoint list (ownership transferred to OpenCPN)
  newRoute->pWaypointList->DeleteContents(true);
  newRoute->pWaypointList->Clear();

  delete newRoute;

  GetParent()->Refresh();

  wxLogMessage(
      "WR_ROUTE_OUTPUT save_route route=\"%s -> %s\" simplified=%d points=%lu",
      c.Start, c.End, simplified ? 1 : 0,
      static_cast<unsigned long>(plotdata.size()));

  wxMessageDialog mdlg(
      this,
      simplified ? _("Simplified routing has been saved as a route in "
                     "the 'Route and Mark' Manager\n")
                 : _("Routing has been saved as a route in the 'Route "
                     "and Mark' Manager\n"),
      _("Weather Routing"), wxOK);
  mdlg.ShowModal();
}

void WeatherRouting::ExportRoute(RouteMapOverlay& routemapoverlay) {
  if (!ValidateRouteForOutput(routemapoverlay, _("Export as GPX"))) return;

  bool simplified = false;
  std::vector<PlotData> plotdata =
      RouteOutputPoints(routemapoverlay, &simplified);

  if (plotdata.empty()) {
    wxMessageDialog mdlg(this, _("Empty Routing, nothing to export\n"),
                         _("Weather Routing"), wxOK | wxICON_WARNING);
    mdlg.ShowModal();
    return;
  }
  wxString simplified_failure;
  if (simplified && !ValidateSimplifiedOutputRoute(routemapoverlay, plotdata,
                                                   &simplified_failure)) {
    m_SimplifiedRoutes.erase(&routemapoverlay);
    wxMessageDialog dialog(
        this,
        wxString::Format(_("The simplified route is no longer chart-safe and "
                           "was not exported.\n\n%s"),
                         simplified_failure),
        _("Weather Routing"), wxOK | wxICON_WARNING);
    dialog.ShowModal();
    return;
  }

  RouteMapConfiguration c = routemapoverlay.GetConfiguration();

  SimpleRoute new_route;
  new_route.m_GUID = GetNewGUID();

  new_route.m_RouteNameString =
      "WXRoute_" + m_SettingsDialog.FormatTime(
                       routemapoverlay.StartTime(), _T("%m-%d-%y_%H-%M"),
                       false);
  new_route.m_RouteNameString += "_" + c.Start + "_" + c.End;

  new_route.m_RouteStartString = c.Start;
  new_route.m_RouteEndString = c.End;
  new_route.m_PlannedDeparture = routemapoverlay.StartTime();

  std::vector<double> lat;
  std::vector<double> lon;
  std::vector<wxDateTime> time;
  std::vector<double> vmga;
  for (std::vector<PlotData>::const_iterator it0 = plotdata.begin();
       it0 != plotdata.end(); ++it0) {
    lat.push_back(it0->lat);
    lon.push_back(heading_resolve(it0->lon));
    time.push_back(it0->time);
    vmga.push_back(-1.);
  }

  unsigned int ip = 0;
  for (std::vector<PlotData>::const_iterator it1 = plotdata.begin();
       it1 != plotdata.end(); ++it1) {
    // calculate leg parameters, mainly VMG
    double vmg = -1.;
    if (ip < time.size() - 1) {
      wxTimeSpan delta_time = time[ip + 1] - time[ip];
      double secs = delta_time.GetSeconds().ToDouble();
      double distance =
          DistGreatCircle_Plugin(lat[ip + 1], lon[ip + 1], lat[ip], lon[ip]);
      if (secs > 0.0) {
        vmg = (distance / secs) * 3600;
        vmga[ip + 1] = vmg;  // VMG belongs to the point ending this leg.
      }
    }
    ip++;
  }

  unsigned int ip1 = 0;
  // Use some part of new route GUID to uniquely name route points
  wxString route_name_suffix = new_route.m_GUID.AfterLast('-').Truncate(4);

  for (std::vector<PlotData>::const_iterator it = plotdata.begin();
       it != plotdata.end(); ++it) {
    const bool destination_point = it + 1 == plotdata.end();
    wxString wp_name;
    if (destination_point) {
      wp_name = _("Weather Route Destination");
    } else {
      wp_name = "RP-";
      wp_name += route_name_suffix;
      wxString np;
      np.Printf("-%d", ip1);
      wp_name += np;
    }

    SimpleRoutePoint* newPoint = new SimpleRoutePoint(
        it->lat, heading_resolve(it->lon), _T("circle"), wp_name, GetNewGUID());

    if (vmga[ip1] >= 0.) newPoint->m_seg_vmg = vmga[ip1];

    newPoint->m_CreateTime = it->time;
    if (ip1 > 0) newPoint->etd = time[ip1 - 1];

    new_route.AddPoint(newPoint);
    ip1++;
  }

  wxLogMessage(
      "WR_ROUTE_OUTPUT export_gpx route=\"%s -> %s\" simplified=%d points=%lu",
      c.Start, c.End, simplified ? 1 : 0,
      static_cast<unsigned long>(plotdata.size()));

  SimpleNavObjectXML* navobj = new SimpleNavObjectXML;
  navobj->CreateNavObjGPXRoute(new_route);

  wxString export_path_base = weather_routing_pi::StandardPath() +
                              _T("PlannedRoutes") +
                              wxFileName::GetPathSeparator();
  if (!wxDir::Exists(export_path_base)) wxDir::Make(export_path_base);

  // Handle duplicate file names by adding "(n)" as needed
  export_path_base += new_route.m_RouteNameString;
  wxString export_path = export_path_base + ".gpx";
  if (wxFileName::Exists(export_path)) {
    int iv = 1;
    bool bok = false;
    wxString tname, vadd;
    while (!bok && iv < 10) {
      vadd.Printf("(%d)", iv);
      wxString tname = export_path_base + vadd + ".gpx";
      if (wxFileName::Exists(tname)) {
        iv++;
      } else
        bok = true;
    }
    if (bok) export_path_base += vadd;
  }

  export_path = export_path_base + ".gpx";

  bool bsave_ok = navobj->save_file(export_path.ToStdString().c_str());
  if (bsave_ok) {
    wxMessageBox(_("GPX Route file created") + ".\n\n" + export_path + "\n",
                 _("OpenCPN Weather Routing Plugin"), wxOK);
  } else {
    wxMessageBox(
        _("GPX Route file export failed") + ".\n\n" + export_path + "\n",
        _("OpenCPN Weather Routing Plugin"), wxICON_ERROR | wxOK);
  }

  delete navobj;

  GetParent()->Refresh();
}

void WeatherRouting::Start(RouteMapOverlay* routemapoverlay) {
  if (!routemapoverlay) return;
  ScopedRoutePreparation route_preparation(m_RoutePreparationDepth);

  RouteMapConfiguration configuration = routemapoverlay->GetConfiguration();
  const bool routeByArrival =
      configuration.TimeMode ==
      RouteMapConfiguration::ROUTE_BY_ARRIVAL_TIME;
  if (routeByArrival && configuration.DepartureTimeOptimizationCandidate) {
    wxLogMessage(
        "WR_DEPARTURE_PROMOTE source=route-start transition=arrival "
        "old_group=\"%s\" old_offset_minutes=%d start=\"%s\" end=\"%s\".",
        configuration.DepartureTimeOptimizationGroupId,
        configuration.DepartureTimeOptimizationOffsetMinutes,
        configuration.Start, configuration.End);
    configuration.PromoteDepartureTimeOptimizationCandidate();
    routemapoverlay->SetConfiguration(configuration);
  }
  bool boatHasMoved = false;
  if (routemapoverlay->Finished() &&
      configuration.StartType == RouteMapConfiguration::START_FROM_BOAT) {
    // Check if the boat has moved significantly since the last calculation.
    double distance = DistGreatCircle_Plugin(
        configuration.StartLat, configuration.StartLon,
        m_weather_routing_pi.m_boat_lat, m_weather_routing_pi.m_boat_lon);
    // Threshold for significant movement is somewhat arbitrarily set to 20
    // meters.
    boatHasMoved = (distance * 1852.0) > 20.0;
  }

  // Skip recalculation if:
  // 1. The route has completed, or
  // 2. Route is from boat and boat has moved, or
  // 3. Configuration specifies to use current start time.
  if (routemapoverlay->Finished() &&
      routemapoverlay->GetWeatherForecastStatus() == WEATHER_FORECAST_SUCCESS &&
      !boatHasMoved && (!configuration.UseCurrentTime || routeByArrival)) {
    return;
  }

  if (m_StabilityCorridorLifecycle.Contains(routemapoverlay) ||
      std::find(m_StabilityCorridorSourceRoutes.begin(),
                m_StabilityCorridorSourceRoutes.end(),
                routemapoverlay) != m_StabilityCorridorSourceRoutes.end()) {
    HideStabilityCorridor("weather_route_recomputed");
    m_StabilityCorridorSourceRoutes.clear();
    m_StabilityCorridorRoutes.clear();
    m_StabilityCorridorResult =
        weather_routing_engine::StabilityCorridorResult();
  }

  bool configUpdated = false;
  // If starting from boat, update the boat position
  if (configuration.StartType == RouteMapConfiguration::START_FROM_BOAT) {
    // Use the current boat position from the plugin
    configuration.StartLat = m_weather_routing_pi.m_boat_lat;
    configuration.StartLon = m_weather_routing_pi.m_boat_lon;
    // Set "Boat" as the starting point name for display purposes
    configuration.Start = _("Boat");
    // Clear any StartGUID since we're using the boat position, not a waypoint
    configuration.StartGUID = wxEmptyString;
    configUpdated = true;
  }
  if (!routeByArrival && configuration.UseCurrentTime) {
    // Use the current time
    configuration.StartTime = wxDateTime::Now();
    configUpdated = true;
  }
  if (configUpdated) {
    // Call Update() to recalculate the bearing and other parameters
    configuration.Update();
    routemapoverlay->SetConfiguration(configuration);
  }
  // Ensure we have valid start coordinates
  if (std::isnan(configuration.StartLat) ||
      std::isnan(configuration.StartLon)) {
    routemapoverlay->SetError(_("Invalid start position"));
    return;
  }

  if (configuration.DeltaTime <= 0) {
    routemapoverlay->SetError(_("Zero Time Step"));
    return;
  }
  if (configuration.DetectBoundary) {
    if (m_weather_routing_pi.InBoundary(configuration.EndLat,
                                        configuration.EndLon) ||
        m_weather_routing_pi.InBoundary(configuration.StartLat,
                                        configuration.StartLon)) {
      routemapoverlay->SetError(_("inside exclusion boundary"));
      return;
    }
  }

  if (fabs(configuration.StartLat) > configuration.MaxLatitude ||
      fabs(configuration.EndLat) > configuration.MaxLatitude) {
    routemapoverlay->SetError(_("lies outside of Max Latitude constraint"));
    return;
  }

  bool use_chart_safety = false;
  bool enforce_chart_safety = false;
  ReadExperimentalChartSafetySettings(use_chart_safety, enforce_chart_safety);
  configuration.chart_safety_runtime_available = use_chart_safety;
  configuration.chart_safety_runtime_enforced = enforce_chart_safety;
  configuration.UseChartSafetyForPropagation =
      weather_routing::ShouldUseAuthoritativeChartSearch(
          configuration.DetectLand, use_chart_safety, enforce_chart_safety,
          configuration.chart_safety_scout_preview);
  routemapoverlay->SetConfiguration(configuration);
  if (configuration.MinimumDepthMeters > 0.0 &&
      (!configuration.DetectLand || !use_chart_safety ||
       !enforce_chart_safety)) {
    routemapoverlay->SetError(
        _("Minimum depth requires enabled and enforced chart-aware safety"));
    wxLogError(
        "WR_MINIMUM_DEPTH unavailable route=\"%s -> %s\" "
        "minimum_depth_m=%.3f detect_land=%d chart_safety_use=%d "
        "chart_safety_enforce=%d",
        configuration.Start, configuration.End,
        configuration.MinimumDepthMeters, configuration.DetectLand ? 1 : 0,
        use_chart_safety ? 1 : 0, enforce_chart_safety ? 1 : 0);
    return;
  }
  const double route_distance_nm =
      DistGreatCircle_Plugin(configuration.StartLat, configuration.StartLon,
                             configuration.EndLat, configuration.EndLon);
  if (configuration.DetectLand && use_chart_safety && enforce_chart_safety) {
    wxLogMessage(
        "WR_CHART_PROPAGATION_POLICY route=\"%s -> %s\" distance_nm=%.3f "
        "stage=%s",
        configuration.Start, configuration.End, route_distance_nm,
        configuration.UseChartSafetyForPropagation
            ? "authoritative-chart-search"
            : "legacy-or-scout-gshhs-search");
  }
  if (configuration.DetectLand && use_chart_safety && enforce_chart_safety &&
      !configuration.UseReverseReachabilityRecovery) {
    configuration.UseReverseReachabilityRecovery = true;
    wxLogMessage(
        "WR_REVERSE_REACHABILITY auto-enabled route=\"%s -> %s\" group=\"%s\" "
        "candidate_offset=%d leg=%d/%d chart_propagation=%d",
        configuration.Start, configuration.End, configuration.MultiLegGroupId,
        configuration.DepartureTimeOptimizationOffsetMinutes,
        configuration.MultiLegLegIndex, configuration.MultiLegLegCount,
        configuration.UseChartSafetyForPropagation ? 1 : 0);
    routemapoverlay->SetConfiguration(configuration);
  }
  const bool prewarm_authoritative_chart_search =
      weather_routing::ShouldPrewarmAuthoritativeChartSearch(
          configuration.DetectLand, use_chart_safety, enforce_chart_safety,
          configuration.UseChartSafetyForPropagation,
          configuration.chart_safety_missing_tile_retry_count);
  wxString routeStartLog = wxString::Format(
      "WR_ROUTE_START route=\"%s -> %s\" group=\"%s\" candidate=%d "
      "candidate_offset=%d time_mode=%s start_time=\"%s\" "
      "planned_arrival=\"%s\" use_current_time=%d ",
      configuration.Start, configuration.End,
      configuration.DepartureTimeOptimizationGroupId,
      configuration.DepartureTimeOptimizationCandidate ? 1 : 0,
      configuration.DepartureTimeOptimizationOffsetMinutes,
      configuration.TimeMode ==
              RouteMapConfiguration::ROUTE_BY_ARRIVAL_TIME
          ? "arrival"
          : "departure",
      configuration.StartTime.IsValid()
          ? configuration.StartTime.FormatISOCombined()
          : wxString("invalid"),
      configuration.PlannedArrivalTime.IsValid()
          ? configuration.PlannedArrivalTime.FormatISOCombined()
          : wxString("invalid"),
      !routeByArrival && configuration.UseCurrentTime ? 1 : 0);
  routeStartLog += wxString::Format(
      "start{type=%s name=\"%s\" guid=\"%s\" lat=%.6f lon=%.6f} "
      "end{type=%s name=\"%s\" guid=\"%s\" lat=%.6f lon=%.6f} ",
      RouteStartTypeName(configuration.StartType), configuration.Start,
      configuration.StartGUID, configuration.StartLat, configuration.StartLon,
      RouteEndTypeName(configuration.EndType), configuration.End,
      configuration.EndGUID, configuration.EndLat, configuration.EndLon);
  routeStartLog += wxString::Format(
      "boat=\"%s\" polars=%lu use_grib=%d climatology=%d currents=%d "
      "detect_land=%d detect_boundary=%d chart_safety_use=%d "
      "chart_safety_enforce=%d chart_safety_propagation=%d "
      "reverse_reachability=%d scout_eligible=%d "
      "safety_margin_land_nm=%.3f minimum_depth_m=%.3f timestep=%.0f ",
      configuration.boatFileName,
      static_cast<unsigned long>(configuration.boat.Polars.size()),
      configuration.UseGrib ? 1 : 0, configuration.ClimatologyType,
      configuration.Currents ? 1 : 0, configuration.DetectLand ? 1 : 0,
      configuration.DetectBoundary ? 1 : 0, use_chart_safety ? 1 : 0,
      enforce_chart_safety ? 1 : 0,
      configuration.UseChartSafetyForPropagation ? 1 : 0,
      configuration.UseReverseReachabilityRecovery ? 1 : 0,
      prewarm_authoritative_chart_search ? 1 : 0,
      configuration.SafetyMarginLand, configuration.MinimumDepthMeters,
      configuration.DeltaTime);
  routeStartLog += wxString::Format(
      "degree{from=%.1f to=%.1f by=%.1f count=%lu} "
      "course{max_diverted=%.1f max_course=%.1f max_search=%.1f} "
      "constraints{max_true_wind=%.1f max_apparent_wind=%.1f "
      "max_swell=%.1f max_latitude=%.1f wind_vs_current=%.1f} "
      "routing_effort=%d%% workers=%d",
      configuration.FromDegree, configuration.ToDegree, configuration.ByDegrees,
      static_cast<unsigned long>(configuration.DegreeSteps.size()),
      configuration.MaxDivertedCourse, configuration.MaxCourseAngle,
      configuration.MaxSearchAngle, configuration.MaxTrueWindKnots,
      configuration.MaxApparentWindKnots, configuration.MaxSwellMeters,
      configuration.MaxLatitude, configuration.WindVSCurrent,
      weather_routing::NormalizeRoutingEffortPercent(
          configuration.RoutingEffortPercent),
      m_SettingsDialog.m_sConcurrentThreads->GetValue());
  wxLogMessage("%s", routeStartLog);

  if (prewarm_authoritative_chart_search &&
      s_chartSafetySharedPrewarmScopes.find(ChartSafetySharedPrewarmScopeKey(
          configuration)) == s_chartSafetySharedPrewarmScopes.end() &&
      s_chartSafetyPreparedScoutScopes.find(ChartSafetySharedPrewarmScopeKey(
          configuration)) == s_chartSafetyPreparedScoutScopes.end()) {
    const bool use_chart_safety_for_propagation =
        configuration.UseChartSafetyForPropagation;
    const bool use_reverse_reachability_recovery =
        configuration.UseReverseReachabilityRecovery;
    PrepareChartSafetyScoutEnvelopes(
        std::vector<RouteMapOverlay*>(1, routemapoverlay),
        _("route start scout"));
    configuration = routemapoverlay->GetConfiguration();
    // PrepareChartSafetyScoutEnvelopes restores and resets the route overlay.
    // Re-apply the routing policy established above after the bounded scout
    // has restored the route configuration.
    configuration.UseChartSafetyForPropagation =
        use_chart_safety_for_propagation;
    configuration.UseReverseReachabilityRecovery =
        use_reverse_reachability_recovery;
    routemapoverlay->SetConfiguration(configuration);
  }

  if (weather_routing::chart_safety_host::PrewarmCancellationRequested()) {
    routemapoverlay->SetError(_("routing cancelled"));
    return;
  }

  configuration.chart_safety_missing_tile_rejections = 0;
  configuration.chart_safety_missing_tile_first_lat_tile = 0;
  configuration.chart_safety_missing_tile_first_lon_tile = 0;
  configuration.chart_safety_missing_tile_first_min_lat = NAN;
  configuration.chart_safety_missing_tile_first_min_lon = NAN;
  configuration.chart_safety_missing_tile_min_lat = NAN;
  configuration.chart_safety_missing_tile_max_lat = NAN;
  configuration.chart_safety_missing_tile_min_lon = NAN;
  configuration.chart_safety_missing_tile_max_lon = NAN;
  routemapoverlay->SetConfiguration(configuration);

  /* initialize crossing land routine from main thread as it is
     not re-entrant, and cannot be done by worker-threads later */
  if (configuration.DetectLand) {
    bool use_experimental_chart_safety = false;
    bool enforce_experimental_chart_safety = false;
    ReadExperimentalChartSafetySettings(use_experimental_chart_safety,
                                        enforce_experimental_chart_safety);
    ConstraintChecker::ResetSegmentSafetyDiagnostics(
        use_experimental_chart_safety, enforce_experimental_chart_safety);
    ConstraintChecker::SetSegmentSafetyDiagnosticContext(wxString::Format(
        _("route=\"%s to %s\" group=%s candidate_offset=%d leg=%d/%d"),
        configuration.Start, configuration.End, configuration.MultiLegGroupId,
        configuration.DepartureTimeOptimizationOffsetMinutes,
        configuration.MultiLegLegIndex, configuration.MultiLegLegCount));
    PlugIn_GSHHS_CrossesLand(0, 0, 0, 0);
    if (prewarm_authoritative_chart_search) {
      PrewarmExperimentalChartSafetyForConfiguration(
          configuration, _("route start"),
          [this](const wxString& stage, const wxString& detail, int value,
                 int range) {
            if (m_RoutingProgressDialog && m_RoutingProgressDialog->IsShown())
              UpdateRoutingProgress(stage, detail, value, range);
          });
    } else if (use_experimental_chart_safety &&
               enforce_experimental_chart_safety) {
      wxLogMessage(
          "WR_ROUTE_MASK_PREWARM_DEFERRED context=route start "
          "route=\"%s to %s\" policy=scout-or-bounded-tile-refinement",
          configuration.Start, configuration.End);
    }
    if (!configuration.chart_safety_scout_preview &&
        configuration.MinimumDepthMeters > 0.0 &&
        use_experimental_chart_safety && enforce_experimental_chart_safety) {
      wxString endpoint_failure;
      if (!EndpointMeetsMinimumDepth(
              configuration, configuration.StartLat, configuration.StartLon,
              _("Route start"), &endpoint_failure) ||
          !EndpointMeetsMinimumDepth(
              configuration, configuration.EndLat, configuration.EndLon,
              _("Route destination"), &endpoint_failure)) {
        routemapoverlay->SetError(endpoint_failure);
        return;
      }
    }
    if (!s_loggedDetectLandGshhsWarning) {
      wxLogMessage(
          use_experimental_chart_safety
              ? (enforce_experimental_chart_safety
                     ? "WeatherRouting Detect Land: initializing "
                       "experimental OpenCPN chart-backed segment safety "
                       "checks with propagation and final-route enforcement "
                       "enabled. Chart safety grid tiles are prewarmed on the "
                       "main thread before worker propagation."
                     : "WeatherRouting Detect Land: initializing "
                       "experimental OpenCPN chart-backed segment safety "
                       "diagnostics. GSHHS remains the enforcement path.")
              : "WeatherRouting Detect Land: initializing GSHHS shoreline "
                "checks.");
      s_loggedDetectLandGshhsWarning = true;
    }
  }

  /* same with grib */
  if (!configuration.RouteGUID.IsEmpty() && configuration.UseGrib)
    SendPluginMessage(wxS("GRIB_VALUES_REQUEST"), _T(""));

  // Climatology may lazily construct its controls on the first data query.
  // Preflight every configured service here so route workers never initialize
  // wxWidgets/GTK state.
  WeatherDataProvider::PrepareClimatologyForWorkers(configuration);

  // already running?
  for (std::list<RouteMapOverlay*>::iterator it = m_RunningRouteMaps.begin();
       it != m_RunningRouteMaps.end(); it++)
    if (*it == routemapoverlay) return;

  if (!m_bRunning) m_StatisticsDialog.SetRunTime(m_RunTime = wxTimeSpan(0));

  // already waiting?
  for (std::list<RouteMapOverlay*>::iterator it = m_WaitingRouteMaps.begin();
       it != m_WaitingRouteMaps.end(); it++) {
    if (*it == routemapoverlay) return;
  }

  if (m_ChartSafetyComputeProgressActive && !m_ActiveMultiLegSequence &&
      !m_ActiveMultiLegDepartureOptimization) {
    int value = m_ChartSafetyComputeProgressStartedRoutes;
    int range = wxMax(m_ChartSafetyComputeProgressTotalRoutes, value + 1);
    UpdateChartSafetyComputeProgress(_("Computing route"), routemapoverlay,
                                     value, range);
    m_ChartSafetyComputeProgressStartedRoutes++;
  }

  routemapoverlay->Reset();
  m_RoutesToRun++;
  m_WaitingRouteMaps.push_back(routemapoverlay);
  SetEnableConfigurationMenu();
  UpdateRouteMap(routemapoverlay);
}

void WeatherRouting::StartAll() {
  for (int i = 0; i < m_panel->m_lWeatherRoutes->GetItemCount(); i++) {
    WeatherRoute* weatherroute = reinterpret_cast<WeatherRoute*>(
        wxUIntToPtr(m_panel->m_lWeatherRoutes->GetItemData(i)));
    Start(weatherroute->routemapoverlay);
  }
}

bool WeatherRouting::RetryRouteAfterMissingChartSafetyTiles(
    RouteMapOverlay* routemapoverlay) {
  if (!routemapoverlay) return false;

  RouteMapConfiguration configuration = routemapoverlay->GetConfiguration();
  if (!configuration.DetectLand ||
      configuration.chart_safety_missing_tile_rejections <= 0) {
    return false;
  }

  bool use_chart_safety = false;
  bool enforce_chart_safety = false;
  ReadExperimentalChartSafetySettings(use_chart_safety, enforce_chart_safety);
  if (!use_chart_safety || !enforce_chart_safety) return false;

  int max_retries = ReadExperimentalChartSafetyMissingTileMaxRetries();
  if (configuration.chart_safety_missing_tile_retry_count >= max_retries) {
    wxString reason = wxString::Format(
        _("Chart safety grid data unavailable for route corridor after %d "
          "prewarm retries"),
        configuration.chart_safety_missing_tile_retry_count);
    routemapoverlay->SetFailureReason(reason);
    wxLogMessage(
        "WR_GRID_TILE_RETRY_EXHAUSTED route=\"%s to %s\" retries=%d "
        "missing_rejections=%ld first_tile=(%d,%d) "
        "first_tile_min=(%.6f,%.6f) "
        "missing_bbox=[lat %.6f..%.6f lon %.6f..%.6f].",
        configuration.Start, configuration.End,
        configuration.chart_safety_missing_tile_retry_count,
        configuration.chart_safety_missing_tile_rejections,
        configuration.chart_safety_missing_tile_first_lat_tile,
        configuration.chart_safety_missing_tile_first_lon_tile,
        configuration.chart_safety_missing_tile_first_min_lat,
        configuration.chart_safety_missing_tile_first_min_lon,
        configuration.chart_safety_missing_tile_min_lat,
        configuration.chart_safety_missing_tile_max_lat,
        configuration.chart_safety_missing_tile_min_lon,
        configuration.chart_safety_missing_tile_max_lon);
    return false;
  }

  int next_retry = configuration.chart_safety_missing_tile_retry_count + 1;
  wxLogMessage(
      "WR_GRID_TILE_RETRY_ROUTE route=\"%s to %s\" retry=%d/%d "
      "missing_rejections=%ld first_tile=(%d,%d) "
      "first_tile_min=(%.6f,%.6f) "
      "missing_bbox=[lat %.6f..%.6f lon %.6f..%.6f].",
      configuration.Start, configuration.End, next_retry, max_retries,
      configuration.chart_safety_missing_tile_rejections,
      configuration.chart_safety_missing_tile_first_lat_tile,
      configuration.chart_safety_missing_tile_first_lon_tile,
      configuration.chart_safety_missing_tile_first_min_lat,
      configuration.chart_safety_missing_tile_first_min_lon,
      configuration.chart_safety_missing_tile_min_lat,
      configuration.chart_safety_missing_tile_max_lat,
      configuration.chart_safety_missing_tile_min_lon,
      configuration.chart_safety_missing_tile_max_lon);

  // Compatibility fallback for a route worker which exited before the normal
  // pause/resume handshake completed.  Service only the exact requests the
  // worker published; do not grow an arbitrary rectangle around them.
  PlugInSegmentSafetyRequestServiceResult service = {};
  service.struct_size = sizeof(service);
  weather_routing::chart_safety_host::ServicePendingRequests(
      64, 250, &service);
  wxLogMessage(
      "WR_GRID_TILE_RETRY_EXACT_SERVICE route=\"%s to %s\" retry=%d/%d "
      "serviced=%d built=%d failed=%d pending_after=%d elapsed_ms=%d",
      configuration.Start, configuration.End, next_retry, max_retries,
      service.requests_serviced, service.masks_built, service.requests_failed,
      service.pending_after, service.elapsed_ms);

  configuration.chart_safety_missing_tile_retry_count = next_retry;
  configuration.chart_safety_missing_tile_rejections = 0;
  configuration.chart_safety_missing_tile_first_lat_tile = 0;
  configuration.chart_safety_missing_tile_first_lon_tile = 0;
  configuration.chart_safety_missing_tile_first_min_lat = NAN;
  configuration.chart_safety_missing_tile_first_min_lon = NAN;
  configuration.chart_safety_missing_tile_min_lat = NAN;
  configuration.chart_safety_missing_tile_max_lat = NAN;
  configuration.chart_safety_missing_tile_min_lon = NAN;
  configuration.chart_safety_missing_tile_max_lon = NAN;
  routemapoverlay->SetConfiguration(configuration);
  routemapoverlay->Reset();
  Start(routemapoverlay);
  return true;
}

void WeatherRouting::Stop(RouteMapOverlay* routemapoverlay) {
  routemapoverlay->Stop();
  // Wait for threads to finish
  while (routemapoverlay->Running()) wxThread::Sleep(100);
  routemapoverlay->ResetFinished();
  routemapoverlay->DeleteThread();
}

void WeatherRouting::StopAll() {
  CancelDeferredRoutingStart();
  CancelMultiLegSequence();

  /* stop all the threads at once, rather than waiting for each one before
     telling the next to stop */
  for (auto it : m_RunningRouteMaps) it->Stop();

  wxProgressDialog* progressdialog = NULL;

  int c = 0;
  for (std::list<RouteMapOverlay*>::iterator it = m_RunningRouteMaps.begin();
       it != m_RunningRouteMaps.end(); it++) {
    // Wait for threads to finish
    while ((*it)->Running()) wxThread::Sleep(100);

    (*it)->ResetFinished();
    (*it)->DeleteThread();

    if (progressdialog) progressdialog->Update(c++);
  }

  delete progressdialog;

  m_RunningRouteMaps.clear();
  m_WaitingRouteMaps.clear();

  s_chartSafetySharedPrewarmScopes.clear();
  s_chartSafetyPreparedScoutScopes.clear();
  weather_routing::chart_safety_host::ReleaseRouteMaskPins();

  UpdateStates();

  m_RoutesToRun = 0;
  m_panel->m_gProgress->SetValue(0);
  m_bRunning = false;
  m_ChartSafetyComputeProgressActive = false;
  m_ChartSafetyComputeProgressAll = false;
  m_ChartSafetyComputeProgressTotalRoutes = 0;
  m_ChartSafetyComputeProgressStartedRoutes = 0;
  m_ChartSafetyComputeProgressCompletedRoutes = 0;
  CloseRoutingProgress();

  SetEnableConfigurationMenu();
  if (m_StartTime.IsValid())
    m_StatisticsDialog.SetRunTime(m_RunTime += wxDateTime::Now() - m_StartTime);
}

void WeatherRouting::Reset() {
  if (m_bRunning) StopAll();

  for (int i = 0; i < m_panel->m_lWeatherRoutes->GetItemCount(); i++) {
    WeatherRoute* weatherroute = reinterpret_cast<WeatherRoute*>(
        wxUIntToPtr(m_panel->m_lWeatherRoutes->GetItemData(i)));
    weatherroute->routemapoverlay->Reset();
  }
  m_positionOnRoute = nullptr;
  UpdateDialogs();

  GetParent()->Refresh();
}

void WeatherRouting::DeleteRouteMaps(
    std::list<RouteMapOverlay*> routemapoverlays) {
  // RoutingTablePanel is retained when its AUI pane is hidden. Detach it from
  // an overlay before deleting that overlay so later chart paints cannot use
  // a dangling route pointer.
  if (m_RoutingTablePanel &&
      std::find(routemapoverlays.begin(), routemapoverlays.end(),
                m_RoutingTablePanel->GetRouteMap()) != routemapoverlays.end())
    m_RoutingTablePanel->SetRouteMap(nullptr);

  for (RouteMapOverlay* route : routemapoverlays) {
    if (std::find(m_StabilityCorridorSourceRoutes.begin(),
                  m_StabilityCorridorSourceRoutes.end(),
                  route) != m_StabilityCorridorSourceRoutes.end()) {
      HideStabilityCorridor("candidate_deleted");
      m_StabilityCorridorSourceRoutes.clear();
      m_StabilityCorridorRoutes.clear();
      m_StabilityCorridorResult =
          weather_routing_engine::StabilityCorridorResult();
      break;
    }
  }
  bool current = false;
  for (std::list<RouteMapOverlay*>::iterator it = routemapoverlays.begin();
       it != routemapoverlays.end(); it++) {
    // Deleting a wxListCtrl row can synchronously change the selection and
    // bind the table to another member of this deletion batch. Check every
    // route as well as detaching the initially displayed route above.
    if (m_RoutingTablePanel &&
        m_RoutingTablePanel->GetRouteMap() == *it)
      m_RoutingTablePanel->SetRouteMap(nullptr);

    std::list<RouteMapOverlay*> currentroutemaps = CurrentRouteMaps();
    for (std::list<RouteMapOverlay*>::iterator cit = currentroutemaps.begin();
         cit != currentroutemaps.end(); cit++)
      if (*it == *cit) {
        current = true;
        break;
      }

    for (std::list<RouteMapOverlay*>::iterator wit = m_WaitingRouteMaps.begin();
         wit != m_WaitingRouteMaps.end(); wit++)
      if (*it == *wit) {
        m_WaitingRouteMaps.erase(wit);
        break;
      }

    for (std::list<RouteMapOverlay*>::iterator rit = m_RunningRouteMaps.begin();
         rit != m_RunningRouteMaps.end(); rit++)
      if (*it == *rit) {
        m_RunningRouteMaps.erase(rit);
        break;
      }

    for (int i = 0; i < m_panel->m_lWeatherRoutes->GetItemCount(); i++) {
      WeatherRoute* weatherroute = reinterpret_cast<WeatherRoute*>(
          wxUIntToPtr(m_panel->m_lWeatherRoutes->GetItemData(i)));
      if (weatherroute->routemapoverlay == *it) {
        m_panel->m_lWeatherRoutes->DeleteItem(i);
        break;
      }
    }

    for (std::list<WeatherRoute*>::iterator writ = m_WeatherRoutes.begin();
         writ != m_WeatherRoutes.end(); writ++)
      if ((*writ)->routemapoverlay == *it) {
        delete *writ;
        m_WeatherRoutes.erase(writ);
        break;
      }
  }

  m_ReportDialog.m_bReportStale = true;

  SetEnableConfigurationMenu();

  if (current) UpdateDialogs();
}

void WeatherRouting::SaveLastUsedConfigurationDefaults(
    const RouteMapConfiguration& configuration) {
  wxFileConfig* pConf = GetOCPNConfigObject();
  if (!pConf) return;

  pConf->SetPath(_T("/PlugIns/WeatherRouting/LastUsedConfiguration"));
  pConf->Write(_T("Boat"), configuration.boatFileName);
  pConf->Write(_T("DeltaTime"), configuration.DeltaTime);
  pConf->Write(_T("Integrator"), static_cast<long>(configuration.Integrator));
  pConf->Write(_T("MaxDivertedCourse"), configuration.MaxDivertedCourse);
  pConf->Write(_T("MaxCourseAngle"), configuration.MaxCourseAngle);
  pConf->Write(_T("MaxSearchAngle"), configuration.MaxSearchAngle);
  pConf->Write(_T("MaxTrueWindKnots"), configuration.MaxTrueWindKnots);
  pConf->Write(_T("MaxApparentWindKnots"), configuration.MaxApparentWindKnots);
  pConf->Write(_T("MaxSwellMeters"), configuration.MaxSwellMeters);
  pConf->Write(_T("MaxLatitude"), configuration.MaxLatitude);
  pConf->Write(_T("TackingTime"), configuration.TackingTime);
  pConf->Write(_T("JibingTime"), configuration.JibingTime);
  pConf->Write(_T("SailPlanChangeTime"), configuration.SailPlanChangeTime);
  pConf->Write(_T("WindVSCurrent"), configuration.WindVSCurrent);
  pConf->Write(_T("AvoidCycloneTracks"), configuration.AvoidCycloneTracks);
  pConf->Write(_T("CycloneMonths"), configuration.CycloneMonths);
  pConf->Write(_T("CycloneDays"), configuration.CycloneDays);
  pConf->Write(_T("UseGrib"), configuration.UseGrib);
  pConf->Write(_T("ClimatologyType"),
               static_cast<long>(configuration.ClimatologyType));
  pConf->Write(_T("AllowDataDeficient"), configuration.AllowDataDeficient);
  pConf->Write(_T("WindStrength"), configuration.WindStrength);
  pConf->Write(_T("UpwindEfficiency"), configuration.UpwindEfficiency);
  pConf->Write(_T("DownwindEfficiency"), configuration.DownwindEfficiency);
  pConf->Write(_T("NightCumulativeEfficiency"),
               configuration.NightCumulativeEfficiency);
  pConf->Write(_T("DetectLand"), configuration.DetectLand);
  pConf->Write(_T("SafetyMarginLand"), configuration.SafetyMarginLand);
  pConf->Write(_T("MinimumDepthMeters"), configuration.MinimumDepthMeters);
  pConf->Write(_T("DetectBoundary"), configuration.DetectBoundary);
  pConf->Write(_T("Currents"), configuration.Currents);
  pConf->Write(_T("OptimizeTacking"), configuration.OptimizeTacking);
  pConf->Write(_T("InvertedRegions"), configuration.InvertedRegions);
  pConf->Write(_T("UseReverseReachabilityRecovery"),
               configuration.UseReverseReachabilityRecovery);
  pConf->Write(_T("Anchoring"), configuration.Anchoring);
  pConf->Write(_T("FromDegree"), configuration.FromDegree);
  pConf->Write(_T("ToDegree"), configuration.ToDegree);
  pConf->Write(_T("UseOptimalAngles"), configuration.UseOptimalAngles);
  pConf->Write(_T("ByDegrees"), configuration.ByDegrees);
  pConf->Write(_T("UseMotor"), configuration.UseMotor);
  pConf->Write(_T("MotorSpeedThreshold"), configuration.MotorSpeedThreshold);
  pConf->Write(_T("MotorSpeed"), configuration.MotorSpeed);
  pConf->Write(_T("DepartureTimeOptimizationConcurrentRoutes"),
               configuration.DepartureTimeOptimizationConcurrentRoutes);
  pConf->Write(_T("ArrivalSearchHorizonMinutes"),
               configuration.ArrivalSearchHorizonMinutes);
  pConf->Write(_T("ArrivalSafetyMarginMinutes"),
               configuration.ArrivalSafetyMarginMinutes);
  pConf->Write(
      _T("RoutingEffortPercent"),
      weather_routing::NormalizeRoutingEffortPercent(
          configuration.RoutingEffortPercent));
  pConf->Flush();
}

void WeatherRouting::ApplyLastUsedConfigurationDefaults(
    RouteMapConfiguration& configuration) const {
  wxFileConfig* pConf = GetOCPNConfigObject();
  if (!pConf) return;

  pConf->SetPath(_T("/PlugIns/WeatherRouting/LastUsedConfiguration"));
  wxString boat;
  if (pConf->Read(_T("Boat"), &boat) && !boat.IsEmpty())
    configuration.boatFileName = boat;

  pConf->Read(_T("DeltaTime"), &configuration.DeltaTime,
              configuration.DeltaTime);
  long integrator = configuration.Integrator;
  pConf->Read(_T("Integrator"), &integrator, integrator);
  configuration.Integrator =
      static_cast<RouteMapConfiguration::IntegratorType>(integrator);
  pConf->Read(_T("MaxDivertedCourse"), &configuration.MaxDivertedCourse,
              configuration.MaxDivertedCourse);
  pConf->Read(_T("MaxCourseAngle"), &configuration.MaxCourseAngle,
              configuration.MaxCourseAngle);
  pConf->Read(_T("MaxSearchAngle"), &configuration.MaxSearchAngle,
              configuration.MaxSearchAngle);
  pConf->Read(_T("MaxTrueWindKnots"), &configuration.MaxTrueWindKnots,
              configuration.MaxTrueWindKnots);
  pConf->Read(_T("MaxApparentWindKnots"), &configuration.MaxApparentWindKnots,
              configuration.MaxApparentWindKnots);
  pConf->Read(_T("MaxSwellMeters"), &configuration.MaxSwellMeters,
              configuration.MaxSwellMeters);
  pConf->Read(_T("MaxLatitude"), &configuration.MaxLatitude,
              configuration.MaxLatitude);
  pConf->Read(_T("TackingTime"), &configuration.TackingTime,
              configuration.TackingTime);
  pConf->Read(_T("JibingTime"), &configuration.JibingTime,
              configuration.JibingTime);
  pConf->Read(_T("SailPlanChangeTime"), &configuration.SailPlanChangeTime,
              configuration.SailPlanChangeTime);
  pConf->Read(_T("WindVSCurrent"), &configuration.WindVSCurrent,
              configuration.WindVSCurrent);
  pConf->Read(_T("AvoidCycloneTracks"), &configuration.AvoidCycloneTracks,
              configuration.AvoidCycloneTracks);
  pConf->Read(_T("CycloneMonths"), &configuration.CycloneMonths,
              configuration.CycloneMonths);
  pConf->Read(_T("CycloneDays"), &configuration.CycloneDays,
              configuration.CycloneDays);
  pConf->Read(_T("UseGrib"), &configuration.UseGrib, configuration.UseGrib);
  long climatology_type = configuration.ClimatologyType;
  pConf->Read(_T("ClimatologyType"), &climatology_type, climatology_type);
  configuration.ClimatologyType =
      static_cast<RouteMapConfiguration::ClimatologyDataType>(climatology_type);
  pConf->Read(_T("AllowDataDeficient"), &configuration.AllowDataDeficient,
              configuration.AllowDataDeficient);
  pConf->Read(_T("WindStrength"), &configuration.WindStrength,
              configuration.WindStrength);
  pConf->Read(_T("UpwindEfficiency"), &configuration.UpwindEfficiency,
              configuration.UpwindEfficiency);
  pConf->Read(_T("DownwindEfficiency"), &configuration.DownwindEfficiency,
              configuration.DownwindEfficiency);
  pConf->Read(_T("NightCumulativeEfficiency"),
              &configuration.NightCumulativeEfficiency,
              configuration.NightCumulativeEfficiency);
  pConf->Read(_T("DetectLand"), &configuration.DetectLand,
              configuration.DetectLand);
  pConf->Read(_T("SafetyMarginLand"), &configuration.SafetyMarginLand,
              configuration.SafetyMarginLand);
  pConf->Read(_T("MinimumDepthMeters"), &configuration.MinimumDepthMeters,
              configuration.MinimumDepthMeters);
  configuration.MinimumDepthMeters =
      wxMax(0.0, configuration.MinimumDepthMeters);
  pConf->Read(_T("DetectBoundary"), &configuration.DetectBoundary,
              configuration.DetectBoundary);
  pConf->Read(_T("Currents"), &configuration.Currents, configuration.Currents);
  pConf->Read(_T("OptimizeTacking"), &configuration.OptimizeTacking,
              configuration.OptimizeTacking);
  pConf->Read(_T("InvertedRegions"), &configuration.InvertedRegions,
              configuration.InvertedRegions);
  pConf->Read(_T("UseReverseReachabilityRecovery"),
              &configuration.UseReverseReachabilityRecovery,
              configuration.UseReverseReachabilityRecovery);
  pConf->Read(_T("Anchoring"), &configuration.Anchoring,
              configuration.Anchoring);
  pConf->Read(_T("FromDegree"), &configuration.FromDegree,
              configuration.FromDegree);
  pConf->Read(_T("ToDegree"), &configuration.ToDegree, configuration.ToDegree);
  pConf->Read(_T("UseOptimalAngles"), &configuration.UseOptimalAngles,
              configuration.UseOptimalAngles);
  pConf->Read(_T("ByDegrees"), &configuration.ByDegrees,
              configuration.ByDegrees);
  pConf->Read(_T("UseMotor"), &configuration.UseMotor, configuration.UseMotor);
  pConf->Read(_T("MotorSpeedThreshold"), &configuration.MotorSpeedThreshold,
              configuration.MotorSpeedThreshold);
  pConf->Read(_T("MotorSpeed"), &configuration.MotorSpeed,
              configuration.MotorSpeed);
  long departure_concurrent_routes =
      configuration.DepartureTimeOptimizationConcurrentRoutes;
  pConf->Read(_T("DepartureTimeOptimizationConcurrentRoutes"),
              &departure_concurrent_routes, departure_concurrent_routes);
  configuration.DepartureTimeOptimizationConcurrentRoutes = std::max(
      0L, std::min(
              static_cast<long>(
                  weather_routing::kMaximumParallelDepartureCandidates),
              departure_concurrent_routes));
  long arrival_search_horizon = configuration.ArrivalSearchHorizonMinutes;
  pConf->Read(_T("ArrivalSearchHorizonMinutes"),
              &arrival_search_horizon, arrival_search_horizon);
  configuration.ArrivalSearchHorizonMinutes =
      static_cast<int>(std::max(60L, std::min(43200L,
                                             arrival_search_horizon)));
  long arrival_safety_margin = configuration.ArrivalSafetyMarginMinutes;
  pConf->Read(_T("ArrivalSafetyMarginMinutes"), &arrival_safety_margin,
              arrival_safety_margin);
  configuration.ArrivalSafetyMarginMinutes =
      static_cast<int>(std::max(0L, std::min(360L,
                                            arrival_safety_margin)));
  long routing_effort_percent = configuration.RoutingEffortPercent;
  pConf->Read(_T("RoutingEffortPercent"), &routing_effort_percent,
              routing_effort_percent);
  configuration.RoutingEffortPercent =
      weather_routing::NormalizeRoutingEffortPercent(
          static_cast<int>(routing_effort_percent));
}

RouteMapConfiguration WeatherRouting::DefaultConfiguration() {
  RouteMapConfiguration configuration;

  if (RouteMap::Positions.size() >= 1) {
    RouteMapPosition& p = *RouteMap::Positions.begin();
    configuration.Start = p.Name;
    configuration.StartLat = p.lat, configuration.StartLon = p.lon;
  } else
    configuration.StartLat = 0, configuration.StartLon = 0;

  configuration.StartTime = wxDateTime::Now();
  configuration.PlannedArrivalTime =
      configuration.StartTime + wxTimeSpan::Hours(24);
  configuration.DeltaTime = 3600;

  if (RouteMap::Positions.size() >= 2) {
    RouteMapPosition& p = *(++RouteMap::Positions.begin());
    configuration.End = p.Name;
    configuration.EndLat = p.lat, configuration.EndLon = p.lon;
  } else
    configuration.EndLat = 0, configuration.EndLon = 0;

  configuration.boatFileName = weather_routing_pi::StandardPath() +
                               _T("boats") + wxFileName::GetPathSeparator() +
                               _T("Boat.xml");

  configuration.Integrator = RouteMapConfiguration::NEWTON;

  configuration.MaxDivertedCourse = 90;
  configuration.MaxCourseAngle = 180;
  configuration.MaxSearchAngle = 120;
  configuration.MaxTrueWindKnots = 50;      // Safety margin for wind speed
  configuration.MaxApparentWindKnots = 50;  // Safety margin for wind speed

  configuration.MaxSwellMeters = 20.;
  configuration.MaxLatitude = 90;
  configuration.TackingTime = 0;
  configuration.JibingTime = 0;
  configuration.SailPlanChangeTime = 0;
  configuration.WindVSCurrent = 0;

  configuration.AvoidCycloneTracks = false;
  configuration.CycloneMonths = 1;
  configuration.CycloneDays = 0;

  configuration.UseGrib = true;
  configuration.ClimatologyType = RouteMapConfiguration::MOST_LIKELY;
  configuration.AllowDataDeficient = false;
  configuration.WindStrength = 1;
  configuration.DetectLand = true;
  configuration.SafetyMarginLand = 0.;
  configuration.MinimumDepthMeters = 0.;
  configuration.DetectBoundary = false;
  configuration.Currents = false;
  configuration.OptimizeTacking = false;
  configuration.InvertedRegions = false;
  configuration.UseReverseReachabilityRecovery = false;
  configuration.Anchoring = false;

  configuration.FromDegree = 0;
  configuration.ToDegree = 180;
  configuration.UseOptimalAngles = false;
  configuration.ByDegrees = 5;
  configuration.UseMotor = false;
  configuration.MotorSpeedThreshold = 2.0;
  configuration.MotorSpeed = 5.0;

  ApplyLastUsedConfigurationDefaults(configuration);

  return configuration;
}
