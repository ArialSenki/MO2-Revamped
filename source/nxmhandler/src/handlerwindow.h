#ifndef HANDLERWINDOW_H
#define HANDLERWINDOW_H

// MO2 Revamped: the handler list ("How to handle Nexus links") rebuilt in the
// Windows 11 Fluent style of MO2 Revamped. It replaces upstream's
// handlerwindow.ui; what it stores and how links are routed is unchanged.

#include "handlerstorage.h"

#include <QMainWindow>

class QFrame;
class QLabel;
class QListWidget;
class QListWidgetItem;
class QPushButton;
class QToolButton;
class HandlerList;

class HandlerWindow : public QMainWindow
{
  Q_OBJECT

public:
  explicit HandlerWindow(QWidget* parent = nullptr);
  ~HandlerWindow();

  // the program Windows sends nxm links to (HKCU\Software\Classes\nxm)
  void setPrimaryHandler(const QString& handlerPath);
  void setHandlerStorage(HandlerStorage* storage);

  // the path of the program registered with Windows, as last set
  QString primaryHandler() const { return m_PrimaryHandler; }

protected:
  void closeEvent(QCloseEvent* event) override;
  void changeEvent(QEvent* event) override;

private:
  void addBinaryDialog();
  void editBinary();
  void removeBinary();
  void removeMissing();
  void moveSelected(int delta);
  void useForAllGames();
  void testLink();
  void registerActive();
  void showContextMenu(const QPoint& pos);

  QListWidgetItem* addItem(const QStringList& games, const QString& executable,
                           const QString& arguments, int row);
  std::list<HandlerInfo> currentHandlers() const;
  int missingCount() const;
  QString registeredElsewhere() const;
  void updateRegistration();
  void updateState();
  void updateIcons();

  HandlerStorage* m_Storage = nullptr;
  QString m_PrimaryHandler;
  bool m_Built = false;

  HandlerList* m_List = nullptr;

  QLabel* m_RegistrationIcon = nullptr;
  QLabel* m_RegistrationValue = nullptr;
  QLabel* m_RegistrationPath = nullptr;
  QLabel* m_RegistrationStatus = nullptr;
  QPushButton* m_RegisterButton = nullptr;

  QFrame* m_MissingBar = nullptr;
  QLabel* m_MissingIcon = nullptr;
  QLabel* m_MissingText = nullptr;

  QToolButton* m_AddButton = nullptr;
  QToolButton* m_TestButton = nullptr;
  QToolButton* m_EditButton = nullptr;
  QToolButton* m_AllGamesButton = nullptr;
  QToolButton* m_UpButton = nullptr;
  QToolButton* m_DownButton = nullptr;
  QToolButton* m_RemoveButton = nullptr;
};

#endif  // HANDLERWINDOW_H
