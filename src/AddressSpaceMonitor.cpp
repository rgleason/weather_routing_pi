#ifdef __WXMSW__

#include "AddressSpaceMonitor.h"
#include "WeatherRouting.h"

#include <wx/app.h>
#include <wx/log.h>
#include <wx/msgdlg.h>
#include <windows.h>
#include <cmath>

wxDEFINE_EVENT(EVT_MEMORY_ALERT_STOP, wxCommandEvent);
wxDEFINE_EVENT(EVT_MEMORY_AUTO_RESET, wxCommandEvent);

// ------------------------------------------------------------
//  Static instance tracking
// ------------------------------------------------------------
static int s_instanceCount = 0;
static int s_instanceId = 0;
static bool s_loggingInitialized = false;

// ------------------------------------------------------------
//  Constructor / Destructor
// ------------------------------------------------------------
AddressSpaceMonitor::AddressSpaceMonitor()
    : m_magic(0xA5A5A5A5),
      m_instanceId(++s_instanceId)
{
    ++s_instanceCount;

    wxThreadIdType tid = wxThread::GetCurrentId();
    wxLogMessage("ASM ctor: this=%p, instance=%d, thread=%lu",
                 static_cast<void*>(this), m_instanceId, (unsigned long)tid);

    LoadSettings();

    if (!s_loggingInitialized) {
        s_loggingInitialized = true;
        wxLogMessage("=== AddressSpaceMonitor: FIRST INSTANCE LOGGING INITIALIZED ===");
    }

    if (!wxTheApp)
        wxLogWarning("ASM: Instance %d created BEFORE wxApp exists!", m_instanceId);
}

AddressSpaceMonitor::~AddressSpaceMonitor()
{
    --s_instanceCount;
    wxLogMessage("ASM dtor: instance=%d (remaining=%d)", m_instanceId, s_instanceCount);

    if (m_isValidState.load() && !m_isShuttingDown)
        Shutdown();

    wxLogMessage("ASM dtor complete: instance=%d", m_instanceId);
}

// ------------------------------------------------------------
//  WeatherRouting Integration
// ------------------------------------------------------------
void AddressSpaceMonitor::SetWeatherRouting(WeatherRouting* wr)
{
    if (!m_isValidState.load() || m_isShuttingDown) {
        wxLogWarning("ASM::SetWeatherRouting called in invalid state");
        return;
    }

    wxMutexLocker lock(m_mutex);
    if (!lock.IsOk()) return;

    m_weatherRouting = wr;
}


// ------------------------------------------------------------
//  Computation Control
// ------------------------------------------------------------
void AddressSpaceMonitor::DisableNewComputations()
{
    m_computationDisabled = true;
    wxLogMessage("ASM: Computations disabled");
}

void AddressSpaceMonitor::EnableNewComputations()
{
    m_computationDisabled = false;
    wxLogMessage("ASM: Computations re-enabled");
}

// ------------------------------------------------------------
//  Shutdown
// ------------------------------------------------------------
void AddressSpaceMonitor::Shutdown() {
  wxMutexLocker lock(m_mutex);
  if (!lock.IsOk()) return;

  if (m_isShuttingDown) {
    wxLogMessage("ASM: Shutdown already in progress");
    return;
  }

  m_isShuttingDown = true;
  m_isValidState.store(false);

  m_usageGauge = nullptr;
  m_textLabel = nullptr;
  m_weatherRouting = nullptr;

  wxLogMessage("ASM: Shutdown complete");
}

// ------------------------------------------------------------
//  Main Monitoring Logic
// ------------------------------------------------------------
void AddressSpaceMonitor::CheckAndAlert() {
  if (!m_weatherRouting) return;
  if (m_degraded) return;

  bool expected = false;
  if (!m_isExecuting.compare_exchange_strong(expected, true)) return;

  struct Guard {
    std::atomic<bool>& f;
    Guard(std::atomic<bool>& x) : f(x) {}
    ~Guard() { f.store(false); }
  } guard(m_isExecuting);

  wxMutexLocker lock(m_mutex);
  if (!lock.IsOk()) return;

  auto now = std::chrono::steady_clock::now();
  if (now - m_lastCheckTime <
      std::chrono::milliseconds(m_memoryCheckIntervalMs))
    return;
  m_lastCheckTime = now;

  if (!m_isValidState.load() || m_isShuttingDown) return;

  size_t used = GetUsedAddressSpace();
  size_t total = GetTotalAddressSpace();
  if (total == 0) return;

  double percent = 100.0 * used / total;

  // ------------------------------------------------------------
  // Computation disable/enable
  // ------------------------------------------------------------
  if (percent >= m_alertThreshold) {
    if (!m_computationDisabled) {
      m_computationDisabled = true;
      DisableNewComputations();
    }
  } else if (percent <= m_alertThreshold - 15.0) {
    if (m_computationDisabled) {
      m_computationDisabled = false;
      EnableNewComputations();
    }
  }

  // ------------------------------------------------------------
  // AlertStop event
  // ------------------------------------------------------------
  bool wasOver = m_wasOverThreshold;
  bool isOver = percent > m_alertThreshold;

  if (!wasOver && isOver && m_alertEnabled) {
    wxCommandEvent evt(EVT_MEMORY_ALERT_STOP);
    wxQueueEvent(m_weatherRouting, evt.Clone());
  }

  m_wasOverThreshold = isOver;

  // ------------------------------------------------------------
  // AutoReset event
  // ------------------------------------------------------------
  if (m_autoStopEnabled && !m_autoStopTriggered) {
    if (percent >= m_autoStopThreshold) {
      m_autoStopTriggered = true;

      wxCommandEvent evt(EVT_MEMORY_AUTO_RESET);
      wxQueueEvent(m_weatherRouting, evt.Clone());
    }
  }

  if (m_autoStopTriggered && percent < m_autoStopThreshold - 5.0) {
    m_autoStopTriggered = false;
  }

  // ------------------------------------------------------------
  // UI gauge + label updates (safe to keep)
  // ------------------------------------------------------------
  if (m_textLabel) {
    wxTheApp->CallAfter([this, percent]() {
      if (!m_textLabel) return;
      m_textLabel->SetLabel(wxString::Format("%.1f%%", percent));
      m_textLabel->Refresh();
    });
  }

  if (m_usageGauge) {
    wxTheApp->CallAfter([this, percent]() {
      if (m_usageGauge) m_usageGauge->SetValue((int)percent);
    });
  }

  if (m_logToFile) {
    wxLogMessage("ASM: %.1f%%", percent);
  }
}


// ------------------------------------------------------------
//  Alert Update Helper
// ------------------------------------------------------------
// ------------------------------------------------------------
//  Settings
// ------------------------------------------------------------

void AddressSpaceMonitor::SetAlertEnabled(bool enabled) {
  if (!m_isValidState.load()) return;
  m_alertEnabled = enabled;
}


void AddressSpaceMonitor::SetAlertThreshold(double p)
{
    if (!m_isValidState.load()) return;
    m_alertThreshold = p;
    SaveSettings();
}

void AddressSpaceMonitor::SetAutoStopEnabled(bool enabled)
{
    if (!m_isValidState.load()) return;
    m_autoStopEnabled = enabled;
    SaveSettings();
}

void AddressSpaceMonitor::SetAutoStopThreshold(double p)
{
    if (!m_isValidState.load()) return;
    m_autoStopThreshold = p;
    SaveSettings();
}

void AddressSpaceMonitor::SetMemoryCheckInterval(int ms)
{
    if (!m_isValidState.load()) return;
    m_memoryCheckIntervalMs = ms;
    SaveSettings();
}

void AddressSpaceMonitor::SetLoggingEnabled(bool enabled)
{
    if (!m_isValidState.load()) return;
    m_logToFile = enabled;
}

void AddressSpaceMonitor::SetUsageGauge(wxGauge* g)
{
    wxTheApp->CallAfter([this, g]() {
        if (!m_isValidState.load()) return;
        m_usageGauge = g;
    });
}

void AddressSpaceMonitor::SetTextLabel(wxStaticText* t)
{
    wxTheApp->CallAfter([this, t]() {
        if (!m_isValidState.load()) return;
        m_textLabel = t;
    });
}

// ------------------------------------------------------------
//  Memory Queries
// ------------------------------------------------------------
size_t AddressSpaceMonitor::GetUsedAddressSpace() const
{
    if (!m_isValidState.load()) return 0;

    MEMORY_BASIC_INFORMATION mbi;
    unsigned char* addr = nullptr;
    size_t used = 0;
    bool failed = false;

    while (true) {
        SIZE_T r = VirtualQuery(addr, &mbi, sizeof(mbi));
        if (r != sizeof(mbi)) {
            if (addr == nullptr) failed = true;
            break;
        }
        if (mbi.State == MEM_COMMIT)
            used += mbi.RegionSize;
        addr += mbi.RegionSize;
    }

    if (failed && !m_degraded) {
        m_degraded = true;
        wxLogWarning("ASM: VirtualQuery failed, disabling monitoring");
        wxTheApp->CallAfter([]() {
            wxMessageBox(
                _("Address space monitoring is not available on this system."),
                _("Weather Routing - Address Space Monitor"),
                wxOK | wxICON_WARNING);
        });
    }

    return used;
}

size_t AddressSpaceMonitor::GetTotalAddressSpace() const
{
    if (!m_isValidState.load() || m_degraded) return 0;
    return 0x80000000ULL; // 2GB
}

double AddressSpaceMonitor::GetUsagePercent() const
{
    if (!m_isValidState.load() || m_degraded) return 0.0;
    size_t total = GetTotalAddressSpace();
    if (total == 0) return 0.0;
    return 100.0 * GetUsedAddressSpace() / total;
}

// ------------------------------------------------------------
//  Settings Persistence
// ------------------------------------------------------------
void AddressSpaceMonitor::SaveSettings()
{
    wxConfigBase* c = wxConfig::Get();
    if (!c) return;

    c->Write("/PlugIns/WeatherRouting/AddressSpaceAlertThreshold", m_alertThreshold);
    c->Write("/PlugIns/WeatherRouting/AddressSpaceAutoStopThreshold", m_autoStopThreshold);
    c->Write("/PlugIns/WeatherRouting/AddressSpaceAutoStopEnabled", m_autoStopEnabled);
    c->Write("/PlugIns/WeatherRouting/AddressSpaceLogToFile", m_logToFile);
    c->Write("/PlugIns/WeatherRouting/AddressSpaceCheckIntervalMs", m_memoryCheckIntervalMs);
}

void AddressSpaceMonitor::LoadSettings()
{
    wxConfigBase* c = wxConfig::Get();
    if (!c) return;

    c->Read("/PlugIns/WeatherRouting/AddressSpaceAlertThreshold", &m_alertThreshold, 80.0);
    c->Read("/PlugIns/WeatherRouting/AddressSpaceAutoStopThreshold", &m_autoStopThreshold, 85.0);
    c->Read("/PlugIns/WeatherRouting/AddressSpaceAutoStopEnabled", &m_autoStopEnabled, true);
    c->Read("/PlugIns/WeatherRouting/AddressSpaceLogToFile", &m_logToFile, false);
    c->Read("/PlugIns/WeatherRouting/AddressSpaceCheckIntervalMs", &m_memoryCheckIntervalMs, 500);
}

#endif // __WXMSW__
