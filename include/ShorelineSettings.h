// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <wx/config.h>
#include <tinyxml.h>
namespace weather_routing {
inline int ParseShorelineResolution(const wxString& value, int fallback = 4) {
  long parsed;
  if (!value.ToLong(&parsed) || parsed < 0 || parsed > 4) return fallback;
  return static_cast<int>(parsed);
}
inline int ReadShorelineResolution(const TiXmlElement& e, int inherited, const char* key = "ShorelineResolution") {
  const char* value = e.Attribute(key);
  return value ? ParseShorelineResolution(wxString::FromUTF8(value)) : inherited;
}
inline int ReadShorelineResolution(wxConfigBase& c, int inherited, const char* key = "ShorelineResolution") {
  wxString value;
  return c.Read(key, &value) ? ParseShorelineResolution(value) : inherited;
}
}  // namespace weather_routing
