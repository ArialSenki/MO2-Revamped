#ifndef FLUENTDIALOGS_H
#define FLUENTDIALOGS_H

// Fluent v4 pass over every dialog when it is first shown: Windows 11 sized
// buttons with one accent-filled primary action, flat page headers instead of
// banner cards, and Settings-style option rows in the dialogs that are mostly
// options. Runs once per dialog; MOApplication calls it for Fluent themes.

class QDialog;

namespace FluentDialogs
{

void polish(QDialog* dialog);

}  // namespace FluentDialogs

#endif  // FLUENTDIALOGS_H
