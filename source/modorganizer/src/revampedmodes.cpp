#include "revampedmodes.h"
#include "fluentdialogs.h"
#include "instancemanager.h"
#include "organizercore.h"
#include "profile.h"
#include "settings.h"
#include "shared/appconfig.h"
#include "shared/util.h"
#include <iplugingame.h>
#include <log.h>

#include <QCheckBox>
#include <QCoreApplication>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QMessageBox>
#include <QPushButton>
#include <QStandardPaths>

#include <Windows.h>
#include <TlHelp32.h>

using namespace MOBase;

namespace RevampedModes
{

namespace
{

QString tr(const char* text)
{
  return QCoreApplication::translate("RevampedModes", text);
}

QString launcherFile(Mode mode)
{
  return mode == Mode::Me3 ? QStringLiteral("me3.exe")
                           : QStringLiteral("modengine2_launcher.exe");
}

QString iniFile(const QString& directory)
{
  return QDir(directory).filePath(QString::fromStdWString(AppConfig::iniFileName()));
}

bool processRunning(const QString& fileName)
{
  HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
  if (snapshot == INVALID_HANDLE_VALUE) {
    return false;
  }
  PROCESSENTRY32W entry{};
  entry.dwSize = sizeof(entry);
  bool found = false;
  for (BOOL more = Process32FirstW(snapshot, &entry); more && !found;
       more = Process32NextW(snapshot, &entry)) {
    found = QString::fromWCharArray(entry.szExeFile).compare(fileName, Qt::CaseInsensitive) == 0;
  }
  CloseHandle(snapshot);
  return found;
}

// the mode's launcher: among the main instance's executables first, then
// where its own installer puts it (me3) or in the game folder (Mod Engine 2)
QString findLauncher(Settings& main, Mode mode, const IPluginGame* game)
{
  for (const auto& executable : main.executables()) {
    const auto stored = executable.find(QStringLiteral("binary"));
    if (stored == executable.end()) {
      continue;
    }
    const QFileInfo binary(stored->second.toString());
    if (binary.fileName().compare(launcherFile(mode), Qt::CaseInsensitive) == 0 &&
        binary.isFile()) {
      return binary.absoluteFilePath();
    }
  }

  QStringList candidates;
  if (mode == Mode::Me3) {
    const QString local    = qEnvironmentVariable("LOCALAPPDATA");
    const QString programs = qEnvironmentVariable("ProgramFiles");
    candidates << QStandardPaths::findExecutable(QStringLiteral("me3"))
               << local + QStringLiteral("/Programs/me3/bin/me3.exe")
               << programs + QStringLiteral("/me3/bin/me3.exe")
               << programs + QStringLiteral("/garyttierney/me3/bin/me3.exe");
  } else if (game != nullptr) {
    for (const QDir& folder : {game->gameDirectory(), game->dataDirectory()}) {
      candidates << folder.filePath(launcherFile(mode))
                 << folder.filePath(QStringLiteral("ModEngine2/") + launcherFile(mode));
    }
  }
  for (const auto& candidate : candidates) {
    if (!candidate.isEmpty() && QFileInfo(candidate).isFile()) {
      return QFileInfo(candidate).absoluteFilePath();
    }
  }
  return {};
}

bool copyFolder(const QString& from, const QString& to)
{
  QDirIterator it(from, QDir::Files | QDir::Hidden | QDir::System,
                  QDirIterator::Subdirectories);
  bool ok = true;
  while (it.hasNext()) {
    const QString source = it.next();
    const QString target = QDir(to).filePath(QDir(from).relativeFilePath(source));
    QDir().mkpath(QFileInfo(target).absolutePath());
    ok = QFile::copy(source, target) && ok;
  }
  return ok;
}

// asks before making a mode's instance; whether to go on, and whether to copy
// the current saves into it
bool confirmCreate(QWidget* parent, Mode mode, const QString& name, bool hasSaves,
                   bool* copySaves)
{
  FluentContentDialog dialog(
      parent, QStringLiteral("RevampedModeCreateDialog"),
      tr("Make the %1 instance?").arg(title(mode)),
      tr("%1 gets an instance of its own, \"%2\": the same game and the same downloads, "
         "but its own mods, profiles, native DLLs, settings and saves, so nothing in it "
         "touches this instance. It starts empty; install there the mods to play with "
         "%1. MO2 restarts in it, and the mode selector brings you back.")
          .arg(title(mode), name));
  QCheckBox* copy = nullptr;
  if (hasSaves) {
    copy = new QCheckBox(tr("Copy this profile's saves into it"), &dialog);
    copy->setChecked(true);
    dialog.body()->addWidget(copy);
  }
  auto* go     = dialog.addButton(tr("Make it and restart"), true);
  auto* cancel = dialog.addButton(tr("Cancel"));
  QObject::connect(go, &QPushButton::clicked, &dialog, &QDialog::accept);
  QObject::connect(cancel, &QPushButton::clicked, &dialog, &QDialog::reject);
  dialog.resize(520, dialog.sizeHint().height());
  if (dialog.exec() != QDialog::Accepted) {
    return false;
  }
  *copySaves = copy != nullptr && copy->isChecked();
  return true;
}

// makes the mode's instance from the main one; its identifier, or empty
QString create(QWidget* parent, OrganizerCore& core, Settings& main, const QString& mainId,
               const QString& mainDir, Mode mode, bool fromMain)
{
  const IPluginGame* game = core.managedGame();
  const QString launcher  = findLauncher(main, mode, game);
  if (launcher.isEmpty()) {
    QMessageBox::information(
        parent, title(mode),
        tr("MO2 could not find %1 (%2). Install it, or add it to this instance's "
           "executables, then choose the mode again.")
            .arg(title(mode), launcherFile(mode)));
    return {};
  }

  const QDir root(InstanceManager::singleton().globalInstancesRootPath());
  const QString base = QStringLiteral("%1 (%2)").arg(QDir(mainDir).dirName(), title(mode));
  QString name       = base;
  for (int n = 2; QFileInfo::exists(root.filePath(name)); ++n) {
    name = QStringLiteral("%1 %2").arg(base).arg(n);
  }

  // the saves of the profile in use, only from the main instance: a mode
  // never hands its saves to another mode
  const Profile* profile = fromMain ? core.currentProfile() : nullptr;
  const QString saves =
      profile != nullptr ? QDir(profile->absolutePath()).filePath(QStringLiteral("saves"))
                         : QString();
  const bool hasSaves =
      !saves.isEmpty() &&
      QDirIterator(saves, QDir::Files, QDirIterator::Subdirectories).hasNext();
  bool copySaves = false;
  if (!confirmCreate(parent, mode, name, hasSaves, &copySaves)) {
    return {};
  }

  const QString directory = root.filePath(name);
  const QString profiles  = QDir(directory).filePath(QString::fromStdWString(AppConfig::profilesPath()));
  for (const auto& folder :
       {directory, QDir(directory).filePath(QString::fromStdWString(AppConfig::modsPath())),
        profiles, QDir(directory).filePath(QString::fromStdWString(AppConfig::overwritePath()))}) {
    if (!QDir().mkpath(folder)) {
      QMessageBox::warning(parent, title(mode),
                           tr("Could not make the folder %1.").arg(QDir::toNativeSeparators(folder)));
      return {};
    }
  }

  {
    Settings s(iniFile(directory));
    if (game != nullptr) {
      s.game().setName(game->gameName());
    }
    if (const auto location = main.game().directory()) {
      s.game().setDirectory(*location);
    }
    if (const auto edition = main.game().edition()) {
      s.game().setEdition(*edition);
    }
    // the same look and language; the theme was already chosen
    s.interface().setStyleName(
        main.interface().styleName().value_or(Settings::defaultInterfaceStyleName()));
    s.interface().setLanguage(main.interface().language());
    s.interface().setThemeChoicePromptCompleted(true);
    // the archives are shared; what is installed from them is not
    s.paths().setDownloads(main.paths().downloads());
    s.setProfileLocalInis(main.profileLocalInis());
    s.setProfileLocalSaves(main.profileLocalSaves());
    s.setProfileArchiveInvalidation(main.profileArchiveInvalidation());
    if (!main.eldenRingDefaultSaveMode().isEmpty()) {
      s.setEldenRingDefaultSaveMode(main.eldenRingDefaultSaveMode());
    }
    s.setBetaMe3(mode == Mode::Me3);
    s.setBetaMe2(mode == Mode::Me2);
    s.setRevampedValue(QStringLiteral("revamped_mode"), key(mode));
    s.setRevampedValue(QStringLiteral("revamped_mode_parent"), mainId);
    s.setRevampedValue(QStringLiteral("revamped_mode_parent_dir"),
                       QDir::fromNativeSeparators(mainDir));
    s.setRevampedValue(QStringLiteral("revamped_mode_launcher"),
                       QDir::fromNativeSeparators(launcher));
    if (s.sync() != QSettings::NoError) {
      QMessageBox::warning(parent, title(mode),
                           tr("Could not write %1.").arg(QDir::toNativeSeparators(iniFile(directory))));
      return {};
    }
  }

  // the categories are how the mods are sorted, not a launcher's settings;
  // with them the new instance doesn't ask how to set categories up
  for (const auto& file : {QStringLiteral("categories.dat"), QStringLiteral("nexuscatmap.dat")}) {
    const QString source = QDir(mainDir).filePath(file);
    if (QFileInfo(source).isFile()) {
      QFile::copy(source, QDir(directory).filePath(file));
    }
  }

  if (copySaves) {
    const QString target = QDir(profiles).filePath(QStringLiteral("Default/saves"));
    if (!copyFolder(saves, target)) {
      log::warn("game modes: some saves were not copied into '{}'", target);
    }
  }

  // listed beside the main instance in the instance manager
  QString group = GlobalSettings::instanceValue(QStringLiteral("InstanceGroups"), mainId);
  if (group.isEmpty()) {
    group = QDir(mainDir).dirName();
  }
  GlobalSettings::setInstanceValue(QStringLiteral("InstanceGroups"), name, group);

  main.setRevampedValue(QStringLiteral("revamped_mode_") + key(mode), name);
  main.sync();
  log::info("game modes: made the {} instance '{}' from '{}'", key(mode), name, mainId);
  return name;
}

}  // namespace

QString key(Mode mode)
{
  switch (mode) {
  case Mode::Me3:
    return QStringLiteral("me3");
  case Mode::Me2:
    return QStringLiteral("me2");
  default:
    return {};
  }
}

QString title(Mode mode)
{
  switch (mode) {
  case Mode::Me3:
    return QStringLiteral("me3");
  case Mode::Me2:
    return QStringLiteral("Mod Engine 2");
  default:
    return QStringLiteral("MO2");
  }
}

QString launcherTitle(Mode mode)
{
  switch (mode) {
  case Mode::Me3:
    return QStringLiteral("me3 (beta)");
  case Mode::Me2:
    return QStringLiteral("Mod Engine 2 (beta)");
  default:
    return {};
  }
}

Mode current(const Settings& settings)
{
  const QString value = settings.revampedValue(QStringLiteral("revamped_mode"));
  if (value == key(Mode::Me3)) {
    return Mode::Me3;
  }
  if (value == key(Mode::Me2)) {
    return Mode::Me2;
  }
  return Mode::MO2;
}

bool available(const Settings& settings)
{
  return settings.betaMe3() || settings.betaMe2() || current(settings) != Mode::MO2;
}

bool switchTo(QWidget* parent, OrganizerCore& core, Mode target)
{
  Settings& settings = Settings::instance();
  const Mode now     = current(settings);
  if (now == target) {
    return false;
  }

  // a mode switch moves to other saves: never while the game runs
  if (const IPluginGame* game = core.managedGame()) {
    if (processRunning(QFileInfo(game->binaryName()).fileName())) {
      QMessageBox::information(parent, title(target),
                               tr("Close the game before you change the game mode."));
      return false;
    }
  }

  const auto active = InstanceManager::singleton().currentInstance();
  if (!active) {
    return false;
  }

  // the main instance: this one, or the one this mode instance was made from
  QString mainId  = active->instanceIdentifier();
  QString mainDir = active->directory();
  if (now != Mode::MO2) {
    mainId  = settings.revampedValue(QStringLiteral("revamped_mode_parent"));
    mainDir = settings.revampedValue(QStringLiteral("revamped_mode_parent_dir"));
    if (!QFileInfo(iniFile(mainDir)).isFile()) {
      QMessageBox::warning(parent, title(target),
                           tr("The instance this mode was made from is gone:\n%1")
                               .arg(QDir::toNativeSeparators(mainDir)));
      return false;
    }
  }

  QString targetId = mainId;
  if (target != Mode::MO2) {
    std::unique_ptr<Settings> other;
    if (now != Mode::MO2) {
      other = std::make_unique<Settings>(iniFile(mainDir));
    }
    Settings& main = other ? *other : settings;
    targetId       = main.revampedValue(QStringLiteral("revamped_mode_") + key(target));
    const QString targetDir =
        QDir(InstanceManager::singleton().globalInstancesRootPath()).filePath(targetId);
    if (targetId.isEmpty() || !QFileInfo(iniFile(targetDir)).isFile()) {
      targetId = create(parent, core, main, mainId, mainDir, target, now == Mode::MO2);
      if (targetId.isEmpty()) {
        return false;
      }
    }
  }

  // the instance only changes once MO2 is sure to close, as in the instance
  // manager
  if (!ExitModOrganizer(Exit::Restart)) {
    return false;
  }
  log::info("game modes: switching from {} to {} ('{}')", now == Mode::MO2 ? "MO2" : key(now),
            target == Mode::MO2 ? "MO2" : key(target), targetId);
  InstanceManager::singleton().setCurrentInstance(targetId);
  return true;
}

}  // namespace RevampedModes
