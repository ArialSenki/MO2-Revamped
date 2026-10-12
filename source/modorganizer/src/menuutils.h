#ifndef MODORGANIZER_MENUUTILS_INCLUDED
#define MODORGANIZER_MENUUTILS_INCLUDED

#include <QAction>
#include <QMenu>
#include <QString>

// The Revamped themes draw menu sections as plain dividers without a title, so
// a section at the top of a menu only adds a stray line above the first item.
// Adds the section only when the menu already contains an action.
inline QAction* addMenuSection(QMenu* menu, const QString& title)
{
  if (menu == nullptr || menu->actions().isEmpty()) {
    return nullptr;
  }

  return menu->addSection(title);
}

#endif  // MODORGANIZER_MENUUTILS_INCLUDED
