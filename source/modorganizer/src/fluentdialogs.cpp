#include "fluentdialogs.h"

#include "fluentchoices.h"
#include "fluenticons.h"
#include "fluentsettings.h"
#include "revampedtheme.h"
#include "settings.h"

#include <QAbstractItemView>
#include <QCommandLinkButton>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFrame>
#include <QGridLayout>
#include <QHeaderView>
#include <QInputDialog>
#include <QKeyEvent>
#include <QLabel>
#include <QLayout>
#include <QMessageBox>
#include <QProgressDialog>
#include <QPushButton>
#include <QToolBar>
#include <QToolButton>
#include <QRegularExpression>
#include <QGuiApplication>
#include <QScreen>
#include <QScrollArea>
#include <QScrollBar>
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
  // a list of choices: none of them stands out over the others
  if (qobject_cast<QCommandLinkButton*>(button) != nullptr) {
    return false;
  }
  auto* box = qobject_cast<QDialogButtonBox*>(button->parentWidget());
  if (box != nullptr) {
    const auto role = box->buttonRole(button);
    if (role == QDialogButtonBox::AcceptRole || role == QDialogButtonBox::YesRole) {
      return true;
    }
    // Cancel can be what Enter presses, but the accent stays on the action
    if (role == QDialogButtonBox::RejectRole || role == QDialogButtonBox::NoRole) {
      const auto buttons = box->buttons();
      const bool hasAction =
          std::any_of(buttons.begin(), buttons.end(), [box](QAbstractButton* other) {
            const auto r = box->buttonRole(other);
            return r == QDialogButtonBox::AcceptRole || r == QDialogButtonBox::YesRole;
          });
      if (hasAction) {
        return false;
      }
    }
  }
  return button->isDefault();
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

// the shape of FluentContentDialog for the dialogs Qt and uibase build
// themselves: the dialog's layout is emptied (their widgets stay its children)
// and filled with a title, a body for the dialog's own widgets and the band of
// buttons. The layout object stays when it is a QVBoxLayout, which is what
// QInputDialog and uibase's size constraint rely on.
struct ContentShape
{
  QVBoxLayout* body;
  QHBoxLayout* footer;
};

ContentShape toContentShape(QDialog* dialog, const QString& title,
                            const QPixmap& titleIcon = {}, int minimumWidth = 440)
{
  auto* root = qobject_cast<QVBoxLayout*>(dialog->layout());
  if (root != nullptr) {
    while (QLayoutItem* item = root->takeAt(0)) {
      // a nested layout is its own item
      if (QLayout* nested = item->layout()) {
        delete nested;
      } else {
        delete item;
      }
    }
  } else {
    delete dialog->layout();
    root = new QVBoxLayout(dialog);
  }
  root->setContentsMargins(0, 0, 0, 0);
  root->setSpacing(0);
  setRole(dialog, "fluentContentDialog");

  // a layout rather than a widget: QMessageBox measures its layout while it
  // changes a label's word wrap, which a widget in between would hide from it
  auto* body = new QVBoxLayout;
  root->addLayout(body, 1);
  body->setContentsMargins(24, 20, 24, 24);
  body->setSpacing(8);
  body->addSpacerItem(new QSpacerItem(minimumWidth, 0, QSizePolicy::Fixed, QSizePolicy::Fixed));

  if (!title.trimmed().isEmpty() || !titleIcon.isNull()) {
    auto* head = new QHBoxLayout;
    head->setContentsMargins(0, 0, 0, 0);
    head->setSpacing(12);
    if (!titleIcon.isNull()) {
      auto* icon = new QLabel(dialog);
      icon->setObjectName(QStringLiteral("fluentDialogIcon"));
      icon->setPixmap(titleIcon);
      icon->setAlignment(Qt::AlignLeft | Qt::AlignTop);
      head->addWidget(icon, 0, Qt::AlignTop);
    }
    auto* label = new QLabel(title, dialog);
    label->setObjectName(QStringLiteral("fluentDialogTitle"));
    label->setWordWrap(true);
    head->addWidget(label, 1, Qt::AlignVCenter);
    body->addLayout(head);
  }

  auto* band = new QFrame(dialog);
  band->setObjectName(QStringLiteral("fluentDialogFooter"));
  auto* footer = new QHBoxLayout(band);
  footer->setContentsMargins(24, 24, 24, 24);
  footer->setSpacing(8);
  root->addWidget(band);

  return {body, footer};
}

// a button box's buttons share the band's width, as in FluentContentDialog; a
// box lays itself out again when its style changes, so this runs after the
// show as well
// Enter on a band button presses that button. These buttons have autoDefault off,
// which keeps the accent on the action, and Qt would press the default one instead
class EnterPressesFocused : public QObject
{
public:
  using QObject::QObject;

protected:
  bool eventFilter(QObject* watched, QEvent* event) override
  {
    if (event->type() == QEvent::KeyPress) {
      const int key = static_cast<QKeyEvent*>(event)->key();
      if (key == Qt::Key_Return || key == Qt::Key_Enter) {
        if (auto* button = qobject_cast<QAbstractButton*>(watched);
            button != nullptr && button->isEnabled()) {
          button->click();
          return true;
        }
      }
    }
    return QObject::eventFilter(watched, event);
  }
};

void enterPressesItself(QAbstractButton* button)
{
  button->installEventFilter(new EnterPressesFocused(button));
}

void equalButtons(QDialogButtonBox* box)
{
  const auto apply = [box] {
    if (box->layout() == nullptr) {
      return;
    }
    for (int i = 0; i < box->layout()->count(); ++i) {
      if (QSpacerItem* spacer = box->layout()->itemAt(i)->spacerItem()) {
        spacer->changeSize(0, 0, QSizePolicy::Fixed, QSizePolicy::Fixed);
      }
    }
    for (QAbstractButton* button : box->buttons()) {
      button->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
      button->setMinimumWidth(110);
    }
    box->layout()->invalidate();
  };
  apply();
  QTimer::singleShot(0, box, apply);
}

// a button in the band or a card in the body that presses a hidden original,
// which keeps whatever its dialog connected to it
void pressesOriginal(QAbstractButton* copy, QAbstractButton* original)
{
  copy->setEnabled(original->isEnabled());
  QObject::connect(copy, &QAbstractButton::clicked, original, &QAbstractButton::click);
}

// Windows 11 dialogs ask in words, as polishMessageBox() does for message boxes
bool isQuestionIcon(const QDialog* dialog, const QPixmap& pixmap)
{
  const QIcon question =
      dialog->style()->standardIcon(QStyle::SP_MessageBoxQuestion, nullptr, dialog);
  return question.pixmap(pixmap.size()).toImage() == pixmap.toImage();
}

// uibase's TaskDialog: the main text is the title, choices with a description
// are option cards, those without one are the band's buttons
void shapeTaskDialog(QDialog* dialog)
{
  auto* main      = dialog->findChild<QLabel*>(QStringLiteral("main"));
  auto* top       = dialog->findChild<QWidget*>(QStringLiteral("topPanel"));
  auto* contents  = dialog->findChild<QWidget*>(QStringLiteral("contentPanel"));
  auto* contentLb = dialog->findChild<QLabel*>(QStringLiteral("content"));
  auto* commands  = dialog->findChild<QWidget*>(QStringLiteral("commandButtons"));
  auto* remember  = dialog->findChild<QWidget*>(QStringLiteral("rememberPanel"));
  auto* standard  = dialog->findChild<QWidget*>(QStringLiteral("standardButtonsPanel"));
  auto* box       = dialog->findChild<QDialogButtonBox*>(QStringLiteral("standardButtons"));
  auto* details   = dialog->findChild<QWidget*>(QStringLiteral("detailsPanel"));
  auto* expander  = dialog->findChild<QWidget*>(QStringLiteral("detailsExpander"));
  auto* iconPanel = dialog->findChild<QWidget*>(QStringLiteral("iconPanel"));
  auto* iconLabel = dialog->findChild<QLabel*>(QStringLiteral("icon"));
  if (main == nullptr || top == nullptr || contents == nullptr || contentLb == nullptr ||
      commands == nullptr || remember == nullptr || standard == nullptr || box == nullptr ||
      details == nullptr || expander == nullptr || iconPanel == nullptr ||
      iconLabel == nullptr) {
    return;
  }

  QPixmap icon;
  if (!iconPanel->isHidden() && !iconLabel->pixmap().isNull() &&
      !isQuestionIcon(dialog, iconLabel->pixmap())) {
    const QPixmap original = iconLabel->pixmap();
    icon = original.scaled(QSize(28, 28) * original.devicePixelRatio(), Qt::KeepAspectRatio,
                           Qt::SmoothTransformation);
    icon.setDevicePixelRatio(original.devicePixelRatio());
  }
  const int width         = top->minimumWidth();
  const bool hasCommands  = !commands->parentWidget()->isHidden();
  const bool showRemember = !remember->isHidden();
  const bool hasDetails   = expander->isEnabled();

  const ContentShape shape = toContentShape(dialog, main->text(), icon, std::max(440, width));

  // the widgets the dialog still uses, without the frame around them
  if (auto* spacer = contents->findChild<QWidget*>(QStringLiteral("buttonsSpacer"))) {
    spacer->hide();
  }
  const bool hasText = !contentLb->text().isEmpty() || contents->layout()->count() > 2;
  contents->setParent(dialog);
  contents->layout()->setContentsMargins(0, 0, 0, 0);
  contentLb->setObjectName(QStringLiteral("fluentDialogText"));
  shape.body->addWidget(contents);
  contents->setVisible(hasText);

  if (hasCommands) {
    QPushButton* first = nullptr;
    bool hasCards      = false;
    auto* cards        = new QVBoxLayout;
    cards->setContentsMargins(0, 8, 0, 0);
    cards->setSpacing(0);
    for (QCommandLinkButton* original : commands->findChildren<QCommandLinkButton*>()) {
      if (!original->description().isEmpty()) {
        auto* card =
            new FluentOptionCard(0, original->text(), original->description(), dialog);
        pressesOriginal(card, original);
        cards->addWidget(card);
        hasCards = true;
      } else {
        auto* button = new QPushButton(original->text(), shape.footer->parentWidget());
        button->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        button->setMinimumWidth(110);
        button->setAutoDefault(false);
        enterPressesItself(button);
        pressesOriginal(button, original);
        shape.footer->addWidget(button);
        if (first == nullptr) {
          first = button;
        }
      }
    }
    shape.body->addLayout(cards);
    if (first != nullptr && !hasCards) {
      first->setDefault(true);
      first->setProperty("fluentPrimary", true);
    }
    if (first == nullptr) {
      shape.footer->parentWidget()->hide();
    }
  } else {
    box->setParent(shape.footer->parentWidget());
    shape.footer->addWidget(box);
    box->show();
    equalButtons(box);
  }
  top->setMinimumWidth(0);
  top->hide();
  standard->hide();

  if (showRemember) {
    remember->setParent(dialog);
    shape.body->addWidget(remember);
    remember->show();
  } else {
    remember->hide();
  }
  if (hasDetails) {
    details->setParent(dialog);
    if (auto* line = details->findChild<QWidget*>(QStringLiteral("line"))) {
      line->hide();
    }
    shape.body->addWidget(details);
    details->show();
  } else {
    details->hide();
  }
}

// uibase's question with "remember my choice"
void shapeQuestionMemory(QDialog* dialog)
{
  auto* frame   = dialog->findChild<QWidget*>(QStringLiteral("frame"));
  auto* message = dialog->findChild<QLabel*>(QStringLiteral("messageLabel"));
  auto* box     = dialog->findChild<QDialogButtonBox*>(QStringLiteral("buttonBox"));
  if (frame == nullptr || message == nullptr || box == nullptr) {
    return;
  }
  auto* remember    = dialog->findChild<QWidget*>(QStringLiteral("rememberCheckBox"));
  auto* rememberFor = dialog->findChild<QWidget*>(QStringLiteral("rememberForCheckBox"));

  const ContentShape shape = toContentShape(dialog, dialog->windowTitle());
  message->setParent(dialog);
  message->setObjectName(QStringLiteral("fluentDialogText"));
  message->setWordWrap(true);
  shape.body->addWidget(message);
  message->show();
  shape.body->addSpacing(8);
  for (QWidget* check : {remember, rememberFor}) {
    if (check != nullptr) {
      check->setParent(dialog);
      shape.body->addWidget(check);
      check->show();
    }
  }
  frame->hide();
  box->setParent(shape.footer->parentWidget());
  shape.footer->addWidget(box);
  box->show();
  equalButtons(box);
}

// QInputDialog: the caption and the field under a title
void shapeInputDialog(QInputDialog* dialog)
{
  auto* box = dialog->findChild<QDialogButtonBox*>();
  if (box == nullptr || qobject_cast<QVBoxLayout*>(dialog->layout()) == nullptr) {
    return;
  }
  QList<QWidget*> widgets;
  for (int i = 0; i < dialog->layout()->count(); ++i) {
    QWidget* widget = dialog->layout()->itemAt(i)->widget();
    if (widget != nullptr && widget != box) {
      widgets.append(widget);
    }
  }
  const ContentShape shape = toContentShape(dialog, dialog->windowTitle());
  for (QWidget* widget : widgets) {
    shape.body->addWidget(widget);
    widget->show();
  }
  box->setParent(shape.footer->parentWidget());
  shape.footer->addWidget(box);
  box->show();
  equalButtons(box);
}

// choices as option cards
void shapeSelectionDialog(QDialog* dialog)
{
  auto* box = dialog->findChild<QDialogButtonBox*>(QStringLiteral("buttonBox"));
  if (box == nullptr ||
      qobject_cast<QVBoxLayout*>(box->parentWidget()->layout()) == nullptr) {
    return;
  }
  const auto originals = box->findChildren<QCommandLinkButton*>();
  auto* layout = static_cast<QVBoxLayout*>(box->parentWidget()->layout());
  const int at = layout->indexOf(box);
  int row      = 0;
  for (QCommandLinkButton* original : originals) {
    auto* card = new FluentOptionCard(0, original->text(), original->description(),
                                      box->parentWidget());
    pressesOriginal(card, original);
    layout->insertWidget(at + row++, card);
  }
  layout->addStretch(1);
  box->hide();
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
          !label->property("fluentKeepIcon").toBool() &&
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

// plugin dialogs color status text with plain CSS names (the manual
// installer's "looks good" / "does not look valid"); the Fluent status
// colors stay readable on light and dark surfaces
class StatusInk : public QObject
{
public:
  explicit StatusInk(QLabel* label) : QObject(label), m_label(label)
  {
    label->installEventFilter(this);
    apply();
  }

protected:
  bool eventFilter(QObject* watched, QEvent* event) override
  {
    if (event->type() == QEvent::StyleChange && !m_applying) {
      apply();
    }
    return QObject::eventFilter(watched, event);
  }

private:
  QLabel* m_label;
  bool m_applying = false;
  // what apply() last wrote, so a theme change can give it the new tone
  QString m_written;
  bool m_bad = false;

  void apply()
  {
    static const QRegularExpression plain(
        QStringLiteral("color\\s*:\\s*(red|green|darkgreen|darkred)\\s*;?"),
        QRegularExpression::CaseInsensitiveOption);
    const QString sheet = m_label->styleSheet();
    const auto match    = plain.match(sheet);
    qsizetype start = -1, length = 0;
    if (match.hasMatch()) {
      m_bad  = match.captured(1).contains(QLatin1String("red"), Qt::CaseInsensitive);
      start  = match.capturedStart();
      length = match.capturedLength();
    } else if (!m_written.isEmpty() && sheet.contains(m_written)) {
      start  = sheet.indexOf(m_written);
      length = m_written.size();
    } else {
      return;
    }
    const QColor ink =
        FluentIcons::color(m_bad ? FluentIcons::Tone::Critical : FluentIcons::Tone::Success);
    m_written     = QStringLiteral("color: %1;").arg(ink.name());
    QString fixed = sheet;
    fixed.replace(start, length, m_written);
    if (fixed != sheet) {
      m_applying = true;
      m_label->setStyleSheet(fixed);
      m_applying = false;
    }
  }
};

// dialogs that are mostly options get Settings rows
bool isOptionsDialog(const QDialog* dialog)
{
  static const QStringList names = {
      QStringLiteral("LootDialog"),
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

  // the small dialogs Qt and uibase build themselves take the content dialog's shape
  if (dialog->objectName() == QLatin1String("TaskDialog")) {
    shapeTaskDialog(dialog);
  } else if (dialog->objectName() == QLatin1String("QuestionBoxMemory")) {
    shapeQuestionMemory(dialog);
  } else if (dialog->objectName() == QLatin1String("SelectionDialog")) {
    shapeSelectionDialog(dialog);
  } else if (auto* input = qobject_cast<QInputDialog*>(dialog)) {
    shapeInputDialog(input);
  } else if (auto* progress = qobject_cast<QProgressDialog*>(dialog)) {
    progress->setMinimumWidth(440);
    if (auto* label = progress->findChild<QLabel*>()) {
      label->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    }
  }

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

  for (QLabel* label : dialog->findChildren<QLabel*>()) {
    new StatusInk(label);
  }

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

  // once the layouts have settled, a short scroll becomes a taller window
  QTimer::singleShot(0, dialog, [dialog] {
    growForShortScroll(dialog);
  });
}

void growForShortScroll(QDialog* dialog)
{
  // a window that scrolls only a little reads better a little taller, as
  // long as it still fits the size dialogs open at; long pages keep their
  // scroll bar
  constexpr int ShortScroll = 240;

  if (dialog == nullptr || !dialog->isVisible() ||
      (dialog->windowState() & (Qt::WindowMaximized | Qt::WindowFullScreen))) {
    return;
  }

  // a dialog grows once; after that MO2 keeps the size it had when it
  // closed, so a window the user made smaller stays smaller
  const QString grownKey =
      QStringLiteral("FluentDialogs/grown/") + dialog->objectName();
  if (dialog->objectName().isEmpty() || GlobalSettings::value(grownKey, false).toBool()) {
    return;
  }
  // the screen fitDialogToAvailableScreen() used for the same dialog
  const QScreen* screen = QGuiApplication::screenAt(dialog->geometry().center());
  if (screen == nullptr) {
    screen = dialog->screen();
  }
  if (screen == nullptr) {
    return;
  }

  int extra = 0;
  for (QScrollArea* area : dialog->findChildren<QScrollArea*>()) {
    QWidget* content = area->widget();
    // only areas that get taller with the window: not nested in another
    // scroll area and not held at a fixed height
    QWidget* outer = area->parentWidget();
    while (outer != nullptr && outer != dialog && !qobject_cast<QScrollArea*>(outer)) {
      outer = outer->parentWidget();
    }
    if (content == nullptr || !area->isVisible() || !area->widgetResizable() ||
        area->verticalScrollBarPolicy() == Qt::ScrollBarAlwaysOff || outer != dialog ||
        area->maximumHeight() < QWIDGETSIZE_MAX) {
      continue;
    }
    const int width  = area->viewport()->width();
    const int needed = content->hasHeightForWidth() ? content->heightForWidth(width)
                                                    : content->sizeHint().height();
    const int overflow = needed - area->viewport()->height();
    if (overflow > 0 && overflow <= ShortScroll) {
      extra = std::max(extra, overflow);
    }
  }
  if (extra == 0) {
    return;
  }

  const QRect available = screen->availableGeometry();
  const int frame       = dialog->frameGeometry().height() - dialog->geometry().height();
  const int maxHeight =
      std::min(maxSize(available).height(), available.height() - std::max(frame, 0));
  const int height = dialog->height() + extra;
  if (height > maxHeight || height > dialog->maximumHeight()) {
    return;
  }

  // grow around the middle, but never above the work area's top
  QRect geometry = dialog->geometry();
  geometry.setTop(std::max(available.top() + std::max(frame, 0),
                           geometry.top() - extra / 2));
  geometry.setHeight(height);
  if (geometry.bottom() > available.bottom()) {
    geometry.moveBottom(available.bottom());
  }
  dialog->setGeometry(geometry);
  GlobalSettings::setValue(grownKey, true);
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

  // the shape of a content dialog; Qt keeps sizing the box itself when it shows
  auto* buttons     = box->findChild<QDialogButtonBox*>(QStringLiteral("qt_msgbox_buttonbox"));
  auto* text        = box->findChild<QLabel*>(QStringLiteral("qt_msgbox_label"));
  auto* informative = box->findChild<QLabel*>(QStringLiteral("qt_msgbox_informativelabel"));
  auto* iconLabel   = box->findChild<QLabel*>(QStringLiteral("qt_msgboxex_icon_label"));
  if (buttons != nullptr && text != nullptr) {
    QList<QWidget*> others;
    for (auto* child : box->findChildren<QWidget*>(QString(), Qt::FindDirectChildrenOnly)) {
      if (child != buttons && child != text && child != informative && child != iconLabel &&
          !child->isWindow()) {
        others.append(child);
      }
    }
    const QPixmap icon = iconLabel != nullptr ? iconLabel->pixmap() : QPixmap();
    if (iconLabel != nullptr) {
      iconLabel->hide();
    }
    // Qt wraps a long text only when the layout's minimum width is past its
    // limit, so only a short text gets the dialog's minimum width
    const ContentShape shape = toContentShape(box, box->windowTitle(), icon, 0);
    const bool wrap = text->wordWrap();
    text->setWordWrap(false);
    if (text->sizeHint().width() < 392) {
      text->setMinimumWidth(392);
    }
    text->setWordWrap(wrap);
    shape.body->addWidget(text);
    text->show();
    if (informative != nullptr) {
      shape.body->addWidget(informative);
      informative->show();
    }
    for (QWidget* other : others) {
      // the check box a plugin added, the details hidden until asked for
      const bool hidden = other->isHidden();
      shape.body->addWidget(other);
      other->setHidden(hidden);
    }
    buttons->setParent(shape.footer->parentWidget());
    shape.footer->addWidget(buttons);
    buttons->show();
    equalButtons(buttons);
    // the band is a widget of its own, whose minimum QMessageBox would not see yet
    shape.footer->parentWidget()->setMinimumWidth(
        48 + static_cast<int>(std::max<qsizetype>(1, buttons->buttons().size())) * 118);
  }

  matchTitleBar(box);
}

}  // namespace FluentDialogs

FluentContentDialog::FluentContentDialog(QWidget* parent, const QString& objectName,
                                         const QString& title, const QString& text)
    : QDialog(parent)
{
  setObjectName(objectName);
  setProperty("fluentContentDialog", true);
  setWindowTitle(title);
  setMinimumWidth(440);

  auto* layout = new QVBoxLayout(this);
  layout->setContentsMargins(0, 0, 0, 0);
  layout->setSpacing(0);

  auto* content = new QWidget(this);
  content->setObjectName(QStringLiteral("fluentDialogContent"));
  auto* contentLayout = new QVBoxLayout(content);
  contentLayout->setContentsMargins(24, 20, 24, 24);
  contentLayout->setSpacing(8);

  auto* titleLabel = new QLabel(title, content);
  titleLabel->setObjectName(QStringLiteral("fluentDialogTitle"));
  titleLabel->setWordWrap(true);
  contentLayout->addWidget(titleLabel);

  m_text = new QLabel(text, content);
  m_text->setObjectName(QStringLiteral("fluentDialogText"));
  m_text->setWordWrap(true);
  m_text->setVisible(!text.isEmpty());
  contentLayout->addWidget(m_text);

  m_body = new QVBoxLayout;
  m_body->setContentsMargins(0, 8, 0, 0);
  m_body->setSpacing(8);
  contentLayout->addLayout(m_body, 1);
  layout->addWidget(content, 1);

  auto* band = new QFrame(this);
  band->setObjectName(QStringLiteral("fluentDialogFooter"));
  m_buttons = new QHBoxLayout(band);
  m_buttons->setContentsMargins(24, 24, 24, 24);
  m_buttons->setSpacing(8);
  layout->addWidget(band);
}

QPushButton* FluentContentDialog::addButton(const QString& text, bool primary)
{
  auto* button = new QPushButton(text, this);
  button->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
  button->setMinimumWidth(110);
  button->setAutoDefault(false);
  enterPressesItself(button);
  if (primary && !m_hasDefault) {
    m_hasDefault = true;
    button->setDefault(true);
  }
  if (primary) {
    button->setProperty("fluentPrimary", true);
  }
  m_buttons->addWidget(button);
  return button;
}

void FluentContentDialog::askFirst(QPushButton* cancel)
{
  for (auto* button : findChildren<QPushButton*>()) {
    button->setDefault(false);
  }
  m_hasDefault = true;
  cancel->setFocus();
}
