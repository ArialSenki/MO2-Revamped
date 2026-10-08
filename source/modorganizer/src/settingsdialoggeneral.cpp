#include "settingsdialoggeneral.h"
#include "categoriesdialog.h"
#include "colortable.h"
#include "shared/appconfig.h"
#include "ui_settingsdialog.h"
#include <questionboxmemory.h>
#include <utility.h>
#include <algorithm>
#include <QDockWidget>
#include <QMainWindow>
#include <QStatusBar>
#include <QToolBar>

using namespace MOBase;

namespace
{
const QSize SmallToolbarIconSize(24, 24);
const QSize MediumToolbarIconSize(32, 32);
const QSize LargeToolbarIconSize(42, 36);
}  // namespace

GeneralSettingsTab::GeneralSettingsTab(Settings& s, SettingsDialog& d)
    : SettingsTab(s, d)
{
  // language
  ui->languageBox->setMinimumWidth(180);
  ui->languageBox->setMinimumHeight(28);
  addLanguages();
  selectLanguage();

  // download list
  ui->compactBox->setChecked(settings().interface().compactDownloads());
  ui->showMetaBox->setChecked(settings().interface().metaDownloads());
  ui->hideDownloadInstallBox->setChecked(
      settings().interface().hideDownloadsAfterInstallation());

  // updates
  ui->checkForUpdates->setChecked(settings().checkForUpdates());
  ui->usePrereleaseBox->setChecked(settings().usePrereleases());

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

  // The legacy View > Toolbars controls now live with the other interface
  // preferences. Read the live widgets so existing toolbar settings carry over.
  auto* mainWindow = qobject_cast<QMainWindow*>(d.parentWidget());
  auto* mainToolbar =
      mainWindow ? mainWindow->findChild<QToolBar*>(QStringLiteral("toolBar"))
                 : nullptr;
  ui->toolbarIconSizeCombo->addItem(QObject::tr("Small"),
                                    QVariant::fromValue(SmallToolbarIconSize));
  ui->toolbarIconSizeCombo->addItem(QObject::tr("Medium"),
                                    QVariant::fromValue(MediumToolbarIconSize));
  ui->toolbarIconSizeCombo->addItem(QObject::tr("Large"),
                                    QVariant::fromValue(LargeToolbarIconSize));
  ui->toolbarIconSizeCombo->setMinimumWidth(170);
  ui->toolbarButtonStyleCombo->addItem(
      QObject::tr("Icons only"), static_cast<int>(Qt::ToolButtonIconOnly));
  ui->toolbarButtonStyleCombo->addItem(
      QObject::tr("Text only"), static_cast<int>(Qt::ToolButtonTextOnly));
  ui->toolbarButtonStyleCombo->addItem(
      QObject::tr("Icons and text"),
      static_cast<int>(Qt::ToolButtonTextUnderIcon));
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
      ui->mainToolbarVisibleBox->setChecked(mainToolbar->isVisible());
    } else {
      ui->toolbarIconSizeCombo->setEnabled(false);
      ui->toolbarButtonStyleCombo->setEnabled(false);
      ui->mainToolbarVisibleBox->setEnabled(false);
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

  if (newLanguage != oldLanguage) {
    settings().interface().setLanguage(newLanguage);
    emit settings().languageChanged(newLanguage);
  }

  // download list
  settings().interface().setCompactDownloads(ui->compactBox->isChecked());
  settings().interface().setMetaDownloads(ui->showMetaBox->isChecked());
  settings().interface().setHideDownloadsAfterInstallation(
      ui->hideDownloadInstallBox->isChecked());

  // updates
  settings().setCheckForUpdates(ui->checkForUpdates->isChecked());
  settings().setUsePrereleases(ui->usePrereleaseBox->isChecked());

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

  if (auto* mainWindow = qobject_cast<QMainWindow*>(dialog().parentWidget())) {
    const QVariant iconSizeData = ui->toolbarIconSizeCombo->currentData();
    const QVariant buttonStyleData = ui->toolbarButtonStyleCombo->currentData();
    if (iconSizeData.isValid() && buttonStyleData.isValid()) {
      const QSize iconSize = iconSizeData.toSize();
      const auto buttonStyle = static_cast<Qt::ToolButtonStyle>(
          buttonStyleData.toInt());
      for (auto* toolbar : mainWindow->findChildren<QToolBar*>()) {
        toolbar->setIconSize(iconSize);
        toolbar->setToolButtonStyle(buttonStyle);
      }
    }

    if (auto* mainToolbar = mainWindow->findChild<QToolBar*>(
            QStringLiteral("toolBar"))) {
      mainToolbar->setVisible(ui->mainToolbarVisibleBox->isChecked());
    }
    if (auto* statusBar = mainWindow->findChild<QStatusBar*>(
            QStringLiteral("statusBar"))) {
      statusBar->setVisible(ui->statusBarVisibleBox->isChecked());
      settings().geometry().saveVisibility(statusBar);
    }
    if (auto* logDock = mainWindow->findChild<QDockWidget*>(
            QStringLiteral("logDock"))) {
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

    QString languageString = QString("%1 (%2)")
                                 .arg(locale.nativeLanguageName())
                                 .arg(locale.nativeCountryName());

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
  const auto r = QMessageBox::question(
      parentWidget(), QObject::tr("Confirm?"),
      QObject::tr(
          "This will reset all the choices you made to dialogs and make them all "
          "visible again. Continue?"),
      QMessageBox::Yes | QMessageBox::No);

  if (r == QMessageBox::Yes) {
    resetDialogs();
  }
}
