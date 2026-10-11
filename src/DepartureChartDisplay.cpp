// Departure optimisation presentation is independent of computation/results.
#include <wx/wx.h>
#include "RouteMapOverlay.h"
#include "WeatherRouting.h"
#include <algorithm>
#include <limits>

wxDEFINE_EVENT(wxEVT_WR_DEPARTURE_DISPLAY_CHANGED, wxCommandEvent);

void WeatherRouting::NotifyDepartureChartDisplay(const wxString& id) {
  m_ConfigurationDialog.RefreshDepartureChartDisplayControls();
  wxCommandEvent event(wxEVT_WR_DEPARTURE_DISPLAY_CHANGED);
  event.SetString(id);
  ProcessWindowEvent(event);
#ifdef __OCPN__ANDROID__
  RefreshAndroidWorkspace();
#endif
}

void WeatherRouting::ApplyDepartureChartDisplay(const wxString& id) {
  const auto found = m_DepartureChartGroups.find(id);
  if (found == m_DepartureChartGroups.end()) return;
  const auto& state = found->second;
  for (auto* item : m_WeatherRoutes) {
    auto* route = item->routemapoverlay;
    auto configuration = route->GetConfiguration();
    if (!configuration.DepartureTimeOptimizationCandidate ||
        configuration.DepartureTimeOptimizationGroupId != id) continue;
    if (configuration.DepartureTimeOptimizationChartDisplay != state.mode) {
      route->SetDepartureChartDisplayMode(state.mode);
    }
    if (state.mode != RouteMapConfiguration::MANUAL_DEPARTURES)
      SetRouteVisibility(
          route, state.mode == RouteMapConfiguration::ALL_DEPARTURES ||
                     configuration.DepartureTimeOptimizationOffsetMinutes ==
                         state.selectedOffset,
          false);
  }
  for (auto* route : state.controllers) {
    if (!RouteMapIsManaged(route)) continue;
    auto configuration = route->GetConfiguration();
    if (configuration.DepartureTimeOptimizationChartDisplay != state.mode) {
      route->SetDepartureChartDisplayMode(state.mode);
    }
  }
}

void WeatherRouting::InitializeDepartureChartGroup(
    const wxString& id, const std::vector<RouteMapOverlay*>& controllers,
    int mode, const std::map<int, bool>& previousVisibility) {
  auto& state = m_DepartureChartGroups[id];
  state = DepartureChartGroup();
  state.controllers = controllers;
  state.mode = std::clamp(mode, 0, 2);
  if (state.mode == RouteMapConfiguration::MANUAL_DEPARTURES) {
    for (auto* item : m_WeatherRoutes) {
      const auto configuration = item->routemapoverlay->GetConfiguration();
      if (!configuration.DepartureTimeOptimizationCandidate ||
          configuration.DepartureTimeOptimizationGroupId != id) continue;
      const auto previous = previousVisibility.find(
          configuration.DepartureTimeOptimizationOffsetMinutes);
      // A new Manual group starts with the nominal departure visible.
      const bool visible = previousVisibility.empty()
          ? configuration.DepartureTimeOptimizationOffsetMinutes == 0
          : previous != previousVisibility.end() && previous->second;
      SetRouteVisibility(item->routemapoverlay, visible, false);
    }
  }
  ApplyDepartureChartDisplay(id);
  RouteMapOverlay* anchor = nullptr;
  for (auto* item : m_WeatherRoutes)
    if (item->routemapoverlay->GetConfiguration()
            .DepartureTimeOptimizationGroupId == id) {
      anchor = item->routemapoverlay;
      break;
    }
  SelectDepartureOnChart(SelectedDepartureOnChart(anchor), false);
}

void WeatherRouting::SetDepartureChartDisplay(RouteMapOverlay* route, int mode) {
  if (!RouteMapIsManaged(route) || mode < 0 || mode > 2) return;
  auto configuration = route->GetConfiguration();
  wxString id = configuration.DepartureTimeOptimizationGroupId;
  if (!configuration.DepartureTimeOptimizationCandidate) {
    id.Clear();
    for (const auto& group : m_DepartureChartGroups)
      if (std::find(group.second.controllers.begin(),
                    group.second.controllers.end(), route) !=
          group.second.controllers.end()) id = group.first;
  }
  auto group = m_DepartureChartGroups.find(id);
  if (group != m_DepartureChartGroups.end()) {
    group->second.mode = mode;
    group->second.userInteracted = true;
    ApplyDepartureChartDisplay(id);
  } else {
    route->SetDepartureChartDisplayMode(mode);
  }
  SaveLastUsedConfigurationDefaults(route->GetConfiguration());
  m_tAutoSaveXML.Start(5000, true);
  NotifyDepartureChartDisplay(id);
}

RouteMapOverlay* WeatherRouting::SelectedDepartureOnChart(
    RouteMapOverlay* route) const {
  if (!RouteMapIsManaged(route)) return nullptr;
  const auto configuration = route->GetConfiguration();
  const auto group = m_DepartureChartGroups.find(
      configuration.DepartureTimeOptimizationGroupId);
  if (group == m_DepartureChartGroups.end()) return nullptr;
  for (auto* item : m_WeatherRoutes) {
    const auto candidate = item->routemapoverlay->GetConfiguration();
    if (candidate.DepartureTimeOptimizationCandidate &&
        candidate.DepartureTimeOptimizationGroupId == group->first &&
        candidate.DepartureTimeOptimizationOffsetMinutes ==
            group->second.selectedOffset) return item->routemapoverlay;
  }
  return nullptr;
}

void WeatherRouting::SelectDepartureOnChart(RouteMapOverlay* route,
                                           bool userChoice,
                                           bool selectMainList) {
  if (!RouteMapIsManaged(route) || m_UpdatingDepartureSelection) return;
  const auto configuration = route->GetConfiguration();
  if (!configuration.DepartureTimeOptimizationCandidate) return;
  const auto found = m_DepartureChartGroups.find(
      configuration.DepartureTimeOptimizationGroupId);
  if (found == m_DepartureChartGroups.end()) return;
  auto& state = found->second;
  state.selectedOffset = configuration.DepartureTimeOptimizationOffsetMinutes;
  state.userInteracted |= userChoice;
  ApplyDepartureChartDisplay(found->first);
  if (selectMainList) {
    std::vector<RouteMapOverlay*> selected;
    for (auto* item : m_WeatherRoutes) {
      const auto candidate = item->routemapoverlay->GetConfiguration();
      if (candidate.DepartureTimeOptimizationCandidate &&
          candidate.DepartureTimeOptimizationGroupId == found->first &&
          candidate.DepartureTimeOptimizationOffsetMinutes == state.selectedOffset)
        selected.push_back(item->routemapoverlay);
    }
    m_UpdatingDepartureSelection = true;
    SelectWeatherRoutesForStability(selected);
    m_UpdatingDepartureSelection = false;
  }
  NotifyDepartureChartDisplay(found->first);
}

void WeatherRouting::UpdateDepartureChartCompletion() {
  for (auto& group : m_DepartureChartGroups) {
    auto& state = group.second;
    if (state.completed) continue;
    if (group.first == m_ActiveMultiLegOptimizationId &&
        m_ActiveMultiLegDepartureOptimization) continue;
    struct Candidate {
      RouteMapOverlay* first = nullptr;
      bool complete = true;
      long seconds = 0;
      size_t order = 0;
    };
    std::map<int, Candidate> candidates;
    bool pending = false;
    for (auto* item : m_WeatherRoutes) {
      auto* route = item->routemapoverlay;
      const auto configuration = route->GetConfiguration();
      if (!configuration.DepartureTimeOptimizationCandidate ||
          configuration.DepartureTimeOptimizationGroupId != group.first) continue;
      pending |= RouteComputationActive(route);
      auto& candidate = candidates[
          configuration.DepartureTimeOptimizationOffsetMinutes];
      if (!candidate.first) {
        candidate.first = route;
        candidate.order = candidates.size();
      }
      candidate.complete &= route->Finished() && route->ReachedDestination();
      if (route->Finished() && route->ReachedDestination())
        candidate.seconds += (route->EndTime() - configuration.StartTime)
                                 .GetSeconds().ToLong();
    }
    if (pending || candidates.empty()) continue;
    state.completed = true;
    if (state.mode != RouteMapConfiguration::SELECTED_DEPARTURE ||
        state.userInteracted) continue;
    RouteMapOverlay* best = nullptr;
    long seconds = std::numeric_limits<long>::max();
    size_t bestOrder = std::numeric_limits<size_t>::max();
    for (const auto& candidate : candidates)
      if (candidate.second.complete && candidate.second.seconds >= 0 &&
          (candidate.second.seconds < seconds ||
           (candidate.second.seconds == seconds &&
            candidate.second.order < bestOrder))) {
        seconds = candidate.second.seconds;
        bestOrder = candidate.second.order;
        best = candidate.second.first;
      }
    if (best) {
      const auto current = CurrentRouteMaps();
      const bool inspectingGroup = std::any_of(
          current.begin(), current.end(), [&](RouteMapOverlay* selected) {
            return selected->GetConfiguration().DepartureTimeOptimizationGroupId
                       == group.first;
          });
      SelectDepartureOnChart(best, false, inspectingGroup);
    }
  }
}
