#pragma once
#include <wx/dialog.h>
#include <wx/sizer.h>
#include <wx/timer.h>
#include <wx/stattext.h>
#include <wx/button.h>

class AutoStopDialog : public wxDialog {
public:
    AutoStopDialog(wxWindow* parent, const wxString& message, int timeoutSeconds = 15);
    int GetResult() const { return m_result; }

private:
    void OnTimer(wxTimerEvent& event);
    void OnOK(wxCommandEvent& event);

    wxStaticText* m_messageText;
    wxTimer m_timer;
    int m_secondsLeft;
    wxButton* m_okButton;

    int m_result = -1; // -1: not closed, 0: closed by timer, 1: closed by user

    wxDECLARE_EVENT_TABLE();
};
