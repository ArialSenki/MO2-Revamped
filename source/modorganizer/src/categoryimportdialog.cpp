#include "categoryimportdialog.h"
#include "circularuihelpers.h"
#include "ui_categoryimportdialog.h"

#include "organizercore.h"

using namespace MOBase;

CategoryImportDialog::CategoryImportDialog(QWidget* parent)
    : QDialog(parent), ui(new Ui::CategoryImportDialog)
{
  ui->setupUi(this);
  CircularUi::addIconToHeading(
      ui->dialogHeading, ":/MO/gui/contextmenu/categories.svg",
      QStringLiteral("categoryImportHeaderIcon"), 34,
      QStringLiteral("C"));
  CircularUi::addGroupHeading(
      ui->strategyBox, ":/MO/gui/mainwindow/settings.svg",
      QStringLiteral("categoryImportStrategyIcon"), 26,
      QStringLiteral("categoryImportSectionTitle"), QStringLiteral("S"));
  CircularUi::addGroupHeading(
      ui->mappingBox, ":/MO/gui/mainwindow/github-updates.svg",
      QStringLiteral("categoryImportMappingsIcon"), 26,
      QStringLiteral("categoryImportSectionTitle"), QStringLiteral("N"));
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
