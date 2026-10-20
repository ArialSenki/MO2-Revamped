#ifndef FLUENTDIALOGS_H
#define FLUENTDIALOGS_H

// Fluent v4 pass over every dialog when it is first shown: Windows 11 sized
// buttons with one accent-filled primary action, flat page headers instead of
// banner cards, and Settings-style option rows in the dialogs that are mostly
// options. Runs once per dialog; MOApplication calls it for Fluent themes.

class QDialog;
class QMessageBox;
class QRect;
class QSize;

namespace FluentDialogs
{

// the largest size a dialog opens at in a work area: 82% of it and at most
// 1240x860, so a 1080p screen shows dialogs whole and a 1440p one does not get
// a wall of a window. MOApplication fits dialogs into it on their first show.
QSize maxSize(const QRect& available);

// whether polish() has run on the dialog
bool polished(const QDialog* dialog);

void polish(QDialog* dialog);

// Windows 11 message boxes: questions without the old "?" badge (Windows 11
// dialogs ask in words), information, warnings and errors with the Fluent
// status glyph in its color; the title bar follows the theme. Runs once.
void polishMessageBox(QMessageBox* box);

}  // namespace FluentDialogs

#endif  // FLUENTDIALOGS_H
