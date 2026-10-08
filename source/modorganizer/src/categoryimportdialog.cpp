#include "categoryimportdialog.h"
#include "ui_categoryimportdialog.h"

#include "organizercore.h"

using namespace MOBase;

CategoryImportDialog::CategoryImportDialog(QWidget* parent)
    : QDialog(parent), ui(new Ui::CategoryImportDialog)
{
  ui->setupUi(this);
  setMinimumSize(580, 240);
  resize(620, 250);
  connect(ui->buttonBox, &QDialogButtonBox::accepted, this,
          &CategoryImportDialog::accepted);
  connect(ui->buttonBox, &QDialogButtonBox::rejected, this,
          &CategoryImportDialog::rejected);
  connect(ui->strategyGroup, &QButtonGroup::buttonClicked, this,
          &CategoryImportDialog::on_strategyClicked);
  connect(ui->assignOption, &QCheckBox::clicked, this,
          &CategoryImportDialog::on_assignOptionClicked);
}

void CategoryImportDialog::accepted()
{
  accept();
}

void CategoryImportDialog::rejected()
{
  reject();
}

CategoryImportDialog::~CategoryImportDialog()
{
  delete ui;
}

CategoryImportDialog::ImportStrategy CategoryImportDialog::strategy()
{
  if (ui->mergeOption->isChecked()) {
    return ImportStrategy::Merge;
  } else if (ui->replaceOption->isChecked()) {
    return ImportStrategy::Overwrite;
  }
  return ImportStrategy::None;
}

bool CategoryImportDialog::assign()
{
  return ui->assignOption->isChecked();
}

bool CategoryImportDialog::remap()
{
  return ui->remapOption->isChecked();
}

void CategoryImportDialog::on_strategyClicked(QAbstractButton* button)
{
  const bool canUpdateMappings =
      button == ui->mergeOption && ui->assignOption->isChecked();
  ui->remapOption->setEnabled(canUpdateMappings);
  if (!canUpdateMappings) {
    ui->remapOption->setChecked(false);
  }
}

void CategoryImportDialog::on_assignOptionClicked(bool checked)
{
  const bool canUpdateMappings = checked && strategy() == ImportStrategy::Merge;
  ui->remapOption->setEnabled(canUpdateMappings);
  if (!canUpdateMappings) {
    ui->remapOption->setChecked(false);
  }
}
