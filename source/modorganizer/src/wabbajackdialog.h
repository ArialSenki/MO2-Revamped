#ifndef WABBAJACKDIALOG_H
#define WABBAJACKDIALOG_H

// MO2 Revamped: the window of a Wabbajack list (.wabbajack): what it is, what
// it needs, its archives and which of them are already in a downloads folder.
// Reading the list and checking files by hash run off the UI thread.

#include "wabbajackdownloads.h"
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
  QString sourceText(const Wabbajack::Archive& a) const;
};

#endif  // WABBAJACKDIALOG_H
