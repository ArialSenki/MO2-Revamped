#ifndef COLLECTIONDIALOG_H
#define COLLECTIONDIALOG_H

// MO2 Revamped: the window of a Nexus collection: what it is, its mods and
// their progress, where it goes, and the buttons that move it on. The work is
// done by CollectionSession.

#include "collectionmanifest.h"
#include "collectionsession.h"

#include <QElapsedTimer>
#include <QDialog>
#include <QNetworkAccessManager>
#include <QTimer>
#include <QPointer>
#include <QVariant>
#include <array>
#include <functional>
#include <optional>

class OrganizerCore;
class QButtonGroup;
class QCheckBox;
class QComboBox;
class QLabel;
class QLineEdit;
class QPlainTextEdit;
class QProgressBar;
class QPushButton;
class QTextBrowser;
class QTreeWidget;
class QTreeWidgetItem;
class QFrame;
class ScreenFitter;

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
  // the main window opens a newer revision, from this one's state
  void setOpenUpdate(std::function<void(const QString& link, int fromRevision)> f);
  void setUpdateFrom(int revision);

  const Collections::CollectionLink& link() const { return m_session->link(); }
  bool isRunning() const;

  // a file link the browser sent for this collection: no notice for it
  bool claims(const QString& nxmUrl) const;
  void setAutoStart(bool b);

public slots:
  // Esc: through closeEvent() too (since Qt 6.3 QDialog::reject() closes
  // without one), so a running collection pauses and a wait isn't cut
  void reject() override;

protected:
  void closeEvent(QCloseEvent* e) override;
  bool eventFilter(QObject* watched, QEvent* event) override;
  void moveEvent(QMoveEvent* e) override;
  void resizeEvent(QResizeEvent* e) override;

private:
  // what the collection's page on Nexus says about it, asked by the window for
  // its header and notes (the session asks for what the install needs)
  struct Info
  {
    QString domain;
    QString summary;
    QString description;
    QString category;
    QString image;
    QStringList gameVersions;
    int endorsements = 0;
    int downloads    = 0;
    bool adult       = false;
    bool known       = false;
  };

  OrganizerCore& m_core;
  CollectionSession* m_session;
  std::function<void()> m_runLoot;
  std::function<void(const QString&, int)> m_openUpdate;
  QPushButton* m_update = nullptr;
  QPushButton* m_retryFailed = nullptr;
  bool m_built = false;
  bool m_inCloseEvent = false;  // reject() from QDialog::closeEvent()
  QTimer m_changeTimer;
  bool m_rowsLocked = false;
  bool m_rowsDrawn  = false;

  QLabel* m_name;
  QLabel* m_caption;
  QLabel* m_loading;
  QLabel* m_image;
  QLabel* m_about;
  QPushButton* m_page;
  QFrame* m_needsCard;
  QLabel* m_needs;
  QLabel* m_needsNotes;
  QFrame* m_stepsCard;
  std::array<QLabel*, 3> m_stepText{};
  std::array<QProgressBar*, 3> m_stepBar{};
  QFrame* m_instructionsCard;
  QTextBrowser* m_instructions;
  QString m_notesShown;
  Info m_info;
  int m_infoRequest = -1;
  QNetworkAccessManager m_network;
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
  QComboBox* m_targetCombo;
  QLineEdit* m_targetName;
  QLabel* m_targetText;
  QComboBox* m_modeCombo;
  QLabel* m_downloadText;
  QCheckBox* m_byCategory;
  QLineEdit* m_separator;
  QCheckBox* m_autoOpen;
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
  // its size by the screen it is on (shared with the Wabbajack list window)
  ScreenFitter* m_fitter = nullptr;
  bool m_placing    = false;
  QFrame* createHeader();
  QFrame* createNeeds();
  QFrame* createSteps();
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
  void updateNeeds();
  void updateSteps();
  void updateNotes();
  void requestInfo();
  void onInfo(QVariant userData, QVariant result, int id);
  void onInfoFailed(QString, int, int, QVariant, int id, int, const QString&);
  void showMenu(const QPoint& pos);
  void chooseFile(int row);
  int currentRow() const;

  QString statusText(const CollectionSession::Entry& e) const;
  QString sourceText(Collections::SourceType t) const;
};

#endif  // COLLECTIONDIALOG_H
