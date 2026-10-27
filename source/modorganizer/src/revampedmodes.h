#ifndef REVAMPEDMODES_H
#define REVAMPEDMODES_H

#include <QString>

class OrganizerCore;
class QWidget;
class Settings;

// Game modes (beta): MO2 itself, me3 and Mod Engine 2. me3 and Mod Engine 2
// each get an instance of their own, made from the main one the first time:
// same game, shared downloads, nothing else carried over (no mods, profiles,
// saves unless asked, plugin settings or launchers), so a problem in one mode
// stays there. Switching restarts MO2 in that mode's instance.
//
// The instances are linked in their ModOrganizer.ini, [Settings]:
//   main:      revamped_mode_me3, revamped_mode_me2 = the mode instance's id
//   mode:      revamped_mode = me3|me2, revamped_mode_parent = the main id,
//              revamped_mode_parent_dir = its folder, revamped_mode_launcher =
//              the launcher the mode starts (read by the game plugin)
namespace RevampedModes
{

enum class Mode
{
  MO2,
  Me3,
  Me2,
};

// "me3" or "me2"; empty for MO2
QString key(Mode mode);

// the mode's name as the selector shows it
QString title(Mode mode);

// the executable the game plugin adds for the mode ("me3 (beta)", ...)
QString launcherTitle(Mode mode);

// the mode of the instance these settings belong to
Mode current(const Settings& settings);

// whether the selector shows: a beta switch is on, or this is a mode
// instance (its way back to MO2)
bool available(const Settings& settings);

// switches to the mode's instance (making it the first time) and restarts
// MO2 there; false when nothing changed (cancelled, game running, ...)
bool switchTo(QWidget* parent, OrganizerCore& core, Mode target);

}  // namespace RevampedModes

#endif  // REVAMPEDMODES_H
