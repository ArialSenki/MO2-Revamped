#ifndef UPDATENOTICEDIALOG_H
#define UPDATENOTICEDIALOG_H

#include "fluentdialogs.h"
#include "selfupdater.h"

#include <QLocale>
#include <vector>

class Settings;

// tells the user a newer MO2 Revamped release is out and what it changes: the
// release notes of every release newer than this build, newest first, with
// links to the release on GitHub and the listing on Nexus. Updating stays the
// user's: nothing is downloaded or installed from here. "Skip this version"
// keeps the newest release from opening the notice by itself again; closing it
// any other way shows it again on the next start.
class UpdateNoticeDialog : public FluentContentDialog
{
  Q_OBJECT

public:
  UpdateNoticeDialog(QWidget* parent, Settings& settings,
                     const QString& currentVersion,
                     const std::vector<SelfUpdater::ReleaseInfo>& releases);

  // the MO2 Revamped listing on Nexus
  static QString nexusPageUrl();

private:
  QLocale m_locale;

  QWidget* createNotes(const std::vector<SelfUpdater::ReleaseInfo>& releases);
};

#endif  // UPDATENOTICEDIALOG_H
