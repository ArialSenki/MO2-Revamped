#ifndef WABBAJACKDIALOG_H
#define WABBAJACKDIALOG_H

// MO2 Revamped: the window of a Wabbajack list (.wabbajack): what it is, what
// it needs, its archives and which of them are already in a downloads folder.
// Reading the list and checking files by hash run off the UI thread.

#include "wabbajackdownloads.h"
#include "wabbajackinstaller.h"
#include "wabbajacklist.h"

#include <QDialog>
#include <QFutureWatcher>
#include <QPointer>

#include <atomic>
#include <memory>
#include <optional>

class OrganizerCore;
class QFrame;
class QHBoxLayout;
class QLabel;
class QLineEdit;
class QPushButton;
class QTreeWidget;
class QTreeWidgetItem;

class WabbajackDialog : public QDialog
{
  Q_OBJECT

public:
  WabbajackDialog(OrganizerCore& core, const QString& path, QWidget* anchor = nullptr);
  ~WabbajackDialog() override;

  QString path() const { return m_path; }
  bool isRunning() const;

  // an nxm link from the browser for a file the list waits for (a free
  // account); false when it isn't one
  bool claimNxm(const QString& url);

  // the list that was downloading when MO2 closed, empty when none; MO2 opens
  // it again at its start
  static QString resumeList();

  bool isInstalling() const;
  // MO2 is closing: the install stops at once (the archive it is in included)
  void stopInstallAndWait();

  // asks for a .wabbajack file; empty when none was chosen
  static QString askFile(QWidget* parent);

protected:
  void closeEvent(QCloseEvent* e) override;
  // Escape closes the window like its close button (and so deletes it)
  void reject() override;

private:
  OrganizerCore& m_core;
  QString m_path;
  QPointer<QWidget> m_anchor;

  std::shared_ptr<Wabbajack::ModList> m_list;
  Wabbajack::DownloadQueue* m_queue = nullptr;
  QFutureWatcher<std::optional<Wabbajack::ModList>>* m_reader = nullptr;
  QString m_readError;
  // the reading stops when the window closes; nothing waits for it
  std::shared_ptr<std::atomic_bool> m_readCancel;
  // the tree's item of each archive, by its index in the list
  std::vector<QTreeWidgetItem*> m_items;

  // the install running in the background, and where it goes
  QFutureWatcher<Wabbajack::InstallResult>* m_installer = nullptr;
  std::shared_ptr<std::atomic_bool> m_installCancel;
  QString m_instanceName;
  QString m_installFolder;

  QLabel* m_image;
  QLabel* m_name;
  QLabel* m_caption;
  QLabel* m_loading;
  QHBoxLayout* m_links;
  QLabel* m_description;
  QFrame* m_needsCard;
  QLabel* m_needs;
  QLabel* m_notes;
  QFrame* m_archivesCard;
  QLabel* m_count;
  QTreeWidget* m_tree;
  QLineEdit* m_folder;
  QPushButton* m_chooseFolder;
  QPushButton* m_check;
  QLabel* m_space;
  QLabel* m_status;
  QPushButton* m_retry;
  QPushButton* m_next;
  QPushButton* m_download;
  QPushButton* m_install;
  QPushButton* m_close;

  void setupUi();
  QFrame* createHeader();
  QFrame* createNeeds();
  QFrame* createArchives();
  void placeWindow();

  void onRead();
  void showList();
  void fillTree();
  void updateRow(int row);
  // the count, the free space and the buttons
  void updateState();
  // the window was closed while downloads ran: they go on next time
  void rememberResume(bool running);

  QString gameDirectory() const;

  // why the list can't be installed now, empty when it can
  QString installProblem() const;
  // the window closes once the install has stopped
  bool m_closeAfterInstall = false;
  // asks for the instance's name and folder, then installs
  void startInstall();
  void onInstalled();
  // the new instance, with the list's own settings; false with a message
  bool makeInstance(const Wabbajack::InstallResult& result, QString* dataPath);
  QString sourceText(const Wabbajack::Archive& a) const;
};

#endif  // WABBAJACKDIALOG_H
