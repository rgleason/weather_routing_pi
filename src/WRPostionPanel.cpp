#include "WRPositionPanel.h"
#include "WeatherRouting.h"

WRPositionPanel::WRPositionPanel(wxWindow* parent,
                                                         WeatherRouting& wr)
    : wxPanel(parent), m_WR(wr) {
  wxBoxSizer* sizer = new wxBoxSizer(wxVERTICAL);

  m_lPositions = new wxListCtrl(this, wxID_ANY, wxDefaultPosition,
                                wxDefaultSize, wxLC_REPORT | wxLC_SINGLE_SEL);

  m_lPositions->InsertColumn(0, _("Position"));
  m_lPositions->InsertColumn(1, _("Lat"));
  m_lPositions->InsertColumn(2, _("Lon"));

  sizer->Add(m_lPositions, 1, wxEXPAND | wxALL, 0);
  SetSizer(sizer);
}


void WRPositionPanel::PopulatePositions() {
  // Clear existing rows
  m_lPositions->DeleteAllItems();

  // Retrieve positions from WeatherRouting
  const auto& positions = m_WR.m_Positions;

  long index = 0;
  for (auto* pos : positions) {
    if (!pos) continue;

    // ---------------------------------------------------------
    // Column 0: Position Name
    // ---------------------------------------------------------
    index = m_lPositions->InsertItem(index, pos->Name);

    // ---------------------------------------------------------
    // Column 1: Latitude
    // ---------------------------------------------------------
    m_lPositions->SetItem(index, 1, pos->LatitudeString());

    // ---------------------------------------------------------
    // Column 2: Longitude
    // ---------------------------------------------------------
    m_lPositions->SetItem(index, 2, pos->LongitudeString());

    // Store pointer for selection/edit/delete
    m_lPositions->SetItemPtrData(index, (wxUIntPtr)pos);

    index++;
  }

  // Autosize columns for readability
  for (int col = 0; col < 3; col++)
    m_lPositions->SetColumnWidth(col, wxLIST_AUTOSIZE);
}
