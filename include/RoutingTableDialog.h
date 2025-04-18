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

#ifndef _WEATHER_TABLE_DIALOG_H_
#define _WEATHER_TABLE_DIALOG_H_

#include "WeatherRoutingUI.h"
#include <wx/aui/aui.h>

class WeatherRouting;

/**
 * Dialog implementation to display a detailed weather table for a specific
 * route. The table shows various weather and navigation data at different
 * timestamps along the route.
 */
class RoutingTableDialog : public wxPanel {
  friend class WeatherRouting;

public:
  /**
   * Constructor for the weather table dialog.
   *
   * @param parent Parent window
   * @param weatherRouting Reference to the main WeatherRouting class
   * @param routemap Pointer to the route to display data from
   */
  RoutingTableDialog(wxWindow* parent, WeatherRouting& weatherRouting,
                     RouteMapOverlay* routemap);
  ~RoutingTableDialog();

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

protected:
  RouteMapOverlay* m_RouteMap;

private:
  void OnClose(wxCommandEvent& event);
  void OnSize(wxSizeEvent& event);

  enum WeatherDataColumn {
    COL_TIMESTAMP,
    COL_LEG_NUMBER,    //!< Leg number
    COL_ETA,           //!< Estimated Time of Arrival (ETA) to this leg
    COL_LEG_DISTANCE,  //!< Distance to the next waypoint
    // COL_LATITUDE,      //!< Latitude position
    // COL_LONGITUDE,     //!< Longitude position
    COL_SOG,          //!< Speed Over Ground
    COL_COG,          //!< Course Over Ground
    COL_STW,          //!< Speed Through Water
    COL_CTW,          //!< Course Through Water
    COL_AWS,          //!< Apparent Wind Speed
    COL_TWS,          //!< True Wind Speed
    COL_WIND_GUST,    //!< Wind Gust
    COL_TWD,          //!< True Wind Direction
    COL_TWA,          //!< True Wind Angle
    COL_AWA,          //!< Apparent Wind Angle
    COL_SAIL_PLAN,    //!< Sail Plan
    COL_RAIN,         //!< Rain
    COL_CLOUD,        //!< Cloud Cover
    COL_AIR_TEMP,     //!< Air Temperature
    COL_SEA_TEMP,     //!< Sea Temperature
    COL_CURRENT_VEL,  //!< Current Velocity
    COL_CURRENT_DIR,  //!< Current Direction
    COL_WAVE_HEIGHT,  //!< Wave Height
    COL_COUNT
  };

  WeatherRouting& m_WeatherRouting;
  PI_ColorScheme m_colorscheme;

  wxGrid* m_gridWeatherTable;
  wxSizer* m_mainSizer;

  DECLARE_EVENT_TABLE()
};

#endif  // _WEATHER_TABLE_DIALOG_H_