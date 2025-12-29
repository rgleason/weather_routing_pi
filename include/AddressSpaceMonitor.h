#ifdef __WXMSW__

#ifndef ADDRESSSPACEMONITOR_H
#define ADDRESSSPACEMONITOR_H

#include <wx/wx.h>
#include <wx/gauge.h>
#include <wx/dialog.h>

// Forward declaration
class AddressSpaceMonitor;

class MemoryAlertDialog : public wxDialog {
public:
  MemoryAlertDialog(wxWindow* parent, AddressSpaceMonitor* monitor);
  void UpdateMemoryInfo(double usedGB, double totalGB, double percent);
  void ClearMonitor();

private:
  void OnHide(wxCommandEvent& event);   // FIX: Add OnHide for Hide button
  void OnClose(wxCommandEvent& event);  // For programmatic close from Settings
  void OnCloseWindow(wxCloseEvent& event);  // For X button

  AddressSpaceMonitor* m_monitor;
  wxStaticText* m_messageText;

  friend class AddressSpaceMonitor;  // Allow CloseAlert to access private
                                     // members
};

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

  void Shutdown();
  void CheckAndAlert();
  void UpdateAlertIfShown(double usedGB, double totalGB, double percent);
  void DismissAlert();  // FIX: Move from MemoryAlertDialog to here - for
                        // Settings checkbox

  void SetThresholdPercent(double percent);
  void SetLoggingEnabled(bool enabled);
  void SetAlertEnabled(bool enabled);
  void SetGauge(wxGauge* gauge);

  size_t GetUsedAddressSpace() const;
  size_t GetTotalAddressSpace() const;
  double GetUsagePercent() const;

  // Validity check
  bool IsValid() const { return m_isValid; }

  // Public members that need external access
  bool
      alertDismissed;  // Public so MemoryAlertDialog and Settings can access it
  double thresholdPercent;  // FIX: Must be public so MemoryAlertDialog can
                            // access it

private:
  void ShowOrUpdateAlert(double usedGB, double totalGB, double percent);
  void CloseAlert();

  bool m_isValid;
  bool m_isShuttingDown;
  int m_instanceId;  // Track which instance this is

  bool logToFile;
  bool alertEnabled;
  wxGauge* usageGauge;
  bool alertShown;
  MemoryAlertDialog* activeAlertDialog;
};

#endif  // ADDRESSSPACEMONITOR_H

#endif  // __WXMSW__
