// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <wx/config.h>
#include <tinyxml.h>
#include <algorithm>
#include <wx/intl.h>
#include "ShorelineSpec.h"
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
// A passage owns one safety resolution. Older files stored three independent
// method choices: retain the finest explicitly saved choice on first import.
// An explicit new choice wins, including Crude; invalid values retain Full's
// fail-closed behavior. The chart-authoritative scout is deliberately separate.
template <typename Reader>
int ReadPassageShorelineResolutionWith(Reader read, int inherited) {
  wxString value;
  if (read("PassageShorelineResolution", value))
    return ParseShorelineResolution(value);
  int finest = -1;
  for (const char* key : {"ShorelineResolution", "QuickShorelineResolution",
                          "OriginalShorelineResolution"})
    if (read(key, value)) finest = std::max(finest, ParseShorelineResolution(value));
  return finest < 0 ? inherited : finest;
}
inline int ReadPassageShorelineResolution(const TiXmlElement& e, int inherited) {
  return ReadPassageShorelineResolutionWith([&](const char* key, wxString& out) {
    const char* value = e.Attribute(key);
    if (!value) return false;
    out = wxString::FromUTF8(value);
    return true;
  }, inherited);
}
inline int ReadPassageShorelineResolution(wxConfigBase& c, int inherited) {
  return ReadPassageShorelineResolutionWith([&](const char* key, wxString& out) {
    return c.Read(key, &out);
  }, inherited);
}
template <typename Writer>
void WritePassageShorelineResolutionWith(int resolution, Writer write) {
  // Mirror the old keys so reopening in an older plugin retains the same
  // shoreline protection for every method rather than resurrecting old values.
  for (const char* key : {"PassageShorelineResolution", "ShorelineResolution",
                          "QuickShorelineResolution", "OriginalShorelineResolution"})
    write(key, resolution);
}
inline void WritePassageShorelineResolution(TiXmlElement& e, int resolution) {
  WritePassageShorelineResolutionWith(resolution, [&](const char* key, int value) {
    e.SetAttribute(key, value);
  });
}
inline void WritePassageShorelineResolution(wxConfigBase& c, int resolution) {
  WritePassageShorelineResolutionWith(resolution, [&](const char* key, int value) {
    c.Write(key, value);
  });
}
inline wxString PassageShorelineSummary(int resolution, bool detect_land,
                                       bool chart_authoritative = false) {
  if (!detect_land) return _("Land avoidance off");
  const auto quality = wxGetTranslation(ShorelineSpecFor(resolution).quality);
  return chart_authoritative
      ? wxString::Format(_("Chart/depth checks enforced; GSHHG %s scout"), quality)
      : wxString::Format(_("GSHHG %s - all routing methods"), quality);
}
}  // namespace weather_routing
