#ifndef UPDATENOTICEDIALOG_H
#define UPDATENOTICEDIALOG_H

#include "selfupdater.h"

#include <QDialog>
#include <QLocale>
#include <vector>

class QLabel;
class QListWidget;
class QListWidgetItem;
class QPushButton;
class QScrollArea;
class Settings;

// tells the user a newer MO2 Revamped release is out and what it changes, laid
// out like the manager dialogs (instances, profiles): the releases newer than
// this build in a list card, newest first, and the selected one's notes in a
// details card with links to the release on GitHub and the listing on Nexus.
// Updating stays the user's: nothing is downloaded or installed from here.
// "Skip this version" keeps the newest release from opening the notice by
// itself again; closing it any other way shows it again on the next start.
class UpdateNoticeDialog : public QDialog
{
  Q_OBJECT

public:
  UpdateNoticeDialog(QWidget* parent, Settings& settings,
                     const QString& currentVersion,
                     const std::vector<SelfUpdater::ReleaseInfo>& releases);

  // the MO2 Revamped listing on Nexus
  static QString nexusPageUrl();

private:
  // dates in the interface's language
  QLocale m_locale;
  std::vector<SelfUpdater::ReleaseInfo> m_releases;

  QListWidget* m_list   = nullptr;
  QLabel* m_name        = nullptr;
  QLabel* m_caption     = nullptr;
  QWidget* m_notesBox   = nullptr;
  QScrollArea* m_scroll = nullptr;
  QPushButton* m_github = nullptr;
  // the index in m_releases of the release on the right, -1 before the first
  int m_shown = -1;

  QWidget* createListCard(const QString& currentVersion);
  QWidget* createDetailsCard(Settings& settings);
  void showRelease(int index);
  void fillNotes(const QString& notes);
  QString facts(const SelfUpdater::ReleaseInfo& release, QLocale::FormatType format) const;
};

#endif  // UPDATENOTICEDIALOG_H
