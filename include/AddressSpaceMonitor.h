/**
 * @file AddressSpaceMonitor.h
 * @brief Windows process address space monitoring and alerting system.
 *
 * @details Provides real-time monitoring of address space usage in
 * OpenCPN processes on Windows. When usage exceeds a configurable threshold,
 * displays a popup alert dialog recommending memory cleanup actions.
 *
 * Key features:
 * - Continuous background monitoring via plugin timer (5 seconds)
 * - Real-time gauge updates in Settings dialog (2 seconds)
 * - Configurable alert threshold (default 80%)
 * - Non-dismissible alerts (hide temporarily, reappear if still over threshold)
 * - Automatic cleanup on plugin shutdown
 * - Option to suppress alerts until address space drops below 5%.
 * - Option to Log Address Space Usage to Opencpn.log file
 *
 * @note This header is only compiled on Windows (__WXMSW__ defined).
 *
 * @author Weather Routing Plugin Team
 * @date 2024
 */
#include <wx/defs.h>

#ifdef __WXMSW__

#ifndef ADDRESSSPACEMONITOR_H
#define ADDRESSSPACEMONITOR_H

#include <wx/wx.h>
#include <wx/gauge.h>
#include <wx/dialog.h>
#include "ProcessAddressSpace.h"

// Forward declaration
class AddressSpaceMonitor;

/**
 * @brief Popup dialog warning user about high address space usage.
 */
class MemoryAlertDialog : public wxDialog {
public:
  MemoryAlertDialog(wxWindow* parent, AddressSpaceMonitor* monitor);
  void UpdateMemoryInfo(double usedGB, double totalGB, double percent);
  void ClearMonitor();

  // Register a text label to update alongside the gauge



private:
  wxGauge* m_gauge;
  wxStaticText* m_messageText;  ///< Label displaying warning message

  void OnHide(wxCommandEvent& event);   ///< Hide button clicked
  void OnClose(wxCommandEvent& event);  ///< Programmatic close from Settings
  void OnCloseWindow(wxCloseEvent& event);  ///< X button clicked

  AddressSpaceMonitor*
      m_monitor;  ///< Pointer to managing monitor (cleared on shutdown)
  friend class AddressSpaceMonitor;  ///< Allow CloseAlert to access private members
};

/**
 * @brief Monitors process address space usage on Windows.
 *
 * @details Tracks reserved and committed address space in OpenCPN processes
 * and alerts users when usage exceeds a configurable threshold. This is not
 * a physical-memory or commit-limit monitor and cannot guarantee allocation
 * success, including when free address space is fragmented.
 *
 * Architecture:
 * - **Plugin timer**: Checks every 5 seconds (continuous monitoring)
 * - **Settings timer**: Updates gauge every 2 seconds (visual feedback)
 * - **Alert dialog**: Shows when threshold exceeded, hides when below
 *
 * Lifecycle:
 * 1. Constructed by weather_routing_pi on plugin initialization
 * 2. Configured by SettingsDialog::LoadMemorySettings()
 * 3. Monitored by plugin timer (OnAddressSpaceTimer)
 * 4. Shutdown explicitly before plugin destruction
 *
 * @note Non-copyable, non-movable (RAII pattern).
 */
class AddressSpaceMonitor {
public:
  AddressSpaceMonitor();
  ~AddressSpaceMonitor();

  // Delete copy constructor and assignment operator to prevent copying
  AddressSpaceMonitor(const AddressSpaceMonitor&) = delete;
  AddressSpaceMonitor& operator=(const AddressSpaceMonitor&) = delete;

  // Delete move constructor and assignment operator
  AddressSpaceMonitor(AddressSpaceMonitor&&) = delete;
  AddressSpaceMonitor& operator=(AddressSpaceMonitor&&) = delete;

  void Shutdown();       ///< Cleans up resources and marks invalid
  void CheckAndAlert();  ///< Checks usage and shows/updates alert if needed
  void UpdateAlertIfShown(double usedGB, double totalGB,
                          double percent);  ///< Updates alert if visible
  void DismissAlert();  ///< Suppresses alerts until memory drops 5% below threshold

  void SetThresholdPercent(double percent);  ///< Sets alert threshold (0-100)
  void SetLoggingEnabled(bool enabled);      ///< Enables periodic usage logging
  void SetAlertEnabled(bool enabled);        ///< Enables popup alerts
  void SetGauge(wxGauge* gauge);  ///< Connects gauge for visual feedback
  void SetTextLabel(wxStaticText* label);  ///< SetTextLabel for percent usedGB totalGB

  std::optional<weather_routing::ProcessAddressSpace> GetAddressSpace() const;
  bool IsValid() const { return m_isValid; }  ///< Valid state check

  bool alertDismissed;      ///< User suppressed alerts via Settings checkbox
  double thresholdPercent;  ///< Alert threshold percentage (default 80%)

private:
  void ShowOrUpdateAlert(double usedGB, double totalGB, double percent);  ///< Creates/updates alert dialog
  void CloseAlert();                       ///< Destroys alert dialog

  // State Flags
  bool m_isValid;         ///< false after Shutdown()
  bool m_isShuttingDown;  ///< true during Shutdown()
  int m_instanceId;       ///< Unique ID for debugging

  // Configuration
  bool logToFile;     ///< Log usage on every check
  bool alertEnabled;  ///< Show popup alerts

  // UI References
  wxGauge* usageGauge;                   ///< Gauge in SettingsDialog
  wxStaticText* m_textLabel;
  bool alertShown;                       ///< Alert dialog exists
  MemoryAlertDialog* activeAlertDialog;  ///< Active dialog or nullptr
};

#endif  // ADDRESSSPACEMONITOR_H
#endif  // __WXMSW__
