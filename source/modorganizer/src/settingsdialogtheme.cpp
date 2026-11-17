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
#include "themeeditordialog.h"
#include "usertheme.h"
#include <QBoxLayout>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLayoutItem>
#include <QPushButton>
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
  addThemeButtons();

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
  ui->verticalLayout_20->setStretchFactor(ui->verticalLayout_18, 0);
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

  // themes of the user's own: any other .qss in the folder, on top of Light or
  // Dark (see docs/THEMES.md)
  const auto userThemes = UserTheme::list(styleDirectory.absolutePath());
  if (!userThemes.isEmpty()) {
    ui->styleBox->insertSeparator(ui->styleBox->count());
  }
  for (const auto& theme : userThemes) {
    if (!styleDirectory.exists(UserTheme::baseFileName(theme.dark))) {
      continue;
    }
    ui->styleBox->addItem(
        QObject::tr("%1 (on %2)")
            .arg(theme.name, theme.dark ? QObject::tr("Dark") : QObject::tr("Light")),
        theme.fileName);
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

QString ThemeSettingsTab::stylesheetsDir() const
{
  return QDir(QCoreApplication::applicationDirPath())
      .filePath(QString::fromStdWString(AppConfig::stylesheetsPath()));
}

void ThemeSettingsTab::addThemeButtons()
{
  auto* layout = qobject_cast<QBoxLayout*>(ui->themeStyleGroup->layout());
  if (!layout) {
    return;
  }

  // themes of your own: made and edited here, or by hand in the folder
  auto* row = new QHBoxLayout;
  row->setContentsMargins(0, 4, 0, 0);
  row->setSpacing(8);
  auto* newButton    = new QPushButton(QObject::tr("New theme..."), ui->themeStyleGroup);
  auto* editButton   = new QPushButton(QObject::tr("Edit theme..."), ui->themeStyleGroup);
  auto* folderButton = new QPushButton(QObject::tr("Open themes folder"), ui->themeStyleGroup);
  for (auto* button : {newButton, editButton, folderButton}) {
    button->setSizePolicy(QSizePolicy::Maximum, QSizePolicy::Fixed);
    row->addWidget(button);
  }
  row->addStretch(1);
  layout->addLayout(row);

  const auto selectedTheme = [this]() -> std::optional<UserTheme::Theme> {
    return UserTheme::load(stylesheetsDir(),
                           ui->styleBox->itemData(ui->styleBox->currentIndex()).toString());
  };
  const auto updateEdit = [editButton, selectedTheme] {
    editButton->setEnabled(selectedTheme().has_value());
  };
  updateEdit();
  QObject::connect(ui->styleBox, &QComboBox::currentIndexChanged, editButton, updateEdit);

  const auto openEditor = [this](const UserTheme::Theme* existing) {
    const QString inUse = settings().interface().styleName().value_or(QString());
    ThemeEditorDialog editor(parentWidget(), stylesheetsDir(), inUse, existing,
                             [this](const QString& file) {
                               emit settings().styleChanged(file);
                             });
    if (editor.exec() != QDialog::Accepted || editor.savedFileName().isEmpty()) {
      return;
    }
    // saved: the theme is in use now, and the list shows it
    const QString saved = editor.savedFileName();
    settings().interface().setStyleName(saved);
    ui->styleBox->blockSignals(true);
    ui->styleBox->clear();
    addStyles();
    const int index = ui->styleBox->findData(saved);
    if (index != -1) {
      ui->styleBox->setCurrentIndex(index);
    }
    ui->styleBox->blockSignals(false);
    ui->styleBox->setProperty("mo2Picked", false);
    emit ui->styleBox->currentIndexChanged(ui->styleBox->currentIndex());
  };

  QObject::connect(newButton, &QPushButton::clicked, newButton, [openEditor] {
    openEditor(nullptr);
  });
  QObject::connect(editButton, &QPushButton::clicked, editButton,
                   [openEditor, selectedTheme] {
                     if (const auto theme = selectedTheme()) {
                       openEditor(&*theme);
                     }
                   });
  QObject::connect(folderButton, &QPushButton::clicked, folderButton, [this] {
    shell::Explore(stylesheetsDir());
  });
}
