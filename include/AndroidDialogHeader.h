#pragma once

#ifdef __OCPN__ANDROID__

#include <wx/wx.h>

#include "WeatherRoutingWxCompat.h"

// wxQt does not display a dialog caption on Android. Modeless plugin dialogs
// therefore need an in-content way back to the routing window.
inline void WR_AddAndroidDoneHeader(wxDialog* dialog, const wxString& title) {
  wxSizer* content = dialog->GetSizer();
  if (!content) return;
  dialog->SetSizer(nullptr, false);

  wxBoxSizer* root = new wxBoxSizer(wxVERTICAL);
  wxBoxSizer* header = new wxBoxSizer(wxHORIZONTAL);
  header->Add(new wxStaticText(dialog, wxID_ANY, title),
              0, wxALIGN_CENTER_VERTICAL | wxALL, 8);
  wxButton* done = new wxButton(dialog, wxID_ANY, _("Done"));
  done->SetMinSize(wxSize(WR_FromDIP(dialog, 88), WR_FromDIP(dialog, 44)));
  done->Bind(wxEVT_BUTTON, [dialog](wxCommandEvent&) { dialog->Hide(); });
  header->Add(done, 0, wxALL, 5);
  root->Add(header, 0, wxEXPAND);
  root->Add(content, 1, wxEXPAND);
  dialog->SetSizer(root, true);
  dialog->Layout();
}

#endif
