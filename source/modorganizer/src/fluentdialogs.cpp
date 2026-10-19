#include "fluentdialogs.h"

#include "fluenticons.h"
#include "fluentsettings.h"
#include "revampedtheme.h"

#include <QAbstractItemView>
#include <QAbstractSpinBox>
#include <QComboBox>
#include <QLineEdit>
#include <QCommandLinkButton>
#include <QDialog>
#include <QDialogButtonBox>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QToolBar>
#include <QToolButton>
#include <QRegularExpression>
#include <QScreen>
#include <QTimer>

#ifdef Q_OS_WIN
#include <windows.h>
#include <dwmapi.h>
#endif

#include <algorithm>

namespace
{
struct ButtonColors
{
  const char* face;
  const char* faceHover;
  const char* line;
  const char* text;
  const char* accent;
  const char* accentHover;
  const char* accentText;
};

// the same values as the Fluent v4 theme tokens (Build-FluentTheme.ps1)
const ButtonColors& colors()
{
  static const ButtonColors light{"#FBFBFB", "#F6F6F6", "#E5E5E5", "#1A1A1A",
                                  "#005FB8", "#1A6FC0", "#FFFFFF"};
  static const ButtonColors dark{"#2D2D2D", "#323232", "#3D3D3D", "#FFFFFF",
                                 "#4CC2FF", "#48B1E8", "#000000"};
  static const ButtonColors black{"#252525", "#2B2B2B", "#353535", "#EDEDED",
                                  "#D6D6D6", "#C6C6C6", "#000000"};
  using namespace MOBase;
  if (RevampedTheme::isBlack()) {
    return black;
  }
  return RevampedTheme::isDark() ? dark : light;
}

bool insideItemView(const QWidget* w, const QWidget* root)
{
  for (const QWidget* p = w->parentWidget(); p != nullptr && p != root;
       p = p->parentWidget()) {
    if (qobject_cast<const QAbstractItemView*>(p) != nullptr) {
      return true;
    }
  }
  return false;
}

bool isPrimary(QPushButton* button)
{
  if (button->isDefault()) {
    return true;
  }
  if (auto* box = qobject_cast<QDialogButtonBox*>(button->parentWidget())) {
    const auto role = box->buttonRole(button);
    return role == QDialogButtonBox::AcceptRole || role == QDialogButtonBox::YesRole;
  }
  return false;
}

// a widget's own style sheet wins over the theme's per-dialog rules, which
// sized and tinted buttons one by one (up to 44px, accent and red outlines)
void styleButton(QAbstractButton* button, bool primary)
{
  const ButtonColors& c = colors();
  const QString face   = QString::fromLatin1(primary ? c.accent : c.face);
  const QString hover  = QString::fromLatin1(primary ? c.accentHover : c.faceHover);
  const QString line   = QString::fromLatin1(primary ? c.accent : c.line);
  const QString text   = QString::fromLatin1(primary ? c.accentText : c.text);
  const QString type   = qobject_cast<QToolButton*>(button) != nullptr
                             ? QStringLiteral("QToolButton")
                             : QStringLiteral("QPushButton");

  button->setStyleSheet(
      QStringLiteral(
          "%5 { min-height: 22px; max-height: 22px; padding: 4px %6px; "
          "border-radius: 4px; color: %1; background-color: %2; border: 1px solid %3; }"
          "%5:hover { background-color: %4; }"
          "%5:disabled { color: rgba(128, 128, 128, 170); "
          "background-color: transparent; border-color: %3; }"
          "%5::menu-indicator { subcontrol-position: right center; right: 8px; }"
          "QToolButton::menu-button { width: 22px; border: none; "
          "border-left: 1px solid %3; }")
          .arg(text, face, line, hover, type)
          .arg(qobject_cast<QToolButton*>(button) != nullptr ? 8 : 16));
}

// banner cards at the top of dialogs become a flat page title, like the
// Settings pages; their decorative icon badges go away
void flattenHeaders(QDialog* dialog)
{
  static const QRegularExpression headerName(
      QStringLiteral("header(card|panel)?$"), QRegularExpression::CaseInsensitiveOption);

  for (QWidget* w : dialog->findChildren<QWidget*>()) {
    if (!headerName.match(w->objectName()).hasMatch() ||
        qobject_cast<QAbstractItemView*>(w) != nullptr ||
        w->objectName().contains(QLatin1String("list"), Qt::CaseInsensitive)) {
      continue;
    }
    w->setStyleSheet(QStringLiteral("#%1 { background-color: transparent; border: none; }")
                         .arg(w->objectName()));
    for (QLabel* label : w->findChildren<QLabel*>()) {
      if (label->text().isEmpty() && label->pixmap().isNull() == false) {
        label->hide();
      }
    }
  }
}

// dialogs open at a comfortable size on any display: at most 82% of the work
// area (and 860px tall), so a 1080p screen shows them whole and a 1440p one
// does not get a wall of a window; saved sizes above that are trimmed too
void fitSize(QDialog* dialog)
{
  if (dialog->isMaximized() || dialog->isFullScreen() || dialog->screen() == nullptr) {
    return;
  }

  const QRect area = dialog->screen()->availableGeometry();
  const int maxWidth  = std::min(1240, static_cast<int>(area.width() * 0.82));
  const int maxHeight = std::min(860, static_cast<int>(area.height() * 0.82));

  QSize minimum = dialog->minimumSize();
  minimum       = minimum.boundedTo(QSize(maxWidth, maxHeight));
  dialog->setMinimumSize(minimum);

  const QSize size = dialog->size().boundedTo(QSize(maxWidth, maxHeight));
  if (size == dialog->size()) {
    return;
  }
  dialog->resize(size);

  // keep it centered where it was going to open
  const QWidget* anchor = dialog->parentWidget() ? dialog->parentWidget()->window() : nullptr;
  const QPoint center = anchor ? anchor->frameGeometry().center() : area.center();
  QRect frame(QPoint(0, 0), dialog->frameGeometry().size().boundedTo(area.size()));
  frame.moveCenter(center);
  frame.moveLeft(std::clamp(frame.left(), area.left(), area.right() - frame.width()));
  frame.moveTop(std::clamp(frame.top(), area.top(), area.bottom() - frame.height()));
  dialog->move(frame.topLeft());
}

// the native title bar follows the theme's light or dark mode; Qt may set the
// frame mode again while the dialog settles, so it is applied once more after
void matchTitleBar(QDialog* dialog)
{
#ifdef Q_OS_WIN
  const auto apply = [dialog] {
    const BOOL dark   = MOBase::RevampedTheme::isDark() ? TRUE : FALSE;
    const HWND handle = reinterpret_cast<HWND>(dialog->winId());
    // DWMWA_USE_IMMERSIVE_DARK_MODE
    DwmSetWindowAttribute(handle, 20, &dark, sizeof(dark));
  };
  apply();
  QTimer::singleShot(0, dialog, apply);
#else
  Q_UNUSED(dialog);
#endif
}

// dialogs that are mostly options get Settings rows
bool isOptionsDialog(const QDialog* dialog)
{
  static const QStringList names = {
      QStringLiteral("ProfilesDialog"),        QStringLiteral("EditExecutablesDialog"),
      QStringLiteral("CreateInstanceDialog"),  QStringLiteral("InstanceManagerDialog"),
      QStringLiteral("TransferSavesDialog"),   QStringLiteral("CategoriesDialog"),
      QStringLiteral("LootDialog"),            QStringLiteral("ProblemsDialog"),
  };
  return names.contains(dialog->objectName());
}
}  // namespace

namespace FluentDialogs
{

void polish(QDialog* dialog)
{
  if (dialog == nullptr || dialog->property("fluentPolished").toBool() ||
      qobject_cast<QMessageBox*>(dialog) != nullptr) {
    return;
  }
  dialog->setProperty("fluentPolished", true);

  fitSize(dialog);
  flattenHeaders(dialog);

  if (isOptionsDialog(dialog)) {
    FluentSettings::restyle(dialog, false);
  }

  for (QPushButton* button : dialog->findChildren<QPushButton*>()) {
    // command links and tile buttons keep their size
    if (qobject_cast<QCommandLinkButton*>(button) != nullptr || button->minimumHeight() > 48) {
      continue;
    }
    if (button->property("fluentKeepStyle").toBool() || insideItemView(button, dialog) ||
        !button->styleSheet().isEmpty()) {
      continue;
    }
    styleButton(button, isPrimary(button));
  }

  // fields at the Windows 11 height (the per-dialog rules made many 40px)
  for (QWidget* field : dialog->findChildren<QWidget*>()) {
    const char* type = qobject_cast<QLineEdit*>(field)          ? "QLineEdit"
                       : qobject_cast<QComboBox*>(field)        ? "QComboBox"
                       : qobject_cast<QAbstractSpinBox*>(field) ? "QAbstractSpinBox"
                                                                : nullptr;
    QWidget* parent = field->parentWidget();
    if (type == nullptr || !field->styleSheet().isEmpty() || insideItemView(field, dialog) ||
        qobject_cast<QComboBox*>(parent) != nullptr ||
        qobject_cast<QAbstractSpinBox*>(parent) != nullptr ||
        field->property("fluentKeepStyle").toBool()) {
      continue;
    }
    field->setStyleSheet(
        QStringLiteral("%1 { min-height: 22px; max-height: 22px; padding: 4px 10px; "
                       "border-radius: 4px; }")
            .arg(QLatin1String(type)));
  }

  // text buttons built as tool buttons (split "Add" buttons, list actions);
  // icon-only and toolbar buttons keep their own look
  for (QToolButton* button : dialog->findChildren<QToolButton*>()) {
    if (button->toolButtonStyle() == Qt::ToolButtonIconOnly || button->text().isEmpty() ||
        button->autoRaise() || qobject_cast<QToolBar*>(button->parentWidget()) != nullptr ||
        button->property("fluentKeepStyle").toBool() || insideItemView(button, dialog) ||
        !button->styleSheet().isEmpty() || button->minimumHeight() > 48) {
      continue;
    }
    styleButton(button, false);
  }

  // restyling re-applies icons the theme sets on buttons; ink them again
  for (QAbstractButton* button : dialog->findChildren<QAbstractButton*>()) {
    FluentIcons::apply(button);
  }

  matchTitleBar(dialog);
}

}  // namespace FluentDialogs
