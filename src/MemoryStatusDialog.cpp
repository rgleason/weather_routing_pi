#include "MemoryStatusDialog.h"
#include <wx/sizer.h>
#include <wx/stattext.h>
#include <wx/button.h>

wxBEGIN_EVENT_TABLE(MemoryStatusDialog, wxDialog)
  EVT_BUTTON(wxID_RESET, MemoryStatusDialog::OnReset)
  EVT_BUTTON(wxID_OK, MemoryStatusDialog::OnOK)
  EVT_TIMER(wxID_ANY, MemoryStatusDialog::OnTimer)
wxEND_EVENT_TABLE()

MemoryStatusDialog::MemoryStatusDialog(wxWindow* parent, MemoryDialogMode mode)
    : wxDialog(parent, wxID_ANY, _("Memory Status"), wxDefaultPosition,
               wxDefaultSize, wxDEFAULT_DIALOG_STYLE | wxSTAY_ON_TOP),
      m_mode(mode) {
  BuildLayout();
}

void MemoryStatusDialog::BuildLayout() {
  wxBoxSizer* main = new wxBoxSizer(wxVERTICAL);

  if (m_mode == MemoryDialogMode::AlertStop) {
    main->Add(new wxStaticText(
                  this, wxID_ANY,
                  _("Memory is critically low.\nRouting has been stopped.")),
              0, wxALL | wxALIGN_CENTER, 10);

    AddAlertStopButtons(main);
  } else {
    main->Add(
        new wxStaticText(this, wxID_ANY,
                         _("Memory has recovered.\nRoutes have been reset.")),
        0, wxALL | wxALIGN_CENTER, 10);

    AddAutoResetButtons(main);
  }

  SetSizerAndFit(main);
}

void MemoryStatusDialog::AddAlertStopButtons(wxSizer* s) {
  wxStdDialogButtonSizer* bs = new wxStdDialogButtonSizer();

  wxButton* reset = new wxButton(this, wxID_RESET, _("Reset"));
  reset->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { EndModal(wxID_RESET); });
  bs->AddButton(reset);

  wxButton* ok = new wxButton(this, wxID_OK, _("OK"));
  bs->AddButton(ok);

  bs->Realize();
  s->Add(bs, 0, wxALIGN_CENTER | wxALL, 10);
}

void MemoryStatusDialog::AddAutoResetButtons(wxSizer* s) {
  wxStdDialogButtonSizer* bs = new wxStdDialogButtonSizer();

  wxButton* ok = new wxButton(this, wxID_OK, _("OK"));
  bs->AddButton(ok);

  bs->Realize();
  s->Add(bs, 0, wxALIGN_CENTER | wxALL, 10);
}

void MemoryStatusDialog::OnReset(wxCommandEvent& event) {
  EndModal(wxID_RESET);
}

void MemoryStatusDialog::OnOK(wxCommandEvent& event) {
    EndModal(wxID_OK);
}

void MemoryStatusDialog::OnTimer(wxTimerEvent& event) {
    // If you don?t need timer behavior yet, leave this empty
}
