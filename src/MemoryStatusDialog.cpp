#include "MemoryStatusDialog.h"
#include <wx/sizer.h>
#include <wx/log.h>

wxBEGIN_EVENT_TABLE(MemoryStatusDialog, wxDialog)
    EVT_BUTTON(wxID_OK, MemoryStatusDialog::OnOK)
    EVT_TIMER(wxID_ANY, MemoryStatusDialog::OnTimer)
wxEND_EVENT_TABLE()

MemoryStatusDialog::MemoryStatusDialog(wxWindow* parent)
    : wxDialog(parent, wxID_ANY, _("Memory Status"),
               wxDefaultPosition, wxDefaultSize,
               wxDEFAULT_DIALOG_STYLE | wxSTAY_ON_TOP),
      m_timer(this)
{
    wxBoxSizer* sizer = new wxBoxSizer(wxVERTICAL);

    m_message = new wxStaticText(this, wxID_ANY, "", wxDefaultPosition, wxSize(400, -1));
    sizer->Add(m_message, 0, wxALL | wxEXPAND, 10);

    m_okButton = new wxButton(this, wxID_OK, _("OK"));
    sizer->Add(m_okButton, 0, wxALL | wxALIGN_CENTER, 5);

    m_hideButton = new wxButton(this, wxID_ANY, _("Hide"));
    sizer->Add(m_hideButton, 0, wxALL | wxALIGN_CENTER, 5);
    m_hideButton->Bind(wxEVT_BUTTON, &MemoryStatusDialog::OnHide, this);

    m_resetButton = new wxButton(this, wxID_ANY, _("Reset Routes"));
    sizer->Add(m_resetButton, 0, wxALL | wxALIGN_CENTER, 5);
    m_resetButton->Bind(wxEVT_BUTTON, &MemoryStatusDialog::OnReset, this);

    SetSizerAndFit(sizer);

    LoadPosition();
}

void MemoryStatusDialog::ApplyState(const MemoryDialogState& state)
{
    m_message->SetLabel(state.message);

    m_hideButton->Show(state.showHideButton);
    m_resetButton->Show(state.showResetRoutesButton);

    if (state.showCountdown) {
        m_secondsLeft = state.countdownSeconds;
        m_okButton->SetLabel(wxString::Format(_("OK (%d)"), m_secondsLeft));
        m_timer.Start(1000);
    } else {
        m_timer.Stop();
        m_okButton->SetLabel(_("OK"));
    }

    Layout();
    Fit();
}

void MemoryStatusDialog::OnHide(wxCommandEvent&)
{
    if (onHide) onHide();
    SavePosition();
    Hide();
}

void MemoryStatusDialog::OnReset(wxCommandEvent&)
{
    if (onResetRoutes) onResetRoutes();
    // For AlertStop, we do NOT auto-hide here; user can choose Hide explicitly.
}

void MemoryStatusDialog::OnOK(wxCommandEvent&)
{
    m_timer.Stop();
    SavePosition();
    Hide();
}

void MemoryStatusDialog::OnTimer(wxTimerEvent&)
{
    m_secondsLeft--;
    m_okButton->SetLabel(wxString::Format(_("OK (%d)"), m_secondsLeft));

    if (m_secondsLeft <= 0) {
        m_timer.Stop();
        if (onCountdownFinished) onCountdownFinished();
        SavePosition();
        Hide();
    }
}

void MemoryStatusDialog::LoadPosition()
{
    wxConfigBase* c = wxConfig::Get();
    if (!c) return;

    long x = -1, y = -1;
    if (c->Read("/PlugIns/WeatherRouting/MemoryDialogX", &x, -1) &&
        c->Read("/PlugIns/WeatherRouting/MemoryDialogY", &y, -1) &&
        x >= 0 && y >= 0)
    {
        Move(wxPoint((int)x, (int)y));
    }
}

void MemoryStatusDialog::SavePosition()
{
    wxConfigBase* c = wxConfig::Get();
    if (!c) return;

    wxPoint p = GetPosition();
    c->Write("/PlugIns/WeatherRouting/MemoryDialogX", (long)p.x);
    c->Write("/PlugIns/WeatherRouting/MemoryDialogY", (long)p.y);
}
