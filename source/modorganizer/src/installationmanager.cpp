/*
Copyright (C) 2012 Sebastian Herbord. All rights reserved.

This file is part of Mod Organizer.

Mod Organizer is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

Mod Organizer is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with Mod Organizer.  If not, see <http://www.gnu.org/licenses/>.
*/

#include <tuple>

#include "installationmanager.h"
#include "fluenticons.h"
#include "fluentdialogs.h"

#include "categories.h"
#include "filesystemutilities.h"
#include "iplugininstallercustom.h"
#include "iplugininstallersimple.h"
#include "messagedialog.h"
#include "modinfo.h"
#include "nexusinterface.h"
#include "queryoverwritedialog.h"
#include "shared/util.h"
#include "questionboxmemory.h"
#include "report.h"
#include "selectiondialog.h"
#include "settings.h"
#include <scopeguard.h>
#include <utility.h>

#include <QApplication>
#include <QLabel>
#include <QLineEdit>
#include <QDateTime>
#include <QDebug>
#include <QDir>
#include <QDirIterator>
#include <QFileInfo>
#include <QInputDialog>
#include <QLibrary>
#include <QMessageBox>
#include <QPushButton>
#include <QSettings>
#include <QTextDocument>
#include <QtConcurrent/QtConcurrentRun>

#include <Shellapi.h>

#include <boost/assign.hpp>
#include <functional>
#include <optional>
#include <boost/scoped_ptr.hpp>

#include "archivefiletree.h"
#include "winrarfallback.h"
#include "scriptextendersetup.h"
#include <scriptextender.h>
#include <iplugingame.h>
#include <QUuid>

using namespace MOBase;
using namespace MOShared;

InstallationResult::InstallationResult(IPluginInstaller::EInstallResult result)
    : m_result(result), m_name(), m_iniTweaks(false), m_backup(false), m_merged(false),
      m_replaced(false)
{}

InstallationManager::InstallationManager() : m_ParentWidget(nullptr), m_IsRunning(false)
{
  m_ArchiveHandler = CreateArchive();
  if (!m_ArchiveHandler->isValid()) {
    throw MyException(getErrorString(m_ArchiveHandler->getLastError()));
  }
  m_ArchiveHandler->setLogCallback([](auto level, auto const& message) {
    using LogLevel = Archive::LogLevel;
    switch (level) {
    case LogLevel::Debug:
      log::debug("{}", message);
      break;
    case LogLevel::Info:
      log::info("{}", message);
      break;
    case LogLevel::Warning:
      log::warn("{}", message);
      break;
    case LogLevel::Error:
      log::error("{}", message);
      break;
    }
  });

  // Connect the query password slot - This is the only way I found to be able to query
  // user from a separate thread. We use a BlockingQueuedConnection so that calling
  // passwordRequested() will block until the end of the slot.
  connect(this, &InstallationManager::passwordRequested, this,
          &InstallationManager::queryPassword, Qt::BlockingQueuedConnection);
}

InstallationManager::~InstallationManager() {}

void InstallationManager::setParentWidget(QWidget* widget)
{
  m_ParentWidget = widget;
}

void InstallationManager::setGameAccess(
    std::function<const IPluginGame*()> game,
    std::function<std::shared_ptr<const ScriptExtender>()> extender)
{
  m_Game           = std::move(game);
  m_ScriptExtender = std::move(extender);
}

void InstallationManager::setPluginContainer(const PluginContainer* pluginContainer)
{
  m_PluginContainer = pluginContainer;
}

namespace
{
// asks for one line of text: a Fluent content dialog with the field and what
// is wrong with the text under it (problem returns that, or nothing), or a
// QInputDialog without the Fluent themes; nullopt when cancelled
std::optional<QString> askText(QWidget* parent, const QString& title, const QString& text,
                               const QString& label, const QString& value,
                               bool password = false,
                               std::function<QString(const QString&)> problem = {})
{

  FluentContentDialog dlg(parent, QStringLiteral("InstallTextDialog"), title, text);
  auto* caption = new QLabel(label, &dlg);
  caption->setObjectName(QStringLiteral("instanceNameLabel"));
  auto* edit = new QLineEdit(value, &dlg);
  edit->setEchoMode(password ? QLineEdit::Password : QLineEdit::Normal);
  edit->selectAll();
  caption->setBuddy(edit);
  auto* error = new QLabel(&dlg);
  error->setObjectName(QStringLiteral("instanceNameValidation"));
  error->setWordWrap(true);
  error->hide();
  dlg.body()->addWidget(caption);
  dlg.body()->addWidget(edit);
  dlg.body()->addWidget(error);

  auto* ok     = dlg.addButton(QObject::tr("OK"), true);
  auto* cancel = dlg.addButton(QObject::tr("Cancel"));
  QObject::connect(ok, &QPushButton::clicked, &dlg, &QDialog::accept);
  QObject::connect(cancel, &QPushButton::clicked, &dlg, &QDialog::reject);

  auto check = [&] {
    const QString wrong = problem ? problem(edit->text().trimmed()) : QString();
    error->setText(wrong);
    error->setVisible(!wrong.isEmpty());
    ok->setEnabled(wrong.isEmpty() && (password ? !edit->text().isEmpty()
                                                : !edit->text().trimmed().isEmpty()));
  };
  QObject::connect(edit, &QLineEdit::textChanged, &dlg, check);
  check();

  dlg.resize(460, dlg.sizeHint().height());
  if (dlg.exec() != QDialog::Accepted) {
    return std::nullopt;
  }
  return password ? edit->text() : edit->text().trimmed();
}
}  // namespace

void InstallationManager::queryPassword()
{
  const auto typed =
      askText(m_ParentWidget, tr("Password required"),
              m_PasswordRejected ? tr("That password did not open the archive. Try again.")
                                 : tr("This archive is protected with a password."),
              tr("Password"), {}, true);
  m_PasswordCancelled = !typed.has_value();
  m_Password          = typed.value_or(QString());
}

bool InstallationManager::extractFiles(QString extractPath, QString title,
                                       bool showFilenames, bool silent)
{
  TimeThis tt("InstallationManager::extractFiles");

  // Callback for errors:
  QString errorMessage;
  auto errorCallback = [&errorMessage, this](std::wstring const& message) {
    m_ArchiveHandler->cancel();
    errorMessage = QString::fromStdWString(message);
  };

  // The future that will hold the result:
  QFuture<bool> future;

  if (silent) {
    future = QtConcurrent::run([&]() -> bool {
      return m_ArchiveHandler->extract(extractPath.toStdWString(), nullptr, nullptr,
                                       errorCallback);
    });
    future.waitForFinished();
  } else {
    QProgressDialog* installationProgress = new QProgressDialog(m_ParentWidget);
    ON_BLOCK_EXIT([=]() {
      installationProgress->cancel();
      installationProgress->hide();
      installationProgress->deleteLater();
    });
    installationProgress->setWindowFlags(installationProgress->windowFlags() &
                                         (~Qt::WindowContextHelpButtonHint));
    installationProgress->setWindowTitle(title.isEmpty() ? tr("Installing") : title);
    installationProgress->setWindowModality(Qt::WindowModal);
    // the width stays when the file names change
    installationProgress->setFixedWidth(600);

    // Turn off auto-reset otherwize the progress dialog is reset before the end. This
    // is kind of annoying because updateProgress consider percentage of progression
    // through the archive (pack), while we are waiting for extracting archive entries,
    // so the percentage of in updateProgress is not really related to the percentage of
    // files extracted...
    installationProgress->setAutoReset(false);

    // Note: Using a loop with a progressUpdate() that only wake-up the loop. The
    // event-loop will be used in a loop and not via exec() because connecting to
    // QProgressDialog::setValue and using .exec() creates huge recursion that leads to
    // stack-overflow. See https://bugreports.qt.io/browse/QTBUG-10561
    QEventLoop loop;
    connect(this, &InstallationManager::progressUpdate, &loop, &QEventLoop::wakeUp,
            Qt::QueuedConnection);

    // Cancelling progress only cancel the extraction, we do not force exiting the
    // event-loop:
    connect(installationProgress, &QProgressDialog::canceled, [this]() {
      m_ArchiveHandler->cancel();
    });

    std::mutex mutex;
    int currentProgress = 0;
    QString currentFileName;

    // The callbacks:
    auto progressCallback = [this, &currentProgress, &mutex](
                                auto progressType, uint64_t current, uint64_t total) {
      if (progressType == Archive::ProgressType::EXTRACTION) {
        {
          std::scoped_lock guard(mutex);
          currentProgress = static_cast<int>(100 * current / total);
        }
        emit progressUpdate();
      }
    };
    Archive::FileChangeCallback fileChangeCallback =
        [this, &currentFileName, &mutex](auto changeType, std::wstring const& file) {
          if (changeType == Archive::FileChangeType::EXTRACTION_START) {
            {
              std::scoped_lock guard(mutex);
              currentFileName = QString::fromStdWString(file);
            }
            emit progressUpdate();
          }
        };

    // unpack only the files we need for the installer
    QFutureWatcher<bool> futureWatcher;
    connect(&futureWatcher, &QFutureWatcher<bool>::finished, &loop, &QEventLoop::wakeUp,
            Qt::QueuedConnection);
    futureWatcher.setFuture(QtConcurrent::run([&]() -> bool {
      return m_ArchiveHandler->extract(extractPath.toStdWString(), progressCallback,
                                       showFilenames ? fileChangeCallback : nullptr,
                                       errorCallback);
    }));

    installationProgress->setModal(true);
    installationProgress->show();

    while (!futureWatcher.isFinished()) {
      loop.processEvents(QEventLoop::AllEvents | QEventLoop::WaitForMoreEvents);
      std::scoped_lock guard(mutex);
      if (currentProgress != installationProgress->value()) {
        installationProgress->setValue(currentProgress);
      }
      if (currentFileName != installationProgress->labelText()) {
        installationProgress->setLabelText(currentFileName);
      }
    }

    installationProgress->hide();

    future = futureWatcher.future();
  }

  // Check the result; an error can come with a result that reports success
  if ((!future.result() || !errorMessage.isEmpty()) && m_PasswordCancelled) {
    return false;
  }
  if (!errorMessage.isEmpty()) {
    // RAR4 and 7z report a wrong password as a damaged file
    if (m_PasswordAsked && (errorMessage == QLatin1String("CRC error") ||
                            errorMessage == QLatin1String("Data error"))) {
      throw MyException(tr("Wrong password, or the archive is damaged."));
    }
    throw MyException(tr("Extraction failed: %1").arg(errorMessage));
  }
  if (!future.result()) {
    if (m_ArchiveHandler->getLastError() == Archive::Error::ERROR_EXTRACT_CANCELLED) {
      return false;
    } else {
      throw MyException(tr("Extraction failed: %1")
                            .arg(static_cast<int>(m_ArchiveHandler->getLastError())));
    }
  }

  return true;
}

QString InstallationManager::extractFile(std::shared_ptr<const FileTreeEntry> entry,
                                         bool silent)
{
  QStringList result = this->extractFiles({entry}, silent);
  return result.isEmpty() ? QString() : result[0];
}

QStringList InstallationManager::extractFiles(
    std::vector<std::shared_ptr<const FileTreeEntry>> const& entries, bool silent)
{
  // Remove the directory since mapToArchive would add them:
  std::vector<std::shared_ptr<const FileTreeEntry>> files;
  std::copy_if(entries.begin(), entries.end(), std::back_inserter(files),
               [](auto const& entry) {
                 return entry->isFile();
               });

  // Update the archive:
  ArchiveFileTree::mapToArchive(*m_ArchiveHandler, files);

  // Retrieve the file path:
  QStringList result;

  for (auto& entry : files) {
    auto path = entry->path();
    result.append(QDir::tempPath().append("/").append(path));
    m_TempFilesToDelete.insert(path);
  }

  if (!extractFiles(QDir::tempPath(), tr("Extracting files"), false, silent)) {
    return QStringList();
  }

  return result;
}

QString
InstallationManager::createFile(std::shared_ptr<const MOBase::FileTreeEntry> entry)
{
  // Use QTemporaryFile to create the temporary file with the given template:
  QTemporaryFile tempFile(
      QDir::cleanPath(QDir::tempPath() + QDir::separator() + "mo2-install"));

  // Turn-off autoRemove otherwise the file is deleted when destructor is called:
  tempFile.setAutoRemove(false);

  // Open/Close the file so that installer can use it properly:
  if (!tempFile.open()) {
    return QString();
  }
  tempFile.close();

  // fileName() returns the full path since we provide a full path in the constructor:
  const QString absPath = tempFile.fileName();

  m_CreatedFiles[entry] = absPath;
  m_TempFilesToDelete.insert(QDir::temp().relativeFilePath(absPath));

  // Returns the path with native separators:
  return QDir::toNativeSeparators(absPath);
}

void InstallationManager::cleanCreatedFiles(
    std::shared_ptr<const MOBase::IFileTree> fileTree)
{
  // We simply have to check if all the entries have fileTree as a parent:
  for (auto it = std::begin(m_CreatedFiles); it != std::end(m_CreatedFiles);) {

    // Find the parent - Could this be in FileTreeEntry?
    bool found = false;
    {
      auto parent = it->first->parent();
      while (parent && !found) {
        if (parent == fileTree) {
          found = true;
        } else {
          parent = parent->parent();
        }
      }
    }

    // If the parent was not found, we remove the entry, otherwize we move to the next
    // one:
    if (!found) {
      it = m_CreatedFiles.erase(it);
    } else {
      ++it;
    }
  }
}

IPluginInstaller::EInstallResult
InstallationManager::installArchive(GuessedValue<QString>& modName,
                                    const QString& archiveName, int modId)
{
  // in earlier versions the modName was copied here and the copy passed to install. I
  // don't know why I did this and it causes a problem if this is called by the bundle
  // installer and the bundled installer adds additional names that then end up being
  // used, because the caller will then not have the right name.
  return install(archiveName, modName, modId).result();
}

QString InstallationManager::generateBackupName(const QString& directoryName) const
{
  QString backupName = directoryName + "_backup";
  if (QDir(backupName).exists()) {
    int idx      = 2;
    QString temp = backupName + QString::number(idx);
    while (QDir(temp).exists()) {
      ++idx;
      temp = backupName + QString::number(idx);
    }
    backupName = temp;
  }
  return backupName;
}

InstallationResult InstallationManager::testOverwrite(GuessedValue<QString>& modName)
{
  // a Replace nobody finished: its folder stays for the question at the next start,
  // and nothing below may act on it
  if (!m_ReplacedFolder.isEmpty()) {
    log::error("a replaced mod was left unfinished: {}", m_ReplacedFolder);
    m_ReplacedFolder.clear();
  }
  m_ReplacedRestored = false;
  m_NewModRemoved    = false;
  m_NewModFolder.clear();

  // the installed mod as its folder is named: a guess that differs only in
  // case would rename the folder on Replace, or when a cancel puts it back, and
  // the mod list would take it for another mod, disabled and at the end
  const QString guessed = modName;
  for (const QString& entry : QDir(m_ModsDirectory)
                                  .entryList(QDir::Dirs | QDir::NoDotAndDotDot |
                                             QDir::Hidden)) {
    if (entry != guessed && entry.compare(guessed, Qt::CaseInsensitive) == 0) {
      modName.update(entry, GUESS_USER);
      break;
    }
  }

  QString targetDirectory =
      QDir::fromNativeSeparators(m_ModsDirectory + "\\" + modName);

  // this is only returned on success
  InstallationResult result{IPluginInstaller::RESULT_SUCCESS};

  while (QDir(targetDirectory).exists()) {
    Settings& settings(Settings::instance());

    const bool backup = settings.keepBackupOnInstall();
    QueryOverwriteDialog overwriteDialog(m_ParentWidget,
                                         backup ? QueryOverwriteDialog::BACKUP_YES
                                                : QueryOverwriteDialog::BACKUP_NO);
    overwriteDialog.setModName(QFileInfo(targetDirectory).fileName());

    if (overwriteDialog.exec()) {
      settings.setKeepBackupOnInstall(overwriteDialog.backup());

      // a new mod beside the installed one changes nothing there
      const bool keepBackup = overwriteDialog.backup() &&
                              overwriteDialog.action() != QueryOverwriteDialog::ACT_RENAME;
      if (keepBackup) {
        QString backupDirectory = generateBackupName(targetDirectory);
        if (!copyDir(targetDirectory, backupDirectory, false)) {
          reportError(tr("Failed to create backup"));
          return {IPluginInstaller::RESULT_CANCELED};
        }
      }

      result.m_merged   = overwriteDialog.action() == QueryOverwriteDialog::ACT_MERGE;
      result.m_replaced = overwriteDialog.action() == QueryOverwriteDialog::ACT_REPLACE;
      result.m_backup   = keepBackup;

      if (overwriteDialog.action() == QueryOverwriteDialog::ACT_RENAME) {
        // a free name to start from: "<name> (2)", "<name> (3)"...
        const QDir mods(m_ModsDirectory);
        QString suggested = modName;
        for (int n = 2; mods.exists(suggested); ++n) {
          suggested = QStringLiteral("%1 (%2)").arg(static_cast<QString>(modName)).arg(n);
        }
        const auto answer = askText(
            m_ParentWidget, tr("Install as a new mod"),
            tr("The installed mod stays as it is; this one is added beside it with "
               "another name."),
            tr("Name"), suggested, false, [&mods](const QString& typed) {
              return mods.exists(typed) ? tr("A mod with this name is already installed.")
                                        : QString();
            });
        const QString name = answer.value_or(QString());
        if (answer && !name.isEmpty()) {
          modName.update(name, GUESS_USER);
          if (!ensureValidModName(modName)) {
            return {IPluginInstaller::RESULT_FAILED};
          }
          targetDirectory = QDir::fromNativeSeparators(m_ModsDirectory) + "/" + modName;
        }
      } else if (overwriteDialog.action() == QueryOverwriteDialog::ACT_REPLACE) {
        // save original settings like categories. Because it makes sense
        QString metaFilename = targetDirectory + "/meta.ini";
        QFile settingsFile(metaFilename);
        QByteArray originalSettings;
        if (settingsFile.open(QIODevice::ReadOnly)) {
          originalSettings = settingsFile.readAll();
          settingsFile.close();
        }

        // the old files wait beside the new ones until the install has worked, so a
        // cancelled or failed install loses nothing
        const QString asideDirectory = setAsideMod(targetDirectory);
        if (asideDirectory.isEmpty()) {
          reportError(tr("The installed mod could not be replaced. Close any program "
                         "using its files and try again."));
          return {IPluginInstaller::RESULT_CANCELED};
        }
        m_ReplacedFolder = asideDirectory;
        m_ReplacedTarget = targetDirectory;
        m_ReplacedInstallFile.reset();
        unsigned int idx = ModInfo::getIndex(modName);
        if (idx != UINT_MAX) {
          m_ReplacedInstallFile = ModInfo::getByIndex(idx)->installationFile();
        }
        if (!QDir().mkdir(targetDirectory)) {
          // windows may keep the directory around for a moment, preventing its
          // re-creation
          Sleep(100);
          if (!QDir().mkdir(targetDirectory)) {
            reportError(tr("The folder for the new version could not be created."));
            restoreReplacedMod();
            return {IPluginInstaller::RESULT_CANCELED};
          }
        }
        // restore the saved settings
        if (settingsFile.open(QIODevice::WriteOnly)) {
          settingsFile.write(originalSettings);
          settingsFile.close();
        } else {
          log::error("failed to restore original settings: {}", metaFilename);
        }
        return result;
      } else if (overwriteDialog.action() == QueryOverwriteDialog::ACT_MERGE) {
        return result;
      } else /* if (overwriteDialog.action() == QueryOverwriteDialog::ACT_NONE) */
      {
        return {IPluginInstaller::RESULT_CANCELED};
      }
    } else {
      return {IPluginInstaller::RESULT_CANCELED};
    }
  }

  if (!QDir(targetDirectory).exists()) {
    m_NewModFolder = targetDirectory;
  }
  QDir().mkdir(targetDirectory);

  return result;
}

namespace
{
// beside the old version in its numbered folder: whose it is and from when
const QString ReplacedManifestName = QStringLiteral("replaced.ini");
}  // namespace

QStringList InstallationManager::replacedFolderRoots() const
{
  // beside the mods folder, so it never shows as a mod; inside it (the mod list skips
  // it) for a mods folder that is on a volume of its own
  const QString mods = QDir::cleanPath(QDir::fromNativeSeparators(m_ModsDirectory));
  const QString name = QString::fromLatin1(ReplacedModsFolder);
  QStringList roots{QDir::cleanPath(QFileInfo(mods).absolutePath() + "/" + name),
                    QDir::cleanPath(mods + "/" + name)};
  roots.removeDuplicates();
  return roots;
}

QString InstallationManager::setAsideMod(const QString& modFolder) const
{
  for (const QString& root : replacedFolderRoots()) {
    // a numbered folder each, as a leftover of the same mod may still be there
    QString slot;
    for (int n = 1; slot.isEmpty() || QFileInfo::exists(slot); ++n) {
      slot = root + "/" + QString::number(n);
    }
    if (!QDir().mkpath(slot)) {
      continue;
    }
    {
      QSettings manifest(slot + "/" + ReplacedManifestName, QSettings::IniFormat);
      manifest.setValue("modsPath", QDir::cleanPath(QDir::fromNativeSeparators(m_ModsDirectory)));
      manifest.setValue("date", QDateTime::currentDateTimeUtc().toString(Qt::ISODate));
      manifest.sync();
    }
    const QString aside = slot + "/" + QFileInfo(modFolder).fileName();
    if (QDir().rename(modFolder, aside)) {
      return aside;
    }
    removeAsideSlot(aside);
  }
  return {};
}

void InstallationManager::removeAsideSlot(const QString& aside) const
{
  // only empty folders go
  QDir slot = QFileInfo(aside).dir();
  const QString root = QFileInfo(slot.absolutePath()).absolutePath();
  QFile::remove(slot.filePath(ReplacedManifestName));
  QDir().rmdir(slot.absolutePath());
  QDir().rmdir(root);
}

QStringList InstallationManager::replacedLeftovers(QStringList* older) const
{
  struct Leftover
  {
    QString path;
    QDateTime date;
    int slot;
  };
  const QString mods = QDir::cleanPath(QDir::fromNativeSeparators(m_ModsDirectory));
  std::map<QString, std::vector<Leftover>> byName;

  for (const QString& root : replacedFolderRoots()) {
    const auto slotList = QDir(root).entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot);
    for (const QFileInfo& slot : slotList) {
      QDateTime date = slot.lastModified();
      const QString manifestPath = slot.absoluteFilePath() + "/" + ReplacedManifestName;
      // no record: it is in this instance's own folder, which is all there is to go on
      if (QFileInfo::exists(manifestPath)) {
        const QSettings manifest(manifestPath, QSettings::IniFormat);
        const QString owner = manifest.value("modsPath").toString();
        // two instances can share the parent folder. A folder that moved along with
        // its instance keeps its name, and the old path is gone
        const bool same = owner.compare(mods, Qt::CaseInsensitive) == 0;
        const bool moved = !QFileInfo::exists(owner) &&
                           QFileInfo(owner).fileName().compare(QFileInfo(mods).fileName(),
                                                               Qt::CaseInsensitive) == 0;
        if (!same && !moved) {
          continue;
        }
        const QDateTime recorded =
            QDateTime::fromString(manifest.value("date").toString(), Qt::ISODate);
        if (recorded.isValid()) {
          date = recorded;
        }
      }
      const auto modList =
          QDir(slot.absoluteFilePath()).entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot);
      for (const QFileInfo& mod : modList) {
        byName[mod.fileName().toLower()].push_back(
            {mod.absoluteFilePath(), date, slot.fileName().toInt()});
      }
    }
  }

  // one copy of each mod at a time: restoring two would send the first to the Bin
  QStringList newest;
  for (auto& [name, copies] : byName) {
    std::sort(copies.begin(), copies.end(), [](const Leftover& a, const Leftover& b) {
      return a.date != b.date ? a.date > b.date : a.slot > b.slot;
    });
    newest.append(copies.front().path);
    if (older != nullptr) {
      for (std::size_t i = 1; i < copies.size(); ++i) {
        older->append(copies[i].path);
      }
    }
  }
  return newest;
}

bool InstallationManager::restoreLeftover(const QString& aside)
{
  const QString target = QDir::fromNativeSeparators(m_ModsDirectory) + "/" +
                         QFileInfo(aside).fileName();

  // the version in its place waits aside too, and goes to the Recycle Bin only
  // once the leftover is back: a rename that fails (a file in use, a mods folder
  // linked to another drive) would otherwise leave neither in the mods folder
  QString current;
  if (QFileInfo::exists(target)) {
    current = setAsideMod(target);
    if (current.isEmpty()) {
      return false;
    }
  }

  if (!QDir().rename(aside, target)) {
    if (!current.isEmpty()) {
      if (QDir().rename(current, target)) {
        removeAsideSlot(current);
      } else {
        log::error("failed to put back {} as {}", current, target);
      }
    }
    return false;
  }

  if (!current.isEmpty() && !shellRemove({current}, true, m_ParentWidget).removed()) {
    // as before: the leftover aside, the current version in place
    if (QDir().rename(target, aside) && QDir().rename(current, target)) {
      removeAsideSlot(current);
    } else {
      log::error("failed to undo restoring {}; the version it replaced is in {}", aside,
                 current);
    }
    return false;
  }

  removeAsideSlot(aside);
  if (!current.isEmpty()) {
    removeAsideSlot(current);
  }
  return true;
}

bool InstallationManager::discardLeftover(const QString& aside)
{
  if (!shellRemove({aside}, true, m_ParentWidget).removed()) {
    return false;
  }
  removeAsideSlot(aside);
  return true;
}

void InstallationManager::removeUnfinishedNewMod()
{
  // the folder didn't exist before this install, so all of it comes from there;
  // left behind, it would show up as a mod with part of its files
  if (m_NewModFolder.isEmpty()) {
    return;
  }
  const QString folder = std::exchange(m_NewModFolder, QString());
  m_NewModRemoved      = QDir(folder).removeRecursively();
  if (!m_NewModRemoved) {
    log::error("failed to remove the unfinished install in {}", folder);
  }
}

void InstallationManager::restoreReplacedMod()
{
  if (m_ReplacedFolder.isEmpty()) {
    return;
  }
  const QString aside = std::exchange(m_ReplacedFolder, QString());

  // what the install left in its place
  QDir(m_ReplacedTarget).removeRecursively();
  if (!QDir().rename(aside, m_ReplacedTarget)) {
    log::error("failed to put back {} as {}", aside, m_ReplacedTarget);
    reportError(tr("The previous version of the mod could not be put back. It is in "
                   "this folder:\n%1")
                    .arg(QDir::toNativeSeparators(aside)));
    return;
  }
  removeAsideSlot(aside);
  m_ReplacedRestored = true;
}

void InstallationManager::discardReplacedMod()
{
  if (m_ReplacedFolder.isEmpty()) {
    return;
  }
  const QString aside = std::exchange(m_ReplacedFolder, QString());

  if (m_ReplacedInstallFile) {
    // mark the old install file as uninstalled
    emit modReplaced(*m_ReplacedInstallFile);
  }
  if (!shellRemove({aside}, true, m_ParentWidget).removed()) {
    reportError(tr("The mod was replaced, but its previous version did not go to the "
                   "Recycle Bin. It is in this folder:\n%1")
                    .arg(QDir::toNativeSeparators(aside)));
    return;
  }
  removeAsideSlot(aside);
}

bool InstallationManager::ensureValidModName(GuessedValue<QString>& name) const
{
  while (name->isEmpty()) {
    const auto answer = askText(
        m_ParentWidget, tr("Invalid name"),
        tr("The name you entered is invalid, please enter a different one."), tr("Name"),
        {});
    if (!answer) {
      return false;
    }
    name.update(*answer, GUESS_USER);
  }
  return true;
}

InstallationResult InstallationManager::doInstall(GuessedValue<QString>& modName,
                                                  QString gameName, int modID,
                                                  const QString& version,
                                                  const QString& newestVersion,
                                                  int categoryID, int fileCategoryID,
                                                  const QString& repository)
{
  if (!ensureValidModName(modName)) {
    return {IPluginInstaller::RESULT_FAILED};
  }

  // determine target directory
  InstallationResult result = testOverwrite(modName);
  if (!result) {
    return result;
  }

  // merging into the installed mod keeps its newer version and its list of
  // installed files
  const bool merge = result.merged();

  // does nothing once the install has finished
  ON_BLOCK_EXIT([this]() {
    restoreReplacedMod();
    removeUnfinishedNewMod();
  });

  result.m_name = modName;

  QString targetDirectory       = QDir(m_ModsDirectory + "/" + modName).canonicalPath();
  QString targetDirectoryNative = QDir::toNativeSeparators(targetDirectory);

  // empty when the folder could not be created (a path too long, no rights):
  // the files would land in the working directory and meta.ini at the root
  if (targetDirectory.isEmpty()) {
    log::error("the folder for the mod '{}' could not be created in {}",
               static_cast<QString>(modName), m_ModsDirectory);
    reportError(tr("The folder for the mod \"%1\" could not be created in the mods "
                   "folder.")
                    .arg(static_cast<QString>(modName)));
    return {IPluginInstaller::RESULT_FAILED};
  }

  log::debug("installing to \"{}\"", targetDirectoryNative);
  if (!extractFiles(targetDirectory, "", true, false)) {
    return {IPluginInstaller::RESULT_CANCELED};
  }

  // Copy the created files:
  for (auto& p : m_CreatedFiles) {
    QString destPath =
        QDir::cleanPath(targetDirectory + QDir::separator() + p.first->path());
    log::debug("Moving {} to {}.", p.second, destPath);

    // We need to remove the path if it exists:
    if (QFile::exists(destPath)) {
      QFile::remove(destPath);
    }

    QDir dir = QFileInfo(destPath).absoluteDir();
    if (!dir.exists()) {
      dir.mkpath(".");
    }

    if (!QFile::copy(p.second, destPath)) {
      log::error("failed to copy {} to {}", p.second, destPath);
      return {IPluginInstaller::RESULT_FAILED};
    }
  }

  QSettings settingsFile(targetDirectory + "/meta.ini", QSettings::IniFormat);

  // overwrite settings only if they are actually are available or haven't been set
  // before
  if ((gameName != "") || !settingsFile.contains("gameName")) {
    settingsFile.setValue("gameName", gameName);
  }
  if ((modID != 0) || !settingsFile.contains("modid")) {
    settingsFile.setValue("modid", modID);
  }
  if (!settingsFile.contains("version") ||
      (!version.isEmpty() &&
       (!merge || (VersionInfo(version) >=
                   VersionInfo(settingsFile.value("version").toString()))))) {
    settingsFile.setValue("version", version);
  }
  if (!newestVersion.isEmpty() || !settingsFile.contains("newestVersion")) {
    settingsFile.setValue("newestVersion", newestVersion);
  }
  // issue #51 used to overwrite the manually set categories
  if (!settingsFile.contains("category")) {
    settingsFile.setValue("category", QString::number(categoryID));
  }
  settingsFile.setValue("nexusFileStatus", fileCategoryID);
  settingsFile.setValue("installationFile", m_CurrentFile);
  settingsFile.setValue("repository", repository);

  if (!merge) {
    // this does not clear the list we have in memory but the mod is going to have to be
    // re-read anyway btw.: installedFiles were written with beginWriteArray but we can
    // still clear it with beginGroup. nice
    settingsFile.beginGroup("installedFiles");
    settingsFile.remove("");
    settingsFile.endGroup();
  }

  discardReplacedMod();
  m_NewModFolder.clear();

  return result;
}

bool InstallationManager::wasCancelled() const
{
  return !m_PasswordCancelled &&
         m_ArchiveHandler->getLastError() == Archive::Error::ERROR_EXTRACT_CANCELLED;
}

bool InstallationManager::isRunning() const
{
  return m_IsRunning;
}

InstallationManager::WinRARResult
InstallationManager::tryWinRARFallback(const QString& fileName)
{
  // a wrong password also ends in "unsupported format"; WinRAR, which never
  // asks for one here, would fail too
  if (m_PasswordAsked || m_PasswordRejected) {
    return WinRARResult::NotTried;
  }
  // only when the archive is there but 7-Zip couldn't read it
  switch (m_ArchiveHandler->getLastError()) {
  case Archive::Error::ERROR_FAILED_TO_OPEN_ARCHIVE:
  case Archive::Error::ERROR_INVALID_ARCHIVE_FORMAT:
  case Archive::Error::ERROR_ARCHIVE_INVALID:
  case Archive::Error::ERROR_LIBRARY_ERROR:
    break;
  default:
    return WinRARResult::NotTried;
  }
  if (!WinRARFallback::canHandle(fileName)) {
    return WinRARResult::NotTried;
  }
  const QString winrar = WinRARFallback::findWinRAR();
  if (winrar.isEmpty()) {
    log::debug("WinRAR is not installed; no other way to open {}", fileName);
    return WinRARResult::NotTried;
  }

  // the main window, also when MO2 isn't in front (a download finishing
  // while the game runs)
  QWidget* parent = m_ParentWidget != nullptr ? m_ParentWidget : qApp->activeWindow();
  const auto answer = QuestionBoxMemory::query(
      parent, "winrarFallback", tr("Open with WinRAR"),
      tr("7-Zip, which MO2 uses for archives, can't open %1.\n\nWinRAR is "
         "installed on this PC and may be able to. Extract the archive with WinRAR "
         "and install it from there?")
          .arg(QFileInfo(fileName).fileName()),
      QDialogButtonBox::Yes | QDialogButtonBox::No, QDialogButtonBox::Yes);
  if (answer != QDialogButtonBox::Yes) {
    return WinRARResult::NotTried;
  }

  WinRARFallback::removeLeftovers();
  const QString folder = QDir::temp().absoluteFilePath(
      WinRARFallback::folderPrefix() + QUuid::createUuid().toString(QUuid::WithoutBraces));
  QString error;
  bool incomplete = false;
  if (!WinRARFallback::extract(parent, winrar, fileName, folder, error, incomplete)) {
    QDir(folder).removeRecursively();
    if (!error.isEmpty()) {
      log::error("WinRAR could not extract {}: {}", fileName, error);
      reportError(tr("WinRAR could not extract the archive: %1").arg(error));
    }
    return WinRARResult::Stopped;
  }
  if (incomplete) {
    QMessageBox::warning(parent, tr("Open with WinRAR"),
                         tr("WinRAR extracted the archive with errors: some of its files "
                            "may be missing from the mod."));
  }

  auto folderArchive = std::make_unique<FolderArchive>();
  if (!folderArchive->open(folder.toStdWString(), {})) {
    QDir(folder).removeRecursively();
    return WinRARResult::NotTried;
  }
  log::info("extracted {} with WinRAR ({}) into {}", fileName, winrar, folder);
  m_SevenZipHandler = std::move(m_ArchiveHandler);
  m_ArchiveHandler  = std::move(folderArchive);
  m_WinRARFolder    = folder;
  return WinRARResult::Opened;
}

void InstallationManager::endWinRARFallback()
{
  if (m_SevenZipHandler) {
    m_ArchiveHandler->close();
    m_ArchiveHandler = std::move(m_SevenZipHandler);
  }
  if (!m_WinRARFolder.isEmpty()) {
    if (!QDir(m_WinRARFolder).removeRecursively()) {
      log::warn("could not remove the WinRAR folder {}", m_WinRARFolder);
    }
    m_WinRARFolder.clear();
  }
}

void InstallationManager::postInstallCleanup()
{
  // Clear the list of created files:
  m_CreatedFiles.clear();

  // Close the archive:
  m_ArchiveHandler->close();
  endWinRARFallback();

  // directories we may want to remove. sorted from longest to shortest to ensure we
  // remove subdirectories first.
  auto longestFirst = [](const QString& LHS, const QString& RHS) -> bool {
    if (LHS.size() != RHS.size())
      return LHS.size() > RHS.size();
    else
      return LHS < RHS;
  };

  std::set<QString, std::function<bool(const QString&, const QString&)>>
      directoriesToRemove(longestFirst);

  // clean up temp files
  // TODO: this doesn't yet remove directories. Also, the files may be left there if
  // this point isn't reached
  for (const QString& tempFile : m_TempFilesToDelete) {
    QFileInfo fileInfo(QDir::tempPath() + "/" + tempFile);
    if (fileInfo.exists()) {
      if (!fileInfo.isReadable() || !fileInfo.isWritable()) {
        QFile::setPermissions(fileInfo.absoluteFilePath(),
                              QFile::ReadOther | QFile::WriteOther);
      }
      if (!QFile::remove(fileInfo.absoluteFilePath())) {
        log::warn("Unable to delete {}", fileInfo.absoluteFilePath());
      }
    }
    directoriesToRemove.insert(fileInfo.absolutePath());
  }

  m_TempFilesToDelete.clear();

  // try to delete each directory we had temporary files in. the call fails for
  // non-empty directories which is ok
  for (const QString& dir : directoriesToRemove) {
    QDir().rmdir(dir);
  }
}

InstallationResult InstallationManager::install(const QString& fileName,
                                                GuessedValue<QString>& modName,
                                                int modID)
{
  m_IsRunning = true;
  ON_BLOCK_EXIT([this]() {
    m_IsRunning = false;
    // a mod a custom installer replaced with createMod(), if nothing finished it
    restoreReplacedMod();
  });

  QFileInfo fileInfo(fileName);
  if (!getSupportedExtensions().contains(fileInfo.suffix(), Qt::CaseInsensitive)) {
    reportError(tr("File format \"%1\" not supported").arg(fileInfo.suffix()));
    return InstallationResult(IPluginInstaller::RESULT_FAILED);
  }

  modName.setFilter(&fixDirectoryName);

  modName.update(QFileInfo(fileName).completeBaseName(), GUESS_FALLBACK);

  // read out meta information from the download if available
  QString gameName      = "";
  QString version       = "";
  QString newestVersion = "";
  int category          = 0;
  int categoryID        = 0;
  int fileCategoryID    = 1;
  // Archives installed from a local file are not known to come from Nexus.
  // Keep them unclassified unless the download metadata identifies a source.
  QString repository;

  QString metaName = fileName + ".meta";
  if (QFile(metaName).exists()) {
    QSettings metaFile(metaName, QSettings::IniFormat);
    gameName = metaFile.value("gameName", "").toString();
    modID    = metaFile.value("modID", 0).toInt();
    QTextDocument doc;
    doc.setHtml(metaFile.value("name", "").toString());
    modName.update(doc.toPlainText(), GUESS_FALLBACK);
    modName.update(metaFile.value("modName", "").toString(), GUESS_META);

    version                    = metaFile.value("version", "").toString();
    newestVersion              = metaFile.value("newestVersion", "").toString();
    category                   = metaFile.value("category", 0).toInt();
    unsigned int categoryIndex = CategoryFactory::instance().resolveNexusID(category);
    if (category != 0 && categoryIndex == 0U &&
        Settings::instance().nexus().categoryMappings()) {
      // an install doesn't stop for this: with no mappings it would ask on every
      // Nexus download. The categories editor and Settings stay for who wants them
      log::debug("Nexus category {} has no mapping; installing without a category.",
                 category);
    } else {
      categoryID = CategoryFactory::instance().getCategoryID(categoryIndex);
    }
    if (metaFile.contains("repository")) {
      repository = metaFile.value("repository").toString();
    } else if (metaFile.value("modID", 0).toInt() > 0 &&
               metaFile.value("fileID", 0).toInt() > 0) {
      // Older MO2 download metadata did not always store the repository name.
      // A paired Nexus mod/file ID is enough to retain that legacy source.
      repository = "Nexus";
    }
    fileCategoryID = metaFile.value("fileCategory", 1).toInt();
  }

  // a file downloaded by hand has no meta file, but Nexus's name says the version
  if (version.isEmpty()) {
    QString nameInFile;
    int idInFile = -1;
    NexusInterface::parseCurrentNexusFileName(fileInfo.fileName(), nameInFile, idInFile,
                                              version);
  }

  if (version.isEmpty()) {
    QDateTime lastMod = fileInfo.lastModified();
    version           = "d" + lastMod.toString("yyyy.M.d");
  }

  {  // guess the mod name and mod if from the file name if there was no meta
     // information
    QString guessedModName;
    int guessedModID = modID;
    NexusInterface::interpretNexusFileName(QFileInfo(fileName).fileName(),
                                           guessedModName, guessedModID, false);
    if ((modID == 0) && (guessedModID != -1)) {
      modID = guessedModID;
    } else if (modID != guessedModID) {
      log::debug("passed mod id: {}, guessed id: {}", modID, guessedModID);
    }

    modName.update(guessedModName, GUESS_GOOD);
  }

  m_CurrentFile = fileInfo.absoluteFilePath();
  if (fileInfo.dir() == QDir(m_DownloadsDirectory)) {
    m_CurrentFile = fileInfo.fileName();
  }
  log::debug("using mod name \"{}\" (id {}) -> {}", QString(modName), modID,
             m_CurrentFile);

  // If there's an archive already open, close it. This happens with the bundle
  // installer when it uncompresses a split archive, then finds it has a real archive
  // to deal with.
  endWinRARFallback();
  m_ArchiveHandler->close();

  // open the archive and construct the directory tree the installers work on

  m_PasswordAsked     = false;
  m_PasswordCancelled = false;
  m_PasswordRejected  = false;
  m_ReplacedRestored  = false;
  m_NewModRemoved     = false;
  const auto askPassword = [this]() -> std::wstring {
    m_Password = QString();

    // cancelled once: the other files of the archive don't ask again
    if (m_PasswordCancelled) {
      return {};
    }
    m_PasswordAsked = true;

    // Note: If we are not in the Qt event thread, we cannot use queryPassword()
    // directly, so we emit passwordRequested() that is connected to
    // queryPassword(). The connection is made using Qt::BlockingQueuedConnection,
    // so the emit "call" is actually blocking. We cannot use emit if we are in the
    // even thread, otherwize we have a deadlock.
    if (QThread::currentThread() != QApplication::instance()->thread()) {
      emit passwordRequested();
    } else {
      queryPassword();
    }
    return m_Password.toStdWString();
  };

  bool archiveOpen = m_ArchiveHandler->open(fileName.toStdWString(), askPassword);
  // a wrong password keeps the archive from opening
  // a few tries: a damaged encrypted archive never opens, whatever is typed
  for (int tries = 1; !archiveOpen && m_PasswordAsked && !m_PasswordCancelled && tries < 5;
       ++tries) {
    m_PasswordRejected = true;
    m_PasswordAsked    = false;
    m_ArchiveHandler->close();
    archiveOpen = m_ArchiveHandler->open(fileName.toStdWString(), askPassword);
  }
  if (!archiveOpen) {
    log::debug("integrated archiver can't open {}: {} ({})", fileName,
               getErrorString(m_ArchiveHandler->getLastError()),
               m_ArchiveHandler->getLastError());
  }
  ON_BLOCK_EXIT(std::bind(&InstallationManager::postInstallCleanup, this));

  if (!archiveOpen && m_PasswordCancelled) {
    return InstallationResult(IPluginInstaller::RESULT_CANCELED);
  }

  if (!archiveOpen) {
    switch (tryWinRARFallback(fileName)) {
    case WinRARResult::Opened:
      archiveOpen = true;
      break;
    case WinRARResult::Stopped:
      return InstallationResult(IPluginInstaller::RESULT_CANCELED);
    case WinRARResult::NotTried:
      break;
    }
  }

  std::shared_ptr<IFileTree> filesTree =
      archiveOpen ? ArchiveFileTree::makeTree(*m_ArchiveHandler) : nullptr;

  // MO2 Revamped: the script extender's loader goes next to the game's exe
  if (filesTree != nullptr) {
    if (auto result = installScriptExtender(filesTree, modName, gameName, modID, version,
                                            newestVersion, categoryID, fileCategoryID,
                                            repository)) {
      return *result;
    }
  }

  auto installers = m_PluginContainer->plugins<IPluginInstaller>();

  std::sort(installers.begin(), installers.end(),
            [](IPluginInstaller* lhs, IPluginInstaller* rhs) {
              return lhs->priority() > rhs->priority();
            });

  InstallationResult installResult(IPluginInstaller::RESULT_NOTATTEMPTED);

  for (IPluginInstaller* installer : installers) {
    // don't use inactive installers (installer can't be null here but vc static code
    // analysis thinks it could)
    if ((installer == nullptr) || !m_PluginContainer->isEnabled(installer)) {
      continue;
    }

    // try only manual installers if that was requested
    if (installResult.result() == IPluginInstaller::RESULT_MANUALREQUESTED) {
      if (!installer->isManualInstaller()) {
        continue;
      }
    } else if (installResult.result() != IPluginInstaller::RESULT_NOTATTEMPTED) {
      break;
    }

    try {
      {  // simple case
        IPluginInstallerSimple* installerSimple =
            dynamic_cast<IPluginInstallerSimple*>(installer);
        if ((installerSimple != nullptr) && (filesTree != nullptr) &&
            (installer->isArchiveSupported(filesTree))) {
          installResult.m_result =
              installerSimple->install(modName, filesTree, version, modID);
          if (installResult) {

            // Downcast to an actual ArchiveFileTree and map to the archive. Test if
            // the tree is still an ArchiveFileTree, otherwize it means the installer
            // did some bad stuff.
            ArchiveFileTree* p = dynamic_cast<ArchiveFileTree*>(filesTree.get());
            if (p == nullptr) {
              throw IncompatibilityException(
                  tr("Invalid file tree returned by plugin."));
            }

            // Detach the file tree (this ensure the parent is null and call to path()
            // stops at this root):
            p->detach();

            p->mapToArchive(*m_ArchiveHandler);

            // Clean the created files:
            cleanCreatedFiles(filesTree);

            // the simple installer only prepares the installation, the rest
            // works the same for all installers
            installResult = doInstall(modName, gameName, modID, version, newestVersion,
                                      categoryID, fileCategoryID, repository);
          }
        }
      }

      if (installResult.result() != IPluginInstaller::RESULT_CANCELED) {  // custom case
        IPluginInstallerCustom* installerCustom =
            dynamic_cast<IPluginInstallerCustom*>(installer);
        if ((installerCustom != nullptr) &&
            (((filesTree != nullptr) && installer->isArchiveSupported(filesTree)) ||
             ((filesTree == nullptr) &&
              installerCustom->isArchiveSupported(fileName)))) {
          std::set<QString> installerExt = installerCustom->supportedExtensions();
          if (installerExt.find(fileInfo.suffix()) != installerExt.end()) {
            installResult.m_result =
                installerCustom->install(modName, gameName, fileName, version, modID);
            if (installResult.result() == IPluginInstaller::RESULT_SUCCESS ||
                installResult.result() == IPluginInstaller::RESULT_SUCCESSCANCEL) {
              discardReplacedMod();
            } else {
              restoreReplacedMod();
            }
            unsigned int idx = ModInfo::getIndex(modName);
            if (idx != UINT_MAX) {
              ModInfo::Ptr info = ModInfo::getByIndex(idx);
              info->setRepository(repository);
            }
          }
        }
      }
    } catch (const IncompatibilityException& e) {
      log::error("plugin \"{}\" incompatible: {}", installer->name(), e.what());
    }

    // act upon the installation result. at this point the files have already been
    // extracted to the correct location
    switch (installResult.result()) {
    case IPluginInstaller::RESULT_FAILED: {
      QMessageBox::warning(qApp->activeWindow(), tr("Installation failed"),
                           tr("The mod could not be installed. The log has the details."),
                           QMessageBox::Ok);
      return installResult;
    } break;
    case IPluginInstaller::RESULT_SUCCESS:
    case IPluginInstaller::RESULT_SUCCESSCANCEL: {
      if (filesTree != nullptr) {
        auto iniTweakEntry = filesTree->find("INI Tweaks", FileTreeEntry::DIRECTORY);
        installResult.m_iniTweaks =
            iniTweakEntry != nullptr && !iniTweakEntry->astree()->empty();
      }
      installResult.m_result = IPluginInstaller::RESULT_SUCCESS;
      return installResult;
    } break;
    case IPluginInstaller::RESULT_NOTATTEMPTED:
    case IPluginInstaller::RESULT_MANUALREQUESTED: {
      continue;
    }
    default:
      return installResult;
    }
  }
  if (installResult.result() == IPluginInstaller::RESULT_NOTATTEMPTED) {
    reportError(
        tr("None of the available installer plugins were able to handle that archive.\n"
           "This is likely due to a corrupted or incompatible download or unrecognized "
           "archive format."));
  }

  return installResult;
}

std::optional<InstallationResult> InstallationManager::installScriptExtender(
    std::shared_ptr<IFileTree> tree, GuessedValue<QString>& modName,
    const QString& gameName, int modID, const QString& version,
    const QString& newestVersion, int categoryID, int fileCategoryID,
    const QString& repository)
{
  const IPluginGame* game = m_Game ? m_Game() : nullptr;
  const auto extender     = m_ScriptExtender ? m_ScriptExtender() : nullptr;
  if (game == nullptr || extender == nullptr) {
    return std::nullopt;
  }
  auto layout = ScriptExtenderSetup::detect(tree, *extender);
  if (!layout || layout->gameFiles.empty()) {
    return std::nullopt;
  }

  QWidget* parent = m_ParentWidget != nullptr ? m_ParentWidget : qApp->activeWindow();
  const auto choice = ScriptExtenderSetup::ask(parent, *extender, *game, *layout);
  switch (choice) {
  case ScriptExtenderSetup::Choice::RegularMod:
    log::info("{}: installed as a regular mod", m_CurrentFile);
    return std::nullopt;
  case ScriptExtenderSetup::Choice::Cancel:
    return InstallationResult(IPluginInstaller::RESULT_CANCELED);
  default:
    break;
  }

  // its name, not the one Nexus's file naming makes of skse64_2_02_06
  modName.update(ScriptExtenderSetup::displayName(*extender), GUESS_GOOD);

  // where the plugin looks for the loader: the game folder for these games
  const QDir gameDir = ScriptExtenderSetup::loaderDir(*extender);
  QStringList fileNames;
  for (const auto& entry : layout->gameFiles) {
    fileNames.append(entry->name());
  }

  // the loader and DLLs first, into the temporary folder: the mod's files
  // are mapped to the archive after them
  QStringList extracted;
  if (choice == ScriptExtenderSetup::Choice::GameFolder) {
    extracted = extractFiles(layout->gameFiles, true);
    if (extracted.size() != fileNames.size()) {
      return InstallationResult(IPluginInstaller::RESULT_CANCELED);
    }
  }

  // the mod: what the Data folder holds, or only the record of the files
  std::shared_ptr<IFileTree> modTree;
  if (layout->data != nullptr) {
    layout->data->detach();
    modTree = layout->data;
  } else {
    tree->removeIf([](const auto&) {
      return true;
    });
    modTree = tree;
  }
  auto* archiveTree = dynamic_cast<ArchiveFileTree*>(modTree.get());
  if (archiveTree == nullptr) {
    return InstallationResult(IPluginInstaller::RESULT_FAILED);
  }
  archiveTree->mapToArchive(*m_ArchiveHandler);

  InstallationResult result = doInstall(modName, gameName, modID, version, newestVersion,
                                        categoryID, fileCategoryID, repository);
  if (!result) {
    return result;
  }

  if (choice == ScriptExtenderSetup::Choice::Myself) {
    ScriptExtenderSetup::showManualSteps(parent, *extender, gameDir, fileNames);
    return result;
  }

  QList<QPair<QString, QString>> files;
  for (int i = 0; i < fileNames.size(); ++i) {
    files.append({fileNames[i], extracted[i]});
  }
  const QString error = ScriptExtenderSetup::copyToGame(
      files, gameDir, ScriptExtenderSetup::filePrefix(*extender), nullptr);
  if (!error.isEmpty()) {
    ScriptExtenderSetup::showManualSteps(parent, *extender, gameDir, fileNames, error);
    return result;
  }
  ScriptExtenderSetup::writeRecord(QDir(m_ModsDirectory).absoluteFilePath(modName),
                                   gameDir, fileNames);
  return result;
}

QString InstallationManager::getErrorString(Archive::Error errorCode)
{
  switch (errorCode) {
  case Archive::Error::ERROR_NONE: {
    return tr("no error");
  } break;
  case Archive::Error::ERROR_LIBRARY_NOT_FOUND: {
    return tr("7z.dll not found");
  } break;
  case Archive::Error::ERROR_LIBRARY_INVALID: {
    return tr("7z.dll isn't valid");
  } break;
  case Archive::Error::ERROR_ARCHIVE_NOT_FOUND: {
    return tr("archive not found");
  } break;
  case Archive::Error::ERROR_FAILED_TO_OPEN_ARCHIVE: {
    return tr("failed to open archive");
  } break;
  case Archive::Error::ERROR_INVALID_ARCHIVE_FORMAT: {
    return tr("unsupported archive type");
  } break;
  case Archive::Error::ERROR_LIBRARY_ERROR: {
    return tr("internal library error");
  } break;
  case Archive::Error::ERROR_ARCHIVE_INVALID: {
    return tr("archive invalid");
  } break;
  default: {
    // this probably means the archiver.dll is newer than this
    return tr("unknown archive error");
  } break;
  }
}

QStringList InstallationManager::getSupportedExtensions() const
{
  std::set<QString, CaseInsensitive> supportedExtensions(
      {"zip", "rar", "7z", "fomod", "001"});
  for (auto* installer : m_PluginContainer->plugins<IPluginInstaller>()) {
    if (m_PluginContainer->isEnabled(installer)) {
      if (auto* installerCustom = dynamic_cast<IPluginInstallerCustom*>(installer)) {
        std::set<QString> extensions = installerCustom->supportedExtensions();
        supportedExtensions.insert(extensions.begin(), extensions.end());
      }
    }
  }
  return QStringList(supportedExtensions.begin(), supportedExtensions.end());
}

void InstallationManager::notifyInstallationStart(QString const& archive,
                                                  bool reinstallation,
                                                  ModInfo::Ptr currentMod)
{
  auto& installers = m_PluginContainer->plugins<IPluginInstaller>();
  for (auto* installer : installers) {
    if (m_PluginContainer->isEnabled(installer)) {
      installer->onInstallationStart(archive, reinstallation, currentMod.get());
    }
  }
}

void InstallationManager::notifyInstallationEnd(const InstallationResult& result,
                                                ModInfo::Ptr newMod)
{
  auto& installers = m_PluginContainer->plugins<IPluginInstaller>();
  for (auto* installer : installers) {
    if (m_PluginContainer->isEnabled(installer)) {
      installer->onInstallationEnd(result.result(), newMod.get());
    }
  }
}
