#pragma once
#include "revampedtheme.h"

#include <QIcon>
#include <QPainter>
#include <QPainterPath>
#include <QPen>
#include <QPixmap>
#include <QRect>
#include <QRectF>
#include <QSize>
#include <QWidget>
#include <QtMath>

namespace MOBase
{

// Present game artwork as a circular badge in the executable selector.
inline QIcon makeRunStyleGameIcon(const QIcon& sourceIcon,
                                  const QWidget* themeWidget,
                                  QSize logicalSize = QSize(32, 32))
{
  Q_UNUSED(themeWidget);

  if (sourceIcon.isNull()) {
    return sourceIcon;
  }

  constexpr qreal devicePixelRatio = 3.0;
  const int logicalPixels =
      qMax(1, qMax(logicalSize.width(), logicalSize.height()));
  const int iconPixels = qRound(logicalPixels * devicePixelRatio);

  QPixmap sourcePixmap = sourceIcon.pixmap(QSize(iconPixels, iconPixels));
  if (sourcePixmap.isNull()) {
    return sourceIcon;
  }

  sourcePixmap = sourcePixmap.scaled(
      QSize(iconPixels, iconPixels), Qt::KeepAspectRatioByExpanding,
      Qt::SmoothTransformation);
  const QRect sourceRect((sourcePixmap.width() - iconPixels) / 2,
                         (sourcePixmap.height() - iconPixels) / 2, iconPixels,
                         iconPixels);

  QPixmap badgePixmap(iconPixels, iconPixels);
  badgePixmap.fill(Qt::transparent);

  QPainter painter(&badgePixmap);
  painter.setRenderHint(QPainter::Antialiasing);
  painter.setRenderHint(QPainter::SmoothPixmapTransform);

  const QRectF badgeRect(1.0, 1.0, iconPixels - 2.0, iconPixels - 2.0);
  painter.setPen(Qt::NoPen);
  painter.setBrush(RevampedTheme::accent(QColor("#293F50")));
  painter.drawEllipse(badgeRect);

  const qreal artworkInset = iconPixels / 8.0;
  const QRectF artworkRect(artworkInset, artworkInset,
                           iconPixels - artworkInset * 2.0,
                           iconPixels - artworkInset * 2.0);
  QPainterPath artworkClip;
  artworkClip.addEllipse(artworkRect);
  painter.setClipPath(artworkClip);
  painter.drawPixmap(artworkRect, sourcePixmap, QRectF(sourceRect));
  painter.setClipping(false);

  painter.setPen(QPen(RevampedTheme::accent(QColor("#7FA7C1")), iconPixels / 40.0));
  painter.setBrush(Qt::NoBrush);
  painter.drawEllipse(badgeRect);
  painter.end();

  badgePixmap.setDevicePixelRatio(devicePixelRatio);
  return QIcon(badgePixmap);
}

}  // namespace MOBase
