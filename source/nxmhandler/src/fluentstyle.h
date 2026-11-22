#ifndef NXMHANDLER_FLUENTSTYLE_H
#define NXMHANDLER_FLUENTSTYLE_H

// MO2 Revamped: a small, self-contained Windows 11 "Fluent" look for the nxm
// handler. nxmhandler.exe is a separate program and cannot use MO2's theme
// files, so the colours below mirror the tokens of MO2 Revamped's Fluent v4
// theme (source/revamped/themes/Build-FluentTheme.ps1). Light or dark follows
// Windows' "app mode" (AppsUseLightTheme) and the accent follows Windows'
// accent colour; nothing is written to the registry.

#include <QColor>
#include <QIcon>
#include <QRectF>
#include <QString>

class QApplication;
class QPainter;
class QWidget;

namespace Fluent
{

struct Theme
{
  bool dark = false;

  QColor bg, card, cardLine, line, hover, pressed, sel;
  QColor field, fieldLine, fieldBottom;
  QColor btn, btnHover, btnPressed, btnLine, btnBottom, disabledBg;
  QColor text, muted, faint, caption;
  QColor accent, accentHover, accentPressed, accentFg, accentFgPressed, accentText,
      accentSubtle;
  QColor warnBg, warnFg, critBg, critFg, okBg, okFg;
  QColor chip, chipText;
};

// the theme in use; valid after install()
const Theme& theme();

// detects light/dark and the accent, sets the style, palette and style sheet
// on the application and keeps every window's title bar in the same mode; the
// look is applied again when Windows switches between light and dark
// (preview builds can pass forceDark 0 or 1 to skip the detection)
void install(QApplication& app, int forceDark = -1);

// a monochrome icon drawn from the Segoe Fluent Icons font (Segoe MDL2 Assets
// on Windows 10), e.g. 0xE710 for "Add"; without either font (Wine, Proton),
// from the glyph's SVG as MO2 bundles it
QIcon glyphIcon(ushort codepoint, const QColor& color, int size = 16);

// one glyph centered in the rectangle, pixelSize tall: as text in the icon
// font, or its SVG without one. The painter's pen and font are kept
void drawGlyph(QPainter* painter, const QRectF& rect, ushort codepoint, const QColor& color,
               int pixelSize);

// the icon font family available on this system
QString iconFontFamily();

// whether either icon font is installed (not on Wine or Proton);
// MO2R_GLYPH_SVG=1 says no, to check the SVGs on Windows
bool hasIconFont();

// marks a frame as a Fluent card (rounded, card background)
void makeCard(QWidget* widget);

}  // namespace Fluent

#endif  // NXMHANDLER_FLUENTSTYLE_H
