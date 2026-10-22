#ifndef MODORGANIZER_SAVESTAB_INCLUDED
#define MODORGANIZER_SAVESTAB_INCLUDED

#include "savegameinfo.h"
#include <filterwidget.h>
#include <QDateTime>

namespace Ui
{
class MainWindow;
}

namespace MOBase
{
class ISaveGame;
class ISaveGameInfoWidget;
}  // namespace MOBase

class MainWindow;
class OrganizerCore;

class SavesTab : public QObject
{
  Q_OBJECT;

public:
  SavesTab(QWidget* window, OrganizerCore& core, Ui::MainWindow* ui);

  void refreshSaveList();
  void displaySaveGameInfo(QTreeWidgetItem* newItem);

  QDir currentSavesDir() const;

  void startMonitorSaves();
  void stopMonitorSaves();
  void hideSaveGameInfo();

protected:
  bool eventFilter(QObject* object, QEvent* e) override;

private:
  struct SavesTabUi
  {
    QTabWidget* mainTabs;
    QWidget* tab;
    QTreeWidget* list;
  };

  QWidget* m_window;
  OrganizerCore& m_core;
  SavesTabUi ui;

  // the footer: which saves the list shows and where they live
  class QLabel* m_route = nullptr;
  QString m_routePath;
  void createFooter();
  void updateRoute(const QDir& savesDir);
  MOBase::FilterWidget m_filter;
  std::vector<std::shared_ptr<const MOBase::ISaveGame>> m_SaveGames;
  MOBase::ISaveGameInfoWidget* m_CurrentSaveView;
  bool m_SaveListRefreshQueued = false;

  QTimer m_SavesWatcherTimer;
  QFileSystemWatcher m_SavesWatcher;

  // the profile folder watched for Save Isolation's route file, and that
  // file's last change; empty when only the saves folder is watched
  QString m_routeWatchFolder;
  QDateTime m_routeStamp;

  void onContextMenu(const QPoint& pos);
  void deleteSavegame();
  void saveSelectionChanged(QTreeWidgetItem* newItem);
  void fixMods(MOBase::SaveGameInfo::MissingAssets const& missingAssets);
  void refreshSavesIfOpen();
  void refreshSaveListNow();
  void openInExplorer();
  void startMonitorSaves(const QDir& savesDir);
};

#endif  // MODORGANIZER_SAVESTAB_INCLUDED
