#include "scriptextendersetup.h"

#include "fluentchoices.h"
#include "fluentdialogs.h"

#include <scriptextender.h>
#include <iplugingame.h>
#include <log.h>
#include <utility.h>

#include <QCryptographicHash>
#include <QFile>
#include <QLayout>
#include <QFileInfo>
#include <QLabel>
#include <QPushButton>
#include <QRegularExpression>
#include <QSettings>
#include <QTemporaryFile>
#include <QVersionNumber>

using namespace MOBase;

namespace
{

constexpr char16_t GameFolderGlyph = 0xE896;  // Download
constexpr char16_t MyselfGlyph     = 0xE8B7;  // Folder
constexpr char16_t RegularModGlyph = 0xE7B8;  // Package

const QString RecordGroup = QStringLiteral("scriptExtender");

}  // namespace

// at the height its wrapped texts take at that width: the size hint measures
// them narrower, and the room left over spread between them
void ScriptExtenderSetup::fitHeight(QDialog& dialog, int width)
{
  const int height = dialog.layout() != nullptr
                         ? dialog.layout()->totalHeightForWidth(width)
                         : -1;
  dialog.resize(width, height > 0 ? height : dialog.sizeHint().height());
}

namespace
{

bool isGameFile(const QString& name)
{
  return name.endsWith(QLatin1String(".exe"), Qt::CaseInsensitive) ||
         name.endsWith(QLatin1String(".dll"), Qt::CaseInsensitive);
}

// an exe or DLL named like the extender's (skse64_*), or its loader; only a
// name, never a path
bool isExtenderFile(const QString& name, const QString& prefix, const QString& loader)
{
  if (name.isEmpty() || name.contains(QLatin1Char('/')) ||
      name.contains(QLatin1Char('\\')) || !isGameFile(name)) {
    return false;
  }
  if (!loader.isEmpty() && name.compare(loader, Qt::CaseInsensitive) == 0) {
    return true;
  }
  return !prefix.isEmpty() && name.startsWith(prefix, Qt::CaseInsensitive);
}

// a game folder as the record keeps it, to compare two of them
QString folderKey(const QDir& dir)
{
  return QDir::cleanPath(dir.absolutePath()).toLower();
}

QStringList names(const ScriptExtenderSetup::Layout& layout)
{
  QStringList result;
  for (const auto& entry : layout.gameFiles) {
    result.append(entry->name());
  }
  return result;
}

// 1.6.1170.0 -> 1.6.1170, 1.2.0.416 -> 1.2.416: the extenders leave the zeros out
QList<int> significant(const QString& version)
{
  QList<int> parts;
  const auto pieces = version.split(QLatin1Char('.'), Qt::SkipEmptyParts);
  for (int i = 0; i < pieces.size(); ++i) {
    bool ok       = false;
    const int n   = pieces[i].trimmed().toInt(&ok);
    if (!ok) {
      break;
    }
    if (i == 0 || n != 0) {
      parts.append(n);
    }
  }
  return parts;
}

}  // namespace

QString ScriptExtenderSetup::filePrefix(const ScriptExtender& extender)
{
  const QString loader = extender.loaderName();
  const QLatin1String suffix("loader.exe");
  if (!loader.endsWith(suffix, Qt::CaseInsensitive) || loader.size() <= suffix.size()) {
    return {};
  }
  return loader.left(loader.size() - suffix.size()).toLower();
}

QString ScriptExtenderSetup::displayName(const ScriptExtender& extender)
{
  QString prefix = filePrefix(extender);
  while (prefix.endsWith(QLatin1Char('_'))) {
    prefix.chop(1);
  }
  return prefix.isEmpty() ? tr("Script Extender") : prefix.toUpper();
}

QStringList ScriptExtenderSetup::runtimeVersions(const QStringList& fileNames,
                                                 const QString& prefix)
{
  QStringList versions;
  if (prefix.isEmpty()) {
    return versions;
  }
  // skse64_1_6_1170.dll; not skse64_steam_loader.dll or nvse_editor_1_4.dll
  const QRegularExpression runtime(
      QStringLiteral("^%1(\\d+(?:_\\d+)+)\\.dll$")
          .arg(QRegularExpression::escape(prefix)),
      QRegularExpression::CaseInsensitiveOption);
  for (const QString& name : fileNames) {
    const auto match = runtime.match(name);
    if (match.hasMatch()) {
      versions.append(match.captured(1).replace(QLatin1Char('_'), QLatin1Char('.')));
    }
  }
  return versions;
}

bool ScriptExtenderSetup::matchesGame(const QStringList& versions,
                                      const QString& gameVersion)
{
  const QList<int> game = significant(gameVersion);
  if (game.isEmpty()) {
    // unknown: nothing to warn about
    return true;
  }
  for (const QString& version : versions) {
    const QList<int> made = significant(version);
    if (!made.isEmpty() && made.size() <= game.size() &&
        game.mid(0, made.size()) == made) {
      return true;
    }
  }
  return versions.isEmpty();
}

QStringList ScriptExtenderSetup::installedFiles(const QDir& gameDir, const QString& prefix)
{
  QStringList result;
  if (prefix.isEmpty()) {
    return result;
  }
  for (const QString& name : gameDir.entryList({prefix + "*"}, QDir::Files)) {
    if (isGameFile(name)) {
      result.append(name);
    }
  }
  return result;
}

std::optional<ScriptExtenderSetup::Layout>
ScriptExtenderSetup::detect(std::shared_ptr<IFileTree> tree, const ScriptExtender& extender)
{
  const QString loader = extender.loaderName();
  const QString prefix = filePrefix(extender);
  if (tree == nullptr || loader.isEmpty()) {
    return std::nullopt;
  }

  const auto check = [&](const std::shared_ptr<IFileTree>& base) -> std::optional<Layout> {
    if (base->find(loader, FileTreeEntry::FILE) == nullptr) {
      return std::nullopt;
    }
    Layout layout;
    layout.base = base;
    // only the extender's own files: a steam_api64.dll next to them would
    // replace the game's
    for (const auto& entry : *base) {
      if (entry->isFile() && isExtenderFile(entry->name(), prefix, loader)) {
        layout.gameFiles.push_back(entry);
      }
    }
    if (auto data = base->find(QStringLiteral("Data"), FileTreeEntry::DIRECTORY)) {
      layout.data = data->astree();
    }
    return layout;
  };

  if (auto layout = check(tree)) {
    return layout;
  }

  // skse64_2_02_06\skse64_loader.exe: the archive's only folder
  std::shared_ptr<IFileTree> only;
  for (const auto& entry : *tree) {
    if (entry->isDir()) {
      if (only != nullptr) {
        return std::nullopt;
      }
      only = entry->astree();
    }
  }
  return only != nullptr ? check(only) : std::nullopt;
}

ScriptExtenderSetup::Choice ScriptExtenderSetup::ask(QWidget* parent,
                                                     const ScriptExtender& extender,
                                                     const IPluginGame& game,
                                                     const Layout& layout)
{
  const QString name      = displayName(extender);
  const QStringList files = names(layout);

  FluentContentDialog dialog(
      parent, QStringLiteral("ScriptExtenderSetupDialog"),
      tr("Install %1 in the game folder?").arg(name),
      tr("%1 is the game's script extender. Mods that need it work only when the game "
         "starts through its loader, which has to be next to the game's exe with its "
         "DLLs. The rest of the archive goes in as a mod.")
          .arg(name));

  const QString gameVersion = game.gameVersion();
  const QStringList versions = runtimeVersions(files, filePrefix(extender));
  if (!matchesGame(versions, gameVersion)) {
    auto* warning = new QLabel(
        tr("This %1 is for version %2 of the game, and the game is version %3: it won't "
           "start the game. Its download page has a build for each game version.")
            .arg(name, versions.join(QStringLiteral(", ")), gameVersion),
        &dialog);
    warning->setObjectName(QStringLiteral("instanceNameValidation"));
    warning->setWordWrap(true);
    dialog.body()->addWidget(warning);
  }

  enum
  {
    GameFolder = 101,
    Myself,
    RegularMod
  };
  FluentChoices::addOption(
      dialog, GameFolderGlyph, tr("&Install it in the game folder"),
      tr("MO2 copies the loader and its DLLs next to the game's exe and keeps their list "
         "in the mod, so removing the mod can send them to the Recycle Bin. The files "
         "of an older %1 there go to the Recycle Bin.")
          .arg(name),
      GameFolder)
      ->setRecommended(true);
  FluentChoices::addOption(dialog, MyselfGlyph, tr("I'll &copy the files myself"),
                           tr("MO2 installs the rest as a mod and shows which files go "
                              "into the game folder."),
                           Myself);
  FluentChoices::addOption(dialog, RegularModGlyph, tr("Install it as a &regular mod"),
                           tr("The whole archive goes into a mod: for tools "
                              "that put a mod's files in the game folder when it starts, "
                              "such as Root Builder."),
                           RegularMod);
  auto* cancel = dialog.addButton(tr("Cancel"));
  QObject::connect(cancel, &QPushButton::clicked, &dialog, &QDialog::reject);
  fitHeight(dialog, 600);

  switch (dialog.exec()) {
  case GameFolder:
    return Choice::GameFolder;
  case Myself:
    return Choice::Myself;
  case RegularMod:
    return Choice::RegularMod;
  default:
    return Choice::Cancel;
  }
}

QString ScriptExtenderSetup::copyToGame(const QList<QPair<QString, QString>>& files,
                                        const QDir& gameDir, const QString& prefix,
                                        QStringList* removed)
{
  // a game in Program Files needs MO2 to run as administrator
  {
    QTemporaryFile probe(gameDir.absoluteFilePath(QStringLiteral("mo2-write-test")));
    if (!probe.open()) {
      log::error("can't write in the game folder {}: {}", gameDir.absolutePath(),
                 probe.errorString());
      return tr("Windows doesn't let MO2 write in the game folder (%1). Start MO2 as "
                "administrator, or copy the files yourself.")
          .arg(QDir::toNativeSeparators(gameDir.absolutePath()));
    }
  }

  // all or nothing: the new files first go in under another name, then the
  // old ones step aside, and only then do the new ones take their names. A
  // failure on the way puts everything back as it was.
  const QString newSuffix = QStringLiteral(".mo2new");
  const QString oldSuffix = QStringLiteral(".mo2old");
  QStringList staged, setAside, placed;
  const auto rollBack = [&] {
    for (const QString& target : placed) {
      QFile::remove(target);
    }
    for (const QString& target : setAside) {
      QFile::rename(target + oldSuffix, target);
    }
    for (const QString& target : staged) {
      QFile::remove(target + newSuffix);
    }
  };

  // Windows lets a loaded DLL be renamed, so the game running would go
  // unnoticed by the swap; a file in use doesn't open for writing
  for (const auto& file : files) {
    const QString target = gameDir.absoluteFilePath(file.first);
    QFile existing(target);
    if (existing.exists() && !existing.open(QIODevice::ReadWrite)) {
      log::error("{} is in use: {}", target, existing.errorString());
      return tr("%1 is in use and can't be replaced: close the game and try again.")
          .arg(QDir::toNativeSeparators(target));
    }
  }

  QStringList newNames;
  for (const auto& file : files) {
    newNames.append(file.first.toLower());
    const QString target = gameDir.absoluteFilePath(file.first);
    QFile::remove(target + newSuffix);
    if (!QFile::copy(file.second, target + newSuffix)) {
      log::error("can't copy {} to {}", file.second, target + newSuffix);
      rollBack();
      return tr("%1 couldn't be copied into the game folder.").arg(file.first);
    }
    staged.append(target);
  }
  for (const QString& target : staged) {
    if (!QFile::exists(target)) {
      continue;
    }
    QFile::remove(target + oldSuffix);
    if (!QFile::rename(target, target + oldSuffix)) {
      log::error("can't replace {}", target);
      rollBack();
      return tr("%1 is in use and can't be replaced: close the game and try again.")
          .arg(QDir::toNativeSeparators(target));
    }
    setAside.append(target);
  }
  for (const QString& target : staged) {
    if (!QFile::rename(target + newSuffix, target)) {
      log::error("can't rename {} to {}", target + newSuffix, target);
      rollBack();
      return tr("%1 couldn't be copied into the game folder.")
          .arg(QFileInfo(target).fileName());
    }
    placed.append(target);
  }
  for (const QString& target : setAside) {
    if (!QFile::moveToTrash(target + oldSuffix) && !QFile::remove(target + oldSuffix)) {
      log::warn("script extender: {} couldn't be removed; delete it by hand",
                target + oldSuffix);
    }
  }
  for (const auto& file : files) {
    log::info("script extender: copied {} into the game folder", file.first);
  }

  // skse64_1_6_1130.dll of the version before
  for (const QString& old : installedFiles(gameDir, prefix)) {
    if (newNames.contains(old.toLower())) {
      continue;
    }
    if (QFile::moveToTrash(gameDir.absoluteFilePath(old))) {
      log::info("script extender: {} of an older version went to the Recycle Bin", old);
      if (removed != nullptr) {
        removed->append(old);
      }
    } else {
      log::warn("script extender: {} of an older version couldn't be removed", old);
    }
  }
  return {};
}

void ScriptExtenderSetup::showManualSteps(QWidget* parent, const ScriptExtender& extender,
                                          const QDir& gameDir, const QStringList& fileNames,
                                          const QString& problem)
{
  const QString folder = QDir::toNativeSeparators(gameDir.absolutePath());
  QString text =
      tr("The rest is installed as a mod. Copy these files from the archive into %1, "
         "next to the game's exe:\n\n%2")
          .arg(folder, fileNames.join(QLatin1Char('\n')));
  if (!problem.isEmpty()) {
    text = problem + QStringLiteral("\n\n") + text;
  }
  FluentContentDialog dialog(parent, QStringLiteral("ScriptExtenderManualDialog"),
                             tr("Copy %1 into the game folder").arg(displayName(extender)),
                             text);
  auto* open  = dialog.addButton(tr("&Open the game folder"), true);
  auto* close = dialog.addButton(tr("Close"));
  QObject::connect(open, &QPushButton::clicked, &dialog, [&dialog, gameDir] {
    shell::Explore(gameDir);
    dialog.accept();
  });
  QObject::connect(close, &QPushButton::clicked, &dialog, &QDialog::reject);
  fitHeight(dialog, 520);
  dialog.exec();
}

// what a copied file holds: a file of the same name another version, a hand
// copy or another instance put there later isn't this mod's
static QString fileDigest(const QString& path)
{
  QFile file(path);
  if (!file.open(QIODevice::ReadOnly)) {
    return {};
  }
  QCryptographicHash hash(QCryptographicHash::Sha256);
  if (!hash.addData(&file)) {
    return {};
  }
  return QString::fromLatin1(hash.result().toHex());
}

void ScriptExtenderSetup::writeRecord(const QString& modPath, const QDir& gameDir,
                                      const QStringList& fileNames)
{
  // the files belong to this mod now: another mod's record for the same game
  // folder (an older version kept as its own mod) would take them away
  const QDir modDir(modPath);
  QDir modsDir(modDir);
  modsDir.cdUp();
  for (const QString& other : modsDir.entryList(QDir::Dirs | QDir::NoDotAndDotDot)) {
    if (other.compare(modDir.dirName(), Qt::CaseInsensitive) == 0) {
      continue;
    }
    const QString metaPath =
        modsDir.absoluteFilePath(other + QStringLiteral("/meta.ini"));
    if (!QFile::exists(metaPath)) {
      continue;
    }
    QSettings meta(metaPath, QSettings::IniFormat);
    if (!meta.childGroups().contains(RecordGroup)) {
      continue;
    }
    const QDir recorded(meta.value(RecordGroup + QStringLiteral("/gameDirectory")).toString());
    if (folderKey(recorded) == folderKey(gameDir)) {
      meta.remove(RecordGroup);
      log::info("script extender: {} no longer owns the files in the game folder", other);
    }
  }

  QSettings meta(modDir.absoluteFilePath(QStringLiteral("meta.ini")), QSettings::IniFormat);
  meta.beginGroup(RecordGroup);
  meta.setValue(QStringLiteral("gameDirectory"),
                QDir::toNativeSeparators(gameDir.absolutePath()));
  meta.setValue(QStringLiteral("gameFiles"), fileNames);
  QStringList digests;
  for (const QString& name : fileNames) {
    digests.append(fileDigest(gameDir.absoluteFilePath(name)));
  }
  meta.setValue(QStringLiteral("gameFileDigests"), digests);
  meta.endGroup();
}

QStringList ScriptExtenderSetup::recordedFiles(const QString& modPath,
                                               const MOBase::ScriptExtender& extender)
{
  const QString metaPath = QDir(modPath).absoluteFilePath(QStringLiteral("meta.ini"));
  if (!QFile::exists(metaPath)) {
    return {};
  }
  QSettings meta(metaPath, QSettings::IniFormat);
  meta.beginGroup(RecordGroup);
  const QDir gameDir(meta.value(QStringLiteral("gameDirectory")).toString());
  const QStringList fileNames = meta.value(QStringLiteral("gameFiles")).toStringList();
  const QStringList digests =
      meta.value(QStringLiteral("gameFileDigests")).toStringList();
  meta.endGroup();

  QStringList result;
  // a mod copied from another instance points at that game's folder
  if (gameDir.path().isEmpty() || gameDir.path() == QLatin1String(".") ||
      folderKey(gameDir) != folderKey(loaderDir(extender))) {
    return result;
  }
  const QString prefix = filePrefix(extender);
  const QString loader = extender.loaderName();
  for (int i = 0; i < fileNames.size(); ++i) {
    const QString& name = fileNames[i];
    // only the extender's names: a damaged record can't take others
    if (!isExtenderFile(name, prefix, loader)) {
      continue;
    }
    const QString path = gameDir.absoluteFilePath(name);
    if (!QFile::exists(path)) {
      continue;
    }
    // only the files as MO2 copied them; a record without their contents
    // (an earlier build) offers none
    const QString digest = i < digests.size() ? digests[i] : QString();
    if (digest.isEmpty() || fileDigest(path) != digest) {
      log::info("script extender: {} isn't the copy this mod made; it stays", path);
      continue;
    }
    result.append(path);
  }
  return result;
}

QStringList ScriptExtenderSetup::removeFiles(const QStringList& paths)
{
  QStringList failed;
  for (const QString& path : paths) {
    if (QFile::moveToTrash(path)) {
      log::info("script extender: {} went to the Recycle Bin", path);
    } else {
      log::warn("script extender: {} couldn't be removed", path);
      failed.append(QDir::toNativeSeparators(path));
    }
  }
  return failed;
}

QDir ScriptExtenderSetup::loaderDir(const ScriptExtender& extender)
{
  return QFileInfo(extender.loaderPath()).absoluteDir();
}

bool ScriptExtenderSetup::isLoader(const QString& path, const ScriptExtender& extender)
{
  if (path.isEmpty()) {
    return false;
  }
  return QDir::cleanPath(QFileInfo(path).absoluteFilePath())
             .compare(QDir::cleanPath(QFileInfo(extender.loaderPath()).absoluteFilePath()),
                      Qt::CaseInsensitive) == 0;
}

bool ScriptExtenderSetup::confirmLaunch(QWidget* parent, const ScriptExtender& extender,
                                        const IPluginGame& game)
{
  const QString prefix       = filePrefix(extender);
  const QStringList versions = runtimeVersions(installedFiles(loaderDir(extender), prefix),
                                               prefix);
  const QString gameVersion  = game.gameVersion();
  if (versions.isEmpty() || matchesGame(versions, gameVersion)) {
    return true;
  }

  const QString name = displayName(extender);
  log::warn("{} in the game folder is for {}, the game is {}", name,
            versions.join(", "), gameVersion);

  FluentContentDialog dialog(
      parent, QStringLiteral("ScriptExtenderVersionDialog"),
      tr("%1 is for another version of the game").arg(name),
      tr("The game is version %1, and the %2 in its folder is for %3. The game won't "
         "start through it, or closes at once. This happens after a game update: %2's "
         "download page has the build for %1.")
          .arg(gameVersion, name, versions.join(QStringLiteral(", "))));
  auto* start  = dialog.addButton(tr("Start anyway"), true);
  auto* cancel = dialog.addButton(tr("Cancel"));
  QObject::connect(start, &QPushButton::clicked, &dialog, &QDialog::accept);
  QObject::connect(cancel, &QPushButton::clicked, &dialog, &QDialog::reject);
  dialog.askFirst(cancel);
  fitHeight(dialog, 500);
  return dialog.exec() == QDialog::Accepted;
}
