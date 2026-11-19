#include "wabbajackinstance.h"

#include "fluentdialogs.h"
#include "organizercore.h"
#include "settings.h"
#include "wabbajackinstaller.h"

#include <iplugingame.h>
#include <log.h>
#include <utility.h>

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDateTime>
#include <QDesktopServices>
#include <QDirIterator>
#include <QHBoxLayout>
#include <QTemporaryFile>
#include <QUrl>
#include <QDir>
#include <QLabel>
#include <QPushButton>
#include <QSet>
#include <QSettings>
#include <QVBoxLayout>

#include <Windows.h>
#include <shellapi.h>

using namespace MOBase;

namespace Wabbajack
{

namespace
{

struct Text
{
  Q_DECLARE_TR_FUNCTIONS(WabbajackInstance)
};

constexpr auto ListKey        = "Wabbajack/list";
constexpr auto GameVersionKey = "Wabbajack/game_version";
constexpr auto ReadmeKey      = "Wabbajack/readme";
constexpr auto ManualKey      = "Wabbajack/manual_folders";
constexpr auto NoticeKey      = "Wabbajack/notice_pending";

// the list's readme is opened by the shell: only a web address, never file:,
// a network share or another protocol handler a list could point it at
bool isWebLink(const QString& text)
{
  const QUrl url(text);
  const QString scheme = url.scheme().toLower();
  return url.isValid() && (scheme == u"http" || scheme == u"https") &&
         !url.host().isEmpty();
}

InstanceInfo readInfo(const QSettings& ini)
{
  InstanceInfo info;
  info.list          = ini.value(ListKey).toString();
  info.gameVersion   = ini.value(GameVersionKey).toString();
  info.readme        = ini.value(ReadmeKey).toString();
  if (!isWebLink(info.readme)) {
    info.readme.clear();
  }
  info.manualFolders = ini.value(ManualKey).toStringList();
  return info;
}

// 1.7.0.3 and 1.7.0.30 differ, 1.7 and 1.7.0.0 don't
QList<int> numbers(const QString& version)
{
  QList<int> result;
  for (const QString& part : version.trimmed().split(u'.')) {
    bool ok       = false;
    const int n   = part.trimmed().toInt(&ok);
    if (!ok) {
      return {};
    }
    result.append(n);
  }
  while (!result.isEmpty() && result.back() == 0) {
    result.removeLast();
  }
  return result;
}

bool sameVersion(const QString& a, const QString& b)
{
  const QList<int> x = numbers(a);
  const QList<int> y = numbers(b);
  // a version that can't be read isn't a difference worth a question
  return x.isEmpty() || y.isEmpty() || x == y;
}

bool inside(const QFileInfo& binary, const QDir& folder)
{
  const QString dir  = QDir::cleanPath(binary.absolutePath());
  const QString base = QDir::cleanPath(folder.absolutePath());
  return dir.compare(base, Qt::CaseInsensitive) == 0 ||
         dir.startsWith(base + u'/', Qt::CaseInsensitive);
}

QLabel* richLabel(const QString& html, QWidget* parent)
{
  auto* label = new QLabel(html, parent);
  label->setTextFormat(Qt::RichText);
  label->setWordWrap(true);
  label->setOpenExternalLinks(false);
  label->setTextInteractionFlags(Qt::TextBrowserInteraction);
  QObject::connect(label, &QLabel::linkActivated, label, [](const QString& link) {
    if (isWebLink(link)) {
      QDesktopServices::openUrl(QUrl(link));
    }
  });
  return label;
}

void fit(QDialog& dialog, int width)
{
  const int height =
      dialog.layout() != nullptr ? dialog.layout()->totalHeightForWidth(width) : -1;
  dialog.resize(width, height > 0 ? height : dialog.sizeHint().height());
}

}  // namespace

void writeInstanceInfo(const QString& iniPath, const InstanceInfo& info)
{
  QSettings ini(iniPath, QSettings::IniFormat);
  ini.setValue(ListKey, info.list);
  ini.setValue(GameVersionKey, info.gameVersion);
  ini.setValue(ReadmeKey, info.readme);
  ini.setValue(ManualKey, info.manualFolders);
  ini.setValue(NoticeKey, true);
  ini.sync();
}

void showListNotice(QWidget* parent, OrganizerCore& core)
{
  const QString file = core.settings().filename();
  QSettings ini(file, QSettings::IniFormat);
  if (!ini.value(NoticeKey, false).toBool()) {
    return;
  }
  // shown once, whatever is answered
  ini.setValue(NoticeKey, false);
  ini.sync();

  const InstanceInfo info = readInfo(ini);
  const IPluginGame* game = core.managedGame();
  const QString have      = game != nullptr ? game->gameVersion() : QString();
  const QString base      = core.settings().paths().base();

  QStringList items;
  if (!info.manualFolders.isEmpty()) {
    QStringList folders;
    for (const QString& folder : info.manualFolders) {
      folders << QStringLiteral("<b>%1</b>").arg(
          QDir::toNativeSeparators(QDir(base).filePath(folder)).toHtmlEscaped());
    }
    items << Text::tr("The list leaves files for you to install by hand, as its guide "
                         "says (usually into the game's folder): %1. The game may not start, "
                         "or the list may not work, until they are in place.")
                 .arg(folders.join(QStringLiteral(", ")));
  }
  if (!info.gameVersion.isEmpty() && !have.isEmpty()) {
    if (sameVersion(info.gameVersion, have)) {
      items << Text::tr("The list was made for version %1 of the game, the version you "
                           "have.")
                   .arg(info.gameVersion.toHtmlEscaped());
    } else {
      items << Text::tr("The list was made for version <b>%1</b> of the game, and you have "
                           "<b>%2</b>. Its script extender and some mods may refuse to start "
                           "until the game is that version; the list's guide says how to get "
                           "it.")
                   .arg(info.gameVersion.toHtmlEscaped(), have.toHtmlEscaped());
    }
  }
  if (items.isEmpty()) {
    return;
  }
  if (!info.readme.isEmpty()) {
    items << Text::tr("The guide: <a href=\"%1\">%2</a>.")
                 .arg(info.readme.toHtmlEscaped(), info.readme.toHtmlEscaped());
  }

  const QString name = info.list.isEmpty() ? Text::tr("The list") : info.list;
  FluentContentDialog dialog(parent, QStringLiteral("WabbajackListNoticeDialog"),
                             Text::tr("Before you start %1").arg(name),
                             Text::tr("What the list's guide still asks of you:"));
  for (const QString& item : items) {
    dialog.body()->addWidget(richLabel(item, &dialog));
  }
  QPushButton* steps = nullptr;
  if (!info.manualFolders.isEmpty()) {
    steps = dialog.addButton(Text::tr("Manual steps…"), true);
    QObject::connect(steps, &QPushButton::clicked, &dialog, &QDialog::accept);
  }
  auto* ok = dialog.addButton(Text::tr("OK"), steps == nullptr);
  QObject::connect(ok, &QPushButton::clicked, &dialog, &QDialog::reject);
  fit(dialog, 560);
  if (dialog.exec() == QDialog::Accepted && steps != nullptr) {
    showManualSteps(parent, core);
  }
}

bool confirmGameVersion(QWidget* parent, OrganizerCore& core, const QFileInfo& binary)
{
  const IPluginGame* game = core.managedGame();
  if (game == nullptr || !inside(binary, game->gameDirectory())) {
    return true;
  }
  const QString file = core.settings().filename();
  const QSettings ini(file, QSettings::IniFormat);
  const InstanceInfo info = readInfo(ini);
  const QString have      = game->gameVersion();
  if (info.gameVersion.isEmpty() || have.isEmpty() || sameVersion(info.gameVersion, have)) {
    return true;
  }

  // asked once per session and version: a game started anyway isn't asked again
  static QSet<QString> asked;
  const QString key = file.toLower() + u'|' + have;
  if (asked.contains(key)) {
    return true;
  }
  log::warn("wabbajack: {} was made for game version {}, the game is {}", info.list,
            info.gameVersion, have);

  const QString name = info.list.isEmpty() ? Text::tr("The list") : info.list;
  FluentContentDialog dialog(
      parent, QStringLiteral("WabbajackGameVersionDialog"),
      Text::tr("The game isn't the version %1 was made for").arg(name),
      Text::tr("%1 was made for version %2 of the game, and the game is now %3 (a game "
                  "update changes it). Its script extender and some of its mods may refuse "
                  "to start, or the game may close at once. The list's guide says how to get "
                  "version %2.")
          .arg(name, info.gameVersion, have));
  if (!info.readme.isEmpty()) {
    dialog.body()->addWidget(richLabel(
        Text::tr("The guide: <a href=\"%1\">%2</a>.")
            .arg(info.readme.toHtmlEscaped(), info.readme.toHtmlEscaped()),
        &dialog));
  }
  auto* start  = dialog.addButton(Text::tr("Start anyway"), true);
  auto* cancel = dialog.addButton(Text::tr("Cancel"));
  QObject::connect(start, &QPushButton::clicked, &dialog, &QDialog::accept);
  QObject::connect(cancel, &QPushButton::clicked, &dialog, &QDialog::reject);
  dialog.askFirst(cancel);
  fit(dialog, 520);
  if (dialog.exec() != QDialog::Accepted) {
    return false;
  }
  asked.insert(key);
  return true;
}

}  // namespace Wabbajack

// ---- the folders the guide has you install by hand ---------------------------

namespace Wabbajack
{

namespace
{

// what a folder of the list's install holds and what is done with it
struct ManualFolder
{
  QString name;  // "__Files Requiring Manual Install"
  QString path;
  // its files go into the game's folder as they are laid out
  // (nvse_loader.exe, Data\NVSE\nvse_config.ini)
  bool forGame = false;
  QStringList files;     // relative to path, for the game's folder
  QStringList programs;  // relative to path: the patchers the guide has you run
  QStringList readmes;   // relative to path
};

bool forGameFolder(const QString& name)
{
  return name.compare(u"__Files Requiring Manual Install", Qt::CaseInsensitive) == 0 ||
         name.compare(u"Game Folder Files", Qt::CaseInsensitive) == 0 ||
         name.compare(u"Manual Game Files", Qt::CaseInsensitive) == 0;
}

// the patchers' own helpers, run by them, not by the user; the script
// extender's loader, which MO2 starts
bool helperProgram(const QString& fileName)
{
  const QString name = fileName.toLower();
  return name.startsWith(u"xdelta") || name.startsWith(u"bsarch") || name.startsWith(u"7z") ||
         name.endsWith(u"_loader.exe");
}

QList<ManualFolder> scanFolders(const QString& base)
{
  QList<ManualFolder> result;
  for (const QString& name : byHandFolders(base)) {
    ManualFolder folder;
    folder.name    = name;
    folder.path    = QDir(base).filePath(name);
    folder.forGame = forGameFolder(name);
    const QDir dir(folder.path);
    QDirIterator it(folder.path, QDir::Files | QDir::Hidden, QDirIterator::Subdirectories);
    while (it.hasNext()) {
      const QString relative = dir.relativeFilePath(it.next());
      const QString lower    = relative.toLower();
      const bool top         = !relative.contains(u'/');
      // in the game's files, every program but the helpers (FNVpatch.exe,
      // Patcher.exe); in a folder of its own only a patcher or decompressor
      // the guide runs from there (GECK.exe in "__Geck Patcher" is a file to
      // copy, not one to start)
      if (top && lower.endsWith(u".exe") && !helperProgram(QFileInfo(relative).fileName()) &&
          (folder.forGame || lower.contains(u"patch") || lower.contains(u"decompress"))) {
        folder.programs.append(relative);
      }
      if (top && (lower.contains(u"readme") || lower.contains(u"read me")) &&
          (lower.endsWith(u".txt") || lower.endsWith(u".md") || lower.endsWith(u".html"))) {
        folder.readmes.append(relative);
      }
      // the script extender's sources and debug symbols aren't for the game
      if (lower.startsWith(u"src/") || lower.endsWith(u".pdb")) {
        continue;
      }
      folder.files.append(relative);
    }
    folder.files.sort(Qt::CaseInsensitive);
    folder.programs.sort(Qt::CaseInsensitive);
    folder.readmes.sort(Qt::CaseInsensitive);
    result.append(folder);
  }
  return result;
}

QByteArray digest(const QString& path)
{
  QFile file(path);
  if (!file.open(QIODevice::ReadOnly)) {
    return {};
  }
  QCryptographicHash hash(QCryptographicHash::Sha256);
  hash.addData(&file);
  return hash.result();
}

// the folder's files into the game's folder. What they replace is copied
// first into the install's state folder; a failure on the way puts the
// replaced files back. Returns an error for people, empty when it worked.
QString copyIntoGame(const ManualFolder& folder, const QDir& gameDir, const QString& base,
                     int* copied, QString* backupFolder)
{
  *copied = 0;
  {
    QTemporaryFile probe(gameDir.absoluteFilePath(QStringLiteral("mo2-write-test")));
    if (!probe.open()) {
      log::error("wabbajack: can't write in the game folder {}: {}", gameDir.absolutePath(),
                 probe.errorString());
      return Text::tr("Windows doesn't let MO2 write in the game folder (%1). Start MO2 as "
                      "administrator, or copy the files yourself.")
          .arg(QDir::toNativeSeparators(gameDir.absolutePath()));
    }
  }

  struct Item
  {
    QString from, to, backup;
    bool exists = false;
  };
  const QString stamp =
      QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd-HHmmss"));
  const QDir backups(
      QDir(installStateFolder(base)).filePath(QStringLiteral("game-backup/") + stamp));
  QList<Item> items;
  for (const QString& relative : folder.files) {
    Item item;
    item.from   = QDir(folder.path).filePath(relative);
    item.to     = gameDir.absoluteFilePath(relative);
    item.exists = QFileInfo::exists(item.to);
    if (item.exists) {
      if (digest(item.from) == digest(item.to)) {
        continue;  // already there
      }
      QFile existing(item.to);
      if (!existing.open(QIODevice::ReadWrite)) {
        return Text::tr("%1 is in use and can't be replaced: close the game and try again.")
            .arg(QDir::toNativeSeparators(item.to));
      }
      item.backup = backups.filePath(relative);
    }
    items.append(item);
  }
  if (items.isEmpty()) {
    return {};
  }

  // the backups first: nothing in the game's folder changes until they are made
  for (const Item& item : items) {
    if (item.backup.isEmpty()) {
      continue;
    }
    QDir().mkpath(QFileInfo(item.backup).absolutePath());
    if (!QFile::copy(item.to, item.backup)) {
      log::error("wabbajack: can't back up {} to {}", item.to, item.backup);
      return Text::tr("%1 couldn't be backed up, so nothing was copied.")
          .arg(QDir::toNativeSeparators(item.to));
    }
  }

  QList<const Item*> done;
  const auto rollBack = [&] {
    for (const Item* item : done) {
      QFile::remove(item->to);
      if (!item->backup.isEmpty()) {
        QFile::copy(item->backup, item->to);
      }
    }
  };
  for (const Item& item : items) {
    QDir().mkpath(QFileInfo(item.to).absolutePath());
    const QString staged = item.to + QStringLiteral(".mo2new");
    QFile::remove(staged);
    if (!QFile::copy(item.from, staged) || (item.exists && !QFile::remove(item.to)) ||
        !QFile::rename(staged, item.to)) {
      log::error("wabbajack: can't copy {} to {}", item.from, item.to);
      QFile::remove(staged);
      // a file removed but not replaced comes back with the others
      if (item.exists && !QFileInfo::exists(item.to)) {
        QFile::copy(item.backup, item.to);
      }
      rollBack();
      return Text::tr("%1 couldn't be copied into the game folder; the files already copied "
                      "were put back.")
          .arg(QDir::toNativeSeparators(item.to));
    }
    done.append(&item);
    log::info("wabbajack: copied {} into the game folder{}", item.to,
              item.backup.isEmpty() ? QString() : QStringLiteral(" (backed up)"));
  }
  *copied = int(items.size());
  for (const Item& item : items) {
    if (!item.backup.isEmpty()) {
      *backupFolder = backups.absolutePath();
      break;
    }
  }
  return {};
}

// a program of the list, outside MO2's virtual folder, as the guide has it run;
// through the shell, so one that asks for administrator rights gets them
bool runProgram(const QString& program, const QString& folder)
{
  const std::wstring file = QDir::toNativeSeparators(program).toStdWString();
  const std::wstring dir  = QDir::toNativeSeparators(folder).toStdWString();
  SHELLEXECUTEINFOW info{};
  info.cbSize      = sizeof(info);
  info.fMask       = SEE_MASK_NOASYNC;
  info.lpVerb      = L"open";
  info.lpFile      = file.c_str();
  info.lpDirectory = dir.c_str();
  info.nShow       = SW_SHOWNORMAL;
  const bool ok    = ShellExecuteExW(&info) != FALSE;
  if (ok) {
    log::info("wabbajack: started {} in {}", program, folder);
  } else {
    log::warn("wabbajack: {} couldn't be started ({})", program, GetLastError());
  }
  return ok;
}

bool confirmRun(QWidget* parent, const QString& program, const QString& folder)
{
  const QString name = QFileInfo(program).fileName();
  FluentContentDialog dialog(
      parent, QStringLiteral("WabbajackRunPatcherDialog"), Text::tr("Run %1?").arg(name),
      Text::tr("%1 comes with the list, and its guide has you run it. It starts in %2, "
               "outside MO2's virtual folder, and changes the game's own files as the guide "
               "says (it may ask for administrator rights). MO2 Revamped doesn't check what "
               "it does: follow the list's guide, and keep a backup of the game folder if "
               "the guide doesn't make one.")
          .arg(name, QDir::toNativeSeparators(folder)));
  auto* run    = dialog.addButton(Text::tr("Run"), true);
  auto* cancel = dialog.addButton(Text::tr("Cancel"));
  QObject::connect(run, &QPushButton::clicked, &dialog, &QDialog::accept);
  QObject::connect(cancel, &QPushButton::clicked, &dialog, &QDialog::reject);
  dialog.askFirst(cancel);
  fit(dialog, 520);
  return dialog.exec() == QDialog::Accepted;
}

QString shortList(const QStringList& files, int shown)
{
  QStringList names;
  for (int i = 0; i < files.size() && i < shown; ++i) {
    names.append(QDir::toNativeSeparators(files[i]).toHtmlEscaped());
  }
  QString text = names.join(QStringLiteral(", "));
  if (files.size() > shown) {
    text += QStringLiteral(" ") + Text::tr("and %n more", nullptr, int(files.size()) - shown);
  }
  return text;
}

}  // namespace

bool hasManualSteps(OrganizerCore& core)
{
  const QString base = core.settings().paths().base();
  return isFinishedInstall(base) && !byHandFolders(base).isEmpty();
}

void showManualSteps(QWidget* parent, OrganizerCore& core)
{
  const QString base      = core.settings().paths().base();
  const IPluginGame* game = core.managedGame();
  if (game == nullptr) {
    return;
  }
  const QDir gameDir                = game->gameDirectory();
  const QList<ManualFolder> folders = scanFolders(base);
  const QSettings ini(core.settings().filename(), QSettings::IniFormat);
  const InstanceInfo info = readInfo(ini);
  const QString name      = info.list.isEmpty() ? Text::tr("The list") : info.list;

  FluentContentDialog dialog(
      parent, QStringLiteral("WabbajackManualStepsDialog"),
      Text::tr("Manual steps of %1").arg(name),
      Text::tr("The list leaves these folders in %1 for you, as Wabbajack does. Follow its "
               "guide for each one; MO2 Revamped can copy the files that go into the game's "
               "folder (%2) and start the programs the guide has you run.")
          .arg(QDir::toNativeSeparators(base),
               QDir::toNativeSeparators(gameDir.absolutePath())));

  if (folders.isEmpty()) {
    dialog.body()->addWidget(richLabel(Text::tr("Nothing is left to do by hand."), &dialog));
  }

  for (const ManualFolder& folder : folders) {
    QString text = QStringLiteral("<b>%1</b><br>").arg(folder.name.toHtmlEscaped());
    if (folder.forGame) {
      text += Text::tr("Files for the game's folder (%n files): %1.", nullptr,
                       int(folder.files.size()))
                  .arg(shortList(folder.files, 6));
      if (!folder.programs.isEmpty()) {
        text += QStringLiteral(" ") +
                Text::tr("The guide has you run %1 in the game's folder once they are there.")
                    .arg(shortList(folder.programs, 4));
      }
    } else {
      text += Text::tr("Tools or choices the guide talks about (%n files). MO2 Revamped "
                       "doesn't copy them: only the guide says which one you need.",
                       nullptr, int(folder.files.size()));
    }
    dialog.body()->addWidget(richLabel(text, &dialog));

    auto* row = new QHBoxLayout;
    row->setSpacing(8);
    auto* status = richLabel(QString(), &dialog);
    status->hide();

    if (folder.forGame && !folder.files.isEmpty()) {
      auto* copy = new QPushButton(Text::tr("Copy into the game folder"), &dialog);
      row->addWidget(copy);
      QObject::connect(copy, &QPushButton::clicked, &dialog,
                       [folder, gameDir, base, status, copy] {
                         int copied = 0;
                         QString backupFolder;
                         const QString error =
                             copyIntoGame(folder, gameDir, base, &copied, &backupFolder);
                         if (!error.isEmpty()) {
                           status->setText(error.toHtmlEscaped());
                         } else if (copied == 0) {
                           status->setText(Text::tr("Already in the game folder."));
                           copy->setEnabled(false);
                         } else {
                           QString done = Text::tr("%n files copied into the game folder.",
                                                   nullptr, copied);
                           if (!backupFolder.isEmpty()) {
                             done += QStringLiteral(" ") +
                                     Text::tr("The files they replaced are in %1.")
                                         .arg(QDir::toNativeSeparators(backupFolder)
                                                  .toHtmlEscaped());
                           }
                           status->setText(done);
                           copy->setEnabled(false);
                         }
                         status->show();
                       });
    }
    for (const QString& program : folder.programs) {
      const QString file = QFileInfo(program).fileName();
      auto* run          = new QPushButton(Text::tr("Run %1…").arg(file), &dialog);
      row->addWidget(run);
      // a patcher among the game's files runs from the game's folder, where
      // it finds the exe it patches; one in a folder of its own runs there
      const QString where = folder.forGame ? gameDir.absolutePath() : folder.path;
      const QString exe   = QDir(where).filePath(program);
      QObject::connect(run, &QPushButton::clicked, &dialog, [&dialog, where, exe, file, status] {
        if (!QFileInfo::exists(exe)) {
          status->setText(Text::tr("Copy the files into the game folder first: %1 runs from "
                                   "there.")
                              .arg(file.toHtmlEscaped()));
          status->show();
          return;
        }
        if (!confirmRun(&dialog, exe, where)) {
          return;
        }
        if (!runProgram(exe, where)) {
          status->setText(Text::tr("%1 couldn't be started.").arg(file.toHtmlEscaped()));
          status->show();
        }
      });
    }
    for (const QString& readme : folder.readmes) {
      auto* open =
          new QPushButton(Text::tr("Open %1").arg(QFileInfo(readme).fileName()), &dialog);
      row->addWidget(open);
      const QString path = QDir(folder.path).filePath(readme);
      QObject::connect(open, &QPushButton::clicked, &dialog, [path] {
        QDesktopServices::openUrl(QUrl::fromLocalFile(path));
      });
    }
    auto* show = new QPushButton(Text::tr("Open the folder"), &dialog);
    row->addWidget(show);
    const QString path = folder.path;
    QObject::connect(show, &QPushButton::clicked, &dialog, [path] {
      shell::Explore(QDir(path));
    });
    row->addStretch(1);
    dialog.body()->addLayout(row);
    dialog.body()->addWidget(status);
  }

  if (!info.readme.isEmpty()) {
    dialog.body()->addWidget(richLabel(
        Text::tr("The guide: <a href=\"%1\">%2</a>.")
            .arg(info.readme.toHtmlEscaped(), info.readme.toHtmlEscaped()),
        &dialog));
  }
  auto* close = dialog.addButton(Text::tr("Close"), true);
  QObject::connect(close, &QPushButton::clicked, &dialog, &QDialog::accept);
  fit(dialog, 640);
  dialog.exec();
}

}  // namespace Wabbajack
