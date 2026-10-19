#ifndef FLUENTICONS_H
#define FLUENTICONS_H

// Fluent v4 icons. Every icon MO2 ships in its resources has a Segoe Fluent
// Icons glyph assigned here; with a Fluent theme those glyphs replace the
// colored drawings, painted in the theme's ink (or a status color) at whatever
// size and scale they are drawn, like the icons of Windows 11. Other themes
// keep the original resources.

#include <QColor>
#include <QIcon>

class QAbstractButton;
class QMenu;

namespace FluentIcons
{

// what a glyph is painted with; the colors follow the active theme
enum class Tone
{
  Ink,       // primary text
  Muted,     // secondary text
  Accent,    // theme accent
  Success,   // green
  Caution,   // amber
  Critical,  // red
};

// true while a Fluent theme is active
bool active();

// the color of a tone in the active theme
QColor color(Tone tone);

// the Segoe Fluent Icons font, Segoe MDL2 Assets on Windows 10
QFont font(int pixelSize);

// a glyph icon; it picks its color when it is painted, so it follows theme
// changes on its own
QIcon glyph(char16_t codepoint, Tone tone = Tone::Ink);

// the glyph assigned to an MO2 resource (":/MO/gui/...") when a Fluent theme
// is active, the resource itself otherwise
QIcon icon(const QString& resource);

// the glyph matching an icon that was created from an MO2 resource (in a .ui
// file, a plugin or a style sheet), or the icon unchanged
QIcon fluent(const QIcon& icon);

// whether the icon is one of the glyph icons above
bool isGlyph(const QIcon& icon);

// the icon in the given ink (results are cached per icon and color); used for
// icons that have no glyph, such as plugin icons
QIcon monochrome(const QIcon& icon, const QColor& ink);

// gives the menu's action icons the Fluent look; safe to call on every show
void apply(QMenu* menu);

// gives a push or tool button's icon the Fluent look; buttons on toolbars and
// those with the "fluentKeepIcon" property are left alone
void apply(QAbstractButton* button);

}  // namespace FluentIcons

#endif  // FLUENTICONS_H
