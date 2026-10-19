#include "fluenticons.h"

#include <QAbstractButton>
#include <QAction>
#include <QCache>
#include <QImage>
#include <QMenu>
#include <QPixmap>
#include <QToolBar>

namespace
{
constexpr int Sizes[] = {16, 20, 24, 32};

// the original icon of an action or button, kept so a theme change redraws
// from the source rather than from an already recolored copy
const char* const OriginalProperty = "fluentOriginalIcon";
const char* const InkProperty      = "fluentIconInk";

QPixmap recolor(const QPixmap& source, const QColor& ink)
{
  QImage image = source.toImage().convertToFormat(QImage::Format_ARGB32);
  for (int y = 0; y < image.height(); ++y) {
    auto* line = reinterpret_cast<QRgb*>(image.scanLine(y));
    for (int x = 0; x < image.width(); ++x) {
      const QRgb px = line[x];
      const int alpha = qAlpha(px);
      if (alpha == 0) {
        continue;
      }
      // darker source pixels carry more ink; the pale tints the drawings use
      // as fills (almost white) nearly vanish
      const double luminance =
          (0.2126 * qRed(px) + 0.7152 * qGreen(px) + 0.0722 * qBlue(px)) / 255.0;
      const double strength = std::clamp((1.0 - luminance) / 0.55, 0.0, 1.0);
      line[x] = qRgba(ink.red(), ink.green(), ink.blue(),
                      static_cast<int>(alpha * strength));
    }
  }
  QPixmap result = QPixmap::fromImage(image);
  result.setDevicePixelRatio(source.devicePixelRatio());
  return result;
}

QString cacheKey(const QIcon& icon, const QColor& ink)
{
  return QString::number(icon.cacheKey()) + QLatin1Char('/') + ink.name(QColor::HexArgb);
}
}  // namespace

namespace FluentIcons
{

QIcon monochrome(const QIcon& icon, const QColor& ink)
{
  if (icon.isNull()) {
    return icon;
  }

  static QCache<QString, QIcon> cache(400);
  const QString key = cacheKey(icon, ink);
  if (const QIcon* cached = cache.object(key)) {
    return *cached;
  }

  QIcon result;
  for (int size : Sizes) {
    for (auto mode : {QIcon::Normal, QIcon::Disabled}) {
      const QPixmap pixmap = icon.pixmap(QSize(size, size), QIcon::Normal);
      if (pixmap.isNull()) {
        continue;
      }
      QColor color = ink;
      if (mode == QIcon::Disabled) {
        color.setAlphaF(color.alphaF() * 0.4);
      }
      result.addPixmap(recolor(pixmap, color), mode);
    }
  }
  cache.insert(key, new QIcon(result));
  return result;
}

void apply(QMenu* menu)
{
  if (menu == nullptr) {
    return;
  }

  const QColor ink = menu->palette().color(QPalette::Active, QPalette::Text);
  for (QAction* action : menu->actions()) {
    QVariant original = action->property(OriginalProperty);
    if (!original.isValid()) {
      if (action->icon().isNull()) {
        continue;
      }
      original = QVariant::fromValue(action->icon());
      action->setProperty(OriginalProperty, original);
    } else if (action->property(InkProperty).value<QColor>() == ink) {
      continue;
    }
    action->setIcon(monochrome(original.value<QIcon>(), ink));
    action->setProperty(InkProperty, ink);
  }
}

void apply(QAbstractButton* button)
{
  if (button == nullptr || button->property("fluentKeepIcon").toBool() ||
      qobject_cast<QToolBar*>(button->parentWidget()) != nullptr) {
    return;
  }

  const QColor ink = button->palette().color(QPalette::Active, QPalette::ButtonText);
  QVariant original = button->property(OriginalProperty);
  if (!original.isValid()) {
    if (button->icon().isNull()) {
      return;
    }
    original = QVariant::fromValue(button->icon());
    button->setProperty(OriginalProperty, original);
  } else if (button->property(InkProperty).value<QColor>() == ink) {
    return;
  }
  button->setIcon(monochrome(original.value<QIcon>(), ink));
  button->setProperty(InkProperty, ink);
}

}  // namespace FluentIcons
