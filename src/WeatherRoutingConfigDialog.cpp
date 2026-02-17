#include "WeatherRoutingConfigDialog.h"

wxBEGIN_EVENT_TABLE(WeatherRoutingConfigDialog, wxDialog)
    EVT_BUTTON(wxID_OK, WeatherRoutingConfigDialog::OnOK) wxEND_EVENT_TABLE()

        WeatherRoutingConfigDialog::WeatherRoutingConfigDialog(
            wxWindow* parent, WeatherRoute* route)
    : wxDialog(parent, wxID_ANY, _("Edit Routing Configuration"),
               wxDefaultPosition, wxDefaultSize,
               wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER),
      m_route(route) {
  wxBoxSizer* top = new wxBoxSizer(wxVERTICAL);

  // Example fields ? expand as needed
  m_txtBoatName = new wxTextCtrl(this, wxID_ANY, route->BoatName);
  m_txtStartLat =
      new wxTextCtrl(this, wxID_ANY, wxString::Format("%f", route->StartLat));
  m_txtStartLon =
      new wxTextCtrl(this, wxID_ANY, wxString::Format("%f", route->StartLon));

  top->Add(new wxStaticText(this, wxID_ANY, _("Boat Name")), 0, wxALL, 5);
  top->Add(m_txtBoatName, 0, wxEXPAND | wxALL, 5);

  top->Add(new wxStaticText(this, wxID_ANY, _("Start Latitude")), 0, wxALL, 5);
  top->Add(m_txtStartLat, 0, wxEXPAND | wxALL, 5);

  top->Add(new wxStaticText(this, wxID_ANY, _("Start Longitude")), 0, wxALL, 5);
  top->Add(m_txtStartLon, 0, wxEXPAND | wxALL, 5);

  top->Add(CreateSeparatedButtonSizer(wxOK | wxCANCEL), 0, wxEXPAND | wxALL,
           10);

  SetSizerAndFit(top);
}

void WeatherRoutingConfigDialog::OnOK(wxCommandEvent& event) {
  // Save changes back into the WeatherRoute
  m_route->BoatName = m_txtBoatName->GetValue();
  m_route->StartLat = wxAtof(m_txtStartLat->GetValue());
  m_route->StartLon = wxAtof(m_txtStartLon->GetValue());

  EndModal(wxID_OK);
}
