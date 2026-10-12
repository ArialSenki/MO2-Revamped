#include "settingsdialogworkarounds.h"
#include "circularuihelpers.h"
#include "settings.h"
#include "spawn.h"
#include "ui_settingsdialog.h"
#include <iplugingame.h>
#include <log.h>
#include <report.h>

#include <QCheckBox>
#include <QFileDialog>
#include <QGridLayout>
#include <QGroupBox>
#include <QSizePolicy>
#include <QVBoxLayout>

WorkaroundsSettingsTab::WorkaroundsSettingsTab(Settings& s, SettingsDialog& d)
    : SettingsTab(s, d)
{
  MOBase::CircularUi::addIconToHeading(
      ui->workaroundPageTitle, ":/MO/gui/mainwindow/tools.svg",
      QStringLiteral("workaroundPageIcon"), 36, QStringLiteral("W"));
  MOBase::CircularUi::addGroupHeading(
      ui->workaroundsOptionsBox, ":/MO/gui/mainwindow/settings.svg",
      QStringLiteral("workaroundBehaviorIcon"), 26,
      QStringLiteral("workaroundBehaviorTitle"), QStringLiteral("B"));
  MOBase::CircularUi::addGroupHeading(
      // run.svg is white (it sits on the blue Run button); use a visible icon.
      ui->steamSettingsGroup, ":/MO/gui/mainwindow/files/executable.svg",
      QStringLiteral("workaroundSteamIcon"), 26,
      QStringLiteral("workaroundSteamTitle"), QStringLiteral("S"));
  MOBase::CircularUi::addGroupHeading(
      ui->networkSettingsGroup, ":/MO/gui/mainwindow/mod-page.svg",
      QStringLiteral("workaroundNetworkIcon"), 26,
      QStringLiteral("workaroundNetworkTitle"), QStringLiteral("N"));
  MOBase::CircularUi::addGroupHeading(
      ui->maintenanceActionsGroup, ":/MO/gui/mainwindow/restore.svg",
      QStringLiteral("workaroundMaintenanceIcon"), 26,
      QStringLiteral("workaroundMaintenanceTitle"), QStringLiteral("M"));
  MOBase::CircularUi::addGroupHeading(
      ui->virtualFilesystemGroup, ":/MO/gui/mainwindow/filter.svg",
      QStringLiteral("workaroundVfsIcon"), 26,
      QStringLiteral("workaroundVfsTitle"), QStringLiteral("V"));

  ui->verticalLayout_5->setContentsMargins(8, 6, 8, 8);
  ui->verticalLayout_5->setSpacing(8);
  ui->workaroundsLayout->setContentsMargins(0, 0, 0, 0);
  ui->workaroundsLayout->setHorizontalSpacing(12);
  ui->workaroundsLayout->setVerticalSpacing(12);
  ui->workaroundsLayout->setAlignment(Qt::AlignTop);

  // Use independent columns so shorter cards do not inherit the height of a
  // neighbouring card. Keep Maintenance compact under the VFS card.
  const auto cards = {ui->workaroundsOptionsBox, ui->steamSettingsGroup,
                      ui->networkSettingsGroup, ui->virtualFilesystemGroup,
                      ui->maintenanceActionsGroup};
  for (QGroupBox* card : cards) {
    ui->workaroundsLayout->removeWidget(card);
    QSizePolicy policy = card->sizePolicy();
    policy.setVerticalPolicy(QSizePolicy::Maximum);
    card->setSizePolicy(policy);
  }

  auto* leftColumn = new QVBoxLayout;
  leftColumn->setContentsMargins(0, 0, 0, 0);
  leftColumn->setSpacing(12);
  leftColumn->addWidget(ui->workaroundsOptionsBox, 0, Qt::AlignTop);
  leftColumn->addWidget(ui->steamSettingsGroup, 0, Qt::AlignTop);
  leftColumn->addStretch(1);

  auto* rightColumn = new QVBoxLayout;
  rightColumn->setContentsMargins(0, 0, 0, 0);
  rightColumn->setSpacing(12);
  rightColumn->addWidget(ui->networkSettingsGroup, 0, Qt::AlignTop);
  rightColumn->addWidget(ui->virtualFilesystemGroup, 0, Qt::AlignTop);
  rightColumn->addWidget(ui->maintenanceActionsGroup, 0, Qt::AlignTop);
  rightColumn->addStretch(1);

  ui->workaroundsLayout->addLayout(leftColumn, 0, 0);
  ui->workaroundsLayout->addLayout(rightColumn, 0, 1);

  ui->verticalLayout_23->setContentsMargins(9, 8, 9, 9);
  ui->gridLayout_7->setContentsMargins(9, 8, 9, 9);
  ui->gridLayout_7->setHorizontalSpacing(10);
  ui->gridLayout_7->setVerticalSpacing(7);
  ui->verticalLayout_22->setContentsMargins(9, 8, 9, 9);
  ui->verticalLayout_22->setSpacing(6);

  // Keep the cards compact while preserving the common spacing between them.
  ui->verticalLayout_23->setSpacing(4);

  ui->customBrowserLayout->setSpacing(4);
  ui->browserCommandLayout->setSpacing(8);
  ui->maintenanceToolsLayout->setContentsMargins(9, 8, 9, 9);
  ui->maintenanceToolsLayout->setSpacing(6);
  ui->virtualFilesystemLayout->setContentsMargins(9, 8, 9, 9);
  ui->virtualFilesystemLayout->setSpacing(4);

  // options
  ui->forceEnableBox->setChecked(settings().game().forceEnableCoreFiles());
  ui->lockGUIBox->setChecked(settings().interface().lockGUI());
  ui->enableArchiveParsingBox->setChecked(settings().archiveParsing());

  // steam
  QString username, password;
  settings().steam().login(username, password);
  if (username.length() > 0)
    MOBase::log::getDefault().addToBlacklist(username.toStdString(), "STEAM_USERNAME");
  if (password.length() > 0)
    MOBase::log::getDefault().addToBlacklist(password.toStdString(), "STEAM_PASSWORD");

  ui->appIDEdit->setText(settings().steam().appID());
  ui->steamUserEdit->setText(username);
  ui->steamPassEdit->setText(password);

  // network
  ui->offlineBox->setChecked(settings().network().offlineMode());
  ui->proxyBox->setChecked(settings().network().useProxy());
  ui->useCustomBrowser->setChecked(settings().network().useCustomBrowser());
  ui->browserCommand->setText(settings().network().customBrowserCommand());
  // Sync after loading the saved state; setChecked() does not emit clicked().
  updateCustomBrowser();
  QObject::connect(ui->useCustomBrowser, &QCheckBox::toggled, [&] {
    updateCustomBrowser();
  });
  QObject::connect(ui->browseCustomBrowser, &QPushButton::clicked, [&] {
    browseCustomBrowser();
  });

  // buttons
  m_ExecutableBlacklist = settings().executablesBlacklist();
  m_SkipFileSuffixes    = settings().skipFileSuffixes();
  m_SkipDirectories     = settings().skipDirectories();

  QObject::connect(ui->bsaDateBtn, &QPushButton::clicked, [&] {
    on_bsaDateBtn_clicked();
  });
  QObject::connect(ui->execBlacklistBtn, &QPushButton::clicked, [&] {
    on_execBlacklistBtn_clicked();
  });
  QObject::connect(ui->skipFileSuffixBtn, &QPushButton::clicked, [&] {
    on_skipFileSuffixBtn_clicked();
  });
  QObject::connect(ui->skipDirectoriesBtn, &QPushButton::clicked, [&] {
    on_skipDirectoriesBtn_clicked();
  });
  QObject::connect(ui->resetGeometryBtn, &QPushButton::clicked, [&] {
    on_resetGeometryBtn_clicked();
  });
}

void WorkaroundsSettingsTab::update()
{
  // options
  settings().game().setForceEnableCoreFiles(ui->forceEnableBox->isChecked());
  settings().interface().setLockGUI(ui->lockGUIBox->isChecked());
  settings().setArchiveParsing(ui->enableArchiveParsingBox->isChecked());

  // steam
  if (ui->appIDEdit->text() != settings().game().plugin()->steamAPPId()) {
    settings().steam().setAppID(ui->appIDEdit->text());
  } else {
    settings().steam().setAppID("");
  }
  settings().steam().setLogin(ui->steamUserEdit->text(), ui->steamPassEdit->text());

  // network
  settings().network().setOfflineMode(ui->offlineBox->isChecked());
  settings().network().setUseProxy(ui->proxyBox->isChecked());
  settings().network().setUseCustomBrowser(ui->useCustomBrowser->isChecked());
  settings().network().setCustomBrowserCommand(ui->browserCommand->text());

  // buttons
  settings().setExecutablesBlacklist(m_ExecutableBlacklist);
  settings().setSkipFileSuffixes(m_SkipFileSuffixes);
  settings().setSkipDirectories(m_SkipDirectories);
}

bool WorkaroundsSettingsTab::changeBlacklistNow(QWidget* parent, Settings& settings)
{
  const auto current = settings.executablesBlacklist();

  if (auto s = changeBlacklistLater(parent, current)) {
    settings.setExecutablesBlacklist(*s);
    return true;
  }

  return false;
}

std::optional<QString>
WorkaroundsSettingsTab::changeBlacklistLater(QWidget* parent, const QString& current)
{
  bool ok = false;

  QString result = QInputDialog::getMultiLineText(
      parent, QObject::tr("Executables Blacklist"),
      QObject::tr("Enter one executable per line to be blacklisted from the virtual "
                  "file system.\n"
                  "Mods and other virtualized files will not be visible to these "
                  "executables and\n"
                  "any executables launched by them.\n\n"
                  "Example:\n"
                  "    Chrome.exe\n"
                  "    Firefox.exe"),
      current.split(";").join("\n"), &ok);

  if (!ok) {
    return {};
  }

  QStringList blacklist;
  for (auto exec : result.split("\n")) {
    if (exec.trimmed().endsWith(".exe", Qt::CaseInsensitive)) {
      blacklist << exec.trimmed();
    }
  }

  return blacklist.join(";");
}

std::optional<QStringList>
WorkaroundsSettingsTab::changeSkipFileSuffixes(QWidget* parent,
                                               const QStringList& current)
{
  bool ok = false;

  QString result = QInputDialog::getMultiLineText(
      parent, QObject::tr("Skip File Suffixes"),
      QObject::tr(
          "Enter one file suffix per line to be skipped / ignored from the virtual "
          "file system.\n"
          "Not to be confused with file extensions, file suffixes are simply how the "
          "filename ends.\n\n"
          "Example:\n"
          "  .txt - Would skip all files that end with .txt, <any text>.txt\n"
          "  some_file.txt - Would skip all files that end with some_file.txt, <any "
          "text>some_file.txt"),
      current.join("\n"), &ok);

  if (!ok) {
    return {};
  }

  QStringList fileSuffixes;
  for (auto& suffix : result.split("\n")) {
    auto trimmed = suffix.trimmed();
    if (!trimmed.isEmpty()) {
      fileSuffixes << trimmed;
    }
  }

  return fileSuffixes;
}

std::optional<QStringList>
WorkaroundsSettingsTab::changeSkipDirectories(QWidget* parent,
                                              const QStringList& current)
{
  bool ok = false;

  QString result = QInputDialog::getMultiLineText(
      parent, QObject::tr("Skip Directories"),
      QObject::tr(
          "Enter one directory per line to be skipped / ignored from the virtual "
          "file system.\n\n"
          "Example:\n"
          "  .git\n"
          "  instructions"),
      current.join("\n"), &ok);

  if (!ok) {
    return {};
  }

  QStringList directories;
  for (auto& dir : result.split("\n")) {
    auto trimmed = dir.trimmed();
    if (!trimmed.isEmpty()) {
      directories << trimmed;
    }
  }

  return directories;
}

void WorkaroundsSettingsTab::on_execBlacklistBtn_clicked()
{
  if (auto s = changeBlacklistLater(parentWidget(), m_ExecutableBlacklist)) {
    m_ExecutableBlacklist = *s;
  }
}

void WorkaroundsSettingsTab::on_skipFileSuffixBtn_clicked()
{
  if (auto s = changeSkipFileSuffixes(parentWidget(), m_SkipFileSuffixes)) {
    m_SkipFileSuffixes = *s;
  }
}

void WorkaroundsSettingsTab::on_skipDirectoriesBtn_clicked()
{
  if (auto s = changeSkipDirectories(parentWidget(), m_SkipDirectories)) {
    m_SkipDirectories = *s;
  }
}

void WorkaroundsSettingsTab::on_bsaDateBtn_clicked()
{
  const auto* game = qApp->property("managed_game").value<MOBase::IPluginGame*>();
  QDir dir         = game->dataDirectory();

  helper::backdateBSAs(parentWidget(), qApp->applicationDirPath().toStdWString(),
                       dir.absolutePath().toStdWString());
}

void WorkaroundsSettingsTab::updateCustomBrowser()
{
  const bool enabled = ui->useCustomBrowser->isChecked();
  ui->browserCommand->setEnabled(enabled);
  ui->browseCustomBrowser->setEnabled(enabled);
}

void WorkaroundsSettingsTab::browseCustomBrowser()
{
  const QString Filters =
      QObject::tr("Executables (*.exe)") + ";;" + QObject::tr("All Files (*.*)");

  QString file = QFileDialog::getOpenFileName(
      parentWidget(), QObject::tr("Select the browser executable"),
      ui->browserCommand->text(), Filters);

  if (file.isEmpty()) {
    return;
  }

  ui->browserCommand->setText(file + " \"%1\"");
}

void WorkaroundsSettingsTab::on_resetGeometryBtn_clicked()
{
  const auto r =
      MOBase::TaskDialog(parentWidget())
          .title(QObject::tr("Restart Mod Organizer"))
          .main(QObject::tr("Restart Mod Organizer"))
          .content(QObject::tr("Geometries will be reset to their default values."))
          .icon(QMessageBox::Question)
          .button({QObject::tr("Restart Mod Organizer"), QMessageBox::Ok})
          .button({QObject::tr("Cancel"), QMessageBox::Cancel})
          .exec();

  if (r == QMessageBox::Ok) {
    settings().geometry().requestReset();
    ExitModOrganizer(Exit::Restart);
    dialog().close();
  }
}
