/***************************************************************************
 *   Copyright (C) 2016 by Sean D'Epagnier                                 *
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

#ifndef _WEATHER_ROUTING_POLAR_H_
#define _WEATHER_ROUTING_POLAR_H_

#include <vector>
#include "PolygonRegion.h"
#include <wx/wx.h>

/**
 * Stores optimal sailing angles for best Velocity Made Good (VMG) in different
 * sailing directions. VMG represents the component of the boat's velocity in
 * the upwind or downwind direction.
 */
struct SailingVMG {
  /** Indexes for the values array representing different sailing directions. */
  enum {
    STARBOARD_UPWIND,    //!< Sailing upwind on starboard tack (0-90 degrees)
    PORT_UPWIND,         //!< Sailing upwind on port tack (270-360 degrees)
    STARBOARD_DOWNWIND,  //!< Sailing downwind on starboard tack (90-180
                         //!< degrees)
    PORT_DOWNWIND        //!< Sailing downwind on port tack (180-270 degrees)
  };
  /**
   * Optimal sailing angles (in degrees) for maximum VMG in each sailing
   * direction. These values represent the wind angles that produce the best
   * progress toward or away from the wind direction.
   */
  float values[4];
};

/**
 * Represents a single performance measurement of a boat at specific wind
 * conditions. Used for generating or refining polar diagrams from real-world or
 * theoretical measurements.
 */
struct PolarMeasurement {
  /**
   * Constructor for creating a new measurement.
   *
   * @param windSpeed Wind speed (apparent or true depending on 'apparent'
   * parameter)
   * @param windAngle Wind angle (apparent or true depending on 'apparent'
   * parameter)
   * @param boatSpeed Boat speed in knots, as observed in the measurement.
   * @param apparent If true, v and d are apparent wind values; if false, they
   * are true wind values
   */
  PolarMeasurement(double windSpeed, double windAngle, double boatSpeed,
                   bool apparent = true);
  /**
   * Calculate the true wind speed for this measurement.
   *
   * @return True wind speed in knots
   */
  double getTWS() const;
  /**
   * Calculate the true wind angle for this measurement.
   *
   * @return True wind angle in degrees
   */
  double getTWA() const;
  double aws;  //!< Apparent wind speed in knots
  double awa;  //!< Apparent wind angle in degrees
  /** Boat speed (Speed Through Water) in knots, as observed in the measurement.
   */
  double stw;
  /**
   * This parameter represents the relationship between the apparent wind
   * conditions and the resulting boat speed. Lower eta values indicate more
   * efficient sailing performance (better conversion of wind energy to boat
   * speed).
   */
  double eta;
};

class Boat;

#define DEGREES 360

/**
 * Enumeration of error codes that can be returned by the Speed() function.
 * Used to provide detailed information about why a speed calculation failed.
 */
enum PolarSpeedStatus {
  /** Calculation was successful */
  POLAR_SPEED_SUCCESS = 0,
  /** Wind speed is negative */
  POLAR_SPEED_NEGATIVE_WINDSPEED,
  /** No polar data available (empty degree_steps or wind_speeds) */
  POLAR_SPEED_NO_POLAR_DATA,
  /** Wind angle is too low (below minimum defined in polar) */
  POLAR_SPEED_ANGLE_TOO_LOW,
  /** Wind angle is too high (above minimum defined in polar) */
  POLAR_SPEED_ANGLE_TOO_HIGH,
  /** Wind speed is too low (below minimum defined in polar) */
  POLAR_SPEED_WIND_TOO_LIGHT,
  /** Wind speed is too high (above maximum defined in polar) */
  POLAR_SPEED_WIND_TOO_STRONG,
  /** Calculation resulted in negative boat speed (likely due to faulty polar
     data) */
  POLAR_SPEED_NEGATIVE_RESULT,
  /** Invalid sail configuration. */
  POLAR_SPEED_INVALID_SAIL_CONFIGURATION
};

class Polar {
public:
  /**
   * Calculates the Apparent Wind Speed (AWS).
   *
   * This function computes the Apparent Wind Speed (AWS) that a vessel
   * experiences, based on the vessel's speed through water and the
   * water-relative wind.
   * The calculation uses the law of cosines: c² = a² + b² * - 2ab·cos(C)
   *
   * @param stw Speed Through Water - the vessel's speed relative to the water
   * in knots.
   * @param relativeWindAngle The angle between the vessel's Course Through
   * Water (CTW) and the True Wind Direction (relative to water) in degrees.
   * @param waterWindSpeed Wind speed relative to the water's frame of reference
   * in knots
   *
   * @return Apparent Wind Speed (AWS) in knots
   */
  static double VelocityApparentWind(double stw, double relativeWindAngle,
                                     double waterWindSpeed);
  /**
   * Calculates the apparent wind angle from true wind and boat velocity
   * vectors.
   *
   * Uses the law of cosines to solve for the apparent wind angle. The apparent
   * wind results from the vector addition of true wind and boat velocity
   * vectors.
   *
   * @param aws Apparent wind speed in knots
   * @param stw Boat speed (Speed Through Water) in knots
   * @param W True wind angle in degrees
   * @param VW True wind speed in knots
   * @return Apparent wind angle in degrees:
   *         - Returns 0 if aws is 0 (apparent wind direction undefined)
   *         - Returns W if stw is 0 (no boat motion, apparent = true wind)
   *         - Otherwise returns calculated angle accounting for both vectors
   */
  static double DirectionApparentWind(double aws, double stw, double W,
                                      double VW);
  /**
   * Calculates the apparent wind angle from true wind and boat velocity
   * vectors.
   *
   * @param stw Boat speed (Speed Through Water) in knots
   * @param W True wind angle in degrees
   * @param VW True wind speed in knots
   * @return Apparent wind angle in degrees
   */
  static double DirectionApparentWind(double stw, double W, double VW);
  /**
   * Calculates true wind speed using the law of cosines given apparent wind and
   * true wind angle.
   *
   * @param aws Apparent wind speed in knots
   * @param stw Boat speed (Speed Through Water) in knots
   * @param W True wind angle in degrees
   * @return True wind speed in knots
   * @note May sometimes have two mathematically possible solutions
   */
  static double VelocityTrueWind(double aws, double stw, double W);
  /**
   * Alternative method to calculate true wind speed using apparent wind angle
   * instead of true wind angle.
   *
   * Uses the law of cosines in a different configuration than
   * VelocityTrueWind().
   *
   * @param aws Apparent wind speed in knots
   * @param stw Boat speed (Speed Through Water) in knots
   * @param A Apparent wind angle in degrees
   * @return True wind speed in knots
   */
  static double VelocityTrueWind2(double aws, double stw, double A);

  /**
   * Get a human-readable, translatable message for a polar speed status code.
   *
   * This helper method converts a PolarSpeedStatus code into a user-friendly
   * message that can be displayed in the UI. All messages are wrapped in _()
   * for translation support.
   *
   * @param status The status code to convert to a message.
   * @return wxString containing the translated status message.
   */
  static wxString GetPolarStatusMessage(PolarSpeedStatus status);

  Polar();

  bool Open(const wxString& filename, wxString& message);
  bool Save(const wxString& filename);

  wxString FileName;

  void OptimizeTackingSpeeds();
  /**
   * Finds the closest wind speed indices in the polar data for a given wind
   * speed.
   *
   * This function searches through the wind_speeds vector to find the indices
   * of the two wind speed entries that bracket the given wind speed (VW). These
   * indices can be used for interpolation between wind speed data points.
   *
   * @param VW The true wind speed to find brackets for, in knots
   * @param[out] VW1i Reference to variable that will receive the lower bracket
   * index
   * @param[out] VW2i Reference to variable that will receive the upper bracket
   * index
   *
   * @note: if VW is less than the first wind speed in the table, VW1 is set to
   * 0 and VW2i is set to 1.
   * @note If VW is greater than the last wind speed in the table, VW1i is set
   * to the index of the last wind speed and VW2i is set to the index of the
   * last wind speed minus 1.
   */
  void ClosestVWi(double VW, int& VW1i, int& VW2i);

  /**
   * Calculate the boat speed based on the wind angle and wind speed using polar
   * data.
   *
   * The return value is strictly based on the polar data and does not consider
   * any other factors such as current. This is the theoretical maximum boat
   * speed for the given wind conditions.
   *
   * @param twa The True Wind Angle in degrees relative to the boat heading
   * (0° = head to wind, 90° = beam reach, 180° = running). Values above 180°
   * are mirrored as the polar is assumed symmetric.
   * @param tws The True Wind Speed in knots.
   * @param status Pointer to a PolarSpeedStatus variable that will receive
   * detailed status code. If nullptr, status details are not returned. On
   * success, *status is set to POLAR_SPEED_SUCCESS.
   * @param bound If true, returns NAN when wind speed is outside the range
   * defined in the polar data. If false, extrapolates the boat speed when wind
   * speed is outside the polar data range.
   * @param optimize_tacking If true, calculates the optimal VMG angle for
   * upwind sailing and returns the corresponding speed projected onto the
   * requested course.
   * @return The boat speed in knots, or NAN if the angle is in a no-go zone or
   * other calculation constraints.
   */
  double Speed(double twa, double tws, PolarSpeedStatus* status = nullptr,
               bool bound = false, bool optimize_tacking = false);
  /**
   * Iteratively solves for boat speed given a target apparent wind direction.
   *
   * Uses an iterative approach to find the boat speed and true wind angle that
   * would result in the specified apparent wind angle. The solver adjusts boat
   * speed and recalculates apparent wind until convergence.
   *
   * @param awa Target apparent wind angle in degrees
   * @param tws True wind speed in knots
   * @param pTWA Optional pointer to store the resulting true wind angle in
   * degrees
   * @return Boat speed in knots, or NAN if no convergence after multiple
   * iterations.
   */
  double SpeedAtApparentWindDirection(double awa, double tws, double* pTWA = 0);
  /**
   * Iteratively solves for boat speed given a target apparent wind speed.
   *
   * Uses an iterative approach to find the boat speed and true wind speed that
   * would result in the specified apparent wind speed at the given true wind
   * angle.
   *
   * @param twa True Wind Angle in degrees.
   * @param aws Target apparent wind speed in knots.
   * @return Boat speed in knots, or NAN if no convergence after multiple
   * iterations.
   */
  double SpeedAtApparentWindSpeed(double twa, double aws);
  /**
   * Iteratively solves for boat speed given both apparent wind angle and speed.
   *
   * Uses an iterative approach to find the boat speed and true wind conditions
   * that would result in both the specified apparent wind angle and speed. This
   * combines the functionality of SpeedAtApparentWindDirection and
   * SpeedAtApparentWindSpeed.
   *
   * @param awa Target apparent wind angle in degrees
   * @param aws Target apparent wind speed in knots
   * @param pTWA Optional pointer to store the resulting true wind angle in
   * degrees
   * @return Boat speed in knots, or NAN if no convergence after multiple
   * iterations.
   */
  double SpeedAtApparentWind(double awa, double aws, double* pTWA = 0);

  /**
   * Gets the smallest True Wind Angle (relative to boat heading) in the polar
   * data.
   *
   * This represents the closest angle to directly upwind that has performance
   * data, typically defining the minimum pointing angle (tacking angle) for the
   * boat.
   *
   * @return The minimum True Wind Angle in degrees from the polar data.
   */
  double MinDegreeStep() { return degree_steps[0]; }

  /**
   * Gets optimal VMG angles for a given true wind speed.
   *
   * Interpolates between the pre-calculated VMG angles in the polar data
   * to find optimal angles for the specified wind speed.
   *
   * @param tws The true wind speed in knots
   * @return SailingVMG containing optimal angles for each point of sail:
   *         - STARBOARD_UPWIND: Best upwind angle on starboard
   *         - PORT_UPWIND: Best upwind angle on port
   *         - STARBOARD_DOWNWIND: Best downwind angle on starboard
   *         - PORT_DOWNWIND: Best downwind angle on port
   *         Values will be NAN if wind speed is outside polar range
   */
  SailingVMG GetVMGTrueWind(double tws);
  /**
   * Calculates optimal VMG angles for a given apparent wind speed.
   *
   * For each of the four modes (upwind/downwind on port/starboard),
   * iteratively finds the true wind conditions that would result in
   * the specified apparent wind speed and give the best VMG.
   *
   * @param aws The apparent wind speed in knots
   * @return SailingVMG containing optimal angles for each point of sail:
   *         - STARBOARD_UPWIND: Best upwind angle on starboard
   *         - PORT_UPWIND: Best upwind angle on port
   *         - STARBOARD_DOWNWIND: Best downwind angle on starboard
   *         - PORT_DOWNWIND: Best downwind angle on port
   *         Values will be NAN if no valid solution found
   */
  SailingVMG GetVMGApparentWind(double aws);

  /**
   * Calculates the true wind speed given boat speed and true wind angle.
   *
   * This function solves for the true wind speed that would result in the
   * given boat speed at the specified true wind angle. It handles cases where
   * the polar may be inverted at high wind speeds (where higher wind speeds
   * result in lower boat speeds).
   *
   * @param stw Boat speed (Speed Through Water) in knots
   * @param W True wind angle in degrees
   * @param maxVW Maximum true wind speed to consider in knots
   * @return The calculated true wind speed in knots, or NAN if no solution
   * found
   */
  double TrueWindSpeed(double stw, double W, double maxVW);
  bool InterpolateSpeeds();
  void UpdateSpeeds();
  void UpdateDegreeStepLookup();

  /**
   * Determines if the current sailing state is within the crossover contour.
   *
   * This function checks if the given True Wind Angle (TWA) and True Wind Speed
   * (TWS) combination falls within the defined crossover region. The crossover
   * region typically represents conditions where a sail change would be
   * beneficial.
   *
   * If optimize_tacking is true, the function first applies VMG optimization
   * to the heading angle before checking the region.
   *
   * The function normalizes the heading angle to the 0-180 degree range for
   * symmetric polars and handles special cases like zero wind speed.
   *
   * @param twa True Wind Angle (TWA), i.e., heading angle in degrees relative
   * to the true wind.
   * @param tws The True Wind Speed (TWS) in knots
   * @param optimize_tacking If true, optimize the heading angle for VMG before
   * checking
   * @param status Pointer to a PolarSpeedStatus variable to receive detailed
   * status information. If nullptr, status details are not returned. On
   * success, *status is set to POLAR_SPEED_SUCCESS.
   * @return true if the sailing state is inside the crossover contour, false
   * otherwise
   */
  bool InsideCrossOverContour(float twa, float tws, bool optimize_tacking,
                              PolarSpeedStatus* status = nullptr);
  PolygonRegion CrossOverRegion;

  void Generate(const std::list<PolarMeasurement>& measurements);
  void AddDegreeStep(double twa);
  void RemoveDegreeStep(int index);
  void AddWindSpeed(double tws);
  void RemoveWindSpeed(int index);

  double m_crossoverpercentage;

private:
  friend class EditPolarDialog;
  friend class BoatDialog;
  friend class Boat;

  /**
   * Calculate and store the optimal VMG angles for a given wind speed entry in
   * the polar.
   *
   * For each of the four sailing modes (upwind/downwind on port/starboard),
   * finds the wind angle that gives the best progress directly upwind or
   * downwind. Uses interpolation to find precise angles between polar data
   * points.
   *
   * For symmetric polars (where data only goes to 180°), automatically mirrors
   * the starboard values to port tack.
   *
   * @param VWi Index into wind_speeds array identifying which wind speed to
   * calculate VMGs for.
   */
  void CalculateVMG(int VWi);

  /**
   * Calculates the angle between the boat's heading and its course through
   * water (leeway angle).
   *
   * The leeway angle occurs due to the sideways force of the sail being
   * balanced by the keel/centerboard, causing the boat to move slightly
   * sideways while sailing.
   *
   * @param awa The apparent wind angle in radians
   * @param aws The apparent wind speed in knots
   * @return The leeway angle in radians
   *
   * @note The current implementation returns 0. This feature is not yet
   * implemented.
   */
  double AngleofAttackBoat(double awa, double aws);

  /**
   * Calculates the theoretical boat speed using the sailboat transform model.
   *
   * Uses a simplified physical model based on the sailboat transform equations:
   * stw = sin(awa/2) * sqrt(aws/eta)
   *
   * Where:
   * - stw is the speed through water
   * - awa is the apparent wind angle
   * - aws is the apparent wind speed
   * - eta is the efficiency coefficient (determines how much of the wind energy
   * converts to boat speed)
   *
   * For wing-on-wing sailing (running downwind with sails on opposite sides),
   * applies a speed bonus of up to 50% when sailing directly downwind.
   *
   * @param awa The apparent wind angle in radians
   * @param aws The apparent wind speed in knots
   * @return The calculated boat speed in knots, or 0 if:
   *         - The apparent wind angle is less than the luffing angle (sails
   * flapping)
   *         - The efficiency coefficient (eta) is <= 0
   */
  double VelocityBoat(double awa, double aws);

  /**
   * Stores boat performance data for a specific wind speed.
   */
  struct SailingWindSpeed {
    SailingWindSpeed(double nVW) : tws(nVW) {
      VMG.values[0] = VMG.values[1] = VMG.values[2] = VMG.values[3] = NAN;
    }

    /** True wind speed in knots. */
    float tws;
    /** Boat speeds indexed by wind angle (matching degree_steps array). */
    std::vector<float> speeds;
    /** Original boat speeds from polar file before any modifications. (by
     * degree_count) */
    std::vector<float> orig_speeds;
    /** Optimal VMG values for upwind and downwind sailing at this wind speed.
     */
    SailingVMG VMG;
  };  // num_wind_speeds

  /**
   * Calculate optimal sailing angle for best Velocity Made Good (VMG) at a
   * given wind speed.
   *
   * This function determines if a better VMG can be achieved by sailing at a
   * different angle than the requested course. If so, it modifies the wind
   * angle (W) to the optimal VMG angle. The Speed() function then uses this to
   * calculate the projected boat speed on the original course.
   *
   * @param ws1 Lower wind speed bracket from polar data
   * @param ws2 Upper wind speed bracket from polar data
   * @param VW Target true wind speed to interpolate for
   * @param W Reference to wind angle (modified if better VMG angle found)
   * @return true if a better VMG angle was found and W was modified, false
   * otherwise
   */
  bool VMGAngle(SailingWindSpeed& ws1, SailingWindSpeed& ws2, float VW,
                float& W);

  /**
   * Stores boat performance data at different wind speeds.
   * - The vector contains multiple SailingWindSpeed entries, each representing
   *   boat performance at a specific true wind speed (tws).
   * - Entries are ordered by increasing wind speed (from lowest to highest).
   */
  std::vector<SailingWindSpeed> wind_speeds;
  /**
   * Stores the wind angles (in degrees) for which boat performance data
   * is available in the polar diagram.
   *
   * Structure:
   * - Contains wind angles in ascending order
   * - Each entry corresponds to a column in the polar table
   * - Ranges from the minimum pointing angle (close-hauled) to running downwind
   */
  std::vector<double> degree_steps;
  unsigned int degree_step_index[DEGREES];
};

#endif
