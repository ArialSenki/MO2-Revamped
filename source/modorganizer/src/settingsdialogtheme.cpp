#include "settingsdialogtheme.h"
#include "categoriesdialog.h"
#include "circularuihelpers.h"
#include "colortable.h"
#include "modlist.h"
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

  if (oldStyle != newStyle) {
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
  }
}

void ThemeSettingsTab::selectStyle()
{
  const int currentID =
      ui->styleBox->findData(settings().interface().styleName().value_or(""));

  if (currentID != -1) {
    ui->styleBox->setCurrentIndex(currentID);
  }
}

void ThemeSettingsTab::onExploreStyles()
{
  QString ssPath = QCoreApplication::applicationDirPath() + "/" +
                   ToQString(AppConfig::stylesheetsPath());
  shell::Explore(ssPath);
}
