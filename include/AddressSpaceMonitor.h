#pragma once
#ifdef __WXMSW__

#include <wx/wx.h>
#include <wx/gauge.h>
#include <wx/dialog.h>
#include <wx/config.h>
#include <atomic>
#include <mutex>
#include <chrono>

// Forward declarations
class WeatherRouting;
class MemoryStatusDialog;

/**
 * @brief Monitors 32‑bit address space usage on Windows and manages
 *        AlertStop + AutoReset behaviors using a unified dialog.
 */
class AddressSpaceMonitor {
public:
    AddressSpaceMonitor();
    ~AddressSpaceMonitor();

    // --- Core monitoring API ---
    void CheckAndAlert();                     ///< Called periodically by plugin timer
    void Shutdown();                          ///< Safe teardown, clears UI + state

    // --- WeatherRouting integration ---
    void SetWeatherRouting(WeatherRouting* wr);
    void SafeStopWeatherRouting();            ///< Calls WR->Reset() safely

    // --- Computation control ---
    void DisableNewComputations();
    void EnableNewComputations();
    bool IsComputationDisabled() const { return m_computationDisabled; }

    // --- UI integration ---
    void SetUsageGauge(wxGauge* gauge);
    void SetTextLabel(wxStaticText* label);

    // --- Alert system control ---
    void DismissAlert();                      ///< User dismissed alert until memory drops
    void CloseAlert();                        ///< Hides unified dialog

    // --- Settings ---
    void SetAlertEnabled(bool enabled);
    void SetAlertThreshold(double percent);
    void SetAutoStopEnabled(bool enabled);
    void SetAutoStopThreshold(double percent);
    void SetMemoryCheckInterval(int ms);
    void SetLoggingEnabled(bool enabled);

    double GetAlertThreshold() const { return m_alertThreshold; }
    double GetUsagePercent() const;

    // --- Memory queries ---
    size_t GetUsedAddressSpace() const;
    size_t GetTotalAddressSpace() const;

    bool IsValidState() const { return m_isValidState.load(); }

private:
    // --- Internal helpers ---
    void ShowOrUpdateAlert(double usedGB, double totalGB, double percent);
    void ShowAutoReset(double usedGB, double totalGB, double percent);
    void UpdateAlertIfShown(double usedGB, double totalGB, double percent);

    void SaveSettings();
    void LoadSettings();

private:
    // --- Concurrency + lifecycle ---
    std::atomic<bool> m_isExecuting{false};   ///< Re‑entrancy guard
    std::atomic<bool> m_isValidState{true};   ///< False after Shutdown()
    bool m_isShuttingDown = false;
    wxMutex m_mutex;

    // --- WeatherRouting integration ---
    WeatherRouting* m_weatherRouting = nullptr;

    // --- UI elements ---
    wxGauge* m_usageGauge = nullptr;
    wxStaticText* m_textLabel = nullptr;
    MemoryStatusDialog* m_dialog = nullptr;

    // --- Thresholds + timing ---
    double m_alertThreshold = 80.0;           ///< AlertStop threshold
    double m_autoStopThreshold = 85.0;        ///< AutoReset threshold
    int m_memoryCheckIntervalMs = 500;        ///< Default 500ms scan interval
    std::chrono::steady_clock::time_point m_lastCheckTime{};

    // --- State flags ---
    bool m_computationDisabled = false;
    bool m_alertEnabled = true;
    bool m_alertHiddenByUser = false;
    bool m_alertDismissed = false;
    bool m_autoStopEnabled = true;
    bool m_autoStopTriggered = false;
    bool m_wasOverThreshold = false;
    bool m_blockAlertDialogAfterAutoStop = false;
    mutable bool m_degraded = false;

    // --- Alert update smoothing ---
    double m_alertUpdateThreshold = 1.0;      ///< Minimum % change to update UI
    double m_alertLastPercent = -1.0;

    // --- Logging ---
    bool m_logToFile = false;

    // --- Debugging ---
    uint32_t m_magic = 0xA5A5A5A5;
    int m_instanceId = 0;
};

#endif // __WXMSW__
