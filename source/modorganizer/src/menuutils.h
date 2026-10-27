#ifndef MODORGANIZER_MENUUTILS_INCLUDED
#define MODORGANIZER_MENUUTILS_INCLUDED

#include "fluenticons.h"

#include <utility.h>

#include <QAction>
#include <QClipboard>
#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QFont>
#include <QGuiApplication>
#include <QMenu>
#include <QString>

#include <utility>
#include <vector>

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

// Glyphs shared by the context menus, so the same command looks the same in
// every list
namespace MenuGlyphs
{
constexpr char16_t Folder      = 0xE838;  // FolderOpen
constexpr char16_t Copy        = 0xE8C8;
constexpr char16_t Delete      = 0xE74D;
constexpr char16_t Rename      = 0xE8AC;
constexpr char16_t Information = 0xE946;
constexpr char16_t Web         = 0xE774;  // Globe
constexpr char16_t Move        = 0xE8DE;  // Switch
constexpr char16_t Color       = 0xE790;
constexpr char16_t Warning     = 0xE7BA;
constexpr char16_t Hide        = 0xED1A;
constexpr char16_t Show        = 0xE890;
constexpr char16_t Install     = 0xE896;
constexpr char16_t Sync        = 0xE895;
constexpr char16_t Add         = 0xE710;
constexpr char16_t Merge       = 0xEA3C;
constexpr char16_t Clear       = 0xE894;
constexpr char16_t Document    = 0xE8A5;
constexpr char16_t Run         = 0xE768;
constexpr char16_t Save        = 0xE74E;
constexpr char16_t Refresh     = 0xE72C;
constexpr char16_t Expand      = 0xF164;
constexpr char16_t Collapse    = 0xF165;
constexpr char16_t Filter      = 0xE71C;
constexpr char16_t Tag         = 0xE8EC;
constexpr char16_t Star        = 0xE734;
constexpr char16_t Heart       = 0xEB51;
constexpr char16_t Restore     = 0xE777;
constexpr char16_t Backup      = 0xEA35;
constexpr char16_t List        = 0xEA37;
}  // namespace MenuGlyphs

inline QIcon menuGlyph(char16_t codepoint)
{
  return FluentIcons::glyph(codepoint);
}

// "Open folder" with one entry per folder; missing folders stay listed but
// disabled, files are shown selected in Explorer
inline QMenu* addOpenFolderMenu(QMenu* menu,
                                const std::vector<std::pair<QString, QString>>& entries,
                                const QString& title = {})
{
  auto* folders = menu->addMenu(menuGlyph(MenuGlyphs::Folder),
                                title.isEmpty() ? QCoreApplication::translate("MenuUtils", "Open folder") : title);
  for (const auto& [text, path] : entries) {
    if (text.isEmpty()) {
      folders->addSeparator();
      continue;
    }
    auto* action = folders->addAction(text, [path] {
      MOBase::shell::Explore(path);
    });
    action->setEnabled(!path.isEmpty() && QFileInfo::exists(path));
  }
  return folders;
}

// "Copy" with one entry per value, each copied to the clipboard as is
inline QMenu* addCopyMenu(QMenu* menu,
                          const std::vector<std::pair<QString, QString>>& entries)
{
  auto* copy = menu->addMenu(menuGlyph(MenuGlyphs::Copy), QCoreApplication::translate("MenuUtils", "Copy"));
  for (const auto& [text, value] : entries) {
    auto* action = copy->addAction(text, [value] {
      QGuiApplication::clipboard()->setText(value);
    });
    action->setEnabled(!value.isEmpty());
  }
  return copy;
}

// a single "Copy ..." entry when there is only one useful value
inline QAction* addCopyAction(QMenu* menu, const QString& text, const QString& value)
{
  auto* action = menu->addAction(menuGlyph(MenuGlyphs::Copy), text, [value] {
    QGuiApplication::clipboard()->setText(value);
  });
  action->setEnabled(!value.isEmpty());
  return action;
}

// the command a double-click runs: the menu's default, in bold as Windows
// shows it (the theme's item font would otherwise hide QMenu's own bold)
inline QAction* setDefaultMenuAction(QMenu* menu, QAction* action)
{
  menu->setDefaultAction(action);
  QFont font = action->font();
  font.setBold(true);
  action->setFont(font);
  return action;
}

inline QString nativePath(const QString& path)
{
  return QDir::toNativeSeparators(path);
}

#endif  // MODORGANIZER_MENUUTILS_INCLUDED
