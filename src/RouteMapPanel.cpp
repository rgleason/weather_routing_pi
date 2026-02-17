#include "RouteMapPanel.h"
#include "WeatherRouting.h"

RouteMapPanel::RouteMapPanel(wxWindow* parent, WeatherRouting& wr)
    : wxPanel(parent), m_WR(wr) {

  // 1. Set up the list control with columns for route details
  wxBoxSizer* sizer = new wxBoxSizer(wxVERTICAL);

  // 2. Create the list control for displaying weather routes
  m_lWeatherRoutes = new wxListCtrl(this, wxID_ANY, wxDefaultPosition, wxDefaultSize,
                     wxLC_REPORT | wxLC_SINGLE_SEL);

  // 3. Bind double-click event to open route details
  m_lWeatherRoutes->Bind(wxEVT_LIST_ITEM_ACTIVATED, &RouteMapPanel::OnRouteActivated, this);

  // Bind left-click event to toggle route visibility
  m_lWeatherRoutes->Bind(wxEVT_LEFT_DOWN, &RouteMapPanel::OnLeftClick, this);

  // Bind right-click event to show context menu
  m_lWeatherRoutes->Bind(wxEVT_CONTEXT_MENU, &RouteMapPanel::OnContextMenu, this);

  // Bind column resize event to save new widths
  m_lWeatherRoutes->Bind(wxEVT_LIST_COL_END_DRAG, &RouteMapPanel::OnColumnResize, this);


  // 4. Define columns for the list control
  m_lWeatherRoutes->InsertColumn(0, _("Vis"));
  m_lWeatherRoutes->InsertColumn(1, _("Boat"));
  m_lWeatherRoutes->InsertColumn(2, _("Start Type"));
  m_lWeatherRoutes->InsertColumn(3, _("Start"));
  m_lWeatherRoutes->InsertColumn(4, _("Start Time"));
  m_lWeatherRoutes->InsertColumn(5, _("End"));
  m_lWeatherRoutes->InsertColumn(6, _("End Time (UTC)"));
  m_lWeatherRoutes->InsertColumn(7, _("Time"));
  m_lWeatherRoutes->InsertColumn(8, _("Distance"));
  m_lWeatherRoutes->InsertColumn(9, _("State"));

  // 5. Add the list control to the sizer and set the sizer for the panel
  sizer->Add(m_lWeatherRoutes, 1, wxEXPAND | wxALL, 0);
  SetSizer(sizer);
}


// Method to populate the list control with current routes from WeatherRouting
// This should be called whenever routes are added, removed, or updated to
// refresh the display
// FIXED: m_WR is a reference, not a pointer, so use dot operator
// FIXED: Use modern wxListCtrl methods to update only changed rows instead of
// full refresh
// MODERN

  void RouteMapPanel::PopulateRoutes() {
  // Clear existing rows
  m_lWeatherRoutes->DeleteAllItems();

  // Retrieve routes from WeatherRouting
  const auto& routes = m_WR.m_WeatherRoutes;

  long index = 0;
  for (auto* route : routes) {
    if (!route) continue;

    // ---------------------------------------------------------
    // Column 0: Visibility checkbox
    // ---------------------------------------------------------
    wxString vis = route->visible ? _("X") : _("");
    long row = m_lWeatherRoutes->InsertItem(index, vis);

    // ---------------------------------------------------------
    // Column 1: Boat
    // ---------------------------------------------------------
    m_lWeatherRoutes->SetItem(row, 1, route->BoatName);

    // ---------------------------------------------------------
    // Column 2: Start Type
    // ---------------------------------------------------------
    m_lWeatherRoutes->SetItem(row, 2, route->StartTypeString());

    // ---------------------------------------------------------
    // Column 3: Start (Position name or lat/lon)
    // ---------------------------------------------------------
    m_lWeatherRoutes->SetItem(row, 3, route->StartString());

    // ---------------------------------------------------------
    // Column 4: Start Time
    // ---------------------------------------------------------
    m_lWeatherRoutes->SetItem(row, 4, route->StartTimeString());

    // ---------------------------------------------------------
    // Column 5: End (Destination)
    // ---------------------------------------------------------
    m_lWeatherRoutes->SetItem(row, 5, route->EndString());

    // ---------------------------------------------------------
    // Column 6: End Time (UTC)
    // ---------------------------------------------------------
    m_lWeatherRoutes->SetItem(row, 6, route->EndTimeString());

    // ---------------------------------------------------------
    // Column 7: Time (duration)
    // ---------------------------------------------------------
    m_lWeatherRoutes->SetItem(row, 7, route->DurationString());

    // ---------------------------------------------------------
    // Column 8: Distance
    // ---------------------------------------------------------
    m_lWeatherRoutes->SetItem(row, 8, route->DistanceString());

    // ---------------------------------------------------------
    // Column 9: State
    // ---------------------------------------------------------
    m_lWeatherRoutes->SetItem(row, 9, route->StateString());

    // Store pointer for double?click / edit / delete / compute
    m_lWeatherRoutes->SetItemPtrData(row, (wxUIntPtr)route);

    index++;
  }

  // Autosize columns for readability
  for (int col = 0; col < 10; col++)
    m_lWeatherRoutes->SetColumnWidth(col, wxLIST_AUTOSIZE);

  // Apply saved widths (user overrides autosize)
  for (int col = 0; col < 10; col++) {
    int w = m_WR.LoadColumnWidth(col);  // FIXED: m_WR is a reference
    if (w > 0) m_lWeatherRoutes->SetColumnWidth(col, w);
  }
}


void RouteMapPanel::OnLeftClick(wxMouseEvent& event) {
  long flags;
  long row = m_lWeatherRoutes->HitTest(event.GetPosition(), flags);

  if (row >= 0) {
    int col = m_lWeatherRoutes->GetColumnFromPoint(event.GetPosition());

    // Column 0 = visibility toggle
    if (col == 0) {
      WeatherRoute* wr =
          reinterpret_cast<WeatherRoute*>(m_lWeatherRoutes->GetItemData(row));

      if (wr) {
        wr->visible = !wr->visible;

        if (wr->routemapoverlay) wr->routemapoverlay->SetVisible(wr->visible);

        // Update only this row (modern call)
        m_WR.UpdateItem(row, false);

        // Redraw chart
        if (GetParent()) GetParent()->Refresh();
      }
    }
  }

  event.Skip();
}


void RouteMapPanel::OnContextMenu(wxContextMenuEvent& event) {
  wxMenu menu;

  menu.Append(ID_ROUTING_EDIT, _("Edit"));
  menu.Append(ID_ROUTING_COMPUTE, _("Compute"));
  menu.Append(ID_ROUTING_RESET, _("Reset"));
  menu.Append(ID_ROUTING_STOP, _("Stop"));
  menu.AppendSeparator();
  menu.Append(ID_ROUTING_SAVE_TRACK, _("Save as Track"));
  menu.Append(ID_ROUTING_SAVE_ROUTE, _("Save as Route"));
  menu.AppendSeparator();
  menu.Append(ID_ROUTING_DELETE, _("Delete"));

  PopupMenu(&menu);
}

void RouteMapPanel::OnColumnResize(wxListEvent& event) {
  int col = event.GetColumn();
  int width = m_lWeatherRoutes->GetColumnWidth(col);

  m_WR.SaveColumnWidth(col, width);
}



