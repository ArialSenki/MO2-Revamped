#ifndef FORCEDLOADDIALOG_H
#define FORCEDLOADDIALOG_H

#include <QDialog>
#include <QPushButton>

#include "executableinfo.h"
#include "iplugingame.h"

namespace Ui
{
class ForcedLoadDialog;
}

class ForcedLoadDialogWidget;
class QCheckBox;
class QLabel;
class QLineEdit;
class QListWidget;
namespace FluentPanels
{
class ElidedLabel;
}

class ForcedLoadDialog : public QDialog
{
  Q_OBJECT

public:
  explicit ForcedLoadDialog(const MOBase::IPluginGame* game, QWidget* parent = nullptr);
  ~ForcedLoadDialog();

  void setValues(QList<MOBase::ExecutableForcedLoadSetting>& values);
  QList<MOBase::ExecutableForcedLoadSetting> values();

private slots:
  void on_addRowButton_clicked();
  void on_deleteRowButton_clicked();

private:
  // Fluent v4: a list card of the rules and a details card of Settings rows;
  // the table keeps one rule widget per row as the data behind them
  void buildFluentLayout();
  void refreshFluent();
  void showFluentListMenu(const QPoint& pos);
  ForcedLoadDialogWidget* selectedRule() const;
  ForcedLoadDialogWidget* addRule();
  QString libraryFile(ForcedLoadDialogWidget* rule) const;

  Ui::ForcedLoadDialog* ui;
  const MOBase::IPluginGame* m_GamePlugin;

  bool m_fluent         = false;
  bool m_fluentSyncing  = false;
  QListWidget* m_list   = nullptr;
  QLabel* m_count       = nullptr;
  QLabel* m_name        = nullptr;
  QLabel* m_caption     = nullptr;
  QCheckBox* m_enabled  = nullptr;
  QLineEdit* m_process  = nullptr;
  QLineEdit* m_library  = nullptr;
  QPushButton* m_browseProcess = nullptr;
  QPushButton* m_browseLibrary = nullptr;
  FluentPanels::ElidedLabel* m_found = nullptr;
  QLabel* m_forcedNote  = nullptr;
  QWidget* m_rows       = nullptr;
  QLabel* m_empty       = nullptr;
  QPushButton* m_duplicate = nullptr;
  QPushButton* m_remove = nullptr;
};

#endif  // FORCEDLOADDIALOG_H
