/***************************************************************************
 *   Copyright (C) 2016 by OpenCPN Development Team                        *
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
 ***************************************************************************/

#ifndef _WEATHER_ROUTING_TABLE_DIALOG_H_
#define _WEATHER_ROUTING_TABLE_DIALOG_H_

#include <wx/aui/aui.h>
#include <map>

#include "WeatherRoutingUI.h"
#include "ocpn_plugin.h"

class WeatherRouting;
class RouteMapOverlay;
struct RouteMapConfiguration;
class PlotData;

/**
 * Dialog implementation to display a detailed weather table for a specific
 * route. The table shows various weather and navigation data at different
 * timestamps along the route.
 */
class RoutingTablePanel : public wxPanel {
  friend class WeatherRouting;

public:
  /**
   * Constructor for the weather table dialog.
   *
   * @param parent Parent window
   * @param weatherRouting Reference to the main WeatherRouting class
   * @param routemap Pointer to the route to display data from
   */
  RoutingTablePanel(wxWindow* parent, WeatherRouting& weatherRouting,
                    RouteMapOverlay* routemap);
  ~RoutingTablePanel();


  /**
   * Multiple Route version - Populates the weather table with data from the route.
   * This method extracts weather and navigation data at each position along the
   * route and fills the grid with the appropriate values.
   */
  void PopulateMultiRouteTable(const std::vector<RouteMapOverlay*>& overlays);

  /**
   * Populates the weather table with data from the route.
   * This method extracts weather and navigation data at each position along the
   * route and fills the grid with the appropriate values.
   */
  void PopulateTable();

  /**
   * Sets the panel background color to match the current color scheme
   */
  void SetColorScheme(PI_ColorScheme cs);

  /**
   * Highlights the row with the closest ETA to the given time.
   * This method helps visualize where on the route the vessel would be at
   * the currently selected time in the GRIB timeline widget.
   *
   * @param timelineTime The time selected in the GRIB timeline widget
   */
  void UpdateTimeHighlight(wxDateTime timelineTime);

  /**
   * Export the table data to Excel XML format with formatting preserved
   */
  void ExportToExcel();

  /**
   * Update the summary information displayed above the table
   */
  void UpdateSummary();

protected:
  RouteMapOverlay* m_RouteMap;

private:
  void OnClose(wxCommandEvent& event);
  void OnSize(wxSizeEvent& event);
  void OnExportButton(wxCommandEvent& event);

  /**
   * Helper function to format and display sail plan information
   *
   * @param row Current row in the table
   * @param data Plot data for the current point
   * @param configuration Current route configuration
   * @param prevData Previous point's plot data (nullptr if this is first point)
   */
  void handleSailPlanCell(int row, const PlotData& data,
                          const RouteMapConfiguration& configuration,
                          const PlotData* prevData);
  /** Helper function to set cell value with colored background. */
  void setCellWithColor(int row, int col, const wxString& value,
                        const wxColor& bgColor);

  /**
   * Helper function to set cell value with time-of-day background color.
   * This method uses the SunCalculator to determine appropriate colors based on
   * the time of day and daylight status at the given location.
   *
   * @param row Grid row number
   * @param col Grid column number
   * @param value Text value to display in the cell
   * @param dateTime UTC time for this route point
   * @param lat Latitude of the route point
   * @param lon Longitude of the route point
   */
  void setCellWithTimeOfDayColor(int row, int col, const wxString& value,
                                 const wxDateTime& dateTime, double lat,
                                 double lon);

  /** Helper functions for Excel export */
  wxString ConvertColorToHex(const wxColour& color);
  wxString ConvertColorToARGB(const wxColour& color);
  wxString EscapeXML(const wxString& text);
  bool WriteExcelXML(const wxString& filename);
  bool WriteXLSX(const wxString& filename);
  bool WriteSimpleXML(const wxString& filename);
  wxString CreateStylesXML();
  wxString CreateWorksheetXML();
  wxString GetCellReference(int row, int col);

  /** Helper functions for cell styling in Excel export */
  struct CellStyle {
    wxColour bgColor;
    wxColour textColor;
    bool isBold;

    CellStyle() : bgColor(*wxWHITE), textColor(*wxBLACK), isBold(false) {}
    CellStyle(const wxColour& bg, const wxColour& text, bool bold = false)
        : bgColor(bg), textColor(text), isBold(bold) {}

    bool operator<(const CellStyle& other) const {
      if (bgColor.GetRGB() != other.bgColor.GetRGB()) {
        return bgColor.GetRGB() < other.bgColor.GetRGB();
      }
      if (textColor.GetRGB() != other.textColor.GetRGB()) {
        return textColor.GetRGB() < other.textColor.GetRGB();
      }
      return isBold < other.isBold;
    }
  };

  CellStyle GetCellStyle(int row, int col);
  int GetOrCreateStyleId(const CellStyle& style);
  wxString CreateStylesXMLWithColors();
  wxString CreateWorksheetXMLWithColors();

  /** Helper function to create summary tab */
  void CreateSummaryTab();

  /** Helper function to calculate summary statistics */
  struct SummaryData {
    wxDateTime startTime, endTime;
    double totalDistance;
    double minWindSpeed, maxWindSpeed;
    double minWindGust, maxWindGust;
    double minAWS, maxAWS;
    double minAWA, maxAWA;
    double minWaveHeight, maxWaveHeight;
    double minTemp, maxTemp;
    int sailChanges;
    int tackChanges;
    int jibeChanges;
    double motorDurationPercentage;  // Percentage of time under motor
    double motorDistancePercentage;  // Percentage of distance under motor
    double motorDistance;            // Total distance under motor
    wxTimeSpan motorDuration;        // Total time under motor
  };
  SummaryData CalculateSummaryData();

  enum WeatherDataColumn {
    COL_LEG_NUMBER,  //!< Leg number
    COL_ETA,  //!< Estimated Time of Arrival - actual date/time of this point
    COL_ENROUTE,         //!< Duration from start (cumulative time)
    COL_LEG_DISTANCE,    //!< Distance from start (cumulative distance)
    COL_SOG,             //!< Speed Over Ground
    COL_COG,             //!< Course Over Ground
    COL_STW,             //!< Speed Through Water
    COL_CTW,             //!< Course Through Water
    COL_HDG,             //!< Boat Heading
    COL_WIND_SOURCE,     //!< Wind data source (GRIB, climatology, deficient)
    COL_AWS,             //!< Apparent Wind Speed
    COL_TWS,             //!< True Wind Speed
    COL_WIND_GUST,       //!< Wind Gust
    COL_TWD,             //!< True Wind Direction
    COL_TWA,             //!< True Wind Angle
    COL_AWA,             //!< Apparent Wind Angle
    COL_SAIL_PLAN,       //!< Sail Plan
    COL_COMFORT,         //!< Sailing Comfort Level
    COL_WAVE_HEIGHT,     //!< Wave Height
    COL_WAVE_DIRECTION,  //!< Wave Direction
    COL_WAVE_REL,        //!< Wave Direction Relative to Heading
    COL_WAVE_PERIOD,     //!< Wave Period
    COL_RAIN,            //!< Rain
    COL_CLOUD,           //!< Cloud Cover
    COL_AIR_TEMP,        //!< Air Temperature
    COL_SEA_TEMP,        //!< Sea Temperature
    COL_REL_HUMIDITY,    //!< Relative Humidity
    COL_AIR_PRESSURE,    //!< Air Pressure
    COL_CAPE,            //!< CAPE
    COL_REFLECTIVITY,    //!< Reflectivity
    COL_CURRENT_SOURCE,  //!< Current data source (GRIB, climatology, deficient)
    COL_CURRENT_SPEED,   //!< Sea Current Speed
    COL_CURRENT_DIR,     //!< Sea Current Direction
    /**
     * True Current Angle relative to COG.
     *   - 0 degrees: Current flowing in exactly the same direction as the
     * boat's course (maximum positive effect)
     *   - 180/-180 degrees: Current flowing in exactly the opposite direction
     * (maximum negative effect)
     *   - 90/-90 degrees: Current flowing perpendicular to the boat's course
     * (minimal direct effect on speed)
     */
    COL_CURRENT_ANGLE,
    COL_COUNT
  };

  WeatherRouting& m_WeatherRouting;
  PI_ColorScheme m_colorscheme;

  wxNotebook* m_notebook;
  wxPanel* m_summaryTab;
  wxPanel* m_tableTab;

  // Summary controls
  wxStaticText* m_departureText;
  wxStaticText* m_arrivalText;
  wxStaticText* m_distanceText;
  wxStaticText* m_durationText;
  wxStaticText* m_windRangeText;
  wxStaticText* m_windGustRangeText;
  wxStaticText* m_awsRangeText;
  wxStaticText* m_awaRangeText;
  wxStaticText* m_waveRangeText;
  wxStaticText* m_tempRangeText;
  wxStaticText* m_sailChangesText;
  wxStaticText* m_motorDurationText;
  wxStaticText* m_motorDistanceText;
  wxStaticText* m_tackChangesText;
  wxStaticText* m_jibeChangesText;
  wxButton* m_exportButton;

  wxGrid* m_gridWeatherTable;
  wxSizer* m_mainSizer;

  // Members for time-based highlighting
  int m_highlightedRow;           // Current highlighted row index
  wxDateTime m_lastTimelineTime;  // Last time value used for highlighting

  // Store original cell colors before highlighting
  std::map<int, std::vector<wxColour>> m_originalCellColors;

  // Style mapping for Excel export
  std::map<CellStyle, int> m_styleMap;
  std::vector<CellStyle> m_styles;

  DECLARE_EVENT_TABLE()
};

#endif
