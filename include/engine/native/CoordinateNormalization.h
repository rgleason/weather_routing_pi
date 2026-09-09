#ifndef WEATHER_ROUTING_ENGINE_NATIVE_COORDINATE_NORMALIZATION_H
#define WEATHER_ROUTING_ENGINE_NATIVE_COORDINATE_NORMALIZATION_H

#include "supercpn/weather_routing/Engine.h"

namespace weather_routing::native {

/**
 * Convert a plugin route endpoint to the canonical longitude convention used
 * by the native engine.
 *
 * RouteMapConfiguration may deliberately store western-Pacific longitudes in
 * the legacy 0..360-degree form so legacy isochrone geometry does not wrap at
 * the antimeridian.  The native engine contract is -180..180 degrees, so the
 * adapter must normalise at this boundary rather than rejecting a valid
 * endpoint such as Palmerston or Niue.
 */
inline supercpn::weather_routing::GeoPoint NativeEnginePoint(double latitude,
                                                             double longitude) {
  return {latitude, supercpn::weather_routing::normalizeLongitude(longitude)};
}

}  // namespace weather_routing::native

#endif
