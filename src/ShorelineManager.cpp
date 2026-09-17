// SPDX-License-Identifier: GPL-3.0-or-later
#include "ShorelineManager.h"
#include "ShorelineSpec.h"
#include <map>
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
using Spec = ShorelineSpec;
std::atomic<bool> busy{false};
// Weak entries reuse datasets held by active routes, without retaining five
// unused caches after those route snapshots are released.
std::map<std::pair<int, std::size_t>, std::weak_ptr<ShorelineDataset>> active;
std::array<wxString, 5> descriptions;
bool Bundled(const Spec& s) { return &s - kShorelineSpecs.data() < 3; }
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
  const auto id = c.Read("Resolution", "intermediate");
  for (const auto& spec : kShorelineSpecs) if (id == spec.id) return spec;
  return kShorelineSpecs[2];
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
  if (!Bundled(s)) return {};
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
void InstallBundled(const Spec& s) {
  if (!Bundled(s))
    throw std::runtime_error("This resolution is optional; install it from Shoreline data on Advanced.");
  auto p = Destination(s);
  InstallShorelineGzip(Archive(s), p, s.hash, s.bytes);
  Record(s, p);
}
std::vector<std::string> Sources(const Spec& s) {
  const std::string name = std::string("poly-") + s.code + "-2.3.7.dat.gz";
  return {
      "https://github.com/pob220/weather_routing_pi/releases/download/gshhg-2.3.7/" + name,
      "https://www.singe.media/weather-routing/gshhg-2.3.7/" + name,
  };
}
void InstallOptional(const Spec& s, wxWindow* parent) {
  if (Bundled(s)) throw std::logic_error("Bundled data is not an optional download");
  // The approved release is pinned in ShorelineSpec. A newer upstream release
  // is not installed until its format and hashes have been qualified in a
  // plugin update.
  try {
    Verify(s, Installed(s));
    return;  // Already at the approved version.
  } catch (const std::bad_alloc&) {
    throw;
  } catch (const std::exception&) {
  }
  const auto destination = Destination(s);
  auto archive = destination;
  archive += ".download.gz";
  DownloadShorelineMirrors(
      Sources(s),
      [&](const std::string& source, const std::filesystem::path& output) {
        wxString outputPath = Wx(output);
#ifdef __ANDROID__
        outputPath = "file://" + outputPath;
#endif
        const auto result = OCPN_downloadFile(
            wxString::FromUTF8(source), outputPath,
            _("Downloading shoreline data"),
            wxString::Format(_("GSHHG 2.3.7 — %s"), wxGetTranslation(s.quality)),
            wxNullBitmap, parent,
            OCPN_DLDS_ELAPSED_TIME | OCPN_DLDS_ESTIMATED_TIME |
                OCPN_DLDS_REMAINING_TIME | OCPN_DLDS_SPEED | OCPN_DLDS_SIZE |
                OCPN_DLDS_CAN_ABORT | OCPN_DLDS_AUTO_CLOSE,
            1800);
        if (result == OCPN_DL_ABORTED) return ShorelineDownloadResult::Cancelled;
        if (result != OCPN_DL_NO_ERROR) return ShorelineDownloadResult::Failed;
        if (std::filesystem::file_size(output) != s.archive_bytes ||
            ShorelineSha256(output) != s.archive_hash)
          throw std::runtime_error("Downloaded shoreline archive failed size or SHA-256 verification");
        return ShorelineDownloadResult::Complete;
      },
      archive, destination, s.hash, s.bytes);
  Record(s, destination);
}
}  // namespace
bool ShorelineManager::Busy() { return busy; }
bool ShorelineManager::Available(int resolution) {
  const auto& s = ShorelineSpecFor(resolution);
  const auto p = Installed(s);
  return (!p.empty() && std::filesystem::exists(p)) ||
         (Bundled(s) && std::filesystem::exists(Archive(s)));
}
int ShorelineManager::DefaultResolution() {
  return static_cast<int>(&Selected() - kShorelineSpecs.data());
}
std::shared_ptr<ShorelineDataset> ShorelineManager::Prepare(int resolution) {
  if (busy) throw std::runtime_error("Close shoreline data management before computing a route.");
  const auto& s = ShorelineSpecFor(resolution);
  const auto key = std::make_pair(resolution, Budget());
  if (auto dataset = active[key].lock(); dataset && dataset->Error().empty()) return dataset;
  auto p = Installed(s);
  std::shared_ptr<ShorelineDataset> dataset;
  try { dataset = Verify(s, p); }
  catch (const std::bad_alloc&) { throw; }
  catch (const std::exception&) {
    if (!Bundled(s))
      throw std::runtime_error(
          std::string(s.quality) +
          " shoreline data is not installed or failed verification. "
          "Open Shoreline data on Advanced to install or repair it; the route "
          "resolution has not been changed.");
    InstallBundled(s);
    p = Installed(s);
    dataset = Verify(s, p);
  }
  descriptions[resolution] = wxString::Format("GSHHG 2.3.7 / %s / SHA256 %s / %s",
                                               s.quality, s.hash, Wx(p));
  wxLogMessage("WR_SHORELINE_READY %s cache_mib=%llu", descriptions[resolution],
               static_cast<unsigned long long>(Budget() / 1024 / 1024));
  active[key] = dataset;
  return dataset;
}
wxString ShorelineManager::Description(int resolution) {
  ShorelineSpecFor(resolution);
  return descriptions[resolution];
}
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
                       _("GSHHG Crude, Low and Intermediate are included for offline use. "
                         "High and Full are optional verified downloads; install High "
                         "before coastal routing where finer land detail is needed. "
                         "Choose each route's resolution in Configuration / Advanced. "
                         "This default is used when importing older routes; "
                         "existing route selections are preserved."));
  text->Wrap(dialog.FromDIP(560));
  main->Add(text, 0, wxALL, 12);
  auto choice = new wxChoice(&dialog, wxID_ANY);
  for (int q = 0; q < 5; ++q)
    choice->Append(wxString::Format("%d — %s", q, wxGetTranslation(kShorelineSpecs[q].quality)));
  choice->SetSelection(DefaultResolution());
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
  auto restore = new wxButton(&dialog, wxID_ANY, _("Install / update selected data"));
  actions->Add(verify, 0, wxRIGHT, 6);
  actions->Add(restore);
  main->Add(actions, 0, wxALL, 12);
  auto installHighRes = new wxButton(
      &dialog, wxID_ANY, _("Install / update High and Full (3–4)..."));
  installHighRes->SetToolTip(_(
      "Download and verify both optional GSHHG resolutions. "
      "Afterwards all five shoreline resolutions are available offline."));
  main->Add(installHighRes, 0, wxLEFT | wxRIGHT | wxBOTTOM, 12);
  auto updates =
      new wxButton(&dialog, wxID_ANY, _("GSHHG release information..."));
  main->Add(updates, 0, wxLEFT | wxRIGHT | wxBOTTOM, 12);
  auto footer = dialog.CreateSeparatedButtonSizer(wxOK | wxCANCEL);
  main->Add(footer, 0, wxEXPAND | wxALL, 12);
  auto selected = [&]() -> const Spec& {
    return ShorelineSpecFor(choice->GetSelection());
  };
  std::filesystem::path verifiedPath;
  auto refresh = [&]() {
    const auto& s = selected();
    auto p = Installed(s);
    const wxString state = (!p.empty() && std::filesystem::exists(p))
        ? (verifiedPath == p ? _("Verified SHA-256 and format; approved version is current")
                             : _("Installed; verification runs before use"))
        : (Bundled(s) ? _("Included in plugin; ready for offline installation")
                      : wxString::Format(
                            _("Not installed; %.1f MiB download, %.1f MiB installed (plus temporary space)"),
                            s.archive_bytes / 1048576.0, s.bytes / 1048576.0));
    status->SetLabel(wxString::Format(
        _("Approved dataset: GSHHG 2.3.7 — %s\n%s"),
        wxGetTranslation(s.quality), state));
    restore->SetLabel(Bundled(s) ? _("Restore selected bundled data")
                                 : _("Install / update selected data"));
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
      wxMessageBox(_("Approved shoreline data is ready and verified."),
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
  restore->Bind(wxEVT_BUTTON, [&](wxCommandEvent&) {
    if (selected().code[0] == 'f' && !ShorelineManager::Available(4) &&
        wxMessageBox(
            _("Full shoreline data needs about 55 MiB to download and 164 MiB "
              "when installed, plus temporary space. Continue?"),
            _("Install Full shoreline data"),
            wxYES_NO | wxICON_QUESTION, &dialog) != wxYES)
      return;
    action([&]() {
      const auto& s = selected();
      if (Bundled(s)) InstallBundled(s);
      else InstallOptional(s, &dialog);
    });
  });
  installHighRes->Bind(wxEVT_BUTTON, [&](wxCommandEvent&) {
    if ((!ShorelineManager::Available(3) || !ShorelineManager::Available(4)) &&
        wxMessageBox(
            _("High and Full shoreline data need about 68 MiB to download "
              "and 196 MiB when installed, plus temporary space. Continue?"),
            _("Install high-resolution shoreline data"),
            wxYES_NO | wxICON_QUESTION, &dialog) != wxYES)
      return;
    action([&]() {
      InstallOptional(kShorelineSpecs[3], &dialog);
      InstallOptional(kShorelineSpecs[4], &dialog);
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
          if (Installed(s).empty()) {
            if (Bundled(s)) InstallBundled(s);
            else throw std::runtime_error(
                "Install this optional resolution before selecting it.");
          }
          Verify(s, Installed(s));
          {
            Config c;
            c.c->Write("Resolution", wxString(s.id));
            c.c->Write("CacheMiB", cache->GetValue());
            c.c->Flush();
          }
          active.clear();
          descriptions.fill(wxString());
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
