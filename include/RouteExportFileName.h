#ifndef WEATHER_ROUTING_ROUTE_EXPORT_FILE_NAME_H
#define WEATHER_ROUTING_ROUTE_EXPORT_FILE_NAME_H

#include <wx/string.h>

namespace weather_routing {
// Generated GPX names start with WXRoute_. Keep user position names in the
// document, but prevent their punctuation from becoming filesystem syntax.
inline wxString RouteExportFileStem(const wxString& route_name) {
  const wxString forbidden = wxT("\"*/:<>?\\|");
  wxString stem;
  for (const wxUniChar ch : route_name)
    stem += ch.GetValue() < 32 || forbidden.Find(ch) != wxNOT_FOUND
                ? wxUniChar('_') : ch;
  stem.Trim();
  while (stem.EndsWith(".")) stem.RemoveLast();
  return stem.IsEmpty() ? wxString("WXRoute") : stem;
}
}  // namespace weather_routing
#endif
