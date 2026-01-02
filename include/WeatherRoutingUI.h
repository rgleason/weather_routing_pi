///////////////////////////////////////////////////////////////////////////
// C++ code generated with wxFormBuilder (version Oct 26 2018)
// http://www.wxformbuilder.org/
//
// PLEASE DO *NOT* EDIT THIS FILE!
///////////////////////////////////////////////////////////////////////////

#pragma once

#include <wx/artprov.h>
#include <wx/xrc/xmlres.h>
#include <wx/intl.h>
#include <wx/string.h>
#include <wx/bitmap.h>
#include <wx/image.h>
#include <wx/icon.h>
#include <wx/menu.h>
#include <wx/gdicmn.h>
#include <wx/font.h>
#include <wx/colour.h>
#include <wx/settings.h>
#include <wx/frame.h>
#include <wx/listctrl.h>
#include <wx/sizer.h>
#include <wx/statbox.h>
#include <wx/panel.h>
#include <wx/button.h>
#include <wx/gauge.h>
#include <wx/splitter.h>
#include <wx/stattext.h>
#include <wx/clrpicker.h>
#include <wx/spinctrl.h>
#include <wx/checkbox.h>
#include <wx/scrolwin.h>
#include <wx/checklst.h>
#include <wx/dialog.h>
#include <wx/combobox.h>
#include <wx/datectrl.h>
#include <wx/dateevt.h>
#include <wx/timectrl.h>
#include <wx/textctrl.h>
#include <wx/choice.h>
#include <wx/valtext.h>
#include <wx/notebook.h>
#include <wx/slider.h>
#include <wx/radiobut.h>
#include <wx/statline.h>
#include <wx/html/htmlwin.h>
#include <wx/listbox.h>
#include <wx/valgen.h>
#include <wx/grid.h>
#include <wx/spinctrl.h>
#include <wx/gauge.h>

#include "wxWTranslateCatalog.h"

/**
 * Base class for the Weather Routing plugin's main interface.
 *
 * WeatherRoutingBase provides the foundational UI framework for the Weather
 * Routing plugin, defining the menu structure, event handlers, and basic window
 * layout.
 *
 * The class defines:
 * - The main menu structure with File, Position, Configuration, and View
 * options
 * - Menu items for all major operations (Open, Save, Compute, etc.)
 * - Event handler declarations for all UI interactions
 * - Context menu for right-click operations
 *
 * This base UI class establishes the interface's overall structure but
 * delegates the implementation of business logic to the derived WeatherRouting
 * class.
 *
 * @see WeatherRouting The implementation class that inherits from this base
 * class
 * @see WeatherRoutingPanel The panel containing the list controls and buttons
 */
class WeatherRoutingBase : public wxFrame {
private:
protected:
  wxMenuBar* m_menubar3;
  wxMenu* m_mFile;
  wxMenu* m_mPosition;
  wxMenu* m_mConfiguration;
  wxMenuItem* m_mBatch;
  wxMenuItem* m_mEdit;
  wxMenuItem* m_mGoTo;
  wxMenuItem* m_mDelete;
  wxMenuItem* m_mCompute;
  wxMenuItem* m_mComputeAll;
  wxMenuItem* m_mStop;
  /** Menu item to save weather routing as a track in OpenCPN core. */
  wxMenuItem* m_mSaveAsTrack;
  /** Menu item to save weather routing as a route in OpenCPN core. */
  wxMenuItem* m_mSaveAsRoute;
  /** Menu item to export weather routing as GPX file. */
  wxMenuItem* m_mExportRouteAsGPX;
  /** Menu item to save all weather routing configurations as tracks in OpenCPN
   * core. */
  wxMenuItem* m_mSaveAllAsTracks;
  wxMenu* m_mView;
  wxMenu* m_mHelp;
  wxMenuItem* m_mEdit1;
  wxMenuItem* m_mCompute1;
  wxMenuItem* m_mComputeAll1;
  wxMenuItem* m_mDelete1;
  wxMenuItem* m_mGoTo1;
  wxMenuItem* m_mStop1;
  wxMenuItem* m_mBatch1;
  wxMenu* m_menu1;

  // Virtual event handlers, override them in your derived class
  /**
   * UI event handler for closing the dialog.
   *
   * Base class handler for the close menu/button action.
   *
   * @param event The command event
   */
  virtual void OnClose(wxCloseEvent& event) { event.Skip(); }
  virtual void OnSize(wxSizeEvent& event) { event.Skip(); }
  /**
   * UI event handler for opening a configuration file.
   *
   * Base class handler for the open menu/button action.
   *
   * @param event The command event
   */
  virtual void OnOpen(wxCommandEvent& event) { event.Skip(); }
  /**
   * UI event handler for saving a configuration file.
   *
   * Base class handler for the save menu/button action.
   *
   * @param event The command event
   */
  virtual void OnSave(wxCommandEvent& event) { event.Skip(); }
  /**
   * UI event handler for saving as a configuration file.
   *
   * Base class handler for the "save as" menu/button action.
   *
   * @param event The command event
   */
  virtual void OnSaveAs(wxCommandEvent& event) { event.Skip(); }
  virtual void OnClose(wxCommandEvent& event) { event.Skip(); }
  virtual void OnNewPosition(wxCommandEvent& event) { event.Skip(); }
  virtual void OnEditPosition(wxCommandEvent& event) { event.Skip(); }
  virtual void OnUpdateBoat(wxCommandEvent& event) { event.Skip(); }
  virtual void OnDeletePosition(wxCommandEvent& event) { event.Skip(); }
  virtual void OnDeleteAllPositions(wxCommandEvent& event) { event.Skip(); }
  virtual void OnNew(wxCommandEvent& event) { event.Skip(); }
  virtual void OnBatch(wxCommandEvent& event) { event.Skip(); }
  virtual void OnEditConfiguration(wxCommandEvent& event) { event.Skip(); }
  virtual void OnGoTo(wxCommandEvent& event) { event.Skip(); }
  virtual void OnDelete(wxCommandEvent& event) { event.Skip(); }
  virtual void OnDeleteAll(wxCommandEvent& event) { event.Skip(); }
  /**
   * Handles the "Compute" button click event.
   *
   * This method starts the computation process for all currently selected route
   * configurations. It retrieves the list of selected route maps, initiates the
   * computation for each one, and updates the UI to reflect the ongoing
   * computation state.
   *
   * @param event The button click event (unused but required by event handler
   * signature)
   */
  virtual void OnCompute(wxCommandEvent& event) { event.Skip(); }
  virtual void OnComputeAll(wxCommandEvent& event) { event.Skip(); }
  virtual void OnStop(wxCommandEvent& event) { event.Skip(); }
  virtual void OnResetAll(wxCommandEvent& event) { event.Skip(); }
  /** Callback invoked when user clicks "Save as Track" menu item. */
  virtual void OnSaveAsTrack(wxCommandEvent& event) { event.Skip(); }
  /** Callback invoked when user clicks "Save as Route" menu item. */
  virtual void OnSaveAsRoute(wxCommandEvent& event) { event.Skip(); }
  /** Callback invoked when user clicks "Export as GPX" menu item. */
  virtual void OnExportRouteAsGPX(wxCommandEvent& event) { event.Skip(); }
  /** Callback invoked when user clicks "Save All as Tracks" menu item. */
  virtual void OnSaveAllAsTracks(wxCommandEvent& event) { event.Skip(); }
  virtual void OnFilter(wxCommandEvent& event) { event.Skip(); }
  virtual void OnSettings(wxCommandEvent& event) { event.Skip(); }
  virtual void OnStatistics(wxCommandEvent& event) { event.Skip(); }
  virtual void OnReport(wxCommandEvent& event) { event.Skip(); }
  virtual void OnPlot(wxCommandEvent& event) { event.Skip(); }
  virtual void OnCursorPosition(wxCommandEvent& event) { event.Skip(); }
  /**
   * Event handler for toggling display of the Route Position dialog.
   *
   * This base class handler for the route position dialog toggle. It is
   * overridden in the derived WeatherRouting class to handle the actual UI
   * state changes.
   *
   * @param event The command event (unused but required by event handler
   * signature)
   */
  virtual void OnRoutePosition(wxCommandEvent& event) { event.Skip(); }
  virtual void OnWeatherTable(wxCommandEvent& event) { event.Skip(); }
  virtual void OnInformation(wxCommandEvent& event) { event.Skip(); }
  virtual void OnManual(wxCommandEvent& event) { event.Skip(); }
  virtual void OnAbout(wxCommandEvent& event) { event.Skip(); }

public:
  wxMenuItem* m_mDeleteAll;
  wxMenu* m_mContextMenu;
  wxMenu* m_mContextMenuPositions;

  WeatherRoutingBase(wxWindow* parent, wxWindowID id = wxID_ANY,
                     const wxString& title = _("Weather Routing"),
                     const wxPoint& pos = wxDefaultPosition,
                     const wxSize& size = wxSize(-1, -1),
                     long style = wxCAPTION | wxCLOSE_BOX |
                                  wxFRAME_FLOAT_ON_PARENT | wxFRAME_NO_TASKBAR |
                                  wxRESIZE_BORDER | wxSYSTEM_MENU |
                                  wxTAB_TRAVERSAL);

  ~WeatherRoutingBase();

  void WeatherRoutingBaseOnContextMenu(wxMouseEvent& event) {
    this->PopupMenu(m_mContextMenu, event.GetPosition());
  }
};

///////////////////////////////////////////////////////////////////////////////
/// Class WeatherRoutingPanel
///////////////////////////////////////////////////////////////////////////////
class WeatherRoutingPanel : public wxPanel {
private:
protected:
  wxPanel* m_panel11;
  wxPanel* m_panel12;

  // Virtual event handlers, overide them in your derived class
  virtual void OnEditPositionClick(wxMouseEvent& event) { event.Skip(); }
  virtual void OnLeftUp(wxMouseEvent& event) { event.Skip(); }
  virtual void OnLeftDown(wxMouseEvent& event) { event.Skip(); }
  virtual void OnPositionKeyDown(wxListEvent& event) { event.Skip(); }
  virtual void OnWeatherPositionSelected(wxListEvent& event) { event.Skip(); }
  virtual void OnEditConfigurationClick(wxMouseEvent& event) { event.Skip(); }
  virtual void OnWeatherRoutesListLeftDown(wxMouseEvent& event) {
    event.Skip();
  }
  virtual void OnWeatherRoutesListLeftUp(wxMouseEvent& event) { event.Skip(); }
  virtual void OnWeatherRouteSort(wxListEvent& event) { event.Skip(); }
  virtual void OnWeatherRouteSelected(wxListEvent& event) { event.Skip(); }
  virtual void OnWeatherRouteKeyDown(wxListEvent& event) { event.Skip(); }
  /**
   * Handles the "Compute" button click event.
   *
   * This method starts the computation process for all currently selected route
   * configurations. It retrieves the list of selected route maps, initiates the
   * computation for each one, and updates the UI to reflect the ongoing
   * computation state.
   *
   * @param event The button click event (unused but required by event handler
   * signature)
   */
  virtual void OnCompute(wxCommandEvent& event) { event.Skip(); }
  /** Callback invoked when user clicks "Save as Track" menu item. */
  virtual void OnSaveAsTrack(wxCommandEvent& event) { event.Skip(); }
  /** Callback invoked when user clicks "Save as Route" menu item. */
  virtual void OnSaveAsRoute(wxCommandEvent& event) { event.Skip(); }
  /** Callback invoked when user clicks "Export as GPX" menu item. */
  virtual void OnExportRouteAsGPX(wxCommandEvent& event) { event.Skip(); }

public:
  wxSplitterWindow* m_splitter1;
  wxListCtrl* m_lPositions;
  /**
   * List control that displays configured weather routes.
   *
   * This list control contains route configurations and their computed results.
   * Each row represents a weather route with columns showing properties like
   * Name, Start position, End position.
   */
  wxListCtrl* m_lWeatherRoutes;
  wxButton* m_bCompute;
  wxButton* m_bSaveAsTrack;
  wxButton* m_bSaveAsRoute;
  wxButton* m_bExportRoute;
  wxGauge* m_gProgress;

  WeatherRoutingPanel(wxWindow* parent, wxWindowID id = wxID_ANY,
                      const wxPoint& pos = wxDefaultPosition,
                      const wxSize& size = wxSize(500, 300),
                      long style = wxTAB_TRAVERSAL,
                      const wxString& name = wxEmptyString);
  ~WeatherRoutingPanel();
};

///////////////////////////////////////////////////////////////////////////////
/// Class SettingsDialogBase
///////////////////////////////////////////////////////////////////////////////
class SettingsDialogBase : public wxDialog {
private:
protected:
  wxScrolledWindow* m_scrolledWindow4;
  wxStaticText* m_staticText74;
  wxStaticText* m_staticText73;
  wxStaticText* m_staticText75;
  wxStaticText* m_staticText70;
  wxStaticText* m_staticText71;
  wxStaticText* m_staticText711;
  wxStaticText* m_staticText115;
  wxStdDialogButtonSizer* m_sdbSizer1;
  wxButton* m_sdbSizer1OK;
  wxButton* m_sdbSizer1Help;

  // Virtual event handlers, overide them in your derived class
  virtual void OnUpdateColor(wxColourPickerEvent& event) { event.Skip(); }
  virtual void OnUpdateSpin(wxSpinEvent& event) { event.Skip(); }
  virtual void OnUpdate(wxCommandEvent& event) { event.Skip(); }
  virtual void OnUpdateColumns(wxCommandEvent& event) { event.Skip(); }
  virtual void OnHelp(wxCommandEvent& event) { event.Skip(); }

public:
  wxColourPickerCtrl* m_cpCursorRoute;
  wxColourPickerCtrl* m_cpDestinationRoute;
  wxSpinCtrl* m_sRouteThickness;
  wxSpinCtrl* m_sIsoChronThickness;
  wxSpinCtrl* m_sAlternateRouteThickness;
  wxSpinCtrl* m_sWindBarbsOnRouteThickness;
  wxCheckBox* m_cbDisplayCursorRoute;
  wxCheckBox* m_cbAlternatesForAll;
  wxCheckBox* m_cbMarkAtPolarChange;
  wxCheckBox* m_cbDisplayCurrent;
  wxCheckBox* m_cbDisplayWindBarbs;
  wxCheckBox* m_cbDisplayApparentWindBarbs;
  wxCheckBox* m_cbDisplayComfort;
  wxSpinCtrl* m_sConcurrentThreads;
  wxCheckListBox* m_cblFields;
  wxCheckBox* m_cbUseLocalTime;

  // Memory Monitor controls
  wxStaticText* m_staticText166;      // "Alert Threshold Percent:" label
  wxSpinCtrlDouble* m_spinThreshold;  // Threshold spinner
  wxCheckBox* m_checkSuppressAlert;   // Suppress checkbox
  wxCheckBox* m_checkLogUsage;        // Log checkbox
  wxStaticText* m_staticText167;      // "Usage:" label
  wxGauge* m_gaugeMemoryUsage;        // Gauge bar
  // m_staticTextMemoryStats - Created dynamically in UpdateMemoryGauge()
  // NOT a wxFormBuilder control - see SettingsDialog.h

  SettingsDialogBase(wxWindow* parent, wxWindowID id = wxID_ANY,
                     const wxString& title = _("Weather Routing Settings"),
                     const wxPoint& pos = wxDefaultPosition,
                     const wxSize& size = wxDefaultSize,
                     long style = wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER);
  ~SettingsDialogBase();
};

/**
 * Base dialog UI class for weather routing configuration.
 *
 * This class provides the UI layout and controls for editing weather routing
 * configurations. It handles visual layout and basic event connections for the
 * weather routing configuration interface.
 *
 * The dialog consists of basic and advanced tabs containing various
 * configuration parameters controlling how routes are calculated:
 * - Basic: start/end positions, departure time, boat selection
 * - Advanced: constraints, efficiency parameters, course settings, etc.
 *
 * This class defines the UI elements but delegates actual event handling and
 * business logic to the derived ConfigurationDialog class.
 */
class ConfigurationDialogBase : public wxDialog {
private:
protected:
  wxNotebook* m_notebook7;
  wxPanel* m_pBasic;
  /** Radio buttons for start selection */
  wxRadioButton* m_rbStartFromBoat;
  wxRadioButton* m_rbStartPositionSelection;
  /** The starting point of the route. */
  wxComboBox* m_cStart;
  wxStaticText* m_staticText28;
  /**
   * Button to set the start time to match the currently loaded GRIB file's
   * time. Clicking this button will set the time controls to match the valid
   * time of the currently loaded weather GRIB file. This is useful for ensuring
   * the route calculation begins with valid weather data.
   */
  wxButton* m_bGribTime;
  /**
   * Checkbox to automatically use current time when computing a route.
   * When checked, the starting time will always be updated to the current
   * system time at the moment computation begins. This option is particularly
   * useful when repeatedly recalculating routes that start from the boat's
   * current position during active navigation, as it ensures the calculation
   * always uses the latest time without requiring manual time updates from the
   * user.
   */
  wxCheckBox* m_cbUseCurrentTime;
  wxStaticText* m_staticText30;
  wxTimePickerCtrl* m_tpTime;
  /**
   * Button to set the start time to the current system time.
   * Clicking this button will update the date and time controls to the current
   * system time. This is useful for quickly setting up a route that starts
   * at the present moment. This button is disabled when m_cbUseCurrentTime is
   * checked, as the current time will be automatically used when computation
   * begins.
   */
  wxButton* m_bCurrentTime;
  wxTextCtrl* m_tBoat;
  wxButton* m_bBoatFilename;
  wxButton* m_bEditBoat;
  wxStaticText* m_staticText20;
  wxSpinCtrl* m_sMaxDivertedCourse;
  wxStaticText* m_staticText1181;
  wxStaticText* m_staticText23;
  wxSpinCtrl* m_sMaxTrueWindKnots;
  wxStaticText* m_staticText128;
  wxStaticText* m_staticText136;
  wxSpinCtrl* m_sMaxApparentWindKnots;
  wxStaticText* m_staticText1282;
  wxStaticText* m_staticText27;
  wxSpinCtrlDouble* m_sMaxSwellMeters;
  wxStaticText* m_staticText129;
  /** The end point of the route. */
  wxComboBox* m_cEnd;
  wxSpinCtrl* m_sTimeStepHours;
  wxStaticText* m_staticText110;
  wxSpinCtrl* m_sTimeStepMinutes;
  wxStaticText* m_staticText111;
  wxCheckBox* m_cbDetectLand;
  wxCheckBox* m_cbDetectBoundary;
  wxCheckBox* m_cbOptimizeTacking;
  wxCheckBox* m_cbAllowDataDeficient;
  wxButton* m_bOK;
  wxPanel* m_pAdvanced;
  wxStaticText* m_staticText26;
  wxSpinCtrl* m_sMaxLatitude;
  wxStaticText* m_staticText131;
  wxStaticText* m_staticText120;
  wxSpinCtrl* m_sWindVSCurrent;
  wxStaticText* m_staticText119;
  wxSpinCtrl* m_sMaxCourseAngle;
  wxStaticText* m_staticText1251;
  wxStaticText* m_staticText124;
  wxSpinCtrl* m_sMaxSearchAngle;
  wxStaticText* m_staticText125;
  wxStaticText* m_staticText1281;
  wxSpinCtrl* m_sCycloneMonths;
  wxStaticText* m_staticText1291;
  wxSpinCtrl* m_sCycloneDays;
  wxStaticText* m_staticText130;
  wxCheckBox* m_cbInvertedRegions;
  wxCheckBox* m_cbAnchoring;
  wxStaticText* m_staticText139;
  wxComboBox* m_cIntegrator;
  wxStaticText* m_staticText1292;
  wxSpinCtrl* m_sWindStrength;
  wxStaticText* m_staticText1301;
  wxStaticText* m_staticText24;
  wxSpinCtrl* m_sTackingTime;
  wxStaticText* m_staticText121;
  wxStaticText* m_staticText25;  // Jibing time label
  wxSpinCtrl* m_sJibingTime;
  wxStaticText* m_staticText122;  // "seconds" label for jibing time
  wxStaticText* m_staticText29;   // Sail plan change time label
  wxSpinCtrl* m_sSailPlanChangeTime;
  wxStaticText* m_staticText141;  // "seconds" label for sail plan change time
  wxStaticText* m_staticText241;
  wxSpinCtrlDouble* m_sSafetyMarginLand;
  wxStaticText* m_staticText1211;
  wxStaticText* m_staticText113;
  wxStaticText* m_staticText115;
  wxStaticText* m_staticText117;
  wxStaticText* m_staticText118;
  wxButton* m_bResetAdvanced;

  // Virtual event handlers, overide them in your derived class
  virtual void OnStartFromBoat(wxCommandEvent& event) { event.Skip(); }
  virtual void OnStartFromPosition(wxCommandEvent& event) { event.Skip(); }
  virtual void OnUpdate(wxCommandEvent& event) { event.Skip(); }
  virtual void OnUpdateDate(wxDateEvent& event) { event.Skip(); }
  virtual void OnUseCurrentTime(wxCommandEvent& event) { event.Skip(); }
  virtual void OnGribTime(wxCommandEvent& event) { event.Skip(); }
  virtual void OnUpdateTime(wxDateEvent& event) { event.Skip(); }
  virtual void OnCurrentTime(wxCommandEvent& event) { event.Skip(); }
  virtual void OnBoatFilename(wxCommandEvent& event) { event.Skip(); }
  virtual void OnEditBoat(wxCommandEvent& event) { event.Skip(); }
  virtual void EnableSpin(wxMouseEvent& event) { event.Skip(); }
  virtual void EnableSpinDouble(wxMouseEvent& event) { event.Skip(); }
  virtual void OnUpdateSpin(wxSpinEvent& event) { event.Skip(); }
  virtual void OnClose(wxCommandEvent& event) { event.Skip(); }
  virtual void OnAvoidCyclones(wxCommandEvent& event) { event.Skip(); }
  virtual void OnUseMotor(wxCommandEvent& event) { event.Skip(); }
  virtual void OnResetAdvanced(wxCommandEvent& event) { event.Skip(); }

public:
  wxDatePickerCtrl* m_dpStartDate;
  wxCheckBox* m_cbCurrents;  //!< specify whether to use currents or not.
  wxCheckBox* m_cbUseGrib;
  wxChoice* m_cClimatologyType;
  wxCheckBox* m_cbAvoidCycloneTracks;
  wxSpinCtrl*
      m_sUpwindEfficiency;  //!< Efficiency coefficient for upwind sailing
  wxSpinCtrl*
      m_sDownwindEfficiency;  // !<Efficiency coefficient for downwind sailing
  wxSpinCtrl* m_sNightCumulativeEfficiency;  //!< Efficiency coefficient for
                                             //!< night sailing
  wxSpinCtrl* m_sFromDegree;  //!< Minimum course relative to true wind.
  wxSpinCtrl* m_sToDegree;    //!< Maximum course relative to true wind.
  /** The increment course angle when calculating a isochrone route. */
  wxSpinCtrlDouble* m_sByDegrees;

  // Motor controls
  wxCheckBox* m_cbUseMotor;  //!< Enable motor when STW is below threshold
  wxSpinCtrlDouble* m_sMotorSpeedThreshold;  //!< STW threshold for motor use
  wxSpinCtrlDouble* m_sMotorSpeed;           //!< Motor speed in knots

  ConfigurationDialogBase(
      wxWindow* parent, wxWindowID id = wxID_ANY,
      const wxString& title = _("Weather Routing Configuration"),
      const wxPoint& pos = wxDefaultPosition,
      const wxSize& size = wxSize(-1, -1), 
      long style = wxDEFAULT_DIALOG_STYLE | wxMAXIMIZE_BOX | wxMINIMIZE_BOX);
  ~ConfigurationDialogBase();
};

///////////////////////////////////////////////////////////////////////////////
/// Class PlotDialogBase
///////////////////////////////////////////////////////////////////////////////
class PlotDialogBase : public wxDialog {
private:
protected:
  wxScrolledWindow* m_PlotWindow;
  wxStaticText* m_staticText138;
  wxSlider* m_sPosition;
  wxStaticText* m_staticText139;
  wxSlider* m_sScale;
  wxChoice* m_cVariable1;
  wxStaticText* m_stMousePosition1;
  wxChoice* m_cVariable2;
  wxStaticText* m_stMousePosition2;
  wxChoice* m_cVariable3;
  wxStaticText* m_stMousePosition3;
  wxRadioButton* m_rbCurrentRoute;
  wxStdDialogButtonSizer* m_sdbSizer4;
  wxButton* m_sdbSizer4OK;

  // Virtual event handlers, overide them in your derived class
  virtual void OnMouseEventsPlot(wxMouseEvent& event) { event.Skip(); }
  virtual void OnPaintPlot(wxPaintEvent& event) { event.Skip(); }
  virtual void OnSizePlot(wxSizeEvent& event) { event.Skip(); }
  virtual void OnUpdateUI(wxUpdateUIEvent& event) { event.Skip(); }
  virtual void OnUpdatePlot(wxScrollEvent& event) { event.Skip(); }
  virtual void OnUpdatePlotVariable(wxCommandEvent& event) { event.Skip(); }
  virtual void OnUpdateRoute(wxCommandEvent& event) { event.Skip(); }

  /**
   * Navigation and weather variables used in route calculations.
   *
   * This enum represents various navigation parameters related to vessel
   * movement, wind conditions, and sea state that are used in weather routing
   * calculations.
   */
  enum Variable {
    /** Vessel's Speed Over Ground (SOG), relative to the earth's surface in
       knots. */
    SPEED_OVER_GROUND,
    /** Course Over Ground (COG), relative to the earth's surface in degrees. */
    COURSE_OVER_GROUND,
    /** Vessel's Speed Through Water (STW) in knots. */
    SPEED_THROUGH_WATER,
    /** Vessel's Course Through Water (CTW) relative to the water in degrees -
       differs from heading by accounting for leeway */
    COURSE_THROUGH_WATER,
    /** Boat heading in degrees */
    HEADING,
    /** True Wind Speed relative to the water's frame of reference in knots. */
    TRUE_WIND_SPEED_OVER_WATER,
    /** True Wind Angle between vessel's Course Through Water and the True Wind
     * Angle, in degrees. */
    TRUE_WIND_ANGLE_OVER_WATER,
    /** True Wind Direction (TWD) relative to the vessel's course over water in
       degrees. */
    TRUE_WIND_DIRECTION_OVER_WATER,
    /** True Wind Speed (TWS) relative to the earth's surface in knots. */
    TRUE_WIND_SPEED_OVER_GROUND,
    /** True Wind Angle (TWA) relative to the vessel's course over ground in
       degrees. */
    TRUE_WIND_ANGLE_OVER_GROUND,
    /** True Wind Direction (TWD) in degrees relative to true north -
       meteorological, where wind is coming FROM */
    TRUE_WIND_DIRECTION_OVER_GROUND,
    /**
     * Apparent Wind Speed (AWS), as experienced by the vessel in knots.
     * This is the wind speed relative to the vessel's frame of reference,
     * taking into account the vessel's speed and direction.
     * It is calculated as the vector sum of the true wind speed and the
     * vessel's speed through the water.
     */
    APPARENT_WIND_SPEED_OVER_WATER,
    /** Apparent Wind Angle (AWA) between vessel heading and apparent wind in
       degrees. */
    APPARENT_WIND_ANGLE_OVER_WATER,
    /** Maximum wind speed in gusts in knots */
    WIND_GUST,
    /** Water current speed in knots */
    CURRENT_VELOCITY,
    /** Water current direction in degrees (from direction) */
    CURRENT_DIRECTION,
    /** Significant wave height in meters */
    SIG_WAVE_HEIGHT,
    WAVE_DIRECTION,  //!< Wave direction in degrees
    WAVE_REL,        //!< Wave direction relative to boat heading
    WAVE_PERIOD,     //!< Wave period in seconds
    /** Number of tacking maneuvers in a sailing route. */
    TACKS,
    /** Number of jibes in a sailing route. */
    JIBES,
    /** Number of sail plan changes in a sailing route. */
    SAIL_PLAN_CHANGES,
    /** Cloud Cloud cover in percent (0-100%). */
    CLOUD_COVER,
    /** Rainfall in mm. */
    RAINFALL,
    /** Air temperature in degrees Celsius. */
    AIR_TEMPERATURE,
    /** Sea surface temperature in degrees Celsius. */
    SEA_SURFACE_TEMPERATURE,
    /** CAPE value */
    CAPE,
    /** Relative humidity */
    RELATIVE_HUMIDITY,
    /** Surface air pressure */
    AIR_PRESSURE,
    /** Reflectivity */
    REFLECTIVITY,
  };

  /**
   * Structure to hold information about weather routing variables.
   *
   * This structure contains the enum value and display name of a weather
   * routing variable. It is used to populate the UI with available variables
   * for selection.
   */
  struct VariableInfo {
    Variable enumValue;
    wxString displayName;
  };

  /**
   * Get the list of available weather routing variables.
   *
   * Returns an array of VariableInfo structures, each containing
   * the enum value and display name of a variable. The count of variables is
   * also returned through the count parameter.
   *
   * @param count Reference to an integer to store the number of variables.
   * @return Pointer to an array of VariableInfo structures.
   */
  static const VariableInfo* GetVariables(int& count) {
    static const VariableInfo variableInfos[] = {
        {SPEED_OVER_GROUND, _("Speed Over Ground (SOG)")},
        {COURSE_OVER_GROUND, _("Course Over Ground (COG)")},
        {SPEED_THROUGH_WATER, _("Speed Through Water (STW)")},
        {COURSE_THROUGH_WATER, _("Course Through Water (CTW)")},
        {HEADING, _("Heading (HDG)")},
        {TRUE_WIND_DIRECTION_OVER_GROUND, _("TWD over Ground")},
        {TRUE_WIND_SPEED_OVER_GROUND, _("TWS over Ground")},
        {TRUE_WIND_ANGLE_OVER_GROUND, _("TWA over Ground")},
        {TRUE_WIND_DIRECTION_OVER_WATER, _("TWD over Water")},
        {TRUE_WIND_SPEED_OVER_WATER, _("TWS Over Water")},
        {TRUE_WIND_ANGLE_OVER_WATER, _("TWA Over Water")},
        {APPARENT_WIND_SPEED_OVER_WATER, _("AWS over Water")},
        {APPARENT_WIND_ANGLE_OVER_WATER, _("AWA over Water")},
        {WIND_GUST, _("Wind Gust")},
        {CURRENT_VELOCITY, _("Current Velocity")},
        {CURRENT_DIRECTION, _("Current Direction")},
        {SIG_WAVE_HEIGHT, _("Significant Wave Height")},
        {WAVE_DIRECTION, _("Wave Dir")},
        {WAVE_REL, _("Wave Rel")},
        {WAVE_PERIOD, _("Wave Period")},
        {TACKS, _("Tacks")},
        {JIBES, _("Jibes")},
        {SAIL_PLAN_CHANGES, _("Sail Plan Changes")},
        {CLOUD_COVER, _("Cloud Cover")},
        {RAINFALL, _("Rainfall")},
        {AIR_TEMPERATURE, _("Air Temperature")},
        {SEA_SURFACE_TEMPERATURE, _("Sea Surface Temperature")},
        {CAPE, _("CAPE")},
        {RELATIVE_HUMIDITY, _("Relative Humidity")},
        {AIR_PRESSURE, _("Air Pressure")},
        {REFLECTIVITY, _("Reflectivity")},
    };

    static const int variableInfoCount =
        sizeof(variableInfos) / sizeof(VariableInfo);
    count = variableInfoCount;
    return variableInfos;
  }

  /** Get the enum value of a variable based on its index in the UI list. */
  Variable GetVariableEnumFromIndex(int index) const;
  /** Get the index of a variable in the UI list based on its enum value. */
  int GetVariableIndexFromEnum(Variable variable) const;

public:
  wxRadioButton* m_rbCursorRoute;

  PlotDialogBase(wxWindow* parent, wxWindowID id = wxID_ANY,
                 const wxString& title = _("Weather Route Plot"),
                 const wxPoint& pos = wxDefaultPosition,
                 const wxSize& size = wxSize(-1, -1),
                 long style = wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER);
  ~PlotDialogBase();
};

///////////////////////////////////////////////////////////////////////////////
/// Class AboutDialogBase
///////////////////////////////////////////////////////////////////////////////
class AboutDialogBase : public wxDialog {
private:
protected:
  wxStaticText* m_staticText135;
  wxStaticText* m_stVersion;
  wxStaticText* m_staticText110;
  wxButton* m_bAboutAuthor;
  wxButton* m_bClose;

  // Virtual event handlers, overide them in your derived class
  virtual void OnAboutAuthor(wxCommandEvent& event) { event.Skip(); }
  virtual void OnClose(wxCommandEvent& event) { event.Skip(); }

public:
  AboutDialogBase(wxWindow* parent, wxWindowID id = wxID_ANY,
                  const wxString& title = _("About Weather Routing"),
                  const wxPoint& pos = wxDefaultPosition,
                  const wxSize& size = wxDefaultSize,
                  long style = wxDEFAULT_DIALOG_STYLE);
  ~AboutDialogBase();
};

///////////////////////////////////////////////////////////////////////////////
/// Class BoatDialogBase
///////////////////////////////////////////////////////////////////////////////
class BoatDialogBase : public wxDialog {
private:
  void CreateCursorInfoPanel(wxWindow* parent, wxSizer* parentSizer);

protected:
  wxFlexGridSizer* m_fgSizer;
  wxSplitterWindow* m_splitter2;
  wxPanel* m_panel20;
  wxNotebook* m_nNotebook;
  wxPanel* m_plot;
  wxScrolledWindow* m_PlotWindow;
  wxPanel* m_panel10;
  wxScrolledWindow* m_CrossOverChart;
  wxStaticText* m_staticText137;
  wxSpinCtrlDouble* m_sOverlapPercentage;
  wxStaticText* m_staticText138;
  wxStaticText* m_staticText139;
  wxChoice* m_cLowWindSpeedInterpolationMethod;
  wxPanel* m_panel24;
  wxStaticText* m_staticText125;
  wxStaticText* m_stBestCourseUpWindPortTack;
  wxStaticText* m_staticText1251;
  wxStaticText* m_stBestCourseUpWindStarboardTack;
  wxStaticText* m_staticText1252;
  wxStaticText* m_stBestCourseDownWindPortTack;
  wxStaticText* m_staticText12511;
  wxStaticText* m_stBestCourseDownWindStarboardTack;
  wxStaticText* m_staticText133;
  wxSpinCtrl* m_sVMGWindSpeed;
  wxChoice* m_cPlotType;
  wxChoice* m_cPlotVariable;
  wxCheckBox* m_cbFullPlot;
  wxPanel* m_panel21;
  wxListCtrl* m_lPolars;
  wxButton* m_bUp;
  wxButton* m_bDown;
  wxButton* m_bEditPolar;
  wxButton* m_bAddPolar;
  wxButton* m_bRemovePolar;
  wxStaticLine* m_staticline1;
  wxButton* m_bOpenBoat;
  wxButton* m_bSaveBoat;
  wxButton* m_bSaveAsBoat;

  // Cursor information display
  wxStaticText* m_stCursorWindAngle;
  wxStaticText* m_stCursorWindSpeed;
  wxStaticText* m_stCursorBoatSpeed;
  wxStaticText* m_stCursorVMG;
  wxStaticText* m_stCursorVMGAngle;

  // Best VMG information display
  wxStaticText* m_stBestVMGWindSpeed;
  wxStaticText* m_stBestVMGUpwindAngle;
  wxStaticText* m_stBestVMGUpwindSpeed;
  wxStaticText* m_stBestVMGUpwindVMG;
  wxStaticText* m_stBestVMGDownwindAngle;
  wxStaticText* m_stBestVMGDownwindSpeed;
  wxStaticText* m_stBestVMGDownwindVMG;

  // Virtual event handlers, overide them in your derived class
  virtual void OnMouseEventsPolarPlot(wxMouseEvent& event) { event.Skip(); }
  virtual void OnPaintPlot(wxPaintEvent& event) { event.Skip(); }
  virtual void OnUpdatePlot(wxSizeEvent& event) { event.Skip(); }
  virtual void OnPaintCrossOverChart(wxPaintEvent& event) { event.Skip(); }
  virtual void OnOverlapPercentage(wxSpinDoubleEvent& event) { event.Skip(); }
  virtual void OnVMGWindSpeed(wxSpinEvent& event) { event.Skip(); }
  virtual void OnUpdatePlot(wxCommandEvent& event) { event.Skip(); }
  virtual void OnPolarSelected(wxListEvent& event) { event.Skip(); }
  virtual void OnUpPolar(wxCommandEvent& event) { event.Skip(); }
  virtual void OnDownPolar(wxCommandEvent& event) { event.Skip(); }
  virtual void OnEditPolar(wxCommandEvent& event) { event.Skip(); }
  virtual void OnAddPolar(wxCommandEvent& event) { event.Skip(); }
  virtual void OnRemovePolar(wxCommandEvent& event) { event.Skip(); }
  virtual void OnOpenBoat(wxCommandEvent& event) { event.Skip(); }
  virtual void OnSaveBoat(wxCommandEvent& event) { event.Skip(); }
  virtual void OnSaveAsBoat(wxCommandEvent& event) { event.Skip(); }

public:
  wxGauge* m_gCrossOverChart;

  BoatDialogBase(wxWindow* parent, wxWindowID id = wxID_ANY,
                 const wxString& title = _("Boat"),
                 const wxPoint& pos = wxDefaultPosition,
                 const wxSize& size = wxSize(-1, -1),
                 long style = wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER);
  ~BoatDialogBase();

  void m_splitter2OnIdle(wxIdleEvent&) {
    m_splitter2->SetSashPosition(0);
    m_splitter2->Disconnect(
        wxEVT_IDLE, wxIdleEventHandler(BoatDialogBase::m_splitter2OnIdle),
        nullptr, this);
  }
};

///////////////////////////////////////////////////////////////////////////////
/// Class StatisticsDialogBase
///////////////////////////////////////////////////////////////////////////////
class StatisticsDialogBase : public wxDialog {
private:
protected:
  wxStaticText* m_staticText511;
  wxStaticText* m_stRunTime;
  wxStaticText* m_staticText47;
  wxStaticText* m_stState;
  wxStaticText* m_staticText53;
  wxStaticText* m_stIsoChrons;
  wxStaticText* m_staticText55;
  wxStaticText* m_stRoutes;
  wxStaticText* m_staticText57;
  wxStaticText* m_stInvRoutes;
  wxStaticText* m_staticText90;
  wxStaticText* m_stSkipPositions;
  wxStaticText* m_staticText49;
  wxStaticText* m_stPositions;
  wxStdDialogButtonSizer* m_sdbSizer5;
  wxButton* m_sdbSizer5OK;

public:
  StatisticsDialogBase(wxWindow* parent, wxWindowID id = wxID_ANY,
                       const wxString& title = _("Weather Routing Statistics"),
                       const wxPoint& pos = wxDefaultPosition,
                       const wxSize& size = wxDefaultSize,
                       long style = wxDEFAULT_DIALOG_STYLE);
  ~StatisticsDialogBase();
};

///////////////////////////////////////////////////////////////////////////////
/// Class ReportDialogBase
///////////////////////////////////////////////////////////////////////////////
class ReportDialogBase : public wxDialog {
private:
protected:
  wxHtmlWindow* m_htmlConfigurationReport;
  wxHtmlWindow* m_htmlRoutesReport;
  wxButton* m_bInformation;
  wxButton* m_bClose;

  // Virtual event handlers, overide them in your derived class
  virtual void OnInformation(wxCommandEvent& event) { event.Skip(); }
  virtual void OnClose(wxCommandEvent& event) { event.Skip(); }

public:
  ReportDialogBase(wxWindow* parent, wxWindowID id = wxID_ANY,
                   const wxString& title = _("Weather Route Report"),
                   const wxPoint& pos = wxDefaultPosition,
                   const wxSize& size = wxSize(-1, -1),
                   long style = wxCLOSE_BOX | wxDEFAULT_DIALOG_STYLE |
                                wxRESIZE_BORDER);
  ~ReportDialogBase();
};

/**
 * Base class for the Configuration Batch Dialog.
 *
 * This dialog allows users to create and manage batch configurations for
 * weather routing. Batch processing enables automated generation of multiple
 * routing scenarios with variations in parameters such as departure times, boat
 * configurations, and wind settings.
 *
 * The dialog provides UI controls for:
 * - Setting start and end dates/times and their spacing intervals
 * - Managing source and destination positions
 * - Selecting boat configurations
 * - Configuring wind strength parameters
 * - Executing batch generation
 */
class ConfigurationBatchDialogBase : public wxDialog {
private:
protected:
  wxNotebook* m_notebookConfigurations;
  wxPanel* m_panel8;
  wxStaticText* m_staticText108;
  wxStaticText* m_stStartDateTime;
  wxStaticText* m_staticText121;
  wxStaticText* m_staticText122;
  wxStaticText* m_staticText123;
  wxStaticText* m_staticText124;
  wxStaticText* m_staticText125;
  wxStaticText* m_staticText126;
  wxButton* m_button41;
  wxButton* m_button38;
  wxButton* m_button39;
  wxButton* m_button40;
  wxPanel* m_pRoutes;
  wxListBox* m_lSources;
  wxListBox* m_lDestinations;
  wxStaticText* m_staticText1241;
  wxTextCtrl* m_tMiles;
  wxStaticText* m_staticText1251;
  wxButton* m_bConnect;
  wxButton* m_bDisconnectAll;
  wxPanel* m_panel9;
  wxButton* m_bAddBoat;
  wxButton* m_bRemoveBoat;
  wxPanel* m_panel17;
  wxStaticText* m_staticText131;
  wxStaticText* m_staticText134;
  wxStaticText* m_staticText132;
  wxStaticText* m_staticText1341;
  wxStaticText* m_staticText133;
  wxStaticText* m_staticText1342;
  wxButton* m_button46;
  wxButton* m_button47;
  wxButton* m_bInformation;
  wxButton* m_bReset;
  wxButton* m_bGenerate;
  wxButton* m_bOK;

  // Virtual event handlers, overide them in your derived class
  virtual void OnOnce(wxCommandEvent& event) { event.Skip(); }
  virtual void OnDaily(wxCommandEvent& event) { event.Skip(); }
  virtual void OnWeekly(wxCommandEvent& event) { event.Skip(); }
  virtual void OnMonthly(wxCommandEvent& event) { event.Skip(); }
  virtual void OnSources(wxCommandEvent& event) { event.Skip(); }
  virtual void OnDestinations(wxCommandEvent& event) { event.Skip(); }
  virtual void OnConnect(wxCommandEvent& event) { event.Skip(); }
  virtual void OnDisconnectAll(wxCommandEvent& event) { event.Skip(); }
  virtual void OnAddBoat(wxCommandEvent& event) { event.Skip(); }
  virtual void OnRemoveBoat(wxCommandEvent& event) { event.Skip(); }
  virtual void On100(wxCommandEvent& event) { event.Skip(); }
  virtual void On80to120(wxCommandEvent& event) { event.Skip(); }
  virtual void OnInformation(wxCommandEvent& event) { event.Skip(); }
  virtual void OnReset(wxCommandEvent& event) { event.Skip(); }
  virtual void OnGenerate(wxCommandEvent& event) { event.Skip(); }
  virtual void OnClose(wxCommandEvent& event) { event.Skip(); }

public:
  wxTextCtrl* m_tStartDays;
  wxTextCtrl* m_tStartHours;
  wxTextCtrl* m_tStartSpacingDays;
  wxTextCtrl* m_tStartSpacingHours;
  wxListBox* m_lBoats;
  wxSpinCtrl* m_sWindStrengthMin;
  wxSpinCtrl* m_sWindStrengthMax;
  wxSpinCtrl* m_sWindStrengthStep;

  ConfigurationBatchDialogBase(
      wxWindow* parent, wxWindowID id = wxID_ANY,
      const wxString& title = _("Weather Routing Configuration Batch"),
      const wxPoint& pos = wxDefaultPosition,
      const wxSize& size = wxSize(-1, -1),
      long style = wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER);
  ~ConfigurationBatchDialogBase();
};

///////////////////////////////////////////////////////////////////////////////
/// Class FilterRoutesDialogBase
///////////////////////////////////////////////////////////////////////////////
class FilterRoutesDialogBase : public wxDialog {
private:
protected:
  wxChoice* m_cCategory;
  wxTextCtrl* m_tFilter;
  wxButton* m_button48;
  wxButton* m_bDone;

  // Virtual event handlers, overide them in your derived class
  virtual void OnCategory(wxCommandEvent& event) { event.Skip(); }
  virtual void OnFilterText(wxCommandEvent& event) { event.Skip(); }
  virtual void OnResetAll(wxCommandEvent& event) { event.Skip(); }
  virtual void OnDone(wxCommandEvent& event) { event.Skip(); }

public:
  FilterRoutesDialogBase(wxWindow* parent, wxWindowID id = wxID_ANY,
                         const wxString& title = _("Filter Routes"),
                         const wxPoint& pos = wxDefaultPosition,
                         const wxSize& size = wxDefaultSize,
                         long style = wxDEFAULT_DIALOG_STYLE);
  ~FilterRoutesDialogBase();
};

///////////////////////////////////////////////////////////////////////////////
/// Class CursorPositionDialog
///////////////////////////////////////////////////////////////////////////////
class CursorPositionDialog : public wxDialog {
private:
protected:
  wxStaticText* m_staticText134;
  wxStaticText* m_staticText128;
  wxStaticText* m_staticText124;
  wxStaticText* m_staticText130;
  wxStaticText* m_staticText126;
  wxStaticText* m_staticText127;
  wxStaticText* m_staticText129;
  wxStaticText* m_staticText122;
  wxStdDialogButtonSizer* m_sdbSizer5;
  wxButton* m_sdbSizer5OK;

public:
  wxStaticText* m_stTime;
  wxStaticText* m_stPosition;
  wxStaticText* m_stPolar;
  wxStaticText* m_stSailChanges;
  wxStaticText* m_stTacks;
  wxStaticText* m_stJibes;
  wxStaticText* m_stSailPlanChanges;
  wxStaticText* m_stWeatherData;

  CursorPositionDialog(wxWindow* parent, wxWindowID id = wxID_ANY,
                       const wxString& title = _("Cursor Position"),
                       const wxPoint& pos = wxDefaultPosition,
                       const wxSize& size = wxSize(-1, -1),
                       long style = wxDEFAULT_DIALOG_STYLE);
  ~CursorPositionDialog();
};

///////////////////////////////////////////////////////////////////////////////
/// Class RoutePositionDialog
///////////////////////////////////////////////////////////////////////////////
class RoutePositionDialog : public wxDialog {
private:
protected:
  wxStaticText* m_staticText134;
  wxStaticText* m_staticDuration;
  wxStaticText* m_staticText128;
  wxStaticText* m_staticText128161;
  wxStaticText* m_staticText12816;
  wxStaticText* m_staticText1281;
  wxStaticText* m_staticText12812;
  wxStaticText* m_staticText12811;
  wxStaticText* m_staticText12813;
  wxStaticText* m_staticText12814;
  wxStaticText* m_staticText12815;
  wxStaticText* m_staticText124;
  wxStaticText* m_staticText130;
  wxStaticText* m_staticText126;
  wxStaticText* m_staticText129;
  wxStaticText* m_staticText127;
  wxStaticText* m_staticText122;
  wxStdDialogButtonSizer* m_sdbSizer5;
  wxButton* m_sdbSizer5OK;

public:
  wxStaticText* m_stTime;
  wxStaticText* m_stDuration;
  wxStaticText* m_stPosition;
  wxStaticText* m_stBoatCourse;
  wxStaticText* m_stBoatSpeed;
  wxStaticText* m_stTWS;
  wxStaticText* m_stAWS;
  wxStaticText* m_stTWA;
  wxStaticText* m_stAWA;
  wxStaticText* m_stWaves;
  wxStaticText* m_stWindGust;
  wxStaticText* m_stPolar;
  wxStaticText* m_stSailChanges;
  wxStaticText* m_stTacks;
  wxStaticText* m_stJibes;
  wxStaticText* m_stSailPlanChanges;
  wxStaticText* m_stWeatherData;

  RoutePositionDialog(wxWindow* parent, wxWindowID id = wxID_ANY,
                      const wxString& title = _("Route Position"),
                      const wxPoint& pos = wxDefaultPosition,
                      const wxSize& size = wxSize(-1, -1),
                      long style = wxDEFAULT_DIALOG_STYLE);
  ~RoutePositionDialog();
};

///////////////////////////////////////////////////////////////////////////////
/// Class NewPositionDialog
///////////////////////////////////////////////////////////////////////////////
class NewPositionDialog : public wxDialog {
private:
protected:
  wxStaticText* m_staticText140;
  wxStaticText* m_staticText142;
  wxStaticText* m_staticText143;
  wxStaticText* m_staticText144;
  wxStaticText* m_staticText145;
  wxStaticText* m_staticText146;
  wxStaticText* m_staticText147;
  wxStdDialogButtonSizer* m_sdbSizer4;
  wxButton* m_sdbSizer4OK;
  wxButton* m_sdbSizer4Cancel;

public:
  wxTextCtrl* m_tName;
  wxTextCtrl* m_tLatitude;
  wxTextCtrl* m_tLongitude;

  NewPositionDialog(wxWindow* parent, wxWindowID id = wxID_ANY,
                    const wxString& title = _("Edit Weather Routing Position"),
                    const wxPoint& pos = wxDefaultPosition,
                    const wxSize& size = wxDefaultSize,
                    long style = wxDEFAULT_DIALOG_STYLE);
  ~NewPositionDialog();
};

///////////////////////////////////////////////////////////////////////////////
/// Class EditPolarDialogBase
///////////////////////////////////////////////////////////////////////////////
class EditPolarDialogBase : public wxDialog {
private:
protected:
  wxNotebook* m_notebook6;
  wxPanel* m_panel19;
  wxGrid* m_gPolar;
  wxStaticText* m_staticText1351;
  wxPanel* m_panel20;
  // The angle between the boat's heading and the True Wind Direction.
  wxTextCtrl* m_tTrueWindAngle;
  wxListBox* m_lTrueWindAngles;
  wxButton* m_bAddTrueWindAngle;
  wxButton* m_bRemoveTrueWindAngle;
  wxTextCtrl* m_tTrueWindSpeed;
  wxListBox* m_lTrueWindSpeeds;
  wxButton* m_bAddTrueWindSpeed;
  wxButton* m_bRemoveTrueWindSpeed;
  wxPanel* m_panel21;
  wxNotebook* m_notebook61;
  wxPanel* m_panel22;
  wxRadioButton* m_rbApparentWind;
  wxRadioButton* m_rbTrueWind;
  wxStaticText* m_staticText133;
  wxTextCtrl* m_tWindSpeed;
  wxStaticText* m_staticText134;
  wxTextCtrl* m_tWindDirection;
  wxStaticText* m_staticText135;
  wxTextCtrl* m_tBoatSpeed;
  wxButton* m_button46;
  wxListCtrl* m_lMeasurements;
  wxButton* m_bRemoveMeasurement;
  wxButton* m_bRemoveAllMeasurements;
  wxButton* m_button48;
  wxButton* m_button50;
  wxPanel* m_panel23;
  wxPanel* m_panel17;
  wxStaticText* m_staticText139;
  wxStaticText* m_staticText100;
  wxChoice* m_cHullType;
  wxStaticText* m_staticText58;
  wxSpinCtrl* m_sDisplacement;
  wxStaticText* m_staticText121;
  wxStaticText* m_staticText128;
  wxSpinCtrl* m_sSailArea;
  wxStaticText* m_staticText129;
  wxStaticText* m_staticText57;
  wxSpinCtrl* m_sLWL;
  wxStaticText* m_staticText122;
  wxStaticText* m_staticText109;
  wxSpinCtrl* m_sLOA;
  wxStaticText* m_staticText127;
  wxStaticText* m_staticText113;
  wxSpinCtrl* m_sBeam;
  wxStaticText* m_staticText126;
  wxStaticText* m_staticText119;
  wxStaticText* m_stSailAreaDisplacementRatio;
  wxStaticText* m_staticText105;
  wxStaticText* m_stDisplacementLengthRatio;
  wxStaticText* m_staticText92;
  wxStaticText* m_stHullSpeed;
  wxStaticText* m_staticText94;
  wxStaticText* m_stCapsizeRisk;
  wxStaticText* m_staticText96;
  wxStaticText* m_stComfortFactor;
  wxPanel* m_panel221;
  wxGrid* m_grid1;
  wxButton* m_button42;
  wxButton* m_button43;
  wxButton* m_button44;
  wxButton* m_button45;
  wxButton* m_button461;
  wxStdDialogButtonSizer* m_sdbSizer6;
  wxButton* m_sdbSizer6Save;
  wxButton* m_sdbSizer6Cancel;

  // Virtual event handlers, overide them in your derived class
  virtual void OnPolarGridChanged(wxGridEvent& event) { event.Skip(); }
  virtual void d(wxCommandEvent& event) { event.Skip(); }
  virtual void OnAddTrueWindAngle(wxCommandEvent& event) { event.Skip(); }
  virtual void OnRemoveTrueWindAngle(wxCommandEvent& event) { event.Skip(); }
  virtual void OnAddTrueWindSpeed(wxCommandEvent& event) { event.Skip(); }
  virtual void OnRemoveTrueWindSpeed(wxCommandEvent& event) { event.Skip(); }
  virtual void OnAddMeasurement(wxCommandEvent& event) { event.Skip(); }
  virtual void OnRemoveMeasurement(wxCommandEvent& event) { event.Skip(); }
  virtual void OnRemoveAllMeasurements(wxCommandEvent& event) { event.Skip(); }
  virtual void OnGeneratePolar(wxCommandEvent& event) { event.Skip(); }
  virtual void OnRecompute(wxCommandEvent& event) { event.Skip(); }
  virtual void OnRecomputeSpin(wxSpinEvent& event) { event.Skip(); }
  virtual void OnSave(wxCommandEvent& event) { event.Skip(); }

public:
  EditPolarDialogBase(wxWindow* parent, wxWindowID id = wxID_ANY,
                      const wxString& title = _("Edit Polar"),
                      const wxPoint& pos = wxDefaultPosition,
                      const wxSize& size = wxSize(-1, -1),
                      long style = wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER);
  ~EditPolarDialogBase();
};

/**
 * UI component to display a weather table for a specific weather routing.
 */
class RoutingTablePanelBase : public wxDialog {
private:
protected:
  wxGrid* m_gridWeatherTable;
  wxButton* m_btnClose;

  // Virtual event handlers, override them in your derived class
  virtual void OnClose(wxCommandEvent& event) { event.Skip(); }

public:
  RoutingTablePanelBase(wxWindow* parent, wxWindowID id = wxID_ANY,
                        const wxString& title = _("Weather Routing Table"),
                        const wxPoint& pos = wxDefaultPosition,
                        const wxSize& size = wxSize(-1, -1),
                        long style = wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER);
  ~RoutingTablePanelBase();
};
