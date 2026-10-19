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
const char* const ShownProperty    = "fluentIconShown";

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

// recolors an action's or a button's icon; when code or the style sheet has
// put a different icon there since, that one becomes the new original
template <class T>
void recolor(T* object, const QColor& ink)
{
  const QIcon current = object->icon();
  if (current.isNull()) {
    return;
  }

  const bool ours = current.cacheKey() == object->property(ShownProperty).toLongLong();
  if (ours && object->property(InkProperty).template value<QColor>() == ink) {
    return;
  }

  const QIcon original =
      ours ? object->property(OriginalProperty).template value<QIcon>() : current;
  const QIcon result = monochrome(original, ink);
  object->setProperty(OriginalProperty, QVariant::fromValue(original));
  object->setIcon(result);
  object->setProperty(ShownProperty, result.cacheKey());
  object->setProperty(InkProperty, ink);
}

void apply(QMenu* menu)
{
  if (menu == nullptr) {
    return;
  }

  const QColor ink = menu->palette().color(QPalette::Active, QPalette::Text);
  for (QAction* action : menu->actions()) {
    recolor(action, ink);
  }
}

void apply(QAbstractButton* button)
{
  if (button == nullptr || button->property("fluentKeepIcon").toBool() ||
      qobject_cast<QToolBar*>(button->parentWidget()) != nullptr) {
    return;
  }

  recolor(button, button->palette().color(QPalette::Active, QPalette::ButtonText));
}

}  // namespace FluentIcons
