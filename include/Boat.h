/***************************************************************************
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
 **************************************************************************/

#ifndef _WEATHER_ROUTING_BOAT_H_
#define _WEATHER_ROUTING_BOAT_H_

#include "Polar.h"

/*
 * This class is responsible for loading and saving the polars of a given boat
 * to disk. and for finding the fastest polar for a given wind and heading.
 */
class Boat {
public:
  Boat();
  ~Boat();

  wxString OpenXML(wxString filename, bool shortcut = true);
  wxString SaveXML(wxString filename);

  std::vector<Polar> Polars;

  /**
   * Determines if a specific polar represents the fastest sail configuration
   * for given conditions.
   *
   * Compares the boat speed from the specified polar against all other
   * available polars to determine if it provides the best performance in the
   * given wind conditions. It applies the crossover percentage to give a small
   * bonus to the currently selected polar, which helps prevent excessive sail
   * changes.
   *
   * The crossover percentage creates a "hysteresis" effect, meaning a sail must
   * be significantly faster (by the percentage specified) to trigger a change
   * from the current sail configuration.
   *
   * The function properly handles error conditions and invalid boat speeds (NaN
   * values) when comparing polars to ensure that only valid data is used in the
   * comparison.
   *
   * Note that extremely light wind (0 knots) and extremely strong wind (maxVW,
   * typically 40 knots) are treated as special cases where no polar can claim
   * to be "fastest".
   *
   * @param p The index of the polar to test against all others
   * @param H Heading relative to true wind in degrees (0-360)
   * @param VW True wind speed in knots
   *
   * @return true if the specified polar is the fastest for these conditions,
   * false if:
   *         - The wind speed is 0 or at maximum (40 knots by default)
   *         - The polar produces invalid or negative boat speed for these
   * conditions
   *         - Any other polar produces a faster boat speed for these conditions
   */
  bool FastestPolar(int p, float H, float VW);
  /**
   * Generates CrossOverRegion and StandaloneRegion polygons for each polar in
   * the boat configuration.
   *
   * Builds two types of regions for each polar (sail configuration):
   * 1. CrossOverRegion: Defines wind conditions where that particular sail
   *    configuration is optimal compared to all other available sails
   * 2. StandaloneRegion: Defines all wind conditions where the sail can
   *    provide meaningful boat speed, regardless of other sails' performance
   *
   * CrossOverRegions are used during routing to automatically select the
   * most appropriate sail for the current conditions. StandaloneRegions are
   * useful for what-if scenarios and analysis when specific sails become
   * unavailable.
   *
   * The algorithm works by:
   * 1. Creating a grid of wind angle (H) vs. wind speed (VW) points
   * 2. For CrossOverRegion: Testing each point to see if the current polar is
   * optimal
   * 3. For StandaloneRegion: Testing each point to see if the polar can sail
   * 4. Finding the boundaries where conditions change
   * 5. Constructing polygons that enclose the respective conditions
   * 6. Simplifying the polygons to improve performance
   *
   * The grid uses a resolution of 1/8 knot for wind speed and 1/8 degree for
   * wind angle, allowing for precise definition of boundaries while
   * maintaining reasonable computational performance.
   *
   * @param arg User-defined argument to pass to the status callback function
   * @param status Optional callback function that reports progress (nullptr for
   * no progress reporting) The callback receives: the user argument, current
   * polar index, and total polar count
   */
  void GenerateCrossOverChart(void* arg = 0,
                              void (*status)(void*, int, int) = 0);

  /**
   * Finds the most suitable polar (sail configuration) for the given weather
   * and sailing conditions.
   *
   * The selection is based on the "CrossOverContour" for each polar, which
   * defines the valid ranges of wind speeds and angles for that particular sail
   * configuration. In sailing, different sail configurations (e.g., full main +
   * jib, reefed main, spinnaker, etc.) perform optimally in different
   * conditions. Helps the routing algorithm automatically select
   * the appropriate sail configuration based on the current wind and course.
   *
   * Follows a multi-step process to select the optimal polar.
   * 1. Check if the current polar is still valid for the conditions. If so,
   *    it's retained for continuity.
   * 2. If current polar is invalid, tries to find any suitable polar from the
   *    available set.
   * 3. If no polar meets the conditions perfectly, implements a fallback
   *    strategy based on the specific limitation:
   *    - When wind is too light, selects the polar with minimum wind speed
   *      requirements closest to the current wind.
   *    - When sailing too close to the wind, finds polars that can sail at the
   *      lowest angles. This could happen while tacking.
   *    - When sailing too far downwind, finds polars that can handle the
   *      highest angles. This could happen while jibing.
   *
   * Each candidate polar receives a score based on its suitability, with the
   * current polar given a slight preference (10% bonus) if it's a viable
   * option. Find the best polar for the weather conditions.
   *
   * @param curpolar Index of the current polar being used, or -1 if no polar is
   * selected yet
   * @param tws True Wind Speed (TWS) in knots
   * @param twa True Wind Angle (TWA), i.e., heading relative to true wind
   * direction in degrees.
   * @param swell Swell height in meters (reserved for future use, currently not
   * used in selection)
   * @param optimize_tacking Whether to optimize the tacking angle for upwind
   * sailing
   * @param status Pointer to a PolarSpeedStatus variable to receive detailed
   * status information. If nullptr, status details are not returned. On
   * success, *status is set to POLAR_SPEED_SUCCESS.
   *
   * @return The index of the valid polar to use, or -1 if no valid polar was
   * found. If curpolar is still valid, it will be returned to avoid unnecessary
   * sail changes. If curpolar is invalid but another polar is valid, the index
   * of that polar is returned. If no valid polar is found, -1 is returned,
   * indicating that sailing is not possible with the available sail
   * configurations in the given conditions.
   */
  int FindBestPolarForCondition(int curpolar, double tws, double twa,
                                double swell, bool optimize_tacking,
                                PolarSpeedStatus* status = nullptr);

private:
  /**
   * Determines if a polar can provide meaningful boat speed for given
   * conditions.
   *
   * Checks if the polar has valid data for the specified wind
   * angle and wind speed, and whether it produces positive boat speed. Unlike
   * FastestPolar(), this function only considers the polar itself, not its
   * performance relative to other polars.
   *
   * @param p Index of the polar to test
   * @param H Wind angle in degrees (0-180)
   * @param VW Wind speed in knots
   * @return true if the polar can provide meaningful speed for these conditions
   */
  bool PolarCanSail(int p, float H, float VW);

  /**
   * Generates the standalone operational envelope for a specific polar.
   *
   * Creates a polygon region representing all wind conditions where the
   * specified polar can provide meaningful boat speed, regardless of other
   * available polars. This is used for what-if analysis scenarios.
   *
   * @param p Index of the polar to analyze
   * @param arg User-defined argument to pass to status callback
   * @param status Optional progress callback function
   */
  void GenerateStandaloneRegion(int p, void* arg = 0,
                                void (*status)(void*, int, int) = 0);

  Point Interp(const Point& p0, const Point& p1, int q, bool q0, bool q1);
  void NewSegment(Point& p0, Point& p1, std::list<Segment>& segments);
  void GenerateSegments(float H, float VW, float step, bool q[4],
                        std::list<Segment>& segments, int p);

  wxString m_last_filename;
  wxDateTime m_last_filetime;
};

#endif
