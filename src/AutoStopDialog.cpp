#include "AutoStopDialog.h"
#include "AddressSpaceMonitor.h"
#include <wx/sizer.h>
#include <wx/stattext.h>
#include <wx/button.h>
#include <wx/timer.h>
#include <wx/log.h>
#include <wx/msgdlg.h>


wxBEGIN_EVENT_TABLE(AutoStopDialog, wxDialog)
    EVT_TIMER(wxID_ANY, AutoStopDialog::OnTimer)
    EVT_BUTTON(wxID_OK, AutoStopDialog::OnOK)
wxEND_EVENT_TABLE()

AutoStopDialog::AutoStopDialog(wxWindow* parent, const wxString& message, int timeoutSeconds)
    : wxDialog(parent, wxID_ANY, _("WR AutoStop"), wxDefaultPosition, wxDefaultSize, wxDEFAULT_DIALOG_STYLE | wxSTAY_ON_TOP),
      m_timer(this), m_secondsLeft(timeoutSeconds)
{
    wxBoxSizer* sizer = new wxBoxSizer(wxVERTICAL);
    m_messageText = new wxStaticText(this, wxID_ANY, message, wxDefaultPosition, wxSize(400, -1));
    sizer->Add(m_messageText, 0, wxALL | wxEXPAND, 10);

    m_okButton = new wxButton(this, wxID_OK, wxString::Format(_("OK (%d)"), m_secondsLeft));
    sizer->Add(m_okButton, 0, wxALL | wxALIGN_CENTER, 10);

    // Add the Reset Memory Alerts button
    wxButton* btnResetMemoryAlerts = new wxButton(this, wxID_ANY, _("Reset Routes"));
    sizer->Add(btnResetMemoryAlerts, 0, wxALL | wxALIGN_CENTER, 10);
    btnResetMemoryAlerts->Bind(wxEVT_BUTTON, &AutoStopDialog::OnResetMemoryAlerts, this);
    ;

    SetSizerAndFit(sizer);

    wxLogMessage("AutoStopDialog position: %d,%d size: %d,%d", GetPosition().x,
                 GetPosition().y, GetSize().GetWidth(), GetSize().GetHeight());

    m_timer.Start(1000); // 1 second interval
}

void AutoStopDialog::OnTimer(wxTimerEvent& event) {
    m_secondsLeft--;
    m_okButton->SetLabel(wxString::Format(_("OK (%d)"), m_secondsLeft));
    if (m_secondsLeft <= 0) {
        m_timer.Stop();
        m_result = 0; // closed by timer
        EndModal(wxID_OK);
    }
}

void AutoStopDialog::OnOK(wxCommandEvent& event) {
    m_timer.Stop();
    m_result = 1; // closed by user
    EndModal(wxID_OK);
}

void AutoStopDialog::OnResetMemoryAlerts(wxCommandEvent& event) {
    if (m_addressSpaceMonitor) {m_addressSpaceMonitor->ResetMemoryAlertSystem();
        wxMessageBox(
            _("Memory usage alerting system has been reset. Monitoring will resume as normal."),
            _("Weather Routing - Memory Alert"), wxOK | wxICON_INFORMATION);
    }
}

void AutoStopDialog::SetAddressSpaceMonitor(AddressSpaceMonitor* monitor) {
    m_addressSpaceMonitor = monitor;
}
