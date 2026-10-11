// Opt-in integration checks run inside a disposable genuine OpenCPN host.
// They exercise the real list event, renderer, reset and computation queues.
#include <wx/wx.h>
#include "RouteMapOverlay.h"
#include "WeatherRouting.h"
#include "RoutingTablePanel.h"
#include "pidc.h"

#include <wx/dcmemory.h>
#include <wx/image.h>
#include <wx/log.h>
#include <wx/utils.h>

bool WeatherRouting::RunDepartureChartContract(RouteMapOverlay* route) {
  if (!route || !route->Finished() || !route->ReachedDestination()) return false;
  bool passed = true;
  const auto check = [&](bool condition, const char* name) {
    wxLogMessage("WR_DEPARTURE_DISPLAY check=%s passed=%d", name, condition);
    passed &= condition;
  };
  const auto original = route->GetConfiguration();
  const auto end = route->EndTime();
  const auto plotCount = route->GetPlotData().size();
  const wxString id = "departure-display-host-contract";
  Show(true);
  auto base = original;
  base.DepartureTimeOptimizationEnabled = true;
  base.DepartureTimeOptimizationChartDisplay =
      RouteMapConfiguration::SELECTED_DEPARTURE;
  if (!AddConfiguration(base)) return false;
  auto* controller = m_WeatherRoutes.back()->routemapoverlay;
  auto candidate = original;
  candidate.DepartureTimeOptimizationCandidate = true;
  candidate.DepartureTimeOptimizationEnabled = false;
  candidate.DepartureTimeOptimizationGroupId = id;
  candidate.DepartureTimeOptimizationNominalStartTime =
      original.StartTime - wxTimeSpan::Minutes(30);
  candidate.DepartureTimeOptimizationOffsetMinutes = 30;
  route->SetConfigurationPreserveResult(candidate);
  candidate.DepartureTimeOptimizationOffsetMinutes = 0;
  candidate.StartTime = candidate.DepartureTimeOptimizationNominalStartTime;
  if (!AddConfiguration(candidate)) return false;
  auto* nominal = m_WeatherRoutes.back()->routemapoverlay;
  candidate.DepartureTimeOptimizationOffsetMinutes = -30;
  candidate.StartTime -= wxTimeSpan::Minutes(30);
  if (!AddConfiguration(candidate)) return false;
  auto* earlier = m_WeatherRoutes.back()->routemapoverlay;
  InitializeDepartureChartGroup(id, {controller},
                               RouteMapConfiguration::SELECTED_DEPARTURE);
  check(nominal->m_bEndRouteVisible && !earlier->m_bEndRouteVisible &&
            !route->m_bEndRouteVisible, "default_shows_nominal_only");
  UpdateDepartureChartCompletion();
  check(route->m_bEndRouteVisible && !nominal->m_bEndRouteVisible &&
            SelectedDepartureOnChart(nominal) == route,
        "completion_selects_fastest_usable_candidate");

  auto* list = m_panel->m_lWeatherRoutes;
  const auto selectRow = [&](RouteMapOverlay* selected) {
    for (long row = 0; row < list->GetItemCount(); ++row) {
      auto* item = reinterpret_cast<WeatherRoute*>(
          wxUIntToPtr(list->GetItemData(row)));
      list->SetItemState(row, item->routemapoverlay == selected
          ? wxLIST_STATE_SELECTED : 0, wxLIST_STATE_SELECTED);
    }
  };
  selectRow(earlier);
  check(earlier->m_bEndRouteVisible && !route->m_bEndRouteVisible &&
            !nominal->m_bEndRouteVisible,
        "actual_main_list_selection_changes_selected_departure");
  check(controller->m_bEndRouteVisible,
        "unrelated_visibility_unchanged");

  // Exercise the real Configuration choice event, not just a model setter.
  auto* choice = wxDynamicCast(wxWindow::FindWindowByName(
      "DepartureChartDisplay", &m_ConfigurationDialog), wxChoice);
  if (!choice) return false;
  choice->SetSelection(RouteMapConfiguration::ALL_DEPARTURES);
  // Move a named start after computing. Presentation must keep the result's
  // original coordinates rather than re-resolving an updated live position.
  RouteMapPosition* movedStart = nullptr;
  double startLatitude = 0;
  for (auto& position : RouteMap::Positions)
    if (position.Name == original.Start) {
      movedStart = &position;
      startLatitude = position.lat;
      position.lat += 0.5;
      break;
    }
  wxCommandEvent all(wxEVT_CHOICE, choice->GetId());
  all.SetEventObject(choice);
  choice->GetEventHandler()->ProcessEvent(all);
  check(movedStart && route->GetConfiguration().StartLat == original.StartLat,
        "display_change_does_not_refresh_live_start_coordinates");
  if (movedStart) movedStart->lat = startLatitude;
  check(route->m_bEndRouteVisible && nominal->m_bEndRouteVisible &&
            earlier->m_bEndRouteVisible,
        "configuration_choice_shows_all_departures");
  SetRouteVisibility(nominal, false);
  check(route->GetConfiguration().DepartureTimeOptimizationChartDisplay ==
            RouteMapConfiguration::MANUAL_DEPARTURES &&
        choice->GetSelection() == RouteMapConfiguration::MANUAL_DEPARTURES,
        "eye_choice_switches_group_and_control_to_manual");
  selectRow(route);
  OnWeatherRouteSelected();
  UpdateDepartureChartCompletion();
  check(!nominal->m_bEndRouteVisible && earlier->m_bEndRouteVisible &&
            route->m_bEndRouteVisible,
        "manual_subset_survives_selection_refresh_and_completion");
  check(controller->GetConfiguration().DepartureTimeOptimizationChartDisplay ==
            RouteMapConfiguration::MANUAL_DEPARTURES,
        "controller_remembers_mode_for_next_run");
  check(route->Finished() && route->ReachedDestination() &&
            route->EndTime() == end && route->GetPlotData().size() == plotCount,
        "mode_and_selection_changes_preserve_computed_results");

  InitializeDepartureChartGroup(id, {controller},
                               RouteMapConfiguration::SELECTED_DEPARTURE);
  selectRow(earlier);
  UpdateDepartureChartCompletion();
  check(earlier->m_bEndRouteVisible && !route->m_bEndRouteVisible,
        "completion_does_not_replace_user_inspected_departure");
  InitializeDepartureChartGroup(id, {controller},
      RouteMapConfiguration::MANUAL_DEPARTURES, {{-30, true}, {0, false},
                                               {30, true}});
  check(earlier->m_bEndRouteVisible && !nominal->m_bEndRouteVisible &&
            route->m_bEndRouteVisible,
        "repeat_manual_group_restores_matching_departure_choices");

  candidate.DepartureTimeOptimizationOffsetMinutes = 0;
  candidate.IsMultiLegGenerated = true;
  candidate.MultiLegGroupId = "host-contract-chain";
  candidate.MultiLegLegIndex = 1;
  candidate.MultiLegLegCount = 2;
  if (!AddConfiguration(candidate)) return false;
  auto* secondLeg = m_WeatherRoutes.back()->routemapoverlay;
  SetDepartureChartDisplay(nominal, RouteMapConfiguration::SELECTED_DEPARTURE);
  SelectDepartureOnChart(nominal);
  check(nominal->m_bEndRouteVisible && secondLeg->m_bEndRouteVisible &&
            !earlier->m_bEndRouteVisible && !route->m_bEndRouteVisible,
        "selected_departure_shows_all_legs_of_that_candidate");

  route->SetConfigurationPreserveResult(original);
  DeleteRouteMaps({controller, nominal, earlier, secondLeg});
  check(m_DepartureChartGroups.count(id) == 0,
        "deleted_group_releases_presentation_references");
  SetRouteVisibility(route, true, false);
  selectRow(route);
  wxLogMessage("WR_DEPARTURE_DISPLAY result=%s", passed ? "passed" : "failed");
  return passed;
}

bool WeatherRouting::RunRouteLifecycleContract(RouteMapOverlay* route) {
  bool passed = true;
  const auto check = [&](bool condition, const char* name) {
    wxLogMessage("WR_ROUTE_LIFECYCLE check=%s passed=%d", name, condition);
    passed &= condition;
  };
  if (!route) return false;
  check(route->Finished() && route->ReachedDestination(), "completed_fixture");
  if (!passed) return false;
  const auto original = route->GetConfiguration();
  Show(true);
  SetSize(wxSize(1200, 650));
  Layout();
  wxYield();
  auto* list = m_panel->m_lWeatherRoutes;
  long selectedRow = -1;
  for (long row = 0; row < list->GetItemCount(); ++row) {
    auto* item =
        reinterpret_cast<WeatherRoute*>(wxUIntToPtr(list->GetItemData(row)));
    const bool selected = item->routemapoverlay == route;
    list->SetItemState(row, selected ? wxLIST_STATE_SELECTED : 0,
                       wxLIST_STATE_SELECTED);
    if (selected) selectedRow = row;
  }
  if (selectedRow < 0) return false;
  list->EnsureVisible(selectedRow);
  OnWeatherRouteSelected();

  // Measure actual pixels, so a successful flag change cannot mask a
  // selection-driven redraw which still paints the hidden route.
  PlugIn_ViewPort vp{};
  vp.clat = (original.StartLat + original.EndLat) / 2;
  vp.clon = (original.StartLon + original.EndLon) / 2;
  vp.view_scale_ppm = 0.01;
  vp.chart_scale = 1000000;
  vp.pix_width = 640;
  vp.pix_height = 480;
  vp.rv_rect = wxRect(0, 0, 640, 480);
  vp.m_projection_type = 1;  // Mercator
  vp.lat_min = vp.clat - 1;
  vp.lat_max = vp.clat + 1;
  vp.lon_min = vp.clon - 1;
  vp.lon_max = vp.clon + 1;
  vp.bValid = true;
  const auto paintedPixels = [&]() {
    wxBitmap bitmap(640, 480, 24);
    wxMemoryDC memory(bitmap);
    memory.SetBackground(*wxWHITE_BRUSH);
    memory.Clear();
    piDC dc(memory);
    Render(dc, vp);
    memory.SelectObject(wxNullBitmap);
    const wxImage image = bitmap.ConvertToImage();
    const unsigned char* data = image.GetData();
    unsigned int painted = 0;
    for (int i = 0; i < 640 * 480; ++i)
      if (data[3 * i] != 255 || data[3 * i + 1] != 255 ||
          data[3 * i + 2] != 255)
        ++painted;
    return painted;
  };
  SetRouteVisibility(route, true);
  check(paintedPixels() > 0, "visible_selected_route_paints");
  wxRect bounds;
  list->GetItemRect(selectedRow, bounds);
  wxMouseEvent click(wxEVT_LEFT_DOWN);
  click.SetPosition(wxPoint(14, bounds.y + bounds.height / 2));
  // Generic GTK hit testing distinguishes the actual icon/text from blank
  // cell padding. Find a point on the first-column item, as a user would.
  for (int y = 0; y < list->GetClientSize().y; ++y) {
    bool found = false;
    for (int x = 0; x < list->GetColumnWidth(columns[VISIBLE]); ++x) {
      int flags = 0;
      long column = -1;
      if (list->HitTest(wxPoint(x, y), flags, &column) == selectedRow &&
          column == columns[VISIBLE]) {
        click.SetPosition(wxPoint(x, y));
        found = true;
        break;
      }
    }
    if (found) break;
  }
  int hitFlags = 0;
  wxLogMessage(
      "WR_ROUTE_LIFECYCLE eye_geometry row=%ld bounds=%d,%d,%d,%d hit=%ld "
      "visible_column=%d width=%d shown=%d",
      selectedRow, bounds.x, bounds.y, bounds.width, bounds.height,
      list->HitTest(click.GetPosition(), hitFlags), columns[VISIBLE],
      columns[VISIBLE] >= 0 ? list->GetColumnWidth(columns[VISIBLE]) : -1,
      list->IsShownOnScreen());
  list->GetEventHandler()->ProcessEvent(click);
  check(!route->m_bEndRouteVisible, "eye_event_hides_selected_route");
  check(FirstCurrentRouteMap() == route, "eye_keeps_selection_for_inspection");
  check(paintedPixels() == 0, "hidden_selected_route_paints_nothing");
  list->GetEventHandler()->ProcessEvent(click);
  check(route->m_bEndRouteVisible && paintedPixels() > 0,
        "eye_event_reversibly_restores_route");
  SetRouteVisibility(route, false);

  auto otherConfiguration = original;
  otherConfiguration.EndLon += 0.5;
  wxString pass;
  wxGetEnv("WR_HEADLESS_LIFECYCLE_CONTRACT", &pass);
  otherConfiguration.End = "Lifecycle second route " + pass;
  AddPosition(otherConfiguration.EndLat, otherConfiguration.EndLon,
              otherConfiguration.End);
  if (!AddConfiguration(otherConfiguration)) return false;
  RouteMapOverlay* other = m_WeatherRoutes.back()->routemapoverlay;
  Start(other);
  check(RouteComputationActive(other), "second_route_queued");
  check(!CanClearComputedResults({other}), "queued_results_cannot_be_cleared");
  m_PreparingChartSafetyRoutes.insert(route);
  check(!CanClearComputedResults({route}),
        "preparing_results_cannot_be_cleared");
  m_PreparingChartSafetyRoutes.erase(route);
  check(CanClearComputedResults({route}),
        "finished_route_clearable_during_other_job");

  if (m_RoutingTablePanel) m_RoutingTablePanel->SetRouteMap(route);
  wxCommandEvent clear(wxEVT_MENU, m_mClearResults->GetId());
  m_mConfiguration->ProcessEvent(clear);
  check(!route->HasComputedResults() && !route->Finished() &&
            !route->ReachedDestination() &&
            route->GetDestination() == nullptr && !route->EndTime().IsValid() &&
            route->GetPlotData().empty() && route->RetainedCandidates().empty(),
        "all_computed_result_data_released");
  check(!route->m_bEndRouteVisible, "clearing_preserves_visibility_choice");
  check(RouteComputationActive(other), "clearing_does_not_cancel_other_job");
  const auto after = route->GetConfiguration();
  check(after.Start == original.Start && after.End == original.End &&
            after.StartTime == original.StartTime &&
            after.StartLat == original.StartLat &&
            after.EndLon == original.EndLon &&
            after.boatFileName == original.boatFileName &&
            after.DeltaTime == original.DeltaTime &&
            after.EngineSettings.EngineId() ==
                original.EngineSettings.EngineId() &&
            after.DetectLand == original.DetectLand &&
            after.UseGrib == original.UseGrib,
        "route_settings_preserved");
  DeleteRouteMaps({other});
  --m_RoutesToRun;
  if (m_RoutingTablePanel)
    check(m_RoutingTablePanel->GetRouteMap() != route ||
              route->GetPlotData().empty(),
          "result_table_has_no_stale_data");
  check(paintedPixels() == 0, "cleared_route_paints_nothing");

  // Failed preflight and stopped calculations must also be clearable.
  route->SetError("Lifecycle synthetic preflight failure");
  check(CanClearComputedResults({route}) && ClearComputedResults({route}) &&
            !route->HasComputedResults(),
        "failed_preflight_results_clearable");
  wxLogMessage("WR_ROUTE_LIFECYCLE result=%s", passed ? "passed" : "failed");
  return passed;
}
