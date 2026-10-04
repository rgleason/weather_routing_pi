#pragma once

#include <wx/utils.h>
#ifdef __OCPN__ANDROID__
#include <QDesktopServices>
#include <QUrl>
#endif

inline bool WR_OpenBrowser(const wxString& url) {
#ifdef __OCPN__ANDROID__
  return QDesktopServices::openUrl(QUrl(QString::fromUtf8(url.ToUTF8().data())));
#else
  return wxLaunchDefaultBrowser(url);
#endif
}
