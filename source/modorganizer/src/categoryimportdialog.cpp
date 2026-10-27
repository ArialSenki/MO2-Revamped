#include "categoryimportdialog.h"
#include "circularuihelpers.h"
#include "fluenticons.h"
#include "fluentpanels.h"
#include "ui_categoryimportdialog.h"

#include "organizercore.h"
#include <QCheckBox>
#include <QFrame>
#include <QLabel>
#include <QRadioButton>
#include <QVBoxLayout>

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

  // Fluent v4: one card of Settings rows, the list choice as radio rows and
  // the mappings as switches, instead of two group boxes side by side
  if (FluentIcons::active()) {
    ui->strategyBox->hide();
    ui->mappingBox->hide();
    auto* card = new QFrame(this);
    card->setProperty("fluentCard", true);
    auto* rows = new QVBoxLayout(card);
    rows->setContentsMargins(0, 10, 0, 0);
    rows->setSpacing(0);
    const auto heading = [card, rows](const QString& text, int top) {
      auto* label = new QLabel(text, card);
      label->setObjectName(QStringLiteral("fluentCardTitle"));
      label->setContentsMargins(18, top, 18, 8);
      rows->addWidget(label);
    };
    const auto radioRow = [card](QRadioButton* radio, char16_t glyph) {
      const QString title = radio->text();
      radio->setText(QString());
      radio->setAccessibleName(title);
      FluentPanels::addEditRow(card, glyph, title, radio->toolTip(), {radio});
    };
    heading(ui->strategyBox->title(), 0);
    radioRow(ui->mergeOption, 0xE710);
    radioRow(ui->replaceOption, 0xE72C);
    heading(ui->mappingBox->title(), 14);
    FluentPanels::addSwitchRow(card, 0xE71B, ui->assignOption->text(),
                               ui->assignOption->toolTip(), ui->assignOption);
    FluentPanels::addSwitchRow(card, 0xE8AB, ui->remapOption->text(),
                               ui->remapOption->toolTip(), ui->remapOption);
    ui->verticalLayout->insertWidget(ui->verticalLayout->indexOf(ui->buttonBox), card);
    resize(640, sizeHint().height());
  }
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
