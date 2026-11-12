#include "dialogplacement.h"

#include "fluentdialogs.h"

#include <log.h>

#include <QApplication>
#include <QCursor>
#include <QDialog>
#include <QGuiApplication>
#include <QLayout>
#include <QMainWindow>
#include <QMessageBox>
#include <QPointer>
#include <QTimer>
#include <QScreen>
#include <QWindow>

using namespace MOBase;

namespace
{

// a window a dialog can sit over: on screen, not a popup, a tooltip or the
// splash screen
bool usable(const QWidget* window)
{
  // a native file dialog's Qt widget is "visible" while Windows shows the
  // real one: never an anchor
  if (window == nullptr || !window->isVisible() || window->isMinimized() ||
      window->testAttribute(Qt::WA_DontShowOnScreen)) {
    return false;
  }
  switch (window->windowType()) {
  case Qt::Popup:
  case Qt::ToolTip:
  case Qt::SplashScreen:
    return false;
  default:
    return true;
  }
}

QWidget* parentWindow(const QWidget* widget)
{
  return widget->parentWidget() != nullptr ? widget->parentWidget()->window() : nullptr;
}

// whether window is modal's own window or one of its dialogs (it sits above)
bool sitsOn(const QWidget* window, const QWidget* modal)
{
  for (const QWidget* w = window; w != nullptr; w = parentWindow(w)) {
    if (w == modal) {
      return true;
    }
  }
  return false;
}

QScreen* screenFor(const QWidget* anchor)
{
  if (anchor != nullptr) {
    if (QScreen* screen = QGuiApplication::screenAt(anchor->frameGeometry().center())) {
      return screen;
    }
    if (anchor->screen() != nullptr) {
      return anchor->screen();
    }
  }
  if (QScreen* screen = QGuiApplication::screenAt(QCursor::pos())) {
    return screen;
  }
  return QGuiApplication::primaryScreen();
}

// the native frame isn't known before the first show: a Windows title bar
// and borders
QSize frameOf(const QWidget* dialog)
{
  if (dialog->windowFlags().testFlag(Qt::FramelessWindowHint)) {
    return QSize(0, 0);
  }
  QSize frame = dialog->frameGeometry().size() - dialog->geometry().size();
  if (frame.width() <= 0 || frame.height() <= 0) {
    frame = QSize(16, 39);
  }
  return frame;
}

}  // namespace

QWidget* DialogPlacement::anchorFor(const QWidget* dialog)
{
  QWidget* parent = parentWindow(dialog);

  // the dialog on top (the instance wizard, Settings, an installer): a
  // window under it would open behind it
  QWidget* modal = QApplication::activeModalWidget();
  if (modal != nullptr) {
    modal = modal->window();
  }
  if (usable(modal) && modal != dialog && !dialog->isAncestorOf(modal)) {
    if (usable(parent) && sitsOn(parent, modal)) {
      return parent;
    }
    // a window-modal dialog blocks only its own window: a dialog opened from
    // another window stays with that one (owned by the modal, it would block
    // the modal's window instead of its own)
    if (modal->windowModality() == Qt::ApplicationModal || !usable(parent)) {
      return modal;
    }
  }

  if (usable(parent)) {
    return parent;
  }
  QWidget* active = QApplication::activeWindow();
  if (active != nullptr && active != dialog && usable(active->window())) {
    return active->window();
  }
  for (QWidget* window : QApplication::topLevelWidgets()) {
    if (window != dialog && qobject_cast<QMainWindow*>(window) != nullptr && usable(window)) {
      return window;
    }
  }
  return nullptr;
}

void DialogPlacement::place(QDialog* dialog, bool fluentSize)
{
  if (dialog == nullptr || !dialog->isWindow()) {
    return;
  }

  QWidget* anchor = anchorFor(dialog);

  // Windows keeps an owned window above its owner, and gives it no taskbar
  // button of its own. Set on every show: an earlier owner may be gone.
  // With nothing of MO2's on screen (the theme question at startup), no
  // owner: a hidden owner would leave it with no taskbar button
  dialog->winId();
  if (QWindow* window = dialog->windowHandle()) {
    if (anchor != nullptr) {
      anchor->winId();
      window->setTransientParent(anchor->windowHandle());
    } else if (!usable(parentWindow(dialog))) {
      window->setTransientParent(nullptr);
    }
  }

  // the place only on the first show: a dialog shown again (the filter
  // options, a progress dialog, a tool window) stays where the user left it
  if (dialog->property("mo2rPlaced").toBool() || dialog->property("mo2rOwnPosition").toBool() ||
      (dialog->windowState() &
       (Qt::WindowMaximized | Qt::WindowFullScreen | Qt::WindowMinimized))) {
    return;
  }
  switch (dialog->windowType()) {
  case Qt::Popup:
  case Qt::Tool:
  case Qt::ToolTip:
  case Qt::SplashScreen:
    return;
  default:
    break;
  }
  dialog->setProperty("mo2rPlaced", true);

  // its final size: the Fluent polish reshapes some dialogs after Qt laid
  // them out, and a dialog that didn't restore a size takes its new hint
  if (QLayout* layout = dialog->layout()) {
    layout->activate();
  }
  if (!dialog->testAttribute(Qt::WA_Resized) && qobject_cast<QMessageBox*>(dialog) == nullptr) {
    dialog->adjustSize();
  }

  QScreen* screen = screenFor(anchor);
  if (screen == nullptr) {
    return;
  }
  const QRect available = screen->availableGeometry();
  const QSize frame     = frameOf(dialog);

  // no larger than the work area (a 768p display, 150% scaling, a smaller
  // monitor), and with fluentSize no larger than dialogs open at
  QSize maxSize(qMax(320, available.width() - frame.width()),
                qMax(240, available.height() - frame.height()));
  if (fluentSize) {
    maxSize = maxSize.boundedTo(FluentDialogs::maxSize(available));
  }
  if (dialog->minimumWidth() > maxSize.width() || dialog->minimumHeight() > maxSize.height()) {
    dialog->setMinimumSize(dialog->minimumSize().boundedTo(maxSize));
  }
  const QSize fitted = dialog->size().boundedTo(maxSize);
  if (fitted != dialog->size()) {
    dialog->resize(fitted);
  }

  // a window that scrolls only a little opens a little taller: done here,
  // before it's centred, instead of growing after its first frame
  FluentDialogs::growForShortScroll(dialog);

  // centred over the anchor's frame, or on the screen, inside the work area
  const QSize outer   = dialog->size() + frame;
  const QPoint middle = anchor != nullptr ? anchor->frameGeometry().center()
                                          : available.center();
  QPoint position(middle.x() - outer.width() / 2, middle.y() - outer.height() / 2);
  position.setX(qBound(available.left(), position.x(),
                       qMax(available.left(), available.right() - outer.width() + 1)));
  position.setY(qBound(available.top(), position.y(),
                       qMax(available.top(), available.bottom() - outer.height() + 1)));
  dialog->move(position);

  // Windows doesn't let a program in the background take the front (a
  // question after a game run, an nxm link from the browser): its taskbar
  // button flashes instead of the dialog waiting unseen
  // (not the window that covers MO2 while a game runs)
  if (QGuiApplication::applicationState() != Qt::ApplicationActive &&
      dialog->objectName() != QLatin1String("UILockerDialog")) {
    QPointer<QDialog> shown(dialog);
    QTimer::singleShot(0, dialog, [shown] {
      if (shown && shown->isVisible()) {
        QApplication::alert(shown);
      }
    });
  }
}
