#pragma once

#include <wx/msgdlg.h>
#include <wx/window.h>

#ifdef __OCPN__ANDROID__
#include <QDialog>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QPushButton>
#include <QLabel>
#include <QScrollArea>
#include <QScroller>
#include <QKeyEvent>
#include <QApplication>
#include <QScreen>
#include <QTimer>
#include <QShowEvent>
#include <QInputMethod>

class WR_MessageSheet : public QDialog {
public:
  WR_MessageSheet(QWidget* parent, int cancelAnswer)
      : QDialog(parent, Qt::Dialog | Qt::FramelessWindowHint), m_cancelAnswer(cancelAnswer) {}
  void reject() override { done(m_cancelAnswer); }
protected:
  void showEvent(QShowEvent* event) override {
    QDialog::showEvent(event);
    // A queued wxQt chart refresh can otherwise raise the workspace over a
    // newly opened sheet. Restore its stacking after that refresh settles.
    QTimer::singleShot(0, this, [this]() { raise(); activateWindow(); update(); });
    QTimer::singleShot(80, this, [this]() { raise(); activateWindow(); update(); });
  }
  void keyPressEvent(QKeyEvent* event) override {
    if (event->key() == Qt::Key_Back || event->key() == Qt::Key_Escape) {
      event->accept();
      if (QApplication::inputMethod()->isVisible()) QApplication::inputMethod()->hide();
      else reject();
    } else QDialog::keyPressEvent(event);
  }
private:
  int m_cancelAnswer;
};

// Keep messages in Qt's event loop. The host's Java alert can hide on Android
// Back without releasing the native wait, leaving an invisible input blocker.
class WR_MessageDialog {
public:
  WR_MessageDialog(wxWindow* parent, const wxString& message,
                   const wxString& caption = wxMessageBoxCaptionStr,
                   long style = wxOK | wxCENTRE,
                   const wxPoint& = wxDefaultPosition)
      : m_parent(parent), m_message(message), m_caption(caption), m_style(style) {}
  bool SetOKLabel(const wxString& label) { m_okLabel = label; return true; }
  int ShowModal() {
    const int cancelAnswer = (m_style & wxCANCEL) ? wxID_CANCEL
        : (m_style & wxYES_NO) ? wxID_NO : wxID_OK;
    WR_MessageSheet box(static_cast<QWidget*>(m_parent ? m_parent->GetHandle() : nullptr),
                         cancelAnswer);
    box.setStyleSheet("QLabel { font-size: 17pt; } QPushButton { font-size: 17pt; "
                      "min-width: 110px; min-height: 64px; padding: 8px; }");
    auto* layout = new QVBoxLayout(&box);
    auto* title = new QLabel(QString::fromUtf8(m_caption.ToUTF8().data()));
    title->setWordWrap(true);
    title->setStyleSheet("QLabel { color: white; background: #193b4c; "
                         "font-weight: bold; padding: 14px; }");
    layout->addWidget(title);
    auto* scroll = new QScrollArea;
    scroll->setWidgetResizable(true);
    auto* text = new QLabel(QString::fromUtf8(m_message.ToUTF8().data()));
    text->setTextFormat(Qt::PlainText);
    text->setWordWrap(true);
    text->setMargin(16);
    text->setAlignment(Qt::AlignTop | Qt::AlignLeft);
    scroll->setWidget(text);
    QScroller::grabGesture(scroll->viewport(), QScroller::TouchGesture);
    layout->addWidget(scroll, 1);
    auto* actions = new QHBoxLayout;
    layout->addLayout(actions);
    auto addButton = [&](const wxString& label, int answer, bool primary) {
      auto* button = new QPushButton(QString::fromUtf8(label.ToUTF8().data()));
      if (primary) button->setStyleSheet("QPushButton { color: white; background: #176b84; }");
      actions->addWidget(button, 1);
      QObject::connect(button, &QPushButton::clicked, &box, [&box, answer]() { box.done(answer); });
    };
    if (m_style & wxYES_NO) {
      addButton(_("No"), wxID_NO, false);
      addButton(_("Yes"), wxID_YES, true);
    }
    if ((m_style & wxOK) || !(m_style & wxYES_NO))
      addButton(m_okLabel.IsEmpty() ? _("OK") : m_okLabel, wxID_OK, true);
    if (m_style & wxCANCEL) addButton(_("Cancel"), wxID_CANCEL, false);
    auto fit = [&]() {
      const QRect available = QApplication::primaryScreen()->availableGeometry();
      const int width = qMin(available.width() - 32, m_parent
          ? qMax(400, m_parent->GetClientSize().x - 80) : 800);
      text->setFixedWidth(width - 48);
      const int height = qMin(available.height() * 3 / 4,
                              qMax(300, text->heightForWidth(width - 48) + 210));
      box.resize(width, height);
      box.move(available.center() - QPoint(width / 2, height / 2));
    };
    QObject::connect(QApplication::primaryScreen(), &QScreen::availableGeometryChanged,
                     &box, [&](const QRect&) { QTimer::singleShot(180, &box, fit); });
    fit();
    return box.exec();
  }

private:
  wxWindow* m_parent;
  wxString m_message, m_caption, m_okLabel;
  long m_style;
};

inline int WR_MessageBox(const wxString& message,
                         const wxString& caption = wxMessageBoxCaptionStr,
                         long style = wxOK | wxCENTRE, wxWindow* parent = nullptr,
                         int = wxDefaultCoord, int = wxDefaultCoord) {
  const int answer = WR_MessageDialog(parent, message, caption, style).ShowModal();
  if (answer == wxID_YES) return wxYES;
  if (answer == wxID_NO) return wxNO;
  if (answer == wxID_OK) return wxOK;
  return wxCANCEL;
}
#else
using WR_MessageDialog = wxMessageDialog;
inline int WR_MessageBox(const wxString& message,
                         const wxString& caption = wxMessageBoxCaptionStr,
                         long style = wxOK | wxCENTRE, wxWindow* parent = nullptr,
                         int x = wxDefaultCoord, int y = wxDefaultCoord) {
  return wxMessageBox(message, caption, style, parent, x, y);
}
#endif
