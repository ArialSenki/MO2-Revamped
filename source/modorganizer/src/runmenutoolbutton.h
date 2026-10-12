#pragma once

#include <QCoreApplication>
#include <QEvent>
#include <QMenu>
#include <QMetaObject>
#include <QMouseEvent>
#include <QPointer>
#include <QTimer>
#include <QToolButton>

class RunMenuToolButton final : public QToolButton
{
public:
  explicit RunMenuToolButton(QWidget* parent = nullptr) : QToolButton(parent) {}

  static constexpr int menuButtonWidth() { return 34; }

protected:
  bool eventFilter(QObject* watched, QEvent* event) override
  {
    Q_UNUSED(watched);

    if (m_dismissalReleasePending) {
      if (event->type() == QEvent::MouseButtonRelease) {
        auto* mouseEvent = static_cast<QMouseEvent*>(event);
        if (mouseEvent->button() == Qt::LeftButton) {
          m_dismissalReleasePending = false;
          removeApplicationMouseFilter();
          event->accept();
          return true;
        }
      }
      return false;
    }

    if (event->type() != QEvent::MouseButtonPress &&
        event->type() != QEvent::MouseButtonDblClick) {
      return false;
    }

    auto* mouseEvent = static_cast<QMouseEvent*>(event);
    if (mouseEvent->button() != Qt::LeftButton || m_connectedMenu.isNull() ||
        !m_connectedMenu->isVisible() ||
        !menuButtonRect().contains(
            mapFromGlobal(mouseEvent->globalPosition().toPoint()))) {
      return false;
    }

    // QMenu has the mouse grab while exec() is running. Catch the arrow press
    // before the menu dismisses itself so the same click cannot be replayed to
    // the button and reopen it.
    m_dismissalReleasePending = true;
    QTimer::singleShot(1000, this, [this] {
      if (m_dismissalReleasePending) {
        m_dismissalReleasePending = false;
        removeApplicationMouseFilter();
      }
    });
    m_connectedMenu->hide();
    setMenuButtonActive(false);
    event->accept();
    return true;
  }

  void mousePressEvent(QMouseEvent* event) override
  {
    if (event->button() == Qt::LeftButton &&
        popupMode() == QToolButton::MenuButtonPopup && menu() != nullptr &&
        menuButtonRect().contains(event->position().toPoint())) {
      // Keep both arrow events away from QToolButton's main-click handling so
      // this side can never trigger the separate Run action.
      m_menuButtonPressed = true;
      setMenuButtonActive(true);
      event->accept();
      return;
    }

    QToolButton::mousePressEvent(event);
  }

  void mouseReleaseEvent(QMouseEvent* event) override
  {
    if (m_menuButtonPressed) {
      const bool openMenu =
          event->button() == Qt::LeftButton &&
          menuButtonRect().contains(event->position().toPoint());
      m_menuButtonPressed = false;
      event->accept();

      if (openMenu) {
        showMenuAtArrow();
      } else if (menu() == nullptr || !menu()->isVisible()) {
        setMenuButtonActive(false);
      }
      return;
    }

    QToolButton::mouseReleaseEvent(event);
  }

private:
  void setMenuButtonActive(bool active)
  {
    if (property("menuButtonActive").toBool() == active) {
      return;
    }

    setProperty("menuButtonActive", active);
    style()->unpolish(this);
    style()->polish(this);
    update();
  }

  QRect menuButtonRect() const
  {
    const int arrowWidth = qMin(width(), menuButtonWidth());
    return QRect(width() - arrowWidth, 0, arrowWidth, height());
  }

  void showMenuAtArrow()
  {
    QPointer<QMenu> popupMenu(menu());
    if (popupMenu.isNull()) {
      return;
    }

    if (m_connectedMenu != popupMenu) {
      QObject::disconnect(m_menuHideConnection);
      m_connectedMenu = popupMenu;
      m_menuHideConnection = connect(
          popupMenu, &QMenu::aboutToHide, this,
          [this] { setMenuButtonActive(false); });
    }

    // Do not replay the dismissal click back to the button; that replay was
    // interpreted as a second request to open the same menu.
    popupMenu->setAttribute(Qt::WA_NoMouseReplay);

    if (popupMenu->isVisible()) {
      popupMenu->hide();
      setMenuButtonActive(false);
      return;
    }

    QPointer<RunMenuToolButton> buttonGuard(this);
    const QPoint anchorBottomRight = mapToGlobal(QPoint(width(), height()));
    const int menuWidth = popupMenu->sizeHint().width();
    const QPoint popupPosition(anchorBottomRight.x() - menuWidth,
                               anchorBottomRight.y());

    // QMenu can update its final width after aboutToShow handlers have
    // populated or decorated the actions. Re-anchor once it is visible, using
    // the actual popup width so its right edge stays on the arrow.
    QTimer::singleShot(0, popupMenu.data(), [popupMenu, anchorBottomRight] {
      if (popupMenu) {
        popupMenu->move(anchorBottomRight.x() - popupMenu->width(),
                        anchorBottomRight.y());
      }
    });

    setMenuButtonActive(true);
    installApplicationMouseFilter();
    popupMenu->exec(popupPosition);

    if (buttonGuard) {
      buttonGuard->setMenuButtonActive(false);
      if (!buttonGuard->m_dismissalReleasePending) {
        buttonGuard->removeApplicationMouseFilter();
      }
    }
  }

  void installApplicationMouseFilter()
  {
    if (QCoreApplication* app = QCoreApplication::instance()) {
      app->installEventFilter(this);
      m_applicationMouseFilterInstalled = true;
    }
  }

  void removeApplicationMouseFilter()
  {
    if (!m_applicationMouseFilterInstalled) {
      return;
    }

    if (QCoreApplication* app = QCoreApplication::instance()) {
      app->removeEventFilter(this);
    }
    m_applicationMouseFilterInstalled = false;
  }

  QPointer<QMenu> m_connectedMenu;
  QMetaObject::Connection m_menuHideConnection;
  bool m_menuButtonPressed = false;
  bool m_applicationMouseFilterInstalled = false;
  bool m_dismissalReleasePending = false;
};
