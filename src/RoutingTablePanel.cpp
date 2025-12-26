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
#include <wx/filedlg.h>
#include <wx/msgdlg.h>
#include <wx/filename.h>
#include <wx/textfile.h>
#include <wx/grid.h>
#include <wx/notebook.h>
#include <wx/zipstrm.h>
#include <wx/wfstream.h>

#include <list>
#include <cfloat>

#include "Utilities.h"
#include "Boat.h"
#include "RouteMapOverlay.h"
#include "WeatherRouting.h"
#include "RoutingTablePanel.h"
#include "SunCalculator.h"

BEGIN_EVENT_TABLE(RoutingTablePanel, wxPanel)
EVT_SIZE(RoutingTablePanel::OnSize)
EVT_BUTTON(wxID_ANY, RoutingTablePanel::OnExportButton)
END_EVENT_TABLE()

// Define ColorMap structure similar to the GRIB plugin
struct ColorMap {
  double val;
  wxString text;
  unsigned char r;
  unsigned char g;
  unsigned char b;
};

// HTML colors taken from zygrib representation - Wind speed colors
static ColorMap WindColorMap[] = {
    {0, "#288CFF"},   // #288CFF light blue
    {3, "#00AFFF"},   // #00AFFF blue
    {6, "#00DCE1"},   // #00DCE1 cyan
    {9, "#00F7B0"},   // #00F7B0 light teal
    {12, "#00EA9C"},  // #00EA9C teal
    {15, "#82F059"},  // #82F059 light green
    {18, "#F0F503"},  // #F0F503 yellow-green
    {21, "#FFED00"},  // #FFED00 yellow
    {24, "#FFDB00"},  // #FFDB00 yellow
    {27, "#FFC700"},  // #FFC700 amber
    {30, "#FFB400"},  // #FFB400 amber-orange
    {33, "#FF9800"},  // #FF9800 orange
    {36, "#FF7E00"},  // #FF7E00 dark orange
    {39, "#F77800"},  // #F77800 orange-red
    {42, "#EC7814"},  // #EC7814 red-orange
    {45, "#E4711E"},  // #E4711E red-orange
    {48, "#E06128"},  // #E06128 reddish-orange
    {51, "#DC5132"},  // #DC5132 light red
    {54, "#D5453C"},  // #D5453C red
    {57, "#CD3A46"},  // #CD3A46 dark red
    {60, "#BE2C50"},  // #BE2C50 crimson
    {63, "#B41A5A"},  // #B41A5A magenta
    {66, "#AA1464"},  // #AA1464 magenta
    {70, "#962878"},  // #962878 purple
    {75, "#8C328C"}   // #8C328C purple
};

// HTML colors taken from zygrib representation - Air temperature colors
static ColorMap AirTempColorMap[] = {
    {0, "#283282"},   // #283282 deep blue
    {5, "#273c8c"},   // #273c8c deep blue
    {10, "#264696"},  // #264696 blue
    {14, "#2350a0"},  // #2350a0 blue
    {18, "#1f5aaa"},  // #1f5aaa blue
    {22, "#1a64b4"},  // #1a64b4 blue
    {26, "#136ec8"},  // #136ec8 blue
    {29, "#0c78e1"},  // #0c78e1 bright blue
    {32, "#0382e6"},  // #0382e6 bright blue
    {35, "#0091e6"},  // #0091e6 bright blue
    {38, "#009ee1"},  // #009ee1 light blue
    {41, "#00a6dc"},  // #00a6dc light blue
    {44, "#00b2d7"},  // #00b2d7 cyan
    {47, "#00bed2"},  // #00bed2 cyan
    {50, "#28c8c8"},  // #28c8c8 teal
    {53, "#78d2aa"},  // #78d2aa light green
    {56, "#8cdc78"},  // #8cdc78 light green
    {59, "#a0eb5f"},  // #a0eb5f lime green
    {62, "#c8f550"},  // #c8f550 yellow-green
    {65, "#f3fb02"},  // #f3fb02 yellow
    {68, "#ffed00"},  // #ffed00 yellow
    {71, "#ffdd00"},  // #ffdd00 yellow
    {74, "#ffc900"},  // #ffc900 amber
    {78, "#ffab00"},  // #ffab00 amber
    {82, "#ff8100"},  // #ff8100 orange
    {86, "#f1780c"},  // #f1780c dark orange
    {90, "#e26a23"},  // #e26a23 orange-red
    {95, "#d5453c"},  // #d5453c red
    {100, "#b53c59"}  // #b53c59 crimson
};

// Color map similar to NOAA SST - Sea temperature colors
// https://www.ospo.noaa.gov/data/sst/contour/global.cf.gif
static ColorMap SeaTempColorMap[] = {
    {-2, "#cc04ae"},  // #cc04ae magenta
    {2, "#8f06e4"},   // #8f06e4 purple
    {6, "#486afa"},   // #486afa blue
    {10, "#00ffff"},  // #00ffff cyan
    {15, "#00d54b"},  // #00d54b green
    {19, "#59d800"},  // #59d800 green
    {23, "#f2fc00"},  // #f2fc00 yellow
    {27, "#ff1500"},  // #ff1500 orange-red
    {32, "#ff0000"},  // #ff0000 red
    {36, "#d80000"},  // #d80000 dark red
    {40, "#a90000"},  // #a90000 dark red
    {44, "#870000"},  // #870000 very dark red
    {48, "#690000"},  // #690000 very dark red
    {52, "#550000"},  // #550000 maroon
    {56, "#330000"}   // #330000 near black
};

// HTML colors taken from ZyGrib representation - Precipitation colors
static ColorMap PrecipitationColorMap[] = {
    {0, "#ffffff"},    // #ffffff white
    {.01, "#c8f0ff"},  // #c8f0ff very light blue
    {.02, "#b4e6ff"},  // #b4e6ff very light blue
    {.05, "#8cd3ff"},  // #8cd3ff light blue
    {.07, "#78caff"},  // #78caff light blue
    {.1, "#6ec1ff"},   // #6ec1ff light blue
    {.2, "#64b8ff"},   // #64b8ff light blue
    {.5, "#50a6ff"},   // #50a6ff blue
    {.7, "#469eff"},   // #469eff blue
    {1.0, "#3c96ff"},  // #3c96ff blue
    {2.0, "#328eff"},  // #328eff blue
    {5.0, "#1e7eff"},  // #1e7eff blue
    {7.0, "#1476f0"},  // #1476f0 blue
    {10, "#0a6edc"},   // #0a6edc dark blue
    {20, "#0064c8"},   // #0064c8 dark blue
    {50, "#0052aa"}    // #0052aa dark blue
};

// HTML colors taken from ZyGrib representation - Cloud cover colors
static ColorMap CloudColorMap[] = {
    {0, "#ffffff"},   // #ffffff white
    {1, "#f0f0e6"},   // #f0f0e6 off-white
    {10, "#e6e6dc"},  // #e6e6dc very light gray
    {20, "#dcdcd2"},  // #dcdcd2 light gray
    {30, "#c8c8b4"},  // #c8c8b4 light gray
    {40, "#aaaa8c"},  // #aaaa8c medium gray
    {50, "#969678"},  // #969678 medium gray
    {60, "#787864"},  // #787864 dark gray
    {70, "#646450"},  // #646450 dark gray
    {80, "#5a5a46"},  // #5a5a46 very dark gray
    {90, "#505036"}   // #505036 very dark gray
};

// Air pressure colors - from low (violet/purple) to high (red)
static ColorMap PressureColorMap[] = {
    {940, "#9B59B6"},   // #9B59B6 violet - extremely low pressure (hurricane)
    {960, "#8E44AD"},   // #8E44AD purple - very low pressure (strong storm)
    {980, "#3498DB"},   // #3498DB blue - low pressure (storm)
    {990, "#5DADE2"},   // #5DADE2 light blue - moderately low pressure
    {1000, "#85C1E9"},  // #85C1E9 very light blue - slightly low pressure
    {1010, "#7DCEA0"},  // #7DCEA0 light green - normal pressure
    {1013, "#82E0AA"},  // #82E0AA green - standard atmospheric pressure
    {1020, "#F7DC6F"},  // #F7DC6F yellow - slightly high pressure
    {1030, "#F8C471"},  // #F8C471 light orange - moderately high pressure
    {1040, "#E67E22"},  // #E67E22 orange - high pressure
    {1050, "#E74C3C"},  // #E74C3C red - very high pressure
    {1060, "#C0392B"}   // #C0392B dark red - extremely high pressure
};

// REFC (Reflectivity) colors
static ColorMap REFCColorMap[] = {
    {0, "#ffffff"},   // #ffffff white
    {5, "#06E8E4"},   // #06E8E4 cyan
    {10, "#009BE9"},  // #009BE9 light blue
    {15, "#0400F3"},  // #0400F3 blue
    {20, "#00F924"},  // #00F924 green
    {25, "#06C200"},  // #06C200 dark green
    {30, "#009100"},  // #009100 dark green
    {35, "#FAFB00"},  // #FAFB00 yellow
    {40, "#EBB608"},  // #EBB608 amber
    {45, "#FF9400"},  // #FF9400 orange
    {50, "#FD0002"},  // #FD0002 red
    {55, "#D70000"},  // #D70000 dark red
    {60, "#C20300"},  // #C20300 dark red
    {65, "#F900FE"},  // #F900FE magenta
    {70, "#945AC8"}   // #945AC8 purple
};

// CAPE (Convective Available Potential Energy) colors
static ColorMap CAPEColorMap[] = {
    {0, "#0046c8"},     // #0046c8 blue
    {5, "#0050f0"},     // #0050f0 blue
    {10, "#005aff"},    // #005aff blue
    {15, "#0069ff"},    // #0069ff blue
    {20, "#0078ff"},    // #0078ff light blue
    {30, "#000cff"},    // #000cff blue
    {45, "#00a1ff"},    // #00a1ff light blue
    {60, "#00b6fa"},    // #00b6fa light blue
    {100, "#00c9ee"},   // #00c9ee cyan
    {150, "#00e0da"},   // #00e0da cyan
    {200, "#00e6b4"},   // #00e6b4 teal
    {300, "#82e678"},   // #82e678 light green
    {500, "#9bff3b"},   // #9bff3b green
    {700, "#ffdc00"},   // #ffdc00 yellow
    {1000, "#ffb700"},  // #ffb700 amber
    {1500, "#f37800"},  // #f37800 orange
    {2000, "#d4440c"},  // #d4440c red-orange
    {2500, "#c8201c"},  // #c8201c red
    {3000, "#ad0430"}   // #ad0430 dark red
};

// Wave height colors - from calm (blue) to extreme (purple)
static ColorMap WaveHeightColorMap[] = {
    {0.0, "#00BFFF"},   // #00BFFF deep sky blue - calm
    {0.5, "#89CFF0"},   // #89CFF0 baby blue - very small waves
    {1.0, "#0080FF"},   // #0080FF azure blue - small waves
    {1.5, "#0070E8"},   // #0070E8 blue - moderate waves
    {2.0, "#0066CC"},   // #0066CC medium blue - moderate waves
    {2.5, "#32CD32"},   // #32CD32 lime green - moderate waves
    {3.0, "#00FF00"},   // #00FF00 green - moderate to rough
    {4.0, "#ADFF2F"},   // #ADFF2F green yellow - rough
    {5.0, "#FFFF00"},   // #FFFF00 yellow - rough to very rough
    {6.0, "#FFD700"},   // #FFD700 gold - very rough
    {7.0, "#FFA500"},   // #FFA500 orange - high seas
    {8.0, "#FF8C00"},   // #FF8C00 dark orange - high seas
    {9.0, "#FF4500"},   // #FF4500 orange red - very high seas
    {10.0, "#FF0000"},  // #FF0000 red - very high seas
    {12.0, "#DC143C"},  // #DC143C crimson - phenomenal seas
    {14.0, "#B22222"},  // #B22222 firebrick - phenomenal seas
    {16.0, "#9400D3"},  // #9400D3 dark violet - extreme
    {18.0, "#800080"},  // #800080 purple - extreme
    {20.0, "#4B0082"}   // #4B0082 indigo - extreme
};

static bool colorsInitialized = false;

// Helper function to initialize a specific color map
static void InitializeColorMap(ColorMap* map, size_t mapSize) {
  wxColour c;
  for (size_t i = 0; i < mapSize; i++) {
    c.Set(map[i].text);
    map[i].r = c.Red();
    map[i].g = c.Green();
    map[i].b = c.Blue();
  }
}

// Function to initialize the color values in the maps
static void InitColors() {
  if (colorsInitialized) return;

  // Initialize all color maps using the helper function
  InitializeColorMap(WindColorMap,
                     sizeof(WindColorMap) / sizeof(*WindColorMap));
  InitializeColorMap(AirTempColorMap,
                     sizeof(AirTempColorMap) / sizeof(*AirTempColorMap));
  InitializeColorMap(SeaTempColorMap,
                     sizeof(SeaTempColorMap) / sizeof(*SeaTempColorMap));
  InitializeColorMap(PrecipitationColorMap, sizeof(PrecipitationColorMap) /
                                                sizeof(*PrecipitationColorMap));
  InitializeColorMap(CloudColorMap,
                     sizeof(CloudColorMap) / sizeof(*CloudColorMap));
  InitializeColorMap(REFCColorMap,
                     sizeof(REFCColorMap) / sizeof(*REFCColorMap));
  InitializeColorMap(CAPEColorMap,
                     sizeof(CAPEColorMap) / sizeof(*CAPEColorMap));
  InitializeColorMap(WaveHeightColorMap,
                     sizeof(WaveHeightColorMap) / sizeof(*WaveHeightColorMap));
  InitializeColorMap(PressureColorMap,
                     sizeof(PressureColorMap) / sizeof(*PressureColorMap));
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
// brightness.
// ITU-R Recommendation BT.709
// W3C Web Content Accessibility Guidelines (WCAG) 2.1
static wxColour GetTextColorForBackground(const wxColour& bgColor) {
  // Convert to linear RGB first (gamma correction)
  auto toLinear = [](double c) {
    c /= 255.0;
    return c <= 0.03928 ? c / 12.92 : std::pow((c + 0.055) / 1.055, 2.4);
  };

  double r = toLinear(bgColor.Red());
  double g = toLinear(bgColor.Green());
  double b = toLinear(bgColor.Blue());

  // Calculate relative luminance using sRGB coefficients
  double luminance = 0.2126 * r + 0.7152 * g + 0.0722 * b;

  // Threshold around 0.5 works well for linear luminance
  return luminance > 0.5 ? *wxBLACK : *wxWHITE;
}

static wxColor GetWindSourceColor(DataMask mask) {
  if (!colorsInitialized) {
    InitColors();
  }
  if (mask == DataMask::DATA_DEFICIENT_WIND) {
    return wxColor(255, 128, 0);  // Orange for data deficient
  } else if (mask == DataMask::GRIB_WIND) {
    return wxColor(0, 255, 0);  // Green for GRIB data
  } else if (mask == DataMask::CLIMATOLOGY_WIND) {
    return wxColor(0, 0, 255);  // Blue for climatology data
  }
  return wxColor(255, 255, 255);  // Default to white for other cases
}

static wxColor GetCurrentSourceColor(DataMask mask) {
  if (!colorsInitialized) {
    InitColors();
  }
  if (mask == DataMask::DATA_DEFICIENT_CURRENT) {
    return wxColor(255, 128, 0);  // Orange for data deficient
  } else if (mask == DataMask::GRIB_CURRENT) {
    return wxColor(0, 255, 0);  // Green for GRIB data
  } else if (mask == DataMask::CLIMATOLOGY_CURRENT) {
    return wxColor(0, 0, 255);  // Blue for climatology data
  }
  return wxColor(255, 255, 255);  // Default to white for other cases
}

// Function to get wind speed color
static wxColor GetWindSpeedColor(double knots) {
  if (!colorsInitialized) {
    InitColors();
  }

  size_t maplen = sizeof(WindColorMap) / sizeof(*WindColorMap);
  return GetColorFromMap(WindColorMap, maplen, knots);
}

// Function to get air temperature color
static wxColor GetAirTempColor(double celsius) {
  if (!colorsInitialized) {
    InitColors();
  }

  size_t maplen = sizeof(AirTempColorMap) / sizeof(*AirTempColorMap);
  return GetColorFromMap(AirTempColorMap, maplen, celsius);
}

// Function to get sea temperature color
static wxColor GetSeaTempColor(double celsius) {
  if (!colorsInitialized) {
    InitColors();
  }

  size_t maplen = sizeof(SeaTempColorMap) / sizeof(*SeaTempColorMap);
  return GetColorFromMap(SeaTempColorMap, maplen, celsius);
}

// Function to get precipitation color
static wxColor GetPrecipitationColor(double mmPerHour) {
  if (!colorsInitialized) {
    InitColors();
  }

  size_t maplen =
      sizeof(PrecipitationColorMap) / sizeof(*PrecipitationColorMap);
  return GetColorFromMap(PrecipitationColorMap, maplen, mmPerHour);
}

// Function to get cloud cover color
static wxColor GetCloudColor(double percentage) {
  if (!colorsInitialized) {
    InitColors();
  }

  size_t maplen = sizeof(CloudColorMap) / sizeof(*CloudColorMap);
  return GetColorFromMap(CloudColorMap, maplen, percentage);
}

// Function to get CAPE color
static wxColor GetCAPEColor(double joulePerKg) {
  if (!colorsInitialized) {
    InitColors();
  }

  size_t maplen = sizeof(CAPEColorMap) / sizeof(*CAPEColorMap);
  return GetColorFromMap(CAPEColorMap, maplen, joulePerKg);
}

// Function to get reflectivity color
static wxColor GetReflectivityColor(double dbZ) {
  if (!colorsInitialized) {
    InitColors();
  }

  size_t maplen = sizeof(REFCColorMap) / sizeof(*REFCColorMap);
  return GetColorFromMap(REFCColorMap, maplen, dbZ);
}

// Function to get wave height color
static wxColor GetWaveHeightColor(double meters) {
  if (!colorsInitialized) {
    InitColors();
  }

  size_t maplen = sizeof(WaveHeightColorMap) / sizeof(*WaveHeightColorMap);
  return GetColorFromMap(WaveHeightColorMap, maplen, meters);
}

// Function to get air pressure color
static wxColor GetPressureColor(double hPa) {
  if (!colorsInitialized) {
    InitColors();
  }

  size_t maplen = sizeof(PressureColorMap) / sizeof(*PressureColorMap);
  return GetColorFromMap(PressureColorMap, maplen, hPa);
}

// Function to get wave relative direction color based on tactical significance
static wxColor GetWaveRelativeColor(double waveRelAngle) {
  // Normalize angle to 0-360 range for consistent coloring
  while (waveRelAngle < 0) waveRelAngle += 360;
  while (waveRelAngle >= 360) waveRelAngle -= 360;

  // Color coding based on wave direction relative to boat heading:
  // Green: Following seas (135-225°) - favorable
  // Yellow: Beam seas (45-135° & 225-315°) - moderate comfort
  // Red: Head seas (315-45°) - challenging

  if ((waveRelAngle >= 135 && waveRelAngle <= 225)) {
    // Following seas - green (favorable)
    return wxColour(0, 200, 0);
  } else if ((waveRelAngle >= 45 && waveRelAngle < 135) ||
             (waveRelAngle > 225 && waveRelAngle <= 315)) {
    // Beam seas - yellow (moderate)
    return wxColour(255, 200, 0);
  } else {
    // Head seas - red (challenging)
    return wxColour(255, 100, 100);
  }
}

// Helper function to format distance values according to user preferences
static wxString FormatDistance(double nm_distance) {
  double value = toUsrDistance_Plugin(nm_distance);
  wxString unit = getUsrDistanceUnit_Plugin();
  return wxString::Format("%.1f %s", value, unit);
}

// Helper function to format speed values according to user preferences
static wxString FormatSpeed(double kts_speed) {
  double value = toUsrSpeed_Plugin(kts_speed);
  wxString unit = getUsrSpeedUnit_Plugin();
  return wxString::Format("%.1f %s", value, unit);
}

// Helper function to format temperature values according to user preferences
// Convert from Kelvin (GRIB standard) to Celsius before applying user
// preference conversion
static wxString FormatTemperature(double kelvin_temp) {
  // Convert from Kelvin to Celsius first
  double celsius = kelvin_temp - 273.15;
  // Then convert to user's preferred unit (C or F)
  double value = toUsrTemp_Plugin(celsius);
  wxString unit = getUsrTempUnit_Plugin();
  return wxString::Format("%.1f%s", value, unit);
}

// Helper function to format pressure values from Pascals to hectopascals
static wxString FormatPressure(double pascal_pressure) {
  // Convert from Pascals to hectopascals (hPa)
  double hPa = pascal_pressure / 100.0;
  return wxString::Format("%.1f hPa", hPa);
}

// Helper function to calculate current angle relative to COG
static double CalculateCurrentAngle(double currentDir, double cog) {
  // Calculate the relative angle between current and COG
  double angle = heading_resolve(currentDir - cog);
  return angle;
}

// Helper function to get current effect color based on current angle
static wxColor GetCurrentEffectColor(double currentAngle, double currentSpeed) {
  // Green for aiding currents, red for hindering currents
  // Intensity based on current speed and cosine of angle

  // Calculate effect intensity (0-1) based on cosine of angle
  double effect = cos(currentAngle * M_PI / 180.0);

  // Normalize effect by current speed (higher speed = stronger effect)
  double intensity =
      fabs(effect) *
      std::min(1.0, currentSpeed / 3.0);  // Cap at 3 knots for full intensity

  if (effect > 0) {
    // Aiding current - green with intensity scaling
    int green = 128 + (int)(127 * intensity);
    return wxColour(0, green, 0);
  } else {
    // Hindering current - red with intensity scaling
    int red = 128 + (int)(127 * intensity);
    return wxColour(red, 0, 0);
  }
}

// Helper function to get time-of-day color based on sun elevation angle
// Color scheme follows nautical standards for visibility and navigation
// conditions:
// - High sun (>15°): Bright yellow - excellent visibility
// - Civil twilight (0° to 6°): Light colors - good navigation conditions
// - Nautical twilight (-6° to 0°): Moderate colors - horizon visible
// - Astronomical twilight (-12° to -6°): Darker colors - star navigation
// - Deep night (<-18°): Dark blue - full night conditions
static wxColor GetTimeOfDayColor(const wxDateTime& dateTime, double lat,
                                 double lon) {
  // Use the enhanced CalculateSun function with precise sun elevation
  // calculation
  wxDateTime sunrise, sunset;
  double sunElevation;
  SunCalculator::CalculateSun(lat, lon, dateTime.GetDayOfYear(), sunrise,
                              sunset, &dateTime, &sunElevation);

  // Define color transitions based on sun elevation angles (following nautical
  // standards)
  if (sunElevation > 15.0) {
    // High sun - excellent visibility for coral reefs, navigation, harbor entry
    return wxColor(255, 255, 180);  // Bright yellow - high visibility
  } else if (sunElevation > 6.0) {
    // Civil twilight begins - sun visible, good general navigation
    return wxColor(255, 248, 220);  // Light cream - good visibility
  } else if (sunElevation > 0.0) {
    // Civil twilight - some natural light, horizon clearly visible
    return wxColor(255, 228, 181);  // Soft peach - moderate visibility
  } else if (sunElevation > -6.0) {
    // Nautical twilight - horizon no longer visible, but navigation still
    // possible
    return wxColor(205, 181, 205);  // Light lavender - twilight navigation
  } else if (sunElevation > -12.0) {
    // Astronomical twilight - quite dark, celestial navigation
    return wxColor(147, 112, 147);  // Medium purple - star navigation
  } else if (sunElevation > -18.0) {
    // End of astronomical twilight - very dark but some astronomical objects
    // visible
    return wxColor(72, 61, 139);  // Dark slate blue - deep twilight
  } else {
    // Complete night - sun more than 18 degrees below horizon
    return wxColor(25, 25, 112);  // Midnight blue - full night
  }
}

RoutingTablePanel::RoutingTablePanel(wxWindow* parent,
                                     WeatherRouting& weatherRouting,
                                     RouteMapOverlay* routemap)
    : wxPanel(parent, wxID_ANY, wxDefaultPosition, wxDefaultSize,
              wxBORDER_NONE),
      m_RouteMap(routemap),
      m_WeatherRouting(weatherRouting),
      m_highlightedRow(-1) {
  // Set the panel background color immediately
  SetBackgroundColour(wxSystemSettings::GetColour(wxSYS_COLOUR_BTNFACE));

  // Create a sizer for the panel
  m_mainSizer = new wxBoxSizer(wxVERTICAL);

  // Create notebook with tabs.
  m_notebook = new wxNotebook(this, wxID_ANY, wxDefaultPosition, wxDefaultSize,
                              wxNB_TOP | wxNB_FIXEDWIDTH);

  // Create summary tab
  CreateSummaryTab();

  // Create table tab
  m_tableTab = new wxPanel(m_notebook, wxID_ANY);
  wxBoxSizer* tableSizer = new wxBoxSizer(wxVERTICAL);

  // Create the grid with the columns we need
  m_gridWeatherTable =
      new wxGrid(m_tableTab, wxID_ANY, wxDefaultPosition, wxDefaultSize);
  m_gridWeatherTable->CreateGrid(0, COL_COUNT);

  // Set column labels
  m_gridWeatherTable->SetColLabelValue(COL_LEG_NUMBER, _("Leg #"));
  m_gridWeatherTable->SetColLabelValue(COL_ETA, _("ETA"));
  m_gridWeatherTable->SetColLabelValue(COL_ENROUTE, _("Total Time"));
  m_gridWeatherTable->SetColLabelValue(COL_LEG_DISTANCE, _("Distance"));
  m_gridWeatherTable->SetColLabelValue(COL_SOG, _("SOG"));
  m_gridWeatherTable->SetColLabelValue(COL_COG, _("COG"));
  m_gridWeatherTable->SetColLabelValue(COL_STW, _("STW"));
  m_gridWeatherTable->SetColLabelValue(COL_CTW, _("CTW"));
  m_gridWeatherTable->SetColLabelValue(COL_HDG, _("HDG"));

  m_gridWeatherTable->SetColLabelValue(COL_WIND_SOURCE, _("Wind Source"));
  m_gridWeatherTable->SetColLabelValue(COL_AWS, _("AWS"));
  m_gridWeatherTable->SetColLabelValue(COL_TWS, _("TWS"));
  m_gridWeatherTable->SetColLabelValue(COL_WIND_GUST, _("Wind Gust"));
  m_gridWeatherTable->SetColLabelValue(COL_TWD, _("TWD"));
  m_gridWeatherTable->SetColLabelValue(COL_TWA, _("TWA"));
  m_gridWeatherTable->SetColLabelValue(COL_AWA, _("AWA"));
  m_gridWeatherTable->SetColLabelValue(COL_WAVE_HEIGHT, _("Wave Height"));
  m_gridWeatherTable->SetColLabelValue(COL_WAVE_DIRECTION, _("Wave Dir"));
  m_gridWeatherTable->SetColLabelValue(COL_WAVE_REL, _("Wave Rel"));
  m_gridWeatherTable->SetColLabelValue(COL_WAVE_PERIOD, _("Wave Period"));
  m_gridWeatherTable->SetColLabelValue(COL_SAIL_PLAN, _("Sail Plan"));
  m_gridWeatherTable->SetColLabelValue(COL_COMFORT, _("Comfort"));
  m_gridWeatherTable->SetColLabelValue(COL_RAIN, _("Rain"));
  m_gridWeatherTable->SetColLabelValue(COL_CLOUD, _("Cloud Cover"));
  m_gridWeatherTable->SetColLabelValue(COL_AIR_TEMP, _("Air Temp"));
  m_gridWeatherTable->SetColLabelValue(COL_SEA_TEMP, _("Sea Temp"));
  m_gridWeatherTable->SetColLabelValue(COL_REL_HUMIDITY, _("Humidity"));
  m_gridWeatherTable->SetColLabelValue(COL_AIR_PRESSURE, _("Pressure"));
  m_gridWeatherTable->SetColLabelValue(COL_CAPE, _("CAPE"));
  m_gridWeatherTable->SetColLabelValue(COL_REFLECTIVITY, _("REFC"));
  m_gridWeatherTable->SetColLabelValue(COL_CURRENT_SOURCE, _("Curr Source"));
  m_gridWeatherTable->SetColLabelValue(COL_CURRENT_SPEED, _("Curr Speed"));
  m_gridWeatherTable->SetColLabelValue(COL_CURRENT_DIR, _("Curr Dir"));
  m_gridWeatherTable->SetColLabelValue(COL_CURRENT_ANGLE, _("Curr Angle"));

  // Auto size all columns initially
  for (int i = 0; i < COL_COUNT; i++) {
    m_gridWeatherTable->AutoSizeColumn(i);
  }

  // Add components to sizer
  tableSizer->Add(m_gridWeatherTable, 1, wxEXPAND | wxALL, 5);
  m_tableTab->SetSizer(tableSizer);

  // Add tabs to notebook with explicit sizing
  m_notebook->AddPage(m_summaryTab, _("Summary"), true);
  m_notebook->AddPage(m_tableTab, _("Detailed Table"), false);

  // Force notebook to calculate proper tab sizes
  m_notebook->SetSelection(1);  // Start with 'Detailed Table' tab active
  m_notebook->InvalidateBestSize();

  // Add notebook to main sizer with proper proportions
  m_mainSizer->Add(m_notebook, 1, wxEXPAND | wxALL, 2);

  SetSizer(m_mainSizer);
  m_mainSizer->Fit(this);

  // Force layout update to ensure tabs are visible
  Layout();
  m_notebook->Layout();

  // Populate the table with data from the route
  PopulateTable();

  // Apply the current color scheme to eliminate any black areas
  SetColorScheme(PI_GLOBAL_COLOR_SCHEME_DAY);  // Default to day scheme
}

RoutingTablePanel::~RoutingTablePanel() {}

void RoutingTablePanel::OnClose(wxCommandEvent& event) {
  // Hide parent Aui pane rather than destroying the dialog
  GetParent()->Hide();
}

void RoutingTablePanel::OnSize(wxSizeEvent& event) {
  event.Skip();
  // Let the sizer handle the layout automatically
  Layout();
}

void RoutingTablePanel::SetColorScheme(PI_ColorScheme cs) {
  m_colorscheme = cs;

  // Apply color scheme to panel background
  wxColour panelBackColor;
  wxColour cellBackColor;
  wxColour textColor;

  switch (cs) {
    case PI_GLOBAL_COLOR_SCHEME_DAY:
      panelBackColor = wxColour(212, 208, 200);
      cellBackColor = *wxWHITE;
      textColor = *wxBLACK;
      break;
    case PI_GLOBAL_COLOR_SCHEME_DUSK:
      panelBackColor = wxColour(128, 128, 128);
      cellBackColor = wxColour(240, 240, 240);
      textColor = *wxBLACK;
      break;
    case PI_GLOBAL_COLOR_SCHEME_NIGHT:
      panelBackColor = wxColour(64, 64, 64);
      cellBackColor = wxColour(48, 48, 48);
      textColor = *wxWHITE;
      break;
    default:
      panelBackColor = wxColour(212, 208, 200);
      cellBackColor = *wxWHITE;
      textColor = *wxBLACK;
  }

  SetBackgroundColour(panelBackColor);
  m_gridWeatherTable->SetDefaultCellBackgroundColour(cellBackColor);
  m_gridWeatherTable->SetDefaultCellTextColour(textColor);

  // Force a complete refresh
  Refresh(true);  // true = erase background
  Update();
}

// Helper function to set cell value with colored background
void RoutingTablePanel::setCellWithColor(int row, int col,
                                         const wxString& value,
                                         const wxColor& bgColor) {
  m_gridWeatherTable->SetCellValue(row, col, value);
  wxGridCellAttr* attr = new wxGridCellAttr();
  attr->SetBackgroundColour(bgColor);
  attr->SetTextColour(GetTextColorForBackground(bgColor));
  m_gridWeatherTable->SetAttr(row, col, attr);
}

// Helper function to set cell value with time-of-day background color
void RoutingTablePanel::setCellWithTimeOfDayColor(int row, int col,
                                                  const wxString& value,
                                                  const wxDateTime& dateTime,
                                                  double lat, double lon) {
  wxColor timeColor = GetTimeOfDayColor(dateTime, lat, lon);
  setCellWithColor(row, col, value, timeColor);
}

// Helper function to format and display sail plan information
void RoutingTablePanel::handleSailPlanCell(
    int row, const PlotData& data, const RouteMapConfiguration& configuration,
    const PlotData* prevData) {
  wxString sailPlanName;

  if (data.polar >= 0 && data.polar < (int)configuration.boat.Polars.size()) {
    // Display the polar/sail plan name from the FileName field
    sailPlanName = configuration.boat.Polars[data.polar].FileName;
    // Extract just the filename without path and extension
    sailPlanName = wxFileNameFromPath(sailPlanName);
    // Remove extension if present
    int pos = sailPlanName.Find('.');
    if (pos != wxNOT_FOUND) sailPlanName = sailPlanName.Left(pos);

    // Apply coloring to identify new sail plan changes (not cumulative)
    if (prevData && prevData->polar != data.polar) {
      // Light amber color for sail changes
      setCellWithColor(row, COL_SAIL_PLAN, sailPlanName,
                       wxColour(255, 230, 160));
    } else {
      m_gridWeatherTable->SetCellValue(row, COL_SAIL_PLAN, sailPlanName);
    }
  } else {
    m_gridWeatherTable->SetCellValue(row, COL_SAIL_PLAN, _("Unknown"));
  }
}

void RoutingTablePanel::PopulateTable() {
  // Get plot data from the route
  std::list<PlotData> plotData = m_RouteMap->GetPlotData(false);
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
  wxDateTime startTime;  // Starting time for cumulative duration
  wxDateTime prevTime;   // Previous point's time for leg duration calculation
  PlotData prevData;     // For calculating leg distances
  bool firstPoint = true;
  double cumulativeDistance = 0.0;  // Track total distance

  for (const PlotData& data : plotData) {
    // Populate the leg number column with time-of-day background color
    setCellWithTimeOfDayColor(row, COL_LEG_NUMBER,
                              wxString::Format("%d", row + 1), data.time,
                              data.lat, data.lon);

    // ETA column - actual date/time of arrival at this point
    // Check for API 1.20 which has toUsrDateTimeFormat_Plugin function
    wxString timeString;
#if OCPN_API_VERSION_MAJOR > 1 || \
    (OCPN_API_VERSION_MAJOR == 1 && OCPN_API_VERSION_MINOR >= 20)
    timeString = toUsrDateTimeFormat_Plugin(data.time);
#else
    // Fallback for earlier API versions - format time with proper timezone
    // label
    wxDateTime displayTime = data.time;
    if (useLocalTime) {
      displayTime = data.time.FromUTC();
    }
    if (useLocalTime) {
      timeString = displayTime.Format("%Y-%m-%d %H:%M %Z");
    } else {
      timeString = displayTime.Format("%Y-%m-%d %H:%M UTC");
    }
#endif
    // ETA column with time-of-day background color
    setCellWithTimeOfDayColor(row, COL_ETA, timeString, data.time, data.lat,
                              data.lon);

    // Store the first point's time to calculate total time from start
    if (firstPoint) {
      startTime = data.time;
      // Total Time column with time-of-day background color
      setCellWithTimeOfDayColor(row, COL_ENROUTE, _("Start"), data.time,
                                data.lat, data.lon);
      m_gridWeatherTable->SetCellValue(row, COL_LEG_DISTANCE, _("0.0"));
      firstPoint = false;
    } else {
      // Calculate leg distance between current and previous point
      double legDistance = DistGreatCircle_Plugin(prevData.lat, prevData.lon,
                                                  data.lat, data.lon);
      cumulativeDistance += legDistance;  // Add to total distance

      // Calculate cumulative time from start
      wxTimeSpan totalDuration = data.time.Subtract(startTime);
      int days = totalDuration.GetDays();
      wxString totalDurationString;

      if (days > 0) {
        totalDurationString = wxString::Format("%dd %02d:%02d", days,
                                               totalDuration.GetHours() % 24,
                                               totalDuration.GetMinutes() % 60);
      } else {
        totalDurationString =
            wxString::Format("%02d:%02d", totalDuration.GetHours(),
                             totalDuration.GetMinutes() % 60);
      }

      // Set the cumulative duration in EnRoute column with time-of-day
      // background color
      setCellWithTimeOfDayColor(row, COL_ENROUTE, totalDurationString,
                                data.time, data.lat, data.lon);

      // Set the cumulative distance in Distance column using unit conversion
      m_gridWeatherTable->SetCellValue(row, COL_LEG_DISTANCE,
                                       FormatDistance(cumulativeDistance));
    }

    if (data.data_mask != DataMask::NONE) {
      // Wind
      wxString windSource;
      if (data.data_mask & DataMask::GRIB_WIND) {
        windSource = _("GRIB");
      } else if (data.data_mask & DataMask::CLIMATOLOGY_WIND) {
        windSource = _("Climatology");
      } else {
        windSource = wxEmptyString;
      }
      wxColor windSourceColor = GetWindSourceColor(data.data_mask);
      setCellWithColor(row, COL_WIND_SOURCE, windSource, windSourceColor);
      wxGridCellAttr* attr = new wxGridCellAttr();
      attr->SetTextColour(GetTextColorForBackground(windSourceColor));
      m_gridWeatherTable->SetAttr(row, COL_WIND_SOURCE, attr);

      // Current
      wxString currentSource;
      if (data.data_mask & DataMask::GRIB_CURRENT) {
        currentSource = _("GRIB");
      } else if (data.data_mask & DataMask::CLIMATOLOGY_CURRENT) {
        currentSource = _("Climatology");
      } else {
        currentSource = wxEmptyString;
      }
      wxColor currSourceColor = GetCurrentSourceColor(data.data_mask);
      setCellWithColor(row, COL_CURRENT_SOURCE, currentSource, currSourceColor);
      attr = new wxGridCellAttr();
      attr->SetTextColour(GetTextColorForBackground(currSourceColor));
      m_gridWeatherTable->SetAttr(row, COL_CURRENT_SOURCE, attr);
    }

    // Speeds and directions - check for NaN values
    if (!std::isnan(data.sog)) {
      m_gridWeatherTable->SetCellValue(row, COL_SOG, FormatSpeed(data.sog));
    }

    if (!std::isnan(data.cog)) {
      m_gridWeatherTable->SetCellValue(
          row, COL_COG,
          wxString::Format("%.0f\u00B0", positive_degrees(data.cog)));
    }

    if (!std::isnan(data.stw)) {
      // Check if vessel is motoring and color-code the STW cell accordingly
      if (data.data_mask & DataMask::MOTOR_USED) {
        // Use a distinct motor color (light blue) to indicate motoring
        setCellWithColor(row, COL_STW, FormatSpeed(data.stw),
                         wxColour(173, 216, 230));  // Light blue for motor

        // Add visual indicator to make motoring more obvious
        wxString motorValue =
            FormatSpeed(data.stw) + " (M)";  // Add (M) for Motor
        m_gridWeatherTable->SetCellValue(row, COL_STW, motorValue);

        // Set cell attribute with background color (setCellWithColor already
        // does this, but we need to override the value)
        wxGridCellAttr* attr = new wxGridCellAttr();
        attr->SetBackgroundColour(wxColour(173, 216, 230));
        attr->SetTextColour(GetTextColorForBackground(wxColour(173, 216, 230)));
        m_gridWeatherTable->SetAttr(row, COL_STW, attr);
      } else {
        // Standard STW display for sailing
        m_gridWeatherTable->SetCellValue(row, COL_STW, FormatSpeed(data.stw));
      }
    }

    if (!std::isnan(data.ctw)) {
      m_gridWeatherTable->SetCellValue(
          row, COL_CTW,
          wxString::Format("%.0f\u00B0", positive_degrees(data.ctw)));
    }

    if (!std::isnan(data.hdg)) {
      m_gridWeatherTable->SetCellValue(
          row, COL_HDG,
          wxString::Format("%.0f\u00B0", positive_degrees(data.hdg)));
    }

    // Wind data
    if (!std::isnan(data.stw) && !std::isnan(data.twdOverWater) &&
        !std::isnan(data.twsOverWater)) {
      double apparentWindSpeed = Polar::VelocityApparentWind(
          data.stw, data.twdOverWater - data.ctw, data.twsOverWater);
      double apparentWindAngle = Polar::DirectionApparentWind(
          apparentWindSpeed, data.stw, data.twdOverWater - data.ctw,
          data.twsOverWater);

      if (!std::isnan(apparentWindSpeed)) {
        // Apply coloring to AWS cell based on apparent wind speed
        wxColor awsColor = GetWindSpeedColor(apparentWindSpeed);
        setCellWithColor(row, COL_AWS, FormatSpeed(apparentWindSpeed),
                         awsColor);
        wxGridCellAttr* attr = new wxGridCellAttr();
        attr->SetTextColour(GetTextColorForBackground(awsColor));
        m_gridWeatherTable->SetAttr(row, COL_AWS, attr);
      }

      if (!std::isnan(apparentWindAngle)) {
        // Color the AWA cell: green for starboard tack, red for port tack
        bool isStarboardTack =
            (apparentWindAngle > 0 && apparentWindAngle < 180);
        wxColor awaColor =
            isStarboardTack ? wxColour(0, 255, 0) : wxColour(255, 0, 0);
        setCellWithColor(row, COL_AWA,
                         wxString::Format("%.0f\u00B0", apparentWindAngle),
                         awaColor);
        wxGridCellAttr* attr = new wxGridCellAttr();
        attr->SetTextColour(GetTextColorForBackground(awaColor));
        m_gridWeatherTable->SetAttr(row, COL_AWA, attr);
      }
    }

    if (!std::isnan(data.twdOverWater)) {
      m_gridWeatherTable->SetCellValue(
          row, COL_TWD,
          wxString::Format("%.0f\u00B0", positive_degrees(data.twdOverWater)));

      // Calculate true wind angle relative to boat course
      if (!std::isnan(data.ctw)) {
        double twa = heading_resolve(data.twdOverWater - data.ctw);
        bool isStarboardTack = (twa > 0 && twa < 180);
        if (twa > 180) twa = 360 - twa;

        // Color the TWA cell: green for starboard tack, red for port tack
        wxColor twaColor =
            isStarboardTack ? wxColour(0, 255, 0) : wxColour(255, 0, 0);

        setCellWithColor(row, COL_TWA, wxString::Format("%.0f\u00B0", twa),
                         twaColor);
      }
    }

    // Set the TWS value and apply color based on wind speed
    if (!std::isnan(data.twsOverWater)) {
      setCellWithColor(row, COL_TWS, FormatSpeed(data.twsOverWater),
                       GetWindSpeedColor(data.twsOverWater));
    }

    handleSailPlanCell(row, data, configuration, row > 0 ? &prevData : nullptr);

    if (!std::isnan(data.VW_GUST)) {
      setCellWithColor(row, COL_WIND_GUST, FormatSpeed(data.VW_GUST),
                       GetWindSpeedColor(data.VW_GUST));
    }

    // Cloud cover (range 0-100%)
    if (!std::isnan(data.cloud_cover)) {
      setCellWithColor(row, COL_CLOUD,
                       wxString::Format("%.1f%%", data.cloud_cover),
                       GetCloudColor(data.cloud_cover));
    }

    // Rain (mm/h)
    if (!std::isnan(data.rain_mm_per_hour)) {
      setCellWithColor(row, COL_RAIN,
                       wxString::Format("%.2f mm/h", data.rain_mm_per_hour),
                       GetPrecipitationColor(data.rain_mm_per_hour));
    }

    // Air temperature
    if (!std::isnan(data.air_temp)) {
      setCellWithColor(row, COL_AIR_TEMP, FormatTemperature(data.air_temp),
                       GetAirTempColor(data.air_temp - 273.15));
    }

    // Sea temperature
    if (!std::isnan(data.sea_surface_temp)) {
      setCellWithColor(row, COL_SEA_TEMP,
                       FormatTemperature(data.sea_surface_temp),
                       GetSeaTempColor(data.sea_surface_temp));
    }

    // Relative humidity (range 0-100%)
    if (!std::isnan(data.relative_humidity)) {
      setCellWithColor(row, COL_REL_HUMIDITY,
                       wxString::Format("%.2f%%", data.relative_humidity),
                       GetCloudColor(data.relative_humidity));
    }

    // Air pressure with proper conversion from Pascals to hectoPascals
    if (!std::isnan(data.air_pressure)) {
      double hPa = data.air_pressure / 100.0;  // Convert from Pa to hPa
      setCellWithColor(row, COL_AIR_PRESSURE, FormatPressure(data.air_pressure),
                       GetPressureColor(hPa));
    }

    // CAPE
    if (!std::isnan(data.cape)) {
      setCellWithColor(row, COL_CAPE, wxString::Format("%.2f J/kg", data.cape),
                       GetCAPEColor(data.cape));
    }

    // Reflectivity
    if (!std::isnan(data.reflectivity)) {
      setCellWithColor(row, COL_REFLECTIVITY,
                       wxString::Format("%.1f dBZ", data.reflectivity),
                       GetReflectivityColor(data.reflectivity));
    }

    // Current data
    if (!std::isnan(data.currentSpeed) && data.currentSpeed > 0) {
      m_gridWeatherTable->SetCellValue(row, COL_CURRENT_SPEED,
                                       FormatSpeed(data.currentSpeed));

      if (!std::isnan(data.currentDir)) {
        m_gridWeatherTable->SetCellValue(
            row, COL_CURRENT_DIR,
            wxString::Format("%.0f\u00B0", positive_degrees(data.currentDir)));

        // Calculate current angle relative to COG if COG is available
        if (!std::isnan(data.cog)) {
          double currentAngle =
              CalculateCurrentAngle(data.currentDir, data.cog);

          // Display current angle with color coding
          wxString currentAngleStr =
              wxString::Format("%.0f\u00B0", currentAngle);
          wxColor effectColor =
              GetCurrentEffectColor(currentAngle, data.currentSpeed);
          setCellWithColor(row, COL_CURRENT_ANGLE, currentAngleStr,
                           effectColor);
        }
      }
    }

    // Significant wave height
    if (!std::isnan(data.WVHT) && data.WVHT > 0) {
      setCellWithColor(row, COL_WAVE_HEIGHT,
                       wxString::Format("%.1f m", data.WVHT),
                       GetWaveHeightColor(data.WVHT));
    }
    if (!std::isnan(data.WVDIR) && data.WVDIR >= 0) {
      m_gridWeatherTable->SetCellValue(
          row, COL_WAVE_DIRECTION,
          wxString::Format("%.0f\u00B0", positive_degrees(data.WVDIR)));
    }
    if (!std::isnan(data.WVREL)) {
      // Display wave direction relative to boat heading with color coding
      double displayAngle = positive_degrees(data.WVREL);
      setCellWithColor(row, COL_WAVE_REL,
                       wxString::Format("%.0f\u00B0", displayAngle),
                       GetWaveRelativeColor(displayAngle));
    }
    if (!std::isnan(data.WVPER) && data.WVPER > 0) {
      m_gridWeatherTable->SetCellValue(row, COL_WAVE_PERIOD,
                                       wxString::Format("%.1f s", data.WVPER));
    }

    // Sailing comfort level
    int comfortLevel = m_RouteMap->sailingConditionLevel(data);
    wxString comfortText = m_RouteMap->sailingConditionText(comfortLevel);
    setCellWithColor(row, COL_COMFORT, comfortText,
                     m_RouteMap->sailingConditionColor(comfortLevel));

    // Store current data as previous for the next iteration
    prevData = data;
    prevTime = data.time;
    row++;
  }

  // Auto-size all columns for better display
  for (int i = 0; i < COL_COUNT; i++) {
    m_gridWeatherTable->AutoSizeColumn(i);
  }

  // Reset highlight state
  m_highlightedRow = -1;

  // Apply highlight if timeline time is valid
  wxDateTime timelineTime =
      m_WeatherRouting.m_ConfigurationDialog.m_GribTimelineTime;
  if (timelineTime.IsValid()) {
    UpdateTimeHighlight(timelineTime);
  }

  // Update the summary information
  UpdateSummary();
}

void RoutingTablePanel::UpdateTimeHighlight(wxDateTime timelineTime) {
  if (!m_RouteMap || !timelineTime.IsValid()) {
    return;
  }

  // If the time hasn't changed, don't bother updating
  if (m_lastTimelineTime.IsValid() && m_lastTimelineTime == timelineTime) {
    return;
  }

  m_lastTimelineTime = timelineTime;

  // Get plot data from the route
  std::list<PlotData> plotData = m_RouteMap->GetPlotData(false);
  if (plotData.empty()) {
    return;
  }

  // First, restore the previous highlighted row to its original colors if
  // needed
  if (m_highlightedRow >= 0 &&
      m_highlightedRow < m_gridWeatherTable->GetNumberRows()) {
    // Check if we have stored colors for this row
    if (m_originalCellColors.find(m_highlightedRow) !=
        m_originalCellColors.end()) {
      size_t numCols = static_cast<size_t>(m_gridWeatherTable->GetNumberCols());
      for (size_t col = 0; col < numCols; col++) {
        if (col < m_originalCellColors[m_highlightedRow].size()) {
          // Restore the original color
          m_gridWeatherTable->SetCellBackgroundColour(
              m_highlightedRow, col,
              m_originalCellColors[m_highlightedRow][col]);
        }
      }
      // Remove the stored colors for this row as they've been restored
      m_originalCellColors.erase(m_highlightedRow);
    }
  }

  // Find the row with ETA closest to the timeline time
  int closestRow = -1;
  wxTimeSpan minDifference;

  int row = 0;
  for (const PlotData& data : plotData) {
    // Calculate the time difference between ETA of this point and timeline time
    wxTimeSpan difference = timelineTime - data.time;
    // Use absolute value for comparison
    difference = wxTimeSpan(difference.GetSeconds().Abs());

    // If this is the first row or if this row has a smaller difference
    if (closestRow < 0 || difference < minDifference) {
      minDifference = difference;
      closestRow = row;
    }
    row++;
  }

  // If we found a closest row, highlight it
  if (closestRow >= 0 && closestRow < m_gridWeatherTable->GetNumberRows()) {
    m_highlightedRow = closestRow;

    // Store the original colors before highlighting
    m_originalCellColors[closestRow] = std::vector<wxColour>();
    m_originalCellColors[closestRow].reserve(
        m_gridWeatherTable->GetNumberCols());

    // Store the cast value outside the loop to avoid repeatedly calling it
    size_t numCols = static_cast<size_t>(m_gridWeatherTable->GetNumberCols());
    for (size_t col = 0; col < numCols; col++) {
      // Store the original background color
      wxColour originalBg =
          m_gridWeatherTable->GetCellBackgroundColour(closestRow, col);
      m_originalCellColors[closestRow].push_back(originalBg);

      // Semi-transparent yellow highlight color
      wxColour highlightColor(255, 255, 0, 128);

      // Blend highlight with current background - simple 50% blend
      wxColour blendedColor((originalBg.Red() + highlightColor.Red()) / 2,
                            (originalBg.Green() + highlightColor.Green()) / 2,
                            (originalBg.Blue() + highlightColor.Blue()) / 2);

      m_gridWeatherTable->SetCellBackgroundColour(closestRow, col,
                                                  blendedColor);
    }

    // Scroll to ensure the highlighted row is visible
    m_gridWeatherTable->MakeCellVisible(closestRow, 0);
  }

  // Refresh the grid to show the changes
  m_gridWeatherTable->Refresh();
}

void RoutingTablePanel::OnExportButton(wxCommandEvent& event) {
  ExportToExcel();
}

void RoutingTablePanel::CreateSummaryTab() {
  m_summaryTab = new wxPanel(m_notebook, wxID_ANY);
  // Use system default colors for better integration
  m_summaryTab->SetBackgroundColour(
      wxSystemSettings::GetColour(wxSYS_COLOUR_WINDOW));

  wxBoxSizer* summaryMainSizer = new wxBoxSizer(wxVERTICAL);

  // Title
  wxStaticText* title =
      new wxStaticText(m_summaryTab, wxID_ANY, _("Route Summary"));
  wxFont titleFont = title->GetFont();
  titleFont.SetPointSize(titleFont.GetPointSize() + 2);
  titleFont.SetWeight(wxFONTWEIGHT_BOLD);
  title->SetFont(titleFont);
  summaryMainSizer->Add(title, 0, wxALIGN_CENTER | wxALL, 10);

  // Content area with export button
  wxBoxSizer* contentSizer = new wxBoxSizer(wxHORIZONTAL);

  // Left side - summary information in a grid
  wxFlexGridSizer* summaryGridSizer = new wxFlexGridSizer(15, 2, 8, 20);
  summaryGridSizer->AddGrowableCol(1);

  // Create summary labels and text controls
  summaryGridSizer->Add(
      new wxStaticText(m_summaryTab, wxID_ANY, _("Departure:")), 0,
      wxALIGN_CENTER_VERTICAL | wxALIGN_RIGHT);
  m_departureText = new wxStaticText(m_summaryTab, wxID_ANY, _("--"));
  summaryGridSizer->Add(m_departureText, 1, wxEXPAND | wxALIGN_CENTER_VERTICAL);

  summaryGridSizer->Add(new wxStaticText(m_summaryTab, wxID_ANY, _("Arrival:")),
                        0, wxALIGN_CENTER_VERTICAL | wxALIGN_RIGHT);
  m_arrivalText = new wxStaticText(m_summaryTab, wxID_ANY, _("--"));
  summaryGridSizer->Add(m_arrivalText, 1, wxEXPAND | wxALIGN_CENTER_VERTICAL);

  summaryGridSizer->Add(
      new wxStaticText(m_summaryTab, wxID_ANY, _("Distance:")), 0,
      wxALIGN_CENTER_VERTICAL | wxALIGN_RIGHT);
  m_distanceText = new wxStaticText(m_summaryTab, wxID_ANY, _("--"));
  summaryGridSizer->Add(m_distanceText, 1, wxEXPAND | wxALIGN_CENTER_VERTICAL);

  summaryGridSizer->Add(
      new wxStaticText(m_summaryTab, wxID_ANY, _("Duration:")), 0,
      wxALIGN_CENTER_VERTICAL | wxALIGN_RIGHT);
  m_durationText = new wxStaticText(m_summaryTab, wxID_ANY, _("--"));
  summaryGridSizer->Add(m_durationText, 1, wxEXPAND | wxALIGN_CENTER_VERTICAL);

  summaryGridSizer->Add(
      new wxStaticText(m_summaryTab, wxID_ANY, _("TWS Range:")), 0,
      wxALIGN_CENTER_VERTICAL | wxALIGN_RIGHT);
  m_windRangeText = new wxStaticText(m_summaryTab, wxID_ANY, _("--"));
  summaryGridSizer->Add(m_windRangeText, 1, wxEXPAND | wxALIGN_CENTER_VERTICAL);

  summaryGridSizer->Add(
      new wxStaticText(m_summaryTab, wxID_ANY, _("Wind Gust Range:")), 0,
      wxALIGN_CENTER_VERTICAL | wxALIGN_RIGHT);
  m_windGustRangeText = new wxStaticText(m_summaryTab, wxID_ANY, _("--"));
  summaryGridSizer->Add(m_windGustRangeText, 1,
                        wxEXPAND | wxALIGN_CENTER_VERTICAL);

  summaryGridSizer->Add(
      new wxStaticText(m_summaryTab, wxID_ANY, _("AWS Range:")), 0,
      wxALIGN_CENTER_VERTICAL | wxALIGN_RIGHT);
  m_awsRangeText = new wxStaticText(m_summaryTab, wxID_ANY, _("--"));
  summaryGridSizer->Add(m_awsRangeText, 1, wxEXPAND | wxALIGN_CENTER_VERTICAL);

  summaryGridSizer->Add(
      new wxStaticText(m_summaryTab, wxID_ANY, _("AWA Range:")), 0,
      wxALIGN_CENTER_VERTICAL | wxALIGN_RIGHT);
  m_awaRangeText = new wxStaticText(m_summaryTab, wxID_ANY, _("--"));
  summaryGridSizer->Add(m_awaRangeText, 1, wxEXPAND | wxALIGN_CENTER_VERTICAL);

  summaryGridSizer->Add(new wxStaticText(m_summaryTab, wxID_ANY,
                                         _("Significant Wave Height Range:")),
                        0, wxALIGN_CENTER_VERTICAL | wxALIGN_RIGHT);
  m_waveRangeText = new wxStaticText(m_summaryTab, wxID_ANY, _("--"));
  summaryGridSizer->Add(m_waveRangeText, 1, wxEXPAND | wxALIGN_CENTER_VERTICAL);

  summaryGridSizer->Add(
      new wxStaticText(m_summaryTab, wxID_ANY, _("Temperature Range:")), 0,
      wxALIGN_CENTER_VERTICAL | wxALIGN_RIGHT);
  m_tempRangeText = new wxStaticText(m_summaryTab, wxID_ANY, _("--"));
  summaryGridSizer->Add(m_tempRangeText, 1, wxEXPAND | wxALIGN_CENTER_VERTICAL);

  summaryGridSizer->Add(
      new wxStaticText(m_summaryTab, wxID_ANY, _("Sail Changes:")), 0,
      wxALIGN_CENTER_VERTICAL | wxALIGN_RIGHT);
  m_sailChangesText = new wxStaticText(m_summaryTab, wxID_ANY, _("--"));
  summaryGridSizer->Add(m_sailChangesText, 1,
                        wxEXPAND | wxALIGN_CENTER_VERTICAL);

  summaryGridSizer->Add(
      new wxStaticText(m_summaryTab, wxID_ANY, _("Tack Changes:")), 0,
      wxALIGN_CENTER_VERTICAL | wxALIGN_RIGHT);
  m_tackChangesText = new wxStaticText(m_summaryTab, wxID_ANY, _("--"));
  summaryGridSizer->Add(m_tackChangesText, 1,
                        wxEXPAND | wxALIGN_CENTER_VERTICAL);

  summaryGridSizer->Add(
      new wxStaticText(m_summaryTab, wxID_ANY, _("Jibe Changes:")), 0,
      wxALIGN_CENTER_VERTICAL | wxALIGN_RIGHT);
  m_jibeChangesText = new wxStaticText(m_summaryTab, wxID_ANY, _("--"));
  summaryGridSizer->Add(m_jibeChangesText, 1,
                        wxEXPAND | wxALIGN_CENTER_VERTICAL);

  summaryGridSizer->Add(
      new wxStaticText(m_summaryTab, wxID_ANY, _("Duration under motor:")), 0,
      wxALIGN_CENTER_VERTICAL | wxALIGN_RIGHT);
  m_motorDurationText = new wxStaticText(m_summaryTab, wxID_ANY, _("--"));
  summaryGridSizer->Add(m_motorDurationText, 1,
                        wxEXPAND | wxALIGN_CENTER_VERTICAL);

  summaryGridSizer->Add(
      new wxStaticText(m_summaryTab, wxID_ANY, _("Distance under motor:")), 0,
      wxALIGN_CENTER_VERTICAL | wxALIGN_RIGHT);
  m_motorDistanceText = new wxStaticText(m_summaryTab, wxID_ANY, _("--"));
  summaryGridSizer->Add(m_motorDistanceText, 1,
                        wxEXPAND | wxALIGN_CENTER_VERTICAL);

  contentSizer->Add(summaryGridSizer, 1, wxEXPAND | wxALL, 20);

  // Right side - export button
  wxBoxSizer* buttonSizer = new wxBoxSizer(wxVERTICAL);
  m_exportButton = new wxButton(m_summaryTab, wxID_ANY, _("Export to Excel"),
                                wxDefaultPosition, wxSize(140, 35));
  buttonSizer->Add(m_exportButton, 0, wxALIGN_CENTER | wxALL, 20);

  contentSizer->Add(buttonSizer, 0, wxALIGN_CENTER_VERTICAL);

  summaryMainSizer->Add(contentSizer, 1, wxEXPAND);
  m_summaryTab->SetSizer(summaryMainSizer);
}

RoutingTablePanel::SummaryData RoutingTablePanel::CalculateSummaryData() {
  SummaryData summary = {};

  // Get plot data from the route
  std::list<PlotData> plotData = m_RouteMap->GetPlotData(false);
  if (plotData.empty()) {
    return summary;
  }

  // Initialize variables for calculations
  summary.totalDistance = 0.0;
  summary.motorDistance = 0.0;
  summary.minWindSpeed = DBL_MAX;
  summary.maxWindSpeed = -DBL_MAX;
  summary.minWindGust = DBL_MAX;
  summary.maxWindGust = -DBL_MAX;
  summary.minAWS = DBL_MAX;
  summary.maxAWS = -DBL_MAX;
  summary.minAWA = DBL_MAX;
  summary.maxAWA = -DBL_MAX;
  summary.minWaveHeight = DBL_MAX;
  summary.maxWaveHeight = -DBL_MAX;
  summary.minTemp = DBL_MAX;
  summary.maxTemp = -DBL_MAX;
  int motorPoints = 0;
  int totalPoints = 0;
  wxTimeSpan totalMotorTime;

  PlotData prevData;
  bool firstPoint = true;
  wxDateTime prevTime;

  for (const PlotData& data : plotData) {
    if (firstPoint) {
      summary.startTime = data.time;
      firstPoint = false;
    } else {
      // Calculate distance
      double legDistance = DistGreatCircle_Plugin(prevData.lat, prevData.lon,
                                                  data.lat, data.lon);
      summary.totalDistance += legDistance;

      // Calculate time duration for this leg
      wxTimeSpan legDuration = data.time.Subtract(prevTime);

      // Check if motor was used for this leg
      if (data.data_mask & DataMask::MOTOR_USED) {
        summary.motorDistance += legDistance;
        totalMotorTime = totalMotorTime.Add(legDuration);
      }
    }

    summary.endTime = data.time;
    totalPoints++;

    // Check for motor usage (for point-based percentage)
    if (data.data_mask & DataMask::MOTOR_USED) {
      motorPoints++;
    }

    // Track wind speed range
    if (!std::isnan(data.twsOverWater)) {
      summary.minWindSpeed = std::min(summary.minWindSpeed, data.twsOverWater);
      summary.maxWindSpeed = std::max(summary.maxWindSpeed, data.twsOverWater);
    }

    // Track wind gust range
    if (!std::isnan(data.VW_GUST)) {
      summary.minWindGust = std::min(summary.minWindGust, data.VW_GUST);
      summary.maxWindGust = std::max(summary.maxWindGust, data.VW_GUST);
    }

    // Track AWA and AWS range (calculate apparent wind from available data)
    if (!std::isnan(data.stw) && !std::isnan(data.twdOverWater) &&
        !std::isnan(data.twsOverWater) && !std::isnan(data.ctw)) {
      double apparentWindSpeed = Polar::VelocityApparentWind(
          data.stw, data.twdOverWater - data.ctw, data.twsOverWater);
      double apparentWindAngle = Polar::DirectionApparentWind(
          apparentWindSpeed, data.stw, data.twdOverWater - data.ctw,
          data.twsOverWater);

      // Track AWS range
      if (!std::isnan(apparentWindSpeed)) {
        summary.minAWS = std::min(summary.minAWS, apparentWindSpeed);
        summary.maxAWS = std::max(summary.maxAWS, apparentWindSpeed);
      }

      // Track AWA range
      if (!std::isnan(apparentWindAngle)) {
        // Convert to absolute value for range calculation (0-180 degrees)
        double absAWA = std::abs(apparentWindAngle);
        if (absAWA > 180) absAWA = 360 - absAWA;
        summary.minAWA = std::min(summary.minAWA, absAWA);
        summary.maxAWA = std::max(summary.maxAWA, absAWA);
      }
    }

    // Track wave height range
    if (!std::isnan(data.WVHT) && data.WVHT > 0) {
      summary.minWaveHeight = std::min(summary.minWaveHeight, data.WVHT);
      summary.maxWaveHeight = std::max(summary.maxWaveHeight, data.WVHT);
    }

    // Track temperature range (convert from Kelvin to Celsius)
    if (!std::isnan(data.air_temp)) {
      double tempC = data.air_temp - 273.15;
      summary.minTemp = std::min(summary.minTemp, tempC);
      summary.maxTemp = std::max(summary.maxTemp, tempC);
    }

    prevData = data;
    prevTime = data.time;
  }

  // Get cumulative tacks, jibes, and sail changes from the last data point
  // These are already cumulative in the PlotData
  if (!plotData.empty()) {
    const PlotData& lastData = plotData.back();
    summary.sailChanges = lastData.sail_plan_changes;
    summary.tackChanges = lastData.tacks;
    summary.jibeChanges = lastData.jibes;
  }

  // Calculate motor percentages
  summary.motorDurationPercentage =
      totalPoints > 0 ? (double)motorPoints / totalPoints * 100.0 : 0.0;
  summary.motorDistancePercentage =
      summary.totalDistance > 0
          ? summary.motorDistance / summary.totalDistance * 100.0
          : 0.0;
  summary.motorDuration = totalMotorTime;

  return summary;
}

void RoutingTablePanel::UpdateSummary() {
  // Calculate summary data using helper function
  SummaryData summary = CalculateSummaryData();

  // Check if we have valid data
  if (!summary.startTime.IsValid()) {
    // No data - set all to default
    m_departureText->SetLabel(_("--"));
    m_arrivalText->SetLabel(_("--"));
    m_distanceText->SetLabel(_("--"));
    m_durationText->SetLabel(_("--"));
    m_windRangeText->SetLabel(_("--"));
    m_windGustRangeText->SetLabel(_("--"));
    m_awsRangeText->SetLabel(_("--"));
    m_awaRangeText->SetLabel(_("--"));
    m_waveRangeText->SetLabel(_("--"));
    m_tempRangeText->SetLabel(_("--"));
    m_sailChangesText->SetLabel(_("--"));
    m_motorDurationText->SetLabel(_("--"));
    m_motorDistanceText->SetLabel(_("--"));
    m_tackChangesText->SetLabel(_("--"));
    m_jibeChangesText->SetLabel(_("--"));
    return;
  }

  bool useLocalTime =
      m_WeatherRouting.m_SettingsDialog.m_cbUseLocalTime->GetValue();

  // Update summary text controls with calculated data
#if OCPN_API_VERSION_MAJOR > 1 || \
    (OCPN_API_VERSION_MAJOR == 1 && OCPN_API_VERSION_MINOR >= 20)
  m_departureText->SetLabel(toUsrDateTimeFormat_Plugin(summary.startTime));
  m_arrivalText->SetLabel(toUsrDateTimeFormat_Plugin(summary.endTime));
#else
  wxDateTime displayStartTime =
      useLocalTime ? summary.startTime.FromUTC() : summary.startTime;
  wxDateTime displayEndTime =
      useLocalTime ? summary.endTime.FromUTC() : summary.endTime;
  std::string timeFormat = useLocalTime? "%Y-%m-%d %H:%M %Z" : "%Y-%m-%d %H:%M UTC"; 
  m_departureText->SetLabel(displayStartTime.Format(timeFormat.c_str()));
  m_arrivalText->SetLabel(displayEndTime.Format(timeFormat.c_str()));
#endif

  m_distanceText->SetLabel(FormatDistance(summary.totalDistance));

  wxTimeSpan totalDuration = summary.endTime.Subtract(summary.startTime);
  int days = totalDuration.GetDays();
  wxString durationString;
  if (days > 0) {
    durationString = wxString::Format("%d days %02d:%02d", days,
                                      totalDuration.GetHours() % 24,
                                      totalDuration.GetMinutes() % 60);
  } else {
    durationString = wxString::Format("%02d:%02d", totalDuration.GetHours(),
                                      totalDuration.GetMinutes() % 60);
  }
  m_durationText->SetLabel(durationString);

  if (summary.minWindSpeed != DBL_MAX && summary.maxWindSpeed != -DBL_MAX) {
    m_windRangeText->SetLabel(FormatSpeed(summary.minWindSpeed) + " - " +
                              FormatSpeed(summary.maxWindSpeed));
  } else {
    m_windRangeText->SetLabel(_("--"));
  }

  if (summary.minWindGust != DBL_MAX && summary.maxWindGust != -DBL_MAX) {
    m_windGustRangeText->SetLabel(FormatSpeed(summary.minWindGust) + " - " +
                                  FormatSpeed(summary.maxWindGust));
  } else {
    m_windGustRangeText->SetLabel(_("--"));
  }

  if (summary.minAWS != DBL_MAX && summary.maxAWS != -DBL_MAX) {
    m_awsRangeText->SetLabel(FormatSpeed(summary.minAWS) + " - " +
                             FormatSpeed(summary.maxAWS));
  } else {
    m_awsRangeText->SetLabel(_("--"));
  }

  if (summary.minAWA != DBL_MAX && summary.maxAWA != -DBL_MAX) {
    m_awaRangeText->SetLabel(wxString::Format("%.0f\u00B0 - %.0f\u00B0",
                                              summary.minAWA, summary.maxAWA));
  } else {
    m_awaRangeText->SetLabel(_("--"));
  }

  if (summary.minWaveHeight != DBL_MAX && summary.maxWaveHeight != -DBL_MAX) {
    m_waveRangeText->SetLabel(wxString::Format(
        "%.1f - %.1f m", summary.minWaveHeight, summary.maxWaveHeight));
  } else {
    m_waveRangeText->SetLabel(_("--"));
  }

  if (summary.minTemp != DBL_MAX && summary.maxTemp != -DBL_MAX) {
    double minTempUser = toUsrTemp_Plugin(summary.minTemp);
    double maxTempUser = toUsrTemp_Plugin(summary.maxTemp);
    wxString tempUnit = getUsrTempUnit_Plugin();
    m_tempRangeText->SetLabel(
        wxString::Format("%.1f - %.1f%s", minTempUser, maxTempUser, tempUnit));
  } else {
    m_tempRangeText->SetLabel(_("--"));
  }

  m_sailChangesText->SetLabel(wxString::Format("%d", summary.sailChanges));

  // Format motor duration
  wxString motorDurationStr;
  int motorDays = summary.motorDuration.GetDays();
  if (motorDays > 0) {
    motorDurationStr = wxString::Format("%dd %02d:%02d (%.1f%%)", motorDays,
                                        summary.motorDuration.GetHours() % 24,
                                        summary.motorDuration.GetMinutes() % 60,
                                        summary.motorDurationPercentage);
  } else {
    motorDurationStr =
        wxString::Format("%02d:%02d (%.1f%%)", summary.motorDuration.GetHours(),
                         summary.motorDuration.GetMinutes() % 60,
                         summary.motorDurationPercentage);
  }
  m_motorDurationText->SetLabel(motorDurationStr);

  // Format motor distance
  wxString motorDistanceStr =
      FormatDistance(summary.motorDistance) +
      wxString::Format(" (%.1f%%)", summary.motorDistancePercentage);
  m_motorDistanceText->SetLabel(motorDistanceStr);

  m_tackChangesText->SetLabel(wxString::Format("%d", summary.tackChanges));
  m_jibeChangesText->SetLabel(wxString::Format("%d", summary.jibeChanges));
}

void RoutingTablePanel::ExportToExcel() {
  wxFileDialog saveFileDialog(
      this, _("Export table to Excel"), "", "routing_table.xlsx",
      "Excel files (*.xlsx)|*.xlsx|Excel 97-2003 (*.xls)|*.xls",
      wxFD_SAVE | wxFD_OVERWRITE_PROMPT);

  if (saveFileDialog.ShowModal() == wxID_CANCEL) {
    return;
  }

  wxString filename = saveFileDialog.GetPath();
  wxString extension = wxFileName(filename).GetExt().Lower();

  bool success = false;
  if (extension == "xlsx" || extension == "xls") {
    success = WriteExcelXML(filename);
  } else {
    // Default to .xlsx if no extension specified
    if (extension.IsEmpty()) {
      filename += ".xlsx";
    }
    success = WriteExcelXML(filename);
  }

  if (!success) {
    wxMessageBox(_("Failed to export table to Excel"), _("Export Error"),
                 wxOK | wxICON_ERROR);
  } else {
    wxMessageBox(_("Table exported successfully"), _("Export Complete"),
                 wxOK | wxICON_INFORMATION);
  }
}

wxString RoutingTablePanel::ConvertColorToHex(const wxColour& color) {
  return wxString::Format("#%02X%02X%02X", color.Red(), color.Green(),
                          color.Blue());
}

wxString RoutingTablePanel::ConvertColorToARGB(const wxColour& color) {
  return wxString::Format("FF%02X%02X%02X", color.Red(), color.Green(),
                          color.Blue());
}

wxString RoutingTablePanel::EscapeXML(const wxString& text) {
  wxString escaped = text;
  escaped.Replace("&", "&amp;");
  escaped.Replace("<", "&lt;");
  escaped.Replace(">", "&gt;");
  escaped.Replace("\"", "&quot;");
  escaped.Replace("'", "&apos;");
  return escaped;
}

RoutingTablePanel::CellStyle RoutingTablePanel::GetCellStyle(int row, int col) {
  CellStyle style;

  if (row >= 0 && row < m_gridWeatherTable->GetNumberRows() && col >= 0 &&
      col < m_gridWeatherTable->GetNumberCols()) {
    // Use public methods to get cell attributes
    style.bgColor = m_gridWeatherTable->GetCellBackgroundColour(row, col);
    style.textColor = m_gridWeatherTable->GetCellTextColour(row, col);

    // Get font information
    wxFont cellFont = m_gridWeatherTable->GetCellFont(row, col);
    style.isBold = cellFont.GetWeight() == wxFONTWEIGHT_BOLD;
  }

  return style;
}

int RoutingTablePanel::GetOrCreateStyleId(const CellStyle& style) {
  auto it = m_styleMap.find(style);
  if (it != m_styleMap.end()) {
    return it->second;
  }

  int styleId = m_styles.size();
  m_styles.push_back(style);
  m_styleMap[style] = styleId;
  return styleId;
}

wxString RoutingTablePanel::CreateStylesXMLWithColors() {
  // Clear and rebuild styles for this export
  m_styles.clear();
  m_styleMap.clear();

  // Add default style
  CellStyle defaultStyle(*wxWHITE, *wxBLACK, false);
  GetOrCreateStyleId(defaultStyle);

  // Add header style
  CellStyle headerStyle(wxColour(211, 211, 211), *wxBLACK, true);
  GetOrCreateStyleId(headerStyle);

  // Scan all cells to build style collection
  for (int row = 0; row < m_gridWeatherTable->GetNumberRows(); row++) {
    for (int col = 0; col < m_gridWeatherTable->GetNumberCols(); col++) {
      CellStyle cellStyle = GetCellStyle(row, col);
      GetOrCreateStyleId(cellStyle);
    }
  }

  wxString xml =
      "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n"
      "<styleSheet "
      "xmlns=\"http://schemas.openxmlformats.org/spreadsheetml/2006/main\">\n";

  // Fonts section
  xml += wxString::Format("<fonts count=\"%d\">\n", (int)m_styles.size());
  for (const auto& style : m_styles) {
    xml += "<font>";
    if (style.isBold) {
      xml += "<b/>";
    }
    xml += "<sz val=\"11\"/>";
    xml += "<name val=\"Calibri\"/>";
    xml += wxString::Format("<color rgb=\"%s\"/>",
                            ConvertColorToARGB(style.textColor));
    xml += "</font>\n";
  }
  xml += "</fonts>\n";

  // Fills section
  xml += wxString::Format("<fills count=\"%d\">\n", (int)m_styles.size() + 2);
  xml += "<fill><patternFill patternType=\"none\"/></fill>\n";
  xml += "<fill><patternFill patternType=\"gray125\"/></fill>\n";
  for (const auto& style : m_styles) {
    xml += "<fill><patternFill patternType=\"solid\">";
    xml += wxString::Format("<fgColor rgb=\"%s\"/>",
                            ConvertColorToARGB(style.bgColor));
    xml += "</patternFill></fill>\n";
  }
  xml += "</fills>\n";

  // Borders section
  xml += "<borders count=\"2\">\n";
  xml += "<border><left/><right/><top/><bottom/><diagonal/></border>\n";
  xml +=
      "<border><left style=\"thin\"/><right style=\"thin\"/><top "
      "style=\"thin\"/><bottom style=\"thin\"/><diagonal/></border>\n";
  xml += "</borders>\n";

  // Cell style formats
  xml += "<cellStyleXfs count=\"1\">\n";
  xml += "<xf numFmtId=\"0\" fontId=\"0\" fillId=\"0\" borderId=\"0\"/>\n";
  xml += "</cellStyleXfs>\n";

  // Cell formats
  xml += wxString::Format("<cellXfs count=\"%d\">\n", (int)m_styles.size());
  for (size_t i = 0; i < m_styles.size(); i++) {
    int borderId = (i == 1) ? 1 : 0;  // Use border for header style
    xml += wxString::Format(
        "<xf numFmtId=\"0\" fontId=\"%d\" fillId=\"%d\" borderId=\"%d\" "
        "xfId=\"0\"/>\n",
        (int)i, (int)i + 2, borderId);
  }
  xml += "</cellXfs>\n";

  xml += "</styleSheet>";
  return xml;
}

wxString RoutingTablePanel::CreateWorksheetXMLWithColors() {
  wxString xml =
      "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n"
      "<worksheet "
      "xmlns=\"http://schemas.openxmlformats.org/spreadsheetml/2006/main\">\n"
      "<sheetData>\n";

  // Header row with style
  xml += "<row r=\"1\">\n";
  CellStyle headerStyle(wxColour(211, 211, 211), *wxBLACK, true);
  int headerStyleId = GetOrCreateStyleId(headerStyle);

  for (int col = 0; col < m_gridWeatherTable->GetNumberCols(); col++) {
    wxString cellRef = GetCellReference(1, col + 1);
    wxString header = EscapeXML(m_gridWeatherTable->GetColLabelValue(col));
    xml += wxString::Format(
        "<c r=\"%s\" t=\"inlineStr\" s=\"%d\"><is><t>%s</t></is></c>\n",
        cellRef, headerStyleId, header);
  }
  xml += "</row>\n";

  // Data rows
  for (int row = 0; row < m_gridWeatherTable->GetNumberRows(); row++) {
    xml += wxString::Format("<row r=\"%d\">\n", row + 2);
    for (int col = 0; col < m_gridWeatherTable->GetNumberCols(); col++) {
      wxString cellRef = GetCellReference(row + 2, col + 1);
      wxString value = EscapeXML(m_gridWeatherTable->GetCellValue(row, col));

      // Get the style for this cell
      CellStyle cellStyle = GetCellStyle(row, col);
      int styleId = GetOrCreateStyleId(cellStyle);

      if (value.IsEmpty()) {
        xml += wxString::Format(
            "<c r=\"%s\" t=\"inlineStr\" s=\"%d\"><is><t></t></is></c>\n",
            cellRef, styleId);
      } else {
        xml += wxString::Format(
            "<c r=\"%s\" t=\"inlineStr\" s=\"%d\"><is><t>%s</t></is></c>\n",
            cellRef, styleId, value);
      }
    }
    xml += "</row>\n";
  }

  xml += "</sheetData>\n</worksheet>";
  return xml;
}

wxString RoutingTablePanel::GetCellReference(int row, int col) {
  wxString colStr;
  while (col > 0) {
    col--;
    colStr = wxChar('A' + (col % 26)) + colStr;
    col /= 26;
  }
  return wxString::Format("%s%d", colStr, row);
}

bool RoutingTablePanel::WriteExcelXML(const wxString& filename) {
  wxString extension = wxFileName(filename).GetExt().Lower();

  if (extension == "xlsx") {
    return WriteXLSX(filename);
  } else {
    // Fallback to simple XML for other extensions
    return WriteSimpleXML(filename);
  }
}

bool RoutingTablePanel::WriteXLSX(const wxString& filename) {
  wxFileOutputStream fileStream(filename);
  if (!fileStream.IsOk()) {
    return false;
  }

  wxZipOutputStream zipStream(fileStream);

  // Create [Content_Types].xml
  wxString contentTypes =
      "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n"
      "<Types "
      "xmlns=\"http://schemas.openxmlformats.org/package/2006/"
      "content-types\">\n"
      "<Default Extension=\"rels\" "
      "ContentType=\"application/"
      "vnd.openxmlformats-package.relationships+xml\"/>\n"
      "<Default Extension=\"xml\" ContentType=\"application/xml\"/>\n"
      "<Override PartName=\"/xl/workbook.xml\" "
      "ContentType=\"application/"
      "vnd.openxmlformats-officedocument.spreadsheetml.sheet.main+xml\"/>\n"
      "<Override PartName=\"/xl/worksheets/sheet1.xml\" "
      "ContentType=\"application/"
      "vnd.openxmlformats-officedocument.spreadsheetml.worksheet+xml\"/>\n"
      "<Override PartName=\"/xl/styles.xml\" "
      "ContentType=\"application/"
      "vnd.openxmlformats-officedocument.spreadsheetml.styles+xml\"/>\n"
      "</Types>";

  zipStream.PutNextEntry("[Content_Types].xml");
  wxCharBuffer buffer = contentTypes.ToUTF8();
  zipStream.Write(buffer.data(), buffer.length());
  zipStream.CloseEntry();

  // Create _rels/.rels
  wxString mainRels =
      "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n"
      "<Relationships "
      "xmlns=\"http://schemas.openxmlformats.org/package/2006/"
      "relationships\">\n"
      "<Relationship Id=\"rId1\" "
      "Type=\"http://schemas.openxmlformats.org/officeDocument/2006/"
      "relationships/officeDocument\" Target=\"xl/workbook.xml\"/>\n"
      "</Relationships>";

  zipStream.PutNextEntry("_rels/.rels");
  buffer = mainRels.ToUTF8();
  zipStream.Write(buffer.data(), buffer.length());
  zipStream.CloseEntry();

  // Create xl/workbook.xml
  wxString workbook =
      "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n"
      "<workbook "
      "xmlns=\"http://schemas.openxmlformats.org/spreadsheetml/2006/main\" "
      "xmlns:r=\"http://schemas.openxmlformats.org/officeDocument/2006/"
      "relationships\">\n"
      "<sheets>\n"
      "<sheet name=\"Routing Table\" sheetId=\"1\" r:id=\"rId1\"/>\n"
      "</sheets>\n"
      "</workbook>";

  zipStream.PutNextEntry("xl/workbook.xml");
  buffer = workbook.ToUTF8();
  zipStream.Write(buffer.data(), buffer.length());
  zipStream.CloseEntry();

  // Create xl/_rels/workbook.xml.rels
  wxString workbookRels =
      "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n"
      "<Relationships "
      "xmlns=\"http://schemas.openxmlformats.org/package/2006/"
      "relationships\">\n"
      "<Relationship Id=\"rId1\" "
      "Type=\"http://schemas.openxmlformats.org/officeDocument/2006/"
      "relationships/worksheet\" Target=\"worksheets/sheet1.xml\"/>\n"
      "<Relationship Id=\"rId2\" "
      "Type=\"http://schemas.openxmlformats.org/officeDocument/2006/"
      "relationships/styles\" Target=\"styles.xml\"/>\n"
      "</Relationships>";

  zipStream.PutNextEntry("xl/_rels/workbook.xml.rels");
  buffer = workbookRels.ToUTF8();
  zipStream.Write(buffer.data(), buffer.length());
  zipStream.CloseEntry();

  // Create xl/styles.xml with colors
  wxString styles = CreateStylesXMLWithColors();
  zipStream.PutNextEntry("xl/styles.xml");
  buffer = styles.ToUTF8();
  zipStream.Write(buffer.data(), buffer.length());
  zipStream.CloseEntry();

  // Create xl/worksheets/sheet1.xml with actual data and colors
  wxString worksheet = CreateWorksheetXMLWithColors();
  zipStream.PutNextEntry("xl/worksheets/sheet1.xml");
  buffer = worksheet.ToUTF8();
  zipStream.Write(buffer.data(), buffer.length());
  zipStream.CloseEntry();

  // Properly close the streams
  bool success = zipStream.Close();
  fileStream.Close();

  return success;
}

wxString RoutingTablePanel::CreateStylesXML() {
  return "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n"
         "<styleSheet "
         "xmlns=\"http://schemas.openxmlformats.org/spreadsheetml/2006/"
         "main\">\n"
         "<fonts count=\"2\">\n"
         "<font><sz val=\"11\"/><name val=\"Calibri\"/></font>\n"
         "<font><b/><sz val=\"11\"/><name val=\"Calibri\"/></font>\n"
         "</fonts>\n"
         "<fills count=\"3\">\n"
         "<fill><patternFill patternType=\"none\"/></fill>\n"
         "<fill><patternFill patternType=\"gray125\"/></fill>\n"
         "<fill><patternFill patternType=\"solid\"><fgColor "
         "rgb=\"FFD3D3D3\"/></patternFill></fill>\n"
         "</fills>\n"
         "<borders count=\"2\">\n"
         "<border><left/><right/><top/><bottom/><diagonal/></border>\n"
         "<border><left style=\"thin\"/><right style=\"thin\"/><top "
         "style=\"thin\"/><bottom style=\"thin\"/><diagonal/></border>\n"
         "</borders>\n"
         "<cellStyleXfs count=\"1\">\n"
         "<xf numFmtId=\"0\" fontId=\"0\" fillId=\"0\" borderId=\"0\"/>\n"
         "</cellStyleXfs>\n"
         "<cellXfs count=\"2\">\n"
         "<xf numFmtId=\"0\" fontId=\"0\" fillId=\"0\" borderId=\"0\" "
         "xfId=\"0\"/>\n"
         "<xf numFmtId=\"0\" fontId=\"1\" fillId=\"2\" borderId=\"1\" "
         "xfId=\"0\"/>\n"
         "</cellXfs>\n"
         "</styleSheet>";
}

wxString RoutingTablePanel::CreateWorksheetXML() {
  wxString xml =
      "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n"
      "<worksheet "
      "xmlns=\"http://schemas.openxmlformats.org/spreadsheetml/2006/main\">\n"
      "<sheetData>\n";

  // Header row with style
  xml += "<row r=\"1\">\n";
  for (int col = 0; col < m_gridWeatherTable->GetNumberCols(); col++) {
    wxString cellRef = GetCellReference(1, col + 1);
    wxString header = EscapeXML(m_gridWeatherTable->GetColLabelValue(col));
    xml += wxString::Format(
        "<c r=\"%s\" t=\"inlineStr\" s=\"1\"><is><t>%s</t></is></c>\n", cellRef,
        header);
  }
  xml += "</row>\n";

  // Data rows
  for (int row = 0; row < m_gridWeatherTable->GetNumberRows(); row++) {
    xml += wxString::Format("<row r=\"%d\">\n", row + 2);
    for (int col = 0; col < m_gridWeatherTable->GetNumberCols(); col++) {
      wxString cellRef = GetCellReference(row + 2, col + 1);
      wxString value = EscapeXML(m_gridWeatherTable->GetCellValue(row, col));
      if (value.IsEmpty()) {
        xml += wxString::Format(
            "<c r=\"%s\" t=\"inlineStr\"><is><t></t></is></c>\n", cellRef);
      } else {
        xml += wxString::Format(
            "<c r=\"%s\" t=\"inlineStr\"><is><t>%s</t></is></c>\n", cellRef,
            value);
      }
    }
    xml += "</row>\n";
  }

  xml += "</sheetData>\n</worksheet>";
  return xml;
}

bool RoutingTablePanel::WriteSimpleXML(const wxString& filename) {
  wxTextFile file(filename);

  if (file.Exists()) {
    file.Open();
    file.Clear();
  } else {
    file.Create();
  }

  // Excel XML header - simplified
  file.AddLine("<?xml version=\"1.0\"?>");
  file.AddLine("<?mso-application progid=\"Excel.Sheet\"?>");
  file.AddLine(
      "<Workbook xmlns=\"urn:schemas-microsoft-com:office:spreadsheet\" "
      "xmlns:o=\"urn:schemas-microsoft-com:office:office\" "
      "xmlns:x=\"urn:schemas-microsoft-com:office:excel\" "
      "xmlns:ss=\"urn:schemas-microsoft-com:office:spreadsheet\" "
      "xmlns:html=\"http://www.w3.org/TR/REC-html40\">");

  // Document properties
  file.AddLine(
      "<DocumentProperties xmlns=\"urn:schemas-microsoft-com:office:office\">");
  file.AddLine("<Title>Weather Routing Table</Title>");
  file.AddLine("<Subject>OpenCPN Weather Routing Export</Subject>");
  file.AddLine("<Created>" + wxDateTime::Now().FormatISOCombined() +
               "</Created>");
  file.AddLine("</DocumentProperties>");

  // Build unique styles based on cell colors
  std::map<wxString, int> styleMap;
  std::vector<wxString> styleDefinitions;
  int styleIndex = 0;

  // Default style
  styleMap["default"] = styleIndex++;
  styleDefinitions.push_back(
      "<Style ss:ID=\"Default\" ss:Name=\"Normal\">"
      "<Alignment ss:Vertical=\"Bottom\"/>"
      "<Borders/>"
      "<Font ss:FontName=\"Calibri\" x:Family=\"Swiss\" ss:Size=\"10\" "
      "ss:Color=\"#000000\"/>"
      "<Interior/>"
      "</Style>");

  // Header style
  styleMap["header"] = styleIndex++;
  styleDefinitions.push_back(
      "<Style ss:ID=\"Header\">"
      "<Font ss:FontName=\"Calibri\" ss:Size=\"10\" ss:Color=\"#000000\" "
      "ss:Bold=\"1\"/>"
      "<Interior ss:Color=\"#D3D3D3\" ss:Pattern=\"Solid\"/>"
      "<Borders>"
      "<Border ss:Position=\"Bottom\" ss:LineStyle=\"Continuous\" "
      "ss:Weight=\"1\"/>"
      "<Border ss:Position=\"Left\" ss:LineStyle=\"Continuous\" "
      "ss:Weight=\"1\"/>"
      "<Border ss:Position=\"Right\" ss:LineStyle=\"Continuous\" "
      "ss:Weight=\"1\"/>"
      "<Border ss:Position=\"Top\" ss:LineStyle=\"Continuous\" "
      "ss:Weight=\"1\"/>"
      "</Borders>"
      "</Style>");

  // Scan all cells to create unique styles
  for (int row = 0; row < m_gridWeatherTable->GetNumberRows(); row++) {
    for (int col = 0; col < m_gridWeatherTable->GetNumberCols(); col++) {
      CellStyle cellStyle = GetCellStyle(row, col);
      wxString bgHex = ConvertColorToHex(cellStyle.bgColor);
      wxString textHex = ConvertColorToHex(cellStyle.textColor);
      wxString styleKey = wxString::Format(
          "%s_%s_%s", bgHex, textHex, cellStyle.isBold ? "bold" : "normal");

      if (styleMap.find(styleKey) == styleMap.end()) {
        styleMap[styleKey] = styleIndex++;
        wxString styleId = wxString::Format("Style%d", styleMap[styleKey]);
        wxString styleDef = wxString::Format(
            "<Style ss:ID=\"%s\">"
            "<Font ss:FontName=\"Calibri\" ss:Size=\"10\" ss:Color=\"%s\"%s/>"
            "<Interior ss:Color=\"%s\" ss:Pattern=\"Solid\"/>"
            "</Style>",
            styleId, textHex, cellStyle.isBold ? " ss:Bold=\"1\"" : "", bgHex);
        styleDefinitions.push_back(styleDef);
      }
    }
  }

  // Write styles section
  file.AddLine("<Styles>");
  for (const auto& styleDef : styleDefinitions) {
    file.AddLine(styleDef);
  }
  file.AddLine("</Styles>");

  // Worksheet
  file.AddLine("<Worksheet ss:Name=\"Routing Table\">");
  file.AddLine(wxString::Format(
      "<Table ss:ExpandedColumnCount=\"%d\" ss:ExpandedRowCount=\"%d\" "
      "x:FullColumns=\"1\" x:FullRows=\"1\" ss:DefaultRowHeight=\"15\">",
      m_gridWeatherTable->GetNumberCols(),
      m_gridWeatherTable->GetNumberRows() + 1));

  // Column definitions
  for (int col = 0; col < m_gridWeatherTable->GetNumberCols(); col++) {
    file.AddLine("<Column ss:AutoFitWidth=\"1\"/>");
  }

  // Header row
  file.AddLine("<Row>");
  for (int col = 0; col < m_gridWeatherTable->GetNumberCols(); col++) {
    file.AddLine("<Cell ss:StyleID=\"Header\">");
    file.AddLine("<Data ss:Type=\"String\">" +
                 EscapeXML(m_gridWeatherTable->GetColLabelValue(col)) +
                 "</Data>");
    file.AddLine("</Cell>");
  }
  file.AddLine("</Row>");

  // Data rows with colors
  for (int row = 0; row < m_gridWeatherTable->GetNumberRows(); row++) {
    file.AddLine("<Row>");
    for (int col = 0; col < m_gridWeatherTable->GetNumberCols(); col++) {
      // Get cell style and determine style ID
      CellStyle cellStyle = GetCellStyle(row, col);
      wxString bgHex = ConvertColorToHex(cellStyle.bgColor);
      wxString textHex = ConvertColorToHex(cellStyle.textColor);
      wxString styleKey = wxString::Format(
          "%s_%s_%s", bgHex, textHex, cellStyle.isBold ? "bold" : "normal");

      wxString styleRef = "Default";
      auto it = styleMap.find(styleKey);
      if (it != styleMap.end()) {
        styleRef = wxString::Format("Style%d", it->second);
      }

      file.AddLine(wxString::Format("<Cell ss:StyleID=\"%s\">", styleRef));

      // Add the data
      wxString value = m_gridWeatherTable->GetCellValue(row, col);
      if (value.IsEmpty()) {
        file.AddLine("<Data ss:Type=\"String\"></Data>");
      } else {
        file.AddLine("<Data ss:Type=\"String\">" + EscapeXML(value) +
                     "</Data>");
      }

      file.AddLine("</Cell>");
    }
    file.AddLine("</Row>");
  }

  file.AddLine("</Table>");
  file.AddLine("</Worksheet>");
  file.AddLine("</Workbook>");

  bool success = file.Write();
  file.Close();
  return success;
}