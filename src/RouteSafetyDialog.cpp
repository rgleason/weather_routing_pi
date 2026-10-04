/* Copyright (C) 2026 OpenCPN contributors. GPL v3 or later. */
#include "RouteSafetyDialog.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <fstream>
#include <wx/wx.h>
#include <wx/fileconf.h>
#include <wx/filename.h>
#include <wx/listctrl.h>

#include "ChartSafetyHost.h"
#include "pidc.h"
#include "weather_routing_pi.h"
#ifdef __OCPN__ANDROID__
#include "AndroidDialogHeader.h"
#endif

using weather_routing::RouteCheckState;
using weather_routing::RouteSafetyCheck;
using weather_routing::SafetyCheckWaypoint;

namespace {
long long NowMs() {
  return std::chrono::duration_cast<std::chrono::milliseconds>(
      std::chrono::steady_clock::now().time_since_epoch()).count();
}
wxString Utf8(const std::string& s) { return wxString::FromUTF8(s.c_str()); }
const char* StateName(RouteCheckState state) {
  switch (state) {
    case RouteCheckState::Running: return "running";
    case RouteCheckState::Complete: return "complete";
    case RouteCheckState::Cancelled: return "cancelled";
    case RouteCheckState::Outdated: return "outdated";
    default: return "invalid";
  }
}
const char* SourceName(int source) {
  switch (source) {
    case PI_SEGMENT_SAFETY_SOURCE_VECTOR_CHART: return "Vector chart";
    case PI_SEGMENT_SAFETY_SOURCE_CM93: return "CM93";
    case PI_SEGMENT_SAFETY_SOURCE_PLUGIN_VECTOR: return "Chart plugin";
    default: return "Unavailable";
  }
}
Json::Value PointsJson(const std::vector<SafetyCheckWaypoint>& points) {
  Json::Value result(Json::arrayValue);
  for (const auto& p : points) {
    Json::Value row;
    row["guid"] = p.guid; row["name"] = p.name;
    row["lat"] = p.lat; row["lon"] = p.lon;
    result.append(row);
  }
  return result;
}
}  // namespace

RouteSafetyDialog::RouteSafetyDialog(weather_routing_pi& plugin,
    const wxString& route_guid, double minimum_depth_m, double land_margin_nm)
    : wxDialog(plugin.GetParentWindow(), wxID_ANY, plugin.GetCommonName() + _(" — Check this route"),
               wxDefaultPosition, wxSize(860, 620), wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER),
      plugin_(plugin), route_guid_(route_guid), timer_(this) {
  auto* root = new wxBoxSizer(wxVERTICAL);
#ifdef __OCPN__ANDROID__
  auto* page = new wxScrolledWindow(this, wxID_ANY);
  page->SetScrollRate(0, 24);
  auto* body = new wxBoxSizer(wxVERTICAL);
  wxWindow* content = page;
#else
  auto* body = root;
  wxWindow* content = this;
#endif
  auto* scope = new wxStaticText(content, wxID_ANY,
      _("Checks land, drying areas, charted hazards and optional depth along the route's rhumb-line legs.\n"
        "Traffic rules, restricted areas, bridge clearance and tide height are not checked."));
  body->Add(scope, 0, wxEXPAND | wxALL, 10);
#ifdef __OCPN__ANDROID__
  auto* settings = new wxBoxSizer(wxVERTICAL);
#else
  auto* settings = new wxBoxSizer(wxHORIZONTAL);
#endif
  settings->Add(new wxStaticText(content, wxID_ANY, _("Minimum charted depth (m):")), 0,
                wxALIGN_CENTER_VERTICAL | wxRIGHT, 5);
  minimum_depth_ = new wxTextCtrl(content, wxID_ANY, wxString::Format("%.2f", minimum_depth_m),
                                 wxDefaultPosition, wxSize(80, -1));
  settings->Add(minimum_depth_, 0, wxRIGHT, 5);
  settings->Add(new wxStaticText(content, wxID_ANY, _("0 = depth off")), 0,
                wxALIGN_CENTER_VERTICAL | wxRIGHT, 20);
  settings->Add(new wxStaticText(content, wxID_ANY, _("Land clearance (NM):")), 0,
                wxALIGN_CENTER_VERTICAL | wxRIGHT, 5);
  land_margin_ = new wxTextCtrl(content, wxID_ANY, wxString::Format("%.2f", land_margin_nm),
                               wxDefaultPosition, wxSize(80, -1));
  settings->Add(land_margin_, 0);
  body->Add(settings, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 10);
  summary_ = new wxStaticText(content, wxID_ANY, _("Ready to check."));
  body->Add(summary_, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 10);
  findings_ = new wxListCtrl(this, wxID_ANY, wxDefaultPosition, wxDefaultSize,
                            wxLC_REPORT | wxLC_SINGLE_SEL);
  findings_->InsertColumn(0, _("Leg"), wxLIST_FORMAT_LEFT, 45);
  findings_->InsertColumn(1, _("Finding"), wxLIST_FORMAT_LEFT, 210);
  findings_->InsertColumn(2, _("Position"), wxLIST_FORMAT_LEFT, 180);
  findings_->InsertColumn(3, _("Depth (m)"), wxLIST_FORMAT_LEFT, 85);
  findings_->InsertColumn(4, _("Chart / source"), wxLIST_FORMAT_LEFT, 270);
#ifdef __OCPN__ANDROID__
  findings_->Hide();
  android_findings_ = new wxChoice(content, wxID_ANY);
  body->Add(android_findings_, 0, wxEXPAND | wxALL, 10);
  android_findings_->Bind(wxEVT_CHOICE, [this](wxCommandEvent&) {
    selected_ = android_findings_->GetSelection(); RefreshReport();
  });
  android_details_ = new wxStaticText(content, wxID_ANY, wxEmptyString);
  body->Add(android_details_, 0, wxEXPAND | wxALL, 10);
#else
  body->Add(findings_, 1, wxEXPAND | wxLEFT | wxRIGHT, 10);
#endif
  details_ = new wxTextCtrl(this, wxID_ANY, wxEmptyString, wxDefaultPosition, wxSize(-1, 110),
                           wxTE_MULTILINE | wxTE_READONLY);
#ifdef __OCPN__ANDROID__
  details_->Hide();
#else
  body->Add(details_, 0, wxEXPAND | wxALL, 10);
#endif
  auto* buttons = new wxBoxSizer(wxHORIZONTAL);
  show_markers_ = new wxCheckBox(content, wxID_ANY, _("Show findings on chart"));
  show_markers_->SetValue(true);
#ifdef __OCPN__ANDROID__
  body->Add(show_markers_, 0, wxEXPAND | wxALL, 10);
  body->AddSpacer(32);
  auto* inset = new wxBoxSizer(wxHORIZONTAL);
  inset->Add(body, 1, wxEXPAND); inset->AddSpacer(40);
  page->SetSizer(inset);
  root->Add(page, 1, wxEXPAND);
#else
  buttons->Add(show_markers_, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 10);
#endif
  show_finding_ = new wxButton(this, wxID_ANY, _("Show selected finding"));
#ifdef __OCPN__ANDROID__
  root->Add(show_finding_, 0, wxEXPAND | wxALL, 8);
#else
  buttons->Add(show_finding_, 0, wxRIGHT, 10);
  buttons->AddStretchSpacer();
#endif
  recheck_ = new wxButton(this, wxID_ANY, _("Recheck"));
  cancel_ = new wxButton(this, wxID_ANY, _("Cancel check"));
#ifdef __OCPN__ANDROID__
  buttons->Add(recheck_, 1, wxEXPAND | wxRIGHT, 8);
  buttons->Add(cancel_, 1, wxEXPAND);
#else
  buttons->Add(recheck_, 0, wxRIGHT, 5);
  buttons->Add(cancel_, 0, wxRIGHT, 5);
#endif
  auto* close = new wxButton(this, wxID_CLOSE, _("Close"));
#ifdef __OCPN__ANDROID__
  close->Hide(); // The persistent touch header provides Close and Android Back.
#else
  buttons->Add(close);
#endif
  root->Add(buttons, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 10);
  SetSizer(root);
#ifndef __OCPN__ANDROID__
  SetMinSize(wxSize(std::max(760, root->GetMinSize().GetWidth()), 540));
#endif
  Bind(wxEVT_TIMER, &RouteSafetyDialog::OnTimer, this);
  recheck_->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { Start(); });
  cancel_->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) {
    if (check_) check_->Cancel();
    RefreshReport();
  });
  show_finding_->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { ShowFinding(); });
  show_markers_->Bind(wxEVT_CHECKBOX, [this](wxCommandEvent&) {
    RequestRefresh(plugin_.GetParentWindow());
  });
  findings_->Bind(wxEVT_LIST_ITEM_SELECTED, [this](wxListEvent& event) {
    selected_ = event.GetIndex(); RefreshReport();
  });
  findings_->Bind(wxEVT_LIST_ITEM_ACTIVATED, [this](wxListEvent&) { ShowFinding(); });
  Bind(wxEVT_CLOSE_WINDOW, [this](wxCloseEvent&) {
    timer_.Stop();
    if (check_) check_->Cancel();
    Hide();
    RequestRefresh(plugin_.GetParentWindow());
  });
  close->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { Close(); });
  show_finding_->Enable(false);
#ifdef __OCPN__ANDROID__
  for (auto* item : settings->GetChildren()) {
    item->SetFlag(wxEXPAND | wxBOTTOM); item->SetBorder(8);
  }
  WR_StyleAndroidControls(this);
  WR_AddAndroidDoneHeader(this, _("Check this route"), [this]() { Close(); }, _("Close"));
  Bind(wxEVT_SIZE, [this](wxSizeEvent& event) { LayoutReport(); event.Skip(); });
  const auto canvas = GetCanvasByIndex(0)->GetClientSize();
  SetSize(canvas.x - 24, canvas.y - 24);
  LayoutReport();
  // The Android host can finish rotating its canvas after the sheet's resize
  // notification, including when returning from the background. Refit only
  // when its size actually changes; keep this independent of check progress.
  auto* fit_timer = new QTimer(GetHandle());
  QObject::connect(fit_timer, &QTimer::timeout, GetHandle(), [this]() {
    if (!GetHandle()->isVisible()) return;
    const auto canvas_size = GetCanvasByIndex(0)->GetClientSize();
    const wxSize sheet_size(canvas_size.x - 24, canvas_size.y - 24);
    if (canvas_size.x >= 100 && canvas_size.y >= 100 && GetSize() != sheet_size) {
      WR_FitAndroidSheet(this);
      LayoutReport();
    }
  });
  fit_timer->start(500);
#endif
  CentreOnParent();
}

RouteSafetyDialog::~RouteSafetyDialog() { timer_.Stop(); }

void RouteSafetyDialog::LayoutReport() {
#ifdef __OCPN__ANDROID__
  WR_LayoutAndroidDetailSheet(this);
#else
  Layout();
#endif
}

bool RouteSafetyDialog::Snapshot(std::vector<SafetyCheckWaypoint>& points,
    wxString& route_name, wxString& error) const {
  points.clear();
  auto route = GetRoute_Plugin(route_guid_);
  if (!route || !route->pWaypointList) { error = _("The route is no longer available."); return false; }
  route_name = route->m_NameString;
  for (auto* node = route->pWaypointList->GetFirst(); node; node = node->GetNext()) {
    const auto* p = node->GetData();
    if (!p) { error = _("The route contains an unavailable waypoint."); return false; }
    points.push_back({p->m_GUID.ToStdString(), p->m_MarkName.ToStdString(), p->m_lat, p->m_lon});
  }
  if (points.size() < 2) { error = _("The route needs at least two waypoints."); return false; }
  return true;
}

void RouteSafetyDialog::Start() {
  timer_.Stop();
  check_.reset();
  findings_->DeleteAllItems();
  details_->Clear(); selected_ = -1;
#ifdef __OCPN__ANDROID__
  android_findings_->Clear(); android_findings_->Enable(false);
  android_details_->SetLabel(wxEmptyString);
#endif
  double depth = 0, margin = 0;
  std::vector<SafetyCheckWaypoint> points;
  wxString error;
  if (!minimum_depth_->GetValue().ToDouble(&depth) || !std::isfinite(depth) || depth < 0 ||
      !land_margin_->GetValue().ToDouble(&margin) || !std::isfinite(margin) || margin < 0 || margin > 100) {
    error = _("Enter a non-negative depth and a land clearance between 0 and 100 NM.");
  } else if (!weather_routing::chart_safety_host::Available()) {
    error = _("Route checking needs a compatible OpenCPN chart-safety host.");
  } else if (!Snapshot(points, route_name_, error)) {
    // Snapshot supplies the explanation.
  } else {
    identity_ = weather_routing::chart_safety_host::ConfirmedIdentity();
    if (identity_.empty()) error = _("Chart identity is unavailable. Try Recheck after charts finish loading.");
  }
  if (!error.IsEmpty()) {
    summary_->SetLabel(error);
    cancel_->Enable(false); recheck_->Enable(true);
    minimum_depth_->Enable(true); land_margin_->Enable(true);
    show_finding_->Enable(false);
    LayoutReport();
    WriteTestOutput();
    RequestRefresh(plugin_.GetParentWindow());
    return;
  }
  PlugInSegmentSafetyOptions options{};
  options.minimum_depth_m = depth;
  options.safety_margin_nm = margin;
  check_ = std::make_unique<RouteSafetyCheck>(std::move(points), options,
      weather_routing::chart_safety_host::ReviewSegment);
  SetTitle(plugin_.GetCommonName() + _(" — Check this route: ") + route_name_);
  if (auto* config = GetOCPNConfigObject()) {
    const auto old_path = config->GetPath();
    config->SetPath("/PlugIns/WeatherRouting/RouteCheck");
    config->Write("MinimumDepthM", depth); config->Write("LandMarginNM", margin);
    config->SetPath(old_path);
  }
  next_validation_ms_ = 0;
  RefreshReport();
  // The timer also invalidates a completed report if charts or route change.
  timer_.StartOnce(100);
}

bool RouteSafetyDialog::ValidateSnapshot() {
  std::vector<SafetyCheckWaypoint> points;
  wxString name, error;
  if (!Snapshot(points, name, error) || !check_->Matches(points) || name != route_name_) {
    check_->Invalidate(wxTRANSLATE("The route changed or was removed. Recheck before using this report."));
    return false;
  }
  const auto current = weather_routing::chart_safety_host::ConfirmedIdentity();
  if (current.empty() || current != identity_) {
    check_->Invalidate(wxTRANSLATE("The chart selection or chart data changed. Recheck before using this report."));
    return false;
  }
  return true;
}

void RouteSafetyDialog::OnTimer(wxTimerEvent&) {
  if (!check_ || !IsShown()) return;
  if (NowMs() >= next_validation_ms_) {
    ValidateSnapshot(); next_validation_ms_ = NowMs() + 500;
  }
  const auto start = NowMs();
  for (int n = 0; n < 8 && check_->State() == RouteCheckState::Running; ++n) {
    const auto checked = check_->Checked();
    check_->Advance();
    if (check_->Checked() == checked || NowMs() - start >= 30) break;
  }
  if (!test_action_done_ && check_->Checked() > 0 && test_action_ == "cancel") {
    test_action_done_ = true; check_->Cancel();
  }
  if (check_->State() == RouteCheckState::Complete) ValidateSnapshot();
  RefreshReport();
  if (check_->State() != RouteCheckState::Running) {
    if (!test_action_done_ && test_action_ == "recheck") {
      test_action_done_ = true; Start(); return;
    }
    const bool awaits_external_edit = test_action_ == "edit" ||
        test_action_ == "remove" || test_action_ == "rename";
    if (!awaits_external_edit || check_->State() == RouteCheckState::Outdated)
      WriteTestOutput();
  }
  if (check_->State() != RouteCheckState::Outdated && check_->State() != RouteCheckState::Invalid)
    timer_.StartOnce(check_->State() == RouteCheckState::Running ? 100 : 500);
}

void RouteSafetyDialog::RefreshReport() {
  if (!check_) return;
  const bool running = check_->State() == RouteCheckState::Running;
  recheck_->Enable(!running); cancel_->Enable(running);
  minimum_depth_->Enable(!running); land_margin_->Enable(!running);
  wxString text;
  if (check_->State() == RouteCheckState::Outdated || check_->State() == RouteCheckState::Invalid)
    text = wxGetTranslation(Utf8(check_->Error()));
  else if (check_->Clear()) text = _("Complete: no hazards found in the checks performed.");
  else text = wxString::Format(_("%s — %zu/%zu sections checked; %zu hazard sections; %zu unverified sections."),
      running ? _("Checking") : check_->State() == RouteCheckState::Cancelled ? _("Cancelled (partial report)") : _("Complete"),
      check_->Checked(), check_->Total(), check_->HazardChunks(), check_->UnverifiedChunks());
  text += check_->Options().check_depth ?
      wxString::Format(_("\nDepth threshold: %.2f m at chart datum; land clearance: %.2f NM."),
        check_->Options().minimum_depth_m, check_->Options().safety_margin_nm) :
      wxString::Format(_("\nDepth was not checked; land clearance: %.2f NM."), check_->Options().safety_margin_nm);
  const auto& rows = check_->Findings();
  if (rows.size() > 250)
    text += _("\nChart markers show the first 250 findings and the selected finding; the table contains all findings.");
  summary_->SetLabel(text);
  // Only append new findings and refresh the final coalesced stretch.
  const long first = std::max(0L, static_cast<long>(findings_->GetItemCount()) - 1);
  for (long i = first; i < static_cast<long>(rows.size()); ++i) {
    const auto& row = rows[i];
    if (i >= findings_->GetItemCount()) findings_->InsertItem(i, wxString::Format("%zu", row.leg));
    findings_->SetItem(i, 1, _(RouteSafetyCheck::Reason(row.status)));
    findings_->SetItem(i, 2, wxString::Format("%.5f, %.5f", row.lat, row.lon));
    findings_->SetItem(i, 3, row.has_depth ? wxString::Format("%.2f", row.minimum_depth_m) : wxString("—"));
    findings_->SetItem(i, 4, row.chart_path.empty() ? _(SourceName(row.source)) : wxFileName(Utf8(row.chart_path)).GetFullName());
  }
  if (selected_ >= 0 && selected_ < static_cast<long>(rows.size())) {
    const auto& row = rows[selected_];
    const auto& points = check_->Waypoints();
    details_->ChangeValue(wxString::Format(_("Leg %zu: %s → %s\n%s (%.1f–%.1f%% along this leg)\nChart: %s\n%s"),
        row.leg, Utf8(points[row.leg - 1].name), Utf8(points[row.leg].name),
        _(RouteSafetyCheck::Reason(row.status)), 100 * row.from_fraction, 100 * row.to_fraction,
        row.chart_path.empty() ? _(SourceName(row.source)) : Utf8(row.chart_path), Utf8(row.message)));
  }
#ifdef __OCPN__ANDROID__
  while (android_findings_->GetCount() < rows.size()) {
    const auto& row = rows[android_findings_->GetCount()];
    android_findings_->Append(wxString::Format(_("Leg %zu: %s"), row.leg,
        _(RouteSafetyCheck::Reason(row.status))));
  }
  android_findings_->Enable(!rows.empty());
  android_findings_->SetSelection(selected_);
  android_details_->SetLabel(details_->GetValue());
#endif
  show_finding_->Enable(selected_ >= 0 && selected_ < static_cast<long>(rows.size()) &&
                        check_->State() != RouteCheckState::Outdated);
  LayoutReport();
  RequestRefresh(plugin_.GetParentWindow());
}

void RouteSafetyDialog::ShowFinding() {
  if (!check_ || selected_ < 0 || selected_ >= static_cast<long>(check_->Findings().size())) return;
  if (!ValidateSnapshot()) { RefreshReport(); return; }
  const auto& row = check_->Findings()[selected_];
  JumpToPosition(row.lat, row.lon, .002);
}

bool RouteSafetyDialog::Render(piDC& dc, PlugIn_ViewPort& vp) {
  if (!IsShown() || !show_markers_->GetValue() || !check_ ||
      check_->State() == RouteCheckState::Outdated || check_->State() == RouteCheckState::Invalid) return false;
  const auto& rows = check_->Findings();
  bool rendered = false;
  for (std::size_t i = 0; i < rows.size(); ++i) {
    if (i >= 250 && static_cast<long>(i) != selected_) continue;
    wxPoint p; GetCanvasPixLL(&vp, &p, rows[i].lat, rows[i].lon);
    if (p.x < -20 || p.y < -20 || p.x > vp.pix_width + 20 || p.y > vp.pix_height + 20) continue;
    const bool unknown = RouteSafetyCheck::IsUnverified(rows[i].status);
    dc.SetPen(wxPen(unknown ? wxColour(210, 125, 0) : wxColour(215, 35, 35), 3));
    dc.SetBrush(*wxTRANSPARENT_BRUSH);
    dc.DrawCircle(p, static_cast<long>(i) == selected_ ? 10 : 7);
    dc.DrawLine(p.x - 4, p.y - 4, p.x + 4, p.y + 4);
    dc.DrawLine(p.x - 4, p.y + 4, p.x + 4, p.y - 4);
    rendered = true;
  }
  return rendered;
}

bool RouteSafetyDialog::SelectMarker(const wxPoint& point, PlugIn_ViewPort& vp) {
  if (!IsShown() || !show_markers_->GetValue() || !check_ ||
      check_->State() == RouteCheckState::Outdated || check_->State() == RouteCheckState::Invalid) return false;
  for (std::size_t i = 0; i < check_->Findings().size(); ++i) {
    if (i >= 250 && static_cast<long>(i) != selected_) continue;
    const auto& row = check_->Findings()[i];
    wxPoint p; GetCanvasPixLL(&vp, &p, row.lat, row.lon);
    if (std::abs(p.x - point.x) <= 12 && std::abs(p.y - point.y) <= 12) {
      if (!ValidateSnapshot()) { RefreshReport(); return false; }
      selected_ = static_cast<long>(i);
      findings_->SetItemState(selected_, wxLIST_STATE_SELECTED, wxLIST_STATE_SELECTED);
      findings_->EnsureVisible(selected_); RefreshReport(); Raise();
      return true;
    }
  }
  return false;
}

void RouteSafetyDialog::SetTestOutput(const wxString& path, const wxString& action) {
  test_output_ = path; test_action_ = action;
}

void RouteSafetyDialog::WriteTestOutput() {
  if (test_output_.IsEmpty() || test_written_) return;
  test_written_ = true;
  Json::Value result;
  result["version"] = "1.25.0";
  result["state"] = check_ ? StateName(check_->State()) : "unavailable";
  result["clear"] = check_ && check_->Clear();
  result["summary"] = summary_->GetLabel().ToStdString();
  result["recheckPerformed"] = test_action_ == "recheck" && test_action_done_;
  if (check_) {
    std::vector<SafetyCheckWaypoint> points; wxString name, error;
    result["routeUnchanged"] = Snapshot(points, name, error) && check_->Matches(points) && name == route_name_;
    result["before"] = PointsJson(check_->Waypoints()); result["after"] = PointsJson(points);
    result["checked"] = static_cast<Json::UInt64>(check_->Checked());
    result["total"] = static_cast<Json::UInt64>(check_->Total());
    result["hazardSections"] = static_cast<Json::UInt64>(check_->HazardChunks());
    result["unverifiedSections"] = static_cast<Json::UInt64>(check_->UnverifiedChunks());
    result["findings"] = Json::Value(Json::arrayValue);
    for (const auto& row : check_->Findings()) {
      Json::Value value;
      value["leg"] = static_cast<Json::UInt64>(row.leg);
      value["status"] = row.status; value["reason"] = RouteSafetyCheck::Reason(row.status);
      value["source"] = SourceName(row.source); value["chart"] = row.chart_path;
      value["lat"] = row.lat; value["lon"] = row.lon;
      value["hasDepth"] = row.has_depth;
      if (row.has_depth) value["minimumDepthM"] = row.minimum_depth_m;
      result["findings"].append(value);
    }
  }
  std::ofstream stream(test_output_.ToStdString());
  stream << result.toStyledString(); stream.close();
  wxLogMessage("WR_ROUTE_CHECK_TEST state=%s clear=%d output=%s", Utf8(result["state"].asString()),
               check_ && check_->Clear() ? 1 : 0, test_output_);
  wxString hold;
  wxGetEnv("WR_HEADLESS_NO_EXIT", &hold);
  // Post a wxWidgets-owned close event. A plugin-owned CallAfter callback
  // must not close the host synchronously: unloading this library during the
  // close would invalidate that callback's destructor. A normal frame-close
  // event also runs the host's full cleanup before its app-level OnExit().
  if (!hold.IsSameAs("resident", false)) {
    timer_.Stop();
    if (auto* top = wxGetTopLevelParent(plugin_.GetParentWindow())) {
      wxCloseEvent close(wxEVT_CLOSE_WINDOW);
      wxPostEvent(top, close);
    } else wxTheApp->ExitMainLoop();
  }
}
