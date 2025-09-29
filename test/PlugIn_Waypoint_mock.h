#include <gmock/gmock.h>
#include <ocpn_plugin.h>

// @todo: Mock these classes properly using gmock.
// For now we just define stub implementations of the 
// actual members of PlugIn_Waypoint, PlugIn_Track, etc
// So that tests compile and link correctly.
class PlugIn_Waypoint_Mock : public PlugIn_Waypoint {
    public:
        // MOCK_METHOD(, Plugin_Waypoint_Mock, ());
};

DECL_EXP PlugIn_Waypoint::PlugIn_Waypoint() {}
DECL_EXP PlugIn_Waypoint::PlugIn_Waypoint(double, double, const wxString &,
                                          const wxString &, const wxString &) {}
DECL_EXP PlugIn_Waypoint::~PlugIn_Waypoint() {}

DECL_EXP PlugIn_Waypoint_Ex::PlugIn_Waypoint_Ex() {}
DECL_EXP PlugIn_Waypoint_Ex::PlugIn_Waypoint_Ex(
    double lat, double lon, const wxString &icon_ident, const wxString &wp_name,
    const wxString &GUID, const double ScaMin, const bool bNameVisible,
    const int nRanges, const double RangeDistance, const wxColor RangeColor) {}
DECL_EXP PlugIn_Waypoint_Ex::~PlugIn_Waypoint_Ex() {}

DECL_EXP PlugIn_Route::PlugIn_Route(void) {}
DECL_EXP PlugIn_Route::~PlugIn_Route(void) {}

DECL_EXP PlugIn_Track::PlugIn_Track() {}
DECL_EXP PlugIn_Track::~PlugIn_Track() {}

DECL_EXP PlugIn_Route_Ex::PlugIn_Route_Ex(void) {}
DECL_EXP PlugIn_Route_Ex::~PlugIn_Route_Ex(void) {}
