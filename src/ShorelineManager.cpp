// SPDX-License-Identifier: GPL-3.0-or-later
#include "WeatherRoutingMessageDialog.h"
#include "ShorelineManager.h"
#include "ShorelineSpec.h"
#include "WeatherRoutingWxCompat.h"
#include "AndroidDialogHeader.h"
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
#ifdef __OCPN__ANDROID__
#include <QProgressBar>
#include <QElapsedTimer>
#include <QtAndroidExtras/QAndroidJniObject>
#include <QtAndroidExtras/QAndroidJniEnvironment>
#endif

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
        "Shoreline data is missing or failed checksum verification. Open "
        "Shoreline data to install or repair it.");
  return std::make_shared<ShorelineDataset>(p, Budget());
}
void InstallBundled(const Spec& s) {
  if (!Bundled(s))
    throw std::runtime_error("This resolution is optional; install it from Shoreline data.");
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
#ifdef __OCPN__ANDROID__
_OCPN_DLStatus DownloadAndroidShoreline(const wxString& url,
    const wxString& output, const Spec& spec, wxWindow* parent) {
  WR_MessageSheet sheet(parent->GetHandle(), wxID_CANCEL);
  sheet.setWindowModality(Qt::ApplicationModal);
  auto* layout = new QVBoxLayout(&sheet);
  auto* heading = new QLabel(QString::fromUtf8(
      wxString::Format(_("Downloading %s shoreline data"),
                       wxGetTranslation(spec.quality)).ToUTF8().data()));
  heading->setWordWrap(true);
  heading->setStyleSheet("font-size: 20pt; color: white; background: #193b4c; padding: 16px;");
  layout->addWidget(heading);
  auto* status = new QLabel;
  status->setWordWrap(true);
  status->setStyleSheet("font-size: 17pt; padding: 16px;");
  layout->addWidget(status);
  auto* progress = new QProgressBar;
  progress->setRange(0, 1000);
  progress->setMinimumHeight(48);
  layout->addWidget(progress);
  auto* cancel = new QPushButton(QString::fromUtf8(_("Cancel download").ToUTF8().data()));
  bool cancelled = false;
  cancel->setStyleSheet("font-size: 17pt; min-height: 72px; padding: 8px;");
  // wxQt's Android parent can consume the synthesized mouse sequence. Use
  // the same explicit stationary-touch handling as the workspace buttons.
  new WR_AndroidButtonDragFilter(cancel);
  layout->addWidget(cancel);
  QObject::connect(cancel, &QPushButton::clicked, &sheet, [&]() {
    cancelled = true;
    wxLogMessage("WR_SHORELINE_DOWNLOAD_CANCEL quality=%s", spec.quality);
    sheet.reject();
  });
  auto fit = [&]() {
    const QRect bounds = QApplication::primaryScreen()->availableGeometry();
    sheet.resize(qMin(800, bounds.width() - 32), qMin(400, bounds.height() - 32));
    sheet.move(bounds.center() - QPoint(sheet.width() / 2, sheet.height() / 2));
  };
  QObject::connect(QApplication::primaryScreen(), &QScreen::availableGeometryChanged,
                   &sheet, [&](const QRect&) { QTimer::singleShot(150, &sheet, fit); });
  fit();
  cancel->setFocus();
  QTimer::singleShot(100, &sheet, []() { QApplication::inputMethod()->hide(); });

  wxEvtHandler handler;
  _OCPN_DLStatus result = OCPN_DL_ABORTED;
  bool complete = false;
  long transferred = 0;
  QElapsedTimer elapsed;
  elapsed.start();
  QTimer timer(&sheet);
  auto refresh = [&]() {
    const wxString message = wxString::Format(
        _("%.1f of %.1f MiB\nElapsed: %ld seconds"),
        transferred / 1048576.0, spec.archive_bytes / 1048576.0,
        long(elapsed.elapsed() / 1000));
    status->setText(QString::fromUtf8(message.ToUTF8().data()));
  };
  QObject::connect(&timer, &QTimer::timeout, &sheet, [&]() {
    refresh();
    if (elapsed.elapsed() > 1800000) {
      result = OCPN_DL_USER_TIMEOUT;
      sheet.reject();
    }
  });
  handler.Bind(wxEventTypeTag<OCPN_downloadEvent>(wxEVT_DOWNLOAD_EVENT),
               [&](OCPN_downloadEvent& event) {
    if (cancelled) return;
    transferred = std::max(0L, event.getTransferred());
    progress->setValue(qMin(1000, int(1000ULL * transferred / spec.archive_bytes)));
    refresh();
    if (event.getDLEventCondition() == OCPN_DL_EVENT_TYPE_END) {
      complete = true;
      result = event.getDLEventStatus();
      sheet.done(wxID_OK);
    }
  });
  refresh();
  long handle = -1;
  const auto started = OCPN_downloadFileBackground(url, output, &handler, &handle);
  if (started != OCPN_DL_STARTED) return started;
  // The host's background API also opens a Java ProgressDialog. Even when
  // hidden behind this Qt sheet it owns touch input. Dismiss that spinner;
  // our progress sheet owns this transfer and the API cleanup still runs.
  QAndroidJniEnvironment env;
  auto activity = QAndroidJniObject::callStaticObjectMethod(
      "org/qtproject/qt5/android/QtNative", "activity",
      "()Landroid/app/Activity;");
  if (activity.isValid())
    activity.callObjectMethod("hideBusyCircle", "()Ljava/lang/String;");
  if (env->ExceptionCheck()) env->ExceptionClear();
  timer.start(250);
  const int answer = complete ? wxID_OK : sheet.exec();
  if ((answer != wxID_OK || cancelled) && result != OCPN_DL_USER_TIMEOUT)
    result = OCPN_DL_ABORTED;
  timer.stop();

  // Clear the host's retained callback before this handler is destroyed.
  // Android DownloadManager cancellation can remove a completed target too;
  // move our unique temporary archive aside until that cleanup has returned.
  const auto target = Path(output.Mid(7));  // The caller supplies file://.
  auto preserved = target;
  preserved += ".complete";
  std::error_code error;
  const bool success = answer == wxID_OK && !cancelled && complete && result == OCPN_DL_NO_ERROR;
  if (success) {
    std::filesystem::rename(target, preserved, error);
    if (error) result = OCPN_DL_FAILED;
  }
  OCPN_cancelDownloadFileBackground(handle);
  if (success && !error) {
    std::filesystem::rename(preserved, target, error);
    if (error) {
      std::filesystem::remove(preserved, error);
      result = OCPN_DL_FAILED;
    }
  }
  wxLogMessage("WR_SHORELINE_DOWNLOAD_RESULT quality=%s status=%d answer=%d cancelled=%d bytes=%ld",
               spec.quality, int(result), answer, cancelled ? 1 : 0, transferred);
  return result;
}
#endif
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
#ifdef __OCPN__ANDROID__
        const auto result = DownloadAndroidShoreline(
            wxString::FromUTF8(source.c_str()), outputPath, s, parent);
#else
        const auto result = OCPN_downloadFile(
            wxString::FromUTF8(source.c_str()), outputPath,
            _("Downloading shoreline data"),
            wxString::Format(_("GSHHG 2.3.7 — %s"), wxGetTranslation(s.quality)),
            wxNullBitmap, parent,
            OCPN_DLDS_ELAPSED_TIME | OCPN_DLDS_ESTIMATED_TIME |
                OCPN_DLDS_REMAINING_TIME | OCPN_DLDS_SPEED | OCPN_DLDS_SIZE |
                OCPN_DLDS_CAN_ABORT | OCPN_DLDS_AUTO_CLOSE,
            1800);
#endif
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
  wxWindow* body = &dialog;
#ifdef __OCPN__ANDROID__
  auto* scroll = new wxScrolledWindow(&dialog, wxID_ANY);
  scroll->SetMinSize(wxSize(0, 0));
  scroll->SetScrollRate(0, 16);
  body = new wxPanel(scroll, wxID_ANY);
#endif
  auto main = new wxBoxSizer(wxVERTICAL);
  auto text =
      new wxStaticText(body, wxID_ANY,
                       _("GSHHG Crude, Low and Intermediate are included for offline use. "
                         "High and Full are optional verified downloads; install High "
                         "before coastal routing where finer land detail is needed. "
                         "Choose each route's shoreline resolution in its setup. "
                         "This default is used when importing older routes; "
                         "existing route selections are preserved."));
#ifdef __OCPN__ANDROID__
  text->SetMinSize(wxSize(0, 100));
#else
  text->Wrap(WR_FromDIP(&dialog, 560));
#endif
  main->Add(text, 0, wxEXPAND | wxALL, 12);
  auto choice = new wxChoice(body, wxID_ANY);
  for (int q = 0; q < 5; ++q)
#ifdef __OCPN__ANDROID__
    choice->Append(wxString::Format("%d - %s", q,
                                    wxGetTranslation(kShorelineSpecs[q].quality)));
#else
    choice->Append(wxString::Format("%d — %s", q, wxGetTranslation(kShorelineSpecs[q].quality)));
#endif
  choice->SetSelection(DefaultResolution());
  main->Add(choice, 0, wxEXPAND | wxLEFT | wxRIGHT, 12);
  auto status = new wxStaticText(body, wxID_ANY, "");
#ifdef __OCPN__ANDROID__
  status->SetMinSize(wxSize(0, 65));
#endif
  main->Add(status, 0, wxEXPAND | wxALL, 12);
  main->Add(new wxStaticText(body, wxID_ANY, _("Installed file:")), 0,
            wxLEFT | wxRIGHT, 12);
  auto installedFile = new wxTextCtrl(body, wxID_ANY, "", wxDefaultPosition,
                                      wxDefaultSize, wxTE_READONLY);
  installedFile->SetMinSize(WR_FromDIP(&dialog, wxSize(300, -1)));
  main->Add(installedFile, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 12);
  auto cacheLabel =
      new wxStaticText(body, wxID_ANY,
                       _("Shoreline tile cache limit (MiB; resolution is never "
                         "reduced automatically):"));
#ifdef __OCPN__ANDROID__
  cacheLabel->SetMinSize(wxSize(0, 50));
#else
  cacheLabel->Wrap(WR_FromDIP(&dialog, 560));
#endif
  main->Add(cacheLabel, 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, 12);
  auto cache = new wxSpinCtrl(body, wxID_ANY);
  cache->SetRange(16, 256);
  cache->SetValue(int(Budget() / 1024 / 1024));
  main->Add(cache, 0, wxEXPAND | wxALL, 12);
#ifdef __OCPN__ANDROID__
  auto actions = new wxBoxSizer(wxVERTICAL);
  auto verify = new wxButton(body, wxID_ANY, _("Verify installed data"));
  auto restore = new wxButton(body, wxID_ANY, _("Install / update selected data"));
  actions->Add(verify, 0, wxEXPAND | wxBOTTOM, 12);
  actions->Add(restore, 0, wxEXPAND);
#else
  auto actions = new wxBoxSizer(wxHORIZONTAL);
  auto verify = new wxButton(body, wxID_ANY, _("Verify installed data"));
  auto restore = new wxButton(body, wxID_ANY, _("Install / update selected data"));
  actions->Add(verify, 0, wxRIGHT, 6);
  actions->Add(restore);
#endif
  main->Add(actions, 0, wxEXPAND | wxALL, 12);
  auto installHighRes = new wxButton(
      body, wxID_ANY,
#ifdef __OCPN__ANDROID__
      _("Install High + Full..."));
#else
      _("Install / update High and Full (3–4)..."));
#endif
  installHighRes->SetToolTip(_(
      "Download and verify both optional GSHHG resolutions. "
      "Afterwards all five shoreline resolutions are available offline."));
  main->Add(installHighRes, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 12);
  auto updates =
      new wxButton(body, wxID_ANY, _("GSHHG release information..."));
  main->Add(updates, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 12);
#ifndef __OCPN__ANDROID__
  auto footer = dialog.CreateSeparatedButtonSizer(wxOK | wxCANCEL);
  main->Add(footer, 0, wxEXPAND | wxALL, 12);
#else
  body->SetSizer(main);
  auto* viewport = new wxBoxSizer(wxVERTICAL);
  viewport->Add(body, 1, wxEXPAND);
  scroll->SetSizer(viewport);
  auto* layout = new wxBoxSizer(wxVERTICAL);
  layout->Add(scroll, 1, wxEXPAND | wxALL, 8);
  dialog.SetSizer(layout);
  WR_StyleAndroidControls(&dialog);
  auto* header = WR_AddAndroidDoneHeader(&dialog, _("Shoreline data"), [&]() {
    dialog.EndModal(wxID_CANCEL);
  }, _("Cancel"));
  auto* apply = new wxButton(header, wxID_ANY, _("Apply"));
  apply->SetMinSize(wxSize(110, 72));
  apply->Bind(wxEVT_BUTTON, [&](wxCommandEvent&) {
    wxCommandEvent event(wxEVT_BUTTON, wxID_OK);
    dialog.GetEventHandler()->ProcessEvent(event);
  });
  WR_StyleAndroidControls(header);
  header->GetSizer()->Insert(1, apply, 0, wxALL, 8);
  scroll->Bind(wxEVT_SIZE, [=](wxSizeEvent& event) {
    const int width = scroll->GetClientSize().x - 48;
    for (auto* label : {text, status, cacheLabel})
      WR_WrapAndroidText(label, label->GetLabel(), width);
    body->Layout();
    scroll->Layout();
    scroll->FitInside();
    event.Skip();
  });
#endif
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
#ifdef __OCPN__ANDROID__
    status->SetLabel(wxString::Format(
        _("Approved dataset: GSHHG 2.3.7 / %s\n%s"),
        wxGetTranslation(s.quality), state));
#else
    status->SetLabel(wxString::Format(
        _("Approved dataset: GSHHG 2.3.7 — %s\n%s"),
        wxGetTranslation(s.quality), state));
#endif
    restore->SetLabel(Bundled(s) ? _("Restore selected bundled data")
                                 : _("Install / update selected data"));
    installedFile->ChangeValue(Wx(p));
    installedFile->SetToolTip(Wx(p));
#ifdef __OCPN__ANDROID__
    WR_WrapAndroidText(status, status->GetLabel(), scroll->GetClientSize().x - 48);
    body->Layout();
    scroll->Layout();
    scroll->FitInside();
#else
    status->Wrap(WR_FromDIP(&dialog, 560));
#endif
    dialog.Layout();
  };
  auto action = [&](const std::function<void()>& work) {
    try {
      wxBusyCursor cursor;
      work();
      verifiedPath = Installed(selected());
      refresh();
      WR_MessageBox(_("Approved shoreline data is ready and verified."),
                   _("Shoreline data"), wxOK | wxICON_INFORMATION, &dialog);
    } catch (const std::exception& e) {
      WR_MessageBox(wxString::FromUTF8(e.what()), _("Shoreline data"),
                   wxOK | wxICON_ERROR, &dialog);
    }
  };
  choice->Bind(wxEVT_CHOICE, [&](wxCommandEvent&) { refresh(); });
  verify->Bind(wxEVT_BUTTON, [&](wxCommandEvent&) {
    action([&]() { Verify(selected(), Installed(selected())); });
  });
  restore->Bind(wxEVT_BUTTON, [&](wxCommandEvent&) {
    if (selected().code[0] == 'f' && !ShorelineManager::Available(4) &&
        WR_MessageBox(
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
        WR_MessageBox(
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
#ifdef __OCPN__ANDROID__
  const wxSize canvas = GetCanvasByIndex(0)->GetClientSize();
  dialog.SetSize(wxSize(canvas.x - 24, canvas.y - 24));
  dialog.CentreOnParent();
#else
  dialog.SetSizerAndFit(main);
  dialog.CentreOnScreen();
#endif
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
          WR_MessageBox(wxString::FromUTF8(e.what()), _("Shoreline data"),
                       wxOK | wxICON_ERROR, &dialog);
        }
      },
      wxID_OK);
  dialog.ShowModal();
}
}  // namespace weather_routing
