#ifndef WABBAJACKDIALOG_H
#define WABBAJACKDIALOG_H

// MO2 Revamped: the window of a Wabbajack list (.wabbajack): what it is, what
// it needs, its archives and which of them are already in a downloads folder.
// Reading the list and checking files by hash run off the UI thread.

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

  // asks for a .wabbajack file; empty when none was chosen
  static QString askFile(QWidget* parent);

protected:
  void closeEvent(QCloseEvent* e) override;
  // Escape closes the window like its close button (and so deletes it)
  void reject() override;

private:
  enum class FileState
  {
    Unknown,     // not looked at yet
    Missing,     // not in the folder
    Present,     // a file of that name and size is there
    Verified,    // and its hash is the list's
    WrongHash,   // a file of that name, but not that file
    GameFound,   // a game file, with the list's hash
    GameDiffers, // a game file, but another version of it
    GameMissing  // the game file isn't there
  };

  OrganizerCore& m_core;
  QString m_path;
  QPointer<QWidget> m_anchor;

  std::shared_ptr<Wabbajack::ModList> m_list;
  std::vector<FileState> m_states;
  QFutureWatcher<std::optional<Wabbajack::ModList>>* m_reader = nullptr;
  // the reading stops when the window closes; nothing waits for it
  std::shared_ptr<std::atomic_bool> m_readCancel;
  // the tree's item of each archive, by its index in the list
  std::vector<QTreeWidgetItem*> m_items;
  // the folder the states were found in: another one starts over
  QString m_scannedFolder;
  QString m_readError;

  // the hash check running in the background
  QFutureWatcher<void>* m_hasher = nullptr;
  std::shared_ptr<std::atomic_bool> m_cancel;

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
  QLabel* m_status;
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
  void updateCount();

  // by name and size, at once
  void scanFolder();
  // by hash: the files found in the folder and the game's files
  void checkHashes();
  void cancelHashes();

  QString gameDirectory() const;
  // the archive's file in the game folder; empty when the list's path would
  // lead out of it
  QString gameFilePath(const QString& gameDir, const Wabbajack::Archive& a) const;
  QString sourceText(const Wabbajack::Archive& a) const;
  QString stateText(FileState s) const;
};

#endif  // WABBAJACKDIALOG_H
