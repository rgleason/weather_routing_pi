#include "WRPanel.h"
#include "WeatherRouting.h"

WRPanel::WRPanel(wxWindow* parent, WeatherRouting& wr)
    : wxPanel(parent), m_wr(wr) {
  wxBoxSizer* topSizer = new wxBoxSizer(wxVERTICAL);

  // --- ROUTE LIST ---
  m_lWeatherRoutes =
      new wxListCtrl(this, wxID_ANY, wxDefaultPosition, wxDefaultSize,
                     wxLC_REPORT | wxLC_SINGLE_SEL | wxLC_HRULES | wxLC_VRULES);

  topSizer->Add(m_lWeatherRoutes, 1, wxEXPAND | wxALL, 5);

  // --- COMMAND BUTTON ROW ---
  wxBoxSizer* buttonSizer = new wxBoxSizer(wxHORIZONTAL);

  buttonSizer->Add(new wxButton(this, wxID_ANY, _("Compute")), 0, wxALL, 5);
  buttonSizer->Add(new wxButton(this, wxID_ANY, _("Compute All")), 0, wxALL, 5);
  buttonSizer->Add(new wxButton(this, wxID_ANY, _("Stop")), 0, wxALL, 5);
  buttonSizer->Add(new wxButton(this, wxID_ANY, _("Reset")), 0, wxALL, 5);
  buttonSizer->Add(new wxButton(this, wxID_ANY, _("Delete")), 0, wxALL, 5);
  buttonSizer->Add(new wxButton(this, wxID_ANY, _("Filter")), 0, wxALL, 5);

  topSizer->Add(buttonSizer, 0, wxALIGN_LEFT);

  SetSizer(topSizer);
}
