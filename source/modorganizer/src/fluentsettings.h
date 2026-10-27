#ifndef FLUENTSETTINGS_H
#define FLUENTSETTINGS_H

// Windows 11 Settings rows for MO2's settings pages. Every check box becomes
// a full-width row: its label as the title, the first sentence of its tooltip
// as the description, and a toggle switch on the right. The check box itself
// stays (its text moves to the row), so the pages' load and save code, its
// signals and its object name are unchanged. Clicking anywhere on the row
// toggles it.

class QWidget;

namespace FluentSettings
{

// restyles the check boxes below root once; calling it again is harmless
// allowScroll wraps a page without a scroll area into one (Settings pages);
// dialogs pass false
void restyle(QWidget* root, bool allowScroll = true);

// turns only the check boxes below root into switch rows, leaving the layout
// as it is; for plugin windows with their own card layout. A word-wrapped
// label placed right below a check box becomes the row's description.
void switchRows(QWidget* root);

// puts a page without a scroll area into one; restyle() does this too, but
// pages built lazily need it up front so they do not inflate the dialog
void wrapInScrollArea(QWidget* page);

// Windows 11 puts a page's scroll bar at the window's edge: the page's own
// scroll areas keep their gap to the edge inside, with the bar beyond it.
// The dialog sets its right margin to 0; calling it again is harmless.
void scrollAtEdge(QWidget* page, int gap = 16);

}  // namespace FluentSettings

#endif  // FLUENTSETTINGS_H
