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

// style box item data: whether the style sheet is a Fluent v4 theme
constexpr int FluentRole = Qt::UserRole + 1;

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

  QObject::connect(ui->resetColorsBtn, &QPushButton::clicked, [&] {
    ui->colorTable->resetColors();
  });

  QObject::connect(ui->exploreStyles, &QPushButton::clicked, [&] {
    onExploreStyles();
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
  const bool picked = ui->styleBox->currentIndex() !=
                      ui->styleBox->property("mo2InitialIndex").toInt();

  if (picked && oldStyle != newStyle) {
    settings().interface().setStyleName(newStyle);
    emit settings().styleChanged(newStyle);

    // the Fluent layout is built with the main window, so going between a
    // Fluent and a classic theme takes a restart to finish
    const bool fluent =
        ui->styleBox->itemData(ui->styleBox->currentIndex(), FluentRole).toBool();
    if (fluent != FluentIcons::active()) {
      dialog().setExitNeeded(dialog().exitNeeded() | Exit::Restart);
    }
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
                                     QStringLiteral("dark.qss"),
                                     QStringLiteral("dark-black.qss")};

  for (const QString& fileName : allowedStyles) {
    if (!styleDirectory.exists(fileName)) {
      continue;
    }

    // the Fluent themes install under the same file names; their dark theme
    // is neutral rather than blue
    QFile file(styleDirectory.filePath(fileName));
    const bool fluent = file.open(QIODevice::ReadOnly) &&
                        QString::fromUtf8(file.read(512)).contains(
                            QStringLiteral("MO2 Revamped Fluent v4"));
    const QString displayName =
        fileName == QStringLiteral("Light.qss") ? QObject::tr("Light")
        : fileName == QStringLiteral("dark-black.qss") ? QObject::tr("Dark Black")
        : fluent                                       ? QObject::tr("Dark")
                                                       : QObject::tr("Dark Blue");
    ui->styleBox->addItem(displayName, fileName);
    ui->styleBox->setItemData(ui->styleBox->count() - 1, fluent, FluentRole);
  }
}

void ThemeSettingsTab::selectStyle()
{
  const int currentID =
      ui->styleBox->findData(settings().interface().styleName().value_or(""));

  if (currentID != -1) {
    ui->styleBox->setCurrentIndex(currentID);
  }
  ui->styleBox->setProperty("mo2InitialIndex", ui->styleBox->currentIndex());
}

void ThemeSettingsTab::onExploreStyles()
{
  QString ssPath = QCoreApplication::applicationDirPath() + "/" +
                   ToQString(AppConfig::stylesheetsPath());
  shell::Explore(ssPath);
}
