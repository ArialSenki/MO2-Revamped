#include "settingsdialogmodlist.h"
#include "categoriesdialog.h"
#include "circularuihelpers.h"
#include "colortable.h"
#include "modlist.h"
#include "shared/appconfig.h"
#include "ui_settingsdialog.h"
#include <questionboxmemory.h>
#include <utility.h>

using namespace MOBase;

ModListSettingsTab::ModListSettingsTab(Settings& s, SettingsDialog& d)
    : SettingsTab(s, d),
      m_columnToBox{
          {ModList::COL_CONFLICTFLAGS, ui->collapsibleSeparatorsIconsConflictsBox},
          {ModList::COL_FLAGS, ui->collapsibleSeparatorsIconsFlagsBox},
          {ModList::COL_CONTENT, ui->collapsibleSeparatorsIconsContentsBox},
          {ModList::COL_VERSION, ui->collapsibleSeparatorsIconsVersionBox}}
{
  CircularUi::addIconToHeading(
      ui->modListPageTitle, ":/MO/gui/mainwindow/settings.svg",
      QStringLiteral("modListPageIcon"), 36, QStringLiteral("M"));
  CircularUi::addGroupHeading(
      ui->modListDisplayGroup, ":/MO/gui/mainwindow/filter.svg",
      QStringLiteral("modListDisplayIcon"), 26,
      QStringLiteral("modListDisplaySectionTitle"), QStringLiteral("F"));
  CircularUi::addGroupHeading(
      ui->modListWorkflowGroup, ":/MO/gui/mainwindow/install.svg",
      QStringLiteral("modListWorkflowIcon"), 26,
      QStringLiteral("modListWorkflowSectionTitle"), QStringLiteral("I"));
  CircularUi::addGroupHeading(
      ui->collapsibleSeparatorsWidget, ":/MO/gui/contextmenu/collapse.svg",
      QStringLiteral("collapsibleSeparatorsSectionIcon"), 26,
      QStringLiteral("collapsibleSeparatorsSectionTitle"),
      QStringLiteral("S"));

  // connect before setting to trigger
  QObject::connect(ui->collapsibleSeparatorsAscBox, &QCheckBox::toggled, [=] {
    updateCollapsibleSeparatorsGroup();
  });
  QObject::connect(ui->collapsibleSeparatorsDscBox, &QCheckBox::toggled, [=] {
    updateCollapsibleSeparatorsGroup();
  });

  ui->colorSeparatorsBox->setChecked(settings().colors().colorSeparatorScrollbar());
  ui->displayForeignBox->setChecked(settings().interface().displayForeign());
  ui->collapsibleSeparatorsAscBox->setChecked(
      settings().interface().collapsibleSeparators(Qt::AscendingOrder));
  ui->collapsibleSeparatorsDscBox->setChecked(
      settings().interface().collapsibleSeparators(Qt::DescendingOrder));
  ui->collapsibleSeparatorsHighlightFromBox->setChecked(
      settings().interface().collapsibleSeparatorsHighlightFrom());
  ui->collapsibleSeparatorsHighlightToBox->setChecked(
      settings().interface().collapsibleSeparatorsHighlightTo());
  ui->collapsibleSeparatorsPerProfileBox->setChecked(
      settings().interface().collapsibleSeparatorsPerProfile());
  ui->saveFiltersBox->setChecked(settings().interface().saveFilters());
  ui->autoCollapseDelayBox->setChecked(settings().interface().autoCollapseOnHover());
  ui->checkUpdateInstallBox->setChecked(
      settings().interface().checkUpdateAfterInstallation());

  for (auto& p : m_columnToBox) {
    p.second->setChecked(settings().interface().collapsibleSeparatorsIcons(p.first));
  }
}

void ModListSettingsTab::update()
{
  // mod list
  settings().colors().setColorSeparatorScrollbar(ui->colorSeparatorsBox->isChecked());
  settings().interface().setDisplayForeign(ui->displayForeignBox->isChecked());
  settings().interface().setCollapsibleSeparators(
      ui->collapsibleSeparatorsAscBox->isChecked(),
      ui->collapsibleSeparatorsDscBox->isChecked());
  settings().interface().setCollapsibleSeparatorsHighlightFrom(
      ui->collapsibleSeparatorsHighlightFromBox->isChecked());
  settings().interface().setCollapsibleSeparatorsHighlightTo(
      ui->collapsibleSeparatorsHighlightToBox->isChecked());
  settings().interface().setCollapsibleSeparatorsPerProfile(
      ui->collapsibleSeparatorsPerProfileBox->isChecked());
  settings().interface().setSaveFilters(ui->saveFiltersBox->isChecked());
  settings().interface().setAutoCollapseOnHover(ui->autoCollapseDelayBox->isChecked());
  settings().interface().setCheckUpdateAfterInstallation(
      ui->checkUpdateInstallBox->isChecked());

  for (auto& p : m_columnToBox) {
    settings().interface().setCollapsibleSeparatorsIcons(p.first,
                                                         p.second->isChecked());
  }
}

void ModListSettingsTab::updateCollapsibleSeparatorsGroup()
{
  const auto checked = ui->collapsibleSeparatorsAscBox->isChecked() ||
                       ui->collapsibleSeparatorsDscBox->isChecked();
  for (auto* widget : ui->collapsibleSeparatorsWidget->findChildren<QWidget*>()) {
    widget->setEnabled(checked);
  }
  if (auto* sectionTitle = ui->collapsibleSeparatorsWidget->findChild<QWidget*>(
          QStringLiteral("collapsibleSeparatorsSectionTitle"))) {
    sectionTitle->setEnabled(true);
  }
  if (auto* sectionIcon = ui->collapsibleSeparatorsWidget->findChild<QWidget*>(
          QStringLiteral("collapsibleSeparatorsSectionIcon"))) {
    sectionIcon->setEnabled(true);
  }
  ui->collapsibleSeparatorsLabel->setEnabled(true);
  ui->collapsibleSeparatorsAscBox->setEnabled(true);
  ui->collapsibleSeparatorsDscBox->setEnabled(true);
}
