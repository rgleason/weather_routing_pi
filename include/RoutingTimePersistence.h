#pragma once

#include <wx/datetime.h>
#include "tinyxml.h"
#ifdef __OCPN__ANDROID__
#include <QDateTime>
#endif

namespace weather_routing {

// Keep the legacy local fields for old readers, and carry an unambiguous
// instant for current readers, including across timezone/device changes.
inline void WriteRoutingTime(TiXmlElement& element, const wxDateTime& instant,
                             const char* dateKey, const char* clockKey,
                             const char* epochKey) {
  if (!instant.IsValid()) return;
  element.SetAttribute(dateKey, instant.FormatISODate().ToUTF8());
  element.SetAttribute(clockKey, instant.FormatISOTime().ToUTF8());
  element.SetAttribute(epochKey, wxString::Format("%lld",
      static_cast<long long>(instant.GetTicks())).ToUTF8());
}

inline wxDateTime ReadRoutingTime(const TiXmlElement& element,
                                 const char* dateKey, const char* clockKey,
                                 const char* epochKey,
                                 const wxDateTime& fallback) {
  if (const char* epoch = element.Attribute(epochKey)) {
    wxLongLong_t seconds;
    if (wxString::FromUTF8(epoch).ToLongLong(&seconds)) {
      const wxDateTime value(static_cast<time_t>(seconds));
      if (value.IsValid() && static_cast<wxLongLong_t>(value.GetTicks()) == seconds)
        return value;
    }
  }
  const char* dateText = element.Attribute(dateKey);
  const char* clockText = element.Attribute(clockKey);
  if (!dateText) return fallback;
#ifdef __OCPN__ANDROID__
  // This wxQt build's ISO parser misapplies DST. The legacy fields were
  // formatted in local time, so let Qt interpret them in the device timezone.
  const auto value = QDateTime::fromString(QString::fromUtf8(dateText) + "T" +
      QString::fromUtf8(clockText ? clockText : "00:00:00"), Qt::ISODate);
  return value.isValid() ? wxDateTime(static_cast<time_t>(value.toSecsSinceEpoch()))
                         : fallback;
#else
  wxDateTime date;
  date.ParseISODate(wxString::FromUTF8(dateText));
  if (!date.IsValid()) return fallback;
  wxDateTime clock;
  if (clockText) clock.ParseISOTime(wxString::FromUTF8(clockText));
  if (clock.IsValid()) {
    date.SetHour(clock.GetHour());
    date.SetMinute(clock.GetMinute());
    date.SetSecond(clock.GetSecond());
  }
  return date;
#endif
}

}  // namespace weather_routing
