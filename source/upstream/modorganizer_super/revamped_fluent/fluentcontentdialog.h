/*
MO2 Revamped: the Windows 11 content dialog for the installer plugins.

The same structure, object names and behaviour as FluentContentDialog in the
organizer (modorganizer/src/fluentdialogs.cpp), which the plugins can't link
to: a title, a short text, the dialog's own content, and a band of buttons of
the same width. The themes style it by those object names. Header-only, so
uibase's exports stay as they are.
*/

#ifndef REVAMPED_FLUENTCONTENTDIALOG_H
#define REVAMPED_FLUENTCONTENTDIALOG_H

#include <algorithm>
#include <cmath>

#include <QAccessible>
#include <QColor>
#include <QCoreApplication>
#include <QDialog>
#include <QEvent>
#include <QFrame>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QPushButton>
#include <QTextBlock>
#include <QTextCursor>
#include <QTextDocument>
#include <QTextEdit>
#include <QTextFrame>
#include <QVBoxLayout>

namespace RevampedFluent
{

// a widget's description for screen readers, announced when it changes (a
// status line, a count)
inline void setDescription(QWidget* widget, const QString& text)
{
  if (widget->accessibleDescription() == text) {
    return;
  }
  widget->setAccessibleDescription(text);
  QAccessibleEvent event(widget, QAccessible::DescriptionChanged);
  QAccessible::updateAccessibility(&event);
}

inline double luminanceChannel(double v)
{
  return v <= 0.03928 ? v / 12.92 : std::pow((v + 0.055) / 1.055, 2.4);
}

inline double relativeLuminance(const QColor& c)
{
  return 0.2126 * luminanceChannel(c.redF()) + 0.7152 * luminanceChannel(c.greenF()) +
         0.0722 * luminanceChannel(c.blueF());
}

// WCAG's contrast ratio between two colours (1 to 21)
inline double contrastRatio(const QColor& a, const QColor& b)
{
  const double la = relativeLuminance(a), lb = relativeLuminance(b);
  return (std::max(la, lb) + 0.05) / (std::min(la, lb) + 0.05);
}

// A mod's own rich text (a FOMOD option's description, an OMOD readme) keeps
// its bold, lists, tables and links, but not colours written for a white
// page: backgrounds go, and a text colour the theme's background makes hard
// to read gives way to the theme's text colour. Black text on Dark or OLED
// Black was unreadable.
inline void adaptRichText(QTextEdit* edit)
{
  QTextDocument* document = edit->document();
  // the themes draw the page with style sheets, which the palette doesn't
  // show; the theme's text colour says whether the page is dark or light
  const bool darkPage = edit->palette().color(QPalette::Text).lightness() > 127;
  const QColor page   = darkPage ? QColor(32, 32, 32) : QColor(251, 251, 251);

  QTextCursor cursor(document);
  cursor.beginEditBlock();
  for (QTextBlock block = document->begin(); block.isValid(); block = block.next()) {
    QTextBlockFormat blockFormat = block.blockFormat();
    if (blockFormat.hasProperty(QTextFormat::BackgroundBrush)) {
      blockFormat.clearBackground();
      QTextCursor(block).setBlockFormat(blockFormat);
    }
    for (QTextBlock::iterator it = block.begin(); !it.atEnd(); ++it) {
      const QTextFragment fragment = it.fragment();
      if (!fragment.isValid()) {
        continue;
      }
      QTextCharFormat format = fragment.charFormat();
      bool changed = false;
      if (format.hasProperty(QTextFormat::BackgroundBrush)) {
        format.clearBackground();
        changed = true;
      }
      if (format.hasProperty(QTextFormat::ForegroundBrush) &&
          contrastRatio(format.foreground().color(), page) < 4.5) {
        format.clearForeground();
        changed = true;
      }
      if (changed) {
        QTextCursor range(document);
        range.setPosition(fragment.position());
        range.setPosition(fragment.position() + fragment.length(), QTextCursor::KeepAnchor);
        range.setCharFormat(format);
      }
    }
  }
  // tables and the page itself can carry a background too
  for (QTextFrame* frame : document->rootFrame()->childFrames()) {
    QTextFrameFormat frameFormat = frame->frameFormat();
    if (frameFormat.hasProperty(QTextFormat::BackgroundBrush)) {
      frameFormat.clearBackground();
      frame->setFrameFormat(frameFormat);
    }
  }
  QTextFrameFormat rootFormat = document->rootFrame()->frameFormat();
  if (rootFormat.hasProperty(QTextFormat::BackgroundBrush)) {
    rootFormat.clearBackground();
    document->rootFrame()->setFrameFormat(rootFormat);
  }
  cursor.endEditBlock();
  document->setModified(false);
}

// Enter on a focused button presses that button, as in Windows, not the
// dialog's default one
class EnterPressesItself : public QObject
{
public:
  using QObject::QObject;

  bool eventFilter(QObject* watched, QEvent* event) override
  {
    if (event->type() == QEvent::KeyPress) {
      const int key = static_cast<QKeyEvent*>(event)->key();
      auto* button  = qobject_cast<QPushButton*>(watched);
      if ((key == Qt::Key_Return || key == Qt::Key_Enter) && button != nullptr &&
          button->isEnabled()) {
        button->click();
        return true;
      }
    }
    return false;
  }
};

inline QPushButton* enterPressesItself(QPushButton* button)
{
  button->installEventFilter(new EnterPressesItself(button));
  return button;
}

// a button inside a dialog's content (Browse…): never the default one
inline QPushButton* contentButton(const QString& text, QWidget* parent)
{
  auto* button = new QPushButton(text, parent);
  button->setAutoDefault(false);
  return enterPressesItself(button);
}

// the parts of a content dialog: its title, its text, where its own content
// goes, and the band of buttons
struct ContentParts
{
  QLabel* title        = nullptr;
  QLabel* text         = nullptr;
  QVBoxLayout* body    = nullptr;
  QHBoxLayout* buttons = nullptr;
};

// the content dialog's structure, built in any dialog: ContentDialog's own,
// and uibase's dialogs, which derive from QDialog themselves
inline ContentParts buildContent(QDialog* dialog, const QString& title, const QString& text)
{
  ContentParts parts;
  dialog->setProperty("fluentContentDialog", true);
  dialog->setWindowTitle(title);
  dialog->setWindowFlags(dialog->windowFlags() & ~Qt::WindowContextHelpButtonHint);
  dialog->setMinimumWidth(440);

  auto* layout = new QVBoxLayout(dialog);
  layout->setContentsMargins(0, 0, 0, 0);
  layout->setSpacing(0);

  auto* content = new QWidget(dialog);
  content->setObjectName(QStringLiteral("fluentDialogContent"));
  auto* contentLayout = new QVBoxLayout(content);
  contentLayout->setContentsMargins(24, 20, 24, 24);
  contentLayout->setSpacing(8);

  parts.title = new QLabel(title, content);
  parts.title->setObjectName(QStringLiteral("fluentDialogTitle"));
  parts.title->setWordWrap(true);
  // as written: titles carry mod names and scripts' own words
  parts.title->setTextFormat(Qt::PlainText);
  contentLayout->addWidget(parts.title);

  parts.text = new QLabel(text, content);
  parts.text->setObjectName(QStringLiteral("fluentDialogText"));
  parts.text->setWordWrap(true);
  parts.text->setTextFormat(Qt::PlainText);
  parts.text->setVisible(!text.isEmpty());
  contentLayout->addWidget(parts.text);
  // a screen reader reads the explanation with the window's name
  dialog->setAccessibleDescription(text);

  parts.body = new QVBoxLayout;
  parts.body->setContentsMargins(0, 8, 0, 0);
  parts.body->setSpacing(8);
  contentLayout->addLayout(parts.body, 1);
  layout->addWidget(content, 1);

  auto* band = new QFrame(dialog);
  band->setObjectName(QStringLiteral("fluentDialogFooter"));
  parts.buttons = new QHBoxLayout(band);
  parts.buttons->setContentsMargins(24, 24, 24, 24);
  parts.buttons->setSpacing(8);
  layout->addWidget(band);
  return parts;
}

// a button of the band, all of the same width; a primary button gets the
// accent, and the first primary one is the default
inline QPushButton* bandButton(QDialog* dialog, QHBoxLayout* band, const QString& text,
                               bool primary, bool& hasDefault)
{
  auto* button = new QPushButton(text, dialog);
  button->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
  button->setMinimumWidth(110);
  button->setAutoDefault(false);
  enterPressesItself(button);
  if (primary && !hasDefault) {
    hasDefault = true;
    button->setDefault(true);
  }
  if (primary) {
    button->setProperty("fluentPrimary", true);
  }
  band->addWidget(button);
  return button;
}

class ContentDialog : public QDialog
{
public:
  ContentDialog(QWidget* parent, const QString& objectName, const QString& title,
                const QString& text = {})
      : QDialog(parent)
  {
    setObjectName(objectName);
    const ContentParts parts = buildContent(this, title, text);
    m_title   = parts.title;
    m_text    = parts.text;
    m_body    = parts.body;
    m_buttons = parts.buttons;
  }

  // where the dialog's own content goes, below the text
  QVBoxLayout* body() const { return m_body; }

  // the heading, when the window's title and the heading differ
  void setHeading(const QString& heading) { m_title->setText(heading); }

  void setText(const QString& text)
  {
    m_text->setText(text);
    m_text->setVisible(!text.isEmpty());
    setAccessibleDescription(text);
  }

  // adds a button to the band; primary buttons get the accent, the first one
  // added is the default
  QPushButton* addButton(const QString& text, bool primary = false)
  {
    return bandButton(this, m_buttons, text, primary, m_hasDefault);
  }

  // for a question: Enter does nothing, the accent stays on the action, and
  // the focus starts on the given button
  void askFirst(QPushButton* cancel)
  {
    for (auto* button : findChildren<QPushButton*>()) {
      button->setDefault(false);
    }
    m_hasDefault = true;
    cancel->setFocus();
  }

  // at least the minimum width: Qt measures wrapped text at this width
  QSize sizeHint() const override
  {
    QSize size = QDialog::sizeHint();
    size.setWidth(qMax(size.width(), minimumWidth()));
    return size;
  }

private:
  QLabel* m_title;
  QLabel* m_text;
  QVBoxLayout* m_body;
  QHBoxLayout* m_buttons;
  bool m_hasDefault = false;
};

// a field's label above it, and the name a screen reader gives the field
inline QLabel* fieldLabel(const QString& text, QWidget* field, QWidget* parent)
{
  auto* label = new QLabel(text, parent);
  label->setBuddy(field);
  field->setAccessibleName(text);
  return label;
}

// what is wrong with a field, in the themes' error tone (instanceNameValidation)
inline QLabel* problemLabel(const QString& text, QWidget* parent)
{
  auto* label = new QLabel(text, parent);
  label->setObjectName(QStringLiteral("instanceNameValidation"));
  label->setWordWrap(true);
  label->setVisible(false);
  return label;
}

// a short notice with one button, in place of QMessageBox::warning
inline void showNotice(QWidget* parent, const QString& title, const QString& text)
{
  ContentDialog dialog(parent, QStringLiteral("revampedInstallerNotice"), title, text);
  auto* ok = dialog.addButton(QCoreApplication::translate("RevampedFluent", "OK"), true);
  // a string connect: C++/CLI code finds no signal for a function pointer
  QObject::connect(ok, SIGNAL(clicked()), &dialog, SLOT(accept()));
  ok->setFocus();
  dialog.exec();
}

}  // namespace RevampedFluent

#endif  // REVAMPED_FLUENTCONTENTDIALOG_H
