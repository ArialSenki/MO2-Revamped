#ifndef FLUENTNAVIGATION_H
#define FLUENTNAVIGATION_H

// Windows 11 Settings style navigation for a QTabWidget: a list on the left
// with a glyph and the tab's label per page, a pill marking the current one,
// and the pages on the right. The tab widget keeps its pages, indices and
// signals (only its tab bar is hidden), so dialog code keeps working.

#include <QList>

#include <functional>

class QListWidget;
class QTabWidget;
class QWidget;

namespace FluentNavigation
{

// a Segoe Fluent Icons code point for the page at the given index
using GlyphFor = std::function<char16_t(int index, QWidget* page)>;

// returns the navigation list, or null when the tab widget has no layout to
// sit in
QListWidget* attach(QTabWidget* tabs, GlyphFor glyphFor);

// glyphs in tab order; missing ones fall back to a dot
QListWidget* attach(QTabWidget* tabs, const QList<char16_t>& glyphs);

// rebuilds the list after the tabs were added, removed, reordered, renamed
// or recolored (a tab whose text color is set shows dimmed)
void sync(QTabWidget* tabs);

}  // namespace FluentNavigation

#endif  // FLUENTNAVIGATION_H
