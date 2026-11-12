#ifndef DIALOGPLACEMENT_H
#define DIALOGPLACEMENT_H

// MO2 Revamped: where every dialog opens. Before a dialog shows the first
// time, MOApplication hands it here:
//
// - its owner is the window the user is looking at: the dialog that is
//   modal on top (the instance wizard, Settings, an installer), else its
//   own parent, else MO2's active or main window. A dialog made with no
//   parent, with the main window as parent while a wizard was open, or with
//   QApplication::activeWindow() while MO2 wasn't in front, opened behind
//   those windows or nowhere near them; with this owner Windows keeps it on
//   top of that window;
// - it is centred over that owner, at its final size (after the Fluent
//   polish reshapes it), and kept inside the work area of the owner's
//   monitor, so it doesn't open at a saved spot, on another monitor, or move
//   after its first frame.
//
// A dialog that keeps its own place sets the property "mo2rOwnPosition".

class QDialog;
class QWidget;

namespace DialogPlacement
{

// the window a dialog belongs over, or null when MO2 shows none
QWidget* anchorFor(const QWidget* dialog);

// before the dialog's first frame: owner, size and place
void place(QDialog* dialog, bool fluentSize);

}  // namespace DialogPlacement

#endif  // DIALOGPLACEMENT_H
