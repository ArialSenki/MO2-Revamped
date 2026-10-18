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

// True for any dark stylesheet (Dark, Dark Black, Fluent Dark or a third-party
// dark theme), so model-supplied text colors stay readable on dark rows.
inline bool isDark()
{
  const auto* app = QCoreApplication::instance();
  return app && app->property("revampedDark").toBool();
}

// Status text colors for item views. Dark Black keeps its approved muted
// tones; other dark themes use the Windows 11 foregrounds for dark surfaces.
inline QColor criticalText()
{
  if (isBlack()) {
    return QColor("#F0A0A0");
  }
  return isDark() ? QColor("#FF99A4") : QColor(Qt::darkRed);
}

inline QColor successText()
{
  if (isBlack()) {
    return QColor("#9BCB9B");
  }
  return isDark() ? QColor("#6CCB5F") : QColor(Qt::darkGreen);
}

inline QColor cautionText()
{
  if (isBlack()) {
    return QColor("#D9C48A");
  }
  return isDark() ? QColor("#FCE100") : QColor(Qt::darkYellow);
}

inline QColor dimmedText()
{
  return isDark() ? QColor("#A6A6A6") : QColor(Qt::darkGray);
}

inline QColor infoText()
{
  if (isBlack()) {
    return QColor("#D0D0D0");
  }
  return isDark() ? QColor("#60CDFF") : QColor(Qt::darkBlue);
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
