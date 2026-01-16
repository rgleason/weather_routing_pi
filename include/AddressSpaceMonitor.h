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
 * @brief ALERT Popup dialog warning user about high address space usage.
 */
class MemoryAlertDialog : public wxDialog {
public:
  MemoryAlertDialog(wxWindow* parent, AddressSpaceMonitor* monitor);
  ~MemoryAlertDialog(); 
  void UpdateMemoryInfo(double usedGB, double totalGB, double percent);
  void ClearMonitor();

private:
  AddressSpaceMonitor*   m_monitor= nullptr;  ///< Pointer to managing monitor (cleared on shutdown)
  wxStaticText* m_messageText = nullptr;  ///< Label displaying warning message
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
  * @note Non-copyable, non-movable (RAII pattern).
 */
class AddressSpaceMonitor {
public:
  AddressSpaceMonitor();
  ~AddressSpaceMonitor();

  // Delete copy/move constructors and assignment operators
  AddressSpaceMonitor(const AddressSpaceMonitor&) = delete;
  AddressSpaceMonitor& operator=(const AddressSpaceMonitor&) = delete;
  AddressSpaceMonitor(AddressSpaceMonitor&&) = delete;
  AddressSpaceMonitor& operator=(AddressSpaceMonitor&&) = delete;

  /// Computation disabled flag
  bool IsComputationDisabled() const { return m_computationDisabled; }
  void SetComputationDisabled(bool disabled) { m_computationDisabled = disabled; }
  void ResetMemoryAlertSystem();

  void SetWeatherRouting(WeatherRouting* wr);  ///< Setter Pointer to WeatherRouting plugin for AutoStop
  void DismissAlert();                         ///< Suppress until memory drops 5% below threshold
  void SafeStopWeatherRouting();
  void Shutdown();                              ///< Cleans up resources and marks invalid Only call this from outside, not from Shutdown
  void CloseAlert();                            ///< Destroys alert dialog
  void CheckAndAlert();                         ///< Checks usage and shows/updates alert if needed

  void SetTextLabel(wxStaticText* label);  ///< SetTextLabel for percent usedGB totalGB
  void SetUsageGauge(wxGauge* gauge);  ///< Connects gauge for visual feedback
  void UpdateAlertIfShown(double usedGB, double totalGB, double percent);  ///< Updates alert if visible
  void ShowOrUpdateAlert(double usedGB, double totalGB, double percent);

  void SetAlertEnabled(bool enabled);  ///< Enables popup alerts
  void SetAlertThreshold(double percent);    ///< Sets alert threshold (0-100)
  void SetAlertHiddenByUser(bool hidden) { m_alertHiddenByUser = hidden; } 
  void SetAlertDismissed(bool dismissed) { m_alertDismissed = dismissed; }
  void SetAutoStopEnabled(bool enabled);  ///  Enable Auto Stop
  void SetAutoStopThreshold(double percent);  /// Set Auto Stop at 5% above threshold percent
  void SetMemoryCheckInterval(int ms);  ///< Set Memory Check Interval in milliseconds
  void SetLoggingEnabled(bool enabled);  ///< Enables periodic usage logging

  size_t GetUsedAddressSpace() const;   ///< Queries current used space (bytes)
  size_t GetTotalAddressSpace() const;  ///< Returns 2 GB (0x80000000)
  double GetAlertThreshold() const { return m_alertThreshold; }

  double GetUsagePercent() const;       ///< Calculates percentage (0-100)
  bool m_IsValidState() const {return m_isValidState.load(); }  ///< Valid state check, false after Shutdown()
  bool IsAlertDismissed() const { return m_alertDismissed; }

private:
  uint32_t m_magic = 0;
  bool m_computationDisabled = false;
;
  // Concurrency and state
  std::atomic<bool> m_isExecuting{false};  ///< For re-entrancy protection
  
  wxMutex m_mutex;
  std::atomic<bool> m_isValidState{true};
  bool m_alertDismissed = false;  ///< User suppressed alerts via Settings checkbox
  bool m_isShuttingDown = false;           ///< true during Shutdown()
  mutable bool m_degraded = false;         ///< true if unable to get memory 

  // UI and plugin references
  WeatherRouting* m_weatherRouting = nullptr;  ///< Pointer to WeatherRouting plugin
  wxGauge* m_usageGauge = nullptr;               ///< Gauge in SettingsDialog
  wxStaticText* m_textLabel = nullptr;
  MemoryAlertDialog* activeAlertDialog = nullptr;  ///< Active dialog or nullptr

  // Timing and thresholds
  std::chrono::steady_clock::time_point m_lastCheckTime{};  ///< For minimum interval
  double m_alertThreshold = 80.0;      ///< Alert threshold percentage (default 80%)
  double m_autoStopThreshold = 85.0;   ///< Auto-stop threshold percentage
  bool m_autoStopEnabled = true;  ///< Auto-stop enabled flag
  bool m_autoStopTriggered = false;    ///< Auto-stop already triggered flag
   
  // State tracking
  bool m_alertEnabled = true;          ///< Show popup alerts
  bool m_alertShown = false;           ///< Alert dialog exists
  bool m_alertHiddenByUser = false;   
  bool m_wasOverThreshold = false;  ///< Tracks last threshold crossing for alert debouncing
  double m_alertUpdateThreshold = 1.0; ///< percent change required to trigger update
  double m_alertLastPercent = 0.0;     ///< Last percent value for change detection
  int m_memoryCheckIntervalMs = 500; ///< Default 500 ms memory check interval

  // Logging and alerting
  bool m_logToFile = false;    ///< Log usage on every check
   int m_instanceId = 0;        ///< Unique ID for debugging

// Persistence of Settings
  void CloseAlertUnlocked();  ///< Helper, assumes mutex is already locked
  void SaveSettings();
  void LoadSettings();
};

#endif  // ADDRESSSPACEMONITOR_H
#endif  // __WXMSW__
