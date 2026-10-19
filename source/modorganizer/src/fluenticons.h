#ifndef FLUENTICONS_H
#define FLUENTICONS_H

// Fluent v4 monochrome icons. MO2's menu and button icons are line drawings in
// several colors, drawn for light backgrounds. These helpers redraw them in
// one ink color: dark strokes become solid ink, pale fills fade out, so the
// drawings keep their detail and follow the theme like Windows 11 icons.

#include <QColor>
#include <QIcon>

class QAbstractButton;
class QMenu;

namespace FluentIcons
{

// the icon in the given ink (results are cached per icon and color)
QIcon monochrome(const QIcon& icon, const QColor& ink);

// recolors the menu's action icons in its text color; safe to call on every
// show, it remembers the originals
void apply(QMenu* menu);

// recolors a push or tool button's icon in its text color, once per theme;
// buttons on toolbars and those with the "fluentKeepIcon" property are left
// alone
void apply(QAbstractButton* button);

}  // namespace FluentIcons

#endif  // FLUENTICONS_H
