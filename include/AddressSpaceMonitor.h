/**
 * @file AddressSpaceMonitor.h
 * @brief Windows-only 32-bit address space monitoring and alerting system.
 *
 * @details Provides real-time monitoring of address space usage in 32-bit
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
#ifdef __WXMSW__

#ifndef ADDRESSSPACEMONITOR_H
#define ADDRESSSPACEMONITOR_H

#include <wx/wx.h>
#include <wx/gauge.h>
#include <wx/dialog.h>
#include <mutex>
#include <atomic>
#include <chrono>
#include <wx/config.h>

// Forward declaration
class AddressSpaceMonitor;
class WeatherRouting;

/**
 * @brief Popup dialog warning user about high address space usage.
 */
class MemoryAlertDialog : public wxDialog {
public:
  MemoryAlertDialog(wxWindow* parent, AddressSpaceMonitor* monitor);
  ~MemoryAlertDialog();  // Add this destructor
  void UpdateMemoryInfo(double usedGB, double totalGB, double percent);
  void ClearMonitor();

private:
  AddressSpaceMonitor*   m_monitor;  ///< Pointer to managing monitor (cleared on shutdown)
  wxStaticText* m_messageText;  ///< Label displaying warning message
  void OnHide(wxCommandEvent& event);   ///< Hide button clicked
  void OnClose(wxCommandEvent& event);  ///< Programmatic close from Settings
  void OnCloseWindow(wxCloseEvent& event);  ///< X button clicked
};

/**
 * @brief Monitors 32-bit address space usage on Windows.
 *
 * @details Tracks address space consumption in 32-bit OpenCPN processes and
 * alerts users when usage exceeds a configurable threshold. Prevents crashes
 * due to address space exhaustion by recommending memory cleanup actions.
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

  void SetWeatherRouting(WeatherRouting* wr);  ///< Setter Pointer to WeatherRouting plugin for AutoStop
  void DismissAlert();  ///< Suppresses alerts until memory drops 5% below threshold
  void SafeStopWeatherRouting();
  void Shutdown();  ///< Cleans up resources and marks invalid Only call this from outside, not from Shutdown
  void CloseAlert();  ///< Destroys alert dialog
  void CheckAndAlert();  ///< Checks usage and shows/updates alert if needed
  void UpdateAlertIfShown(double usedGB, double totalGB, double percent);  ///< Updates alert if visible
  void ShowOrUpdateAlert(double usedGB, double totalGB, double percent);
  void SetThresholdPercent(double percent);  ///< Sets alert threshold (0-100)
  void SetLoggingEnabled(bool enabled);      ///< Enables periodic usage logging
  void SetAlertEnabled(bool enabled);        ///< Enables popup alerts
  void SetGauge(wxGauge* gauge);  ///< Connects gauge for visual feedback
  void SetTextLabel(wxStaticText* label);  ///< SetTextLabel for percent usedGB totalGB
  void SetAutoStopThreshold(double percent); /// Set Auto Stop at 5% above threshold percent
  void SetMemoryCheckInterval(int ms);  ///< Set Memory Check Interval in milliseconds
  void SetAutoStopEnabled(bool enabled);  ///  Enable Auto Stop

  size_t GetUsedAddressSpace() const;   ///< Queries current used space (bytes)
  size_t GetTotalAddressSpace() const;  ///< Returns 2 GB (0x80000000)
  double GetUsagePercent() const;       ///< Calculates percentage (0-100)

  bool alertDismissed;      ///< User suppressed alerts via Settings checkbox
  double thresholdPercent;  ///< Alert threshold percentage (default 80%)
  bool IsValid() const {return m_isValidState.load(); }  ///< Valid state check         ///< false after Shutdown()

private:

 // --- Re-entrancy and concurrency guards ---
  std::atomic<bool> m_isExecuting{false};  // For re-entrancy protection
  std::mutex m_mutex;
  void CloseAlertUnlocked();  // Helper, assumes mutex is already locked

 // --- Throttle check frequency ---
  std::chrono::steady_clock::time_point m_lastCheckTime{};  // For minimum interval

 // ---State and Configuration---
  std::atomic<bool> m_isValidState{true};  // Changed from bool to atomic<bool>
  bool m_isShuttingDown;                   ///< true during Shutdown()
  bool logToFile;     ///< Log usage on every check
  bool alertEnabled;  ///< Show popup alerts
  double m_autoStopThreshold;  ///< Auto-stop threshold percentage
  bool m_autoStopEnabled;      ///< Auto-stop enabled flag
  bool m_autoStopTriggered;    ///< Auto-stop already triggered flag
  double updatePercentThreshold;  // percent change required to trigger update
  WeatherRouting* m_weatherRouting;  ///< Pointer to WeatherRouting plugin

 // UI References
  wxGauge* usageGauge;                   ///< Gauge in SettingsDialog
  wxStaticText* m_textLabel;
  bool alertShown;                       ///< Alert dialog exists
  MemoryAlertDialog* activeAlertDialog;  ///< Active dialog or nullptr
  int m_instanceId;                      ///< Unique ID for debugging
  double m_lastPercent;     ///< Last percent value for change detection
  bool m_wasOverThreshold;  ///< Tracks last threshold crossing for alert debouncing
  int m_memoryCheckIntervalMs = 500;  ///< Default 500 ms memory check interval


// Persistence of Settings
  void SaveSettings();
  void LoadSettings();

};

#endif  // ADDRESSSPACEMONITOR_H
#endif  // __WXMSW__
