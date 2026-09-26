#include "WeatherRoutingMessageDialog.h"
#include <wx/wx.h>

#include "Utilities.h"
#include "Boat.h"
#include "RouteMapOverlay.h"
#include "RoutingFootprint.h"
#include "WeatherRouting.h"

#ifdef __OCPN__ANDROID__

#include <QWidget>
#include <QLineEdit>
#include <cmath>
#include <array>
#include <numeric>
#include <set>
#include <wx/scrolwin.h>
#include <wx/sizer.h>
#include <wx/stattext.h>
#include <wx/filename.h>

#include "WeatherRoutingWxCompat.h"
#include "AndroidDialogHeader.h"

namespace {

void SetTabletFont(wxWindow* window, int points, bool bold = false) {
  if (wxDynamicCast(window, wxStaticText)) WR_EnableAndroidLabelScrolling(window);
  wxFont font = window->GetFont();
  font.SetPointSize(points);
  font.SetWeight(bold ? wxFONTWEIGHT_BOLD : wxFONTWEIGHT_NORMAL);
  window->SetFont(font);
}

void StyleTabletButton(wxButton* button, bool primary = false) {
  WR_EnableAndroidButton(button);
  SetTabletFont(button, 17, primary);
  button->GetHandle()->setStyleSheet(primary
      ? "QPushButton { font-size: 18pt; font-weight: 600; color: white; "
        "background-color: #176b87; border: 1px solid #176b87; "
        "border-radius: 9px; padding: 5px; } "
        "QPushButton:pressed { background-color: #104e64; } "
        "QPushButton:disabled { color: #74818a; background: #e4e8eb; border-color: #c8d0d5; }"
      : "QPushButton { font-size: 17pt; color: #173849; "
        "background-color: white; border: 1px solid #9fb9c6; "
        "border-radius: 9px; padding: 5px; } "
        "QPushButton:pressed { background-color: #d5e9f0; } "
        "QPushButton:disabled { color: #74818a; background: #e4e8eb; border-color: #c8d0d5; }");
}

wxStaticText* AddHeading(wxWindow* parent, wxBoxSizer* sizer,
                         const wxString& title) {
  auto* text = new wxStaticText(parent, wxID_ANY, title);
  SetTabletFont(text, 19, true);
  sizer->Add(text, 0, wxLEFT | wxRIGHT | wxTOP | wxEXPAND, 14);
  return text;
}

wxScrolledWindow* AddScrollPage(wxSimplebook* book, const wxString& title,
                                wxBoxSizer*& content) {
  auto* page = new wxScrolledWindow(book, wxID_ANY);
  page->SetScrollRate(0, 20);
  WR_EnableAndroidScrolling(page);
  content = new wxBoxSizer(wxVERTICAL);
  // wxQt includes its scrollbar strip in GetClientSize. Reserve that strip
  // explicitly so the final column of actions remains completely visible.
  auto* outer = new wxBoxSizer(wxHORIZONTAL);
  outer->Add(content, 1, wxEXPAND);
  outer->AddSpacer(42);
  page->SetSizer(outer);
  book->AddPage(page, title);
  return page;
}

wxString TabletDuration(const wxDateTime& start, const wxDateTime& end) {
  if (!start.IsValid() || !end.IsValid() || end < start) return _("N/A");
  const long long seconds = (end - start).GetSeconds().GetValue();
  const long long days = seconds / 86400;
  const long long hours = seconds / 3600 % 24;
  const long long minutes = seconds / 60 % 60;
  if (days) return wxString::Format(_("%lldd %lldh %lldm"), days, hours, minutes);
  if (hours) return wxString::Format(_("%lldh %lldm"), hours, minutes);
  return wxString::Format(_("%lldm %llds"), minutes, seconds % 60);
}

std::array<wxString, WeatherRouting::NUM_COLS> TabletRouteValues(const WeatherRoute& route) {
  return {wxString(), route.BoatFilename, route.StartType, route.Start,
      route.StartTime, route.End, route.EndTime,
      route.routemapoverlay && route.Time != _("N/A")
          ? TabletDuration(route.routemapoverlay->StartTime(), route.routemapoverlay->EndTime())
          : route.Time, route.Distance,
      route.AvgSpeed, route.MaxSpeed, route.AvgSpeedGround, route.MaxSpeedGround,
      route.AvgWind, route.MaxWind, route.MaxWindGust, route.AvgCurrent,
      route.MaxCurrent, route.AvgSwell, route.MaxSwell, route.UpwindPercentage,
      route.PortStarboard, route.Tacks, route.Jibes, route.SailPlanChanges,
      route.Comfort, route.WeatherSource, route.State};
}

wxString TabletRouteState(const wxString& state) {
  if (state.Contains("retained lineage limit") ||
      state.Contains("graph label limit"))
    return _("Search memory limit reached. Increase the Engine memory budget or use a coarser search.");
  if (state.Contains("wall-clock limit"))
    return _("Search time limit reached. Try a shorter offshore time step or another engine.");
  if (state.Contains("candidate validation allowance"))
    return _("No safe candidate passed validation. Try a shorter offshore time step or another engine.");
  if (state.Contains("geometry work limit"))
    return _("Geometry work limit reached. Use fewer heading candidates or another engine.");
  if (state.Contains("contour nesting limit"))
    return _("Search contour limit reached. Try a shorter offshore time step or another engine.");
  if (state.Contains("generated state limit"))
    return _("Search limit reached. Try a coarser search or another engine.");
  return state;
}

wxString ChooseTabletOpenCPNRoute(wxWindow* parent) {
  wxArrayString guids, names;
  for (const auto& guid : GetRouteGUIDArray()) {
    auto route = GetRoute_Plugin(guid);
    if (!route || !route->pWaypointList || route->pWaypointList->GetCount() < 2)
      continue;
    guids.Add(guid);
    names.Add(route->m_NameString.IsEmpty() ? _("Unnamed route") : route->m_NameString);
  }
  if (guids.IsEmpty()) {
    WR_MessageBox(_("Create or import an OpenCPN route with at least two waypoints using the Route and Mark Manager first."),
                 _("OpenCPN routes"), wxOK | wxICON_INFORMATION, parent);
    return wxString();
  }
  wxDialog dialog(parent, wxID_ANY, _("OpenCPN routes"));
  auto* content = new wxBoxSizer(wxVERTICAL);
  auto* label = new wxStaticText(&dialog, wxID_ANY,
      _("Choose the OpenCPN route to use. Its waypoints will be retained."));
  content->Add(label, 0, wxEXPAND | wxALL, 16);
  auto* picker = new wxChoice(&dialog, wxID_ANY, wxDefaultPosition, wxDefaultSize, names);
  picker->SetSelection(0);
  content->Add(picker, 0, wxEXPAND | wxALL, 16);
  content->AddStretchSpacer();
  dialog.SetSizer(content);
  WR_StyleAndroidControls(&dialog);
  bool accepted = false;
  auto* header = WR_AddAndroidDoneHeader(&dialog, _("OpenCPN routes"), [&]() {
    dialog.EndModal(wxID_CANCEL);
  }, _("Cancel"));
  auto* use = new wxButton(header, wxID_ANY, _("Use route"));
  StyleTabletButton(use, true);
  use->SetMinSize(wxSize(170, 72));
  header->GetSizer()->Insert(1, use, 0, wxALL, 8);
  use->Bind(wxEVT_BUTTON, [&](wxCommandEvent&) {
    accepted = true;
    dialog.EndModal(wxID_OK);
  });
  const wxSize canvas = GetCanvasByIndex(0)->GetClientSize();
  dialog.SetMinSize(wxSize(0, 0));
  dialog.SetSize(canvas.x - 24, canvas.y - 24);
  dialog.CentreOnParent();
  WR_WrapAndroidText(label, label->GetLabel(), dialog.GetClientSize().x - 48);
  dialog.Layout();
  dialog.ShowModal();
  const int selected = picker->GetSelection();
  return accepted && selected >= 0 ? guids[selected] : wxString();
}

}  // namespace

wxWindow* WeatherRouting::BuildAndroidWorkspace(wxBoxSizer* root) {
  WR_InstallAndroidBack(this, [this]() {
    if (m_androidBook && m_androidBook->GetSelection() != 0) {
      m_androidBook->ChangeSelection(0);
      RefreshAndroidWorkspace();
    } else {
      m_androidChartVisible = false;
      Hide();
      GetParent()->Refresh();
    }
  });
  auto* header = new wxPanel(this, wxID_ANY);
  header->SetBackgroundColour(wxColour(25, 59, 76));
  auto* headerSizer = new wxBoxSizer(wxHORIZONTAL);
  auto* title = new wxStaticText(header, wxID_ANY, _("xWeatherRouting"));
  SetTabletFont(title, 21, true);
  title->SetForegroundColour(*wxWHITE);
  title->GetHandle()->setStyleSheet("QLabel { color: white; }");
  headerSizer->Add(title, 1, wxALIGN_CENTER_VERTICAL | wxLEFT, 18);
  auto showChart = [this]() {
    if (!CurrentRouteMaps().empty()) {
      wxCommandEvent event;
      OnGoTo(event);
    }
    m_androidChartVisible = true;
    Hide();
    GetParent()->Refresh();
  };
  auto* chart = new wxButton(header, wxID_ANY, _("Chart"));
  StyleTabletButton(chart);
  chart->SetMinSize(WR_FromDIP(this, wxSize(112, 62)));
  chart->Bind(wxEVT_BUTTON, [showChart](wxCommandEvent&) { showChart(); });
  headerSizer->Add(chart, 0, wxALIGN_CENTER_VERTICAL | wxALL, 7);
  auto* close = new wxButton(header, wxID_ANY, _("Close"));
  StyleTabletButton(close);
  close->SetMinSize(WR_FromDIP(this, wxSize(112, 62)));
  close->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) {
    m_androidChartVisible = false;
    Hide();
    GetParent()->Refresh();
  });
  headerSizer->Add(close, 0, wxALIGN_CENTER_VERTICAL | wxALL, 7);
  header->SetSizer(headerSizer);
  root->Add(header, 0, wxEXPAND);

  auto* navigation = new wxPanel(this, wxID_ANY);
  auto* navigationSizer = new wxBoxSizer(wxHORIZONTAL);
  navigation->SetSizer(navigationSizer);
  root->Add(navigation, 0, wxEXPAND);

  m_androidBook = new wxSimplebook(this, wxID_ANY);
  root->Add(m_androidBook, 1, wxEXPAND);

  auto addAction = [this](wxWindow* parent, wxSizer* sizer,
                          const wxString& label, std::function<void()> action) {
    auto* button = new wxButton(parent, wxID_ANY, label);
    if (label == _("Routing status") || label == _("Show on chart"))
      button->SetName("wr-available-during-routing");
    StyleTabletButton(button, label == _("Create route") ||
                              label == _("Compute selected"));
    button->SetMinSize(WR_FromDIP(this, wxSize(170, 72)));
    button->Bind(wxEVT_BUTTON, [this, action](wxCommandEvent&) {
      action();
      CallAfter([this]() { RefreshAndroidWorkspace(); });
    });
    if (label == _("Pick start") || label == _("Pick destination"))
      m_androidPickButtons.push_back(button);
    if (label == _("Stop")) m_androidStopButtons.push_back(button);
    if (label == _("Save all as tracks")) m_androidAnyResultButtons.push_back(button);
    if (label == _("Compute all") || label == _("Reset all") || label == _("Delete all"))
      m_androidAnyRouteButtons.push_back(button);
    if (label == _("Delete all positions")) m_androidAnyPositionButtons.push_back(button);
    if (label == _("Edit selected position") || label == _("Delete selected position"))
      m_androidNeedsPosition.push_back(button);
    for (const wxString& required : {_("Edit selected route"),
         _("Compute selected"), _("Show on chart"), _("Endpoints"),
         _("Time"), _("Boat"), _("Weather"), _("Safety"), _("Engine"),
         _("Sailing"), _("Edit boat and polars"), _("Routing status"),
         _("Edit selected"), _("Duplicate selected"), _("Delete selected"), _("Multi-leg settings"),
         _("Compute multi-leg"), _("Optimise multi-leg departure")})
      if (label == required) m_androidNeedsRoute.push_back(button);
    for (const wxString& required : {_("Statistics"), _("Report"), _("Plot"),
         _("Route table"), _("Cursor position"), _("Route position"),
         _("Stability corridor"), _("Save as OpenCPN route"),
         _("Save as track"), _("Simplify route"), _("Export GPX")})
      if (label == required) m_androidNeedsResult.push_back(button);
    for (const wxString& output : {_("Simplify route"), _("Export GPX"),
         _("Save as OpenCPN route"), _("Save as track"), _("Report")})
      if (label == output) button->SetName("wr-all-selected-complete");
    sizer->Add(button, 1, wxEXPAND | wxALL, 5);
  };
  auto command = [this](void (WeatherRouting::*handler)(wxCommandEvent&)) {
    return [this, handler]() {
      wxCommandEvent event(wxEVT_BUTTON);
      (this->*handler)(event);
    };
  };
  auto destructive = [this](void (WeatherRouting::*handler)(wxCommandEvent&),
                             const wxString& question) {
    return [this, handler, question]() {
      if (WR_MessageBox(question, _("Confirm action"),
                       wxYES_NO | wxICON_WARNING, this) != wxYES)
        return;
      wxCommandEvent event(wxEVT_BUTTON);
      (this->*handler)(event);
    };
  };
  auto addActions = [&](wxWindow* parent, wxBoxSizer* content,
                        const wxString& title,
                        std::initializer_list<std::pair<wxString,
                            std::function<void()>>> actions) {
    AddHeading(parent, content, title);
    auto* grid = new wxGridSizer(0, 2, 6, 6);
    for (const auto& action : actions)
      addAction(parent, grid, action.first, action.second);
    content->Add(grid, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 10);
  };

  wxBoxSizer* planContent = nullptr;
  wxScrolledWindow* plan = AddScrollPage(m_androidBook, _("Plan"), planContent);
  AddHeading(plan, planContent, _("Plan a passage"));
  auto* planIntro = new wxStaticText(
      plan, wxID_ANY,
      _("Set endpoints, time, boat, forecast, engine and safety limits."));
  SetTabletFont(planIntro, 16);
  planContent->Add(planIntro, 0, wxEXPAND | wxALL, 14);
  m_androidPlanStatus = new wxStaticText(
      plan, wxID_ANY, _("Select a saved route on Routes, or create a new one."));
  SetTabletFont(m_androidPlanStatus, 17);
  m_androidPlanStatus->SetMinSize(wxSize(-1, 40));
  planContent->Add(m_androidPlanStatus, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 14);
  addActions(plan, planContent, _("Route setup"), {
      {_("Create route"), command(&WeatherRouting::OnNew)},
      {_("Edit selected route"), [this]() { OnEditConfiguration(); }},
      {_("Batch planner"), command(&WeatherRouting::OnBatch)},
      {_("Edit boat and polars"), [this]() { m_ConfigurationDialog.EditBoat(); }},
      {_("Compute selected"), command(&WeatherRouting::OnCompute)},
      {_("Show on chart"), showChart},
  });
  auto section = [this](size_t index) {
    return [this, index]() {
      OnEditConfiguration();
      m_ConfigurationDialog.ShowAndroidSection(index);
    };
  };
  addActions(plan, planContent, _("Adjust selected route"), {
      {_("Endpoints"), section(0)}, {_("Time"), section(1)},
      {_("Boat"), section(2)}, {_("Weather"), section(3)},
      {_("Safety"), section(4)}, {_("Engine"), section(5)},
      {_("Sailing"), section(6)},
  });
  addActions(plan, planContent, _("Choose on chart"), {
      {_("Pick start"), [this]() { BeginAndroidChartPick(0); }},
      {_("Pick destination"), [this]() { BeginAndroidChartPick(1); }},
  });
  auto* forecastNote = new wxStaticText(
      plan, wxID_ANY,
      _("Forecast: load or generate weather with xGRIB on the OpenCPN toolbar."));
  SetTabletFont(forecastNote, 16);
  planContent->Add(forecastNote, 0, wxEXPAND | wxALL, 14);

  auto* routes = new wxPanel(m_androidBook, wxID_ANY);
  auto* routesContent = new wxBoxSizer(wxVERTICAL);
  routes->SetSizer(routesContent);
  m_androidBook->AddPage(routes, _("Routes"));
  auto* routeActions = new wxBoxSizer(wxHORIZONTAL);
  addAction(routes, routeActions, _("New"), command(&WeatherRouting::OnNew));
  addAction(routes, routeActions, _("Compute selected"),
            command(&WeatherRouting::OnCompute));
  addAction(routes, routeActions, _("Compute all"),
            command(&WeatherRouting::OnComputeAll));
  addAction(routes, routeActions, _("Stop"), command(&WeatherRouting::OnStop));
  routesContent->Add(routeActions, 0, wxEXPAND | wxALL, 5);
  auto* sortRow = new wxBoxSizer(wxHORIZONTAL);
  auto* sortLabel = new wxStaticText(routes, wxID_ANY, _("Sort by"));
  SetTabletFont(sortLabel, 16);
  sortRow->Add(sortLabel, 0, wxALIGN_CENTER_VERTICAL | wxALL, 8);
  m_androidRouteSort = new wxChoice(routes, wxID_ANY);
  m_androidRouteSort->Append(_("Saved order"));
  for (int field = 1; field < NUM_COLS; ++field)
    m_androidRouteSort->Append(column_names[field]);
  m_androidRouteSort->SetSelection(0);
  m_androidRouteSort->SetMinSize(wxSize(0, 72));
  SetTabletFont(m_androidRouteSort, 16);
  WR_StyleAndroidCombo(m_androidRouteSort);
  sortRow->Add(m_androidRouteSort, 1, wxEXPAND | wxALL, 5);
  m_androidRouteDescending = new wxCheckBox(routes, wxID_ANY, _("Descending"));
  SetTabletFont(m_androidRouteDescending, 16);
  m_androidRouteDescending->SetMinSize(wxSize(250, 72));
  m_androidRouteDescending->GetHandle()->setStyleSheet(
      "QCheckBox { font-size: 16pt; min-height: 64px; spacing: 16px; } "
      "QCheckBox::indicator { width: 30px; height: 30px; }");
  sortRow->Add(m_androidRouteDescending, 0, wxALIGN_CENTER_VERTICAL | wxALL, 8);
  auto refreshSort = [this](wxCommandEvent&) {
    m_androidRouteSignature.clear();
    CallAfter([this]() { RefreshAndroidWorkspace(); });
  };
  m_androidRouteSort->Bind(wxEVT_CHOICE, refreshSort);
  m_androidRouteDescending->Bind(wxEVT_CHECKBOX, refreshSort);
  routesContent->Add(sortRow, 0, wxEXPAND | wxLEFT | wxRIGHT, 8);
  m_androidRoutesStatus = new wxStaticText(routes, wxID_ANY, wxEmptyString);
  SetTabletFont(m_androidRoutesStatus, 15);
  routesContent->Add(m_androidRoutesStatus, 0, wxEXPAND | wxALL, 10);
  m_androidRoutesList = new wxScrolledWindow(routes, wxID_ANY,
      wxDefaultPosition, wxDefaultSize, wxVSCROLL | wxTAB_TRAVERSAL);
  m_androidRoutesList->SetScrollRate(0, 28);
  WR_EnableAndroidScrolling(m_androidRoutesList);
  m_androidRouteCards = new wxBoxSizer(wxVERTICAL);
  m_androidRoutesList->SetSizer(m_androidRouteCards);
  routesContent->Add(m_androidRoutesList, 1, wxEXPAND | wxALL, 7);

  wxBoxSizer* resultsContent = nullptr;
  wxScrolledWindow* results =
      AddScrollPage(m_androidBook, _("Results"), resultsContent);
  AddHeading(results, resultsContent, _("Selected route"));
  m_androidResultStatus = new wxStaticText(
      results, wxID_ANY, _("Select a route on Routes to see its result."));
  SetTabletFont(m_androidResultStatus, 17);
  m_androidResultStatus->SetMinSize(wxSize(-1, 100));
  resultsContent->Add(m_androidResultStatus, 0,
                      wxEXPAND | wxALL, 16);
  addActions(results, resultsContent, _("Inspect"), {
      {_("Show on chart"), showChart},
      {_("Routing status"), command(&WeatherRouting::OnShowRoutingStatus)},
      {_("Statistics"), command(&WeatherRouting::OnStatistics)},
      {_("Report"), command(&WeatherRouting::OnReport)},
      {_("Plot"), command(&WeatherRouting::OnPlot)},
      {_("Route table"), command(&WeatherRouting::OnWeatherTable)},
      {_("Cursor position"), command(&WeatherRouting::OnCursorPosition)},
      {_("Route position"), command(&WeatherRouting::OnRoutePosition)},
      {_("Stability corridor"), command(&WeatherRouting::OnViewStabilityCorridor)},
  });
  addActions(results, resultsContent, _("Use this route"), {
      {_("Save as OpenCPN route"), command(&WeatherRouting::OnSaveAsRoute)},
      {_("Save as track"), command(&WeatherRouting::OnSaveAsTrack)},
      {_("Simplify route"), command(&WeatherRouting::OnSimplifyRoute)},
      {_("Export GPX"), command(&WeatherRouting::OnExportRouteAsGPX)},
      {_("Save all as tracks"), command(&WeatherRouting::OnSaveAllAsTracks)},
  });

  wxBoxSizer* toolsContent = nullptr;
  wxScrolledWindow* tools = AddScrollPage(m_androidBook, _("Tools"), toolsContent);
  m_androidToolsPage = tools;
  AddHeading(tools, toolsContent, _("Saved positions"));
  m_androidPositionCards = new wxBoxSizer(wxVERTICAL);
  toolsContent->Add(m_androidPositionCards, 0, wxEXPAND | wxALL, 8);
  addActions(tools, toolsContent, _("Positions"), {
      {_("New position"), command(&WeatherRouting::OnNewPosition)},
      {_("Pick position on chart"), [this]() { BeginAndroidChartPick(-1); }},
      {_("Edit selected position"), [this]() { OnEditPosition(); }},
      {_("Update boat position"), command(&WeatherRouting::OnUpdateBoat)},
      {_("Delete selected position"), destructive(&WeatherRouting::OnDeletePosition,
          _("Delete the selected position?"))},
      {_("Delete all positions"), destructive(&WeatherRouting::OnDeleteAllPositions,
          _("Delete all saved positions?"))},
  });
  addActions(tools, toolsContent, _("Route management"), {
      {_("Use OpenCPN route"), [this]() {
        wxString guid = ChooseTabletOpenCPNRoute(this);
        if (!guid.IsEmpty()) AddRoute(guid);
      }},
      {_("Create multi-leg passage"), [this]() {
        const wxString guid = ChooseTabletOpenCPNRoute(this);
        if (!guid.IsEmpty()) CreateMultiLegConfigurationsFromRoute(guid);
      }},
      {_("Filter routes"), command(&WeatherRouting::OnFilter)},
      {_("Edit selected"), [this]() { OnEditConfiguration(); }},
      {_("Duplicate selected"), [this]() {
        std::vector<RouteMapConfiguration> copies;
        for (const auto& group : SelectedRouteOutputGroups()) {
          const wxString newGroup = group.multi_leg ? "multileg-copy-" + GetNewGUID()
                                                    : wxString();
          for (auto* map : group.routes) {
            auto copy = map->GetConfiguration();
            copy.PromoteDepartureTimeOptimizationCandidate();
            if (group.multi_leg) copy.MultiLegGroupId = newGroup;
            copies.push_back(copy);
          }
        }
        std::set<WeatherRoute*> added;
        for (auto& copy : copies)
          if (AddConfiguration(copy)) added.insert(m_WeatherRoutes.back());
        for (long row = 0; row < m_panel->m_lWeatherRoutes->GetItemCount(); ++row) {
          auto* route = reinterpret_cast<WeatherRoute*>(
              wxUIntToPtr(m_panel->m_lWeatherRoutes->GetItemData(row)));
          m_panel->m_lWeatherRoutes->SetItemState(row,
              added.count(route) ? wxLIST_STATE_SELECTED : 0, wxLIST_STATE_SELECTED);
        }
        OnWeatherRouteSelected();
        m_androidBook->ChangeSelection(1);
        RefreshAndroidWorkspace();
      }},
      {_("Multi-leg settings"), command(&WeatherRouting::OnEditMultiLegGroupSettings)},
      {_("Compute multi-leg"), command(&WeatherRouting::OnComputeMultiLegSequence)},
      {_("Optimise multi-leg departure"), command(&WeatherRouting::OnOptimizeMultiLegDeparture)},
      {_("Reset all"), destructive(&WeatherRouting::OnResetAll,
          _("Reset every routing result?"))},
      {_("Delete selected"), [this]() {
        const size_t count = CurrentRouteMaps(false).size();
        if (!count) return;
        const wxString question = wxString::Format(_("Delete %lu selected routes?"),
            static_cast<unsigned long>(count));
        if (WR_MessageBox(question, _("Weather Routing"), wxYES_NO | wxNO_DEFAULT,
                          this) == wxYES) {
          wxCommandEvent event; OnDelete(event); RefreshAndroidWorkspace();
        }
      }},
      {_("Delete all"), destructive(&WeatherRouting::OnDeleteAll,
          _("Delete every route?"))},
  });
  addActions(tools, toolsContent, _("Boats and polars"), {
      {_("Open boat library"), [this]() {
        if (auto* map = FirstCurrentRouteMap())
          m_BoatDialog.LoadPolar(map->GetConfiguration().boatFileName);
        m_BoatDialog.Show();
        m_BoatDialog.Raise();
      }},
  });
  addActions(tools, toolsContent, _("Route sets"), {
      {_("Open XML"), command(&WeatherRouting::OnOpen)},
      {_("Save XML"), command(&WeatherRouting::OnSave)},
      {_("Save XML as"), command(&WeatherRouting::OnSaveAs)},
  });
  addActions(tools, toolsContent, _("Display and safety"), {
      {_("Display settings"), command(&WeatherRouting::OnSettings)},
      {_("Chart awareness"), command(&WeatherRouting::OnChartAwarenessSettings)},
      {_("Shoreline data"), command(&WeatherRouting::OnShorelineData)},
  });
  addActions(tools, toolsContent, _("Help"), {
      {_("Information"), command(&WeatherRouting::OnInformation)},
      {_("Manual"), command(&WeatherRouting::OnManual)},
      {_("About"), command(&WeatherRouting::OnAbout)},
  });

  const wxString labels[] = {_("Plan"), _("Routes"), _("Results"), _("Tools")};
  for (size_t i = 0; i < 4; ++i) {
    auto* tab = new wxButton(navigation, wxID_ANY, labels[i]);
    m_androidNavigation.push_back(tab);
    StyleTabletButton(tab);
    tab->SetMinSize(WR_FromDIP(this, wxSize(120, 72)));
    tab->Bind(wxEVT_BUTTON, [this, i](wxCommandEvent&) {
      m_androidBook->ChangeSelection(i);
      if (i == 1) m_androidRouteSignature.clear();
      RefreshAndroidWorkspace();
    });
    navigationSizer->Add(tab, 1, wxEXPAND | wxALL, 3);
  }
  m_androidBook->ChangeSelection(0);
  return routes;
}

void WeatherRouting::RefreshAndroidWorkspace() {
  if (!m_panel || !m_androidPlanStatus || !m_androidResultStatus) return;
  for (size_t i = 0; i < m_androidNavigation.size(); ++i)
    StyleTabletButton(m_androidNavigation[i], i == m_androidBook->GetSelection());
  wxString routeSignature = wxString::Format("%d:%d:",
      m_androidRouteSort->GetSelection(), m_androidRouteDescending->GetValue());
  int selectedCount = 0, runningCount = 0, completeCount = 0;
  for (auto* route : m_WeatherRoutes) {
    if (route->routemapoverlay->Running()) ++runningCount;
    else if (route->routemapoverlay->Finished() &&
             route->routemapoverlay->ReachedDestination()) ++completeCount;
  }
  for (long index = -1; (index = m_panel->m_lWeatherRoutes->GetNextItem(
           index, wxLIST_NEXT_ALL, wxLIST_STATE_SELECTED)) != -1;)
    ++selectedCount;
  m_androidRoutesStatus->SetLabel(wxString::Format(
      _("%d selected / %d running / %d complete"),
      selectedCount, runningCount, completeCount));
  for (int field = 0; field < NUM_COLS; ++field)
    routeSignature += m_SettingsDialog.m_cblFields->IsChecked(field) ? "1" : "0";
  for (long i = 0; i < m_panel->m_lWeatherRoutes->GetItemCount(); ++i) {
    auto* route = reinterpret_cast<WeatherRoute*>(
        wxUIntToPtr(m_panel->m_lWeatherRoutes->GetItemData(i)));
    if (!route || std::find(m_WeatherRoutes.begin(), m_WeatherRoutes.end(), route)
                      == m_WeatherRoutes.end()) continue;
    routeSignature += wxString::Format("%p", route) + route->Start + route->End +
                      route->StartTime + route->State + route->Time +
                      route->Distance +
                      wxString::Format("%d", (m_panel->m_lWeatherRoutes->GetItemState(
                          i, wxLIST_STATE_SELECTED) & wxLIST_STATE_SELECTED) != 0);
  }
  if (routeSignature != m_androidRouteSignature && m_androidRouteCards) {
    m_androidRouteSignature = routeSignature;
    int savedX, savedY;
    m_androidRoutesList->GetViewStart(&savedX, &savedY);
    m_androidRoutesList->Freeze();
    m_androidRouteCards->Clear(true);
    const long count = m_panel->m_lWeatherRoutes->GetItemCount();
    if (!count) {
      auto* empty = new wxStaticText(m_androidRoutesList, wxID_ANY,
          m_WeatherRoutes.empty() ? _("No routes yet. Create one on Plan.") :
          _("No routes match your filters. Clear filters to show your routes."));
      SetTabletFont(empty, 17);
      m_androidRouteCards->Add(empty, 0, wxALL, 16);
      if (!m_WeatherRoutes.empty()) {
        auto* clear = new wxButton(m_androidRoutesList, wxID_ANY,
                                   _("Clear all filters"));
        StyleTabletButton(clear);
        clear->SetMinSize(wxSize(-1, 72));
        clear->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) {
          wxCommandEvent event;
          m_FilterRoutesDialog.OnResetAll(event);
          RefreshAndroidWorkspace();
        });
        m_androidRouteCards->Add(clear, 0, wxEXPAND | wxALL, 16);
      }
    }
    // Sort the presentation only: the controller's row indexes remain stable
    // for multi-selection, filtering and all legacy route commands.
    std::vector<long> cardOrder(count);
    std::iota(cardOrder.begin(), cardOrder.end(), 0);
    const int sortField = m_androidRouteSort->GetSelection();
    if (sortField > 0 && sortField < NUM_COLS) {
      auto rowValue = [this, sortField](long row) {
        auto* route = reinterpret_cast<WeatherRoute*>(wxUIntToPtr(
            m_panel->m_lWeatherRoutes->GetItemData(row)));
        if (!route || std::find(m_WeatherRoutes.begin(), m_WeatherRoutes.end(), route)
                         == m_WeatherRoutes.end()) return wxString();
        return TabletRouteValues(*route)[sortField];
      };
      std::stable_sort(cardOrder.begin(), cardOrder.end(), [&](long a, long b) {
        const wxString left = rowValue(a), right = rowValue(b);
        int comparison = left.CmpNoCase(right);
        double leftNumber, rightNumber;
        if (sortField >= DISTANCE && sortField <= SAIL_PLAN_CHANGES &&
            left.BeforeFirst('/').BeforeFirst(' ').ToDouble(&leftNumber) &&
            right.BeforeFirst('/').BeforeFirst(' ').ToDouble(&rightNumber))
          comparison = leftNumber < rightNumber ? -1 : leftNumber > rightNumber ? 1 : 0;
        return m_androidRouteDescending->GetValue() ? comparison > 0 : comparison < 0;
      });
    } else if (m_androidRouteDescending->GetValue())
      std::reverse(cardOrder.begin(), cardOrder.end());
    for (long i : cardOrder) {
      auto* route = reinterpret_cast<WeatherRoute*>(
          wxUIntToPtr(m_panel->m_lWeatherRoutes->GetItemData(i)));
      if (!route || std::find(m_WeatherRoutes.begin(), m_WeatherRoutes.end(), route)
                      == m_WeatherRoutes.end()) continue;
      const bool selected = (m_panel->m_lWeatherRoutes->GetItemState(
          i, wxLIST_STATE_SELECTED) & wxLIST_STATE_SELECTED) != 0;
      auto* card = new wxPanel(m_androidRoutesList, wxID_ANY);
      card->SetBackgroundColour(selected ? wxColour(210, 231, 239) : *wxWHITE);
      auto* layout = new wxBoxSizer(wxVERTICAL);
      auto* title = new wxStaticText(card, wxID_ANY,
                                     route->Start + _("  to  ") + route->End);
      SetTabletFont(title, 19, true);
      const int cardWidth = wxMax(400, m_androidRoutesList->GetClientSize().x - 38);
      WR_WrapAndroidText(title, title->GetLabel(), cardWidth - 24);
      layout->Add(title, 0, wxEXPAND | wxALL, 9);
      wxString cardDetail = TabletRouteState(route->State);
      const auto configuration = route->routemapoverlay->GetConfiguration();
      if (configuration.IsMultiLegGenerated)
        cardDetail += wxString::Format(_("\nPassage leg %d of %d"),
            configuration.MultiLegLegIndex, configuration.MultiLegLegCount);
      cardDetail += _("\nEngine: ") + wxString::FromUTF8(
          weather_routing::EngineTitle(configuration.EngineSettings.engine));
      if (std::abs(configuration.WindStrength - 1.0) > 0.0001)
        cardDetail += wxString::Format(_("\nWind strength: %.0f%%"),
                                      configuration.WindStrength * 100.0);
      const auto values = TabletRouteValues(*route);
      for (int field = 1; field < NUM_COLS; ++field) {
        if (field == START || field == END || field == STATE ||
            !m_SettingsDialog.m_cblFields->IsChecked(field) ||
            values[field].IsEmpty() || values[field] == _("N/A")) continue;
        const wxString value = field == BOAT ? wxFileName(values[field]).GetFullName() : values[field];
        cardDetail += "\n" + column_names[field] + ": " + value;
      }
      auto* detail = new wxStaticText(card, wxID_ANY, cardDetail);
      SetTabletFont(detail, 15);
      WR_WrapAndroidText(detail, detail->GetLabel(), cardWidth - 24);
      layout->Add(detail, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 9);
      auto* actions = new wxBoxSizer(wxHORIZONTAL);
      auto* select = new wxButton(card, wxID_ANY,
                                  selected ? _("Deselect") : _("Select"));
      StyleTabletButton(select, selected);
      select->SetMinSize(wxSize(150, 72));
      select->Bind(wxEVT_BUTTON, [this, i](wxCommandEvent&) {
        const bool isSelected = (m_panel->m_lWeatherRoutes->GetItemState(
            i, wxLIST_STATE_SELECTED) & wxLIST_STATE_SELECTED) != 0;
        m_panel->m_lWeatherRoutes->SetItemState(i,
            isSelected ? 0 : wxLIST_STATE_SELECTED,
                                               wxLIST_STATE_SELECTED);
        OnWeatherRouteSelected();
        CallAfter([this]() { RefreshAndroidWorkspace(); });
      });
      actions->Add(select, 1, wxEXPAND | wxRIGHT, 6);
      auto* edit = new wxButton(card, wxID_ANY, _("Edit"));
      StyleTabletButton(edit);
      edit->Enable(!route->routemapoverlay->Running());
      edit->SetMinSize(wxSize(150, 72));
      edit->Bind(wxEVT_BUTTON, [this, i](wxCommandEvent&) {
        for (long row = 0; row < m_panel->m_lWeatherRoutes->GetItemCount(); ++row)
          m_panel->m_lWeatherRoutes->SetItemState(row, 0, wxLIST_STATE_SELECTED);
        m_panel->m_lWeatherRoutes->SetItemState(i, wxLIST_STATE_SELECTED,
                                               wxLIST_STATE_SELECTED);
        OnWeatherRouteSelected();
        OnEditConfiguration();
        CallAfter([this]() { RefreshAndroidWorkspace(); });
      });
      actions->Add(edit, 1, wxEXPAND);
      layout->Add(actions, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 9);
      auto* visibility = new wxCheckBox(card, wxID_ANY, _("Visible on chart"));
      visibility->SetValue(route->routemapoverlay->m_bEndRouteVisible);
      SetTabletFont(visibility, 16);
      visibility->SetMinSize(wxSize(0, 72));
      visibility->GetHandle()->setStyleSheet(
          "QCheckBox { font-size: 16pt; min-height: 64px; spacing: 20px; } "
          "QCheckBox::indicator { width: 30px; height: 30px; }");
      visibility->Bind(wxEVT_CHECKBOX, [this, visibility, route](wxCommandEvent&) {
        if (std::find(m_WeatherRoutes.begin(), m_WeatherRoutes.end(), route)
                == m_WeatherRoutes.end()) return;
        route->routemapoverlay->m_bEndRouteVisible = visibility->GetValue();
        GetParent()->Refresh();
      });
      layout->Add(visibility, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 9);
      card->SetSizer(layout);
      card->SetMinSize(wxSize(0, -1));
      card->SetMaxSize(wxSize(cardWidth, -1));
      m_androidRouteCards->Add(card, 0, wxEXPAND | wxBOTTOM, 9);
    }
    m_androidRoutesList->FitInside();
    m_androidRoutesList->Layout();
    m_androidRoutesList->Scroll(savedX, savedY);
    m_androidRoutesList->Thaw();
  }
  const auto positionText = [this](long index) {
    const wxString name = m_panel->m_lPositions->GetItemText(index);
    for (const auto& position : RouteMap::Positions)
      if (position.Name == name)
        return name + wxString::Format("\n%.5f, %.5f", position.lat, position.lon);
    return name;
  };
  wxString positionSignature;
  for (long i = 0; i < m_panel->m_lPositions->GetItemCount(); ++i) {
    positionSignature += positionText(i) +
        wxString::Format("%d", (m_panel->m_lPositions->GetItemState(
            i, wxLIST_STATE_SELECTED) & wxLIST_STATE_SELECTED) != 0);
  }
  if (positionSignature != m_androidPositionSignature && m_androidPositionCards) {
    m_androidPositionSignature = positionSignature;
    m_androidPositionCards->Clear(true);
    for (long i = 0; i < m_panel->m_lPositions->GetItemCount(); ++i) {
      const bool selected = (m_panel->m_lPositions->GetItemState(
          i, wxLIST_STATE_SELECTED) & wxLIST_STATE_SELECTED) != 0;
      auto* card = new wxPanel(m_androidToolsPage, wxID_ANY);
      card->SetBackgroundColour(selected ? wxColour(210, 231, 239) : *wxWHITE);
      auto* layout = new wxBoxSizer(wxHORIZONTAL);
      auto* label = new wxStaticText(card, wxID_ANY, positionText(i));
      SetTabletFont(label, 16);
      WR_WrapAndroidText(label, positionText(i),
          wxMax(200, (m_androidToolsPage->GetClientSize().x > 400
              ? m_androidToolsPage->GetClientSize().x
              : GetCanvasByIndex(0)->GetClientSize().x * 9 / 10) - 230));
      layout->Add(label, 1, wxALIGN_CENTER_VERTICAL | wxALL, 8);
      auto* select = new wxButton(card, wxID_ANY,
                                  selected ? _("Selected") : _("Select"));
      StyleTabletButton(select, selected);
      select->SetMinSize(wxSize(150, 72));
      select->Bind(wxEVT_BUTTON, [this, i](wxCommandEvent&) {
        for (long row = 0; row < m_panel->m_lPositions->GetItemCount(); ++row)
          m_panel->m_lPositions->SetItemState(row, 0, wxLIST_STATE_SELECTED);
        m_panel->m_lPositions->SetItemState(i, wxLIST_STATE_SELECTED,
                                           wxLIST_STATE_SELECTED);
        CallAfter([this]() { RefreshAndroidWorkspace(); });
      });
      layout->Add(select, 0, wxALIGN_CENTER_VERTICAL | wxALL, 8);
      card->SetSizer(layout);
      m_androidPositionCards->Add(card, 0, wxEXPAND | wxBOTTOM, 6);
    }
    m_androidToolsPage->FitInside();
  }
  const bool positionSelected = m_panel->m_lPositions->GetNextItem(
      -1, wxLIST_NEXT_ALL, wxLIST_STATE_SELECTED) >= 0;
  for (auto* button : m_androidNeedsPosition) button->Enable(positionSelected);
  for (auto* button : m_androidAnyPositionButtons)
    button->Enable(!RouteMap::Positions.empty());
  for (auto* button : m_androidAnyRouteButtons)
    button->Enable(!m_WeatherRoutes.empty());
  for (auto* button : m_androidAnyResultButtons) button->Enable(completeCount > 0);
  const long selected = m_panel->m_lWeatherRoutes->GetNextItem(
      -1, wxLIST_NEXT_ALL, wxLIST_STATE_SELECTED);
  bool running = false;
  for (auto* route : CurrentRouteMaps(false)) running |= route->Running();
  for (auto* button : m_androidNeedsRoute)
    button->Enable(selected >= 0 && (!running ||
        button->GetName() == "wr-available-during-routing"));
  for (auto* button : m_androidPickButtons) button->Enable(!running);
  for (auto* button : m_androidStopButtons)
    button->Enable(!m_RunningRouteMaps.empty() || !m_WaitingRouteMaps.empty() ||
                   m_ChartSafetyComputeProgressActive);
  RouteMapOverlay* selectedMap = FirstCurrentRouteMap();
  const bool hasResult = selectedMap && selectedMap->Finished() &&
                        selectedMap->ReachedDestination();
  bool allComplete = hasResult;
  for (auto* map : CurrentRouteMaps(false))
    allComplete &= map->Finished() && map->ReachedDestination();
  for (auto* button : m_androidNeedsResult)
    button->Enable(button->GetName() == "wr-all-selected-complete" ? allComplete : hasResult);
  if (selected < 0) {
    const wxString none = _("No route selected. Create one on Plan or select a route on Routes.");
    m_androidPlanStatus->SetLabel(none);
    m_androidResultStatus->SetLabel(none);
    m_androidPlanStatus->GetParent()->Layout();
    m_androidResultStatus->GetParent()->Layout();
    return;
  }
  auto* route = reinterpret_cast<WeatherRoute*>(
      wxUIntToPtr(m_panel->m_lWeatherRoutes->GetItemData(selected)));
  if (!route || std::find(m_WeatherRoutes.begin(), m_WeatherRoutes.end(), route)
                    == m_WeatherRoutes.end()) return;
  const wxString endpoints = route->Start + _("  to  ") + route->End;
  WR_WrapAndroidText(m_androidPlanStatus, endpoints + _("\n") + route->StartTime,
      wxMax(300, m_androidPlanStatus->GetParent()->GetClientSize().x - 80));
  wxString result = endpoints + _("\n") + TabletRouteState(route->State) + _("\n") +
      _("Duration: ") + (hasResult ? TabletDuration(selectedMap->StartTime(), selectedMap->EndTime()) : route->Time) +
      _("   Distance: ") + route->Distance +
      _("   Weather: ") + route->WeatherSource;
  const auto outputGroups = SelectedRouteOutputGroups();
  if (outputGroups.size() == 1 && outputGroups.front().multi_leg &&
      !outputGroups.front().routes.empty()) {
    const auto& legs = outputGroups.front().routes;
    size_t completed = 0;
    double distance = 0.0;
    for (auto* leg : legs) {
      if (!leg->Finished() || !leg->ReachedDestination()) continue;
      ++completed;
      const auto points = FullOutputRoute(*leg);
      for (size_t i = 1; i < points.size(); ++i)
        distance += DistGreatCircle_Plugin(points[i - 1].lat, points[i - 1].lon,
                                          points[i].lat, points[i].lon);
    }
    result = legs.front()->GetConfiguration().Start + _("  to  ") +
        legs.back()->GetConfiguration().End + _("\n") +
        wxString::Format(_("Passage: %lu of %lu legs complete"),
            static_cast<unsigned long>(completed), static_cast<unsigned long>(legs.size()));
    if (completed == legs.size()) {
      result += _("\nDuration: ") +
          TabletDuration(legs.front()->StartTime(), legs.back()->EndTime()) +
          wxString::Format(_("   Distance: %.1f NM"), distance) +
          _("\nETA: ") + m_SettingsDialog.FormatTime(legs.back()->EndTime(), "%x %H:%M");
    }
    result += _("\nExports use the complete passage. Inspectors show the selected leg.");
    for (auto* button : m_androidNeedsResult)
      if (button->GetName() == "wr-all-selected-complete")
        button->Enable(completed == legs.size());
  }
  WR_WrapAndroidText(m_androidResultStatus, result,
      wxMax(300, m_androidResultStatus->GetParent()->GetClientSize().x - 80));
  m_androidPlanStatus->GetParent()->Layout();
  m_androidResultStatus->GetParent()->Layout();
}

bool WeatherRouting::EditAndroidPosition(wxString& name, double& latitude,
                                        double& longitude) {
  bool accepted = false;
  wxDialog dialog(this, wxID_ANY, _("Position"), wxDefaultPosition,
                  wxDefaultSize, wxDEFAULT_DIALOG_STYLE);
  auto* root = new wxBoxSizer(wxVERTICAL);
  auto* content = new wxScrolledWindow(&dialog, wxID_ANY);
  content->SetScrollRate(0, 16);
  auto* fields = new wxBoxSizer(wxVERTICAL);
  auto field = [&](const wxString& label, const wxString& value) {
    AddHeading(content, fields, label);
    auto* input = new wxTextCtrl(content, wxID_ANY, value);
    SetTabletFont(input, 18);
    input->SetMinSize(wxSize(0, 72));
    fields->Add(input, 0, wxEXPAND | wxALL, 12);
    return input;
  };
  auto* positionName = field(_("Name"), name);
  auto* latitudeInput = field(_("Latitude (decimal degrees)"),
      wxString::Format("%.6f", latitude));
  auto* longitudeInput = field(_("Longitude (decimal degrees)"),
      wxString::Format("%.6f", longitude));
  auto* note = new wxStaticText(content, wxID_ANY,
      _("North and east are positive. South and west are negative."));
  SetTabletFont(note, 16);
  note->Wrap(900);
  fields->Add(note, 0, wxEXPAND | wxALL, 12);
  auto* error = new wxStaticText(content, wxID_ANY, wxEmptyString);
  SetTabletFont(error, 16);
  error->SetForegroundColour(wxColour(160, 30, 30));
  error->GetHandle()->setStyleSheet("QLabel { color: #a01e1e; font-size: 16pt; }");
  fields->Add(error, 0, wxEXPAND | wxALL, 12);
  content->SetSizer(fields);
  root->Add(content, 1, wxEXPAND);
  auto* actions = new wxBoxSizer(wxHORIZONTAL);
  auto* cancel = new wxButton(&dialog, wxID_CANCEL, _("Cancel"));
  auto* save = new wxButton(&dialog, wxID_ANY, _("Save position"));
  for (auto* button : {cancel, save}) {
    StyleTabletButton(button, button == save);
    button->SetMinSize(wxSize(0, 80));
    actions->Add(button, 1, wxEXPAND | wxALL, 12);
  }
  root->Add(actions, 0, wxEXPAND);
  dialog.SetSizer(root);
  WR_AddAndroidDoneHeader(&dialog, _("Position"),
      [&dialog]() { dialog.EndModal(wxID_CANCEL); }, _("Cancel"));
  cancel->Bind(wxEVT_BUTTON,
      [&dialog](wxCommandEvent&) { dialog.EndModal(wxID_CANCEL); });
  save->Bind(wxEVT_BUTTON, [&](wxCommandEvent&) {
    double lat, lon;
    wxString proposedName = positionName->GetValue();
    proposedName.Trim(true).Trim(false);
    if (proposedName.IsEmpty() ||
        !latitudeInput->GetValue().ToDouble(&lat) ||
        !longitudeInput->GetValue().ToDouble(&lon) ||
        !std::isfinite(lat) || !std::isfinite(lon) ||
        lat < -90 || lat > 90 || lon < -180 || lon > 180) {
      error->SetLabel(_("Enter a name, latitude from -90 to 90 and longitude from -180 to 180."));
      error->Wrap(content->GetClientSize().x - 32);
      content->Layout();
      content->FitInside();
      return;
    }
    // Renaming an existing position may retain its own name.
    for (const auto& position : RouteMap::Positions) {
      if (position.Name == proposedName && proposedName != name) {
        error->SetLabel(_("A position with this name already exists."));
        content->Layout();
        return;
      }
    }
    name = proposedName;
    latitude = lat;
    longitude = lon;
    accepted = true;
    dialog.EndModal(wxID_OK);
  });
  const wxSize canvas = GetCanvasByIndex(0)->GetClientSize();
  dialog.SetSize(canvas.x - 24, canvas.y - 24);
  dialog.CentreOnParent();
  dialog.ShowModal();
  return accepted;
}

void WeatherRouting::BeginAndroidChartPick(int target) {
  for (auto* route : CurrentRouteMaps(false)) {
    if (route->Running()) {
      WR_MessageBox(_("Wait for routing to finish or stop it before changing endpoints."),
                   _("Routing in progress"), wxOK | wxICON_INFORMATION, this);
      return;
    }
  }
  if (target >= 0 && CurrentRouteMaps().empty()) {
    wxCommandEvent event;
    OnNew(event);
  }
  m_ConfigurationDialog.Hide();
  m_androidChartPickTarget = target;
  m_androidChartPickPressed = false;
  if (!m_androidChartPickHint) {
    m_androidChartPickHint = new wxDialog(GetCanvasByIndex(0), wxID_ANY,
        _("Choose a position"), wxDefaultPosition, wxDefaultSize,
        wxBORDER_NONE | wxSTAY_ON_TOP);
    auto* row = new wxBoxSizer(wxHORIZONTAL);
    auto* instructions = new wxStaticText(m_androidChartPickHint, wxID_ANY,
        _("Tap the chart to choose. Drag to pan."));
    SetTabletFont(instructions, 17);
    row->Add(instructions, 1, wxALIGN_CENTER_VERTICAL | wxALL, 14);
    auto* cancel = new wxButton(m_androidChartPickHint, wxID_ANY, _("Cancel"));
    StyleTabletButton(cancel);
    cancel->SetMinSize(wxSize(150, 72));
    cancel->Bind(wxEVT_BUTTON,
        [this](wxCommandEvent&) { CancelAndroidChartPick(); });
    row->Add(cancel, 0, wxALL, 10);
    m_androidChartPickHint->SetSizer(row);
    WR_InstallAndroidBack(m_androidChartPickHint,
        [this]() { CancelAndroidChartPick(); }, GetCanvasByIndex(0));
  }
  const wxSize size = GetCanvasByIndex(0)->GetClientSize();
  m_androidChartPickHint->SetSize(12, 12, size.x - 24, 100);
  Hide();
  m_androidChartPickHint->Show();
  m_androidChartPickHint->Raise();
}

void WeatherRouting::CancelAndroidChartPick() {
  m_androidChartPickTarget = -2;
  m_androidChartPickPressed = false;
  if (m_androidChartPickHint) m_androidChartPickHint->Hide();
  Show(true);
  Raise();
}

bool WeatherRouting::HandleAndroidChartPick(wxMouseEvent& event,
                                           PlugIn_ViewPort* viewport) {
  if (!AndroidChartPickPending()) return false;
  if (event.LeftDown()) {
    m_androidChartPickDown = event.GetPosition();
    m_androidChartPickPressed = true;
  }
  if (!event.LeftUp() || !m_androidChartPickPressed) return false;
  m_androidChartPickPressed = false;
  const wxPoint movement = event.GetPosition() - m_androidChartPickDown;
  if (std::abs(movement.x) > 20 || std::abs(movement.y) > 20) return false;
  double latitude, longitude;
  GetCanvasLLPix(viewport, event.GetPosition(), &latitude, &longitude);
  if (!std::isfinite(latitude) || !std::isfinite(longitude)) return false;
  const int target = m_androidChartPickTarget;
  m_androidChartPickTarget = -2;
  m_androidChartPickHint->Hide();
  CallAfter([this, target, latitude, longitude]() {
    wxString name;
    double lat = latitude, lon = longitude;
    const bool saved = EditAndroidPosition(name, lat, lon);
    if (saved) AddPosition(lat, lon, name);
    Show(true);
    Raise();
    if (saved && target >= 0)
      m_ConfigurationDialog.SetAndroidEndpoint(target == 0, name);
  });
  // Let OpenCPN finish its pointer gesture, including clearing its drag state.
  return false;
}

#endif  // __OCPN__ANDROID__
