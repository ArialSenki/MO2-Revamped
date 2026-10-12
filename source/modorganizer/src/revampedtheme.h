#pragma once

#include <QColor>
#include <QCoreApplication>
#include <QVariant>

namespace MOBase::RevampedTheme {

inline bool isBlack()
{
  const auto* app = QCoreApplication::instance();
  return app && app->property("revampedDarkBlack").toBool();
}

// Runtime-painted accent surfaces follow the same neutral palette as the QSS.
inline QColor accent(QColor color)
{
  if (isBlack()) {
    const int gray = qRound(0.2126 * color.red() + 0.7152 * color.green() +
                           0.0722 * color.blue());
    color = QColor(gray, gray, gray, color.alpha());
  }
  return color;
}

}  // namespace MOBase::RevampedTheme
