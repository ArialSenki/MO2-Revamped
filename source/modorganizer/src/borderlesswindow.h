#ifndef BORDERLESSWINDOW_H
#define BORDERLESSWINDOW_H

// MO2 Revamped: one toggle in the Run menu that starts the game in a window
// without a border that covers the screen. Skyrim Special Edition, Enderal SE
// and Fallout 4 do it themselves (bBorderless in their Prefs INI). Fallout 3,
// New Vegas, Oblivion and the original Skyrim only have a window with a title
// bar: MO2 starts them windowed at the screen's size and takes the border off
// the game's window when it appears. Turned off, the INI gets back the values
// it had before.

#include <QCoreApplication>
#include <QFileInfo>
#include <QString>

#include <optional>
#include <utility>

class OrganizerCore;

namespace MOBase
{
class IPluginGame;
}

class BorderlessWindow
{
  Q_DECLARE_TR_FUNCTIONS(BorderlessWindow)

public:
  // whether the game has the option
  static bool supported(const MOBase::IPluginGame* game);

  // the instance's choice
  static bool enabled(OrganizerCore& core);
  static void setEnabled(OrganizerCore& core, bool enabled);

  // right before a program in the game folder starts: writes the INI keys
  // (or puts back the earlier values when the option is off); true when
  // the game's window has to lose its border once it shows
  static bool prepareLaunch(OrganizerCore& core, const QFileInfo& binary);

  // watches for the game's window and takes its border off, again if the
  // game puts it back, until the game closes
  static void watchGameWindow(const MOBase::IPluginGame* game);

  // for the games MO2 takes the frame off: MO2's DLL that does it from
  // inside the game (the game's exe, the DLL's path); nullopt for the
  // others, or when the DLL isn't installed
  static std::optional<std::pair<QString, QString>>
  forcedLibrary(const MOBase::IPluginGame* game);
};

#endif  // BORDERLESSWINDOW_H
