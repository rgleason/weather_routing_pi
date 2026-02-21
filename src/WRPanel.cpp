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

  m_bCompute = new wxButton(this, wxID_ANY, _("Compute"));
  m_bComputeAll = new wxButton(this, wxID_ANY, _("Compute All"));
  m_bStop = new wxButton(this, wxID_ANY, _("Stop"));
  m_bReset = new wxButton(this, wxID_ANY, _("Reset"));
  m_bDelete = new wxButton(this, wxID_ANY, _("Delete"));
  m_bFilter = new wxButton(this, wxID_ANY, _("Filter"));

  buttonSizer->Add(m_bCompute, 0, wxALL, 5);
  buttonSizer->Add(m_bComputeAll, 0, wxALL, 5);
  buttonSizer->Add(m_bStop, 0, wxALL, 5);
  buttonSizer->Add(m_bReset, 0, wxALL, 5);
  buttonSizer->Add(m_bDelete, 0, wxALL, 5);
  buttonSizer->Add(m_bFilter, 0, wxALL, 5);

  topSizer->Add(buttonSizer, 0, wxALIGN_LEFT);

  // --- EVENT BINDINGS ---
  m_bCompute->Bind(wxEVT_BUTTON,
                   [this](wxCommandEvent& e) { m_wr.OnCompute(e); });

  m_bComputeAll->Bind(wxEVT_BUTTON,
                      [this](wxCommandEvent& e) { m_wr.OnComputeAll(e); });

  m_bStop->Bind(wxEVT_BUTTON, [this](wxCommandEvent& e) { m_wr.OnStop(e); });

  m_bReset->Bind(wxEVT_BUTTON,
                 [this](wxCommandEvent& e) { m_wr.OnResetSelected(e); });

  m_bDelete->Bind(wxEVT_BUTTON,
                  [this](wxCommandEvent& e) { m_wr.OnDelete(e); });

  m_bFilter->Bind(wxEVT_BUTTON,
                  [this](wxCommandEvent& e) { m_wr.OnFilter(e); });

  SetSizer(topSizer);
}

// --- PANEL API CALLED BY WeatherRouting ---

void WRPanel::SetStopButtonEnabled(bool enabled) {
  if (m_bStop) m_bStop->Enable(enabled);
}

void WRPanel::SetStatusText(const wxString& text) {
  // Add a wxStaticText later if desired
}
