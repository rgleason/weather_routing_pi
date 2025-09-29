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

#ifndef _WEATHER_ROUTING_POLYGON_REGION_H_
#define _WEATHER_ROUTING_POLYGON_REGION_H_

#include <string>
#include <list>
#include "tess.h"

/**
 * Represents a 2D point with x and y coordinates.
 */
struct Point {
  Point() {}
  Point(float _x, float _y) : x(_x), y(_y) {}
  float x, y;

  /**
   * Equality comparison operator for Point objects.
   *
   * @param point The point to compare with
   * @return true if both x and y coordinates are equal, false otherwise
   */
  bool operator==(const Point& point) const {
    return point.x == x && point.y == y;
  }
};

/**
 * Represents a line segment defined by two endpoints.
 */
struct Segment {
  Segment() { p[0] = Point(0, 0), p[1] = Point(0, 0); }
  Segment(const Point& p0, const Point& p1) { p[0] = p0, p[1] = p1; }
  /** Array of two points defining the segment endpoints */
  Point p[2];
};

/**
 * Represents a polygon contour as a sequence of connected vertices.
 *
 * A contour is a closed boundary that forms part of a polygon region.
 * It stores vertices as a flat array of coordinates (x1, y1, x2, y2, ...).
 */
struct Contour {
  /**
   * Copy constructor.
   *
   * @param contour The contour to copy from
   */
  Contour(const Contour& contour) { Init(contour.points, contour.n); }

  /**
   * Constructs a contour from a flat array of coordinates.
   *
   * @param p Pointer to array of coordinates (x1, y1, x2, y2, ...)
   * @param n Number of vertices (not coordinate pairs)
   */
  Contour(const float* p, int n) { Init(p, n); }

  /**
   * Constructs a contour from a list of Point objects.
   *
   * @param points List of Point objects defining the contour vertices
   */
  Contour(const std::list<Point>& points);

  ~Contour() { delete[] points; }

  /**
   * Assignment operator.
   *
   * @param contour The contour to assign from
   * @return Reference to this contour
   */
  Contour operator=(const Contour& contour) {
    delete[] points;
    Init(contour.points, contour.n);
    return *this;
  }

  /**
   * Initializes the contour from a coordinate array.
   *
   * @param p Pointer to array of coordinates
   * @param c Number of vertices
   */
  void Init(const float* p, int c);

  /**
   * Determines if the contour is counter-clockwise oriented.
   *
   * Uses the shoelace formula to calculate the signed area.
   * A negative area indicates counter-clockwise orientation.
   *
   * @return true if the contour is counter-clockwise, false otherwise
   */
  bool CCW();

  /**
   * Reverses the order of vertices in the contour.
   *
   * Changes clockwise to counter-clockwise orientation and vice versa.
   */
  void Reverse();

  /**
   * Calculates the absolute area of the contour.
   *
   * Uses the shoelace formula to compute the polygon area.
   * Always returns a positive value regardless of orientation.
   *
   * @return Absolute area of the contour
   */
  float Area() const;

  /**
   * Simplifies the contour by removing vertices that don't significantly
   * contribute to the shape.
   *
   * Uses a cross-product based algorithm to identify vertices that lie
   * approximately on the line between their neighbors.
   *
   * @param epsilon Tolerance for vertex removal (default: 1e-6)
   */
  void Simplify(float epsilon = 1e-6);

  /** Array of vertex coordinates stored as (x1, y1, x2, y2, ...) */
  float* points;
  /** Number of vertices in the contour */
  int n;
};

/**
 * Represents a complex polygon region composed of multiple contours.
 *
 * A polygon region can contain multiple disjoint polygons and holes.
 * It supports boolean operations (union, intersection, subtraction) and
 * point-in-polygon testing. Commonly used for geographic regions and
 * sailing polar diagrams where coordinates represent wind angles and speeds.
 *
 * @section hole_semantics Hole Semantics
 * Holes are supported through contour orientation and winding number algorithm:
 * - Outer boundaries should be counter-clockwise (CCW)
 * - Holes should be clockwise (CW)
 * - Contains() uses winding number: odd crossings = inside, even = outside
 * - Point inside both outer boundary and hole = even crossings = outside
 *
 * @section orientation Contour Orientation
 * Single-contour constructors automatically correct orientation to CCW.
 * Multi-contour regions preserve orientation, enabling proper hole
 * representation.
 *
 * @section serialization Serialization Format
 * String format: "x1,y1,x2,y2,;x3,y3,x4,y4,;" where:
 * - Semicolons (;) separate contours
 * - Commas (,) separate coordinates
 * - Orientation is preserved (critical for holes)
 */
class PolygonRegion {
public:
  /**
   * Default constructor creating an empty polygon region.
   */
  PolygonRegion() { InitMem(); }

  /**
   * Constructs a polygon region from a single convex or concave polygon.
   *
   * Creates a single-contour polygon region. The polygon should not be
   * self-intersecting. The contour orientation is automatically corrected
   * to counter-clockwise if needed.
   *
   * @param n Number of vertices
   * @param points Array of vertex coordinates (x1, y1, x2, y2, ...)
   */
  PolygonRegion(int n, float* points);

  /**
   * Constructs a polygon region from a collection of line segments.
   *
   * Attempts to connect the segments into closed contours. Segments that
   * cannot be connected into closed polygons are discarded with a warning.
   * This is useful for creating polygons from edge data.
   *
   * @param segments List of line segments to connect into contours
   */
  PolygonRegion(std::list<Segment>& segments);
  /**
   * Constructs a PolygonRegion from a serialized string representation.
   *
   * Parses a string containing polygon contour data. See class documentation
   * for detailed format specification and hole semantics.
   *
   * @param str Serialized polygon data string
   *
   * @note Each contour must contain an even number of coordinate values (x,y
   * pairs)
   * @note Empty contours between consecutive semicolons are ignored
   * @note When used for polars: x = TWA (degrees), y = TWS (knots)
   *
   * @see toString() for the inverse operation
   * @see @ref serialization "Serialization Format" in class documentation
   * @see @ref hole_semantics "Hole Semantics" in class documentation
   */
  PolygonRegion(const std::string& str);

  ~PolygonRegion() { FreeMem(); }

  /**
   * Removes all contours from the polygon region.
   */
  void Clear() { contours.clear(); }

  /**
   * Checks if the polygon region contains any contours.
   *
   * @return true if the region has no contours, false otherwise
   */
  bool Empty() const { return contours.empty(); }

  /**
   * Prints all contour vertices to stdout for debugging.
   *
   * Each contour is printed as a series of coordinate pairs,
   * separated by blank lines between contours.
   */
  void Print();

  /**
   * Serializes the polygon region to a string representation.
   *
   * @return String representation with format "x1,y1,x2,y2,;x3,y3,x4,y4,;"
   *
   * @note Contour orientation is preserved (critical for holes)
   *
   * @see @ref serialization "Serialization Format" in class documentation
   */
  std::string toString();

  /**
   * Checks if a point is inside the polygon region.
   *
   * Uses winding number algorithm to handle complex regions with holes.
   * Points on the boundary are considered inside.
   *
   * @param x The x-coordinate of the point to check
   * @param y The y-coordinate of the point to check
   * @return true if the point is inside the region, false otherwise
   *
   * @see @ref hole_semantics "Hole Semantics" in class documentation
   */
  bool Contains(float x, float y);

  /**
   * Computes the intersection of this region with another region.
   *
   * Modifies this polygon region to contain only the areas that are
   * present in both regions. Uses tessellation with ABS_GEQ_TWO winding rule.
   *
   * @param region The region to intersect with
   */
  void Intersect(PolygonRegion& region);

  /**
   * Computes the union of this region with another region.
   *
   * Modifies this polygon region to contain all areas that are present
   * in either region. Uses tessellation with POSITIVE winding rule.
   *
   * @param region The region to union with
   */
  void Union(PolygonRegion& region);

  /**
   * Subtracts another region from this region.
   *
   * Modifies this polygon region to contain only the areas that are
   * present in this region but not in the other region.
   *
   * @param region The region to subtract from this region
   */
  void Subtract(PolygonRegion& region);

  /**
   * Removes tiny sub-regions (holes or regular) from the polygon region.
   */
  void RemoveTinySubRegions();

  /**
   * Simplifies all contours in the polygon region.
   *
   * Removes vertices that don't significantly contribute to the shape
   * of each contour. Contours with fewer than 3 vertices after
   * simplification are removed entirely.
   *
   * @param epsilon Tolerance for vertex removal (default: 1e-6)
   * @param remove_small_holes If true, removes tiny holes or regular
   * sub-regions based on area threshold
   */
  void Simplify(float epsilon = 1e-6, bool remove_small_holes = true);

  /**
   * Creates a tessellation of the polygon region.
   *
   * Converts the polygon region into a tessellated representation suitable
   * for rendering or further geometric processing.
   *
   * @param triangles If true, tessellates into triangles; if false,
   *                  tessellates into boundary contours
   * @return Pointer to TESStesselator object, or NULL on failure.
   *         Caller is responsible for deleting the returned tessellator.
   */
  TESStesselator* Tesselate(bool triangles);

private:
  /**
   * Performs boolean operations between this region and another region.
   *
   * Internal method that handles union, intersection, and subtraction
   * operations using tessellation with specified winding rules.
   *
   * @param region The region to combine with this region
   * @param winding_rule The tessellation winding rule to apply
   * @param reverse If true, reverses the second region's contours
   */
  void Put(const PolygonRegion& region, int winding_rule, bool reverse);

  /**
   * Adds all contours from this region to a tessellator.
   *
   * Helper method for tessellation operations. Optionally reverses
   * contour orientation for subtraction operations.
   *
   * @param tess Pointer to the tessellator object
   * @param reverse If true, reverses contour orientation
   */
  void PutContours(TESStesselator* tess, bool reverse) const;

  /**
   * Initializes memory management fields to default values.
   */
  void InitMem() { memsize = 0, mem = nullptr; }

  /**
   * Allocates memory for tessellation operations.
   */
  void AllocateMem();
  void FreeMem() { /*delete [] mem;*/ }

  /** List of contours that make up the polygon region. */
  std::list<Contour> contours;
  /** Memory buffer for tessellation operations. */
  unsigned char* mem;
  /** Size of the allocated memory buffer. */
  int memsize;
};

#endif
