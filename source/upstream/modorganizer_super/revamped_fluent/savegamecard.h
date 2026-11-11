/*
MO2 Revamped: the card the Saves tab shows over a save, for the game plugins'
save details widgets (Gamebryo games and Morrowind): the save's screenshot,
the character, a few lines about the save, and the plugins it needs that aren't
active. The card paints its own rounded background. Header-only,
so the shared libraries' exports stay as they are.
*/

#ifndef REVAMPED_SAVEGAMECARD_H
#define REVAMPED_SAVEGAMECARD_H

#include <QCoreApplication>
#include <QCursor>
#include <QGuiApplication>
#include <QScreen>
#include <QFrame>
#include <QImage>
#include <QLabel>
#include <QPainter>
#include <QPainterPath>
#include <QPixmap>
#include <QStyle>
#include <QVBoxLayout>
#include <QWidget>

namespace RevampedFluent
{

// the card's rounded background, drawn here so any theme shows it
class SaveGameCardFrame : public QFrame
{
public:
  using QFrame::QFrame;

protected:
  void paintEvent(QPaintEvent*) override
  {
    const bool dark = palette().color(QPalette::Window).lightness() < 128;
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setPen(QColor(dark ? 0x3A3A3A : 0xE0E0E0));
    painter.setBrush(QColor(dark ? 0x2C2C2C : 0xF9F9F9));
    painter.drawRoundedRect(QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5), 8, 8);
  }
};

class SaveGameCard
{
public:
  // the screenshot's width; the card is as wide plus its margins
  static constexpr int PictureWidth = 320;
  // the plugins shown per kind; the rest is counted
  static constexpr int MaxPlugins = 7;

  void setup(QWidget* host)
  {
    m_host = host;
    host->setWindowFlags(Qt::ToolTip | Qt::BypassGraphicsProxyWidget);
    // the card draws its own rounded background
    host->setAttribute(Qt::WA_TranslucentBackground);
    auto* outer = new QVBoxLayout(host);
    outer->setContentsMargins(0, 0, 0, 0);
    outer->setSizeConstraint(QLayout::SetFixedSize);

    m_card = new SaveGameCardFrame(host);
    m_card->setObjectName(QStringLiteral("revampedSaveCard"));
    outer->addWidget(m_card);
    auto* layout = new QVBoxLayout(m_card);
    layout->setContentsMargins(12, 12, 12, 12);
    layout->setSpacing(4);

    m_picture = new QLabel(m_card);
    m_picture->setAlignment(Qt::AlignCenter);
    layout->addWidget(m_picture);
    layout->addSpacing(6);

    m_title = new QLabel(m_card);
    m_title->setObjectName(QStringLiteral("instanceDetailName"));
    m_title->setTextFormat(Qt::PlainText);
    m_title->setWordWrap(true);
    layout->addWidget(m_title);

    // the save's details in the normal text, not the small grey one
    m_lines = new QLabel(m_card);
    m_lines->setTextFormat(Qt::PlainText);
    m_lines->setWordWrap(true);
    layout->addWidget(m_lines);

    m_sections = new QVBoxLayout;
    m_sections->setContentsMargins(0, 6, 0, 0);
    m_sections->setSpacing(2);
    layout->addLayout(m_sections);

    m_card->setFixedWidth(PictureWidth + 24);
  }

  void setScreenshot(const QImage& image)
  {
    if (image.isNull()) {
      m_picture->clear();
      m_picture->setVisible(false);
      return;
    }
    // the scale of the screen the card opens on: the window doesn't exist
    // yet the first time
    QScreen* screen   = QGuiApplication::screenAt(QCursor::pos());
    const qreal ratio = screen != nullptr ? screen->devicePixelRatio() : m_host->devicePixelRatioF();
    QSize size = image.size();
    // smaller pictures (Morrowind's) keep their size
    if (size.width() > PictureWidth || size.height() > PictureWidth * 3 / 4) {
      size = size.scaled(QSize(PictureWidth, PictureWidth * 3 / 4), Qt::KeepAspectRatio);
    }
    // smooth, at the screen's scale, with rounded corners
    QPixmap pixmap(size * ratio);
    pixmap.setDevicePixelRatio(ratio);
    pixmap.fill(Qt::transparent);
    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setRenderHint(QPainter::SmoothPixmapTransform);
    QPainterPath clip;
    clip.addRoundedRect(QRectF(QPointF(0, 0), QSizeF(size)), 6, 6);
    painter.setClipPath(clip);
    painter.drawImage(QRectF(QPointF(0, 0), QSizeF(size)), image);
    painter.end();
    m_picture->setPixmap(pixmap);
    m_picture->setVisible(true);
  }

  void setTitle(const QString& title) { m_title->setText(title); }

  void setLines(const QStringList& lines) { m_lines->setText(lines.join(QLatin1Char('\n'))); }

  void clearSections()
  {
    while (QLayoutItem* item = m_sections->takeAt(0)) {
      delete item->widget();
      delete item;
    }
    m_anyMissing = false;
  }

  // a line of its own under the details ("Has script extender data")
  void addNote(const QString& text)
  {
    auto* label = new QLabel(text, m_card);
    label->setObjectName(QStringLiteral("fluentSettingDescription"));
    label->setWordWrap(true);
    m_sections->addWidget(label);
  }

  // the plugins of one kind the save needs that aren't active
  void addMissing(const QString& heading, const QStringList& missing)
  {
    if (missing.isEmpty()) {
      return;
    }
    m_anyMissing = true;
    auto* title = new QLabel(heading, m_card);
    title->setObjectName(QStringLiteral("instanceNameValidation"));
    title->setWordWrap(true);
    m_sections->addSpacing(4);
    m_sections->addWidget(title);
    for (int i = 0; i < missing.size() && i < MaxPlugins; ++i) {
      // the card has a fixed width: long names wrap, also after _ - and .
      auto* label = new QLabel(breakable(missing[i]), m_card);
      label->setTextFormat(Qt::PlainText);
      label->setWordWrap(true);
      label->setIndent(10);
      label->setAccessibleName(missing[i]);
      m_sections->addWidget(label);
    }
    if (missing.size() > MaxPlugins) {
      auto* more = new QLabel(
          QCoreApplication::translate("RevampedSaveCard", "and %1 more")
              .arg(missing.size() - MaxPlugins),
          m_card);
      more->setObjectName(QStringLiteral("fluentSettingDescription"));
      more->setIndent(10);
      m_sections->addWidget(more);
    }
  }

  // after the sections: a line when nothing is missing
  void finish()
  {
    if (!m_anyMissing) {
      auto* label = new QLabel(
          QCoreApplication::translate("RevampedSaveCard",
                                      "Every plugin this save uses is active."),
          m_card);
      label->setObjectName(QStringLiteral("fluentSettingDescription"));
      m_sections->addSpacing(4);
      m_sections->addWidget(label);
    }
    m_host->adjustSize();
  }

private:
  // a zero-width space after the characters plugin names join words with
  static QString breakable(const QString& name)
  {
    QString text;
    text.reserve(name.size() + 8);
    for (const QChar c : name) {
      text.append(c);
      if (c == QLatin1Char('_') || c == QLatin1Char('-') || c == QLatin1Char('.')) {
        text.append(QChar(0x200B));
      }
    }
    return text;
  }

  QWidget* m_host      = nullptr;
  QFrame* m_card       = nullptr;
  QLabel* m_picture    = nullptr;
  QLabel* m_title      = nullptr;
  QLabel* m_lines      = nullptr;
  QVBoxLayout* m_sections = nullptr;
  bool m_anyMissing    = false;
};

}  // namespace RevampedFluent

#endif  // REVAMPED_SAVEGAMECARD_H
