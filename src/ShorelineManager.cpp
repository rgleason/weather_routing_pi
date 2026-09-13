// SPDX-License-Identifier: GPL-3.0-or-later
#include "ShorelineManager.h"
#include "ocpn_plugin.h"
#include "version.h"
#include <algorithm>
#include <stdexcept>
#include <wx/wx.h>
#include <wx/fileconf.h>
#include <wx/filename.h>
#include <wx/choice.h>
#include <wx/spinctrl.h>
#include <wx/progdlg.h>
#include <chrono>
#include <atomic>
#include <functional>

namespace weather_routing {
namespace {
struct Spec {
  const char* quality;
  const char* code;
  const char* hash;
  std::size_t bytes;
};
const Spec full{
    "Full", "f",
    "8d4d73897c82dd0e8df63f33e4dab9dd3aea7a26459b923bf404cb9299f1cf04",
    171582632};
const Spec high{
    "High", "h",
    "f833f23da2de4d2083b9a82f4fe9d2563c7ed1575a5a382c190150690d4d5a84",
    33435856};
std::atomic<bool> busy{false};
std::shared_ptr<ShorelineDataset> active;
wxString activeDescription;
std::filesystem::path Path(const wxString& p) {
  return std::filesystem::u8path(p.ToUTF8().data());
}
wxString Wx(const std::filesystem::path& p) {
  auto s = p.u8string();
  return wxString::FromUTF8(reinterpret_cast<const char*>(s.data()), s.size());
}
struct Config {
  wxFileConfig* c = GetOCPNConfigObject();
  wxString previous;
  Config() {
    previous = c->GetPath();
    c->SetPath("/PlugIns/WeatherRouting/Shoreline");
  }
  ~Config() { c->SetPath(previous); }
  wxString Read(const wxString& key, const wxString& fallback = "") {
    wxString out;
    c->Read(key, &out, fallback);
    return out;
  }
};
const Spec& Selected() {
  Config c;
  return c.Read("Resolution", "full") == "high" ? high : full;
}
std::size_t Budget() {
  Config c;
  long mib;
  c.c->Read("CacheMiB", &mib, 64);
  return std::clamp(mib, 16L, 256L) * 1024u * 1024u;
}
std::filesystem::path Root() {
  // Stock cores can keep their default private-data root even with --configdir.
  // Explicit override isolates automated tests and supports custom data
  // storage.
  wxString overridePath;
  if (wxGetEnv("WR_SHORELINE_DATA_HOME", &overridePath) &&
      !overridePath.empty())
    return Path(overridePath) / "2.3.7";
  return Path(*GetpPrivateApplicationDataLocation()) / "plugins" /
         "weather_routing" / "shoreline" / "2.3.7";
}
std::filesystem::path Archive(const Spec& s) {
  wxString name = wxString::Format("poly-%s-2.3.7.dat.gz", s.code);
  auto installed = Path(GetPluginDataDir(PLUGIN_PACKAGE_NAME)) / "data" /
                   "shoreline" / Path(name);
  if (std::filesystem::exists(installed)) return installed;
#ifdef WEATHER_ROUTING_SOURCE_DATA_DIR
  auto local = std::filesystem::u8path(WEATHER_ROUTING_SOURCE_DATA_DIR) /
               "shoreline" / Path(name);
  if (std::filesystem::exists(local)) return local;
#endif
  return installed;
}
wxString Key(const Spec& s) { return wxString::Format("Installed_%s", s.code); }
std::filesystem::path Installed(const Spec& s) {
  Config c;
  return Path(c.Read(Key(s)));
}
void Record(const Spec& s, const std::filesystem::path& p) {
  Config c;
  c.c->Write(Key(s), Wx(p));
  c.c->Flush();
}
std::filesystem::path Destination(const Spec& s) {
  const auto stamp =
      std::chrono::high_resolution_clock::now().time_since_epoch().count();
  return Root() /
         (std::string("poly-") + s.code + "-" + std::to_string(stamp) + ".dat");
}
std::shared_ptr<ShorelineDataset> Verify(const Spec& s,
                                         const std::filesystem::path& p) {
  if (p.empty() || !std::filesystem::exists(p) ||
      std::filesystem::file_size(p) != s.bytes || ShorelineSha256(p) != s.hash)
    throw std::runtime_error(
        "Shoreline data is missing or failed checksum verification. Open View "
        "/ Shoreline data to install or repair it.");
  return std::make_shared<ShorelineDataset>(p, Budget());
}
void InstallBundled() {
  auto p = Destination(full);
  InstallShorelineGzip(Archive(full), p, full.hash, full.bytes);
  Record(full, p);
}
std::vector<std::string> Mirrors(const Spec& s) {
  const std::string name = std::string("poly-") + s.code + "-2.3.7.dat.gz";
  return {
      "https://github.com/pob220/xweather_routing_pi/releases/download/"
      "gshhg-2.3.7/" +
          name,
      "https://github.com/pob220/weather_routing_pi/releases/download/"
      "gshhg-2.3.7/" +
          name};
}
void Download(wxWindow* parent, const Spec& s) {
  std::filesystem::create_directories(Root());
  auto destination = Destination(s);
  auto archive = destination;
  archive += ".download";
  const auto source = DownloadShorelineMirrors(
      Mirrors(s),
      [&](const std::string& url, const std::filesystem::path& output) {
        auto status = OCPN_downloadFile(
            wxString::FromUTF8(url.c_str()), Wx(output), _("Shoreline data"),
            wxString::Format(_("Downloading GSHHG 2.3.7 — %s resolution"),
                             s.quality),
            wxNullBitmap, parent,
            OCPN_DLDS_CAN_ABORT | OCPN_DLDS_AUTO_CLOSE |
                OCPN_DLDS_ELAPSED_TIME | OCPN_DLDS_REMAINING_TIME |
                OCPN_DLDS_SPEED | OCPN_DLDS_SIZE,
            120);
        return status == OCPN_DL_ABORTED    ? ShorelineDownloadResult::Cancelled
               : status == OCPN_DL_NO_ERROR ? ShorelineDownloadResult::Complete
                                            : ShorelineDownloadResult::Failed;
      },
      archive, destination, s.hash, s.bytes);
  Record(s, destination);
  wxLogMessage("WR_SHORELINE_INSTALLED source=%s quality=%s sha256=%s",
               source.c_str(), s.quality, s.hash);
}
}  // namespace
bool ShorelineManager::Busy() { return busy; }
std::shared_ptr<ShorelineDataset> ShorelineManager::Prepare() {
  if (busy)
    throw std::runtime_error(
        "Shoreline data management is open. Close it before computing a "
        "route.");
  if (active && active->Error().empty()) return active;
  active.reset();
  const auto& s = Selected();
  auto p = Installed(s);
  try {
    active = Verify(s, p);
  } catch (...) {
    if (&s != &full) throw;
    InstallBundled();
    p = Installed(s);
    active = Verify(s, p);
  }
  activeDescription = wxString::Format("GSHHG 2.3.7 / %s / SHA256 %s / %s",
                                       s.quality, s.hash, Wx(p));
  wxLogMessage("WR_SHORELINE_READY %s cache_mib=%llu", activeDescription,
               static_cast<unsigned long long>(Budget() / 1024 / 1024));
  return active;
}
wxString ShorelineManager::Description() { return activeDescription; }
void ShorelineManager::Show(wxWindow* parent) {
  if (busy.exchange(true)) return;
  struct Unlock {
    ~Unlock() { busy = false; }
  } unlock;
  wxDialog dialog(parent, wxID_ANY, _("Shoreline data"), wxDefaultPosition,
                  wxDefaultSize, wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER);
  auto main = new wxBoxSizer(wxVERTICAL);
  auto text =
      new wxStaticText(&dialog, wxID_ANY,
                       _("Land detection on standard OpenCPN uses the plugin's "
                         "own GSHHG data.\nFull resolution is included and "
                         "works offline. Chart-aware routing is unchanged."));
  text->Wrap(dialog.FromDIP(560));
  main->Add(text, 0, wxALL, 12);
  auto choice = new wxChoice(&dialog, wxID_ANY);
  choice->Append(_("Full — most detailed (164 MiB installed)"));
  choice->Append(_("High — smaller dataset (32 MiB installed)"));
  choice->SetSelection(&Selected() == &high ? 1 : 0);
  main->Add(choice, 0, wxEXPAND | wxLEFT | wxRIGHT, 12);
  auto status = new wxStaticText(&dialog, wxID_ANY, "");
  main->Add(status, 0, wxALL, 12);
  main->Add(new wxStaticText(&dialog, wxID_ANY, _("Installed file:")), 0,
            wxLEFT | wxRIGHT, 12);
  auto installedFile = new wxTextCtrl(&dialog, wxID_ANY, "", wxDefaultPosition,
                                      wxDefaultSize, wxTE_READONLY);
  installedFile->SetMinSize(dialog.FromDIP(wxSize(300, -1)));
  main->Add(installedFile, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 12);
  auto cacheLabel =
      new wxStaticText(&dialog, wxID_ANY,
                       _("Shoreline tile cache limit (MiB; resolution is never "
                         "reduced automatically):"));
  cacheLabel->Wrap(dialog.FromDIP(560));
  main->Add(cacheLabel, 0, wxLEFT | wxRIGHT | wxTOP, 12);
  auto cache = new wxSpinCtrl(&dialog, wxID_ANY);
  cache->SetRange(16, 256);
  cache->SetValue(int(Budget() / 1024 / 1024));
  main->Add(cache, 0, wxALL, 12);
  auto actions = new wxBoxSizer(wxHORIZONTAL);
  auto verify = new wxButton(&dialog, wxID_ANY, _("Verify installed data"));
  auto download = new wxButton(&dialog, wxID_ANY, _("Download and install"));
  auto restore = new wxButton(&dialog, wxID_ANY, _("Restore bundled full"));
  actions->Add(verify, 0, wxRIGHT, 6);
  actions->Add(download, 0, wxRIGHT, 6);
  actions->Add(restore);
  main->Add(actions, 0, wxALL, 12);
  auto updates =
      new wxButton(&dialog, wxID_ANY, _("Dataset release information..."));
  main->Add(updates, 0, wxLEFT | wxRIGHT | wxBOTTOM, 12);
  auto footer = dialog.CreateSeparatedButtonSizer(wxOK | wxCANCEL);
  main->Add(footer, 0, wxEXPAND | wxALL, 12);
  auto selected = [&]() -> const Spec& {
    return choice->GetSelection() == 1 ? high : full;
  };
  std::filesystem::path verifiedPath;
  auto refresh = [&]() {
    const auto& s = selected();
    auto p = Installed(s);
    status->SetLabel(wxString::Format(
        _("Approved dataset: GSHHG 2.3.7 — %s\n%s"), s.quality,
        (!p.empty() && std::filesystem::exists(p))
            ? (verifiedPath == p ? _("Verified SHA-256 and format")
                                 : _("Installed; verification runs before use"))
            : (&s == &full
                   ? _("Included in plugin; ready for offline installation")
                   : _("Not installed"))));
    installedFile->ChangeValue(Wx(p));
    installedFile->SetToolTip(Wx(p));
    status->Wrap(dialog.FromDIP(560));
    dialog.Layout();
  };
  auto action = [&](const std::function<void()>& work) {
    try {
      wxBusyCursor cursor;
      work();
      verifiedPath = Installed(selected());
      refresh();
      wxMessageBox(_("Shoreline data verified successfully."),
                   _("Shoreline data"), wxOK | wxICON_INFORMATION, &dialog);
    } catch (const std::exception& e) {
      wxMessageBox(wxString::FromUTF8(e.what()), _("Shoreline data"),
                   wxOK | wxICON_ERROR, &dialog);
    }
  };
  choice->Bind(wxEVT_CHOICE, [&](wxCommandEvent&) { refresh(); });
  verify->Bind(wxEVT_BUTTON, [&](wxCommandEvent&) {
    action([&]() { Verify(selected(), Installed(selected())); });
  });
  download->Bind(wxEVT_BUTTON, [&](wxCommandEvent&) {
    action([&]() { Download(&dialog, selected()); });
  });
  restore->Bind(wxEVT_BUTTON, [&](wxCommandEvent&) {
    action([&]() {
      InstallBundled();
      choice->SetSelection(0);
    });
  });
  updates->Bind(wxEVT_BUTTON, [&](wxCommandEvent&) {
    LaunchDefaultBrowser_Plugin(
        "https://github.com/chartcatalogs/gshhg/releases");
  });
  refresh();
  dialog.SetSizerAndFit(main);
  dialog.CentreOnScreen();
  // Validate before closing; a missing optional dataset cannot be selected.
  dialog.Bind(
      wxEVT_BUTTON,
      [&](wxCommandEvent&) {
        try {
          const auto& s = selected();
          if (&s == &full && Installed(s).empty()) InstallBundled();
          Verify(s, Installed(s));
          {
            Config c;
            c.c->Write("Resolution", wxString(&s == &high ? "high" : "full"));
            c.c->Write("CacheMiB", cache->GetValue());
            c.c->Flush();
          }
          active.reset();
          activeDescription.clear();
          dialog.EndModal(wxID_OK);
        } catch (const std::exception& e) {
          wxMessageBox(wxString::FromUTF8(e.what()), _("Shoreline data"),
                       wxOK | wxICON_ERROR, &dialog);
        }
      },
      wxID_OK);
  dialog.ShowModal();
}
}  // namespace weather_routing
