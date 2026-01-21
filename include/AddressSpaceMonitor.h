#pragma once
#ifdef __WXMSW__

#include <wx/wx.h>
#include <wx/gauge.h>
#include <wx/config.h>
#include <atomic>
#include <mutex>
#include <chrono>

wxDECLARE_EVENT(EVT_MEMORY_ALERT_STOP, wxCommandEvent);
wxDECLARE_EVENT(EVT_MEMORY_AUTO_RESET, wxCommandEvent);

class WeatherRouting;

class AddressSpaceMonitor {
public:
  AddressSpaceMonitor();
  ~AddressSpaceMonitor();

  double GetAlertThreshold() const { return m_alertThreshold; }
  double GetUsagePercent() const;

  bool IsValidState() const { return m_isValidState.load(); }

  size_t GetUsedAddressSpace() const;
  size_t GetTotalAddressSpace() const;

  void CheckAndAlert();
  void Shutdown();

  void SetWeatherRouting(WeatherRouting* wr);

  void DisableNewComputations();
  void EnableNewComputations();
  bool IsComputationDisabled() const { return m_computationDisabled; }

  void SetUsageGauge(wxGauge* gauge);
  void SetTextLabel(wxStaticText* label);

  void SetAlertEnabled(bool enabled);
  void SetAlertThreshold(double percent);
  void SetAutoStopEnabled(bool enabled);
  void SetAutoStopThreshold(double percent);
  void SetMemoryCheckInterval(int ms);
  void SetLoggingEnabled(bool enabled);

private:
  void SaveSettings();
  void LoadSettings();

  // --- Concurrency + lifecycle ---
  std::atomic<bool> m_isExecuting{false};
  std::atomic<bool> m_isValidState{true};
  bool m_isShuttingDown = false;
  wxMutex m_mutex;

  // --- WeatherRouting integration ---
  WeatherRouting* m_weatherRouting = nullptr;

  // --- UI elements ---
  wxGauge* m_usageGauge = nullptr;
  wxStaticText* m_textLabel = nullptr;

  // --- Thresholds + timing ---
  double m_alertThreshold = 80.0;
  double m_autoStopThreshold = 85.0;
  int m_memoryCheckIntervalMs = 500;
  std::chrono::steady_clock::time_point m_lastCheckTime{};

  // --- State flags ---
  bool m_computationDisabled = false;
  bool m_alertEnabled = true;
  bool m_autoStopEnabled = true;
  mutable bool m_degraded = false;

  bool m_autoStopTriggered = false;
  bool m_wasOverThreshold = false;

  // --- Logging ---
  bool m_logToFile = false;

  // --- Debugging ---
  uint32_t m_magic = 0xA5A5A5A5;
  int m_instanceId = 0;
};

#endif  // __WXMSW__
