/***************************************************************************
 *
 * Project:  OpenCPN Weather Routing plugin
 * Author:   Sean D'Epagnier
 *
 ***************************************************************************
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
 ***************************************************************************
 *
 */
#include <cstdint>

#include <wx/wx.h>

#include <stdlib.h>
#include <math.h>

#include "AboutDialog.h"
#include "weather_routing_pi.h"
#include "WeatherRoutingWxCompat.h"

AboutDialog::AboutDialog(wxWindow* parent) : AboutDialogBase(parent) {
  //     m_stVersion->SetLabel(wxString::Format(_T("%d.%d"),PLUGIN_VERSION_MAJOR,
  //     PLUGIN_VERSION_MINOR));

  m_stVersion->SetLabel(wxString::Format(
      _T("%d.%d.%d"), PLUGIN_VERSION_MAJOR, PLUGIN_VERSION_MINOR,
      PLUGIN_VERSION_PATCH));

#ifdef __OCPN__ANDROID__
  // wxQt keeps the generated empty-label width after SetLabel(). Give the
  // version and the wrapped description enough room for their actual text.
  m_stVersion->SetMinSize(wxSize(WR_FromDIP(this, 125), -1));
  m_staticText110->Wrap(WR_FromDIP(this, 560));
  m_staticText110->SetMinSize(WR_FromDIP(this, wxSize(560, 380)));
  Layout();
  wxSize sz = ::wxGetDisplaySize();
  SetSize(0, 0, sz.x, sz.y - 40);
#endif
}

void AboutDialog::OnAboutAuthor(wxCommandEvent& event) {
  wxLaunchDefaultBrowser(_T(ABOUT_AUTHOR_URL));
}
