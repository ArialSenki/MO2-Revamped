#ifndef COLLECTIONDIALOG_H
#define COLLECTIONDIALOG_H

// MO2 Revamped: the window of a Nexus collection: what it is, its mods and
// their progress, where it goes, and the buttons that move it on. The work is
// done by CollectionSession.

#include "collectionmanifest.h"
#include "collectionsession.h"

#include <QElapsedTimer>
#include <QDialog>
#include <QPointer>
#include <functional>
#include <optional>

class OrganizerCore;
class QButtonGroup;
class QCheckBox;
class QLabel;
class QLineEdit;
class QPlainTextEdit;
class QPushButton;
class QTextBrowser;
class QTreeWidget;
class QTreeWidgetItem;
class QFrame;

class CollectionDialog : public QDialog
{
  Q_OBJECT

public:
  CollectionDialog(OrganizerCore& core, const Collections::CollectionLink& link,
                   QWidget* anchor = nullptr);
  ~CollectionDialog() override;

  // asks for a collection's link (the clipboard's, when it holds one)
  static std::optional<Collections::CollectionLink> askLink(QWidget* parent);

  void setSwitchProfile(std::function<bool(const QString&)> f);
  void setRunLoot(std::function<void()> f);

  const Collections::CollectionLink& link() const { return m_session->link(); }
  bool isRunning() const;

  // a file link the browser sent for this collection: no notice for it
  bool claims(const QString& nxmUrl) const;
  void setAutoStart(bool b);

protected:
  void closeEvent(QCloseEvent* e) override;
  bool eventFilter(QObject* watched, QEvent* event) override;
  void moveEvent(QMoveEvent* e) override;
  void resizeEvent(QResizeEvent* e) override;

private:
  OrganizerCore& m_core;
  CollectionSession* m_session;
  std::function<void()> m_runLoot;
  bool m_built = false;

  QLabel* m_name;
  QLabel* m_caption;
  QLabel* m_loading;
  QFrame* m_instructionsCard;
  QTextBrowser* m_instructions;
  QFrame* m_modsCard;
  QLabel* m_count;
  QTreeWidget* m_tree;
  QFrame* m_external;
  QLabel* m_externalText;
  QPushButton* m_externalOpen;
  QPushButton* m_externalChoose;
  QPushButton* m_externalDone;
  QLabel* m_notes;
  QFrame* m_optionsCard;
  QPointer<QWidget> m_anchor;
  QButtonGroup* m_targets;
  QButtonGroup* m_modes;
  QCheckBox* m_byCategory;
  QFrame* m_autoModeRow;
  QFrame* m_pageModeRow;
  QFrame* m_instanceRow;
  QLineEdit* m_instanceName;
  QLabel* m_instanceProblem;
  QLineEdit* m_profileName;
  QLineEdit* m_separator;
  QCheckBox* m_autoOpen;
  QFrame* m_autoOpenRow;
  QFrame* m_summaryCard;
  QPlainTextEdit* m_summary;
  QPushButton* m_loot;
  QLabel* m_status;
  QPushButton* m_next;
  QPushButton* m_start;
  QPushButton* m_cancel;

  void setupUi();
  void placeWindow();
  QElapsedTimer m_placedAt;
  bool m_userPlaced = false;
  bool m_placing    = false;
  QFrame* createHeader();
  QFrame* createInstructions();
  QFrame* createMods();
  QFrame* createOptions();
  QFrame* createSummary();

  void onChanged();
  void onEntryChanged(int row);
  void buildRows();
  void updateRow(QTreeWidgetItem* item, int row);
  void updateExternal();
  void updateButtons();
  void showMenu(const QPoint& pos);
  void chooseFile(int row);
  int currentRow() const;

  QString statusText(const CollectionSession::Entry& e) const;
  QString sourceText(Collections::SourceType t) const;
};

#endif  // COLLECTIONDIALOG_H
