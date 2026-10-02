// SPDX-License-Identifier: GPL-3.0-or-later
#include "RouteComparisonDialog.h"
#include "WeatherRouting.h"
#include "weather_routing_pi.h"
#include "TimeZoneDisplay.h"
#include <wx/button.h>
#include <wx/fileconf.h>
#include <wx/sizer.h>
#include <numeric>
#include <tuple>
#include <cstdlib>

namespace {
wxString DurationText(std::int64_t seconds) {
  auto text = wxString::Format("%lld h %02lld min", static_cast<long long>(seconds / 3600),
                          static_cast<long long>(seconds % 3600 / 60));
  if (seconds % 60) text += wxString::Format(" %02lld s", static_cast<long long>(seconds % 60));
  return text;
}
wxString TableDuration(std::int64_t seconds) {
  return wxString::Format("%lld:%02lld:%02lld", static_cast<long long>(seconds / 3600),
      static_cast<long long>(seconds % 3600 / 60), static_cast<long long>(seconds % 60));
}
wxDateTime WxTime(supercpn::weather_routing::TimePoint time) {
  return wxDateTime(static_cast<time_t>(time.time_since_epoch().count()));
}
wxString UtcText(const wxDateTime& time) {
  return marine_time::FormatInTimeZone(time, "%Y-%m-%d %H:%M:%S", "UTC", false);
}
wxString EngineTitle(const std::string& engine) {
  if (engine == "original") return _("Quick");
  if (engine == "quick") return _("Standard");
  if (engine == "main") return _("Professional");
  if (engine == "alternative") return _("Alternative");
  return wxString::FromUTF8(engine);
}
wxString WorstKnownText(const weather_routing::RouteComfort& comfort) {
  return comfort.worstCategory > 0 ? wxString::Format("%s %.3f",
      RouteMapOverlay::sailingConditionText(comfort.worstCategory), comfort.worstSeverity)
      : _("Unknown");
}
}

RouteComparisonDialog::RouteComparisonDialog(WeatherRouting& routing)
    : wxDialog(&routing, wxID_ANY, _("Fastest / Comfort — 1.22 prototype"),
               wxDefaultPosition, wxSize(1750, 650),
               wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER), m_Routing(routing) {
  auto* layout = new wxBoxSizer(wxVERTICAL);
  auto* controls = new wxBoxSizer(wxHORIZONTAL);
  controls->Add(new wxStaticText(this, wxID_ANY, _("Fastest")), 0,
                wxALIGN_CENTER_VERTICAL | wxALL, 6);
  m_Slider = new wxSlider(this, wxID_ANY, 0, 0, 100);
  controls->Add(m_Slider, 1, wxEXPAND | wxALL, 6);
  controls->Add(new wxStaticText(this, wxID_ANY, _("Most comfortable")), 0,
                wxALIGN_CENTER_VERTICAL | wxALL, 6);
  m_Order = new wxChoice(this, wxID_ANY);
  m_Order->Append(_("Fastest first")); m_Order->Append(_("Comfort first"));
  m_Order->SetSelection(0);
  controls->Add(m_Order, 0, wxALIGN_CENTER_VERTICAL | wxALL, 6);
  layout->Add(controls, 0, wxEXPAND);
  m_WindOnly = new wxCheckBox(this, wxID_ANY, _("Rank by wind comfort only"));
  auto* config = GetOCPNConfigObject();
  m_WindOnly->SetValue(!config || config->ReadBool(
      "/PlugIns/WeatherRouting/ComfortComparisonWindOnly", true));
  m_WindOnly->SetToolTip(_("Exclude waves from comfort ranking. The worst known leg still includes available waves. Route validation is unchanged."));
  layout->Add(m_WindOnly, 0, wxALL, 6);
  m_Summary = new wxStaticText(this, wxID_ANY, wxEmptyString);
  layout->Add(m_Summary, 0, wxEXPAND | wxALL, 6);
  m_List = new wxListCtrl(this, wxID_ANY, wxDefaultPosition, wxDefaultSize,
                          wxLC_REPORT | wxLC_SINGLE_SEL);
  auto tableFont = m_List->GetFont(); tableFont.SetPointSize(11); m_List->SetFont(tableFont);
  const wxString columns[] = {_("ID"), _("Engine"), _("Departure UTC"), _("ETA UTC"),
      _("Passage"), _("Extra time"), _("Exposure"), _("Average"), _("Difficult"),
      _("Worst known (wind + waves)"), _("Longest difficult"), _("Wave coverage"),
      _("Missing waves"), _("Validation")};
  for (unsigned i = 0; i < std::size(columns); ++i)
    m_List->InsertColumn(i, columns[i], wxLIST_FORMAT_LEFT,
                         i == 2 || i == 3 ? 180 : i == 10 ? 140 : i == 0 ? 75 : 110);
  layout->Add(m_List, 1, wxEXPAND | wxALL, 6);
  auto* note = new wxStaticText(this, wxID_ANY,
      _("Choose a row to show that validated route immediately. Slider sets an extra-time allowance. "
        "Table durations use hours:minutes:seconds. Exposure accumulates condition severity over time (severity-hours). "
        "Wind + wave ranking requires wind and wave height at every route-leg sample (100% coverage). "
        "Coastal wave-grid gaps can prevent this even when the GRIB contains waves. "
        "Rank by wind comfort only to compare without waves. Worst known leg always includes available waves; "
        "conditions in gaps remain unknown. Coverage and missing-wave duration are weighted by leg duration."));
  note->Wrap(1050);
  layout->Add(note, 0, wxEXPAND | wxALL, 6);
  auto* close = new wxButton(this, wxID_CLOSE, _("Close"));
  layout->Add(close, 0, wxALIGN_RIGHT | wxALL, 6);
  SetSizer(layout);
  close->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { Destroy(); });
  Bind(wxEVT_CLOSE_WINDOW, [this](wxCloseEvent&) { Destroy(); });
  m_Slider->Bind(wxEVT_SLIDER, [this](wxCommandEvent&) { Choose(); });
  m_WindOnly->Bind(wxEVT_CHECKBOX, [this](wxCommandEvent&) {
    if (auto* config = GetOCPNConfigObject()) {
      config->Write("/PlugIns/WeatherRouting/ComfortComparisonWindOnly", m_WindOnly->GetValue());
      config->Flush();
    }
    Populate(); Choose();
  });
  m_Order->Bind(wxEVT_CHOICE, [this](wxCommandEvent&) {
    m_SortColumn = m_Order->GetSelection() == 0 ? 4 : 6;
    m_Ascending = true; Populate(); Choose();
  });
  m_List->Bind(wxEVT_LIST_COL_CLICK, [this](wxListEvent& event) {
    if (event.GetColumn() == m_SortColumn) m_Ascending = !m_Ascending;
    else { m_SortColumn = event.GetColumn(); m_Ascending = true; }
    Populate();
  });
  m_List->Bind(wxEVT_LIST_ITEM_SELECTED, [this](wxListEvent& event) {
    if (!m_Updating) Preview(m_List->GetItemData(event.GetIndex()));
  });
  Collect(); Populate(); Choose();
  wxString contract, scenario;
  if (wxGetEnv("WR_HEADLESS_COMFORT_CONTRACT", &contract) &&
      !contract.empty() && wxGetEnv("WR_HEADLESS_SCENARIO", &scenario) && !scenario.empty())
    CallAfter([this, lifecycle = contract == "lifecycle"] { RunHostContract(lifecycle); });
}

void RouteComparisonDialog::RunHostContract(bool lifecycle) {
  bool passed = !m_Entries.empty();
  const bool initialWindOnly = m_WindOnly->GetValue();
  wxString expectedWindOnly;
  if (wxGetEnv("WR_HEADLESS_EXPECTED_COMPARISON_WIND_ONLY", &expectedWindOnly))
    passed = passed && initialWindOnly == (expectedWindOnly == "1");
  wxString expectedUtc;
  wxGetEnv("WR_HEADLESS_EXPECTED_COMPARISON_UTC", &expectedUtc);
  for (long row = 0; row < m_List->GetItemCount(); ++row) {
    wxListEvent event(wxEVT_LIST_ITEM_SELECTED, m_List->GetId());
    event.SetIndex(row); m_List->GetEventHandler()->ProcessEvent(event);
    const auto& entry = m_Entries[m_List->GetItemData(row)];
    passed = passed && m_PreviewId == entry.metric.id;
    if (!expectedUtc.empty())
      passed = passed && m_List->GetItemText(row, 2) == expectedUtc;
    for (const auto& segment : entry.segments) {
      const auto& legs = segment.candidate.result->legs;
      passed = passed && segment.owner->SelectedRetainedCandidateId() == segment.candidate.id &&
          segment.owner->EndTime() == WxTime(legs.back().endTime) &&
          segment.owner->GetPlotData().size() == legs.size();
    }
  }
  for (bool windOnly : {initialWindOnly, !initialWindOnly}) {
    m_WindOnly->SetValue(windOnly);
    wxCommandEvent mode(wxEVT_CHECKBOX, m_WindOnly->GetId());
    m_WindOnly->GetEventHandler()->ProcessEvent(mode);
    if (auto* config = GetOCPNConfigObject())
      passed = passed && config->ReadBool(
          "/PlugIns/WeatherRouting/ComfortComparisonWindOnly", !windOnly) == windOnly;
    for (long row = 0; row < m_List->GetItemCount(); ++row) {
      const auto& full = m_Entries[m_List->GetItemData(row)].fullComfort;
      passed = passed && m_List->GetItemText(row, 9) == WorstKnownText(full) &&
          m_List->GetItemText(row, 12) == TableDuration(full.durationSeconds - full.waveCoveredSeconds);
    }
    for (int value = 0; value <= 100; value += 5) {
      m_Slider->SetValue(value);
      wxCommandEvent event(wxEVT_SLIDER, m_Slider->GetId());
      m_Slider->GetEventHandler()->ProcessEvent(event);
      std::vector<weather_routing::RouteComparisonMetric> metrics;
      for (const auto& entry : m_Entries) metrics.push_back(entry.metric);
      const auto selected = weather_routing::SelectComfortCandidate(metrics, value);
      passed = passed && m_PreviewId == metrics[selected.selected].id;
      const auto& full = m_Entries[selected.selected].fullComfort;
      passed = passed && m_Summary->GetLabel().Contains(
          wxString::Format(_("Missing waves: %s."), DurationText(full.durationSeconds - full.waveCoveredSeconds)));
      if (full.worstCategory > 0)
        passed = passed && m_Summary->GetLabel().Contains(wxString::Format(
            _("Worst known leg, including available waves: #%lu, %s, severity %.3f"),
            static_cast<unsigned long>(full.worstLegIndex + 1),
            RouteMapOverlay::sailingConditionText(full.worstCategory), full.worstSeverity));
    }
  }
  for (int column : {4, 6, 9, 12, 2}) {
    wxListEvent sort(wxEVT_LIST_COL_CLICK, m_List->GetId());
    sort.SetColumn(column); m_List->GetEventHandler()->ProcessEvent(sort);
  }
  if (lifecycle && !m_Entries.empty()) {
    // Dedicated headless fixture only: prove that a dialog holding immutable
    // snapshots never dereferences its removed owner.
    auto* owner = m_Entries.front().segments.front().owner;
    m_Routing.DeleteRouteMaps({owner});
    Preview(0);
    passed = passed && !m_Slider->IsEnabled() &&
        m_Summary->GetLabel().Contains("Results changed");
  }
  wxLogMessage("WR_COMFORT_UI_CONTRACT passed=%d rows=%lu lifecycle=%d initial_wind_only=%d",
      passed ? 1 : 0, static_cast<unsigned long>(m_Entries.size()), lifecycle ? 1 : 0,
      initialWindOnly ? 1 : 0);
  wxLog::FlushActive();
  if (!passed) std::_Exit(4);
}

void RouteComparisonDialog::Collect() {
  auto selected = m_Routing.CurrentRouteMaps();
  if (selected.empty()) return;
  const auto anchor = selected.front()->GetConfiguration();
  const auto addItinerary = [&](const std::vector<RouteMapOverlay*>& routes) {
    Entry entry;
    std::vector<weather_routing::wr::RouteLeg> legs;
    for (auto* owner : routes) {
      const auto candidates = owner->RetainedCandidates();
      if (candidates.empty()) return;
      const auto chosen = owner->SelectedRetainedCandidateId();
      const auto it = std::find_if(candidates.begin(), candidates.end(),
          [&](const auto& c) { return c.id == chosen; });
      if (it == candidates.end()) return;
      const auto& next = it->result->legs;
      if (!legs.empty()) {
        if (next.front().startTime < legs.back().endTime) return;
        if (next.front().startTime > legs.back().endTime) {
          weather_routing::wr::RouteLeg gap;
          gap.start = gap.end = legs.back().end;
          gap.startTime = legs.back().endTime; gap.endTime = next.front().startTime;
          gap.wind.eastKnots = std::numeric_limits<double>::quiet_NaN();
          legs.push_back(gap); // Unmodelled stop is unknown, never calm.
        }
      }
      legs.insert(legs.end(), next.begin(), next.end());
      entry.segments.push_back({owner, owner->ModernProgressGeneration(), *it});
      if (!entry.metric.id.empty()) entry.metric.id += "+";
      entry.metric.id += it->id;
    }
    if (legs.empty()) return;
    entry.fullComfort = weather_routing::CalculateRouteComfort(legs);
    entry.windComfort = weather_routing::CalculateRouteComfort(legs, true);
    entry.metric.comfort = entry.fullComfort;
    entry.metric.elapsedSeconds = (legs.back().endTime - legs.front().startTime).count();
    entry.departure = WxTime(legs.front().startTime); entry.eta = WxTime(legs.back().endTime);
    entry.engine = _("Complete itinerary");
    m_Entries.push_back(std::move(entry));
  };
  if (anchor.IsMultiLegGenerated) {
    bool family = false;
    for (const auto& itinerary : m_Routing.m_MultiLegOptimizationCandidates) {
      if (!itinerary.complete || itinerary.routes.empty()) continue;
      bool belongs = false;
      for (auto* owner : itinerary.routes)
        if (std::find(selected.begin(), selected.end(), owner) != selected.end()) belongs = true;
      if (belongs) { family = true; break; }
    }
    if (family) {
      for (const auto& itinerary : m_Routing.m_MultiLegOptimizationCandidates)
        if (itinerary.complete) addItinerary(itinerary.routes);
    } else {
      const auto routes = m_Routing.GetMultiLegGroupRoutes(anchor.MultiLegGroupId);
      addItinerary(routes);
    }
    return;
  }
  const auto add = [&](RouteMapOverlay* owner) {
    for (const auto& candidate : owner->RetainedCandidates()) {
      Entry entry;
      entry.segments.push_back({owner, owner->ModernProgressGeneration(), candidate});
      entry.metric = {candidate.id, candidate.result->metrics.elapsed.count(), candidate.comfort};
      entry.fullComfort = candidate.comfort;
      entry.windComfort = candidate.windOnlyComfort;
      entry.engine = EngineTitle(candidate.result->engineIdentity);
      entry.departure = WxTime(candidate.result->legs.front().startTime);
      entry.eta = WxTime(candidate.result->legs.back().endTime);
      m_Entries.push_back(std::move(entry));
    }
  };
  // A departure family is generated from one voyage configuration. Never mix
  // unrelated voyages merely because they happen to be in the main table.
  if (!anchor.DepartureTimeOptimizationGroupId.empty()) {
    for (auto* route : m_Routing.m_WeatherRoutes)
      if (route->routemapoverlay->GetConfiguration().DepartureTimeOptimizationGroupId ==
          anchor.DepartureTimeOptimizationGroupId) add(route->routemapoverlay);
  } else {
    for (auto* owner : selected) {
      const auto c = owner->GetConfiguration();
      if (c.StartLat == anchor.StartLat && c.StartLon == anchor.StartLon &&
          c.EndLat == anchor.EndLat && c.EndLon == anchor.EndLon &&
          c.boatFileName == anchor.boatFileName && c.TimeMode == anchor.TimeMode)
        add(owner);
    }
  }
}

void RouteComparisonDialog::Populate() {
  m_Updating = true;
  for (auto& entry : m_Entries)
    entry.metric.comfort = m_WindOnly->GetValue() ? entry.windComfort : entry.fullComfort;
  std::vector<std::size_t> order(m_Entries.size());
  std::iota(order.begin(), order.end(), 0);
  const auto number = [&](const Entry& e) -> double {
    const auto& c = e.metric.comfort;
    switch (m_SortColumn) {
      case 2: return e.departure.GetTicks();
      case 3: return e.eta.GetTicks();
      case 6: return c.comparable() ? c.exposureHours : std::numeric_limits<double>::infinity();
      case 7: return c.comparable() ? c.averageDiscomfort : std::numeric_limits<double>::infinity();
      case 8: return c.categorySeconds[3];
      case 9: return e.fullComfort.worstSeverity;
      case 10: return c.longestDifficultSeconds;
      case 11: return e.fullComfort.durationSeconds > 0 ?
          double(e.fullComfort.waveCoveredSeconds) / e.fullComfort.durationSeconds : 0;
      case 12: return e.fullComfort.durationSeconds - e.fullComfort.waveCoveredSeconds;
      default: return e.metric.elapsedSeconds;
    }
  };
  std::stable_sort(order.begin(), order.end(), [&](auto a, auto b) {
    if (m_SortColumn == 0 || m_SortColumn == 1) {
      const auto x = m_SortColumn == 0 ? wxString::FromUTF8(m_Entries[a].metric.id) : m_Entries[a].engine;
      const auto y = m_SortColumn == 0 ? wxString::FromUTF8(m_Entries[b].metric.id) : m_Entries[b].engine;
      return m_Ascending ? x < y : x > y;
    }
    const double x = number(m_Entries[a]), y = number(m_Entries[b]);
    if (x != y) return m_Ascending ? x < y : x > y;
    if (m_SortColumn == 4 || m_SortColumn == 5) {
      const auto& cx = m_Entries[a].metric.comfort;
      const auto& cy = m_Entries[b].metric.comfort;
      if (cx.comparable() != cy.comparable()) return cx.comparable();
      if (cx.exposureHours != cy.exposureHours) return cx.exposureHours < cy.exposureHours;
    }
    return m_Entries[a].metric.elapsedSeconds < m_Entries[b].metric.elapsedSeconds;
  });
  std::int64_t fastest = std::numeric_limits<std::int64_t>::max();
  for (const auto& e : m_Entries) fastest = std::min(fastest, e.metric.elapsedSeconds);
  m_List->Freeze(); m_List->DeleteAllItems();
  for (auto index : order) {
    const auto& e = m_Entries[index]; const auto& c = e.metric.comfort;
    const long row = m_List->InsertItem(m_List->GetItemCount(), wxString::FromUTF8(e.metric.id));
    m_List->SetItemData(row, index);
    m_List->SetItem(row, 1, e.engine);
    m_List->SetItem(row, 2, UtcText(e.departure));
    m_List->SetItem(row, 3, UtcText(e.eta));
    m_List->SetItem(row, 4, TableDuration(e.metric.elapsedSeconds));
    m_List->SetItem(row, 5, TableDuration(e.metric.elapsedSeconds - fastest));
    m_List->SetItem(row, 6, c.comparable() ? wxString::Format("%.3f", c.exposureHours) : _("Incomplete"));
    m_List->SetItem(row, 7, c.comparable() ? wxString::Format("%.3f", c.averageDiscomfort) : _("Incomplete"));
    m_List->SetItem(row, 8, c.comparable() ? TableDuration(c.categorySeconds[3]) : _("Incomplete"));
    m_List->SetItem(row, 9, WorstKnownText(e.fullComfort));
    m_List->SetItem(row, 10, c.comparable() ? TableDuration(c.longestDifficultSeconds) : _("Incomplete"));
    m_List->SetItem(row, 11, wxString::Format("%.0f%%", e.fullComfort.durationSeconds > 0 ?
        100.0 * e.fullComfort.waveCoveredSeconds / e.fullComfort.durationSeconds : 0.0));
    m_List->SetItem(row, 12, TableDuration(e.fullComfort.durationSeconds - e.fullComfort.waveCoveredSeconds));
    m_List->SetItem(row, 13, _("Pass"));
    if (e.metric.id == m_PreviewId) m_List->SetItemState(row, wxLIST_STATE_SELECTED, wxLIST_STATE_SELECTED);
  }
  for (int column = 0; column < m_List->GetColumnCount(); ++column) {
    wxListItem heading; heading.SetMask(wxLIST_MASK_TEXT); m_List->GetColumn(column, heading);
    int width = m_List->GetTextExtent(heading.GetText()).x;
    for (long row = 0; row < m_List->GetItemCount(); ++row)
      width = std::max(width, m_List->GetTextExtent(m_List->GetItemText(row, column)).x);
    m_List->SetColumnWidth(column, std::min(column == 0 ? 160 : 260, width + 24));
  }
  m_List->Thaw(); m_Updating = false;
}

void RouteComparisonDialog::Choose() {
  if (m_Entries.empty()) {
    m_Summary->SetLabel(_("No completed validated candidates. Compute All, or select completed departure results."));
    m_Slider->Disable(); return;
  }
  std::vector<weather_routing::RouteComparisonMetric> metrics;
  for (const auto& entry : m_Entries) metrics.push_back(entry.metric);
  const auto selection = weather_routing::SelectComfortCandidate(metrics, m_Slider->GetValue());
  m_Slider->Enable(selection.tradeOffAvailable);
  Preview(selection.selected);
}

bool RouteComparisonDialog::Current(const Segment& segment) const {
  bool alive = false;
  for (auto* route : m_Routing.m_WeatherRoutes)
    if (route->routemapoverlay == segment.owner) { alive = true; break; }
  if (!alive) return false;
  if (segment.owner->Running() || segment.owner->ModernProgressGeneration() != segment.generation)
    return false;
  for (const auto& c : segment.owner->RetainedCandidates())
    if (c.id == segment.candidate.id) return true;
  return false;
}

void RouteComparisonDialog::Preview(std::size_t index) {
  if (index >= m_Entries.size()) return;
  const auto& entry = m_Entries[index];
  for (const auto& segment : entry.segments)
    if (!Current(segment)) {
      m_Summary->SetLabel(_("Results changed or were removed. Close and reopen comparison after computation finishes."));
      m_Slider->Disable(); return;
    }
  const auto started = std::chrono::steady_clock::now();
  if (m_PreviewId != entry.metric.id) {
    m_Routing.m_positionOnRoute = nullptr;
    for (const auto& segment : entry.segments) {
      if (!segment.owner->SelectRetainedCandidate(segment.candidate.id)) return;
      m_Routing.UpdateRouteMap(segment.owner);
    }
    m_PreviewId = entry.metric.id;
    m_Routing.m_bSkipUpdateCurrentItems = true;
    auto* list = m_Routing.m_panel->m_lWeatherRoutes;
    for (long row = 0; row < list->GetItemCount(); ++row) {
      auto* route = reinterpret_cast<WeatherRoute*>(wxUIntToPtr(list->GetItemData(row)));
      const bool selected = std::any_of(entry.segments.begin(), entry.segments.end(),
          [&](const Segment& s) { return s.owner == route->routemapoverlay; });
      list->SetItemState(row, selected ? wxLIST_STATE_SELECTED : 0, wxLIST_STATE_SELECTED);
    }
    m_Routing.m_bSkipUpdateCurrentItems = false;
    m_Routing.OnWeatherRouteSelected();
    RequestRefresh(m_Routing.m_weather_routing_pi.GetParentWindow());
    wxLogMessage("WR_COMFORT_PREVIEW id=%s refresh_ms=%.3f search_restarted=0",
        wxString::FromUTF8(entry.metric.id), std::chrono::duration<double, std::milli>(
            std::chrono::steady_clock::now() - started).count());
  }
  const auto& c = entry.metric.comfort;
  std::vector<weather_routing::RouteComparisonMetric> metrics;
  for (const auto& e : m_Entries) metrics.push_back(e.metric);
  const auto selection = weather_routing::SelectComfortCandidate(metrics, m_Slider->GetValue());
  wxString text = wxString::Format(_("%lu validated candidates. Allow up to %s extra. Selected %s (%s): passage %s.\n"),
      static_cast<unsigned long>(m_Entries.size()), DurationText(selection.allowanceSeconds),
      wxString::FromUTF8(entry.metric.id), entry.engine, DurationText(entry.metric.elapsedSeconds));
  const auto& full = entry.fullComfort;
  text += wxString::Format(_("Wave coverage: %.0f%%. Missing waves: %s.\n"),
      full.durationSeconds > 0 ? 100.0 * full.waveCoveredSeconds / full.durationSeconds : 0.0,
      DurationText(full.durationSeconds - full.waveCoveredSeconds));
  if (c.comparable()) text += wxString::Format(_("Exposure %.3f; average %.3f. Good %s; Bumpy %s; Difficult %s.\n"),
      c.exposureHours, c.averageDiscomfort, DurationText(c.categorySeconds[1]),
      DurationText(c.categorySeconds[2]), DurationText(c.categorySeconds[3]));
  else if (!m_WindOnly->GetValue() && entry.windComfort.comparable()) {
    text += _("Wind + wave comfort is incomplete: wave height is required at every route-leg sample. "
              "Tick Rank by wind comfort only to compare without waves.\n");
  } else text += _("Comfort comparison incomplete: wind conditions are missing for part of this route.\n");
  if (full.worstCategory > 0) text += wxString::Format(
      _("Worst known leg, including available waves: #%lu, %s, severity %.3f, %s UTC for %s, %.4f / %.4f.\n"),
      static_cast<unsigned long>(full.worstLegIndex + 1), RouteMapOverlay::sailingConditionText(full.worstCategory),
      full.worstSeverity, UtcText(WxTime(full.worstLegStartTime)),
      DurationText((full.worstLegEndTime - full.worstLegStartTime).count()),
      full.worstLegStart.latitude, full.worstLegStart.longitude);
  else text += _("Worst known leg, including available waves: unknown; no leg has both wind and wave-height data.\n");
  if (!selection.comfortAvailable)
    text += _("Comfort slider unavailable until comparable condition data are available.");
  else if (!selection.tradeOffAvailable)
    text += _("No fastest / comfort trade-off available among comparable candidates.");
  if (m_WindOnly->GetValue()) text += _(" Wind-only ranking: waves excluded from ranking; worst known leg still includes available waves.");
  m_Summary->SetLabel(text); m_Summary->Wrap(std::max(300, GetClientSize().x - 24));
  m_Updating = true;
  for (long row = 0; row < m_List->GetItemCount(); ++row) {
    const bool selected = m_List->GetItemData(row) == index;
    m_List->SetItemState(row, selected ? wxLIST_STATE_SELECTED : 0, wxLIST_STATE_SELECTED);
    if (selected) m_List->EnsureVisible(row);
  }
  m_Updating = false; Layout();
}

void WeatherRouting::ShowRouteComparison() {
  auto* dialog = new RouteComparisonDialog(*this);
  dialog->Show();
}
