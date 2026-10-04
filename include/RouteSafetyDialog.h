/* Copyright (C) 2026 OpenCPN contributors. GPL v3 or later. */
#ifndef WEATHER_ROUTING_ROUTE_SAFETY_DIALOG_H
#define WEATHER_ROUTING_ROUTE_SAFETY_DIALOG_H

#include <memory>
#include <wx/dialog.h>
#include <wx/timer.h>
#include "RouteSafetyCheck.h"

class wxListCtrl;
class wxListEvent;
class wxTextCtrl;
class wxStaticText;
class wxButton;
class wxCheckBox;
class piDC;
class weather_routing_pi;

class RouteSafetyDialog : public wxDialog {
public:
  RouteSafetyDialog(weather_routing_pi& plugin, const wxString& route_guid,
                    double minimum_depth_m, double land_margin_nm);
  ~RouteSafetyDialog() override;
  bool Render(piDC& dc, PlugIn_ViewPort& vp);
  bool SelectMarker(const wxPoint& point, PlugIn_ViewPort& vp);
  void Start();
  bool IsChecking() const {
    return check_ && check_->State() == weather_routing::RouteCheckState::Running;
  }
  /** Existing isolated-profile test entry point; never used by ordinary UI. */
  void SetTestOutput(const wxString& path, const wxString& action);
private:
  void OnTimer(wxTimerEvent&);
  void RefreshReport();
  void ShowFinding();
  bool Snapshot(std::vector<weather_routing::SafetyCheckWaypoint>& points,
                wxString& route_name, wxString& error) const;
  bool ValidateSnapshot();
  void WriteTestOutput();
  weather_routing_pi& plugin_;
  wxString route_guid_, route_name_;
  std::string identity_;
  std::unique_ptr<weather_routing::RouteSafetyCheck> check_;
  wxTimer timer_;
  wxTextCtrl *minimum_depth_, *land_margin_, *details_;
  wxStaticText* summary_;
  wxListCtrl* findings_;
  wxButton *recheck_, *cancel_, *show_finding_;
  wxCheckBox* show_markers_;
  long selected_{-1};
  long long next_validation_ms_{0};
  wxString test_output_, test_action_;
  bool test_written_{false}, test_action_done_{false};
};
#endif
