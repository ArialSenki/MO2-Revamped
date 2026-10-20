#include "fluentdialogs.h"

#include "fluenticons.h"
#include "fluentsettings.h"
#include "revampedtheme.h"

#include <QAbstractItemView>
#include <QDialog>
#include <QDialogButtonBox>
#include <QGridLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLayout>
#include <QMessageBox>
#include <QPushButton>
#include <QToolBar>
#include <QToolButton>
#include <QRegularExpression>
#include <QTimer>

#ifdef Q_OS_WIN
#include <windows.h>
#include <dwmapi.h>
#endif

#include <algorithm>

namespace
{
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

// gives a widget a dynamic property the theme styles it by; only the widget
// itself is repolished, which is far cheaper than a style sheet of its own
void setRole(QWidget* widget, const char* name)
{
  if (widget->property(name).toBool()) {
    return;
  }
  widget->setProperty(name, true);
  widget->style()->unpolish(widget);
  widget->style()->polish(widget);
}

// banner cards at the top of dialogs become a flat page title, like the
// Settings pages; their decorative icon badges go away
void flattenHeaders(QDialog* dialog)
{
  static const QRegularExpression headerName(
      QStringLiteral("(header|intro)(card|panel)?$"), QRegularExpression::CaseInsensitiveOption);

  // the theme draws the banners flat (Build-FluentTheme.ps1); their
  // decorative icon badges go away, a program logo stays
  for (QWidget* w : dialog->findChildren<QWidget*>()) {
    if (!headerName.match(w->objectName()).hasMatch() ||
        qobject_cast<QAbstractItemView*>(w) != nullptr) {
      continue;
    }
    // a flat title needs no card padding: its text lines up with the content
    if (QLayout* layout = w->layout(); layout != nullptr && w->parentWidget() == dialog) {
      const QMargins m = layout->contentsMargins();
      layout->setContentsMargins(qMin(m.left(), 4), m.top(), qMin(m.right(), 4), m.bottom());
    }
    for (QLabel* label : w->findChildren<QLabel*>()) {
      // a label the theme gives a frame indents its text by half an "x"
      // (7 px for a title); flat titles start where their subtitle starts
      if (label->indent() < 0 && label->frameWidth() > 0) {
        label->setIndent(0);
      }
      if (label->text().isEmpty() && label->pixmap().isNull() == false &&
          !label->objectName().contains(QLatin1String("Logo"), Qt::CaseInsensitive)) {
        label->hide();
      }
    }
  }
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

// the Elden Ring plugin windows (Python): their own card layout stays, but
// their check boxes become switch rows and their heading badges go away
bool isPluginDialog(const QDialog* dialog)
{
  const QString name = dialog->objectName();
  return name.startsWith(QLatin1String("EldenRing")) ||
         name.startsWith(QLatin1String("NativeProfile"));
}

// the round icon badges next to the plugin card headings; Windows 11 cards
// head with plain text
void hideHeadingBadges(QDialog* dialog)
{
  for (QLabel* label : dialog->findChildren<QLabel*>()) {
    if (label->objectName().endsWith(QLatin1String("Icon")) && label->maximumWidth() <= 40 &&
        label->maximumHeight() <= 40) {
      label->hide();
    }
  }
}

// table headings start where their column's text starts, like the main
// window's lists
void alignHeaders(QDialog* dialog)
{
  for (QHeaderView* header : dialog->findChildren<QHeaderView*>()) {
    if (header->orientation() == Qt::Horizontal && header->styleSheet().isEmpty()) {
      header->setDefaultAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    }
  }
}

// dialogs that are mostly options get Settings rows
bool isOptionsDialog(const QDialog* dialog)
{
  static const QStringList names = {
      QStringLiteral("ProfilesDialog"),        QStringLiteral("EditExecutablesDialog"),
      QStringLiteral("CreateInstanceDialog"),  QStringLiteral("InstanceManagerDialog"),
      QStringLiteral("TransferSavesDialog"),
      QStringLiteral("LootDialog"),            QStringLiteral("ProblemsDialog"),
  };
  return names.contains(dialog->objectName());
}
}  // namespace

namespace FluentDialogs
{

QSize maxSize(const QRect& available)
{
  return QSize(std::min(1240, static_cast<int>(available.width() * 0.82)),
               std::min(860, static_cast<int>(available.height() * 0.82)));
}

bool polished(const QDialog* dialog)
{
  return dialog->property("fluentPolished").toBool();
}

void polish(QDialog* dialog)
{
  if (dialog == nullptr || polished(dialog) ||
      qobject_cast<QMessageBox*>(dialog) != nullptr) {
    return;
  }
  dialog->setProperty("fluentPolished", true);

  flattenHeaders(dialog);

  if (isOptionsDialog(dialog)) {
    FluentSettings::restyle(dialog, false);
  } else if (isPluginDialog(dialog)) {
    hideHeadingBadges(dialog);
    FluentSettings::switchRows(dialog);
    // notes were indented to line up with a check box's text; with switch
    // rows they span the card like a Windows 11 info bar
    for (QLabel* note : dialog->findChildren<QLabel*>()) {
      const QMargins m = note->contentsMargins();
      if (note->objectName().endsWith(QLatin1String("Note")) && m.left() > 16) {
        note->setContentsMargins(8, m.top(), m.right(), m.bottom());
      }
    }
  }
  alignHeaders(dialog);

  // page titles in semibold like Windows 11 (some .ui files make them bold)
  const int bodySize = dialog->font().pointSize();
  for (QLabel* label : dialog->findChildren<QLabel*>()) {
    QFont f = label->font();
    if (f.weight() > QFont::DemiBold && f.pointSize() >= bodySize + 2) {
      f.setWeight(QFont::DemiBold);
      label->setFont(f);
    }
  }

  // sizes and colors come from the theme's generic dialog rules; the
  // primary button and text tool buttons need a role. The accent stays on the
  // primary button: Qt moves "default" to whichever button has focus.
  for (QPushButton* button : dialog->findChildren<QPushButton*>()) {
    if (isPrimary(button) && !insideItemView(button, dialog)) {
      setRole(button, "fluentPrimary");
    }
  }
  for (QToolButton* button : dialog->findChildren<QToolButton*>()) {
    // an "icon only" tool button without an icon shows its text
    const bool iconOnly =
        button->toolButtonStyle() == Qt::ToolButtonIconOnly && !button->icon().isNull();
    if (iconOnly || button->text().isEmpty() ||
        button->autoRaise() || qobject_cast<QToolBar*>(button->parentWidget()) != nullptr ||
        button->property("fluentKeepStyle").toBool() || insideItemView(button, dialog) ||
        !button->styleSheet().isEmpty()) {
      continue;
    }
    setRole(button, "fluentButton");
  }

  // restyling re-applies icons the theme sets on buttons; ink them again
  for (QAbstractButton* button : dialog->findChildren<QAbstractButton*>()) {
    FluentIcons::apply(button);
  }

  matchTitleBar(dialog);
}

void polishMessageBox(QMessageBox* box)
{
  if (box == nullptr || box->property("fluentPolished").toBool()) {
    return;
  }
  box->setProperty("fluentPolished", true);

  char16_t glyph = 0;
  auto tone      = FluentIcons::Tone::Accent;
  switch (box->icon()) {
  case QMessageBox::Question:
    box->setIcon(QMessageBox::NoIcon);
    break;
  case QMessageBox::Information:
    glyph = 0xE946;  // Info
    break;
  case QMessageBox::Warning:
    glyph = 0xE7BA;  // Warning
    tone  = FluentIcons::Tone::Caution;
    break;
  case QMessageBox::Critical:
    glyph = 0xEA39;  // ErrorBadge
    tone  = FluentIcons::Tone::Critical;
    break;
  default:
    break;
  }
  if (glyph != 0) {
    box->setIconPixmap(
        FluentIcons::glyph(glyph, tone).pixmap(QSize(28, 28), box->devicePixelRatioF()));
  }

  // the roomy padding of a Windows 11 dialog, and a width that does not wrap
  // a short question onto a cramped line
  if (auto* grid = qobject_cast<QGridLayout*>(box->layout())) {
    grid->setContentsMargins(24, 20, 24, 20);
    grid->setVerticalSpacing(16);
    // Qt indents the text with a spacer column (15 px without an icon, 7 px
    // after one) that the button row spans, so the grid's spacing comes on
    // top of it; Windows 11 starts the text at the margin, or 16 px past the
    // icon
    grid->setHorizontalSpacing(0);
    const bool hasIcon = !box->iconPixmap().isNull();
    for (int i = 0; i < grid->count(); ++i) {
      QSpacerItem* spacer = grid->itemAt(i)->spacerItem();
      if (spacer != nullptr && spacer->sizeHint().height() <= 1) {
        spacer->changeSize(hasIcon ? 16 : 0, 1, QSizePolicy::Fixed, QSizePolicy::Fixed);
      }
    }
    grid->invalidate();
  }
  if (auto* text = box->findChild<QLabel*>(QStringLiteral("qt_msgbox_label"))) {
    text->setMinimumWidth(qMax(text->minimumWidth(), 320));
  }

  matchTitleBar(box);
}

}  // namespace FluentDialogs
