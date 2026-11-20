#ifndef REVAMPEDGUIDE_H
#define REVAMPEDGUIDE_H

// The MO2 Revamped guide: short help pages for the parts of the program, the
// first steps of a new user and the games MO2 Revamped has its own plugin
// for, opened from the "…" menu; and the What's This texts of the main
// window's command bar, for every game.

#include <QString>

#include <functional>

class QWidget;

namespace MOBase
{
class IPluginGame;
}

namespace Ui
{
class MainWindow;
}

namespace RevampedGuide
{

// what a button in the guide asks the window that opened it to do, once the
// guide has closed: "settings:<page object name>", "instances", "install",
// "profiles", "executables", "collections", "wabbajack", "bugreport",
// "game-guide"
using Action = std::function<void(const QString& id)>;

// topic ids
inline const QString FirstSteps = QStringLiteral("first-steps");
inline const QString SettingsTopic = QStringLiteral("settings");

// shows the guide as a modal window at the given topic (the first one when
// empty or unknown); without an action handler the buttons that need the
// main window are left out
void show(QWidget* parent, const QString& topic = {}, const Action& action = {});

// the topic about the given game's own plugin, or empty when MO2 Revamped
// has none for it
QString gameTopic(const MOBase::IPluginGame* game);

// gives the main window's command bar, tabs and log their What's This
// texts; the Elden Ring help replaces some of them afterwards for its games
void applyWhatsThis(Ui::MainWindow* ui);

}  // namespace RevampedGuide

#endif  // REVAMPEDGUIDE_H
