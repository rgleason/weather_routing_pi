#include <wx/defs.h>

#ifdef __WXMSW__

#include "AddressSpaceMonitor.h"
#include <wx/sizer.h>
#include <wx/button.h>
#include <wx/log.h>
#include <windows.h>

// ADD: Static counter at file scope (for tracking multiple instances)
static int s_instanceCount = 0;
static int s_instanceId = 0;
static bool s_loggingInitialized = false;  // ADD THIS

// MemoryAlertDialog implementation
MemoryAlertDialog::MemoryAlertDialog(wxWindow* parent,
                                     AddressSpaceMonitor* monitor)
    : wxDialog(parent, wxID_ANY, _("Address Space Warning"), wxDefaultPosition,
               wxDefaultSize, wxDEFAULT_DIALOG_STYLE | wxSTAY_ON_TOP),
      m_monitor(monitor),
      m_messageText(nullptr) {  // Initialize to nullptr for safety
  wxBoxSizer* mainSizer = new wxBoxSizer(wxVERTICAL);

  // Create the message text (will be updated)
  m_messageText =
      new wxStaticText(this, wxID_ANY, "", wxDefaultPosition, wxSize(400, -1));
  mainSizer->Add(m_messageText, 0, wxALL | wxEXPAND, 10);

  // Change to Hide button
  wxButton* hideButton = new wxButton(this, wxID_ANY, _("Hide"));
  mainSizer->Add(hideButton, 0, wxALL | wxALIGN_CENTER, 10);

  SetSizer(mainSizer);

  // Connect events
  hideButton->Bind(wxEVT_BUTTON, &MemoryAlertDialog::OnHide, this);
  Bind(wxEVT_CLOSE_WINDOW, &MemoryAlertDialog::OnCloseWindow, this);

  Layout();
  Fit();
  Centre();
}

void MemoryAlertDialog::UpdateMemoryInfo(double usedGB, double totalGB,
                                         double percent) {
  // Guard against accessing destroyed monitor
  if (!m_monitor) {
    wxLogMessage("MemoryAlertDialog: Attempted update after monitor destroyed");
    return;
  }

  // Guard against null message text (defensive programming)
  if (!m_messageText) {
    wxLogWarning("MemoryAlertDialog: m_messageText is null");
    return;
  }

  wxString message = wxString::Format(
      _("WARNING: Address space used: %.1f%% (%.2f GiB / %.1f GiB)\n"
        "OpenCPN + Pi address threshold: %.0f%%\n\n"
        "To release routing results:\n"
        "Select WR_Pi - Routing - Reset All\n"
        "This may free address space."),
      percent, usedGB, totalGB, m_monitor->thresholdPercent);

  m_messageText->SetLabel(message);
  m_messageText->Wrap(380);
  Layout();
  Fit();
}

void MemoryAlertDialog::OnHide(wxCommandEvent& event) {
  // User clicked Hide button - just hide, don't dismiss
  // Monitoring continues and alert will reappear if threshold exceeded
  if (m_monitor) {
    wxLogMessage(
        "MemoryAlertDialog: User clicked Hide - continuing to monitor");
  }
  Hide();  // Don't destroy, just hide
}

void MemoryAlertDialog::OnClose(wxCommandEvent& event) {
  // This is for programmatic close (e.g., from Settings checkbox)
  // Set alertDismissed to prevent re-showing until memory drops
  if (m_monitor) {
    m_monitor->alertDismissed = true;
    wxLogMessage("MemoryAlertDialog: Alert dismissed via Settings");
  }
  Hide();  // Don't destroy, just hide
}

void MemoryAlertDialog::OnCloseWindow(wxCloseEvent& event) {
  // User closed via X button - treat as Hide (not dismiss)
  // This way user can still get alerts if memory gets worse
  if (m_monitor) {
    wxLogMessage(
        "MemoryAlertDialog: User closed window via X - continuing to monitor");
  }
  Hide();        // Don't destroy, just hide
  event.Veto();  // Prevent actual destruction
}

void MemoryAlertDialog::ClearMonitor() {
  m_monitor = nullptr;
  wxLogMessage("MemoryAlertDialog: Monitor reference cleared");
}

AddressSpaceMonitor::AddressSpaceMonitor()
    : m_isValid(true),
      m_isShuttingDown(false),
      thresholdPercent(80.0),
      logToFile(false),
      alertEnabled(true),
      usageGauge(nullptr),
      m_textLabel(nullptr),
      alertShown(false),
      alertDismissed(false),
      activeAlertDialog(nullptr),
      m_instanceId(++s_instanceId) {  // ADD: Unique ID for each instance

  ++s_instanceCount;

  // ADD: Force logging initialization check
  if (!s_loggingInitialized) {
    s_loggingInitialized = true;
    wxLogMessage(
        "=== AddressSpaceMonitor: FIRST INSTANCE LOGGING INITIALIZED ===");
  }

  // FIX: Cast thread ID to unsigned long for consistent formatting
  wxThreadIdType threadId = wxThread::GetCurrentId();
  wxLogMessage(
      "AddressSpaceMonitor: Constructor - Instance #%d created (total active: "
      "%d) - Thread ID: %lu",
      m_instanceId, s_instanceCount, (unsigned long)threadId);

  // ADD THIS: Log if this is being called during static initialization
  // Check wxApp and thread
  if (!wxTheApp) {
    wxLogWarning(
        "AddressSpaceMonitor: Instance #%d created BEFORE wxApp exists!",
        m_instanceId);
  } else {
    wxLogMessage(
        "AddressSpaceMonitor: Instance #%d created WITH wxApp, thread ID: %lu",
        m_instanceId, (unsigned long)threadId);
  }
}

AddressSpaceMonitor::~AddressSpaceMonitor() {
  --s_instanceCount;
  wxLogMessage(
      "AddressSpaceMonitor: Destructor called - Instance #%d (remaining "
      "active: %d)",
      m_instanceId, s_instanceCount);

  // Ensure shutdown has been called
  if (m_isValid && !m_isShuttingDown) {
    Shutdown();
  }

  wxLogMessage("AddressSpaceMonitor: Destructor complete - Instance #%d",
               m_instanceId);
}

void AddressSpaceMonitor::Shutdown() {
  // ADD: Prevent multiple shutdown calls
  if (m_isShuttingDown) {
    wxLogMessage(
        "AddressSpaceMonitor: Shutdown already in progress for Instance #%d",
        m_instanceId);
    return;
  }

  m_isShuttingDown = true;

  wxLogMessage(
      "AddressSpaceMonitor: Shutdown called - Instance #%d cleaning up "
      "resources",
      m_instanceId);

  // Mark as invalid immediately to prevent any new operations
  m_isValid = false;

  // Clear gauge reference first to prevent updates during shutdown
  usageGauge = nullptr;
  m_textLabel = nullptr;

  // Close and destroy alert dialog
  CloseAlert();

  wxLogMessage("AddressSpaceMonitor: Shutdown complete - Instance #%d",
               m_instanceId);
}

void AddressSpaceMonitor::DismissAlert() {
  if (!m_isValid) {
    wxLogWarning(
        "AddressSpaceMonitor::DismissAlert() called on invalid object");
    return;
  }

  alertDismissed = true;

  // Hide any active dialog
  if (activeAlertDialog && activeAlertDialog->IsShown()) {
    activeAlertDialog->Hide();
    wxLogMessage("AddressSpaceMonitor: Alert dismissed and dialog hidden");
  } else {
    wxLogMessage("AddressSpaceMonitor: Alert dismissed (no active dialog)");
  }
}

void AddressSpaceMonitor::CheckAndAlert() {
  // Guard against use after destruction
  if (!m_isValid) {
    wxLogWarning(
        "AddressSpaceMonitor::CheckAndAlert() called on invalid object");
    return;
  }

  // Prevent re-entrant calls
  static bool isExecuting = false;
  if (isExecuting) {
    wxLogMessage("AddressSpaceMonitor: Skipping re-entrant call");
    return;
  }
  isExecuting = true;

  const auto space = GetAddressSpace();
  if (!space) {
    if (m_textLabel) m_textLabel->SetLabel(_("Address space unavailable"));
    if (usageGauge) usageGauge->SetValue(0);
    if (activeAlertDialog && activeAlertDialog->IsShown())
      activeAlertDialog->Hide();
    if (logToFile)
      wxLogWarning("AddressSpaceMonitor: process address-space query failed");
    isExecuting = false;
    return;
  }
  const auto used = space->UsedBytes();
  const auto total = space->total;
  const double percent = space->UsedPercent();

  double usedGB = used / (1024.0 * 1024.0 * 1024.0);
  double totalGB = total / (1024.0 * 1024.0 * 1024.0);

//  Update text label if connected
  if (m_textLabel) {
    wxString stats = wxString::Format("%.1f%% (%.2f GiB / %.1f GiB)", percent,
                                      usedGB, totalGB);
    m_textLabel->SetLabel(stats);

    // Set color based on threshold
    wxColour textColor;
    if (percent >= thresholdPercent) {
      textColor = *wxRED;
    } else if (percent >= 70.0) {
      textColor = wxColour(255, 140, 0);  // Dark orange
    } else {
      textColor = wxColour(0, 128, 0);  // Dark green
    }
    m_textLabel->SetForegroundColour(textColor);
    m_textLabel->Refresh();
  }

  if (logToFile) {
    wxLogMessage("AddressSpaceMonitor: reserved+committed=%.2f GiB "
                 "total=%.1f GiB (%.1f%%) process_bits=%u",
                 usedGB, totalGB, percent,
                 static_cast<unsigned>(sizeof(void*) * 8));
  }

  UpdateAlertIfShown(usedGB, totalGB, percent);

  // Update gauge if it exists and is valid
  if (usageGauge) {
    try {
      usageGauge->SetValue(static_cast<int>(percent));
    } catch (...) {
      wxLogWarning(
          "AddressSpaceMonitor: Exception accessing gauge, clearing reference");
      usageGauge = nullptr;
    }
  }

  isExecuting = false;
}

void AddressSpaceMonitor::UpdateAlertIfShown(double usedGB, double totalGB,
                                             double percent) {
  // Guard against use after destruction
  if (!m_isValid) {
    wxLogWarning(
        "AddressSpaceMonitor::UpdateAlertIfShown() called on invalid object");
    return;
  }

  // Check if user dismissed alerts via Settings checkbox
  if (alertDismissed) {
    // Only re-enable if memory drops significantly below threshold
    if (percent < thresholdPercent - 5.0) {
      alertDismissed = false;
      wxLogMessage(
          "AddressSpaceMonitor: Memory dropped below threshold (%.1f%%), "
          "re-enabling alerts",
          percent);
    }
    return;  // Don't show alerts while dismissed
  }

  // Show/update alert if above threshold
  if (percent >= thresholdPercent && alertEnabled) {
    ShowOrUpdateAlert(usedGB, totalGB, percent);
  } else if (percent < thresholdPercent && activeAlertDialog &&
             activeAlertDialog->IsShown()) {
    // Memory dropped below threshold - hide the dialog
    activeAlertDialog->Hide();
    wxLogMessage(
        "AddressSpaceMonitor: Memory dropped below threshold (%.1f%%), hiding "
        "alert",
        percent);
  }
}

void AddressSpaceMonitor::ShowOrUpdateAlert(double usedGB, double totalGB,
                                            double percent) {
  if (!activeAlertDialog) {
    // Create new dialog
    activeAlertDialog = new MemoryAlertDialog(nullptr, this);
    if (!activeAlertDialog) {
      wxLogError("AddressSpaceMonitor: Failed to create alert dialog");
      return;
    }
    activeAlertDialog->UpdateMemoryInfo(usedGB, totalGB, percent);
    activeAlertDialog->Show();
    alertShown = true;

    wxLogMessage("AddressSpaceMonitor: Showing new alert dialog");
  } else if (activeAlertDialog->IsShown()) {
    // Update existing visible dialog
    activeAlertDialog->UpdateMemoryInfo(usedGB, totalGB, percent);
  } else {
    // Dialog exists but is hidden - show it again since we're over threshold
    activeAlertDialog->UpdateMemoryInfo(usedGB, totalGB, percent);
    activeAlertDialog->Show();
    wxLogMessage("AddressSpaceMonitor: Re-showing alert dialog (usage: %.1f%%)",
                 percent);
  }
}

void AddressSpaceMonitor::CloseAlert() {
  if (activeAlertDialog) {
    wxLogMessage("AddressSpaceMonitor: Closing alert dialog");

    // Clear the monitor reference in the dialog first
    activeAlertDialog->ClearMonitor();

    // Hide before destroying to prevent visual glitches
    if (activeAlertDialog->IsShown()) {
      activeAlertDialog->Hide();
    }

    // Destroy the dialog
    activeAlertDialog->Destroy();
    activeAlertDialog = nullptr;

    wxLogMessage("AddressSpaceMonitor: Alert dialog destroyed");
  }

  alertShown = false;
  alertDismissed = false;
}

void AddressSpaceMonitor::SetThresholdPercent(double percent) {
  if (!m_isValid) {
    wxLogWarning(
        "AddressSpaceMonitor::SetThresholdPercent() called on invalid object");
    return;
  }

  double oldThreshold = thresholdPercent;
  thresholdPercent = percent;
  wxLogMessage("AddressSpaceMonitor: Threshold changed from %.1f%% to %.1f%%",
               oldThreshold, percent);

  // If dialog is shown and we're now below the new threshold, hide it
  if (activeAlertDialog && activeAlertDialog->IsShown()) {
    const auto space = GetAddressSpace();
    if (!space) return;
    const double currentPercent = space->UsedPercent();
    if (currentPercent < thresholdPercent) {
      activeAlertDialog->Hide();
      wxLogMessage(
          "AddressSpaceMonitor: Hiding alert - usage (%.1f%%) below new "
          "threshold (%.1f%%)",
          currentPercent, thresholdPercent);
    } else {
      // Update the dialog to show the new threshold
      double usedGB = space->UsedBytes() / (1024.0 * 1024.0 * 1024.0);
      double totalGB = space->total / (1024.0 * 1024.0 * 1024.0);
      activeAlertDialog->UpdateMemoryInfo(usedGB, totalGB, currentPercent);
    }
  }
}

void AddressSpaceMonitor::SetLoggingEnabled(bool enabled) {
  if (!m_isValid) {
    wxLogWarning(
        "AddressSpaceMonitor::SetLoggingEnabled() called on invalid object");
    return;
  }
  logToFile = enabled;
  wxLogMessage("AddressSpaceMonitor: Logging %s",
               enabled ? "enabled" : "disabled");
}

void AddressSpaceMonitor::SetAlertEnabled(bool enabled) {
  if (!m_isValid) {
    wxLogWarning(
        "AddressSpaceMonitor::SetAlertEnabled() called on invalid object");
    return;
  }
  alertEnabled = enabled;
  if (!enabled) {
    CloseAlert();
  }
  wxLogMessage("AddressSpaceMonitor: Alerts %s",
               enabled ? "enabled" : "disabled");
}

void AddressSpaceMonitor::SetGauge(wxGauge* gauge) {
  if (!m_isValid) {
    wxLogWarning("AddressSpaceMonitor::SetGauge() called on invalid object");
    return;
  }
  usageGauge = gauge;
  if (gauge) {
    wxLogMessage("AddressSpaceMonitor: Gauge connected");
  } else {
    wxLogMessage(
        "AddressSpaceMonitor: Gauge disconnected (cleared to prevent dangling "
        "pointer)");
  }
}

std::optional<weather_routing::ProcessAddressSpace>
AddressSpaceMonitor::GetAddressSpace() const {
  if (!m_isValid) return std::nullopt;
  return weather_routing::QueryProcessAddressSpace();
}

void AddressSpaceMonitor::SetTextLabel(wxStaticText* label) {
  if (!m_isValid) {
    wxLogWarning(
        "AddressSpaceMonitor::SetTextLabel() called on invalid object");
    return;
  }
  m_textLabel = label;
  if (label) {
    wxLogMessage("AddressSpaceMonitor: Text label connected");
  } else {
    wxLogMessage("AddressSpaceMonitor: Text label disconnected");
  }
}

#endif  // __WXMSW__
