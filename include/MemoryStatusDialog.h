#pragma once
#include <wx/dialog.h>
#include <wx/stattext.h>
#include <wx/button.h>
#include <wx/timer.h>
#include <wx/config.h>
#include <functional>

enum class MemoryDialogMode {
    AlertStop,
    AutoReset
};

class MemoryStatusDialog : public wxDialog {
public:
  MemoryStatusDialog(wxWindow* parent, MemoryDialogMode mode);

private:
  MemoryDialogMode m_mode;
  void BuildLayout();
  void AddAlertStopButtons(wxSizer* s);
  void AddAutoResetButtons(wxSizer* s);
};

struct MemoryDialogState {
    MemoryDialogMode mode;
    wxString message;

    bool showHideButton = false;
    bool showResetRoutesButton = false;
    bool showCountdown = false;
    int countdownSeconds = 0;
};

class MemoryStatusDialog : public wxDialog {
public:
    MemoryStatusDialog(wxWindow* parent);

    void ApplyState(const MemoryDialogState& state);

    std::function<void()> onHide;
    std::function<void()> onResetRoutes;
    std::function<void()> onCountdownFinished;

private:
    void OnHide(wxCommandEvent&);
    void OnReset(wxCommandEvent&);
    void OnOK(wxCommandEvent&);
    void OnTimer(wxTimerEvent&);

    void LoadPosition();
    void SavePosition();

    wxStaticText* m_message = nullptr;
    wxButton* m_hideButton = nullptr;
    wxButton* m_resetButton = nullptr;
    wxButton* m_okButton = nullptr;

    wxTimer m_timer;
    int m_secondsLeft = 0;

    wxDECLARE_EVENT_TABLE();
};
