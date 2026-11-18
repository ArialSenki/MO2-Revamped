#ifndef REVAMPEDGUIDE_H
#define REVAMPEDGUIDE_H

// The MO2 Revamped guide: short help pages for the parts of the program,
// opened from the "…" menu, and the What's This texts of the main window's
// command bar, for every game.

class QWidget;

namespace Ui
{
class MainWindow;
}

namespace RevampedGuide
{

// shows the guide as a modal window
void show(QWidget* parent);

// gives the main window's command bar, tabs and log their What's This
// texts; the Elden Ring help replaces some of them afterwards for its games
void applyWhatsThis(Ui::MainWindow* ui);

}  // namespace RevampedGuide

#endif  // REVAMPEDGUIDE_H
