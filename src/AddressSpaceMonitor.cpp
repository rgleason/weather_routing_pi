#ifdef __WXMSW__

#include "AddressSpaceMonitor.h"
#include "AutoStopDialog.h"
#include <wx/sizer.h>
#include <wx/button.h>
#include <wx/log.h>
#include <wx/event.h>
#include <wx/config.h>
#include <wx/app.h> // Ensure this is included for wxTheApp
#include <windows.h>
#include "WeatherRouting.h"
#include <mutex>
#include <atomic> // Add this include at the top of your file

// ADD: Static counter at file scope (for tracking multiple instances)
static int s_instanceCount = 0;
static int s_instanceId = 0;
static bool s_loggingInitialized = false;  // ADD THIS

class AutoStopDialog;  /// Forward declaration  try to remove if possible   


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

MemoryAlertDialog::~MemoryAlertDialog() {
  // Disconnect all event handlers
  Unbind(wxEVT_BUTTON, &MemoryAlertDialog::OnHide, this);
  Unbind(wxEVT_CLOSE_WINDOW, &MemoryAlertDialog::OnCloseWindow, this);
  // Add other Unbind/Disconnect calls if you have more event handlers
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
      _("WARNING: \n\n"
        "Route Computing Stopped,\n\n"
        "Current Use: %.1f%% (%.2f GB / %.1f GB)\n\n"
        "Alert threshold: %.0f%%\n\n"
        "Reset to Compute."),
      percent, usedGB, totalGB, m_monitor->GetAlertThreshold());

  m_messageText->SetLabel(message);
  m_messageText->Wrap(380);
  Layout();
  Fit();
}

void MemoryAlertDialog::OnHide(wxCommandEvent& event) {
  // User clicked Hide button
  // Monitoring continues and alert will reappear if threshold drops below percent
  // and then goes above alertthreshold again
  if (m_monitor) {
    wxLogMessage(
        "MemoryAlertDialog: User clicked Hide - continuing to monitor");
    m_monitor->SetAlertHiddenByUser(true);
  }
  Hide();  // Don't destroy, just hide
}

void MemoryAlertDialog::OnClose(wxCommandEvent& event) {
  // This is for programmatic close (e.g., from Settings checkbox)
  // Set alertDismissed to prevent re-showing until memory drops
  if (m_monitor) {
    m_monitor->SetAlertDismissed(true);
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
    : m_isValidState(true), m_isShuttingDown(false),
      m_weatherRouting(nullptr),
      m_usageGauge(nullptr),
      m_textLabel(nullptr),
      activeAlertDialog(nullptr),
      m_alertEnabled(true),
      m_alertShown(false),
      m_alertDismissed(false),  // is this still needed?
      m_alertThreshold(80.0),
      m_alertLastPercent(-1.0),  // Stores last recorded percent.
      m_alertUpdateThreshold(1.0),  // Perform refresh/updates/logging at this interval.
      m_wasOverThreshold(false),
      m_autoStopEnabled(true),
      m_autoStopThreshold(85.0),
      m_autoStopTriggered(false),
      m_instanceId(++s_instanceId),
      m_logToFile(false),
      m_blockAlertDialogAfterAutoStop(false)
  {
  ++s_instanceCount;
  m_magic = 0xA5A5A5A5;
  wxThreadIdType threadIdx = wxThread::GetCurrentId();
  wxLogMessage("ASM ctor: this=%p, &m_mutex=%p, instance=%d, thread=%lu",
               static_cast<void*>(this), static_cast<void*>(&m_mutex),
               m_instanceId, (unsigned long)threadIdx);
  
  // Force logging initialization check
  {
    LoadSettings();
    // ... rest of constructor ...
  }
  if (!s_loggingInitialized) {
    s_loggingInitialized = true;
    wxLogMessage( "=== AddressSpaceMonitor: FIRST INSTANCE LOGGING INITIALIZED ===");
  }

  // Cast thread ID to unsigned long for consistent formatting
  wxThreadIdType threadId = wxThread::GetCurrentId();
  wxLogMessage(
      "AddressSpaceMonitor: Constructor - Instance #%d created (total active: "
      "%d) - Thread ID: %lu",
      m_instanceId, s_instanceCount, (unsigned long)threadId);

  // Log if this is being called during static initialization
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

void AddressSpaceMonitor::SetWeatherRouting(WeatherRouting* wr) {
  wxLogMessage(
      "ASM::SetWeatherRouting this=%p, &m_mutex=%p, instance=%d, magic=%08x",
      static_cast<void*>(this), static_cast<void*>(&m_mutex), m_instanceId,
      m_magic);
  if (!m_isValidState.load() || m_isShuttingDown) {
    wxLogWarning(
        "AddressSpaceMonitor::Set... called in invalid/shutdown state");
    return;
  }
   //std::lock_guard<std::mutex> lock(m_mutex);
   wxMutexLocker lock(m_mutex);


   m_weatherRouting = wr;
}

AddressSpaceMonitor::~AddressSpaceMonitor() {
  --s_instanceCount;
  wxLogMessage(
      "AddressSpaceMonitor: Destructor called - Instance #%d (remaining "
      "active: %d)",
      m_instanceId, s_instanceCount);

  // Ensure shutdown has been called
  if (m_isValidState.load() && !m_isShuttingDown) {
    Shutdown();
  }

  wxLogMessage("AddressSpaceMonitor: Destructor complete - Instance #%d",
               m_instanceId);
}

void AddressSpaceMonitor::DismissAlert() {
  if (!m_isValidState.load()) {
    wxLogWarning(
        "AddressSpaceMonitor::DismissAlert() called on invalid object");
    return;
  }

  m_alertDismissed = true;

  // Hide any active dialog
  if (activeAlertDialog && activeAlertDialog->IsShown()) {
    activeAlertDialog->Hide();
    wxLogMessage("AddressSpaceMonitor: Alert dismissed and dialog hidden");
  } else {
    wxLogMessage("AddressSpaceMonitor: Alert dismissed (no active dialog)");
  }
}

// Begin Auto-Stop process by Safely calling a method on m_weatherRouting
void AddressSpaceMonitor::SafeStopWeatherRouting() {
  if (!m_isValidState.load()) {
    wxLogWarning("SafeStopWeatherRouting() called on invalid object");
    return;
  }
  if (m_weatherRouting) {
    wxLogMessage(
        "AddressSpaceMonitor: Calling WeatherRouting::Reset() from "
        "SafeStopWeatherRouting()");
    m_weatherRouting->Reset();
  } else {
    wxLogWarning(
        "AddressSpaceMonitor: m_weatherRouting is nullptr in "
        "SafeStopWeatherRouting()");
  }
}

// Shutdown the Alert Dialog and clean up resources
void AddressSpaceMonitor::Shutdown() {
  //std::lock_guard<std::mutex> lock(m_mutex);

  // Prevent multiple shutdown calls
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
  m_isValidState.store(false);

  // Clear gauge reference first to prevent updates during shutdown
  m_usageGauge = nullptr;
  m_textLabel = nullptr;
  m_weatherRouting = nullptr;

 // Call the unlocked version, since we already hold the lock
  CloseAlertUnlocked();

  wxLogMessage("AddressSpaceMonitor: Shutdown complete - Instance #%d",
               m_instanceId);
}


void AddressSpaceMonitor::CloseAlert() {
wxMutexLocker lock(m_mutex);
if (!lock.IsOk()) {
  wxLogWarning(
      "AddressSpaceMonitor: Mutex already locked, skipping this check");
  return;
}
// ... critical section ...
// Mutex is automatically unlocked when 'lock' goes out of scope
  CloseAlertUnlocked();
}

void AddressSpaceMonitor::CloseAlertUnlocked() {
  wxTheApp->CallAfter([this]() {
    wxLogMessage("AddressSpaceMonitor: CloseAlertUnlocked() called");
    if (activeAlertDialog) {
      wxLogMessage(
          "AddressSpaceMonitor: Closing alert dialog (activeAlertDialog=%p, "
          "IsShown=%d)",
          static_cast<void*>(activeAlertDialog), activeAlertDialog->IsShown());
      activeAlertDialog->ClearMonitor();
      if (activeAlertDialog->IsShown()) {
        wxLogMessage(
            "AddressSpaceMonitor: Hiding alert dialog (activeAlertDialog=%p)",
            static_cast<void*>(activeAlertDialog));
        activeAlertDialog->Hide();
      }
      wxLogMessage(
          "AddressSpaceMonitor: Destroying alert dialog (activeAlertDialog=%p)",
          static_cast<void*>(activeAlertDialog));
      activeAlertDialog->Destroy();
      activeAlertDialog = nullptr;
      wxLogMessage(
          "AddressSpaceMonitor: Alert dialog destroyed and pointer cleared");
    } else {
      wxLogMessage("AddressSpaceMonitor: No active alert dialog to close");
    }
    m_alertShown = false;
    m_alertDismissed = false;
  });
}

// ================ CORE MONITORING LOGIC ==================    
void AddressSpaceMonitor::CheckAndAlert() {
  if (!m_weatherRouting) return;  
  
  // Check if memory monitoring is degraded.    
  if (m_degraded) {
    return;
  }
  // First line of defense: Check re-entrancy WITHOUT locking
  bool expected = false;
  if (!m_isExecuting.compare_exchange_strong(expected, true)) {
    wxLogMessage(
        "AddressSpaceMonitor: Skipping re-entrant call (already executing)");
    return;
  }

  // RAII guard to always reset the flag, even on exceptions
  // RAII guard for m_isExecuting  - Suggested to keep the flag reset even if
  // exceptions occur. Keep if if you still need to reset the flag on scope exit.
  struct ExecutionGuard {
     std::atomic<bool>& flag;
     ExecutionGuard(std::atomic<bool>& f) : flag(f) {}
     ~ExecutionGuard() { flag.store(false); }
  } guard(m_isExecuting);

  // RAII guard for mutex
  wxMutexLocker lock(m_mutex);
  if (!lock.IsOk()) {
    wxLogWarning(
        "AddressSpaceMonitor: Mutex already locked, skipping this check");
    return;
  }

  // ... critical section code ...
  auto now = std::chrono::steady_clock::now();
  if (now - m_lastCheckTime < std::chrono::milliseconds(500)) return;
  m_lastCheckTime = now;

  // Second line of defense: Check validity before locking
  if (!m_isValidState.load()) {
    wxLogWarning(
        "AddressSpaceMonitor::CheckAndAlert() called on invalid object");
    return;
  }

  // Fourth line of defense: Verify state again after acquiring lock
  if (!m_isValidState.load() || m_isShuttingDown) {
    wxLogWarning(
        "AddressSpaceMonitor: Object became invalid while waiting for lock");
    return;
  }

  size_t used = GetUsedAddressSpace();
  size_t total = GetTotalAddressSpace();
  double percent = 100.0 * used / total;


  // Disable new computations if percent >= alert threshold
  if (percent >= m_alertThreshold) {
    if (!m_computationDisabled) {
      m_computationDisabled = true;
      DisableNewComputations();
    }
  } else if (percent <= m_alertThreshold - 15.0) {  // Set hysteresis to avoid flapping
    // Enable new computations if percent drops below or equal to (alert
    // threshold - 3)
    if (m_computationDisabled) {
       m_computationDisabled = false;
      EnableNewComputations();
    }
  }

  // Debounce logic for alert
  bool wasOverThreshold = m_wasOverThreshold;
  bool isOverThreshold = percent > m_alertThreshold;

  // Reset hidden flag if we drop below threshold
  if (!isOverThreshold) {
    m_alertHiddenByUser = false;
  }

  // Only show alert if we just crossed from below to above the threshold
  if (!wasOverThreshold && isOverThreshold && m_alertEnabled &&
      !m_alertDismissed && !m_alertHiddenByUser) {
    double usedGB = used / (1024.0 * 1024.0 * 1024.0);
    double totalGB = total / (1024.0 * 1024.0 * 1024.0);
    ShowOrUpdateAlert(usedGB, totalGB, percent);
    wxLogMessage(
        "AddressSpaceMonitor: Alert shown (debounced threshold crossing)");
  }
  
  // Hide alert if we just crossed from above to below
  if (wasOverThreshold && !isOverThreshold && activeAlertDialog &&
       activeAlertDialog->IsShown()) {
    CloseAlert();
    wxLogMessage(
        "AddressSpaceMonitor: Alert closed (debounced threshold crossing)");
  }

  // Update the Alert State for next scan
  m_wasOverThreshold = isOverThreshold;

 // =========== AUTOSTOP LOGIC   ==============
  //  1. If autostopenabled, weatherrouting and autostoptrigged show the values.
  //  2. If percent >= m_autoStopThreshold
  //       a. issue a log message
  //       b. Prepare memory data string
  //       c. wxMutex  lock turned on.
  //       d. Set autoStopTriggered flag inside mutex block
  //       e. mutex is then released.
  //  3. Close the alert dialog(asynchronously, using `wxTheApp->CallAfter`)
  //  4. Show the AutoStop message box
  //  5. Then call `SafeStopWeatherRouting()` (which calls `Reset()`)

  if (m_autoStopEnabled && m_weatherRouting && !m_autoStopTriggered) {
    wxLogMessage(
        "AutoStop: percent=%.1f, threshold=%.1f, enabled=%d, triggered=%d, "
        "weatherRouting=%p",
        percent, m_autoStopThreshold, m_autoStopEnabled, m_autoStopTriggered,
        m_weatherRouting);

    if (percent >= m_autoStopThreshold) {
      wxLogMessage(
          "AddressSpaceMonitor: Auto-stop threshold (%.1f%%) reached at %.1f%% "
          "usage",
          m_autoStopThreshold, percent);

      wxString message = wxString::Format(
          _("Memory use reached: %.0f%%  Threshold: %.0f%%\n\n"
            "Running route computations have been automatically\n\n"
            "stopped to prevent memory exhaustion."),
          percent, m_autoStopThreshold);

      // Where we set the triggered flag inside the mutex-protected block
      // Mutex lock scope
      // wxMutexLocker lock(m_mutex);

      // Set the triggered flag inside the mutex-protected block
      m_autoStopTriggered = true;
      m_blockAlertDialogAfterAutoStop = true; // <--- Block further alerts
      // Mutex is released here

      // Chain: close alert, show message, then reset (all on main thread)
      // Close the alert dialog(asynchronously, using `wxTheApp->CallAfter`)
      // Always close (destroy) the alert dialog if it exists, regardless of
      // visibility (hidden or visible)

      wxTheApp->CallAfter([this, message, percent]() {
        // 1. Close the alert dialog if it exists
        if (activeAlertDialog) {
          wxLogMessage(
              "AddressSpaceMonitor: Opening AutoStop message box "
              "(percent=%.1f, threshold=%.1f)",
              percent, m_autoStopThreshold);
          activeAlertDialog->ClearMonitor();
          if (activeAlertDialog->IsShown()) activeAlertDialog->Hide();
          activeAlertDialog->Destroy();
          activeAlertDialog = nullptr;
          m_alertShown = false;
          m_alertDismissed = false;
        }
        // 2. Show the AutoStop message box  (non-blocking)
        wxLogMessage(
            "AddressSpaceMonitor: Showing AutoStop dialog (non-blocking)");
        AutoStopDialog* dlg = new AutoStopDialog(nullptr, message, 120);
        dlg->Show();   // Non-modal, does not block
             dlg->Raise();
             dlg->SetFocus();
             // 3. Immediately execute SafeStopWeatherRouting() Stop & Reset WR
        wxLogMessage(
            "AddressSpaceMonitor: Calling SafeStopWeatherRouting() after "
            "AutoStop dialog shown");
        SafeStopWeatherRouting();
        // Optionally, connect to dialog destruction for logging:
        dlg->Bind(wxEVT_CLOSE_WINDOW, [dlg](wxCloseEvent&) {
          int result = dlg->GetResult();
          wxLogMessage(
              "AddressSpaceMonitor: AutoStop dialog closed by %s (result=%d)",
              (result == 1 ? "user" : "timer"), result);
          dlg->Destroy();
        });  //<-- Bind close and destroy
      });    // <-- This closes the lambda and CallAfter
    }  // <-- this closes the function

    if (m_autoStopTriggered && percent < m_autoStopThreshold - 5.0) {
      m_autoStopTriggered = false;
      m_blockAlertDialogAfterAutoStop = false; // <--- Allow alerts again
      wxLogMessage(
          "AddressSpaceMonitor: Usage dropped below %.1f%%, auto-stop "
          "reset",
          m_autoStopThreshold - 5.0);
      wxLogMessage(
          "AutoStop: percent=%.1f, threshold=%.1f, enabled=%d, "
          "triggered=%d, "
          "weatherRouting=%p",
          percent, m_autoStopThreshold, m_autoStopEnabled, m_autoStopTriggered,
          m_weatherRouting);
    }  //....reset Alert logic

    double usedGB = used / (1024.0 * 1024.0 * 1024.0);
    double totalGB = total / (1024.0 * 1024.0 * 1024.0);

    // Only update if percent changed by >AlertUpdateThreshold%
    if (fabs(percent - m_alertLastPercent) > m_alertUpdateThreshold) {
      m_alertLastPercent = percent;

      // Update text label if connected
      if (m_textLabel) {
        wxTheApp->CallAfter([this, percent, usedGB, totalGB]() {
          wxString stats = wxString::Format("%.1f%% (%.2f GB / %.1f GB)",
                                            percent, usedGB, totalGB);
          m_textLabel->SetLabel(stats);
          wxColour textColor;
          if (percent >= m_alertThreshold) {
            textColor = *wxRED;
          } else if (percent >= 70.0) {
            textColor = wxColour(255, 140, 0);
          } else {
            textColor = wxColour(0, 128, 0);
          }
          m_textLabel->SetForegroundColour(textColor);
          m_textLabel->Refresh();
        });  // ...update logic
      }  // closes if  m_autoStopEnabled && m_weatherRouting &&  !m_autoStopTriggered
  
      if (m_logToFile) {
        wxLogMessage("AddressSpaceMonitor: %.2f GB / %.1f GB (%.1f%%)", usedGB,
                     totalGB, percent);
      }

      UpdateAlertIfShown(usedGB, totalGB, percent);

      if (m_usageGauge) {
        wxTheApp->CallAfter([this, percent]() {
          try {
            m_usageGauge->SetValue(static_cast<int>(percent));
          } catch (...) {
            wxLogWarning(
                "AddressSpaceMonitor: Exception accessing gauge, clearing "
                "reference");
            m_usageGauge = nullptr;
          }
        });
      }
    }
  }
 // =============== END AUTOSTOP LOGIC ==================
 // ======= PROTECT MEMORY and ADDRESS SPACE ============
 // When memory use is high. disable Route Computation, show dialog, and ResetAll button
  // AddressSpaceMonitor.cpp (inside CheckAndAlert, after AutoStop logic)
  wxLogMessage(
      "CheckAndAlert: Entering memory protection logic. "
      "m_autoStopTriggered=%d, percent=%.2f, m_computationDisabled=%d, "
      "m_memoryAvailableNoticeShown=%d",
      m_autoStopTriggered, percent, m_computationDisabled,
      m_memoryAvailableNoticeShown);


  if (m_autoStopTriggered && percent >= m_autoStopThreshold) {
    wxLogMessage(
        "CheckAndAlert: AutoStop triggered, percent=%.2f, threshold=%.2f",
        percent, m_autoStopThreshold);
    if (!m_computationDisabled) {
      m_computationDisabled = true;
      wxString msg = wxString::Format(_("Route computation disabled \n\n"
                                        "until memory is available.\n\n"
                                        "use ResetAll. Memory use: %.1f%%"),
                                      percent);
      wxTheApp->CallAfter([msg]() {
        wxLogMessage(
            "CheckAndAlert: Showing computation disabled message box.");
        wxMessageBox(msg, _("WR - Memory Alert"),
                     wxOK | wxICON_WARNING);
      });
    }
  }
  if (m_computationDisabled && percent < m_autoStopThreshold - 5.0) {
      wxLogMessage(
        "CheckAndAlert: Memory use below threshold, re-enabling computation "
        "(percent=%.2f, threshold=%.2f)",
        percent, m_autoStopThreshold);
    m_computationDisabled = false;
      wxLogMessage(
          "CheckAndAlert: Computation re-enabled. m_computationDisabled set to "
          "false.");

    if (!m_memoryAvailableNoticeShown) {
        m_memoryAvailableNoticeShown = true;
        wxLogMessage(
          "CheckAndAlert: Showing computation re-enabled message box.");
  
        wxTheApp->CallAfter([]() {
            wxMessageBox(_("Route computation re-enabled. Memory use:\n\n"
                           "now below the critical threshold."),
                         _("WR - Memory Alert"),
                         wxOK | wxICON_INFORMATION);
        });
    }
    } else if (percent >= m_autoStopThreshold - 5.0) {
    m_memoryAvailableNoticeShown = false;
  }
    wxLogMessage(
        "CheckAndAlert: Exiting memory protection logic. "
        "m_computationDisabled=%d, m_memoryAvailableNoticeShown=%d",
        m_computationDisabled, m_memoryAvailableNoticeShown);
}

void AddressSpaceMonitor::UpdateAlertIfShown(double usedGB, double totalGB,
                                             double percent) {
  // Guard against use after destruction
  if (!m_isValidState.load()) {
    wxLogWarning(
        "AddressSpaceMonitor::UpdateAlertIfShown() called on invalid object");
    return;
  }

  // Check if user dismissed alerts via Settings checkbox
  if (m_alertDismissed) {
    // Only re-enable if memory drops significantly below threshold
    if (percent < m_alertThreshold - 5.0) {
      m_alertDismissed = false;
      wxLogMessage(
          "AddressSpaceMonitor: Memory dropped below threshold (%.1f%%), "
          "re-enabling alerts",
          percent);
    }
    return;  // Don't show alerts while dismissed
  }

// Show/update alert if above threshold and not hidden by user
  if (percent >= m_alertThreshold && m_alertEnabled && !m_alertHiddenByUser) {
    ShowOrUpdateAlert(usedGB, totalGB, percent);
  } else if (percent < m_alertThreshold && activeAlertDialog &&
             activeAlertDialog->IsShown()) {
    // Memory dropped below threshold - hide the dialog
    CloseAlert();  // <-- Use this instead of just Hide()
    wxLogMessage(
        "AddressSpaceMonitor: Memory dropped below threshold (%.1f%%), closing  alert",
        percent);
  }
}

void AddressSpaceMonitor::ShowOrUpdateAlert(double usedGB, double totalGB, double percent) {
    if (m_blockAlertDialogAfterAutoStop) {
        wxLogMessage("AddressSpaceMonitor: Alert dialog creation blocked after AutoStop.");
        return;
    }
    wxTheApp->CallAfter([this, usedGB, totalGB, percent]() {
    wxLogMessage(
        "AddressSpaceMonitor: ShowOrUpdateAlert() called "
        "(activeAlertDialog=%p, percent=%.1f)",
        static_cast<void*>(activeAlertDialog), percent);
    if (!activeAlertDialog) {
      wxLogMessage("AddressSpaceMonitor: Creating new MemoryAlertDialog");
      activeAlertDialog = new MemoryAlertDialog(nullptr, this);
      if (!activeAlertDialog) {
        wxLogError("AddressSpaceMonitor: Failed to create alert dialog");
        return;
      }
      activeAlertDialog->UpdateMemoryInfo(usedGB, totalGB, percent);
      activeAlertDialog->Show();
      m_alertShown = true;
      wxLogMessage(
          "AddressSpaceMonitor: Showing new alert dialog "
          "(activeAlertDialog=%p)",
          static_cast<void*>(activeAlertDialog));
    } else if (activeAlertDialog->IsShown()) {
      wxLogMessage(
          "AddressSpaceMonitor: Updating existing visible alert dialog "
          "(activeAlertDialog=%p)",
          static_cast<void*>(activeAlertDialog));
      activeAlertDialog->UpdateMemoryInfo(usedGB, totalGB, percent);
    } else {
      // Only re-show if NOT hidden by user
      if (!m_alertHiddenByUser) {
        wxLogMessage(
            "AddressSpaceMonitor: Re-showing hidden alert dialog "
            "(activeAlertDialog=%p)",
            static_cast<void*>(activeAlertDialog));
        activeAlertDialog->UpdateMemoryInfo(usedGB, totalGB, percent);
        activeAlertDialog->Show();
        wxLogMessage(
            "AddressSpaceMonitor: Alert dialog re-shown (usage: %.1f%%, "
            "activeAlertDialog=%p)",
            percent, static_cast<void*>(activeAlertDialog));
      } else {
        wxLogMessage(
            "AddressSpaceMonitor: Alert dialog remains hidden by user (usage: "
            "%.1f%%, "
            "activeAlertDialog=%p)",
            percent, static_cast<void*>(activeAlertDialog));
      }
    }
  });
}

void AddressSpaceMonitor::SetAlertThreshold(double percent) {
  if (!m_isValidState.load()) { 
    wxLogWarning(
        "AddressSpaceMonitor::SetAlertThreshold() called on invalid object");
    return;
  }

  double oldThreshold = m_alertThreshold;
  m_alertThreshold = percent;
  SaveSettings();  // <-- Save immediately after change

  wxLogMessage("AddressSpaceMonitor: Threshold changed from %.1f%% to %.1f%%",
               oldThreshold, percent);

  // If dialog is shown and we're now below the new threshold, hide it
  if (activeAlertDialog && activeAlertDialog->IsShown()) {
    double currentPercent = GetUsagePercent();
    if (currentPercent < m_alertThreshold) {
      activeAlertDialog->Hide();
      wxLogMessage(
          "AddressSpaceMonitor: Hiding alert - usage (%.1f%%) below new "
          "threshold (%.1f%%)",
          currentPercent, m_alertThreshold);
    } else {
      // Update the dialog to show the new threshold
      double usedGB = GetUsedAddressSpace() / (1024.0 * 1024.0 * 1024.0);
      double totalGB = GetTotalAddressSpace() / (1024.0 * 1024.0 * 1024.0);
      activeAlertDialog->UpdateMemoryInfo(usedGB, totalGB, currentPercent);
    }
  }
}



void AddressSpaceMonitor::SetLoggingEnabled(bool enabled) {
  if (!m_isValidState.load()) {  
    wxLogWarning(
        "AddressSpaceMonitor::SetLoggingEnabled() called on invalid object");
    return;
  }
  m_logToFile = enabled;
  wxLogMessage("AddressSpaceMonitor: Logging %s",
               enabled ? "enabled" : "disabled");
}

void AddressSpaceMonitor::SetAlertEnabled(bool enabled) {
  if (!m_isValidState.load()) {
    wxLogWarning(
        "AddressSpaceMonitor::SetAlertEnabled() called on invalid object");
    return;
  }
  m_alertEnabled = enabled;
  if (!enabled) {
    CloseAlert();
  }
  wxLogMessage("AddressSpaceMonitor: Alerts %s",
               enabled ? "enabled" : "disabled");
}

void AddressSpaceMonitor::SetUsageGauge(wxGauge* gauge) {
  wxTheApp->CallAfter([this, gauge]() {
    if (!m_isValidState.load()) {
      wxLogWarning(
          "AddressSpaceMonitor::SetUsageGauge() called on invalid object");
      return;
    }
    m_usageGauge = gauge;
    if (gauge) {
      wxLogMessage("AddressSpaceMonitor: Gauge connected");
    } else {
      wxLogMessage(
          "AddressSpaceMonitor: Gauge disconnected (cleared to prevent "
          "dangling pointer)");
    }
  });
}

size_t AddressSpaceMonitor::GetUsedAddressSpace() const {
  if (!m_isValidState.load()) {
    return 0;
  }

  MEMORY_BASIC_INFORMATION mbi;
  unsigned char* addr = nullptr;
  size_t used = 0;
  bool failed = false;

  while (true) {
    SIZE_T result = VirtualQuery(addr, &mbi, sizeof(mbi));
    if (result != sizeof(mbi)) {
      if (addr == nullptr) {  // failed on first call
        failed = true;
      }
      break;
    }
    if (mbi.State == MEM_COMMIT) {
      used += mbi.RegionSize;
    }
    addr += mbi.RegionSize;
  }

  if (failed) {
    // Only warn once per session
    if (!m_degraded) {
      m_degraded = true;
      wxLogWarning(
          "AddressSpaceMonitor: Unable to query address space usage. "
          "Monitoring will be disabled.");
      wxTheApp->CallAfter([]() {
        wxMessageBox(_("Address space monitoring is not available on this "
                       "system. Memory alerts and auto-stop are disabled."),
                     _("Weather Routing - Address Space Monitor"),
                     wxOK | wxICON_WARNING);
      });
    }
    return 0;
  }

  return used;
}

size_t AddressSpaceMonitor::GetTotalAddressSpace() const {
  if (!m_isValidState.load() || m_degraded) {
    return 0;
  }
  return 0x80000000ULL;  // 2 GB for 32-bit process
}

double AddressSpaceMonitor::GetUsagePercent() const {
  if (!m_isValidState.load() || m_degraded) {
    return 0.0;
  }
  size_t total = GetTotalAddressSpace();
  if (total == 0) return 0.0;
  return 100.0 * GetUsedAddressSpace() / total;
}

void AddressSpaceMonitor::SetTextLabel(wxStaticText* label) {
  wxTheApp->CallAfter([this, label]() {
    if (!m_isValidState.load()) {
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
  });
}

void AddressSpaceMonitor::SetAutoStopThreshold(double percent) {
  m_autoStopThreshold = percent;
  // Immediate check if usage is already above new threshold and not triggered
  if (m_autoStopEnabled && !m_autoStopTriggered &&
      GetUsagePercent() >= m_autoStopThreshold) {
    // Call the same logic as in CheckAndAlert
    SafeStopWeatherRouting();
    m_autoStopTriggered = true;
    if (activeAlertDialog) {
      CloseAlert();
      wxLogMessage(
          "AddressSpaceMonitor: Alert closed due to auto-stop trigger "
          "(threshold changed)");
    }
    wxString message = wxString::Format(
        _("Memory use reached; %.0f%% \n\n"
          "AutoStop Threshold: %.0f%% \n\n"
          "Route Computations stopped \n\n"
          "to prevent memory corruption."),
        GetUsagePercent(), m_autoStopThreshold);
    wxTheApp->CallAfter([this, percent, message]() {
      wxLogMessage(
          "AddressSpaceMonitor: Opening AutoStop message box (percent=%.1f, "
          "threshold=%.1f)",
          percent, m_autoStopThreshold);
      AutoStopDialog* dlg = new AutoStopDialog(nullptr, message, 60);
      dlg->Show();
      dlg->Bind(wxEVT_CLOSE_WINDOW, [dlg](wxCloseEvent&) {
        int result = dlg->GetResult();
        wxLogMessage(
            "AddressSpaceMonitor: AutoStop dialog closed by %s (result=%d)",
            (result == 1 ? "user" : "timer"), result);
        dlg->Destroy();
        // You can log here if you want to know when the dialog is dismissed
        wxLogMessage(
            "AddressSpaceMonitor: AutoStop message box dismissed by user "
            "(result=%d)",
            result);
      });
    });
  }
}

void AddressSpaceMonitor::SetAutoStopEnabled(bool enabled) {
  m_autoStopEnabled = enabled;
}

void AddressSpaceMonitor::SetMemoryCheckInterval(int ms) {
  // Store and use this value in your timer logic
  m_memoryCheckIntervalMs = ms;
}

void AddressSpaceMonitor::ResetMemoryAlertSystem() {
  wxLogMessage(
      "AddressSpaceMonitor: ResetMemoryAlertSystem() called by user action");
  m_autoStopTriggered = false;
  m_computationDisabled = false;
  m_alertDismissed = false;
  m_alertHiddenByUser = false;
  m_wasOverThreshold = false;
  m_alertLastPercent = -1.0;
  if (activeAlertDialog && activeAlertDialog->IsShown()) {
    activeAlertDialog->Hide();
  }
}

void AddressSpaceMonitor::DisableNewComputations() {
  if (m_weatherRouting) {
    m_weatherRouting->DisableNewComputations();
    wxLogMessage(
        "AddressSpaceMonitor: Disabled new route computations due to high "
        "memory usage.");
  }
}

void AddressSpaceMonitor::EnableNewComputations() {
  if (m_weatherRouting) {
    m_weatherRouting->EnableNewComputations();
    wxLogMessage(
        "AddressSpaceMonitor: Enabled new route computations as memory usage "
        "is back to normal.");
  }
}

void AddressSpaceMonitor::SaveSettings() {
  wxConfig config("WeatherRouting");
  config.Write("AlertThreshold", m_alertThreshold);
  config.Flush();
}

void AddressSpaceMonitor::LoadSettings() {
  wxConfig config("WeatherRouting");
  double defaultThreshold = 80.0;
  config.Read("AlertThreshold", &m_alertThreshold, defaultThreshold);
}
#endif  // __WXMSW__
