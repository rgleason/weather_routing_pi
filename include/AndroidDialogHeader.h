#pragma once

#ifdef __OCPN__ANDROID__

#include <wx/wx.h>
#include <wx/weakref.h>
#include <wx/spinctrl.h>
#include <wx/notebook.h>
#include <wx/scrolwin.h>
#include <wx/listbox.h>
#include <wx/listctrl.h>
#include <QWidget>
#include <QScroller>
#include <QScrollEvent>
#include <QScrollPrepareEvent>
#include <QComboBox>
#include <QStyledItemDelegate>
#include <QApplication>
#include <QScreen>
#include <QTimer>
#include <QKeyEvent>
#include <QLabel>
#include <QSlider>
#include <QTouchEvent>
#include <QMouseEvent>
#include <QAbstractButton>
#include <QElapsedTimer>
#include <QPointer>
#include <QStyle>
#include <wx/slider.h>
#include <QFontMetrics>
#include <QFileInfo>
#include <QAbstractItemView>
#include <QTabWidget>
#include <QTabBar>
#include "ocpn_plugin.h"
#include <functional>
#include <vector>

#include "WeatherRoutingWxCompat.h"

class WR_AndroidChoiceDelegate : public QStyledItemDelegate {
public:
  explicit WR_AndroidChoiceDelegate(QObject* parent)
      : QStyledItemDelegate(parent) {}
  QSize sizeHint(const QStyleOptionViewItem& option,
                 const QModelIndex& index) const override {
    QSize size = QStyledItemDelegate::sizeHint(option, index);
    size.setHeight(qMax(size.height(), 72));
    return size;
  }
};

class WR_AndroidFileDelegate : public WR_AndroidChoiceDelegate {
public:
  explicit WR_AndroidFileDelegate(QObject* parent)
      : WR_AndroidChoiceDelegate(parent) {}
protected:
  void initStyleOption(QStyleOptionViewItem* option,
                       const QModelIndex& index) const override {
    QStyledItemDelegate::initStyleOption(option, index);
    option->text = QFileInfo(option->text).fileName();
  }
};

inline void WR_StyleAndroidFileList(wxWindow* window) {
  auto* view = qobject_cast<QAbstractItemView*>(window->GetHandle());
  if (!view) view = window->GetHandle()->findChild<QAbstractItemView*>();
  if (view) view->setItemDelegate(new WR_AndroidFileDelegate(view));
}

inline void WR_EnableAndroidChoiceScrolling(QComboBox* combo);

inline void WR_StyleAndroidCombo(wxWindow* window) {
  auto* combo = qobject_cast<QComboBox*>(window->GetHandle());
  if (!combo) combo = window->GetHandle()->findChild<QComboBox*>();
  if (combo) {
    if (wxDynamicCast(window, wxChoice)) combo->setEditable(false);
    combo->setItemDelegate(new WR_AndroidChoiceDelegate(combo));
    if (!combo->isEditable()) WR_EnableAndroidChoiceScrolling(combo);
  }
}

inline void WR_WrapAndroidText(wxStaticText* text, const wxString& value,
                               int width) {
  width = qMax(width, 160);
  if (text->GetLabel() != value) text->SetLabel(value);
  if (auto* label = qobject_cast<QLabel*>(text->GetHandle())) {
    label->ensurePolished();
    label->setWordWrap(true);
    const QRect bounds = QFontMetrics(label->font()).boundingRect(
        QRect(0, 0, width, 10000), Qt::TextWordWrap, label->text());
    text->SetMinSize(wxSize(0, bounds.height() + 8));
    text->SetMaxSize(wxSize(width, -1));
  }
}

// wxQt uses wxScrollHelper rather than QScrollArea's content model. Supply
// QScroller's geometry and apply its pixel positions through the wx API.
class WR_AndroidScrollFilter : public QObject {
public:
  WR_AndroidScrollFilter(wxScrolledWindow* window, QWidget* target)
      : QObject(target), m_window(window) { target->installEventFilter(this); }
protected:
  bool eventFilter(QObject*, QEvent* event) override {
    if (!m_window) return false;
    int xUnit, yUnit, x, y;
    m_window->GetScrollPixelsPerUnit(&xUnit, &yUnit);
    m_window->GetViewStart(&x, &y);
    if (event->type() == QEvent::ScrollPrepare) {
      auto* prepare = static_cast<QScrollPrepareEvent*>(event);
      const wxSize view = m_window->GetClientSize();
      const wxSize content = m_window->GetVirtualSize();
      prepare->setViewportSize(QSizeF(view.x, view.y));
      prepare->setContentPosRange(QRectF(0, 0,
          xUnit ? qMax(0, content.x - view.x) : 0,
          yUnit ? qMax(0, content.y - view.y) : 0));
      prepare->setContentPos(QPointF(x * xUnit, y * yUnit));
      prepare->accept();
      return true;
    }
    if (event->type() == QEvent::Scroll) {
      const QPointF position = static_cast<QScrollEvent*>(event)->contentPos();
      m_window->Scroll(xUnit ? qRound(position.x() / xUnit) : -1,
                       yUnit ? qRound(position.y() / yUnit) : -1);
      // wxQt can scroll the viewport pixels without moving reparented static
      // boxes. Lay out their controls at the actual wx scroll offset as well.
      m_window->Layout();
      event->accept();
      return true;
    }
    return false;
  }
private:
  wxWeakRef<wxScrolledWindow> m_window;
};

inline void WR_EnableAndroidScrolling(wxScrolledWindow* window) {
  QWidget* target = window->GetHandle();
  if (target->property("wrTouchScroll").toBool()) return;
  target->setProperty("wrTouchScroll", true);
  target->setAttribute(Qt::WA_AcceptTouchEvents);
  new WR_AndroidScrollFilter(window, target);
  QScroller::grabGesture(target, QScroller::TouchGesture);
  wxWeakRef<wxScrolledWindow> weakWindow(window);
  const auto afterScroll = [weakWindow](wxScrollWinEvent& event) {
    if (weakWindow) weakWindow->CallAfter([weakWindow]() {
      if (weakWindow) weakWindow->Layout();
    });
    event.Skip();
  };
  for (const auto& type : {wxEVT_SCROLLWIN_TOP, wxEVT_SCROLLWIN_BOTTOM,
                          wxEVT_SCROLLWIN_LINEUP, wxEVT_SCROLLWIN_LINEDOWN,
                          wxEVT_SCROLLWIN_PAGEUP, wxEVT_SCROLLWIN_PAGEDOWN,
                          wxEVT_SCROLLWIN_THUMBTRACK, wxEVT_SCROLLWIN_THUMBRELEASE})
    window->Bind(type, afterScroll);
}

// A swipe beginning over a wxQt button otherwise releases as a click. Forward
// that gesture to the nearest scrolling sheet and click only a stationary tap.
class WR_AndroidButtonDragFilter : public QObject {
public:
  explicit WR_AndroidButtonDragFilter(QWidget* widget)
      : QObject(widget), m_widget(widget),
        m_button(qobject_cast<QAbstractButton*>(widget)),
        m_combo(qobject_cast<QComboBox*>(widget)) {
    widget->setAttribute(Qt::WA_AcceptTouchEvents);
    widget->installEventFilter(this);
  }
protected:
  bool eventFilter(QObject*, QEvent* event) override {
    const bool touch = event->type() == QEvent::TouchBegin ||
        event->type() == QEvent::TouchUpdate || event->type() == QEvent::TouchEnd ||
        event->type() == QEvent::TouchCancel;
    const bool mouse = event->type() == QEvent::MouseButtonPress ||
        event->type() == QEvent::MouseMove || event->type() == QEvent::MouseButtonRelease;
    if (!touch && !mouse) return false;
    QPoint global;
    bool begin, end, cancel = false;
    if (touch) {
      auto* input = static_cast<QTouchEvent*>(event);
      if (input->touchPoints().isEmpty()) return false;
      global = input->touchPoints().first().screenPos().toPoint();
      begin = event->type() == QEvent::TouchBegin;
      end = event->type() == QEvent::TouchEnd || event->type() == QEvent::TouchCancel;
      cancel = event->type() == QEvent::TouchCancel;
    } else {
      auto* input = static_cast<QMouseEvent*>(event);
      // Android/Qt may synthesize mouse events after an accepted touch ends.
      // Our touch release already clicked the button; consume that second
      // sequence so destructive actions execute once.
      if (m_touchActive || (m_suppressMouse && m_clock.elapsed() < 500)) return true;
      m_suppressMouse = false;
      if (event->type() != QEvent::MouseMove && input->button() != Qt::LeftButton)
        return false;
      global = input->globalPos();
      begin = event->type() == QEvent::MouseButtonPress;
      end = event->type() == QEvent::MouseButtonRelease;
    }
    if (begin) {
      if (!m_widget->isEnabled()) return false;
      m_active = true;
      m_touchActive = touch;
      m_moved = false;
      m_origin = global;
      m_clock.start();
      m_scroll = nullptr;
      for (QWidget* parent = m_widget->parentWidget(); parent; parent = parent->parentWidget())
        if (parent->property("wrTouchScroll").toBool()) { m_scroll = parent; break; }
      if (!m_button && !m_combo && !m_scroll) {
        m_active = m_touchActive = false;
        return false;
      }
      if (m_button) m_button->setDown(true);
      if (m_scroll) QScroller::scroller(m_scroll)->handleInput(QScroller::InputPress,
          m_scroll->mapFromGlobal(global), 0);
    } else if (!m_active) return false;
    if ((global - m_origin).manhattanLength() > qMax(12, QApplication::startDragDistance()))
      m_moved = true;
    if (m_moved && m_button) m_button->setDown(false);
    if (m_scroll && !begin)
      QScroller::scroller(m_scroll)->handleInput(end ? QScroller::InputRelease : QScroller::InputMove,
          m_scroll->mapFromGlobal(global), m_clock.elapsed());
    if (end) {
      const bool click = (m_button || m_combo) && !cancel && !m_moved &&
          m_widget->isEnabled() &&
          m_widget->rect().contains(m_widget->mapFromGlobal(global));
      m_active = m_touchActive = false;
      if (m_button) m_button->setDown(false);
      if (touch) { m_suppressMouse = true; m_clock.restart(); }
      if (click) {
        if (m_button) m_button->click();
        else m_combo->showPopup();
      }
    }
    event->accept();
    return true;
  }
private:
  QPointer<QWidget> m_widget;
  QAbstractButton* m_button;
  QComboBox* m_combo;
  QPointer<QWidget> m_scroll;
  QPoint m_origin;
  QElapsedTimer m_clock;
  bool m_active = false, m_touchActive = false, m_moved = false;
  bool m_suppressMouse = false;
};

inline void WR_EnableAndroidChoiceScrolling(QComboBox* combo) {
  if (combo->property("wrChoiceDrag").toBool()) return;
  combo->setProperty("wrChoiceDrag", true);
  new WR_AndroidButtonDragFilter(combo);
}

inline void WR_EnableAndroidButton(wxButton* control) {
  auto* button = qobject_cast<QAbstractButton*>(control->GetHandle());
  if (!button || button->property("wrButtonDrag").toBool()) return;
  button->setProperty("wrButtonDrag", true);
  new WR_AndroidButtonDragFilter(button);
}

// wxQt labels consume synthesized mouse drags before an ancestor's QScroller
// sees them. They have no tap action; forward their drags like button drags.
inline void WR_EnableAndroidLabelScrolling(wxWindow* label) {
  QWidget* widget = label->GetHandle();
  if (widget->property("wrLabelDrag").toBool()) return;
  widget->setProperty("wrLabelDrag", true);
  new WR_AndroidButtonDragFilter(widget);
}

// Sliders inside a scrolling sheet must consume their own touch sequence.
class WR_AndroidSliderTouchFilter : public QObject {
public:
  explicit WR_AndroidSliderTouchFilter(QSlider* slider)
      : QObject(slider), m_slider(slider) {
    slider->setAttribute(Qt::WA_AcceptTouchEvents);
    slider->installEventFilter(this);
  }
protected:
  bool eventFilter(QObject*, QEvent* event) override {
    if (event->type() != QEvent::TouchBegin && event->type() != QEvent::TouchUpdate &&
        event->type() != QEvent::TouchEnd && event->type() != QEvent::TouchCancel)
      return false;
    const auto* touch = static_cast<QTouchEvent*>(event);
    if (!touch->touchPoints().isEmpty()) {
      const auto point = touch->touchPoints().first().pos();
      const bool horizontal = m_slider->orientation() == Qt::Horizontal;
      const int length = horizontal ? m_slider->width() : m_slider->height();
      const int position = qRound(horizontal ? point.x() : point.y());
      m_slider->setSliderDown(event->type() != QEvent::TouchEnd);
      m_slider->setValue(QStyle::sliderValueFromPosition(
          m_slider->minimum(), m_slider->maximum(), position - 16,
          qMax(1, length - 32), !horizontal));
    }
    if (event->type() == QEvent::TouchEnd || event->type() == QEvent::TouchCancel)
      m_slider->setSliderDown(false);
    event->accept();
    return true;
  }
private:
  QSlider* m_slider;
};

inline void WR_EnableAndroidSlider(wxSlider* control,
                                    std::function<void()> changed) {
  auto* slider = qobject_cast<QSlider*>(control->GetHandle());
  if (!slider) slider = control->GetHandle()->findChild<QSlider*>();
  if (!slider) return;
  new WR_AndroidSliderTouchFilter(slider);
  QObject::connect(slider, &QSlider::valueChanged, slider,
                   [changed](int) { changed(); });
}

inline void WR_StyleAndroidControls(wxWindow* parent) {
  if (auto* scroll = wxDynamicCast(parent, wxScrolledWindow))
    WR_EnableAndroidScrolling(scroll);
  for (auto* child : parent->GetChildren()) {
    if (child->GetName() == "wr-android-header" ||
        child->GetName() == "wr-android-title") continue;
    const bool label = wxDynamicCast(child, wxStaticText);
    if (label) WR_EnableAndroidLabelScrolling(child);
    const bool choice = wxDynamicCast(child, wxChoice) ||
                        wxDynamicCast(child, wxComboBox);
    const bool button = wxDynamicCast(child, wxButton);
    if (button) WR_EnableAndroidButton(static_cast<wxButton*>(child));
    const bool check = wxDynamicCast(child, wxCheckBox) ||
                       wxDynamicCast(child, wxRadioButton);
    const bool input = wxDynamicCast(child, wxTextCtrl) ||
                       wxDynamicCast(child, wxSpinCtrl) ||
                       wxDynamicCast(child, wxSpinCtrlDouble);
    const bool list = wxDynamicCast(child, wxListBox) ||
                      wxDynamicCast(child, wxListCtrl);
    if (wxDynamicCast(child, wxSpinCtrl) || wxDynamicCast(child, wxSpinCtrlDouble)) {
      child->SetMaxSize(wxSize(-1, -1));
      child->SetMinSize(wxSize(180, 72));
    }
    if (label || choice || button || check || input || list) {
      wxFont font = child->GetFont();
      font.SetPointSize(16);
      child->SetFont(font);
      if (button)
        child->GetHandle()->setStyleSheet(
            "QPushButton { font-size: 16pt; min-height: 62px; padding: 5px; "
            "border: 1px solid #9fb9c6; border-radius: 8px; "
            "color: #173849; background: white; } "
            "QPushButton:disabled { color: #74818a; background: #e4e8eb; }");
      else if (check)
        child->GetHandle()->setStyleSheet(
            "QCheckBox, QRadioButton { font-size: 16pt; min-height: 64px; } "
            "QCheckBox::indicator, QRadioButton::indicator { width: 30px; height: 30px; }");
      else if (choice) {
        WR_StyleAndroidCombo(child);
        child->GetHandle()->setStyleSheet(
            "QComboBox { font-size: 16pt; min-height: 64px; }");
      } else if (label) {
        const wxColour colour = child->GetForegroundColour();
        child->GetHandle()->setStyleSheet(colour.IsOk()
            ? QString("QLabel { font-size: 16pt; color: %1; }").arg(
                QString::fromUtf8(colour.GetAsString(wxC2S_HTML_SYNTAX).ToUTF8().data()))
            : QString("QLabel { font-size: 16pt; }"));
      }
      else if (input)
        child->GetHandle()->setStyleSheet(
            "QLineEdit, QSpinBox, QDoubleSpinBox { font-size: 16pt; min-height: 64px; }");
      if (choice || button || check || input) {
        const wxSize minimum = child->GetMinSize();
        child->SetMinSize(wxSize(minimum.x, qMax(minimum.y, 72)));
      }
      if (list) {
        auto* view = qobject_cast<QAbstractItemView*>(child->GetHandle());
        if (!view) view = child->GetHandle()->findChild<QAbstractItemView*>();
        if (view) view->setItemDelegate(new WR_AndroidChoiceDelegate(view));
      }
    }
    WR_StyleAndroidControls(child);
  }
}

inline void WR_AddAndroidBookPicker(wxWindow* parent, wxNotebook* book,
                                    size_t insertion = 0) {
  auto* panel = new wxPanel(parent, wxID_ANY);
  auto* row = new wxBoxSizer(wxHORIZONTAL);
  auto* picker = new wxChoice(panel, wxID_ANY);
  for (size_t i = 0; i < book->GetPageCount(); ++i)
    picker->Append(book->GetPageText(i));
  picker->SetSelection(wxMax(0, book->GetSelection()));
  row->Add(picker, 1, wxEXPAND | wxALL, 8);
  panel->SetSizer(row);
  WR_StyleAndroidControls(panel);
  picker->Bind(wxEVT_CHOICE, [picker, book](wxCommandEvent&) {
    book->SetSelection(picker->GetSelection());
  });
  book->Bind(wxEVT_NOTEBOOK_PAGE_CHANGED, [picker](wxBookCtrlEvent& event) {
    if (event.GetSelection() >= 0) picker->SetSelection(event.GetSelection());
    event.Skip();
  });
  if (auto* tabs = qobject_cast<QTabWidget*>(book->GetHandle()))
    tabs->tabBar()->hide();
  parent->GetSizer()->Insert(insertion, panel, 0, wxEXPAND);
}

class WR_AndroidBackFilter : public QObject {
public:
  WR_AndroidBackFilter(wxWindow* dialog, std::function<void()> action,
                      wxWindow* additionalTarget = nullptr)
      : QObject(dialog->GetHandle()), m_dialog(dialog), m_action(action),
        m_additionalTarget(additionalTarget) {
    qApp->installEventFilter(this);
  }
protected:
  bool eventFilter(QObject* target, QEvent* event) override {
    if (event->type() != QEvent::KeyPress &&
        event->type() != QEvent::KeyRelease) return false;
    const auto* key = static_cast<QKeyEvent*>(event);
    if (key->key() != Qt::Key_Back && key->key() != Qt::Key_Escape)
      return false;
    if (event->type() == QEvent::KeyRelease && m_consumedPress) {
      m_consumedPress = false;
      // Keep the sheet registered until Android has processed key-up. Hiding
      // on key-down makes the host see only its chart window on key-up and
      // also invoke its double-Back exit handling for this same gesture.
      m_action();
      return true;
    }
    if (!m_dialog->IsShown() || QApplication::activePopupWidget()) return false;
    QWidget* widget = qobject_cast<QWidget*>(target);
    if (!widget) widget = QApplication::activeWindow();
    if (!widget || (widget->window() != m_dialog->GetHandle()->window() &&
        (!m_additionalTarget || widget->window() !=
            m_additionalTarget->GetHandle()->window())))
      return false;
    if (event->type() == QEvent::KeyPress && !key->isAutoRepeat()) {
      m_consumedPress = true;
    }
    return true;
  }
private:
  wxWindow* m_dialog;
  std::function<void()> m_action;
  wxWeakRef<wxWindow> m_additionalTarget;
  bool m_consumedPress{false};
};

inline void WR_InstallAndroidBack(wxWindow* dialog,
                                  std::function<void()> action,
                                  wxWindow* additionalTarget = nullptr) {
  new WR_AndroidBackFilter(dialog, action, additionalTarget);
}

inline void WR_FitAndroidSheet(wxDialog* dialog) {
  if (!dialog->GetHandle()->isVisible()) return;
  const wxSize canvas = GetCanvasByIndex(0)->GetClientSize();
  if (canvas.x < 100 || canvas.y < 100) return;
  dialog->SetSize(wxSize(canvas.x - 24, canvas.y - 24));
  dialog->CentreOnParent();
}

// Modal wxQt sheets may miss wx show/parent-size notifications. Listen to
// native show and canvas rotation too, after the host layout has settled.
class WR_AndroidSheetFitFilter : public QObject {
public:
  explicit WR_AndroidSheetFitFilter(wxDialog* dialog)
      : QObject(dialog->GetHandle()), m_dialog(dialog),
        m_canvas(GetCanvasByIndex(0)->GetHandle()) {
    dialog->GetHandle()->installEventFilter(this);
    m_canvas->installEventFilter(this);
    QObject::connect(QApplication::primaryScreen(), &QScreen::geometryChanged,
                     this, [this](const QRect&) { Schedule(); });
  }
protected:
  bool eventFilter(QObject* target, QEvent* event) override {
    if (event->type() == QEvent::Show ||
        (target == m_canvas && event->type() == QEvent::Resize))
      Schedule();
    return false;
  }
private:
  void Schedule() {
    QTimer::singleShot(150, this, [this]() {
      if (m_dialog) WR_FitAndroidSheet(m_dialog.get());
    });
  }
  wxWeakRef<wxDialog> m_dialog;
  QPointer<QWidget> m_canvas;
};

// wxQt does not display a dialog caption on Android. Modeless plugin dialogs
// therefore need an in-content way back to the routing window.
inline wxPanel* WR_AddAndroidDoneHeader(
    wxDialog* dialog, const wxString& title,
    std::function<void()> onDone = {}, const wxString& exitLabel = _("Done")) {
  wxSizer* content = dialog->GetSizer();
  if (!content) return nullptr;
  dialog->SetSizer(nullptr, false);

  wxBoxSizer* root = new wxBoxSizer(wxVERTICAL);
  wxPanel* headerPanel = new wxPanel(dialog, wxID_ANY);
  headerPanel->SetName("wr-android-header");
  headerPanel->SetBackgroundColour(wxColour(25, 59, 76));
  wxBoxSizer* header = new wxBoxSizer(wxHORIZONTAL);
  wxStaticText* heading = new wxStaticText(headerPanel, wxID_ANY, title);
  heading->SetName("wr-android-title");
  wxFont font = heading->GetFont();
  font.SetPointSize(20);
  font.SetWeight(wxFONTWEIGHT_BOLD);
  heading->SetFont(font);
  heading->SetForegroundColour(*wxWHITE);
  heading->GetHandle()->setStyleSheet("QLabel { color: white; }");
  header->Add(heading, 1, wxALIGN_CENTER_VERTICAL | wxALL, 10);
  wxButton* done = new wxButton(headerPanel, wxID_ANY, exitLabel);
  done->GetHandle()->setStyleSheet(
      "QPushButton { font-size: 17pt; color: #173849; background-color: white; "
      "border: 1px solid #9fb9c6; border-radius: 8px; padding: 5px; }");
  done->SetMinSize(wxSize(WR_FromDIP(dialog, 110), 72));
  done->Bind(wxEVT_BUTTON, [dialog, onDone](wxCommandEvent&) {
    if (onDone) onDone();
    else dialog->Hide();
  });
  WR_InstallAndroidBack(dialog, [dialog, onDone]() {
    if (onDone) onDone();
    else dialog->Hide();
  });
  const wxWeakRef<wxDialog> weakDialog(dialog);
  dialog->Bind(wxEVT_SHOW, [weakDialog](wxShowEvent& event) {
    if (event.IsShown() && weakDialog)
      weakDialog->CallAfter([weakDialog]() {
        if (weakDialog) WR_FitAndroidSheet(weakDialog.get());
      });
    event.Skip();
  });
  dialog->GetParent()->Bind(wxEVT_SIZE, [weakDialog](wxSizeEvent& event) {
    if (weakDialog) weakDialog->CallAfter([weakDialog]() {
      if (weakDialog) WR_FitAndroidSheet(weakDialog.get());
    });
    event.Skip();
  });
  header->Add(done, 0, wxALL, 8);
  headerPanel->SetSizer(header);
  root->Add(headerPanel, 0, wxEXPAND);
  root->Add(content, 1, wxEXPAND);
  dialog->SetSizer(root, true);
  dialog->SetMinSize(wxSize(0, 0));
  dialog->Layout();
  new WR_AndroidSheetFitFilter(dialog);
  return headerPanel;
}

inline void WR_LayoutAndroidDetailSheet(wxDialog* dialog) {
  struct ScrollPosition { wxScrolledWindow* scroll; int x, y; };
  std::vector<ScrollPosition> positions;
  for (auto* child : dialog->GetChildren()) {
    auto* scroll = wxDynamicCast(child, wxScrolledWindow);
    if (!scroll) continue;
    std::vector<wxStaticText*> labels;
    for (auto* item : scroll->GetChildren()) {
      if (auto* label = wxDynamicCast(item, wxStaticText)) labels.push_back(label);
      if (item->GetName() == "wr-detail-fields")
        for (auto* field : item->GetChildren())
          if (auto* label = wxDynamicCast(field, wxStaticText)) labels.push_back(label);
    }
    QString signature = QString::number(scroll->GetClientSize().x);
    for (auto* label : labels)
      signature += QString::fromUtf8(label->GetLabel().ToUTF8()) + "\n";
    if (scroll->GetHandle()->property("wrFieldLayoutSignature").toString() == signature)
      continue;
    scroll->GetHandle()->setProperty("wrFieldLayoutSignature", signature);
    int x, y;
    scroll->GetViewStart(&x, &y);
    positions.push_back({scroll, x, y});
    scroll->Scroll(0, 0);
    for (auto* label : labels)
      WR_WrapAndroidText(label, label->GetLabel(),
          qMax(200, scroll->GetClientSize().x - 72));
    scroll->Layout();
    scroll->FitInside();
  }
  if (positions.empty()) return;
  dialog->Layout();
  for (const auto& position : positions)
    position.scroll->Scroll(position.x, position.y);
}

inline void WR_BuildAndroidDetailSheet(wxDialog* dialog, wxFlexGridSizer* fields,
                                       const wxString& title) {
  dialog->GetSizer()->Detach(fields);
  auto* scroll = new wxScrolledWindow(dialog, wxID_ANY);
  scroll->SetScrollRate(0, 16);
  // Scroll one content panel. wxQt can reposition individual live labels
  // during their size updates even while the scroll offset is unchanged.
  auto* fieldPanel = new wxPanel(scroll, wxID_ANY);
  fieldPanel->SetName("wr-detail-fields");
  fields->RemoveGrowableCol(1);
  fields->SetCols(1);
  fields->AddGrowableCol(0);
  for (auto* item : fields->GetChildren()) {
    item->SetFlag(wxEXPAND | wxALL);
    item->SetBorder(8);
    if (auto* window = item->GetWindow()) window->Reparent(fieldPanel);
  }
  fieldPanel->SetSizer(fields);
  auto* content = new wxBoxSizer(wxVERTICAL);
  content->Add(fieldPanel, 0, wxEXPAND | wxALL, 16);
  content->AddSpacer(42);
  scroll->SetSizer(content);
  // Hide the original desktop footer, whose dialog owns its windows.
  for (auto* child : dialog->GetChildren())
    if (child != scroll) child->Hide();
  auto* root = new wxBoxSizer(wxVERTICAL);
  root->Add(scroll, 1, wxEXPAND);
  dialog->SetSizer(root, true);
  WR_StyleAndroidControls(scroll);
  WR_AddAndroidDoneHeader(dialog, title);
  const wxSize canvas = GetCanvasByIndex(0)->GetClientSize();
  dialog->SetSize(canvas.x - 24, canvas.y - 24);
  dialog->CentreOnParent();
  WR_LayoutAndroidDetailSheet(dialog);
}

// Display every comparison field without relying on wxQt's report-list cells.
class WR_AndroidComparisonView : public wxPanel {
public:
  WR_AndroidComparisonView(wxWindow* parent, const wxString* columns,
                           size_t count, std::function<void(int)> selected)
      : wxPanel(parent), m_selected(selected) {
    auto* root = new wxBoxSizer(wxVERTICAL);
    m_picker = new wxChoice(this, wxID_ANY);
    root->Add(m_picker, 0, wxEXPAND | wxALL, 12);
    m_scroll = new wxScrolledWindow(this);
    m_scroll->SetScrollRate(0, 16);
    auto* fields = new wxBoxSizer(wxVERTICAL);
    for (size_t i = 0; i < count; ++i) {
      auto* title = new wxStaticText(m_scroll, wxID_ANY, columns[i]);
      title->SetForegroundColour(wxColour(70, 94, 108));
      auto* value = new wxStaticText(m_scroll, wxID_ANY, wxEmptyString);
      fields->Add(title, 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, 12);
      fields->Add(value, 0, wxEXPAND | wxALL, 12);
      m_values.push_back(value);
    }
    fields->AddSpacer(42);
    m_scroll->SetSizer(fields);
    root->Add(m_scroll, 1, wxEXPAND);
    SetSizer(root);
    WR_StyleAndroidControls(this);
    m_picker->Bind(wxEVT_CHOICE, [this](wxCommandEvent&) {
      ShowRow(m_picker->GetSelection());
      m_selected(m_picker->GetSelection());
    });
    const wxWeakRef<WR_AndroidComparisonView> weakView(this);
    Bind(wxEVT_SIZE, [weakView](wxSizeEvent& event) {
      if (weakView) weakView->CallAfter([weakView]() {
        if (weakView) weakView->ShowRow(weakView->m_picker->GetSelection());
      });
      event.Skip();
    });
  }
  void SetRows(const std::vector<std::vector<wxString>>& rows, int selection) {
    if (m_rows == rows && m_picker->GetSelection() == (selection >= 0 ? selection : 0))
      return;
    m_rows = rows;
    // Do not close a native picker while the one-second refresh is running.
    if (!QApplication::activePopupWidget()) {
      m_picker->Clear();
      for (size_t i = 0; i < rows.size(); ++i) {
        const auto& row = rows[i];
        m_picker->Append(wxString::Format(_("Candidate %u"), unsigned(i + 1)) +
            (row.size() > 2 ? " | " + row[2] : wxString()) +
            (!row.empty() && !row[0].IsEmpty() ? " | " + row[0] : wxString()));
      }
      if (!rows.empty()) m_picker->SetSelection(selection >= 0 ? selection : 0);
    }
    ShowRow(m_picker->GetSelection());
  }
private:
  void ShowRow(int index) {
    int x, y;
    m_scroll->GetViewStart(&x, &y);
    m_scroll->Scroll(0, 0);
    Layout();
    for (size_t i = 0; i < m_values.size(); ++i) {
      wxString value = index >= 0 && size_t(index) < m_rows.size() &&
          i < m_rows[index].size() ? m_rows[index][i] : wxString();
      WR_WrapAndroidText(m_values[i], value.IsEmpty()
          ? (i == 0 ? _("No") : _("Unavailable")) : value,
          qMax(200, m_scroll->GetClientSize().x - 72));
    }
    m_scroll->Layout();
    m_scroll->FitInside();
    m_scroll->Scroll(index == m_displayedRow ? x : 0,
                     index == m_displayedRow ? y : 0);
    m_displayedRow = index;
  }
  wxChoice* m_picker;
  int m_displayedRow{-1};
  wxScrolledWindow* m_scroll;
  std::vector<wxStaticText*> m_values;
  std::vector<std::vector<wxString>> m_rows;
  std::function<void(int)> m_selected;
};

#endif
