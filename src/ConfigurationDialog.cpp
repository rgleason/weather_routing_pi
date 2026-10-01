/***************************************************************************
 *
 * Project:  OpenCPN Weather Routing plugin
 * Author:   Sean D'Epagnier
 *
 ***************************************************************************
 *   Copyright (C) 2015 by Sean D'Epagnier                                 *
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 3 of the License, or     *
 *   (at your option) any later version.                                   *
 *                                                                         *
 *   This program is distributed in the hope that it will be useful,       *
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of        *
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the         *
 *   GNU General Public License for more details.                          *
 *                                                                         *
 *   You should have received a copy of the GNU General Public License     *
 *   along with this program; if not, write to the                         *
 *   Free Software Foundation, Inc.,                                       *
 *   51 Franklin Street, Fifth Floor, Boston, MA 02110-1301,  USA.         *
 ***************************************************************************
 *
 */

#include "WeatherRoutingMessageDialog.h"
#include "SystemMemory.h"
#include "ModernNativeRoute.h"
#include "WeatherRoutingWxCompat.h"
#include <wx/wx.h>
#include "WeatherRoutingFileDialog.h"

#include <stdlib.h>
#include <math.h>
#include <time.h>

#include "tinyxml.h"

#include "Utilities.h"
#include "Boat.h"
#include "RouteMapOverlay.h"
#include "DepartureScheduler.h"
#include "RoutingResourcePolicy.h"
#include "ConfigurationDialog.h"
#include "ChartSafetyDefaults.h"
#include "BoatDialog.h"
#include "weather_routing_pi.h"
#include "WeatherRouting.h"
#include "ShorelineManager.h"
#include "ShorelineSpec.h"
#include "icons.h"

#ifdef __OCPN__ANDROID__
#include <QAbstractSpinBox>
#include <QDateTimeEdit>
#include <QLineEdit>
#include <QSignalBlocker>
#include <QTabWidget>
#include <QTabBar>
#include <wx/calctrl.h>
#include <QCalendarWidget>
#include <QSpinBox>
#include <QDoubleSpinBox>
#include "AndroidDialogHeader.h"
#endif

#include <algorithm>
#include <iterator>

namespace {

#ifdef __OCPN__ANDROID__
void FitTabletConfigurationPage(wxScrolledWindow* page) {
  // wxQt caches the best size of static-box parents before the tablet style
  // enlarges their controls. Invalidate the whole moved control tree before
  // calculating the virtual size, including pages hidden during construction.
  // Reflow the section's help paragraphs at its current visible width.
  page->SendSizeEvent();
  const auto invalidate = [](auto&& self, wxWindow* window) -> void {
    for (auto* child : window->GetChildren()) self(self, child);
    window->InvalidateBestSize();
  };
  invalidate(invalidate, page);
  page->Layout();
  page->FitInside();
}

wxDateTime TabletWallPicker(const wxDateTime& wall) {
  const auto value = QDateTime::fromSecsSinceEpoch(wall.GetTicks(), Qt::UTC);
  return wxDateTime(value.date().day(),
      static_cast<wxDateTime::Month>(value.date().month() - 1),
      value.date().year(), value.time().hour(), value.time().minute(),
      value.time().second());
}

void SetTabletPickerText(wxWindow* control, const wxDateTime& value,
                         bool isDate) {
  if (!value.IsValid()) return;
  auto* text = control->GetHandle()->findChild<QLineEdit*>();
  if (!text) return;
  // The host's generic date picker uses a locale format which can collapse to
  // the year when SetValue is called. Keep the editable value unambiguous.
  const wxString formatted = isDate ? value.FormatISODate()
                                    : value.FormatISOTime();
  const QSignalBlocker blocker(text);
  text->setText(QString::fromUtf8(formatted.utf8_str()));
}

wxDateTime TabletPickerValue(wxWindow* control, bool isDate) {
  auto* edit = qobject_cast<QDateTimeEdit*>(control->GetHandle());
  if (!edit) edit = control->GetHandle()->findChild<QDateTimeEdit*>();
  if (edit && edit->date().isValid() && edit->time().isValid()) {
    const QDate date = edit->date();
    const QTime time = edit->time();
    return wxDateTime(date.day(), static_cast<wxDateTime::Month>(date.month() - 1),
                      date.year(), time.hour(), time.minute(), time.second());
  }
  // This host uses wx's generic composite date/time controls. Their cached
  // value can be invalid while the Qt text editor displays a valid value.
  auto* text = control->GetHandle()->findChild<QLineEdit*>();
  if (!text) return wxDateTime();
  const wxString value = wxString::FromUTF8(text->text().toUtf8().constData());
  wxDateTime parsed;
  if (isDate) parsed.ParseISODate(value);
  else parsed.ParseISOTime(value);
  return parsed;
}

void StyleTabletControls(wxWindow* parent) {
  for (auto node = parent->GetChildren().GetFirst(); node;
       node = node->GetNext()) {
    wxWindow* child = node->GetData();
    int points = 0;
    int minHeight = 0;
    if (wxDynamicCast(child, wxStaticText)) points = 14;
    else if (wxDynamicCast(child, wxButton)) {
      points = 15;
      minHeight = 54;
    } else if (wxDynamicCast(child, wxCheckBox) ||
               wxDynamicCast(child, wxRadioButton)) {
      points = 14;
      minHeight = 46;
    } else if (wxDynamicCast(child, wxChoice) ||
               wxDynamicCast(child, wxComboBox) ||
               wxDynamicCast(child, wxTextCtrl) ||
               wxDynamicCast(child, wxSpinCtrl) ||
               wxDynamicCast(child, wxSpinCtrlDouble)) {
      points = 14;
      minHeight = 49;
    }
    if (points) {
      wxFont font = child->GetFont();
      font.SetPointSize(points);
      child->SetFont(font);
#ifdef __WXQT__
      if (wxDynamicCast(child, wxChoice) || wxDynamicCast(child, wxComboBox)) {
        WR_StyleAndroidCombo(child);
        child->GetHandle()->setStyleSheet(
            "QComboBox { font-size: 16pt; min-height: 52px; } "
            "QAbstractItemView::item { min-height: 56px; padding: 8px; }");
      } else if (wxDynamicCast(child, wxButton))
        child->GetHandle()->setStyleSheet(
            "QPushButton { font-size: 16pt; min-height: 54px; padding: 5px; "
            "border: 1px solid #9fb9c6; border-radius: 8px; "
            "color: #173849; background-color: white; }");
      else if (wxDynamicCast(child, wxCheckBox) || wxDynamicCast(child, wxRadioButton))
        child->GetHandle()->setStyleSheet(
            "QCheckBox, QRadioButton { font-size: 16pt; min-height: 52px; } "
            "QCheckBox::indicator, QRadioButton::indicator { width: 24px; height: 24px; }");
      else if (wxDynamicCast(child, wxStaticText))
        child->GetHandle()->setStyleSheet(child->GetHandle()->styleSheet() +
            " QLabel { font-size: 16pt; }");
#endif
    }
    if (minHeight) {
      wxSize size = child->GetMinSize();
      child->SetMinSize(wxSize(size.x, wxMax(size.y, minHeight)));
    }
    StyleTabletControls(child);
  }
}
#endif

constexpr int kDefaultConfigurationWidthDip = 1320;
constexpr int kDefaultConfigurationHeightDip = 900;
constexpr int kConfigurationScreenMarginDip = 40;

wxSize DefaultConfigurationDialogSize(wxWindow* window) {
  const wxSize best = window->GetBestSize();
  const wxSize preferred = WR_FromDIP(window,
      wxSize(kDefaultConfigurationWidthDip, kDefaultConfigurationHeightDip));
  const int margin = WR_FromDIP(window, kConfigurationScreenMarginDip);
  const wxSize display = wxGetClientDisplayRect().GetSize();
  const wxSize available(std::max(1, display.x - margin),
                         std::max(1, display.y - margin));
  return wxSize(std::min(available.x, std::max(best.x, preferred.x)),
                std::min(available.y, std::max(best.y, preferred.y)));
}

bool GetWaypointByGuid(const wxString& guid, PlugIn_Waypoint* waypoint) {
  return !guid.IsEmpty() && GetSingleWaypoint(guid, waypoint);
}

bool FindWaypointByName(const wxString& name, PlugIn_Waypoint* waypoint,
                        wxString* guid = nullptr) {
  wxArrayString waypoint_guids = GetWaypointGUIDArray();
  for (const auto& waypoint_guid : waypoint_guids) {
    PlugIn_Waypoint candidate;
    if (!GetSingleWaypoint(waypoint_guid, &candidate)) continue;
    if (candidate.m_MarkName != name) continue;

    if (waypoint) *waypoint = candidate;
    if (guid) *guid = waypoint_guid;
    return true;
  }
  return false;
}

wxString WaypointNameForGuid(const wxString& guid) {
  PlugIn_Waypoint waypoint;
  if (GetWaypointByGuid(guid, &waypoint)) return waypoint.m_MarkName;
  return wxEmptyString;
}

wxString GetWaypointGuidForSelection(wxComboBox* combo) {
  if (!combo) return wxEmptyString;

  int selection = combo->GetSelection();
  if (selection >= 0) {
    auto* data = dynamic_cast<wxStringClientData*>(combo->GetClientObject(selection));
    if (data) return data->GetData();
  }

  wxString guid;
  FindWaypointByName(combo->GetValue(), nullptr, &guid);
  return guid;
}

void SelectWaypointByGuid(wxComboBox* combo, const wxString& guid) {
  if (guid.IsEmpty()) return;
  for (unsigned i = 0; i < combo->GetCount(); ++i) {
    auto* data = dynamic_cast<wxStringClientData*>(combo->GetClientObject(i));
    if (data && data->GetData() == guid) {
      combo->SetSelection(i);
      return;
    }
  }
}

int RoutingEffortSelection(int percent) {
  switch (weather_routing::NormalizeRoutingEffortPercent(percent)) {
    case 150:
      return 1;
    case 200:
      return 2;
    case 400:
      return 3;
    default:
      return 0;
  }
}

int RoutingEffortPercentForSelection(int selection) {
  static constexpr int kEffortPercent[] = {100, 150, 200, 400};
  if (selection < 0 ||
      selection >= static_cast<int>(WXSIZEOF(kEffortPercent)))
    return weather_routing::kDefaultRoutingEffortPercent;
  return kEffortPercent[selection];
}

}  // namespace

ConfigurationDialog::ConfigurationDialog(WeatherRouting& weatherrouting)
#ifndef __WXOSX__
    : ConfigurationDialogBase(&weatherrouting),
#else
    : ConfigurationDialogBase(&weatherrouting, wxID_ANY,
                              _("WeatherRouting Configuration"),
                              wxDefaultPosition, wxDefaultSize,
                              wxDEFAULT_DIALOG_STYLE | wxSTAY_ON_TOP),
#endif
      m_WeatherRouting(weatherrouting),
      m_bBlockUpdate(false) {
  m_cShorelineResolution->Bind(wxEVT_CHOICE,
      [this](wxCommandEvent& event) {
        const int selection = m_cShorelineResolution->GetSelection();
        if (selection < 0 || selection >=
                static_cast<int>(m_shorelineChoiceResolutions.size())) return;
        const int resolution = m_shorelineChoiceResolutions[selection];
        if (resolution >= 0 && !weather_routing::ShorelineManager::Available(resolution)) {
          WR_MessageBox(_("This shoreline resolution is not installed. "
                         "Use Shoreline data to install the approved "
                         "High or Full dataset before selecting it. Your route "
                         "selection has not changed."),
                       _("Shoreline data"), wxOK | wxICON_INFORMATION, this);
          UpdateEngineControls();
          return;
        }
        HandleEngineEdit(event.GetEventObject());
      });
  m_bShorelineData->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) {
    weather_routing::ShorelineManager::Show(this);
    UpdateEngineControls();
  });
  m_sQuickHeadingStepDegrees->Bind(wxEVT_SPINCTRLDOUBLE,
      [this](wxSpinDoubleEvent& event) { HandleEngineEdit(event.GetEventObject()); });
  m_cbUseExperimentalChartSafety->Bind(
      wxEVT_CHECKBOX, &ConfigurationDialog::OnChartSafetyChanged, this);
  m_cbEnforceExperimentalChartSafety->Bind(
      wxEVT_CHECKBOX, &ConfigurationDialog::OnChartSafetyChanged, this);
  const wxString detect_land_note =
      _("Detect Land uses the selected GSHHG shoreline when chart enforcement "
        "is off. Crude, Low and Intermediate are bundled; High and Full can be "
        "installed from Shoreline data. With both chart options "
        "enabled on a compatible host, loaded charts decide route land and "
        "depth safety; GSHHG helps the initial search.");
  m_cbDetectLand->SetToolTip(detect_land_note);
  m_sSafetyMarginLand->SetToolTip(
      detect_land_note + _("\n\nSpecify a minimum distance in nautical miles "
                           "to maintain from land during routing "
                           "calculations."));
  m_sMinimumDepthMeters->SetToolTip(
      _("Reject chart-aware routes through water charted shallower than this "
        "depth in metres. Zero disables the depth constraint. A positive "
        "value requires enabled and enforced chart-aware safety."));
  m_cbUseExperimentalChartSafety->SetToolTip(
      _("Inspect the highest-priority loaded chart, including supported "
        "o-charts, for land and depth diagnostics. Leave the requirement "
        "below off to compare a GSHHS route; that comparison is not "
        "depth-validated."));
  m_cbEnforceExperimentalChartSafety->SetToolTip(
      _("Reject route candidates which fail loaded-chart land, drying or "
        "configured minimum-depth checks. For a manual GSHHS comparison, "
        "uncheck this and set Minimum Depth to 0 m."));

  wxFileConfig* pConf = GetOCPNConfigObject();
  pConf->SetPath(_T( "/PlugIns/WeatherRouting" ));
  m_cbUseExperimentalChartSafety->SetValue(
      (bool)pConf->Read(
          _T("UseExperimentalChartSafety"),
          weather_routing::chart_safety_defaults::kCheckLoadedCharts));
  m_cbEnforceExperimentalChartSafety->SetValue(
      (bool)pConf->Read(
          _T("EnforceExperimentalChartSafety"),
          weather_routing::chart_safety_defaults::kRequireChartDepthChecks));
  if (!m_WeatherRouting.HasEnhancedChartSafety()) {
    const wxString unavailable =
        _("The optional chart-backed safety service is not available in this "
          "stock OpenCPN build. Detect Land continues to use standard GSHHS "
          "shoreline checks.");
    m_cbUseExperimentalChartSafety->SetValue(false);
    m_cbEnforceExperimentalChartSafety->SetValue(false);
    m_cbUseExperimentalChartSafety->Enable(false);
    m_cbEnforceExperimentalChartSafety->Enable(false);
    m_cbUseExperimentalChartSafety->SetToolTip(unavailable);
    m_cbEnforceExperimentalChartSafety->SetToolTip(unavailable);
  }
  for (const wxString& zone : marine_time::AvailableTimeZones())
    m_cTimeZone->Append(zone);
  wxString displayZone = marine_time::SystemTimeZone();
  if (m_cTimeZone->FindString(displayZone) == wxNOT_FOUND) {
    displayZone = "UTC";
  }
  m_cTimeZone->SetStringSelection(displayZone);
  m_cbUseLocalTimeZone->SetValue(false);
  m_cTimeZone->Enable(false);
  UpdateRoutingTimeModeControls();

#ifdef __OCPN__ANDROID__
  // Route settings keep the shared immediate-save behaviour. Done commits any
  // text still being edited by Qt before returning to the workspace.
  WR_AddAndroidDoneHeader(this, _("Route setup"), [this]() {
    // wxQt does not always emit a spin update when a value is typed with the
    // Android keyboard. Commit the visible editor values before closing.
    for (auto* spin : GetHandle()->findChildren<QAbstractSpinBox*>())
      spin->interpretText();
    Update();
    Hide();
  });
  auto* navigation = new wxPanel(this, wxID_ANY);
  auto* navigationSizer = new wxBoxSizer(wxHORIZONTAL);
  auto* sectionPicker = new wxChoice(navigation, wxID_ANY);
  for (size_t i = 0; i < m_notebook7->GetPageCount(); ++i)
    sectionPicker->Append(m_notebook7->GetPageText(i));
  sectionPicker->SetSelection(0);
  sectionPicker->SetMinSize(wxSize(240, 64));
  navigationSizer->Add(sectionPicker, 1, wxEXPAND | wxALL, 8);
  sectionPicker->Bind(wxEVT_CHOICE, [this, sectionPicker](wxCommandEvent&) {
    m_notebook7->SetSelection(sectionPicker->GetSelection());
  });
  m_notebook7->Bind(wxEVT_NOTEBOOK_PAGE_CHANGED,
      [this, sectionPicker](wxBookCtrlEvent& event) {
        sectionPicker->SetSelection(event.GetSelection());
        if (event.GetSelection() == 5) UpdateAndroidMemoryStatus();
        CallAfter([this]() {
          FitTabletConfigurationPage(static_cast<wxScrolledWindow*>(
              m_notebook7->GetCurrentPage()));
        });
        event.Skip();
      });
  if (auto* tabs = qobject_cast<QTabWidget*>(m_notebook7->GetHandle()))
    tabs->tabBar()->hide();
  navigation->SetSizer(navigationSizer);
  GetSizer()->Insert(1, navigation, 0, wxEXPAND);
  wxFlexGridSizer* boatRow =
      static_cast<wxFlexGridSizer*>(m_tBoat->GetContainingSizer());
  boatRow->Detach(m_bBoatFilename);
  boatRow->Detach(m_bEditBoat);
  boatRow->Detach(m_tBoat);
  m_tBoat->Hide();
  m_androidBoatName = new wxStaticText(m_tBoat->GetParent(), wxID_ANY,
      _("Choose a boat file"));
  boatRow->Add(m_androidBoatName, 0, wxEXPAND | wxALL, 8);
  boatRow->SetRows(0);
  boatRow->SetCols(1);
  wxBoxSizer* boatActions = new wxBoxSizer(wxHORIZONTAL);
  m_bBoatFilename->SetLabel(_("Choose boat file"));
  m_bEditBoat->SetLabel(_("Edit boat and polars"));
  for (auto* button : {m_bBoatFilename, m_bEditBoat})
    button->SetMinSize(wxSize(350, 72));
  boatActions->Add(m_bBoatFilename, 1, wxEXPAND | wxALL, 5);
  boatActions->Add(m_bEditBoat, 1, wxEXPAND | wxALL, 5);
  boatRow->Add(boatActions, 0, wxEXPAND);
  // wxQt measures checkbox text too narrowly in the generated horizontal
  // sizers. Reserve the complete labels without changing the desktop grid.
  m_cbUseCurrentTime->SetMinSize(WR_FromDIP(this, wxSize(285, 46)));
  m_staticTextDepartureStep->SetMinSize(wxSize(350, 72));
  m_staticTextDepartureRange->SetMinSize(wxSize(350, 72));
  m_cbUseLocalTimeZone->SetMinSize(WR_FromDIP(this, wxSize(320, 46)));
  m_cbDepartureTimeOptimizationEnabled->SetMinSize(
      WR_FromDIP(this, wxSize(370, 46)));
  m_cbUseExperimentalChartSafety->SetMinSize(
      WR_FromDIP(this, wxSize(750, 40)));
  m_cbEnforceExperimentalChartSafety->SetMinSize(
      WR_FromDIP(this, wxSize(700, 40)));
  m_cbUseGrib->SetMinSize(WR_FromDIP(this, wxSize(110, 40)));
  m_cbAllowDataDeficient->SetMinSize(WR_FromDIP(this, wxSize(440, 40)));
  m_cbUseReverseReachabilityRecovery->SetLabel(_("Recover final approach"));
  m_cbUseReverseReachabilityRecovery->SetMinSize(
      WR_FromDIP(this, wxSize(350, 40)));
  m_cbAvoidCycloneTracks->SetMinSize(WR_FromDIP(this, wxSize(430, 40)));
  m_cbUseMotor->SetMinSize(WR_FromDIP(this, wxSize(340, 40)));
  m_cbUseOptimalAngles->SetMinSize(WR_FromDIP(this, wxSize(310, 40)));
  m_cbInvertedRegions->SetMinSize(WR_FromDIP(this, wxSize(300, 40)));
  m_cbAnchoring->SetMinSize(WR_FromDIP(this, wxSize(240, 40)));
  m_cRoutingEffortPercent->Clear();
  for (const wxString& label : {_("100% / Standard"), _("150% / Extended"),
                                _("200% / Thorough"), _("400% / Exhaustive")})
    m_cRoutingEffortPercent->Append(label);
  m_cRoutingEffortPercent->SetSelection(0);
  m_cRoutingEffortPercent->SetMinSize(wxSize(340, 72));
  m_pBasic->Layout();
  m_pAdvanced->Layout();
  m_pAdvanced->FitInside();
  auto* enginePage = m_notebook7->GetPage(5);
  m_androidMemoryStatus = new wxStaticText(enginePage, wxID_ANY,
      _("Checking available memory…"));
  wxFont memoryFont = m_androidMemoryStatus->GetFont();
  memoryFont.SetPointSize(16);
  m_androidMemoryStatus->SetFont(memoryFont);
  // Each page's outer horizontal sizer adds the scrollbar inset. Put status
  // above the controls in its inner vertical sizer, not beside that column.
  enginePage->GetSizer()->GetItem(static_cast<size_t>(0))->GetSizer()->Insert(
      0, m_androidMemoryStatus, 0, wxEXPAND | wxALL, 12);

  for (auto* endpoint : {m_cStart, m_cEnd}) {
    auto* combo = qobject_cast<QComboBox*>(endpoint->GetHandle());
    if (!combo) combo = endpoint->GetHandle()->findChild<QComboBox*>();
    if (combo) combo->setEditable(false);
  }
  WR_StyleAndroidControls(this);
  // This host's wxQt does not consistently forward values typed into spin
  // controls. Use the native value signal while preserving edited-field
  // semantics for a multi-route selection.
  const auto connectSpins = [this](auto&& self, wxWindow* parent) -> void {
    for (auto* child : parent->GetChildren()) {
      const auto changed = [this, child]() {
        if (m_bBlockUpdate) return;
        wxCommandEvent event;
        event.SetEventObject(child);
        OnUpdate(event);
      };
      if (wxDynamicCast(child, wxSpinCtrl)) {
        auto* spin = qobject_cast<QSpinBox*>(child->GetHandle());
        if (!spin) spin = child->GetHandle()->findChild<QSpinBox*>();
        if (spin) QObject::connect(spin,
            static_cast<void (QSpinBox::*)(int)>(&QSpinBox::valueChanged),
            child->GetHandle(), [changed](int) { changed(); });
      } else if (wxDynamicCast(child, wxSpinCtrlDouble)) {
        auto* spin = qobject_cast<QDoubleSpinBox*>(child->GetHandle());
        if (!spin) spin = child->GetHandle()->findChild<QDoubleSpinBox*>();
        if (spin) QObject::connect(spin,
            static_cast<void (QDoubleSpinBox::*)(double)>(&QDoubleSpinBox::valueChanged),
            child->GetHandle(), [changed](double) { changed(); });
      }
      self(self, child);
    }
  };
  connectSpins(connectSpins, this);
  if (auto* calendar = m_dpStartDate->GetCalendar()) {
    wxFont calendarFont = calendar->GetFont();
    calendarFont.SetPointSize(16);
    calendar->SetFont(calendarFont);
    calendar->SetMinSize(wxSize(600, 480));
    calendar->GetHandle()->setStyleSheet(
        "QCalendarWidget { font-size: 16pt; min-width: 580px; min-height: 460px; } "
        "QCalendarWidget QToolButton { min-height: 60px; font-size: 16pt; } "
        "QCalendarWidget QAbstractItemView { font-size: 16pt; }");
  }
  m_pBasic->SetMinSize(wxSize(0, 0));
  for (size_t i = 0; i < m_notebook7->GetPageCount(); ++i)
    FitTabletConfigurationPage(static_cast<wxScrolledWindow*>(
        m_notebook7->GetPage(i)));
  Bind(wxEVT_SHOW, [this](wxShowEvent& event) {
    if (event.IsShown()) CallAfter([this]() {
      FitTabletConfigurationPage(static_cast<wxScrolledWindow*>(
          m_notebook7->GetCurrentPage()));
    });
    event.Skip();
  });

  wxSize sz = ::wxGetDisplaySize();
  SetSize(0, 0, sz.x, sz.y - 40);
#else
  const wxSize default_size = DefaultConfigurationDialogSize(this);
  long width = default_size.x;
  long height = default_size.y;
  pConf->Read(_T("ConfigurationWidth"), &width,
              static_cast<long>(default_size.x));
  pConf->Read(_T("ConfigurationHeight"), &height,
              static_cast<long>(default_size.y));
  if (width > 0 && height > 0)
    SetSize(wxSize(static_cast<int>(width), static_cast<int>(height)));

  wxPoint p = GetPosition();
  pConf->Read(_T ( "ConfigurationX" ), &p.x, p.x);
  pConf->Read(_T ( "ConfigurationY" ), &p.y, p.y);
  SetPosition(p);
#endif
}

#ifdef __OCPN__ANDROID__
void ConfigurationDialog::SetAndroidEndpoint(bool start, const wxString& name) {
  auto* choice = start ? m_cStart : m_cEnd;
  if (start) {
    m_rbStartFromBoat->SetValue(false);
    m_rbStartWaypointSelection->SetValue(false);
    m_rbStartPositionSelection->SetValue(true);
  } else {
    m_rbEndWaypointSelection->SetValue(false);
    m_rbEndPositionSelection->SetValue(true);
  }
  AddPositions(start);
  choice->SetStringSelection(name);
  m_edited_controls.push_back(start ? static_cast<wxObject*>(m_rbStartPositionSelection)
                                    : static_cast<wxObject*>(m_rbEndPositionSelection));
  m_edited_controls.push_back(choice);
  Update();
  ShowAndroidSection(0);
}

void ConfigurationDialog::ShowAndroidSection(size_t section) {
  if (section < m_notebook7->GetPageCount()) m_notebook7->SetSelection(section);
  wxWindow* canvas = GetCanvasByIndex(0);
  const wxSize size = canvas ? canvas->GetClientSize() : ::wxGetDisplaySize();
  SetSize(20, 15, size.x - 40, size.y - 30);
  if (section == 5) UpdateAndroidMemoryStatus();
  Show();
  Raise();
}

void ConfigurationDialog::UpdateAndroidMemoryStatus() {
  if (!m_androidMemoryStatus) return;
  const auto routes = m_WeatherRouting.CurrentRouteMaps(false);
  if (routes.empty()) return;
  const auto config = routes.front()->GetConfiguration();
  const auto available = weather_routing::AvailablePhysicalMemoryMiB();
  const auto cache = weather_routing::EvaluateGribTimelineCacheAdmission(
      config.SelectedGribTimelineCacheMiB(), config.IsFastEngine(), available);
  const wxString text = available
      ? wxString::Format(_("Available RAM: %llu MiB. GRIB cache allowance now: %d MiB.\nCache retention and concurrent routes adapt to RAM; forecast resolution and routing effort are preserved."),
          static_cast<unsigned long long>(available), cache.effective_mib)
      : _("RAM information unavailable. Conservative Android cache and concurrency limits apply.");
  WR_WrapAndroidText(m_androidMemoryStatus, text,
      wxMax(350, GetClientSize().x - 80));
  m_androidMemoryStatus->Show();
  m_androidMemoryStatus->GetParent()->Layout();
  if (auto* page = wxDynamicCast(m_androidMemoryStatus->GetParent(), wxScrolledWindow))
    page->FitInside();
}

void ConfigurationDialog::UpdateAndroidBoatName() {
  if (!m_androidBoatName) return;
  const wxString filename = wxFileName(m_tBoat->GetValue()).GetFullName();
  WR_WrapAndroidText(m_androidBoatName,
      filename.empty() ? _("Choose a boat file") : filename,
      wxMax(350, GetClientSize().x - 120));
  m_androidBoatName->GetParent()->Layout();
}
#endif

void ConfigurationDialog::RefreshTimeZoneControls() {
  wxString displayZone =
      m_WeatherRouting.m_SettingsDialog.DisplayTimeZone();
  if (m_cTimeZone->FindString(displayZone) == wxNOT_FOUND) {
    displayZone = marine_time::SystemTimeZone();
  }
  if (m_cTimeZone->FindString(displayZone) == wxNOT_FOUND) displayZone = "UTC";
  m_cTimeZone->SetStringSelection(displayZone);
  m_cbUseLocalTimeZone->SetValue(
      m_WeatherRouting.m_SettingsDialog.UseLocalTimeZone());
  m_cTimeZone->Enable(m_cbUseLocalTimeZone->GetValue());
}

ConfigurationDialog::~ConfigurationDialog() {
  if (getenv("WR_HEADLESS_ROUTE_TEST")) {
    wxLogMessage(
        "WR_HEADLESS_ROUTE_TEST shutdown: skipping ConfigurationDialog "
        "position save during headless app teardown.");
    return;
  }
  wxFileConfig* pConf = GetOCPNConfigObject();
  pConf->SetPath(_T( "/PlugIns/WeatherRouting" ));

  wxPoint p = GetPosition();
  pConf->Write(_T ( "ConfigurationX" ), p.x);
  pConf->Write(_T ( "ConfigurationY" ), p.y);
  const wxSize size = GetSize();
  pConf->Write(_T("ConfigurationWidth"), size.x);
  pConf->Write(_T("ConfigurationHeight"), size.y);
}

void ConfigurationDialog::EditBoat() {
  m_WeatherRouting.m_BoatDialog.LoadPolar(m_tBoat->GetValue());
  m_WeatherRouting.m_BoatDialog.Show();
}
void ConfigurationDialog::OnGribTime(wxCommandEvent& event) {
  SetStartDateTime(m_GribTimelineTime);
  Update();
}

void ConfigurationDialog::OnCurrentTime(wxCommandEvent& event) {
  SetStartDateTime(wxDateTime::Now());
  Update();
}

void ConfigurationDialog::OnUseCurrentTime(wxCommandEvent& event) {
  if (m_cbUseCurrentTime->IsChecked() &&
      m_rbRouteByDepartureTime->GetValue())
    SetStartDateTime(wxDateTime::Now());
  UpdateRoutingTimeModeControls();
  Update();
}

void ConfigurationDialog::OnTimeZoneDisplay(wxCommandEvent& event) {
  const std::list<RouteMapOverlay*> routes =
      m_WeatherRouting.CurrentRouteMaps();
  wxDateTime selectedTime;
  if (!routes.empty()) {
    const RouteMapConfiguration configuration =
        routes.front()->GetConfiguration();
    selectedTime = m_rbRouteByArrivalTime->GetValue()
                       ? configuration.PlannedArrivalTime
                       : configuration.StartTime;
  }
  if (m_rbRouteByDepartureTime->GetValue() &&
      m_cbUseCurrentTime->IsChecked())
    selectedTime = wxDateTime::Now();
  if (!selectedTime.IsValid()) selectedTime = wxDateTime::Now();

  wxString zone = m_cTimeZone->GetStringSelection();
  if (!marine_time::IsTimeZoneAvailable(zone)) {
    zone = marine_time::SystemTimeZone();
    if (!marine_time::IsTimeZoneAvailable(zone)) zone = "UTC";
    m_cTimeZone->SetStringSelection(zone);
  }
  m_WeatherRouting.m_SettingsDialog.SetDisplayTimeZone(
      m_cbUseLocalTimeZone->GetValue(), zone);
  m_cTimeZone->Enable(m_cbUseLocalTimeZone->GetValue());

  m_bBlockUpdate = true;
  SetStartDateTime(selectedTime);
  m_bBlockUpdate = false;
  m_WeatherRouting.UpdateColumns();
  m_WeatherRouting.UpdateDisplaySettings();
  Update();
}

void ConfigurationDialog::OnRoutingTimeMode(wxCommandEvent& event) {
#ifdef __OCPN__ANDROID__
  if (m_bBlockUpdate) return;
  OnValueChange(event);
#endif
  const bool arrival = m_rbRouteByArrivalTime->GetValue();
  std::list<RouteMapOverlay*> routes = m_WeatherRouting.CurrentRouteMaps();
  if (!routes.empty()) {
    const RouteMapConfiguration configuration =
        routes.front()->GetConfiguration();
    wxDateTime selectedTime =
        arrival ? configuration.PlannedArrivalTime : configuration.StartTime;
    if (!selectedTime.IsValid())
      selectedTime = arrival ? wxDateTime::Now() + wxTimeSpan::Hours(24)
                             : wxDateTime::Now();
    m_bBlockUpdate = true;
    SetStartDateTime(selectedTime);
    m_sDepartureTimeOptimizationRangeHours->SetValue(
        arrival ? std::max(1, configuration.ArrivalSearchHorizonMinutes / 60)
                : std::max(
                      0,
                      configuration.DepartureTimeOptimizationRangeMinutes /
                          60));
    m_sArrivalSafetyMarginMinutes->SetValue(
        std::max(0, configuration.ArrivalSafetyMarginMinutes));
    m_cbUseCurrentTime->SetValue(
        !arrival && configuration.UseCurrentTime);
    m_cbDepartureTimeOptimizationEnabled->SetValue(
        arrival || configuration.DepartureTimeOptimizationEnabled);
    m_bBlockUpdate = false;
  }
  OnValueChange(event);
  UpdateRoutingTimeModeControls();
  Update();
}

void ConfigurationDialog::UpdateRoutingTimeModeControls() {
  const bool arrival = m_rbRouteByArrivalTime->GetValue();
  m_staticTextPlannedTime->SetLabel(
#ifdef __OCPN__ANDROID__
      arrival ? _("Arrival deadline") : _("Departure"));
#else
      arrival ? _("Planned Arrival Time") : _("Planned Departure Time"));
#endif
  m_dpStartDate->SetToolTip(
      arrival ? _("Select the required destination arrival date.")
              : _("Select the departure date for weather routing."));
  m_tpTime->SetToolTip(
      arrival ? _("Select the required destination arrival time.")
              : _("Select the departure time for weather routing."));
  m_cbUseCurrentTime->Enable(!arrival);
  if (arrival) m_cbUseCurrentTime->SetValue(false);
  const bool manualTime = arrival || !m_cbUseCurrentTime->IsChecked();
  m_dpStartDate->Enable(manualTime);
  m_tpTime->Enable(manualTime);
  m_bGribTime->Enable(manualTime);
  m_bCurrentTime->Enable(!arrival && manualTime);

  m_cbDepartureTimeOptimizationEnabled->Enable(!arrival);
  if (arrival) m_cbDepartureTimeOptimizationEnabled->SetValue(true);
  m_staticTextDepartureRange->SetLabel(
#ifdef __OCPN__ANDROID__
      arrival ? _("Search before arrival") : _("Departure window +/-"));
#else
      arrival ? _("Search departures up to")
              : _("Range before/after departure +/-"));
#endif
  m_staticTextDepartureStep->SetLabel(
      arrival ? _("Initial search interval") : _("Step"));
  m_sDepartureTimeOptimizationRangeHours->SetToolTip(
      arrival
          ? _("Maximum number of hours before the planned arrival time in "
              "which the engine may search for a departure.")
          : _("Hours before and after the nominal departure time to test."));
  m_sDepartureTimeOptimizationStepHours->SetToolTip(
      arrival
          ? _("Initial interval between arrival-planning departure probes. "
              "The engine refines promising times automatically.")
          : _("Hours between alternative departure time calculations."));
  m_sDepartureTimeOptimizationStepMinutes->SetToolTip(
      arrival
          ? _("Initial interval between arrival-planning departure probes. "
              "The engine refines promising times automatically.")
          : _("Minutes between alternative departure time calculations."));
  m_staticTextArrivalSafetyMargin->Show(arrival);
  m_sArrivalSafetyMarginMinutes->Show(arrival);
  m_staticTextArrivalSafetyMarginMinutes->Show(arrival);
  m_tArrivalPlanningHint->Show(arrival);
  const wxSize current_size = GetSize();
  Layout();
  Fit();
  const wxSize fitted_size = GetSize();
  SetSize(wxSize(std::max(current_size.x, fitted_size.x),
                 std::max(current_size.y, fitted_size.y)));
}

void ConfigurationDialog::OnStartFromBoat(wxCommandEvent& event) {
#ifdef __OCPN__ANDROID__
  if (m_bBlockUpdate) return;
  OnValueChange(event);
#endif
  m_cStart->Enable(!m_rbStartFromBoat->GetValue());
  Update();
}

void ConfigurationDialog::OnStartFromPosition(wxCommandEvent& event) {
#ifdef __OCPN__ANDROID__
  if (m_bBlockUpdate) return;
#endif
  AddPositions(true);
#ifdef __OCPN__ANDROID__
  m_edited_controls.push_back(m_cStart);
  OnValueChange(event);
#endif
  m_cStart->Enable(m_rbStartPositionSelection->GetValue());
  Update();
}

void ConfigurationDialog::OnStartFromWaypoint(wxCommandEvent& event) {
#ifdef __OCPN__ANDROID__
  if (m_bBlockUpdate) return;
#endif
  AddWaypoints(true);
#ifdef __OCPN__ANDROID__
  m_edited_controls.push_back(m_cStart);
  OnValueChange(event);
#endif
  m_cStart->Enable(m_rbStartWaypointSelection->GetValue());
  Update();
}

void ConfigurationDialog::OnEndAtPosition(wxCommandEvent& event) {
#ifdef __OCPN__ANDROID__
  if (m_bBlockUpdate) return;
#endif
  AddPositions(false);
#ifdef __OCPN__ANDROID__
  m_edited_controls.push_back(m_cEnd);
  OnValueChange(event);
#endif
  m_cEnd->Enable(m_rbEndPositionSelection->GetValue());
  Update();
}

void ConfigurationDialog::OnEndAtWaypoint(wxCommandEvent& event) {
#ifdef __OCPN__ANDROID__
  if (m_bBlockUpdate) return;
#endif
  AddWaypoints(false);
#ifdef __OCPN__ANDROID__
  m_edited_controls.push_back(m_cEnd);
  OnValueChange(event);
#endif
  m_cEnd->Enable(m_rbEndWaypointSelection->GetValue());
  Update();
}

void ConfigurationDialog::OnAvoidCyclones(wxCommandEvent& event) { Update(); }

void ConfigurationDialog::OnUseMotor(wxCommandEvent& event) {
  const bool enabled = m_cbUseMotor->IsChecked();
  m_sMotorSpeedThreshold->Enable(enabled);
  m_sMotorSpeed->Enable(enabled);
  OnValueChange(event);
  Update();
}

void ConfigurationDialog::OnUseOptimalAngles(wxCommandEvent& event) {
  // Optimal angles refine the user's configured course envelope; they never
  // widen or overwrite it.
  Update();
}

void ConfigurationDialog::OnBoatFilename(wxCommandEvent& event) {
  WR_FileDialog openDialog(
      this, _("Select Boat File"), wxFileName(m_tBoat->GetValue()).GetPath(),
      wxT(""), wxT("xml (*.xml)|*.XML;*.xml|All files (*.*)|*.*"), wxFD_OPEN);

  if (openDialog.ShowModal() == wxID_OK) SetBoatFilename(openDialog.GetPath());
}

#define SET_CHECKBOX_FIELD(FIELD, VALUE)                          \
  do {                                                            \
    bool alltrue = true, allfalse = true;                         \
    for (std::list<RouteMapConfiguration>::iterator it =          \
             configurations.begin();                              \
         it != configurations.end(); it++)                        \
      if (VALUE)                                                  \
        allfalse = false;                                         \
      else                                                        \
        alltrue = false;                                          \
    m_cb##FIELD->Set3StateValue(alltrue    ? wxCHK_CHECKED        \
                                : allfalse ? wxCHK_UNCHECKED      \
                                           : wxCHK_UNDETERMINED); \
  } while (0)

#define SET_CHECKBOX(FIELD) SET_CHECKBOX_FIELD(FIELD, (*it).FIELD)

#define SET_CONTROL_VALUE(VALUE, CONTROL, SETTER, TYPE, NULLVALUE)          \
  do {                                                                      \
    bool allsame = true;                                                    \
    std::list<RouteMapConfiguration>::iterator it = configurations.begin(); \
    TYPE value = (VALUE);                                                   \
    for (it++; it != configurations.end(); it++) {                          \
      if (value != (VALUE)) {                                               \
        allsame = false;                                                    \
        break;                                                              \
      }                                                                     \
    }                                                                       \
    CONTROL->SETTER(allsame ? value : TYPE(NULLVALUE));                     \
    wxSize s(CONTROL->GetSize());                                           \
    if (allsame)                                                            \
      CONTROL->SetForegroundColour(wxColour(0, 0, 0));                      \
    else                                                                    \
      CONTROL->SetForegroundColour(wxColour(180, 180, 180));                \
    CONTROL->Fit();                                                         \
    CONTROL->SetSize(s);                                                    \
  } while (0)

#define SET_CONTROL(FIELD, CONTROL, SETTER, TYPE, NULLVALUE) \
  SET_CONTROL_VALUE((*it).FIELD, CONTROL, SETTER, TYPE, NULLVALUE)

static void SetConfigurationChoiceValue(wxComboBox* combo,
                                        const wxString& value) {
#ifdef __OCPN__ANDROID__
  // wxQt's editable combo does not update its displayed selection reliably
  // through SetValue() when populated position names are restored.
  const int index = combo->FindString(value, true);
  if (index != wxNOT_FOUND) {
    combo->SetSelection(index);
    return;
  }
#endif
  combo->SetValue(value);
}

#define SET_CHOICE_VALUE(FIELD, VALUE)                                        \
  do {                                                                        \
    bool allsame = true;                                                      \
    std::list<RouteMapConfiguration>::iterator it = configurations.begin();   \
    wxString value = VALUE;                                                   \
    for (it++; it != configurations.end(); it++) {                            \
      if (value != VALUE) {                                                   \
        allsame = false;                                                      \
        break;                                                                \
      }                                                                       \
    }                                                                         \
    if (allsame)                                                              \
      SetConfigurationChoiceValue(m_c##FIELD, value);                        \
    else {                                                                    \
      if (m_c##FIELD->GetString(m_c##FIELD->GetCount() - 1) != wxEmptyString) \
        m_c##FIELD->Append(wxEmptyString);                                    \
      SetConfigurationChoiceValue(m_c##FIELD, wxEmptyString);                \
    }                                                                         \
  } while (0)
#define SET_CHOICE(FIELD) SET_CHOICE_VALUE(FIELD, (*it).FIELD)

#define SET_SPIN_VALUE(FIELD, VALUE) \
  SET_CONTROL_VALUE(VALUE, m_s##FIELD, SetValue, int, value)

#define SET_SPIN(FIELD) SET_SPIN_VALUE(FIELD, (*it).FIELD)

#define SET_SPIN_DOUBLE_VALUE(FIELD, VALUE) \
  SET_CONTROL_VALUE(VALUE, m_s##FIELD, SetValue, double, value)

#define SET_SPIN_DOUBLE(FIELD) SET_SPIN_DOUBLE_VALUE(FIELD, (*it).FIELD)

#define NO_EDITED_CONTROLS 0

void ConfigurationDialog::SetConfigurations(
    std::list<RouteMapConfiguration> configurations) {
  m_bBlockUpdate = true;

  m_edited_controls.clear();

  if (configurations.empty()) {
    m_bBlockUpdate = false;
    return;
  }

  std::list<RouteMapConfiguration>::iterator it = configurations.begin();

  const bool routeByArrival =
      it->TimeMode == RouteMapConfiguration::ROUTE_BY_ARRIVAL_TIME;
  wxDateTime displayedTime =
      routeByArrival ? it->PlannedArrivalTime : it->StartTime;
  if (!routeByArrival && it->UseCurrentTime)
    displayedTime = wxDateTime::Now();
  if (!displayedTime.IsValid()) displayedTime = it->StartTime;
  const wxDateTime wall =
      m_WeatherRouting.m_SettingsDialog.ToDisplayWallClock(displayedTime);
#ifdef __OCPN__ANDROID__
  wxDateTime timeValue = TabletWallPicker(wall);
#else
  wxDateTime timeValue(
      wall.GetDay(wxDateTime::UTC), wall.GetMonth(wxDateTime::UTC),
      wall.GetYear(wxDateTime::UTC), wall.GetHour(wxDateTime::UTC),
      wall.GetMinute(wxDateTime::UTC), wall.GetSecond(wxDateTime::UTC));
#endif
  wxDateTime dateValue = timeValue.GetDateOnly();
  SET_CONTROL_VALUE(dateValue, m_dpStartDate, SetValue, wxDateTime,
                    wxDateTime());
  SET_CONTROL_VALUE(timeValue, m_tpTime, SetValue, wxDateTime, wxDateTime());
#ifdef __OCPN__ANDROID__
  SetTabletPickerText(m_dpStartDate, dateValue, true);
  SetTabletPickerText(m_tpTime, timeValue, false);
#endif

  m_rbRouteByDepartureTime->SetValue(!routeByArrival);
  m_rbRouteByArrivalTime->SetValue(routeByArrival);
  m_cbUseLocalTimeZone->SetValue(
      m_WeatherRouting.m_SettingsDialog.UseLocalTimeZone());
  m_cTimeZone->SetStringSelection(
      m_WeatherRouting.m_SettingsDialog.DisplayTimeZone());
  m_cTimeZone->Enable(m_cbUseLocalTimeZone->GetValue());
  SET_CHECKBOX(UseCurrentTime);
  SET_CHECKBOX(DepartureTimeOptimizationEnabled);
  if (routeByArrival) {
    m_cbUseCurrentTime->SetValue(false);
    m_cbDepartureTimeOptimizationEnabled->SetValue(true);
  }
  SET_SPIN_VALUE(DepartureTimeOptimizationRangeHours,
                 routeByArrival
                     ? (*it).ArrivalSearchHorizonMinutes / 60
                     : (*it).DepartureTimeOptimizationRangeMinutes / 60);
  SET_SPIN_VALUE(DepartureTimeOptimizationStepHours,
                 (*it).DepartureTimeOptimizationStepMinutes / 60);
  SET_SPIN_VALUE(DepartureTimeOptimizationStepMinutes,
                 (*it).DepartureTimeOptimizationStepMinutes % 60);
  bool sameShoreline = true;
  for (const auto& config : configurations)
    sameShoreline = sameShoreline && config.SelectedShorelineResolution() == it->SelectedShorelineResolution();
  const int shorelineValue = sameShoreline ? it->SelectedShorelineResolution() : wxNOT_FOUND;
  const auto shorelineChoice = std::find(m_shorelineChoiceResolutions.begin(),
                                        m_shorelineChoiceResolutions.end(), shorelineValue);
  m_cShorelineResolution->SetSelection(shorelineChoice == m_shorelineChoiceResolutions.end()
      ? wxNOT_FOUND : static_cast<int>(shorelineChoice - m_shorelineChoiceResolutions.begin()));
  const auto firstEngine = it->EngineSettings.engine;
  bool sameEngine = true;
  for (const auto& config : configurations)
    sameEngine = sameEngine && config.EngineSettings.engine == firstEngine;
  m_cRoutingEngine->SetSelection(!sameEngine ? wxNOT_FOUND :
      weather_routing::EngineSelection(firstEngine));
  SET_SPIN_VALUE(QuickMemoryBudgetMiB, (*it).EngineSettings.FastSettings().memoryBudgetMiB);
  SET_SPIN(MainGribTimelineCacheMiB);
  SET_SPIN_VALUE(QuickGribTimelineCacheMiB, (*it).IsOriginal() ? (*it).EngineSettings.originalGribTimelineCacheMiB : (*it).QuickGribTimelineCacheMiB);
  SET_SPIN_VALUE(QuickOffshoreStepMinutes, (*it).EngineSettings.FastSettings().offshoreStepMinutes);
  SET_SPIN_DOUBLE_VALUE(QuickHeadingStepDegrees, (*it).EngineSettings.FastSettings().headingStepDegrees);
  SET_SPIN_VALUE(QuickMaximumSearchAngle, (*it).EngineSettings.FastSettings().maximumSearchAngle);
  SET_SPIN(ArrivalSafetyMarginMinutes);
  SET_SPIN(DepartureTimeOptimizationConcurrentRoutes);
  const int firstRoutingEffort =
      weather_routing::NormalizeRoutingEffortPercent(it->RoutingEffortPercent);
  bool allRoutingEffortSame = true;
  for (auto compare = std::next(it); compare != configurations.end();
       ++compare) {
    if (weather_routing::NormalizeRoutingEffortPercent(
            compare->RoutingEffortPercent) != firstRoutingEffort) {
      allRoutingEffortSame = false;
      break;
    }
  }
  m_cRoutingEffortPercent->SetSelection(
      allRoutingEffortSame ? RoutingEffortSelection(firstRoutingEffort)
                           : wxNOT_FOUND);
  m_cRoutingEffortPercent->SetForegroundColour(
      allRoutingEffortSame ? wxColour(0, 0, 0) : wxColour(180, 180, 180));
  m_sChartSafetyRamCacheMiB->SetValue(
      m_WeatherRouting.ChartSafetyRamCacheMiB());
  UpdateChartSafetyRamLabel();

  SET_SPIN_VALUE(TimeStepHours, (int)((*it).DeltaTime / 3600));
  SET_SPIN_VALUE(TimeStepMinutes, ((int)(*it).DeltaTime / 60) % 60);


  SET_CONTROL(boatFileName, m_tBoat, SetValue, wxString, wxString());
#ifdef __OCPN__ANDROID__
  UpdateAndroidBoatName();
#endif
  long l = m_tBoat->GetValue().Length();
  m_tBoat->SetSelection(l, l);

  // if there's a Route GUID it's an OpenCPN route, in that case disable start
  // and end.
  bool oRoute = false;
  bool allStartFromBoat = true;
  bool allStartFromPosition = true;
  bool allStartFromWaypoint = true;
  bool allEndAtPosition = true;
  bool allEndAtWaypoint = true;
  for (auto it : configurations) {
    if (!it.RouteGUID.IsEmpty()) {
      oRoute = true;
      break;
    }
    if (it.StartType != RouteMapConfiguration::START_FROM_BOAT)
      allStartFromBoat = false;
    if (it.StartType != RouteMapConfiguration::START_FROM_POSITION)
      allStartFromPosition = false;
    if (it.StartType != RouteMapConfiguration::START_FROM_WAYPOINT)
      allStartFromWaypoint = false;
    if (it.EndType != RouteMapConfiguration::END_AT_POSITION)
      allEndAtPosition = false;
    if (it.EndType != RouteMapConfiguration::END_AT_WAYPOINT)
      allEndAtWaypoint = false;
  }

  if (allStartFromWaypoint)
    AddWaypoints(true);
  else
    AddPositions(true);
  if (allEndAtWaypoint)
    AddWaypoints(false);
  else
    AddPositions(false);

  wxString start = (*it).Start;
  if (allStartFromWaypoint && !(*it).StartGUID.IsEmpty()) {
    wxString waypoint_name = WaypointNameForGuid((*it).StartGUID);
    if (!waypoint_name.IsEmpty()) start = waypoint_name;
  }
  SET_CHOICE_VALUE(Start, start);
  if (allStartFromWaypoint) SelectWaypointByGuid(m_cStart, (*it).StartGUID);

  wxString end = (*it).End;
  if (allEndAtWaypoint && !(*it).EndGUID.IsEmpty()) {
    wxString waypoint_name = WaypointNameForGuid((*it).EndGUID);
    if (!waypoint_name.IsEmpty()) end = waypoint_name;
  }
  SET_CHOICE_VALUE(End, end);
  if (allEndAtWaypoint) SelectWaypointByGuid(m_cEnd, (*it).EndGUID);

  const bool sharedPassage = configurations.size() > 1 &&
      !it->MultiLegGroupId.IsEmpty() &&
      std::all_of(configurations.begin(), configurations.end(), [&](const auto& config) {
        return config.IsMultiLegGenerated && config.MultiLegGroupId == it->MultiLegGroupId;
      });
  m_endpointLocked = oRoute || sharedPassage;
#ifdef __OCPN__ANDROID__
  if (auto* title = wxDynamicCast(FindWindowByName("wr-android-title", this), wxStaticText))
    title->SetLabel(sharedPassage
        ? wxString::Format(_("Passage setup (%lu legs)"),
                           static_cast<unsigned long>(configurations.size()))
        : _("Route setup"));
#endif
  m_rbStartFromBoat->Enable(!m_endpointLocked);
  m_rbStartPositionSelection->Enable(!m_endpointLocked);
  m_rbStartWaypointSelection->Enable(!m_endpointLocked);
  m_rbEndPositionSelection->Enable(!m_endpointLocked);
  m_rbEndWaypointSelection->Enable(!m_endpointLocked);
  m_rbStartFromBoat->SetValue(allStartFromBoat);
  m_rbStartPositionSelection->SetValue(allStartFromPosition);
  m_rbStartWaypointSelection->SetValue(allStartFromWaypoint);
  m_rbEndPositionSelection->SetValue(allEndAtPosition);
  m_rbEndWaypointSelection->SetValue(allEndAtWaypoint);

  m_cStart->Enable(!m_endpointLocked && !m_rbStartFromBoat->GetValue());
  m_cEnd->Enable(!m_endpointLocked);

  UpdateRoutingTimeModeControls();

  SET_SPIN(FromDegree);
  SET_SPIN(ToDegree);
  SET_CHECKBOX(UseOptimalAngles);
  SET_SPIN_DOUBLE(ByDegrees);


  SET_CHECKBOX(UseMotor);
  SET_SPIN_DOUBLE(MotorSpeedThreshold);
  SET_SPIN_DOUBLE(MotorSpeed);
  const bool motorEnabled = m_cbUseMotor->IsChecked();
  m_sMotorSpeedThreshold->Enable(motorEnabled);
  m_sMotorSpeed->Enable(motorEnabled);

  SET_CHOICE_VALUE(Integrator,
                   ((*it).Integrator == RouteMapConfiguration::RUNGE_KUTTA
                        ? _T("Runge Kutta")
                        : _T("Newton")));

  SET_SPIN(MaxDivertedCourse);
  SET_SPIN(MaxCourseAngle);
  SET_SPIN(MaxSearchAngle);
  SET_SPIN(MaxTrueWindKnots);
  SET_SPIN(MaxApparentWindKnots);

  SET_SPIN_DOUBLE(MaxSwellMeters);
  SET_SPIN(MaxLatitude);
  SET_SPIN(TackingTime);
  SET_SPIN(JibingTime);
  SET_SPIN(SailPlanChangeTime);
  SET_SPIN(WindVSCurrent);

  SET_CHECKBOX(AvoidCycloneTracks);
  SET_SPIN(CycloneMonths);
  SET_SPIN(CycloneDays);
  SET_SPIN_DOUBLE(SafetyMarginLand);
  SET_SPIN_DOUBLE(MinimumDepthMeters);

  SET_CHECKBOX(DetectLand);
  SET_CHECKBOX(DetectBoundary);
  SET_CHECKBOX(Currents);
  SET_CHECKBOX(OptimizeTacking);

  SET_CHECKBOX(InvertedRegions);
  SET_CHECKBOX(UseReverseReachabilityRecovery);
  SET_CHECKBOX(Anchoring);

  SET_CHECKBOX(UseGrib);
  SET_CONTROL(ClimatologyType, m_cClimatologyType, SetSelection, int, -1);
  SET_CHECKBOX(AllowDataDeficient);
  SET_SPIN_VALUE(WindStrength, (int)((*it).WindStrength * 100));

  SET_SPIN_VALUE(UpwindEfficiency, (int)((*it).UpwindEfficiency * 100));
  SET_SPIN_VALUE(DownwindEfficiency, (int)((*it).DownwindEfficiency * 100));
  SET_SPIN_VALUE(NightCumulativeEfficiency,
                 (int)((*it).NightCumulativeEfficiency * 100));

  UpdateEngineControls();
#ifdef __OCPN__ANDROID__
  m_cEnginePreset->SetMinSize(wxSize(220, 72));
#endif
  RefreshEnginePresetStatus();
  m_bBlockUpdate = false;
}

void ConfigurationDialog::AddSource(wxString name) {
  if (m_rbStartPositionSelection->GetValue()) m_cStart->Append(name);
  if (m_rbEndPositionSelection->GetValue()) m_cEnd->Append(name);
}

void ConfigurationDialog::RemoveSource(wxString name) {
  int i = m_cStart->FindString(name, true);
  if (i >= 0) m_cStart->Delete(i);
  i = m_cEnd->FindString(name, true);
  if (i >= 0) m_cEnd->Delete(i);
}

void ConfigurationDialog::RenameSource(const wxString& oldName,
                                       const wxString& newName) {
  int i = m_cStart->FindString(oldName, true);
  if (i >= 0) m_cStart->SetString(i, newName);
  i = m_cEnd->FindString(oldName, true);
  if (i >= 0) m_cEnd->SetString(i, newName);
}

void ConfigurationDialog::ClearSources() {
  m_cStart->Clear();
  m_cEnd->Clear();
}

void ConfigurationDialog::AddWaypoints(const bool toStart) {
  wxComboBox* combo = toStart ? m_cStart : m_cEnd;
  wxString value = combo->GetValue();
  combo->Clear();

  wxArrayString waypoint_guids = GetWaypointGUIDArray();
  for (const auto& guid : waypoint_guids) {
    PlugIn_Waypoint waypoint;
    if (GetSingleWaypoint(guid, &waypoint)) {
      wxString label = waypoint.m_MarkName;
#ifdef __OCPN__ANDROID__
      label = wxString::Format("%s  (%+.4f, %+.4f)",
          label.IsEmpty() ? _("Unnamed waypoint") : label,
          waypoint.m_lat, waypoint.m_lon);
#endif
      combo->Append(label, new wxStringClientData(guid));
    }
  }

#ifdef __OCPN__ANDROID__
  if (combo->GetCount()) {
    const int selected = combo->FindString(value, true);
    combo->SetSelection(selected >= 0 ? selected : 0);
  }
#else
  if (!value.IsEmpty()) combo->SetValue(value);
#endif
}

void ConfigurationDialog::AddPositions(const bool toStart) {
  wxComboBox* combo = toStart ? m_cStart : m_cEnd;
  wxString value = combo->GetValue();
  combo->Clear();

  for (const auto& position : RouteMap::Positions)
    combo->Append(position.Name);

#ifdef __OCPN__ANDROID__
  if (combo->GetCount()) {
    const int selected = combo->FindString(value, true);
    combo->SetSelection(selected >= 0 ? selected : 0);
  }
#else
  if (!value.IsEmpty()) combo->SetValue(value);
#endif
}

void ConfigurationDialog::SetBoatFilename(wxString path) {
  m_tBoat->SetValue(path);
#ifdef __OCPN__ANDROID__
  UpdateAndroidBoatName();
#endif
  long l = m_tBoat->GetValue().Length();
  m_tBoat->SetSelection(l, l);

  Update();
}

bool ConfigurationDialog::HandleEngineEdit(wxObject* control) {
  const bool mainField = control == m_sTimeStepHours || control == m_sTimeStepMinutes ||
      control == m_sByDegrees || control == m_cRoutingEffortPercent ||
      control == m_sMaxSearchAngle || control == m_cbUseReverseReachabilityRecovery;
  const bool quickField = control == m_sQuickOffshoreStepMinutes ||
      control == m_sQuickHeadingStepDegrees || control == m_sQuickMaximumSearchAngle;
  if (!mainField && !quickField && control != m_cRoutingEngine &&
      control != m_sQuickMemoryBudgetMiB &&
      control != m_sMainGribTimelineCacheMiB &&
      control != m_sQuickGribTimelineCacheMiB &&
      control != m_cShorelineResolution) return false;
  if (m_bBlockUpdate) return true;
  // Engine edits must not round-trip unrelated controls: some old controls
  // display rounded percentages or mixed values. Keep their exact saved data.
  for (auto* route : m_WeatherRouting.CurrentRouteMaps(false)) {
    if (route->Running()) continue;
    auto config = route->GetConfiguration();
    if (control == m_cShorelineResolution) {
      const int selection = m_cShorelineResolution->GetSelection();
      if (selection < 0 || selection >=
              static_cast<int>(m_shorelineChoiceResolutions.size())) continue;
      const int resolution = m_shorelineChoiceResolutions[selection];
      if (!weather_routing::ShorelineManager::Available(resolution)) continue;
      const bool chartMode = m_WeatherRouting.HasEnhancedChartSafety() &&
          m_cbUseExperimentalChartSafety->GetValue() && m_cbEnforceExperimentalChartSafety->GetValue();
      if (chartMode) config.ChartShorelineResolution = resolution;
      else if (config.IsFastEngine()) config.FastShorelineResolution() = resolution;
      else config.ShorelineResolution = resolution;
      config.shoreline_dataset.reset();
      config.shoreline_description.clear();
      config.shoreline_error.clear();
    }
    if (control == m_cRoutingEngine) {
      if (m_cRoutingEngine->GetSelection() == wxNOT_FOUND) continue;
      config.EngineSettings.engine = weather_routing::EngineFromSelection(m_cRoutingEngine->GetSelection());
      config.EngineSettings.unsupportedId.clear();
    }
    if (control == m_sTimeStepHours || control == m_sTimeStepMinutes) {
      // Preserve the other component for mixed selections unless it was edited.
      const int hours = control == m_sTimeStepHours ? m_sTimeStepHours->GetValue()
          : static_cast<int>(config.DeltaTime) / 3600;
      const int minutes = control == m_sTimeStepMinutes ? m_sTimeStepMinutes->GetValue()
          : (static_cast<int>(config.DeltaTime) / 60) % 60;
      config.DeltaTime = 3600 * hours + 60 * minutes;
    }
    if (control == m_sByDegrees) config.ByDegrees = m_sByDegrees->GetValue();
    if (control == m_cRoutingEffortPercent &&
        config.EngineSettings.engine != weather_routing::RoutingEngine::Auto)
      config.RoutingEffortPercent = RoutingEffortPercentForSelection(m_cRoutingEffortPercent->GetSelection());
    if (control == m_sMaxSearchAngle) config.MaxSearchAngle = m_sMaxSearchAngle->GetValue();
    if (control == m_cbUseReverseReachabilityRecovery)
      config.UseReverseReachabilityRecovery = m_cbUseReverseReachabilityRecovery->IsChecked();
    if (control == m_sQuickMemoryBudgetMiB)
      config.EngineSettings.FastSettings().memoryBudgetMiB = m_sQuickMemoryBudgetMiB->GetValue();
    if (control == m_sMainGribTimelineCacheMiB)
      config.MainGribTimelineCacheMiB =
          weather_routing::NormalizeGribTimelineCacheMiB(
              m_sMainGribTimelineCacheMiB->GetValue(), false);
    if (control == m_sQuickGribTimelineCacheMiB)
      config.FastGribTimelineCacheMiB() =
          weather_routing::NormalizeGribTimelineCacheMiB(
              m_sQuickGribTimelineCacheMiB->GetValue(), true);
    if (control == m_sQuickOffshoreStepMinutes)
      config.EngineSettings.FastSettings().offshoreStepMinutes = m_sQuickOffshoreStepMinutes->GetValue();
    if (control == m_sQuickHeadingStepDegrees)
      config.EngineSettings.FastSettings().headingStepDegrees = m_sQuickHeadingStepDegrees->GetValue();
    if (control == m_sQuickMaximumSearchAngle)
      config.EngineSettings.FastSettings().maximumSearchAngle = m_sQuickMaximumSearchAngle->GetValue();
    if (mainField) config.EngineSettings.mainPreset = {};
    if (quickField) config.EngineSettings.FastSettings().preset = {};
    route->SetConfiguration(config);
    m_WeatherRouting.SaveLastUsedConfigurationDefaults(config);
  }
  if (control == m_cRoutingEngine) {
    std::list<RouteMapConfiguration> selected;
    for (auto* route : m_WeatherRouting.CurrentRouteMaps(false))
      selected.push_back(route->GetConfiguration());
    SetConfigurations(selected);
  } else {
    UpdateEngineControls();
    RefreshEnginePresetStatus();
  }
  m_WeatherRouting.UpdateCurrentConfigurations();
  m_WeatherRouting.ScheduleAutoSave();
  return true;
}

void ConfigurationDialog::UpdateEngineControls() {
  const auto engine = weather_routing::EngineFromSelection(m_cRoutingEngine->GetSelection());
  const bool combined = weather_routing::IsCombinedEngine(engine);
  m_pMainEngine->Show(engine == weather_routing::RoutingEngine::Main || combined);
  m_pQuickEngine->Show(engine == weather_routing::RoutingEngine::Original || engine == weather_routing::RoutingEngine::Quick);
  m_bResetAdvanced->Enable(engine != weather_routing::RoutingEngine::Unsupported);
  m_cEnginePreset->Enable(engine != weather_routing::RoutingEngine::Unsupported);
  bool running = false;
  for (auto* route : m_WeatherRouting.CurrentRouteMaps(false)) running |= route->Running();
  m_cRoutingEngine->Enable(!running);
  const bool chartAuthoritative = m_WeatherRouting.HasEnhancedChartSafety() &&
      m_cbUseExperimentalChartSafety->GetValue() && m_cbEnforceExperimentalChartSafety->GetValue();
  int shoreline = wxNOT_FOUND;
  bool first = true;
  for (auto* route : m_WeatherRouting.CurrentRouteMaps(false)) {
    const auto config = route->GetConfiguration();
    const int value = chartAuthoritative ? config.ChartShorelineResolution : config.SelectedShorelineResolution();
    if (first) shoreline = value;
    else if (shoreline != value) { shoreline = wxNOT_FOUND; break; }
    first = false;
  }
  m_tShorelineResolution->SetLabel(chartAuthoritative ? _("Scout shoreline resolution") : _("Shoreline resolution"));
  m_cShorelineResolution->Clear();
  m_shorelineChoiceResolutions.clear();
  int selectedIndex = wxNOT_FOUND;
  for (int q = 0; q < 5; ++q) {
    const bool available = weather_routing::ShorelineManager::Available(q);
    if (!available && q != shoreline) continue;
#ifdef __OCPN__ANDROID__
    wxString label = wxString::Format("%d / %s", q,
        wxGetTranslation(weather_routing::kShorelineSpecs[q].quality));
#else
    wxString label = wxString::Format("%d - %s", q,
        wxGetTranslation(weather_routing::kShorelineSpecs[q].quality));
#endif
    if (!available) label += _(" (missing; install)");
    if (q == shoreline) selectedIndex = static_cast<int>(m_shorelineChoiceResolutions.size());
    m_shorelineChoiceResolutions.push_back(q);
    m_cShorelineResolution->Append(label);
  }
  m_cShorelineResolution->SetSelection(selectedIndex);
  m_cShorelineResolution->Enable(!running);
  m_cShorelineResolution->SetToolTip(chartAuthoritative
      ? _("Chart geometry and depth checks are authoritative. This separately saved shoreline choice controls preliminary scouting. It starts at Crude; increase it for finer coastal detail.")
      : _("Shoreline detail for the selected engine. Quick, Standard and Professional remember independent choices. High and Full require installation from Shoreline data on Advanced. Lower resolutions omit smaller coastal features."));
  m_pMainEngine->Enable(!running);
  m_cRoutingEffortPercent->Enable(!running && engine != weather_routing::RoutingEngine::Auto);
  if (engine == weather_routing::RoutingEngine::Auto)
    m_cRoutingEffortPercent->SetSelection(RoutingEffortSelection(400));
  m_pQuickEngine->Enable(!running);
  if (running) m_bResetAdvanced->Enable(false);
  m_tRoutingEngineDescription->SetLabel(engine == weather_routing::RoutingEngine::Auto
      ? _("Auto: Quick, then Standard, then Professional up to 400% effort; stops at the first validated route.")
      : engine == weather_routing::RoutingEngine::All
      ? _("All (slow): compares Quick, Standard, Alternative and Professional; returns the earliest validated arrival.")
      : engine == weather_routing::RoutingEngine::Main
      ? _("Professional: broader search with multiple recovery methods.")
      : engine == weather_routing::RoutingEngine::Quick ? _("Standard: bounded adaptive search with recovery.")
      : engine == weather_routing::RoutingEngine::Original ? _("Quick: fast contour search with independently validated arrival; may miss a feasible route.")
      : _("Mixed or unsupported engines. Select Auto, Quick, Standard, Professional or All (slow)."));
  // These legacy controls do not tune either native engine. Preserve their
  // saved values for compatibility without suggesting that they affect search.
  bool allNative = true;
  for (auto* route : m_WeatherRouting.CurrentRouteMaps(false))
    allNative = allNative && ModernNativeRouteEnabled(route->GetConfiguration());
  m_cbInvertedRegions->Enable(!allNative);
  // Both native engines pass this option to the shared polar evaluator.
  m_cbOptimizeTacking->Enable(!running && engine != weather_routing::RoutingEngine::Original);
  m_cIntegrator->Enable(!allNative);
  // Departure concurrency is a scheduler setting shared by all three engines.
  m_sDepartureTimeOptimizationConcurrentRoutes->Enable(!running);
  m_pAdvanced->Layout();
  m_pAdvanced->FitInside();
  m_pBasic->SendSizeEvent();
  m_pBasic->Layout();
}

void ConfigurationDialog::RefreshEnginePresetStatus() {
  wxString status;
  const auto engine = weather_routing::EngineFromSelection(m_cRoutingEngine->GetSelection());
  const bool combined = weather_routing::IsCombinedEngine(engine);
  for (auto* route : m_WeatherRouting.CurrentRouteMaps(false)) {
    const auto config = route->GetConfiguration();
    const auto& preset = (engine == weather_routing::RoutingEngine::Original || engine == weather_routing::RoutingEngine::Quick) ? config.EngineSettings.FastSettings().preset
                                    : config.EngineSettings.mainPreset;
    wxString label = preset.id == "balanced"
        ? wxString::Format(_("Balanced (revision %d)"), preset.revision)
        : _("Custom");
    if (combined && (config.EngineSettings.original.preset != preset ||
                     config.EngineSettings.quick.preset != preset)) label = _("Custom");
    if (status.empty()) status = label;
    else if (status != label) { status = _("Mixed"); break; }
  }
  if (engine == weather_routing::RoutingEngine::Unsupported) status = _("Mixed or unsupported");
#ifdef __OCPN__ANDROID__
  WR_WrapAndroidText(m_tEnginePresetStatus, _("Current settings: ") + status,
      wxMax(300, m_pAdvanced->GetClientSize().x - 100));
  m_tEnginePresetStatus->GetContainingSizer()->GetItem(m_tEnginePresetStatus)->SetFlag(wxEXPAND | wxALL);
  m_pAdvanced->Layout();
#else
  m_tEnginePresetStatus->SetLabel(_("Current settings: ") + status);
#endif
}

void ConfigurationDialog::OnResetAdvanced(wxCommandEvent&) {
  const auto engine = weather_routing::EngineFromSelection(m_cRoutingEngine->GetSelection());
  const bool combined = weather_routing::IsCombinedEngine(engine);
  if (m_bBlockUpdate || engine == weather_routing::RoutingEngine::Unsupported || m_cEnginePreset->GetSelection() != 0)
    return;
  const auto routes = m_WeatherRouting.CurrentRouteMaps(false);
  if (routes.empty()) return;
  for (auto* route : routes) if (route->Running()) return;
  // The configuration editor applies changes immediately. Previewing the
  // explicit reset gives it an Apply/Cancel boundary without changing that
  // established workflow or modifying routes before the user accepts.
  const wxString values = combined
      ? _("Auto / All - Balanced\nReset Quick and Standard to 3-hour adaptive steps and 10-degree headings. Reset Professional to 1-hour steps, 10-degree headings and 100% effort. Auto always allows Professional up to 400% effort.")
      : engine == weather_routing::RoutingEngine::Main
      ? _("Professional - Balanced\nTime step: 1 hour\nHeading separation: 10 degrees\nRouting effort: 100%\nMaximum search angle: 120 degrees\nOptional reverse reachability recovery: off")
      : _("Quick / Standard - Balanced\nOffshore time step: 3 hours (adaptive)\nHeading separation: 10 degrees (adaptive)\nMaximum search angle: 120 degrees");
  WR_MessageDialog preview(this, values +
      _("\n\nApplies to all selected routes. Memory and GRIB cache budgets, vessel, weather and safety settings are preserved."),
      _("Reset engine to preset"), wxOK | wxCANCEL);
  preview.SetOKLabel(_("Apply preset"));
  if (preview.ShowModal() != wxID_OK) return;
  std::list<RouteMapConfiguration> configurations;
  for (auto* route : routes) {
    auto config = route->GetConfiguration();
    if (combined) {
      weather_routing::ResetMainToBalanced(config);
      config.EngineSettings.ResetOriginalToBalanced();
      config.EngineSettings.ResetQuickToBalanced();
    }
    else if (engine == weather_routing::RoutingEngine::Main) weather_routing::ResetMainToBalanced(config);
    else if (engine == weather_routing::RoutingEngine::Original) config.EngineSettings.ResetOriginalToBalanced();
    else config.EngineSettings.ResetQuickToBalanced();
    route->SetConfiguration(config);
    m_WeatherRouting.SaveLastUsedConfigurationDefaults(config);
    configurations.push_back(config);
  }
  SetConfigurations(configurations);
  m_WeatherRouting.UpdateCurrentConfigurations();
  m_WeatherRouting.ScheduleAutoSave();
}

void ConfigurationDialog::UpdateChartSafetyRamLabel() {
  const int effective = m_WeatherRouting.EffectiveChartSafetyRamCacheMiB();
  m_tChartSafetyRamEffective->SetLabel(
      m_sChartSafetyRamCacheMiB->GetValue() == 0
          ? wxString::Format(_("MiB; Auto = %d"), effective)
          : wxString::Format(_("MiB; active = %d"), effective));
}

void ConfigurationDialog::SetStartDateTime(wxDateTime datetime) {
  if (datetime.IsValid()) {
    const wxDateTime wall =
        m_WeatherRouting.m_SettingsDialog.ToDisplayWallClock(datetime);
#ifdef __OCPN__ANDROID__
    wxDateTime pickerValue = TabletWallPicker(wall);
#else
    wxDateTime pickerValue(
        wall.GetDay(wxDateTime::UTC), wall.GetMonth(wxDateTime::UTC),
        wall.GetYear(wxDateTime::UTC), wall.GetHour(wxDateTime::UTC),
        wall.GetMinute(wxDateTime::UTC), wall.GetSecond(wxDateTime::UTC));
#endif
    m_dpStartDate->SetValue(pickerValue);
    m_tpTime->SetValue(pickerValue);
#ifdef __OCPN__ANDROID__
    SetTabletPickerText(m_dpStartDate, pickerValue, true);
    SetTabletPickerText(m_tpTime, pickerValue, false);
#endif
    m_edited_controls.push_back(m_tpTime);
    m_edited_controls.push_back(m_dpStartDate);
  } else {
    WR_MessageDialog mdlg(this, _("Invalid Date Time."),
                         wxString(_("Weather Routing"), wxOK | wxICON_WARNING));
    mdlg.ShowModal();
  }
}

#define GET_CHECKBOX(FIELD)                                  \
  do {                                                       \
    if (m_cb##FIELD->Get3StateValue() == wxCHK_UNCHECKED)    \
      configuration.FIELD = false;                           \
    else if (m_cb##FIELD->Get3StateValue() == wxCHK_CHECKED) \
      configuration.FIELD = true;                            \
  } while (0)

#define GET_SPIN(FIELD)                                              \
  if (NO_EDITED_CONTROLS ||                                          \
      std::find(m_edited_controls.begin(), m_edited_controls.end(),  \
                (wxObject*)m_s##FIELD) != m_edited_controls.end()) { \
    configuration.FIELD = m_s##FIELD->GetValue();                    \
    m_s##FIELD->SetForegroundColour(wxColour(0, 0, 0));              \
  }

#define GET_CHOICE(FIELD)                                                     \
  if (NO_EDITED_CONTROLS ||                                                   \
      std::find(m_edited_controls.begin(), m_edited_controls.end(),           \
                (wxObject*)m_c##FIELD) != m_edited_controls.end())            \
    if (m_c##FIELD->GetValue() != wxEmptyString) {                            \
      configuration.FIELD = m_c##FIELD->GetValue();                           \
      if (m_c##FIELD->GetString(m_c##FIELD->GetCount() - 1) == wxEmptyString) \
        m_c##FIELD->Delete(m_c##FIELD->GetCount() - 1);                       \
    }

void ConfigurationDialog::OnChartSafetyChanged(wxCommandEvent&) {
  if (m_bBlockUpdate || !m_WeatherRouting.HasEnhancedChartSafety()) return;
  m_WeatherRouting.ApplyChartSafetySettings(
      m_cbUseExperimentalChartSafety->GetValue(),
      m_cbEnforceExperimentalChartSafety->GetValue());
  UpdateEngineControls();
}

void ConfigurationDialog::Update() {
  if (m_bBlockUpdate) return;

  if (std::find(m_edited_controls.begin(), m_edited_controls.end(),
                (wxObject*)m_sChartSafetyRamCacheMiB) !=
      m_edited_controls.end()) {
    m_WeatherRouting.SetChartSafetyRamCacheMiB(
        m_sChartSafetyRamCacheMiB->GetValue());
    m_sChartSafetyRamCacheMiB->SetForegroundColour(wxColour(0, 0, 0));
    UpdateChartSafetyRamLabel();
  }

  m_cStart->Enable(!m_endpointLocked && !m_rbStartFromBoat->GetValue());
  m_cEnd->Enable(!m_endpointLocked);

  bool refresh = false;
  RouteMapConfiguration configuration;
  std::list<RouteMapOverlay*> currentroutemaps =
      m_WeatherRouting.CurrentRouteMaps();
  for (std::list<RouteMapOverlay*>::iterator it = currentroutemaps.begin();
       it != currentroutemaps.end(); it++) {
    configuration = (*it)->GetConfiguration();

#ifdef __OCPN__ANDROID__
    const auto endpointEdited = [&](wxObject* control) {
      return std::find(m_edited_controls.begin(), m_edited_controls.end(), control)
          != m_edited_controls.end();
    };
    const bool startTypeEdited = endpointEdited(m_rbStartFromBoat) ||
        endpointEdited(m_rbStartPositionSelection) || endpointEdited(m_rbStartWaypointSelection);
    const bool endTypeEdited = endpointEdited(m_rbEndPositionSelection) ||
        endpointEdited(m_rbEndWaypointSelection);
    const bool timeModeEdited = endpointEdited(m_rbRouteByDepartureTime) ||
        endpointEdited(m_rbRouteByArrivalTime);
#else
    const bool startTypeEdited = true, endTypeEdited = true, timeModeEdited = true;
#endif
    if (startTypeEdited) {
    if (m_rbStartFromBoat->GetValue()) {
      configuration.StartType = RouteMapConfiguration::START_FROM_BOAT;
      configuration.StartGUID = wxEmptyString;
    } else if (m_rbStartWaypointSelection->GetValue()) {
      configuration.StartType = RouteMapConfiguration::START_FROM_WAYPOINT;
      GET_CHOICE(Start);
      configuration.StartGUID = GetWaypointGuidForSelection(m_cStart);
    } else {
      configuration.StartType = RouteMapConfiguration::START_FROM_POSITION;
      GET_CHOICE(Start);
      configuration.StartGUID = wxEmptyString;
    }

    } else {
      GET_CHOICE(Start);
      if (configuration.StartType == RouteMapConfiguration::START_FROM_WAYPOINT &&
          m_cStart->GetValue() == configuration.Start)
        configuration.StartGUID = GetWaypointGuidForSelection(m_cStart);
    }

    if (endTypeEdited) {
    if (m_rbEndWaypointSelection->GetValue()) {
      configuration.EndType = RouteMapConfiguration::END_AT_WAYPOINT;
      GET_CHOICE(End);
      configuration.EndGUID = GetWaypointGuidForSelection(m_cEnd);
    } else {
      configuration.EndType = RouteMapConfiguration::END_AT_POSITION;
      GET_CHOICE(End);
      configuration.EndGUID = wxEmptyString;
    }

    } else {
      GET_CHOICE(End);
      if (configuration.EndType == RouteMapConfiguration::END_AT_WAYPOINT &&
          m_cEnd->GetValue() == configuration.End)
        configuration.EndGUID = GetWaypointGuidForSelection(m_cEnd);
    }

    // The Android picker adds coordinates for disambiguation. Keep only the
    // actual waypoint name in the model; its identity is the attached GUID.
    if (configuration.StartType == RouteMapConfiguration::START_FROM_WAYPOINT &&
        !configuration.StartGUID.IsEmpty())
      configuration.Start = WaypointNameForGuid(configuration.StartGUID);
    if (configuration.EndType == RouteMapConfiguration::END_AT_WAYPOINT &&
        !configuration.EndGUID.IsEmpty())
      configuration.End = WaypointNameForGuid(configuration.EndGUID);

    if (timeModeEdited) configuration.TimeMode =
        m_rbRouteByArrivalTime->GetValue() ? RouteMapConfiguration::ROUTE_BY_ARRIVAL_TIME
                       : RouteMapConfiguration::ROUTE_BY_DEPARTURE_TIME;
    const bool routeByArrival = configuration.TimeMode ==
        RouteMapConfiguration::ROUTE_BY_ARRIVAL_TIME;
    if (!routeByArrival) {
      GET_CHECKBOX(UseCurrentTime);
      GET_CHECKBOX(DepartureTimeOptimizationEnabled);
    }
    if (weather_routing::
            ShouldPromoteDepartureOptimizationCandidateForTimeMode(
                routeByArrival,
                configuration.DepartureTimeOptimizationEnabled,
                configuration.DepartureTimeOptimizationCandidate)) {
      wxLogMessage(
          "WR_DEPARTURE_PROMOTE source=configuration transition=%s "
          "old_group=\"%s\" old_offset_minutes=%d start=\"%s\" end=\"%s\".",
          routeByArrival ? wxString("arrival")
                         : wxString("departure-optimization"),
          configuration.DepartureTimeOptimizationGroupId,
          configuration.DepartureTimeOptimizationOffsetMinutes,
          configuration.Start, configuration.End);
      configuration.PromoteDepartureTimeOptimizationCandidate();
    }
    if (NO_EDITED_CONTROLS ||
        std::find(m_edited_controls.begin(), m_edited_controls.end(),
                  (wxObject*)m_sDepartureTimeOptimizationRangeHours) !=
            m_edited_controls.end()) {
      if (routeByArrival)
        configuration.ArrivalSearchHorizonMinutes =
            60 * m_sDepartureTimeOptimizationRangeHours->GetValue();
      else
        configuration.DepartureTimeOptimizationRangeMinutes =
            60 * m_sDepartureTimeOptimizationRangeHours->GetValue();
      m_sDepartureTimeOptimizationRangeHours->SetForegroundColour(
          wxColour(0, 0, 0));
    }
    if (NO_EDITED_CONTROLS ||
        std::find(m_edited_controls.begin(), m_edited_controls.end(),
                  (wxObject*)m_sDepartureTimeOptimizationStepHours) !=
            m_edited_controls.end() ||
        std::find(m_edited_controls.begin(), m_edited_controls.end(),
                  (wxObject*)m_sDepartureTimeOptimizationStepMinutes) !=
            m_edited_controls.end()) {
      configuration.DepartureTimeOptimizationStepMinutes =
          60 * m_sDepartureTimeOptimizationStepHours->GetValue() +
          m_sDepartureTimeOptimizationStepMinutes->GetValue();
      m_sDepartureTimeOptimizationStepHours->SetForegroundColour(
          wxColour(0, 0, 0));
      m_sDepartureTimeOptimizationStepMinutes->SetForegroundColour(
          wxColour(0, 0, 0));
    }

    if (NO_EDITED_CONTROLS ||
        std::find(m_edited_controls.begin(), m_edited_controls.end(),
                  (wxObject*)m_dpStartDate) != m_edited_controls.end() ||
        std::find(m_edited_controls.begin(), m_edited_controls.end(),
                  (wxObject*)m_tpTime) != m_edited_controls.end()) {
#ifdef __OCPN__ANDROID__
      const wxDateTime controlDate = TabletPickerValue(m_dpStartDate, true);
      const wxDateTime controlTime = TabletPickerValue(m_tpTime, false);
#else
      const wxDateTime controlDate = m_dpStartDate->GetDateCtrlValue();
      const wxDateTime controlTime = m_tpTime->GetTimeCtrlValue();
#endif
      if (!controlDate.IsValid() || !controlTime.IsValid()) {
        m_dpStartDate->SetForegroundColour(*wxRED);
        m_tpTime->SetForegroundColour(*wxRED);
      } else {
        const marine_time::WallClockConversion conversion =
            m_WeatherRouting.m_SettingsDialog.DisplayWallClockToUtc(
                controlDate.GetYear(),
                static_cast<int>(controlDate.GetMonth()) + 1,
                controlDate.GetDay(), controlTime.GetHour(),
                controlTime.GetMinute(), controlTime.GetSecond());
        if (!conversion.utc.IsValid()) {
        const wxString error =
            conversion.status == marine_time::WallClockStatus::Nonexistent
                ? _("This local time does not exist because the clocks move "
                    "forward. Select another time.")
                : _("This date and time is invalid for the selected time "
                    "zone.");
        m_dpStartDate->SetForegroundColour(*wxRED);
        m_tpTime->SetForegroundColour(*wxRED);
        m_tpTime->SetToolTip(error);
        } else {
          const wxDateTime time = conversion.utc;

          if (routeByArrival)
            configuration.PlannedArrivalTime = time;
          else
            configuration.StartTime = time;
          if (std::find(m_edited_controls.begin(), m_edited_controls.end(),
                        (wxObject*)m_dpStartDate) != m_edited_controls.end())
            m_dpStartDate->SetForegroundColour(wxColour(0, 0, 0));
          if (std::find(m_edited_controls.begin(), m_edited_controls.end(),
                        (wxObject*)m_tpTime) != m_edited_controls.end())
            m_tpTime->SetForegroundColour(wxColour(0, 0, 0));
          if (conversion.status == marine_time::WallClockStatus::Ambiguous) {
            m_tpTime->SetToolTip(
                _("This time occurs twice when the clocks move back. The earlier "
                  "occurrence is used."));
          } else {
            m_tpTime->SetToolTip(
                _("Select the starting time for weather routing"));
          }
        }
      }
    }

    if (!m_tBoat->GetValue().empty()) {
      configuration.boatFileName = m_tBoat->GetValue();
      m_tBoat->SetForegroundColour(wxColour(0, 0, 0));
    }

    if (NO_EDITED_CONTROLS ||
        std::find(m_edited_controls.begin(), m_edited_controls.end(),
                  (wxObject*)m_sTimeStepHours) != m_edited_controls.end() ||
        std::find(m_edited_controls.begin(), m_edited_controls.end(),
                  (wxObject*)m_sTimeStepMinutes) != m_edited_controls.end()) {
      configuration.DeltaTime = 60 * (60 * m_sTimeStepHours->GetValue() +
                                      m_sTimeStepMinutes->GetValue());
      m_sTimeStepHours->SetForegroundColour(wxColour(0, 0, 0));
      m_sTimeStepMinutes->SetForegroundColour(wxColour(0, 0, 0));
    }

    if (m_cIntegrator->GetValue() == _T("Newton"))
      configuration.Integrator = RouteMapConfiguration::NEWTON;
    else if (m_cIntegrator->GetValue() == _T("Runge Kutta"))
      configuration.Integrator = RouteMapConfiguration::RUNGE_KUTTA;

    GET_SPIN(MaxDivertedCourse);
    GET_SPIN(MaxCourseAngle);
    GET_SPIN(MaxSearchAngle);
    GET_SPIN(MaxTrueWindKnots);
    GET_SPIN(MaxApparentWindKnots);

    GET_SPIN(MaxSwellMeters);
    GET_SPIN(MaxLatitude);
    GET_SPIN(TackingTime);
    GET_SPIN(JibingTime);
    GET_SPIN(SailPlanChangeTime);
    GET_SPIN(WindVSCurrent);

    if (m_sWindStrength->IsEnabled())
      configuration.WindStrength = m_sWindStrength->GetValue() / 100.0;

    if (m_sUpwindEfficiency->IsEnabled())
      configuration.UpwindEfficiency = m_sUpwindEfficiency->GetValue() / 100.0;
    if (m_sDownwindEfficiency->IsEnabled())
      configuration.DownwindEfficiency =
          m_sDownwindEfficiency->GetValue() / 100.0;
    if (m_sNightCumulativeEfficiency->IsEnabled())
      configuration.NightCumulativeEfficiency =
          m_sNightCumulativeEfficiency->GetValue() / 100.0;

    GET_CHECKBOX(AvoidCycloneTracks);
    GET_SPIN(CycloneMonths);
    GET_SPIN(CycloneDays);
    GET_SPIN(SafetyMarginLand);
    GET_SPIN(MinimumDepthMeters);

    GET_CHECKBOX(DetectLand);
    GET_CHECKBOX(DetectBoundary);
    GET_CHECKBOX(Currents);
    GET_CHECKBOX(OptimizeTacking);
    const auto edited = [&](wxObject* control) {
      return std::find(m_edited_controls.begin(), m_edited_controls.end(), control)
          != m_edited_controls.end();
    };
    if (edited(m_cRoutingEngine) && m_cRoutingEngine->GetSelection() != wxNOT_FOUND) {
      configuration.EngineSettings.engine = weather_routing::EngineFromSelection(m_cRoutingEngine->GetSelection());
      configuration.EngineSettings.unsupportedId.clear();
    }
    if (edited(m_sQuickMemoryBudgetMiB))
      configuration.EngineSettings.FastSettings().memoryBudgetMiB = m_sQuickMemoryBudgetMiB->GetValue();
    if (edited(m_sMainGribTimelineCacheMiB))
      configuration.MainGribTimelineCacheMiB =
          weather_routing::NormalizeGribTimelineCacheMiB(
              m_sMainGribTimelineCacheMiB->GetValue(), false);
    if (edited(m_sQuickGribTimelineCacheMiB))
      configuration.FastGribTimelineCacheMiB() =
          weather_routing::NormalizeGribTimelineCacheMiB(
              m_sQuickGribTimelineCacheMiB->GetValue(), true);
    if (edited(m_sQuickOffshoreStepMinutes))
      configuration.EngineSettings.FastSettings().offshoreStepMinutes = m_sQuickOffshoreStepMinutes->GetValue();
    if (edited(m_sQuickHeadingStepDegrees))
      configuration.EngineSettings.FastSettings().headingStepDegrees = m_sQuickHeadingStepDegrees->GetValue();
    if (edited(m_sQuickMaximumSearchAngle))
      configuration.EngineSettings.FastSettings().maximumSearchAngle = m_sQuickMaximumSearchAngle->GetValue();
    if (edited(m_sQuickOffshoreStepMinutes) || edited(m_sQuickHeadingStepDegrees) ||
        edited(m_sQuickMaximumSearchAngle))
      configuration.EngineSettings.FastSettings().preset = {};
    if (edited(m_sTimeStepHours) || edited(m_sTimeStepMinutes) ||
        edited(m_sByDegrees) || edited(m_cRoutingEffortPercent) ||
        edited(m_sMaxSearchAngle) || edited(m_cbUseReverseReachabilityRecovery))
      configuration.EngineSettings.mainPreset = {};

    GET_CHECKBOX(InvertedRegions);
    GET_CHECKBOX(UseReverseReachabilityRecovery);
    GET_CHECKBOX(Anchoring);
    if (configuration.EngineSettings.engine != weather_routing::RoutingEngine::Auto &&
        (NO_EDITED_CONTROLS ||
         std::find(m_edited_controls.begin(), m_edited_controls.end(),
                   static_cast<wxObject*>(m_cRoutingEffortPercent)) !=
             m_edited_controls.end())) {
      configuration.RoutingEffortPercent = RoutingEffortPercentForSelection(
          m_cRoutingEffortPercent->GetSelection());
      m_cRoutingEffortPercent->SetForegroundColour(wxColour(0, 0, 0));
    }
    GET_SPIN(DepartureTimeOptimizationConcurrentRoutes);
    if (routeByArrival) {
      GET_SPIN(ArrivalSafetyMarginMinutes);
    }

    GET_CHECKBOX(UseGrib);
    if (m_cClimatologyType->GetSelection() != -1)
      configuration.ClimatologyType =
          (RouteMapConfiguration::ClimatologyDataType)
              m_cClimatologyType->GetSelection();
    GET_CHECKBOX(AllowDataDeficient);
    if (m_sWindStrength->IsEnabled())
      configuration.WindStrength = m_sWindStrength->GetValue() / 100.0;

    GET_SPIN(FromDegree);
    GET_SPIN(ToDegree);
    GET_CHECKBOX(UseOptimalAngles);
    GET_SPIN(ByDegrees);

    GET_CHECKBOX(UseMotor);
    if (NO_EDITED_CONTROLS ||
        std::find(m_edited_controls.begin(), m_edited_controls.end(),
                  (wxObject*)m_sMotorSpeedThreshold) !=
            m_edited_controls.end()) {
      configuration.MotorSpeedThreshold =
          m_sMotorSpeedThreshold->GetValue();
    }
    if (NO_EDITED_CONTROLS ||
        std::find(m_edited_controls.begin(), m_edited_controls.end(),
                  (wxObject*)m_sMotorSpeed) != m_edited_controls.end()) {
      configuration.MotorSpeed = m_sMotorSpeed->GetValue();
    }

    m_WeatherRouting.PreserveMultiLegLegFieldsForDialog(*it, configuration);
    (*it)->SetConfiguration(configuration);
    m_WeatherRouting.SaveLastUsedConfigurationDefaults(configuration);

    /* if the start position changed, we must reset the route */
    RouteMapConfiguration newc = (*it)->GetConfiguration();
    if (newc.StartLat != configuration.StartLat ||
        newc.StartLon != configuration.StartLon) {
      (*it)->Reset();
      refresh = true;
    } else if (newc.EndLat != configuration.EndLat ||
               newc.EndLon != configuration.EndLon)
      refresh = true;  // update drawing
  }

  double by = m_sByDegrees->GetValue();
  if (m_cRoutingEngine->GetSelection() == weather_routing::EngineSelection(weather_routing::RoutingEngine::Main) &&
      m_sToDegree->GetValue() - m_sFromDegree->GetValue() < 2 * by) {
    WR_MessageDialog mdlg(
        this, _("Warning: less than 4 different degree steps specified\n"),
        wxString(_("Weather Routing"), wxOK | wxICON_WARNING));
    mdlg.ShowModal();
  }

  m_WeatherRouting.UpdateCurrentConfigurations();

  wxFileConfig* pConf = GetOCPNConfigObject();
  pConf->SetPath(_T( "/PlugIns/WeatherRouting" ));
  // Disabled stock-host controls show the effective state (off), but must not
  // erase preferences saved for a compatible enhanced host using the same
  // profile.
  if (m_WeatherRouting.HasEnhancedChartSafety()) {
    pConf->Write(_T("UseExperimentalChartSafety"),
                 m_cbUseExperimentalChartSafety->GetValue());
    pConf->Write(_T("EnforceExperimentalChartSafety"),
                 m_cbEnforceExperimentalChartSafety->GetValue());
  }

  if (refresh) m_WeatherRouting.GetParent()->Refresh();

  m_edited_controls.clear();
  UpdateEngineControls();
  RefreshEnginePresetStatus();

  // Schedule auto-save to persist any configuration changes
  m_WeatherRouting.ScheduleAutoSave();
}
