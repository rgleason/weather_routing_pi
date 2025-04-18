/***************************************************************************
 *   Copyright (C) 2022 by OpenCPN Development Team                        *
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

#include <wx/wx.h>

#include <list>

#include "Utilities.h"
#include "Boat.h"
#include "RouteMapOverlay.h"
#include "WeatherRouting.h"
#include "RoutingTableDialog.h"

BEGIN_EVENT_TABLE(RoutingTableDialog, wxPanel)
EVT_SIZE(RoutingTableDialog::OnSize)
END_EVENT_TABLE()

// Define ColorMap structure similar to the GRIB plugin
struct ColorMap {
  double val;
  wxString text;
  unsigned char r;
  unsigned char g;
  unsigned char b;
};

// HTML colors taken from zygrib representation
static ColorMap WindMap[] = {
    {0, "#288CFF"},  {3, "#00AFFF"},  {6, "#00DCE1"},  {9, "#00F7B0"},
    {12, "#00EA9C"}, {15, "#82F059"}, {18, "#F0F503"}, {21, "#FFED00"},
    {24, "#FFDB00"}, {27, "#FFC700"}, {30, "#FFB400"}, {33, "#FF9800"},
    {36, "#FF7E00"}, {39, "#F77800"}, {42, "#EC7814"}, {45, "#E4711E"},
    {48, "#E06128"}, {51, "#DC5132"}, {54, "#D5453C"}, {57, "#CD3A46"},
    {60, "#BE2C50"}, {63, "#B41A5A"}, {66, "#AA1464"}, {70, "#962878"},
    {75, "#8C328C"}};

// HTML colors taken from zygrib representation
static ColorMap AirTempMap[] = {
    {0, "#283282"},  {5, "#273c8c"},  {10, "#264696"}, {14, "#2350a0"},
    {18, "#1f5aaa"}, {22, "#1a64b4"}, {26, "#136ec8"}, {29, "#0c78e1"},
    {32, "#0382e6"}, {35, "#0091e6"}, {38, "#009ee1"}, {41, "#00a6dc"},
    {44, "#00b2d7"}, {47, "#00bed2"}, {50, "#28c8c8"}, {53, "#78d2aa"},
    {56, "#8cdc78"}, {59, "#a0eb5f"}, {62, "#c8f550"}, {65, "#f3fb02"},
    {68, "#ffed00"}, {71, "#ffdd00"}, {74, "#ffc900"}, {78, "#ffab00"},
    {82, "#ff8100"}, {86, "#f1780c"}, {90, "#e26a23"}, {95, "#d5453c"},
    {100, "#b53c59"}};

// Color map similar to:
// https://www.ospo.noaa.gov/data/sst/contour/global.cf.gif
static ColorMap SeaTempMap[] = {
    {-2, "#cc04ae"}, {2, "#8f06e4"},  {6, "#486afa"},  {10, "#00ffff"},
    {15, "#00d54b"}, {19, "#59d800"}, {23, "#f2fc00"}, {27, "#ff1500"},
    {32, "#ff0000"}, {36, "#d80000"}, {40, "#a90000"}, {44, "#870000"},
    {48, "#690000"}, {52, "#550000"}, {56, "#330000"}};

// HTML colors taken from ZyGrib representation
static ColorMap PrecipitationMap[] = {
    {0, "#ffffff"},   {.01, "#c8f0ff"}, {.02, "#b4e6ff"}, {.05, "#8cd3ff"},
    {.07, "#78caff"}, {.1, "#6ec1ff"},  {.2, "#64b8ff"},  {.5, "#50a6ff"},
    {.7, "#469eff"},  {1.0, "#3c96ff"}, {2.0, "#328eff"}, {5.0, "#1e7eff"},
    {7.0, "#1476f0"}, {10, "#0a6edc"},  {20, "#0064c8"},  {50, "#0052aa"}};

// HTML colors taken from ZyGrib representation
static ColorMap CloudMap[] = {{0, "#ffffff"},  {1, "#f0f0e6"},  {10, "#e6e6dc"},
                              {20, "#dcdcd2"}, {30, "#c8c8b4"}, {40, "#aaaa8c"},
                              {50, "#969678"}, {60, "#787864"}, {70, "#646450"},
                              {80, "#5a5a46"}, {90, "#505036"}};

static ColorMap REFCMap[] = {{0, "#ffffff"},  {5, "#06E8E4"},  {10, "#009BE9"},
                             {15, "#0400F3"}, {20, "#00F924"}, {25, "#06C200"},
                             {30, "#009100"}, {35, "#FAFB00"}, {40, "#EBB608"},
                             {45, "#FF9400"}, {50, "#FD0002"}, {55, "#D70000"},
                             {60, "#C20300"}, {65, "#F900FE"}, {70, "#945AC8"}};

static ColorMap CAPEMap[] = {
    {0, "#0046c8"},    {5, "#0050f0"},    {10, "#005aff"},   {15, "#0069ff"},
    {20, "#0078ff"},   {30, "#000cff"},   {45, "#00a1ff"},   {60, "#00b6fa"},
    {100, "#00c9ee"},  {150, "#00e0da"},  {200, "#00e6b4"},  {300, "#82e678"},
    {500, "#9bff3b"},  {700, "#ffdc00"},  {1000, "#ffb700"}, {1500, "#f37800"},
    {2000, "#d4440c"}, {2500, "#c8201c"}, {3000, "#ad0430"}};

static bool colorsInitialized = false;

// Function to initialize the color values in the maps
static void InitColors() {
  if (colorsInitialized) return;

  wxColour c;
  // Initialize WindMap colors
  for (size_t i = 0; i < sizeof(WindMap) / sizeof(*WindMap); i++) {
    c.Set(WindMap[i].text);
    WindMap[i].r = c.Red();
    WindMap[i].g = c.Green();
    WindMap[i].b = c.Blue();
  }

  // Initialize AirTempMap colors
  for (size_t i = 0; i < sizeof(AirTempMap) / sizeof(*AirTempMap); i++) {
    c.Set(AirTempMap[i].text);
    AirTempMap[i].r = c.Red();
    AirTempMap[i].g = c.Green();
    AirTempMap[i].b = c.Blue();
  }

  // Initialize SeaTempMap colors
  for (size_t i = 0; i < sizeof(SeaTempMap) / sizeof(*SeaTempMap); i++) {
    c.Set(SeaTempMap[i].text);
    SeaTempMap[i].r = c.Red();
    SeaTempMap[i].g = c.Green();
    SeaTempMap[i].b = c.Blue();
  }

  // Initialize PrecipitationMap colors
  for (size_t i = 0; i < sizeof(PrecipitationMap) / sizeof(*PrecipitationMap);
       i++) {
    c.Set(PrecipitationMap[i].text);
    PrecipitationMap[i].r = c.Red();
    PrecipitationMap[i].g = c.Green();
    PrecipitationMap[i].b = c.Blue();
  }

  // Initialize CloudMap colors
  for (size_t i = 0; i < sizeof(CloudMap) / sizeof(*CloudMap); i++) {
    c.Set(CloudMap[i].text);
    CloudMap[i].r = c.Red();
    CloudMap[i].g = c.Green();
    CloudMap[i].b = c.Blue();
  }

  // Initialize REFCMap colors
  for (size_t i = 0; i < sizeof(REFCMap) / sizeof(*REFCMap); i++) {
    c.Set(REFCMap[i].text);
    REFCMap[i].r = c.Red();
    REFCMap[i].g = c.Green();
    REFCMap[i].b = c.Blue();
  }

  // Initialize CAPEMap colors
  for (size_t i = 0; i < sizeof(CAPEMap) / sizeof(*CAPEMap); i++) {
    c.Set(CAPEMap[i].text);
    CAPEMap[i].r = c.Red();
    CAPEMap[i].g = c.Green();
    CAPEMap[i].b = c.Blue();
  }

  colorsInitialized = true;
}

// Generic function to get color from a map based on value
static wxColor GetColorFromMap(const ColorMap* map, size_t maplen,
                               double value) {
  // Find the appropriate color range for the value
  for (size_t i = 1; i < maplen; i++) {
    if (value < map[i].val) {
      return wxColour(map[i - 1].r, map[i - 1].g, map[i - 1].b);
    }
  }

  // Return the color for the highest value if above all ranges
  return wxColour(map[maplen - 1].r, map[maplen - 1].g, map[maplen - 1].b);
}

// Helper function to determine appropriate text color based on background
// brightness
static wxColour GetTextColorForBackground(const wxColour& bgColor) {
  int brightness = (bgColor.Red() + bgColor.Green() + bgColor.Blue()) / 3;
  return brightness > 128 ? *wxBLACK : *wxWHITE;
}

// Function to get wind speed color
static wxColor GetWindSpeedColor(double knots) {
  // Ensure colors are initialized
  if (!colorsInitialized) {
    InitColors();
  }

  size_t maplen = sizeof(WindMap) / sizeof(*WindMap);
  return GetColorFromMap(WindMap, maplen, knots);
}

// Function to get air temperature color
static wxColor GetAirTempColor(double celsius) {
  // Ensure colors are initialized
  if (!colorsInitialized) {
    InitColors();
  }

  size_t maplen = sizeof(AirTempMap) / sizeof(*AirTempMap);
  return GetColorFromMap(AirTempMap, maplen, celsius);
}

// Function to get sea temperature color
static wxColor GetSeaTempColor(double celsius) {
  // Ensure colors are initialized
  if (!colorsInitialized) {
    InitColors();
  }

  size_t maplen = sizeof(SeaTempMap) / sizeof(*SeaTempMap);
  return GetColorFromMap(SeaTempMap, maplen, celsius);
}

// Function to get precipitation color
static wxColor GetPrecipitationColor(double mmPerHour) {
  // Ensure colors are initialized
  if (!colorsInitialized) {
    InitColors();
  }

  size_t maplen = sizeof(PrecipitationMap) / sizeof(*PrecipitationMap);
  return GetColorFromMap(PrecipitationMap, maplen, mmPerHour);
}

// Function to get cloud cover color
static wxColor GetCloudColor(double percentage) {
  // Ensure colors are initialized
  if (!colorsInitialized) {
    InitColors();
  }

  size_t maplen = sizeof(CloudMap) / sizeof(*CloudMap);
  return GetColorFromMap(CloudMap, maplen, percentage);
}
RoutingTableDialog::RoutingTableDialog(wxWindow* parent,
                                       WeatherRouting& weatherRouting,
                                       RouteMapOverlay* routemap)
    : wxPanel(parent, wxID_ANY, wxDefaultPosition, wxDefaultSize,
              wxBORDER_NONE),
      m_RouteMap(routemap),
      m_WeatherRouting(weatherRouting) {
  // Create a sizer for the panel
  m_mainSizer = new wxBoxSizer(wxVERTICAL);

  // Create the grid with the columns we need
  m_gridWeatherTable =
      new wxGrid(this, wxID_ANY, wxDefaultPosition, wxDefaultSize);
  m_gridWeatherTable->CreateGrid(0, COL_COUNT);

  // Set column labels
  m_gridWeatherTable->SetColLabelValue(COL_TIMESTAMP, _("Timestamp"));
  m_gridWeatherTable->SetColLabelValue(COL_LEG_NUMBER, _("Leg #"));
  m_gridWeatherTable->SetColLabelValue(COL_ETA, _("ETA"));
  m_gridWeatherTable->SetColLabelValue(COL_LEG_DISTANCE, _("Dist NM"));
  // m_gridWeatherTable->SetColLabelValue(COL_LATITUDE, _("Lat"));
  // m_gridWeatherTable->SetColLabelValue(COL_LONGITUDE, _("Lon"));
  m_gridWeatherTable->SetColLabelValue(COL_SOG, _("SOG"));
  m_gridWeatherTable->SetColLabelValue(COL_COG, _("COG"));
  m_gridWeatherTable->SetColLabelValue(COL_STW, _("STW"));
  m_gridWeatherTable->SetColLabelValue(COL_CTW, _("CTW"));
  m_gridWeatherTable->SetColLabelValue(COL_AWS, _("AWS"));
  m_gridWeatherTable->SetColLabelValue(COL_TWS, _("TWS"));
  m_gridWeatherTable->SetColLabelValue(COL_WIND_GUST, _("Wind Gust"));
  m_gridWeatherTable->SetColLabelValue(COL_TWD, _("TWD"));
  m_gridWeatherTable->SetColLabelValue(COL_TWA, _("TWA"));
  m_gridWeatherTable->SetColLabelValue(COL_AWA, _("AWA"));
  m_gridWeatherTable->SetColLabelValue(COL_SAIL_PLAN, _("Sail Plan"));
  m_gridWeatherTable->SetColLabelValue(COL_RAIN, _("Rain"));
  m_gridWeatherTable->SetColLabelValue(COL_CLOUD, _("Cloud Cover"));
  m_gridWeatherTable->SetColLabelValue(COL_AIR_TEMP, _("Air Temp"));
  m_gridWeatherTable->SetColLabelValue(COL_SEA_TEMP, _("Sea Temp"));
  m_gridWeatherTable->SetColLabelValue(COL_CURRENT_VEL, _("Curr Vel"));
  m_gridWeatherTable->SetColLabelValue(COL_CURRENT_DIR, _("Curr Dir"));
  m_gridWeatherTable->SetColLabelValue(COL_WAVE_HEIGHT, _("Wave Height"));

  // Auto size all columns initially
  for (int i = 0; i < COL_COUNT; i++) {
    m_gridWeatherTable->AutoSizeColumn(i);
  }

  // Add components to sizer
  m_mainSizer->Add(m_gridWeatherTable, 1, wxEXPAND | wxALL, 5);

  SetSizer(m_mainSizer);
  m_mainSizer->SetSizeHints(this);
  m_mainSizer->Fit(this);

  // Populate the table with data from the route
  PopulateTable();
}

RoutingTableDialog::~RoutingTableDialog() {}

void RoutingTableDialog::OnClose(wxCommandEvent& event) {
  // Hide parent Aui pane rather than destroying the dialog
  GetParent()->Hide();
}

void RoutingTableDialog::OnSize(wxSizeEvent& event) {
  event.Skip();
  if (m_gridWeatherTable) {
    // Resize the grid to fill the panel
    m_gridWeatherTable->SetSize(GetClientSize());
  }
}

void RoutingTableDialog::SetColorScheme(PI_ColorScheme cs) {
  m_colorscheme = cs;

  // Apply color scheme to panel background and grid
  wxColour backColor;

  switch (cs) {
    case PI_GLOBAL_COLOR_SCHEME_DAY:
      backColor = wxColour(212, 208, 200);
      break;
    case PI_GLOBAL_COLOR_SCHEME_DUSK:
      backColor = wxColour(128, 128, 128);
      break;
    case PI_GLOBAL_COLOR_SCHEME_NIGHT:
      backColor = wxColour(64, 64, 64);
      break;
    default:
      backColor = wxColour(212, 208, 200);
  }

  SetBackgroundColour(backColor);
  m_gridWeatherTable->SetDefaultCellBackgroundColour(backColor);

  // Set appropriate text color for the selected scheme
  wxColour textColor = cs == PI_GLOBAL_COLOR_SCHEME_DAY ? *wxBLACK : *wxWHITE;
  m_gridWeatherTable->SetDefaultCellTextColour(textColor);

  // Refresh the grid to show the new colors
  Refresh();
}

void RoutingTableDialog::PopulateTable() {
  if (!m_RouteMap) {
    wxMessageDialog mdlg(this, _("No route selected"), _("Weather Routing"),
                         wxOK | wxICON_WARNING);
    mdlg.ShowModal();
    return;
  }

  // Get plot data from the route
  std::list<PlotData> plotData = m_RouteMap->GetPlotData(false);
  if (plotData.empty()) {
    wxMessageDialog mdlg(this, _("Route contains no data"),
                         _("Weather Routing"), wxOK | wxICON_WARNING);
    mdlg.ShowModal();
    return;
  }

  // Clear existing grid content and set new size
  if (m_gridWeatherTable->GetNumberRows() > 0)
    m_gridWeatherTable->DeleteRows(0, m_gridWeatherTable->GetNumberRows());

  m_gridWeatherTable->AppendRows(plotData.size());

  // Hide the row labels (leftmost column with numbers)
  m_gridWeatherTable->SetRowLabelSize(0);

  // Get configuration for formatting
  RouteMapConfiguration configuration = m_RouteMap->GetConfiguration();
  bool useLocalTime =
      m_WeatherRouting.m_SettingsDialog.m_cbUseLocalTime->GetValue();

  // Fill the grid with data
  int row = 0;
  for (const PlotData& data : plotData) {
    wxDateTime timestamp = data.time;

    // Populate the leg number column
    m_gridWeatherTable->SetCellValue(row, COL_LEG_NUMBER,
                                     wxString::Format("%d", row + 1));

    // Convert to local time if needed
    if (useLocalTime && timestamp.IsValid()) timestamp = timestamp.FromUTC();

    // Format the timestamp
    wxString timeString = timestamp.Format("%Y-%m-%d %H:%M");

    m_gridWeatherTable->SetCellValue(row, COL_TIMESTAMP, timeString);

    // Format coordinates with appropriate signs
    /*
    m_gridWeatherTable->SetCellValue(
        row, COL_LATITUDE,
        wxString::Format("%7.4f%c", fabs(data.lat), data.lat < 0 ? 'S' : 'N'));

    m_gridWeatherTable->SetCellValue(
        row, COL_LONGITUDE,
        wxString::Format("%8.4f%c", fabs(data.lon), data.lon < 0 ? 'W' : 'E'));
    */

    // Speeds and directions
    m_gridWeatherTable->SetCellValue(row, COL_SOG,
                                     wxString::Format("%.1f kts", data.sog));
    m_gridWeatherTable->SetCellValue(
        row, COL_COG, wxString::Format("%.0f°", positive_degrees(data.cog)));
    m_gridWeatherTable->SetCellValue(row, COL_STW,
                                     wxString::Format("%.1f kts", data.stw));
    m_gridWeatherTable->SetCellValue(
        row, COL_CTW, wxString::Format("%.0f°", positive_degrees(data.ctw)));

    // Wind data
    double apparentWindSpeed = Polar::VelocityApparentWind(
        data.stw, data.twdOverWater - data.ctw, data.twsOverWater);
    double apparentWindAngle = Polar::DirectionApparentWind(
        apparentWindSpeed, data.stw, data.twdOverWater - data.ctw,
        data.twsOverWater);

    // Apply coloring to AWS cell based on apparent wind speed
    wxGridCellAttr* awsAttr = new wxGridCellAttr();
    wxColor bgColor = GetWindSpeedColor(apparentWindSpeed);
    awsAttr->SetBackgroundColour(bgColor);
    awsAttr->SetTextColour(GetTextColorForBackground(bgColor));
    m_gridWeatherTable->SetAttr(row, COL_AWS, awsAttr);

    m_gridWeatherTable->SetCellValue(
        row, COL_AWS, wxString::Format("%.1f kts", apparentWindSpeed));
    m_gridWeatherTable->SetCellValue(
        row, COL_AWA, wxString::Format("%.0f°", apparentWindAngle));
    m_gridWeatherTable->SetCellValue(
        row, COL_TWD,
        wxString::Format("%.0f°", positive_degrees(data.twdOverWater)));

    // Calculate true wind angle relative to boat course
    double twa = heading_resolve(data.twdOverWater - data.ctw);
    bool isStarboardTack = (twa > 0 && twa < 180);
    if (twa > 180) twa = 360 - twa;
    // Color the TWA cell: green for starboard tack, red for port tack
    wxGridCellAttr* attr = new wxGridCellAttr();
    if (isStarboardTack) {
      attr->SetBackgroundColour(wxColour(0, 255, 0));  // Green
    } else {
      attr->SetBackgroundColour(wxColour(255, 0, 0));  // Red
    }
    m_gridWeatherTable->SetAttr(row, COL_TWA, attr);
    m_gridWeatherTable->SetCellValue(row, COL_TWA,
                                     wxString::Format("%.0f°", twa));

    // Set the TWS value and apply color based on wind speed
    m_gridWeatherTable->SetCellValue(
        row, COL_TWS, wxString::Format("%.1f kts", data.twsOverWater));

    // Apply coloring to TWS cell based on wind speed
    wxGridCellAttr* twsAttr = new wxGridCellAttr();
    bgColor = GetWindSpeedColor(data.twsOverWater);
    twsAttr->SetBackgroundColour(bgColor);
    twsAttr->SetTextColour(GetTextColorForBackground(bgColor));
    m_gridWeatherTable->SetAttr(row, COL_TWS, twsAttr);

    // Sail plan information
    if (data.polar >= 0 && data.polar < (int)configuration.boat.Polars.size())
      m_gridWeatherTable->SetCellValue(row, COL_SAIL_PLAN,
                                       _("TODO"));  // Not implemented yet
    else
      m_gridWeatherTable->SetCellValue(row, COL_SAIL_PLAN, _("Unknown"));

    // Weather data
    if (!std::isnan(data.VW_GUST)) {
      m_gridWeatherTable->SetCellValue(
          row, COL_WIND_GUST, wxString::Format("%.1f kts", data.VW_GUST));

      // Apply coloring to Wind Gust cell based on speed
      wxGridCellAttr* gustAttr = new wxGridCellAttr();
      wxColor bgColor = GetWindSpeedColor(data.VW_GUST);
      gustAttr->SetBackgroundColour(bgColor);
      gustAttr->SetTextColour(GetTextColorForBackground(bgColor));
      m_gridWeatherTable->SetAttr(row, COL_WIND_GUST, gustAttr);
    }

    // Add placeholders for weather data that isn't fully implemented yet
    // Cloud cover - add coloring for when data becomes available
    if (!std::isnan(data.cloud_cover)) {
      double cloudPercentage =
          data.cloud_cover * 100.0;  // Assuming it's stored as 0-1 fraction
      m_gridWeatherTable->SetCellValue(
          row, COL_CLOUD, wxString::Format("%.0f%%", cloudPercentage));

      // Apply coloring to Cloud Cover cell
      wxGridCellAttr* cloudAttr = new wxGridCellAttr();
      wxColor bgColor = GetCloudColor(cloudPercentage);
      cloudAttr->SetBackgroundColour(bgColor);
      cloudAttr->SetTextColour(GetTextColorForBackground(bgColor));
      m_gridWeatherTable->SetAttr(row, COL_CLOUD, cloudAttr);
    } else {
      m_gridWeatherTable->SetCellValue(row, COL_CLOUD, "N/A");
    }

    // Precipitation (Rain) - add coloring for when data becomes available
    if (!std::isnan(data.rain_mm_per_hour)) {
      m_gridWeatherTable->SetCellValue(
          row, COL_RAIN, wxString::Format("%.1f mm/h", data.rain_mm_per_hour));

      // Apply coloring to Precipitation cell
      wxGridCellAttr* rainAttr = new wxGridCellAttr();
      wxColor bgColor = GetPrecipitationColor(data.rain_mm_per_hour);
      rainAttr->SetBackgroundColour(bgColor);
      rainAttr->SetTextColour(GetTextColorForBackground(bgColor));
      m_gridWeatherTable->SetAttr(row, COL_RAIN, rainAttr);
    } else {
      m_gridWeatherTable->SetCellValue(row, COL_RAIN, "N/A");
    }

    // Add air temperature if available
    if (!std::isnan(data.air_temperature)) {
      m_gridWeatherTable->SetCellValue(
          row, COL_AIR_TEMP, wxString::Format("%.1f °C", data.air_temperature));

      // Apply coloring to Air Temperature cell based on temperature
      wxGridCellAttr* airTempAttr = new wxGridCellAttr();
      wxColor bgColor = GetAirTempColor(data.air_temperature);
      airTempAttr->SetBackgroundColour(bgColor);
      airTempAttr->SetTextColour(GetTextColorForBackground(bgColor));
      m_gridWeatherTable->SetAttr(row, COL_AIR_TEMP, airTempAttr);
    } else {
      m_gridWeatherTable->SetCellValue(row, COL_AIR_TEMP, "N/A");
    }

    // Add sea temperature if available
    if (!std::isnan(data.sea_surface_temperature)) {
      m_gridWeatherTable->SetCellValue(
          row, COL_SEA_TEMP,
          wxString::Format("%.1f °C", data.sea_surface_temperature));

      // Apply coloring to Sea Temperature cell based on temperature
      wxGridCellAttr* seaTempAttr = new wxGridCellAttr();
      wxColor bgColor = GetSeaTempColor(data.sea_surface_temperature);
      seaTempAttr->SetBackgroundColour(bgColor);
      seaTempAttr->SetTextColour(GetTextColorForBackground(bgColor));
      m_gridWeatherTable->SetAttr(row, COL_SEA_TEMP, seaTempAttr);
    } else {
      m_gridWeatherTable->SetCellValue(row, COL_SEA_TEMP, "N/A");
    }

    // Current data
    if (data.currentSpeed > 0) {
      m_gridWeatherTable->SetCellValue(
          row, COL_CURRENT_VEL,
          wxString::Format("%.1f kts", data.currentSpeed));
      m_gridWeatherTable->SetCellValue(
          row, COL_CURRENT_DIR,
          wxString::Format("%.0f°", positive_degrees(data.currentDir)));
    }

    // Wave data
    if (!std::isnan(data.WVHT) && data.WVHT > 0)
      m_gridWeatherTable->SetCellValue(row, COL_WAVE_HEIGHT,
                                       wxString::Format("%.1f m", data.WVHT));

    row++;
  }

  // Auto-size all columns for better display
  for (int i = 0; i < COL_COUNT; i++) {
    m_gridWeatherTable->AutoSizeColumn(i);
  }
}
