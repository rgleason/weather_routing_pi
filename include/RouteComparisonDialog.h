// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <wx/dialog.h>
#include <wx/listctrl.h>
#include <wx/slider.h>
#include <wx/checkbox.h>
#include <wx/choice.h>
#include <wx/stattext.h>
#include "RouteMapOverlay.h"

class WeatherRouting;
class RouteComparisonDialog : public wxDialog {
 public:
  explicit RouteComparisonDialog(WeatherRouting& routing);
 private:
  struct Segment {
    RouteMapOverlay* owner{};
    std::uint64_t generation{};
    weather_routing::RetainedRouteCandidate candidate;
  };
  struct Entry {
    std::vector<Segment> segments;
    weather_routing::RouteComparisonMetric metric;
    weather_routing::RouteComfort fullComfort, windComfort;
    wxString engine;
    wxDateTime departure, eta;
  };
  void Collect();
  void Populate();
  void Choose();
  void Preview(std::size_t index);
#ifdef __OCPN__ANDROID__
  void RefreshAndroidComparison();
  wxScrolledWindow* m_AndroidScroll{};
  wxChoice* m_AndroidCandidate{};
  wxStaticText* m_AndroidDetails{};
  wxStaticText* m_AndroidNote{};
  wxButton* m_AndroidShowChart{};
#endif
  void RunHostContract(bool lifecycle);
  bool Current(const Segment& segment) const;
  WeatherRouting& m_Routing;
  std::vector<Entry> m_Entries;
  std::vector<std::vector<wxString>> m_RowValues;
  wxListCtrl* m_List{};
  wxSlider* m_Slider{};
  wxCheckBox* m_WindOnly{};
  wxCheckBox* m_ExploreComfort{};
  wxChoice* m_Order{};
  wxStaticText* m_Summary{};
  bool m_Updating{};
  int m_SortColumn{4};
  bool m_Ascending{true};
  std::string m_PreviewId;
};
