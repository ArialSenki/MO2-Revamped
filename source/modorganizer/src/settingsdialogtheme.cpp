#include "settingsdialogtheme.h"
#include "categoriesdialog.h"
#include "circularuihelpers.h"
#include "colortable.h"
#include "fluenticons.h"
#include "game_features.h"
#include "modlist.h"
#include "plugincontainer.h"
#include "settingsdialog.h"
#include <dataarchives.h>
#include <gameplugins.h>
#include "shared/appconfig.h"
#include "ui_settingsdialog.h"
#include <QBoxLayout>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLayoutItem>
#include <QStringList>
#include <questionboxmemory.h>
#include <utility.h>

using namespace MOBase;

namespace
{

void stackThemeStyleControls(QGroupBox* group)
{
  auto* layout = group ? qobject_cast<QBoxLayout*>(group->layout()) : nullptr;
  if (!layout || layout->direction() != QBoxLayout::LeftToRight) {
    return;
  }

  auto* controlsRow = new QHBoxLayout;
  controlsRow->setContentsMargins(0, 0, 0, 0);
  controlsRow->setSpacing(layout->spacing());
  while (QLayoutItem* item = layout->takeAt(0)) {
    controlsRow->addItem(item);
  }

  layout->setDirection(QBoxLayout::TopToBottom);
  layout->addLayout(controlsRow);
}

}  // namespace

ThemeSettingsTab::ThemeSettingsTab(Settings& s, SettingsDialog& d) : SettingsTab(s, d)
{
  CircularUi::addIconToHeading(
      ui->themePageTitle, ":/MO/gui/mainwindow/settings.svg",
      QStringLiteral("themePageIcon"), 36, QStringLiteral("T"));
  stackThemeStyleControls(ui->themeStyleGroup);
  CircularUi::addGroupHeading(
      ui->themeStyleGroup, ":/MO/gui/mainwindow/settings.svg",
      QStringLiteral("themeStyleIcon"), 26,
      QStringLiteral("themeSectionTitle"), QStringLiteral("I"));
  CircularUi::addGroupHeading(
      ui->themeColorsGroup, ":/MO/gui/mainwindow/tools.svg",
      QStringLiteral("themeColorsIcon"), 26,
      QStringLiteral("themeSectionTitle"), QStringLiteral("C"));

  // style
  addStyles();
  selectStyle();

  // colors
  ui->colorTable->load(s);

  // the archive conflict and plugin rows only matter for games with archives
  // or plugins (the Bethesda games); Elden Ring has neither
  if (PluginContainer* pc = dialog().pluginContainer();
      pc != nullptr && s.game().plugin() != nullptr) {
    auto& features      = pc->gameFeatures();
    const bool archives = features.gameFeature<MOBase::DataArchives>() != nullptr;
    const bool plugins  = features.gameFeature<MOBase::GamePlugins>() != nullptr;
    ui->colorTable->setRowHidden(2, !archives);
    ui->colorTable->setRowHidden(3, !archives);
    ui->colorTable->setRowHidden(4, !plugins);
    ui->colorTable->setRowHidden(5, !plugins);
  }
  ui->colorTable->updateGeometry();

  // the width of the other pickers, and the Restore button as wide as its
  // text, as the other pages' action buttons
  ui->styleBox->setFixedWidth(260);
  ui->resetColorsBtn->setSizePolicy(QSizePolicy::Maximum, QSizePolicy::Fixed);
  ui->horizontalLayout_3->addStretch(1);
  // the cards stay at the top now that the table fits its rows
  ui->verticalLayout_20->addStretch(1);

  QObject::connect(ui->resetColorsBtn, &QPushButton::clicked, [&] {
    ui->colorTable->resetColors();
  });

}

void ThemeSettingsTab::update()
{
  // style
  const QString oldStyle = settings().interface().styleName().value_or("");
  const QString newStyle =
      ui->styleBox->itemData(ui->styleBox->currentIndex()).toString();

  // only a theme the user picked here: a stored style the list doesn't
  // know leaves the box on its first entry, which is not a choice
  const bool picked = ui->styleBox->property("mo2Picked").toBool();

  if (picked && oldStyle != newStyle) {
    settings().interface().setStyleName(newStyle);
    emit settings().styleChanged(newStyle);
  }

  // colors
  ui->colorTable->commitColors();
}

void ThemeSettingsTab::addStyles()
{
  const QDir styleDirectory(
      QCoreApplication::applicationDirPath() + "/" +
      QString::fromStdWString(AppConfig::stylesheetsPath()));
  const QStringList allowedStyles = {QStringLiteral("Light.qss"),
                                     QStringLiteral("dark.qss")};

  for (const QString& fileName : allowedStyles) {
    if (!styleDirectory.exists(fileName)) {
      continue;
    }

    const QString displayName = fileName == QStringLiteral("Light.qss")
                                    ? QObject::tr("Light")
                                    : QObject::tr("Dark");
    ui->styleBox->addItem(displayName, fileName);
  }
}

void ThemeSettingsTab::selectStyle()
{
  const int currentID =
      ui->styleBox->findData(settings().interface().styleName().value_or(""));

  if (currentID != -1) {
    ui->styleBox->setCurrentIndex(currentID);
  }
  // set once the user chooses an entry, even the one already shown
  QObject::connect(ui->styleBox, &QComboBox::activated, ui->styleBox,
                   [box = ui->styleBox] {
                     box->setProperty("mo2Picked", true);
                   });
}
