#include "settingsdialoggeneral.h"
#include "categoriesdialog.h"
#include "fluenticons.h"
#include "circularuihelpers.h"
#include "colortable.h"
#include "fluentmotion.h"
#include "shared/appconfig.h"
#include "ui_settingsdialog.h"
#include <questionboxmemory.h>
#include <utility.h>
#include <algorithm>
#include <QDockWidget>
#include <QIcon>
#include <QHBoxLayout>
#include <QLabel>
#include <QCheckBox>
#include <QFrame>
#include <QComboBox>
#include <QGridLayout>
#include "messagedialog.h"
#include <QLayout>
#include <QMainWindow>
#include <QMessageBox>
#include <QStatusBar>
#include <QToolBar>
#include <QVBoxLayout>

using namespace MOBase;

namespace
{
}  // namespace

GeneralSettingsTab::GeneralSettingsTab(Settings& s, SettingsDialog& d)
    : SettingsTab(s, d)
{
  CircularUi::addIconToHeading(
      ui->generalPageTitle, ":/MO/gui/mainwindow/settings.svg",
      QStringLiteral("generalPageIcon"), 36, QStringLiteral("G"));
  CircularUi::addGroupHeading(
      ui->groupBox_61, ":/MO/gui/mainwindow/settings.svg",
      QStringLiteral("generalLanguageIcon"), 26,
      QStringLiteral("generalSectionTitle"), QStringLiteral("L"));
  CircularUi::addGroupHeading(
      ui->groupBox_5, ":/MO/gui/mainwindow/files/archive.svg",
      QStringLiteral("generalDownloadsIcon"), 26,
      QStringLiteral("generalSectionTitle"), QStringLiteral("D"));
  CircularUi::addGroupHeading(
      ui->groupBox_6, ":/MO/gui/mainwindow/github-updates.svg",
      QStringLiteral("generalUpdatesIcon"), 26,
      QStringLiteral("generalSectionTitle"), QStringLiteral("U"));
  CircularUi::addGroupHeading(
      ui->groupBox_8, ":/MO/gui/mainwindow/profile.svg",
      QStringLiteral("generalProfileDefaultsIcon"), 26,
      QStringLiteral("generalSectionTitle"), QStringLiteral("P"));
  CircularUi::addGroupHeading(
      ui->groupBox_9, ":/MO/gui/mainwindow/settings.svg",
      QStringLiteral("generalMiscIcon"), 26,
      QStringLiteral("generalSectionTitle"), QStringLiteral("M"));
  CircularUi::addGroupHeading(
      ui->toolbarViewGroup, ":/MO/gui/mainwindow/tools.svg",
      QStringLiteral("generalToolbarIcon"), 26,
      QStringLiteral("generalSectionTitle"), QStringLiteral("T"));

  // Keep the two General-page actions in a card that matches the settings
  // sections above, while retaining the same buttons and click behavior.
  ui->widget_10->setObjectName(QStringLiteral("generalActionsCard"));
  ui->widget_10->setTitle(QObject::tr("Categories and dialog choices"));
  if (QLayout* oldLayout = ui->widget_10->layout()) {
    while (QLayoutItem* item = oldLayout->takeAt(0)) {
      delete item;
    }
    delete oldLayout;
  }
  auto* actionsLayout = new QVBoxLayout(ui->widget_10);
  auto* actionButtonsLayout = new QHBoxLayout;
  actionButtonsLayout->setContentsMargins(0, 0, 0, 0);
  actionButtonsLayout->setSpacing(8);
  actionButtonsLayout->addWidget(ui->resetDialogsButton);
  actionButtonsLayout->addWidget(ui->categoriesBtn);
  actionButtonsLayout->addStretch(1);
  actionsLayout->addLayout(actionButtonsLayout);
  // a card as the ones above it: same heading, margins and background
  CircularUi::addGroupHeading(
      ui->widget_10, ":/MO/gui/mainwindow/tools.svg",
      QStringLiteral("generalActionsIcon"), 26,
      QStringLiteral("generalSectionTitle"), QStringLiteral("A"));

  // language
  ui->languageBox->setFixedWidth(260);
  ui->languageBox->setMaxVisibleItems(8);
  addLanguages();
  selectLanguage();

  // MO2 Revamped: the size of the whole interface, beside the language
  if (auto* grid = qobject_cast<QGridLayout*>(ui->groupBox_61->layout())) {
    auto* label = new QLabel(QObject::tr("Interface size"), ui->groupBox_61);
    m_scaleBox  = new QComboBox(ui->groupBox_61);
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
    // a row of its own below the language, as "Versions to be notified of": the
    // card's heading already moved the grid's first rows
    auto* row       = new QFrame(ui->groupBox_61);
    // styled as the other settings rows ("Versions to be notified of")
    row->setObjectName(QStringLiteral("fluentSettingRow"));
    auto* rowLayout = new QHBoxLayout(row);
    rowLayout->setContentsMargins(0, 0, 0, 0);
    rowLayout->setSpacing(12);
    rowLayout->addWidget(label);
    rowLayout->addStretch(1);
    rowLayout->addWidget(m_scaleBox);
    grid->addWidget(row, grid->rowCount(), 0, 1, qMax(1, grid->columnCount()));

    // text in grey for OLED screens, where ClearType's colours fringe
    m_greyText = new QCheckBox(QObject::tr("Smooth text in grey (OLED screens)"),
                               ui->groupBox_61);
    m_greyText->setObjectName(QStringLiteral("greyscaleTextBox"));
    m_greyText->setChecked(GlobalSettings::greyscaleText());
    m_greyText->setToolTip(QObject::tr(
        "Draws text without Windows' coloured edges, which show as a coloured halo on "
        "OLED screens. It applies when Mod Organizer starts again."));
    grid->addWidget(m_greyText, grid->rowCount(), 0, 1, qMax(1, grid->columnCount()));

    // the small notices at the bottom of the window, or only their sound
    m_soundOnly = new QCheckBox(QObject::tr("Notices: sound only, without the card"),
                                ui->groupBox_61);
    m_soundOnly->setObjectName(QStringLiteral("noticeSoundOnlyBox"));
    m_soundOnly->setChecked(GlobalSettings::noticeSoundOnly());
    m_soundOnly->setToolTip(QObject::tr(
        "Plays Windows' notification sound instead of showing the small notices at "
        "the bottom of the window (\"Installation successful\" and so on). They're "
        "still in MO2's log. Questions and errors still show."));
    grid->addWidget(m_soundOnly, grid->rowCount(), 0, 1, qMax(1, grid->columnCount()));
  }

  // download list
  ui->compactBox->setChecked(settings().interface().compactDownloads());
  ui->showMetaBox->setChecked(settings().interface().metaDownloads());
  ui->hideDownloadInstallBox->setChecked(
      settings().interface().hideDownloadsAfterInstallation());

  // updates
  ui->checkForUpdates->setChecked(settings().checkForUpdates());
  // the channels, in the order of SelfUpdater::UpdateChannel
  ui->updateChannelBox->clear();
  ui->updateChannelBox->addItem(QObject::tr("Stable releases"));
  ui->updateChannelBox->addItem(QObject::tr("Stable and beta"));
  ui->updateChannelBox->addItem(QObject::tr("Stable, beta and development builds"));
  ui->updateChannelBox->setCurrentIndex(static_cast<int>(settings().updateChannel()));
  // the channel only matters while updates are looked for
  const auto followCheck = [this](bool on) {
    ui->updateChannelBox->setEnabled(on);
    ui->updateChannelLabel->setEnabled(on);
  };
  followCheck(ui->checkForUpdates->isChecked());
  QObject::connect(ui->checkForUpdates, &QCheckBox::toggled, ui->updateChannelBox,
                   followCheck);


  // profile defaults
  ui->localINIs->setChecked(settings().profileLocalInis());
  ui->localSaves->setChecked(settings().profileLocalSaves());
  ui->automaticArchiveInvalidation->setChecked(settings().profileArchiveInvalidation());

  // miscellaneous
  ui->centerDialogs->setChecked(settings().geometry().centerDialogs());
  ui->changeGameConfirmation->setChecked(
      settings().interface().showChangeGameConfirmation());
  ui->doubleClickPreviews->setChecked(
      settings().interface().doubleClicksOpenPreviews());
  ui->animationsBox->setChecked(settings().interface().animationsEnabled());

  // The legacy View > Toolbars controls now live with the other interface
  // preferences. Read the live widgets so existing toolbar settings carry over.
  auto* mainWindow = qobject_cast<QMainWindow*>(d.parentWidget());
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

  QObject::connect(ui->categoriesBtn, &QPushButton::clicked, [&] {
    onEditCategories();
  });

  QObject::connect(ui->resetDialogsButton, &QPushButton::clicked, [&] {
    onResetDialogs();
  });
}

void GeneralSettingsTab::update()
{
  // language
  const QString oldLanguage = settings().interface().language();
  const QString newLanguage =
      ui->languageBox->itemData(ui->languageBox->currentIndex()).toString();

  // the entry the list fell back to when it opened isn't a change: en_US shows
  // as en, which otherwise asked for a restart on the first OK
  int shownAtOpen = ui->languageBox->findData(oldLanguage);
  if (shownAtOpen == -1) {
    shownAtOpen = ui->languageBox->findData(oldLanguage.mid(0, 2));
  }
  if (newLanguage != oldLanguage && ui->languageBox->currentIndex() != shownAtOpen) {
    settings().interface().setLanguage(newLanguage);
    emit settings().languageChanged(newLanguage);
  }

  // interface size: kept for the next start, which is when Qt applies it
  bool restartNeeded = false;
  if (m_scaleBox != nullptr) {
    const int scale = m_scaleBox->currentData().toInt();
    if (scale != GlobalSettings::interfaceScale()) {
      GlobalSettings::setInterfaceScale(scale);
      restartNeeded = true;
    }
  }
  if (m_soundOnly != nullptr) {
    GlobalSettings::setNoticeSoundOnly(m_soundOnly->isChecked());
  }
  if (m_greyText != nullptr && m_greyText->isChecked() != GlobalSettings::greyscaleText()) {
    GlobalSettings::setGreyscaleText(m_greyText->isChecked());
    restartNeeded = true;
  }
  if (restartNeeded) {
    MessageDialog::showMessage(
        QObject::tr("These changes apply when Mod Organizer starts again."),
        parentWidget(), true);
  }

  // download list
  settings().interface().setCompactDownloads(ui->compactBox->isChecked());
  settings().interface().setMetaDownloads(ui->showMetaBox->isChecked());
  settings().interface().setHideDownloadsAfterInstallation(
      ui->hideDownloadInstallBox->isChecked());

  // updates
  settings().setCheckForUpdates(ui->checkForUpdates->isChecked());
  // stored only when changed: an unchanged channel keeps following the build
  const auto channel = static_cast<UpdateChannel>(ui->updateChannelBox->currentIndex());
  if (channel != settings().updateChannel()) {
    settings().setUpdateChannel(channel);
  }

  // profile defaults
  settings().setProfileLocalInis(ui->localINIs->isChecked());
  settings().setProfileLocalSaves(ui->localSaves->isChecked());
  settings().setProfileArchiveInvalidation(
      ui->automaticArchiveInvalidation->isChecked());

  // miscellaneous
  settings().geometry().setCenterDialogs(ui->centerDialogs->isChecked());
  settings().interface().setShowChangeGameConfirmation(
      ui->changeGameConfirmation->isChecked());
  settings().interface().setDoubleClicksOpenPreviews(
      ui->doubleClickPreviews->isChecked());
  settings().interface().setAnimationsEnabled(ui->animationsBox->isChecked());
  FluentMotion::setEnabled(ui->animationsBox->isChecked());

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

void GeneralSettingsTab::addLanguages()
{
  // matches the end of filenames for something like "_en.qm" or "_zh_CN.qm"
  const QString pattern = QString::fromStdWString(AppConfig::translationPrefix()) +
                          "_([a-z]{2,3}(_[A-Z]{2,2})?).qm";

  const QRegularExpression exp(QRegularExpression::anchoredPattern(pattern));

  QDirIterator iter(QCoreApplication::applicationDirPath() + "/translations",
                    QDir::Files);

  std::vector<std::pair<QString, QString>> languages;

  while (iter.hasNext()) {
    iter.next();

    const QString file = iter.fileName();
    auto match         = exp.match(file);
    if (!match.hasMatch()) {
      continue;
    }

    const QString languageCode = match.captured(1);
    const QLocale locale(languageCode);

    // Some native names already include the territory ("español de España"),
    // so only append it when it adds information.
    QString languageString = locale.nativeLanguageName();
    const QString territory = locale.nativeTerritoryName();
    if (!territory.isEmpty() &&
        !languageString.contains(territory, Qt::CaseInsensitive)) {
      languageString += QString(" (%1)").arg(territory);
    }

    if (locale.language() == QLocale::Chinese) {
      if (languageCode == "zh_TW") {
        languageString = "Chinese (Traditional)";
      } else {
        languageString = "Chinese (Simplified)";
      }
    }

    languages.push_back({languageString, match.captured(1)});
  }

  const bool hasEnglish = std::any_of(
      languages.begin(), languages.end(), [](const auto& language) {
        return language.second.compare("en_US", Qt::CaseInsensitive) == 0 ||
               language.second.compare("en", Qt::CaseInsensitive) == 0;
      });
  if (!hasEnglish) {
    languages.emplace_back(QString("English"), QString("en_US"));
  }

  std::sort(languages.begin(), languages.end());

  for (const auto& lang : languages) {
    ui->languageBox->addItem(lang.first, lang.second);
  }
}

void GeneralSettingsTab::selectLanguage()
{
  QString languageCode = settings().interface().language();
  int currentID        = ui->languageBox->findData(languageCode);
  // I made a mess. :( Most languages are stored with only the iso country
  // code (2 characters like "de") but chinese
  // with the exact language variant (zh_TW) so I have to search for both
  // variants
  if (currentID == -1) {
    currentID = ui->languageBox->findData(languageCode.mid(0, 2));
  }
  if (currentID == -1) {
    currentID = ui->languageBox->findData("en_US");
  }
  if (currentID == -1 && ui->languageBox->count() > 0) {
    currentID = 0;
  }
  if (currentID != -1) {
    ui->languageBox->setCurrentIndex(currentID);
  }
}

void GeneralSettingsTab::resetDialogs()
{
  settings().widgets().resetQuestionButtons();
  GlobalSettings::resetDialogs();
}

void GeneralSettingsTab::onEditCategories()
{
  CategoriesDialog catDialog(&dialog());

  if (catDialog.exec() == QDialog::Accepted) {
    catDialog.commitChanges();
  }
}

void GeneralSettingsTab::onResetDialogs()
{
  QMessageBox prompt(parentWidget());
  prompt.setObjectName(QStringLiteral("GeneralResetDialogsConfirmDialog"));
  prompt.setWindowTitle(QObject::tr("Confirm?"));
  prompt.setText(QObject::tr(
      "This will reset all the choices you made to dialogs and make them all "
      "visible again. Continue?"));
  prompt.setStandardButtons(QMessageBox::Yes | QMessageBox::No);
  const auto r = prompt.exec();

  if (r == QMessageBox::Yes) {
    resetDialogs();
  }
}
