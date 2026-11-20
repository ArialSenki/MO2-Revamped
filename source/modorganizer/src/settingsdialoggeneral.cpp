#include "settingsdialoggeneral.h"
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
#include <QAbstractItemView>
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
  // MO2 Revamped: Updates right after the language, then the lists, the
  // profile defaults and the dialogs card (the toolbar card is on Appearance,
  // the categories button on Mod list)
  if (auto* page = qobject_cast<QBoxLayout*>(ui->groupBox_6->parentWidget()->layout())) {
    const int languageAt = page->indexOf(ui->groupBox_61);
    if (languageAt >= 0 && page->indexOf(ui->groupBox_6) > languageAt + 1) {
      page->removeWidget(ui->groupBox_6);
      page->insertWidget(languageAt + 1, ui->groupBox_6);
    }
  }
  // Reset dialog choices as wide as its text, on the left, the last thing in
  // the card
  ui->resetDialogsButton->setSizePolicy(QSizePolicy::Maximum, QSizePolicy::Fixed);
  if (auto* box = qobject_cast<QBoxLayout*>(ui->groupBox_9->layout())) {
    const int at = box->indexOf(ui->resetDialogsButton);
    if (at >= 0) {
      box->removeWidget(ui->resetDialogsButton);
      auto* buttons = new QHBoxLayout;
      buttons->setContentsMargins(0, 0, 0, 0);
      buttons->addWidget(ui->resetDialogsButton);
      buttons->addStretch(1);
      box->insertLayout(at, buttons);
    }
  }

  // language
  ui->languageBox->setFixedWidth(260);
  ui->languageBox->setMaxVisibleItems(8);
  addLanguages();
  selectLanguage();

  // MO2 Revamped: the language as a row of its own
  if (auto* grid = qobject_cast<QGridLayout*>(ui->groupBox_61->layout())) {
    // the language as a row like the others, its box on the right; alone under
    // the heading it sat further left than every title below it
    // the card's heading took row 0 and moved the language down a row
    int languageAt = 0, column = 0, rowSpan = 1, columnSpan = 1;
    grid->getItemPosition(grid->indexOf(ui->languageBox), &languageAt, &column, &rowSpan,
                          &columnSpan);
    if (QLayoutItem* spacer = grid->itemAtPosition(languageAt, 2);
        spacer != nullptr && spacer->spacerItem() != nullptr) {
      grid->removeItem(spacer);
      delete spacer;
    }
    grid->removeWidget(ui->languageBox);
    auto* languageRow    = new QFrame(ui->groupBox_61);
    languageRow->setObjectName(QStringLiteral("fluentSettingRow"));
    auto* languageLayout = new QHBoxLayout(languageRow);
    languageLayout->setContentsMargins(4, 6, 4, 6);
    languageLayout->setSpacing(12);
    auto* languageLabel = new QLabel(QObject::tr("Interface language"), languageRow);
    languageLabel->setObjectName(QStringLiteral("fluentSettingTitle"));
    languageLabel->setToolTip(ui->languageBox->toolTip());
    languageLabel->setBuddy(ui->languageBox);
    languageLayout->addWidget(languageLabel);
    languageLayout->addStretch(1);
    ui->languageBox->setParent(languageRow);
    languageLayout->addWidget(ui->languageBox);
    grid->addWidget(languageRow, languageAt, 0, 1, qMax(1, grid->columnCount()));

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
  // the small notices at the bottom of the window, or only their sound; with
  // the other window and dialog behaviour
  m_soundOnly = new QCheckBox(QObject::tr("Notices: sound only, without the card"),
                              ui->groupBox_9);
  m_soundOnly->setObjectName(QStringLiteral("noticeSoundOnlyBox"));
  m_soundOnly->setChecked(GlobalSettings::noticeSoundOnly());
  m_soundOnly->setToolTip(QObject::tr(
      "Plays Windows' notification sound instead of showing the small notices at "
      "the bottom of the window (\"Installation successful\" and so on). They're "
      "still in MO2's log. Questions and errors still show."));
  // before the row of Reset dialog choices, the card's last item
  if (auto* box = qobject_cast<QBoxLayout*>(ui->groupBox_9->layout())) {
    box->insertWidget(std::max(0, box->count() - 1), m_soundOnly);
  }

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

  if (m_soundOnly != nullptr) {
    GlobalSettings::setNoticeSoundOnly(m_soundOnly->isChecked());
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
