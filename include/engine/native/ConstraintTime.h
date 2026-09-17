#ifndef WEATHER_ROUTING_ENGINE_NATIVE_CONSTRAINT_TIME_H
#define WEATHER_ROUTING_ENGINE_NATIVE_CONSTRAINT_TIME_H

#include <wx/datetime.h>

#include <optional>

namespace weather_routing::native {

inline std::optional<wxDateTime> ResolveConstraintTime(
    const wxDateTime& segment_time, const wxDateTime& departure_time) {
  if (segment_time.IsValid()) return segment_time;
  if (departure_time.IsValid()) return departure_time;
  return std::nullopt;
}

}  // namespace weather_routing::native

#endif  // WEATHER_ROUTING_ENGINE_NATIVE_CONSTRAINT_TIME_H
