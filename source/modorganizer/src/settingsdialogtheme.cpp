#include "settingsdialogtheme.h"
#include "quickui.h"
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
#include "fluentmotion.h"
#include "messagedialog.h"
#include <QAbstractItemView>
#include <QCheckBox>
#include <QComboBox>
#include <QDockWidget>
#include <QFrame>
#include <QGroupBox>
#include <QLabel>
#include <QMainWindow>
#include <QStatusBar>
#include <QToolBar>
#include <QVBoxLayout>
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

  // the size of everything, the animations and the toolbar, under the theme
  addDisplayCard();
  loadToolbarCard();

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
  ui->styleBox->setAccessibleName(QObject::tr("Interface theme"));
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
  const bool oldOled     = settings().interface().oledBackground();
  const QString choice =
      ui->styleBox->itemData(ui->styleBox->currentIndex()).toString();
  // OLED Black is stored as Dark with a setting of its own
  const bool newOled = choice == UserTheme::oledChoice();
  const QString newStyle = newOled ? UserTheme::baseFileName(true) : choice;

  // only a theme the user picked here: a stored style the list doesn't
  // know leaves the box on its first entry, which is not a choice
  const bool picked = ui->styleBox->property("mo2Picked").toBool();

  if (picked && (oldStyle != newStyle || oldOled != newOled)) {
    settings().interface().setStyleName(newStyle);
    settings().interface().setOledBackground(newOled);
    emit settings().styleChanged(newStyle);
  }

  // colors
  ui->colorTable->commitColors();

  updateDisplayAndToolbar();
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
    // true black for OLED screens: Dark in other colors, right under it
    if (fileName == UserTheme::baseFileName(true)) {
      ui->styleBox->addItem(QObject::tr("OLED Black"), UserTheme::oledChoice());
    }
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
  const QString style = settings().interface().styleName().value_or("");
  const int currentID = ui->styleBox->findData(
      style == UserTheme::baseFileName(true) && settings().interface().oledBackground()
          ? UserTheme::oledChoice()
          : style);

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
    settings().interface().setOledBackground(false);
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

void ThemeSettingsTab::addDisplayCard()
{
  // MO2 Revamped: how big and how smooth everything is, beside the theme
  // (these were on the General page)
  auto* page = ui->themeStyleGroup->parentWidget();
  auto* card = new QGroupBox(QObject::tr("Display"), page);
  card->setObjectName(QStringLiteral("themeDisplayGroup"));
  auto* box = new QVBoxLayout(card);
  box->setContentsMargins(9, 9, 9, 9);
  if (auto* pageLayout = qobject_cast<QBoxLayout*>(page->layout())) {
    pageLayout->insertWidget(pageLayout->indexOf(ui->themeStyleGroup) + 1, card);
  }

  {
    auto* label = new QLabel(QObject::tr("Interface size"), card);
    m_scaleBox  = new QComboBox(card);
    m_scaleBox->setObjectName(QStringLiteral("interfaceScaleBox"));
    m_scaleBox->setFixedWidth(260);
    for (int percent : GlobalSettings::interfaceScales()) {
      m_scaleBox->addItem(percent == 100 ? QObject::tr("%1% (normal)").arg(percent)
                                         : QObject::tr("%1%").arg(percent),
                          percent);
    }
    m_scaleBox->setCurrentIndex(m_scaleBox->findData(GlobalSettings::interfaceScale()));
    const QString tip = QObject::tr(
        "Makes all of Mod Organizer's text, boxes and windows smaller or larger "
        "together. It applies when Mod Organizer starts again.");
    m_scaleBox->setToolTip(tip);
    label->setToolTip(tip);
    label->setBuddy(m_scaleBox);
    auto* row       = new QFrame(card);
    // styled as the other settings rows ("Versions to be notified of")
    row->setObjectName(QStringLiteral("fluentSettingRow"));
    auto* rowLayout = new QHBoxLayout(row);
    // the margins of the other rows, so the title and the box line up with them
    rowLayout->setContentsMargins(4, 6, 4, 6);
    rowLayout->setSpacing(12);
    label->setObjectName(QStringLiteral("fluentSettingTitle"));
    rowLayout->addWidget(label);
    rowLayout->addStretch(1);
    rowLayout->addWidget(m_scaleBox);
    box->addWidget(row);

    // text in grey for OLED screens, where ClearType's colours fringe
    m_greyText = new QCheckBox(QObject::tr("Smooth text in grey (OLED screens)"),
                               card);
    m_greyText->setObjectName(QStringLiteral("greyscaleTextBox"));
    m_greyText->setChecked(GlobalSettings::greyscaleText());
    m_greyText->setToolTip(QObject::tr(
        "Draws text without Windows' coloured edges, which show as a coloured halo on "
        "OLED screens. It applies when Mod Organizer starts again."));
    box->addWidget(m_greyText);

    // MO2 Revamped: the Qt Quick versions of some windows (QuickUi)
    m_quickUi = new QCheckBox(
        QObject::tr("Use the new interface (Qt Quick) where available"), card);
    m_quickUi->setObjectName(QStringLiteral("quickInterfaceBox"));
    m_quickUi->setChecked(QuickUi::stored());
    m_quickUi->setToolTip(QObject::tr(
        "Draws the parts that have a new version with Qt Quick: the Instances "
        "window, the theme question of a new instance and the summary row of the "
        "main window. They work the same and use a little more memory. It "
        "applies when Mod Organizer starts again."));
    box->addWidget(m_quickUi);
  }

  // how smoothly things move
  {
    auto* label    = new QLabel(QObject::tr("Animations"), card);
    m_animationBox = new QComboBox(card);
    m_animationBox->setObjectName(QStringLiteral("animationLevelBox"));
    m_animationBox->setFixedWidth(260);
    // the theme pads the list, which Qt leaves out of the list's height: it would
    // scroll a whole row to show the last level and hide the first
    m_animationBox->view()->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    // the rate Monitor runs at: the main window's screen
    const int monitorHz = FluentMotion::monitorRate();
    m_animationBox->addItem(QObject::tr("Off"), FluentMotion::levelKey(FluentMotion::Level::Off));
    m_animationBox->addItem(QObject::tr("Saver (30 Hz)"),
                            FluentMotion::levelKey(FluentMotion::Level::Saver));
    m_animationBox->addItem(QObject::tr("Standard (60 Hz)"),
                            FluentMotion::levelKey(FluentMotion::Level::Standard));
    m_animationBox->addItem(monitorHz > 0 ? QObject::tr("Monitor (%1 Hz)").arg(monitorHz)
                                          : QObject::tr("Monitor"),
                            FluentMotion::levelKey(FluentMotion::Level::Monitor));
    const int current = m_animationBox->findData(FluentMotion::levelKey(
        FluentMotion::levelFromKey(settings().interface().animationLevel())));
    m_animationBox->setCurrentIndex(current >= 0 ? current : 2);
    const QString tip = QObject::tr(
        "How smoothly tabs, switches, menus, the log panel and the Run button move. "
        "Off shows every change at once. Monitor moves them at your screen's refresh "
        "rate (up to 240 Hz); it uses a little more CPU while something moves, and "
        "none while nothing does.");
    m_animationBox->setToolTip(tip);
    label->setToolTip(tip);
    label->setBuddy(m_animationBox);
    label->setObjectName(QStringLiteral("fluentSettingTitle"));
    auto* row = new QFrame(card);
    // styled as the other settings rows ("Interface size")
    row->setObjectName(QStringLiteral("fluentSettingRow"));
    auto* rowLayout = new QHBoxLayout(row);
    rowLayout->setContentsMargins(4, 6, 4, 6);
    rowLayout->setSpacing(12);
    rowLayout->addWidget(label);
    rowLayout->addStretch(1);
    rowLayout->addWidget(m_animationBox);
    box->addWidget(row);
  }

  CircularUi::addGroupHeading(card, ":/MO/gui/mainwindow/settings.svg",
                              QStringLiteral("themeStyleIcon"), 26,
                              QStringLiteral("themeSectionTitle"), QStringLiteral("D"));
}

void ThemeSettingsTab::loadToolbarCard()
{
  CircularUi::addGroupHeading(
      ui->toolbarViewGroup, ":/MO/gui/mainwindow/tools.svg",
      QStringLiteral("themeColorsIcon"), 26,
      QStringLiteral("themeSectionTitle"), QStringLiteral("T"));

  // The legacy View > Toolbars controls now live with the other interface
  // preferences. Read the live widgets so existing toolbar settings carry over.
  auto* mainWindow = qobject_cast<QMainWindow*>(dialog().parentWidget());
  auto* mainToolbar =
      mainWindow ? mainWindow->findChild<QToolBar*>(QStringLiteral("toolBar"))
                 : nullptr;
  // command bars use the Windows 11 sizes (see MainWindow)
  ui->toolbarIconSizeCombo->addItem(
      QObject::tr("Small"),
      QVariant::fromValue(QSize(16, 16)));
  ui->toolbarIconSizeCombo->addItem(
      QObject::tr("Medium"),
      QVariant::fromValue(QSize(20, 20)));
  ui->toolbarIconSizeCombo->addItem(
      QObject::tr("Large"),
      QVariant::fromValue(QSize(28, 28)));
  ui->toolbarIconSizeCombo->setMinimumWidth(170);
  ui->toolbarButtonStyleCombo->addItem(
      QObject::tr("Icons only"), static_cast<int>(Qt::ToolButtonIconOnly));
  ui->toolbarButtonStyleCombo->addItem(
      QObject::tr("Text only"), static_cast<int>(Qt::ToolButtonTextOnly));
  ui->toolbarButtonStyleCombo->addItem(
      QObject::tr("Icons and text"),
      static_cast<int>(Qt::ToolButtonTextBesideIcon));
  ui->toolbarButtonStyleCombo->setMinimumWidth(190);

  if (mainWindow) {
    if (mainToolbar) {
      int sizeIndex = ui->toolbarIconSizeCombo->findData(
          QVariant::fromValue(mainToolbar->iconSize()));
      if (sizeIndex < 0) {
        const QSize currentSize = mainToolbar->iconSize();
        ui->toolbarIconSizeCombo->addItem(
            QObject::tr("Current (%1 × %2)")
                .arg(currentSize.width())
                .arg(currentSize.height()),
            QVariant::fromValue(currentSize));
        sizeIndex = ui->toolbarIconSizeCombo->count() - 1;
      }
      ui->toolbarIconSizeCombo->setCurrentIndex(sizeIndex);
      int styleIndex = ui->toolbarButtonStyleCombo->findData(
          static_cast<int>(mainToolbar->toolButtonStyle()));
      if (styleIndex < 0) {
        ui->toolbarButtonStyleCombo->addItem(
            QObject::tr("Current custom style"),
            static_cast<int>(mainToolbar->toolButtonStyle()));
        styleIndex = ui->toolbarButtonStyleCombo->count() - 1;
      }
      ui->toolbarButtonStyleCombo->setCurrentIndex(styleIndex);
    } else {
      ui->toolbarIconSizeCombo->setEnabled(false);
      ui->toolbarButtonStyleCombo->setEnabled(false);
    }

    if (auto* statusBar = mainWindow->findChild<QStatusBar*>(
            QStringLiteral("statusBar"))) {
      ui->statusBarVisibleBox->setChecked(statusBar->isVisible());
    } else {
      ui->statusBarVisibleBox->setEnabled(false);
    }

    if (auto* logDock = mainWindow->findChild<QDockWidget*>(
            QStringLiteral("logDock"))) {
      ui->logPanelVisibleBox->setChecked(logDock->isVisible());
    } else {
      ui->logPanelVisibleBox->setEnabled(false);
    }
  } else {
    ui->toolbarViewGroup->setEnabled(false);
  }
  // update() applies only the boxes the user changes
  for (auto* box : {ui->statusBarVisibleBox, ui->logPanelVisibleBox}) {
    box->setProperty("mo2InitialChecked", box->isChecked());
  }
}

void ThemeSettingsTab::updateDisplayAndToolbar()
{
  // interface size: kept for the next start, which is when Qt applies it
  bool restartNeeded = false;
  if (m_scaleBox != nullptr) {
    const int scale = m_scaleBox->currentData().toInt();
    if (scale != GlobalSettings::interfaceScale()) {
      GlobalSettings::setInterfaceScale(scale);
      restartNeeded = true;
    }
  }
  if (m_greyText != nullptr && m_greyText->isChecked() != GlobalSettings::greyscaleText()) {
    GlobalSettings::setGreyscaleText(m_greyText->isChecked());
    restartNeeded = true;
  }
  if (m_quickUi != nullptr && m_quickUi->isChecked() != QuickUi::stored()) {
    QuickUi::setStored(m_quickUi->isChecked());
    restartNeeded = true;
  }
  if (restartNeeded) {
    MessageDialog::showMessage(
        QObject::tr("These changes apply when Mod Organizer starts again."),
        parentWidget(), true);
  }

  if (m_animationBox != nullptr) {
    const QString level = m_animationBox->currentData().toString();
    settings().interface().setAnimationLevel(level);
    FluentMotion::setLevel(FluentMotion::levelFromKey(level));
  }

  if (auto* mainWindow = qobject_cast<QMainWindow*>(dialog().parentWidget())) {
    const QVariant iconSizeData = ui->toolbarIconSizeCombo->currentData();
    const QVariant buttonStyleData = ui->toolbarButtonStyleCombo->currentData();
    if (iconSizeData.isValid() && buttonStyleData.isValid()) {
      const QSize iconSize = iconSizeData.toSize();
      const auto buttonStyle = static_cast<Qt::ToolButtonStyle>(
          buttonStyleData.toInt());
      // through MO2's main window, which also resets the buttons' fixed
      // sizes and the notification badge; plain QMainWindows take it as is
      if (!QMetaObject::invokeMethod(mainWindow, "setToolbarSize",
                                     Q_ARG(QSize, iconSize)) ||
          !QMetaObject::invokeMethod(mainWindow, "setToolbarButtonStyle",
                                     Q_ARG(Qt::ToolButtonStyle, buttonStyle))) {
        for (auto* toolbar : mainWindow->findChildren<QToolBar*>()) {
          toolbar->setIconSize(iconSize);
          toolbar->setToolButtonStyle(buttonStyle);
        }
      }
    }

    // only what the user changed here: a log panel tabbed behind another
    // dock reads as hidden and must not be closed by an unrelated OK
    auto changed = [](QCheckBox* box) {
      return box->isChecked() != box->property("mo2InitialChecked").toBool();
    };
    if (auto* statusBar = mainWindow->findChild<QStatusBar*>(
            QStringLiteral("statusBar"));
        statusBar && changed(ui->statusBarVisibleBox)) {
      statusBar->setVisible(ui->statusBarVisibleBox->isChecked());
      settings().geometry().saveVisibility(statusBar);
    }
    if (auto* logDock = mainWindow->findChild<QDockWidget*>(
            QStringLiteral("logDock"));
        logDock && changed(ui->logPanelVisibleBox)) {
      logDock->setVisible(ui->logPanelVisibleBox->isChecked());
    }

    // These are the same persisted geometry settings used by MO2's original
    // View menu, so existing user preferences remain in effect.
    settings().geometry().saveToolbars(mainWindow);
    settings().geometry().saveDocks(mainWindow);
  }
}
