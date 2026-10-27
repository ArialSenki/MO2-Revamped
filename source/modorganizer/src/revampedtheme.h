#pragma once

#include <QColor>
#include <QCoreApplication>
#include <QVariant>

namespace MOBase::RevampedTheme {

// True for any dark stylesheet (Dark, Fluent Dark or a third-party dark
// theme), so model-supplied text colors stay readable on dark rows.
inline bool isDark()
{
  const auto* app = QCoreApplication::instance();
  return app && app->property("revampedDark").toBool();
}

// Status text colors for item views: the Windows 11 foregrounds for dark
// surfaces, or the classic dark tones on light ones.
inline QColor criticalText()
{
  return isDark() ? QColor("#FF99A4") : QColor(Qt::darkRed);
}

inline QColor successText()
{
  return isDark() ? QColor("#6CCB5F") : QColor(Qt::darkGreen);
}

inline QColor cautionText()
{
  return isDark() ? QColor("#FCE100") : QColor(Qt::darkYellow);
}

inline QColor dimmedText()
{
  return isDark() ? QColor("#A6A6A6") : QColor(Qt::darkGray);
}

inline QColor infoText()
{
  return isDark() ? QColor("#60CDFF") : QColor(Qt::darkBlue);
}

// Runtime-painted accent surfaces keep their color in every theme.
inline QColor accent(QColor color)
{
  return color;
}

}  // namespace MOBase::RevampedTheme
