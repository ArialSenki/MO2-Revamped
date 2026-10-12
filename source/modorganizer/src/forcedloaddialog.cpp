#include "forcedloaddialog.h"
#include "ui_forcedloaddialog.h"

#include "forcedloaddialogwidget.h"

#include <QHBoxLayout>
#include <QGroupBox>
#include <QIcon>
#include <QComboBox>
#include <QLabel>
#include <QPixmap>
#include <QVBoxLayout>

using namespace MOBase;

namespace
{
void addRulesGroupHeading(QGroupBox* group)
{
  if (group == nullptr) {
    return;
  }

  auto* layout = qobject_cast<QVBoxLayout*>(group->layout());
  if (layout == nullptr) {
    return;
  }

  const QString title = group->title();
  group->setTitle(QString());

  auto* row = new QHBoxLayout();
  row->setContentsMargins(0, 0, 0, 0);
  row->setSpacing(8);

  auto* icon = new QLabel(group);
  icon->setObjectName(QStringLiteral("rulesSectionIcon"));
  icon->setAlignment(Qt::AlignCenter);
  icon->setFixedSize(26, 26);
  const QPixmap pixmap = QIcon(QStringLiteral(":/MO/gui/mainwindow/files/library.svg"))
                             .pixmap(18, 18);
  if (!pixmap.isNull()) {
    icon->setPixmap(pixmap);
  }

  auto* heading = new QLabel(title, group);
  heading->setObjectName(QStringLiteral("rulesSectionTitle"));
  row->addWidget(icon, 0, Qt::AlignVCenter);
  row->addWidget(heading, 0, Qt::AlignVCenter);
  row->addStretch(1);
  layout->insertLayout(0, row);
}
}  // namespace

ForcedLoadDialog::ForcedLoadDialog(const IPluginGame* game, QWidget* parent)
    : QDialog(parent), ui(new Ui::ForcedLoadDialog), m_GamePlugin(game)
{
  ui->setupUi(this);
  addRulesGroupHeading(ui->rulesGroup);
  resize(860, 520);
}

ForcedLoadDialog::~ForcedLoadDialog()
{
  delete ui;
}

void ForcedLoadDialog::setValues(QList<MOBase::ExecutableForcedLoadSetting>& values)
{
  ui->tableWidget->clearContents();

  for (int i = 0; i < values.count(); i++) {
    ForcedLoadDialogWidget* item = new ForcedLoadDialogWidget(m_GamePlugin, this);
    item->setEnabled(values[i].enabled());
    item->setProcess(values[i].process());
    item->setLibraryPath(values[i].library());
    item->setForced(values[i].forced());

    ui->tableWidget->insertRow(i);
    ui->tableWidget->setCellWidget(i, 0, item);
  }

  ui->tableWidget->resizeRowsToContents();
}

QList<MOBase::ExecutableForcedLoadSetting> ForcedLoadDialog::values()
{
  QList<MOBase::ExecutableForcedLoadSetting> results;
  for (int row = 0; row < ui->tableWidget->rowCount(); row++) {
    auto widget = (ForcedLoadDialogWidget*)ui->tableWidget->cellWidget(row, 0);
    results.append(
        ExecutableForcedLoadSetting(widget->getProcess(), widget->getLibraryPath())
            .withEnabled(widget->getEnabled())
            .withForced(widget->getForced()));
  }
  return results;
}

void ForcedLoadDialog::on_addRowButton_clicked()
{
  int row = ui->tableWidget->rowCount();
  ui->tableWidget->insertRow(row);
  ForcedLoadDialogWidget* item = new ForcedLoadDialogWidget(m_GamePlugin, this);
  ui->tableWidget->setCellWidget(row, 0, item);
  ui->tableWidget->resizeRowsToContents();
}

void ForcedLoadDialog::on_deleteRowButton_clicked()
{
  for (auto rowIndex : ui->tableWidget->selectionModel()->selectedRows()) {
    int row     = rowIndex.row();
    auto widget = (ForcedLoadDialogWidget*)ui->tableWidget->cellWidget(row, 0);
    if (!widget->getForced()) {
      ui->tableWidget->removeRow(row);
    }
  }
}
