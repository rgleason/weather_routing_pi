#pragma once
#include <wx/progdlg.h>
#include "WeatherRoutingMessageDialog.h"
#ifdef __OCPN__ANDROID__
#include <QProgressBar>
#include <QElapsedTimer>
class WR_ProgressDialog {
public:
  WR_ProgressDialog(const wxString& title, const wxString&, int maximum,
                    wxWindow* parent, int)
      : m_sheet(static_cast<QWidget*>(parent->GetHandle()), wxID_CANCEL), m_maximum(maximum) {
    auto* layout = new QVBoxLayout(&m_sheet);
    auto* heading = new QLabel(QString::fromUtf8(title.ToUTF8().data()));
    heading->setStyleSheet("color: white; background: #193b4c; font-size: 20pt; padding: 16px;");
    layout->addWidget(heading);
    m_status = new QLabel;
    m_status->setWordWrap(true);
    m_status->setStyleSheet("font-size: 17pt; padding: 16px;");
    layout->addWidget(m_status);
    m_progress = new QProgressBar;
    m_progress->setRange(0, maximum);
    m_progress->setMinimumHeight(48);
    layout->addWidget(m_progress);
    auto* cancel = new QPushButton(QString::fromUtf8(_("Cancel batch").ToUTF8().data()));
    cancel->setStyleSheet("font-size: 17pt; min-height: 72px; padding: 8px;");
    layout->addWidget(cancel);
    QObject::connect(cancel, &QPushButton::clicked, &m_sheet, [this]() { m_cancelled = true; });
    QObject::connect(&m_sheet, &QDialog::finished, &m_sheet, [this](int) { m_cancelled = true; });
    auto fit = [this]() {
      const QRect bounds = QApplication::primaryScreen()->availableGeometry();
      m_sheet.resize(qMin(800, bounds.width() - 32), qMin(400, bounds.height() - 32));
      m_sheet.move(bounds.center() - QPoint(m_sheet.width()/2, m_sheet.height()/2));
    };
    QObject::connect(QApplication::primaryScreen(), &QScreen::availableGeometryChanged,
                     &m_sheet, [this, fit](const QRect&) {
      QTimer::singleShot(150, &m_sheet, fit);
    });
    fit();
    m_sheet.setWindowModality(Qt::ApplicationModal);
    m_sheet.show();
    m_clock.start();
  }
  bool Update(int value) {
    m_progress->setValue(value);
    const wxString status = wxString::Format(_("Created %d of %d routes.\nElapsed: %ld seconds.\nCancelling keeps the original templates and the routes already created."),
        value, m_maximum, long(m_clock.elapsed()/1000));
    m_status->setText(QString::fromUtf8(status.ToUTF8().data()));
    m_sheet.raise();
    QApplication::processEvents(QEventLoop::AllEvents, 20);
    return !m_cancelled;
  }
private:
  WR_MessageSheet m_sheet;
  QLabel* m_status;
  QProgressBar* m_progress;
  QElapsedTimer m_clock;
  int m_maximum;
  bool m_cancelled{false};
};
#else
using WR_ProgressDialog = wxProgressDialog;
#endif
