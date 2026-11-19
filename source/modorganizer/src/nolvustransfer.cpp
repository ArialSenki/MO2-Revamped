#include "nolvustransfer.h"
#include "envfs.h"
#include "filesystemutilities.h"
#include "fluentdialogs.h"
#include "instancemanager.h"
#include "shared/util.h"
#include "envfs.h"
#include <log.h>
#include <utility.h>

#include <QButtonGroup>
#include <QCheckBox>
#include <QComboBox>
#include <QCoreApplication>
#include <QDateTime>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QProgressDialog>
#include <QPushButton>
#include <QRadioButton>
#include <QSaveFile>
#include <QRegularExpression>
#include <QSet>
#include <QSettings>
#include <QStorageInfo>
#include <QTime>
#include <QTimer>
#include <QVBoxLayout>

#include <Windows.h>
#include <io.h>

#include <algorithm>

#include <memory>
#include <thread>
#include <vector>

using namespace MOBase;

namespace Nolvus
{

namespace
{

struct Text
{
  Q_DECLARE_TR_FUNCTIONS(Nolvus)
};

const QString ManifestName = QStringLiteral("nolvus-transfer.jsonl");
const QString StockGameName = QStringLiteral("Stock Game");
const QString ToolsName     = QStringLiteral("Tools");

// the source's folders; empty when it has none
struct Paths
{
  QString mods, overwrite, profiles, stockGame, tools, archives;
};

QString existing(const QString& path)
{
  return !path.isEmpty() && QFileInfo(path).exists() ? cleanPath(path) : QString();
}

Paths sourcePaths(const TransferSource& source)
{
  const auto& i = source.info;
  Paths p;
  p.mods      = existing(QDir(i.modsPath).filePath(QStringLiteral("mods")));
  p.overwrite = existing(QDir(i.modsPath).filePath(QStringLiteral("overwrite")));
  // a linked instance keeps the profiles it already has
  if (source.linkedInstance.isEmpty()) {
    p.profiles = existing(QDir(i.modsPath).filePath(QStringLiteral("profiles")));
  }
  p.stockGame = existing(i.stockGame);
  p.tools     = existing(QDir(i.installPath).filePath(QStringLiteral("TOOLS")));
  p.archives  = existing(i.archivePath);
  return p;
}

QString longPath(const QString& path)
{
  const QString native = QDir::toNativeSeparators(QDir::cleanPath(path));
  return native.startsWith(QStringLiteral("\\\\")) ? native
                                                   : QStringLiteral("\\\\?\\") + native;
}

QString systemError(DWORD e)
{
  return qt_error_string(static_cast<int>(e)).trimmed();
}

QString existingAncestor(QString path)
{
  path = cleanPath(path);
  while (!path.isEmpty() && !QFileInfo(path).exists()) {
    const QString parent = QFileInfo(path).path();
    if (parent == path) {
      break;
    }
    path = parent;
  }
  return path;
}

// files under a folder, with their sizes; folders walked through the NT API,
// like the downloads list
struct Walk
{
  std::wstring root;
  std::vector<std::wstring> folders;
  const std::atomic_bool* cancel = nullptr;
  bool collect                   = false;
  std::uint64_t bytes = 0, files = 0;
  struct File
  {
    QString relative;
    std::uint64_t size;
  };
  std::vector<File> list;
  std::vector<QString> dirs;
  // folders that couldn't be read: a copy without them isn't complete
  std::size_t errors = 0;
};

void walk(const QString& folder, Walk& w)
{
  w.root = QDir::toNativeSeparators(cleanPath(folder)).toStdWString();
  // counts the folders it can't read, and stops at once when cancelled
  env::WalkControl control;
  control.cancel = w.cancel;
  env::setWalkControl(&control);
  env::forEachEntry(
      w.root, &w,
      [](void* data, std::wstring_view name) {
        auto& w = *static_cast<Walk*>(data);
        w.folders.emplace_back(name);
        if (w.collect) {
          QString rel;
          for (const auto& f : w.folders) {
            rel += (rel.isEmpty() ? QString() : QStringLiteral("/")) +
                   QString::fromStdWString(f);
          }
          w.dirs.push_back(rel);
        }
      },
      [](void* data, std::wstring_view) {
        static_cast<Walk*>(data)->folders.pop_back();
      },
      [](void* data, std::wstring_view f, FILETIME, uint64_t size) {
        auto& w = *static_cast<Walk*>(data);
        if (w.cancel && *w.cancel) {
          return;
        }
        w.bytes += size;
        ++w.files;
        if (w.collect) {
          QString rel;
          for (const auto& part : w.folders) {
            rel += QString::fromStdWString(part) + QLatin1Char('/');
          }
          rel += QString::fromWCharArray(f.data(), static_cast<int>(f.size()));
          w.list.push_back({rel, size});
        }
      });
  env::setWalkControl(nullptr);
  w.errors += control.errors;
}

std::uint64_t folderSize(const QString& folder, const std::atomic_bool& cancel,
                         std::uint64_t* files)
{
  if (folder.isEmpty()) {
    return 0;
  }
  Walk w;
  w.cancel = &cancel;
  walk(folder, w);
  if (files) {
    *files += w.files;
  }
  return w.bytes;
}

// a copy of our own, never a link: safe to delete
void removeOwnTree(const QString& path)
{
  const QFileInfo fi(path);
  if (fi.isJunction() || fi.isSymLink()) {
    QDir().rmdir(path);
    return;
  }
  if (!fi.isDir()) {
    QFile f(path);
    f.setPermissions(f.permissions() | QFileDevice::WriteOwner);
    f.remove();
    return;
  }
  for (const QFileInfo& child :
       QDir(path).entryInfoList(QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden |
                                QDir::System)) {
    removeOwnTree(child.absoluteFilePath());
  }
  QDir().rmdir(path);
}

class Manifest
{
public:
  explicit Manifest(const QString& path) : m_file(path) {}

  bool open() { return m_file.open(QIODevice::Append | QIODevice::Text); }
  void close() { m_file.close(); }

  // false when the line isn't on disk: nothing may be done that it doesn't
  // record
  bool write(QJsonObject o)
  {
    o.insert(QStringLiteral("time"), QDateTime::currentDateTime().toString(Qt::ISODate));
    const QByteArray line = QJsonDocument(o).toJson(QJsonDocument::Compact) + "\n";
    if (!m_file.isOpen() || m_file.write(line) != line.size() || !m_file.flush()) {
      return false;
    }
    return ::FlushFileBuffers(reinterpret_cast<HANDLE>(_get_osfhandle(m_file.handle())));
  }

  static std::vector<QJsonObject> read(const QString& path)
  {
    std::vector<QJsonObject> list;
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) {
      return list;
    }
    while (!f.atEnd()) {
      const QByteArray line = f.readLine().trimmed();
      if (!line.isEmpty()) {
        list.push_back(QJsonDocument::fromJson(line).object());
      }
    }
    return list;
  }

private:
  QFile m_file;
};

QJsonObject op(const QString& name, const QString& from = {}, const QString& to = {})
{
  QJsonObject o{{QStringLiteral("op"), name}};
  if (!from.isEmpty()) {
    o.insert(QStringLiteral("from"), from);
  }
  if (!to.isEmpty()) {
    o.insert(QStringLiteral("to"), to);
  }
  return o;
}

bool moveItem(const QString& from, const QString& to, QString* error)
{
  const QFileInfo fi(from);
  if (fi.isJunction() || fi.isSymLink()) {
    *error = Text::tr("%1 is a link to another folder; it can't be moved, only copied.")
                 .arg(QDir::toNativeSeparators(from));
    return false;
  }
  if (QFileInfo(to).exists()) {
    *error = Text::tr("%1 already exists.").arg(QDir::toNativeSeparators(to));
    return false;
  }
  QDir().mkpath(QFileInfo(to).path());
  if (!::MoveFileExW(longPath(from).toStdWString().c_str(),
                     longPath(to).toStdWString().c_str(), 0)) {
    const DWORD e = ::GetLastError();
    *error = Text::tr("Can't move %1 to %2: %3")
                 .arg(QDir::toNativeSeparators(from), QDir::toNativeSeparators(to),
                      systemError(e));
    return false;
  }
  log::info("Nolvus transfer: moved '{}' to '{}'", QDir::toNativeSeparators(from),
            QDir::toNativeSeparators(to));
  return true;
}

// puts back what the manifest says was done, newest first; returns what
// couldn't be put back. Copies go to the Recycle Bin when recycle is set (an
// undo the user asked for: those copies may hold what was made since);
// otherwise, right after a failed transfer, they are deleted
QStringList applyUndo(const std::vector<QJsonObject>& entries, bool recycle = false,
                      QWidget* parent = nullptr)
{
  QStringList problems;
  QStringList toRemove;
  std::vector<QString> emptyFolders;
  for (auto it = entries.rbegin(); it != entries.rend(); ++it) {
    const QString kind = it->value(QStringLiteral("op")).toString();
    const QString from = it->value(QStringLiteral("from")).toString();
    const QString to   = it->value(QStringLiteral("to")).toString();

    if (kind == QLatin1String("move")) {
      // only what is still where it was moved to; a folder made again at the
      // old place (by the Dashboard) is left for the user to sort out
      if (QFileInfo(to).exists() && QFileInfo(from).exists()) {
        problems << Text::tr("%1 exists again; %2 stays where it is")
                        .arg(QDir::toNativeSeparators(from), QDir::toNativeSeparators(to));
      } else if (!QFileInfo(to).exists() && !QFileInfo(from).exists()) {
        // moved or renamed since: it can't be found to be put back
        problems << Text::tr("%1 isn't where the transfer put it (%2)")
                        .arg(QDir::toNativeSeparators(from), QDir::toNativeSeparators(to));
      } else if (QFileInfo(to).exists()) {
        QString error;
        QDir().mkpath(QFileInfo(from).path());
        if (!::MoveFileExW(longPath(to).toStdWString().c_str(),
                           longPath(from).toStdWString().c_str(), 0)) {
          problems << Text::tr("%1 is still in %2: %3")
                          .arg(QDir::toNativeSeparators(from), QDir::toNativeSeparators(to),
                               systemError(::GetLastError()));
        } else {
          log::info("Nolvus transfer undo: moved '{}' back to '{}'",
                    QDir::toNativeSeparators(to), QDir::toNativeSeparators(from));
        }
      }
    } else if (kind == QLatin1String("copy") || kind == QLatin1String("created")) {
      if (QFileInfo(to).exists()) {
        if (recycle) {
          toRemove << QDir::toNativeSeparators(to);
        } else {
          removeOwnTree(to);
          if (QFileInfo(to).exists()) {
            problems << Text::tr("Couldn't remove %1").arg(QDir::toNativeSeparators(to));
          }
        }
      }
    } else if (kind == QLatin1String("meta")) {
      if (QFileInfo(to).isFile()) {
        QSettings meta(to, QSettings::IniFormat);
        meta.setValue(QStringLiteral("installationFile"),
                      it->value(QStringLiteral("old")).toString());
        meta.sync();
        if (meta.status() != QSettings::NoError) {
          problems << Text::tr("Couldn't restore %1").arg(QDir::toNativeSeparators(to));
        }
      }
    } else if (kind == QLatin1String("mkdir")) {
      emptyFolders.push_back(to);  // only when empty, once the rest is gone
    } else if (kind == QLatin1String("ini")) {
      // a linked instance's settings, as they were
      if (QFileInfo(from).isFile()) {
        QFile::remove(to);
        if (!QFile::copy(from, to)) {
          problems << Text::tr("Couldn't restore %1").arg(QDir::toNativeSeparators(to));
        }
      } else {
        problems << Text::tr("Couldn't restore %1").arg(QDir::toNativeSeparators(to));
      }
    }
  }

  if (!toRemove.isEmpty()) {
    const auto r = MOShared::shellRemove(toRemove, true, parent);
    for (const QString& left : r.remaining) {
      problems << Text::tr("Couldn't remove %1").arg(left);
    }
  }
  for (const QString& folder : emptyFolders) {
    QDir().rmdir(folder);
  }
  return problems;
}

// the record's first entry says whether a linked instance was transferred; a
// damaged first line falls back to the linked instance's settings backup
bool transferWasLinked(const std::vector<QJsonObject>& entries, const QString& instanceDir)
{
  for (const auto& e : entries) {
    if (e.value(QStringLiteral("op")).toString() == QLatin1String("begin")) {
      return e.value(QStringLiteral("linked")).toBool();
    }
  }
  return QFileInfo(InstanceManager::singleton().iniPath(instanceDir) +
                   QStringLiteral(".linked-backup"))
      .isFile();
}


QString remapped(QString value, const std::vector<std::pair<QString, QString>>& map)
{
  for (const auto& [from, to] : map) {
    value.replace(from, to, Qt::CaseInsensitive);
    value.replace(QDir::toNativeSeparators(from), QDir::toNativeSeparators(to),
                  Qt::CaseInsensitive);
  }
  return value;
}

// rewrites a group's paths in the instance's own settings (a linked
// instance: what its user set up there stays)
void remapGroupInPlace(QSettings& ini, const QString& group,
                       const std::vector<std::pair<QString, QString>>& map)
{
  for (const QString& key : ini.allKeys()) {
    if (key.startsWith(group + QLatin1Char('/'))) {
      const QVariant v = ini.value(key);
      if (v.typeId() == QMetaType::QString) {
        ini.setValue(key, remapped(v.toString(), map));
      }
    }
  }
}

void copyGroupRemapped(const QSettings& from, QSettings& to, const QString& group,
                       const std::vector<std::pair<QString, QString>>& map)
{
  to.remove(group);
  for (const QString& key : from.allKeys()) {
    if (!key.startsWith(group + QLatin1Char('/'))) {
      continue;
    }
    const QVariant v = from.value(key);
    if (v.typeId() == QMetaType::QString) {
      to.setValue(key, remapped(v.toString(), map));
    } else {
      to.setValue(key, v);
    }
  }
}

// Nexus' file names: <name>-<mod id>-<version>-<upload time>
void describeArchive(const QString& fileName, QString* name, int* modId, QString* version)
{
  static const QRegularExpression nexusName(
      QStringLiteral("^(.+?)-(\\d+)-(.+)-(\\d{9,11})$"));
  const QString base = QFileInfo(fileName).completeBaseName();
  const auto m       = nexusName.match(base);
  *name              = m.hasMatch() ? m.captured(1) : base;
  *modId             = m.hasMatch() ? m.captured(2).toInt() : 0;
  *version = m.hasMatch() ? m.captured(3).replace(QLatin1Char('-'), QLatin1Char('.'))
                          : QString();
}

// copies the files in parallel: big lists are many small files, where one
// thread waits on the disk most of the time
bool copyFiles(const std::vector<std::pair<QString, QString>>& files,
               const std::vector<std::uint64_t>& sizes, TransferProgress& progress,
               QString* error)
{
  std::atomic<std::size_t> next{0};
  std::atomic_bool failed{false};
  std::mutex errorMutex;

  const auto work = [&] {
    for (std::size_t i = next++; i < files.size(); i = next++) {
      if (failed || progress.cancel) {
        return;
      }
      const auto& [from, to] = files[i];
      if (!::CopyFileExW(longPath(from).toStdWString().c_str(),
                         longPath(to).toStdWString().c_str(), nullptr, nullptr, nullptr,
                         COPY_FILE_FAIL_IF_EXISTS)) {
        const DWORD e = ::GetLastError();
        std::scoped_lock lock(errorMutex);
        if (!failed) {
          failed = true;
          *error = Text::tr("Can't copy %1: %2")
                       .arg(QDir::toNativeSeparators(from), systemError(e));
        }
        return;
      }
      progress.done += sizes[i];
    }
  };

  const std::size_t threads =
      std::clamp<std::size_t>(std::thread::hardware_concurrency(), 2, 6);
  std::vector<std::thread> pool;
  for (std::size_t t = 1; t < threads; ++t) {
    pool.emplace_back(work);
  }
  work();
  for (auto& t : pool) {
    t.join();
  }
  return !failed && !progress.cancel;
}

}  // namespace

// why the data folder can't be used, or empty: it must be a full path, and
// not inside Nolvus' folders or MO2's own
QString badDataFolder(const TransferSource& source, const QString& data)
{
  if (data.isEmpty() || !QDir::isAbsolutePath(data)) {
    return Text::tr("Choose the data folder with its full path (for example D:\\Games\\%1).")
        .arg(source.info.name);
  }
  const QString d = cleanPath(data);
  const auto& i   = source.info;
  const auto inside = [&](const QString& folder) {
    const QString c = cleanPath(folder);
    return !c.isEmpty() && (d.compare(c, Qt::CaseInsensitive) == 0 ||
                            d.startsWith(c + QLatin1Char('/'), Qt::CaseInsensitive));
  };
  // MO2's folder, except its PortableInstances, where instances belong
  const QString app = cleanPath(QCoreApplication::applicationDirPath());
  if (inside(app) &&
      !(inside(InstanceManager::singleton().portableInstancesRootPath()) &&
        d.compare(cleanPath(InstanceManager::singleton().portableInstancesRootPath()),
                  Qt::CaseInsensitive) != 0)) {
    return Text::tr("The data folder can't be inside %1.").arg(QDir::toNativeSeparators(app));
  }
  for (const QString& f : {i.installPath, i.modsPath, i.stockGame, i.archivePath, i.root}) {
    const QString c = cleanPath(f);
    if (c.isEmpty()) {
      continue;
    }
    if (d.compare(c, Qt::CaseInsensitive) == 0 ||
        d.startsWith(c + QLatin1Char('/'), Qt::CaseInsensitive)) {
      return Text::tr("The data folder can't be inside %1.").arg(QDir::toNativeSeparators(c));
    }
  }
  return {};
}

QString transferInstanceDir(const TransferSource& source)
{
  if (!source.linkedInstance.isEmpty()) {
    return cleanPath(source.linkedInstance);
  }
  auto& m            = InstanceManager::singleton();
  const QString root = m.portableInstancesRootPath();
  const QString name = m.makeUniqueName(source.info.name, root);
  return name.isEmpty() ? QString() : cleanPath(QDir(root).filePath(name));
}

bool sameVolume(const QString& a, const QString& b)
{
  const auto volume = [](const QString& p) {
    wchar_t buffer[MAX_PATH + 1] = {};
    const QString e = QDir::toNativeSeparators(existingAncestor(p));
    if (e.isEmpty() || !::GetVolumePathNameW(e.toStdWString().c_str(), buffer, MAX_PATH)) {
      return QString();
    }
    return QString::fromWCharArray(buffer).toCaseFolded();
  };
  const QString va = volume(a);
  return !va.isEmpty() && va == volume(b);
}

std::uint64_t freeSpace(const QString& dir)
{
  const QStorageInfo info(existingAncestor(dir));
  return info.isValid() ? static_cast<std::uint64_t>(info.bytesAvailable()) : 0;
}

TransferSizes measureTransfer(const TransferSource& source, const std::atomic_bool& cancel)
{
  TransferSizes s;
  const Paths p = sourcePaths(source);
  s.mods        = folderSize(p.mods, cancel, &s.files);
  s.overwrite   = folderSize(p.overwrite, cancel, &s.files);
  s.profiles    = folderSize(p.profiles, cancel, &s.files);
  if (!p.profiles.isEmpty()) {
    for (const QString& profile :
         QDir(p.profiles).entryList(QDir::Dirs | QDir::NoDotAndDotDot)) {
      s.saves += folderSize(existing(p.profiles + QLatin1Char('/') + profile +
                                     QStringLiteral("/saves")),
                            cancel, nullptr);
    }
  }
  s.stockGame = folderSize(p.stockGame, cancel, &s.files);
  s.tools     = folderSize(p.tools, cancel, &s.files);
  s.archives  = folderSize(p.archives, cancel, nullptr);
  return s;
}

void runTransfer(const TransferSource& source, const TransferOptions& options,
                 TransferProgress& progress)
{
  using Mode     = TransferOptions::Mode;
  using Archives = TransferOptions::Archives;

  const auto setStep = [&](const QString& step) {
    std::scoped_lock lock(progress.mutex);
    progress.step = step;
  };
  const auto finish = [&](const QString& error) {
    {
      std::scoped_lock lock(progress.mutex);
      progress.error = error;
    }
    progress.failed   = !error.isEmpty();
    progress.finished = true;
  };

  const auto& info    = source.info;
  const bool linked   = !source.linkedInstance.isEmpty();
  const Paths p       = sourcePaths(source);
  const QString inst  = transferInstanceDir(source);
  const bool inPlace  = options.mode == Mode::InPlace;
  const QString data  = inPlace ? QString()
                                : cleanPath(options.dataDir.isEmpty() ? inst : options.dataDir);
  const QString downloads =
      (inPlace ? inst : data) + QStringLiteral("/downloads");

  if (inst.isEmpty()) {
    finish(Text::tr("No free folder name for the instance."));
    return;
  }
  if (!linked && QFileInfo(inst).exists()) {
    finish(Text::tr("%1 already exists.").arg(QDir::toNativeSeparators(inst)));
    return;
  }

  // what goes where
  std::vector<std::pair<QString, QString>> items;
  if (!inPlace) {
    if (!p.mods.isEmpty()) items.push_back({p.mods, data + QStringLiteral("/mods")});
    if (!p.overwrite.isEmpty())
      items.push_back({p.overwrite, data + QStringLiteral("/overwrite")});
    if (!p.stockGame.isEmpty())
      items.push_back({p.stockGame, data + QLatin1Char('/') + StockGameName});
    if (!p.tools.isEmpty()) items.push_back({p.tools, data + QLatin1Char('/') + ToolsName});
    // the profiles entry by entry, so their saves can stay behind
    if (!p.profiles.isEmpty()) {
      for (const QString& profile :
           QDir(p.profiles).entryList(QDir::Dirs | QDir::NoDotAndDotDot)) {
        const QDir dir(p.profiles + QLatin1Char('/') + profile);
        // whole, so an undo also takes back what was added to it since
        if (options.keepSaves) {
          items.push_back({dir.absolutePath(), data + QStringLiteral("/profiles/") + profile});
          continue;
        }
        for (const QString& entry : dir.entryList(QDir::AllEntries | QDir::NoDotAndDotDot |
                                                  QDir::Hidden | QDir::System)) {
          if (!options.keepSaves &&
              entry.compare(QStringLiteral("saves"), Qt::CaseInsensitive) == 0) {
            continue;
          }
          items.push_back({dir.filePath(entry), data + QStringLiteral("/profiles/") +
                                                    profile + QLatin1Char('/') + entry});
        }
      }
    }
  }

  // checks before anything is done
  if (!inPlace) {
    if (const QString bad = badDataFolder(source, data); !bad.isEmpty()) {
      finish(bad);
      return;
    }
  }
  for (const auto& [from, to] : items) {
    if (QFileInfo(to).exists()) {
      finish(Text::tr("%1 already exists.").arg(QDir::toNativeSeparators(to)));
      return;
    }
    if (options.mode == Mode::Move && !sameVolume(from, to)) {
      finish(Text::tr("%1 and %2 are on different drives: moving would copy everything. "
                      "Choose a folder on the same drive, or copy.")
                 .arg(QDir::toNativeSeparators(from), QDir::toNativeSeparators(to)));
      return;
    }
  }
  const bool archivesToo = !p.archives.isEmpty() && options.archives != Archives::Leave;
  if (archivesToo && options.archives == Archives::Move &&
      !sameVolume(p.archives, downloads)) {
    finish(Text::tr("The Nolvus archives are on another drive than %1: copy them, or leave "
                    "them where they are.")
               .arg(QDir::toNativeSeparators(downloads)));
    return;
  }

  setStep(Text::tr("Preparing"));
  const bool newInstance = !QFileInfo(inst).exists();
  if (!QDir().mkpath(inst)) {
    finish(Text::tr("Can't create %1").arg(QDir::toNativeSeparators(inst)));
    return;
  }
  // a new record for each transfer: an earlier one (undone) stays beside it
  const QString manifestPath = inst + QLatin1Char('/') + ManifestName;
  if (QFileInfo(manifestPath).exists()) {
    QFile::rename(manifestPath,
                  inst + QStringLiteral("/nolvus-transfer-") +
                      QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd-HHmmsszzz")) +
                      QStringLiteral(".jsonl"));
  }
  Manifest manifest(manifestPath);
  if (!manifest.open()) {
    finish(Text::tr("Can't write %1").arg(QDir::toNativeSeparators(inst + "/" + ManifestName)));
    if (newInstance) {
      QDir().rmdir(inst);
    }
    return;
  }

  const QString modeName = options.mode == Mode::Move   ? QStringLiteral("move")
                           : options.mode == Mode::Copy ? QStringLiteral("copy")
                                                        : QStringLiteral("inplace");
  QJsonObject begin = op(QStringLiteral("begin"), info.installPath, inst);
  begin.insert(QStringLiteral("mode"), modeName);
  begin.insert(QStringLiteral("linked"), linked);
  if (!manifest.write(begin) ||
      (newInstance && !manifest.write(op(QStringLiteral("mkdir"), {}, inst)))) {
    manifest.close();
    QFile::remove(manifestPath);
    if (newInstance) {
      QDir().rmdir(inst);
    }
    finish(Text::tr("Can't write %1").arg(QDir::toNativeSeparators(manifestPath)));
    return;
  }
  log::info("Nolvus transfer: {} '{}' into '{}' (data in '{}')", modeName,
            QDir::toNativeSeparators(info.installPath), QDir::toNativeSeparators(inst),
            QDir::toNativeSeparators(data.isEmpty() ? info.modsPath : data));

  const QString iniPath = InstanceManager::singleton().iniPath(inst);
  if (linked) {
    const QString backup = iniPath + QStringLiteral(".linked-backup");
    QFile::remove(backup);
    if (!QFile::copy(iniPath, backup) ||
        !manifest.write(op(QStringLiteral("ini"), backup, iniPath))) {
      manifest.close();
      QFile::remove(manifestPath);
      finish(Text::tr("Can't write %1").arg(QDir::toNativeSeparators(backup)));
      return;
    }
  }

  // on failure: everything back, and the new instance's folder gone
  const auto rollback = [&](const QString& error) {
    setStep(Text::tr("Putting everything back"));
    log::error("Nolvus transfer failed: {}", error);
    manifest.close();
    const QStringList problems =
        applyUndo(Manifest::read(inst + QLatin1Char('/') + ManifestName));
    if (problems.isEmpty()) {
      QFile::remove(inst + QLatin1Char('/') + ManifestName);
      if (linked) {
        QFile::remove(iniPath + QStringLiteral(".linked-backup"));
      } else {
        QDir().rmdir(inst);
      }
      finish(error);
    } else {
      for (const QString& problem : problems) {
        log::error("Nolvus transfer undo: {}", problem);
      }
      finish(error + QStringLiteral("\n\n") +
             Text::tr("Some things couldn't be put back; the list is in %1. Instances offers "
                      "to try again (\"Put everything back\").")
                 .arg(QDir::toNativeSeparators(inst + QLatin1Char('/') + ManifestName)) +
             QStringLiteral("\n") + problems.join(QLatin1Char('\n')));
    }
  };

  // every step is on disk before it is done
  const auto record = [&](const QJsonObject& o) {
    if (manifest.write(o)) {
      return true;
    }
    rollback(Text::tr("Can't write %1; nothing more was done.")
                 .arg(QDir::toNativeSeparators(manifestPath)));
    return false;
  };
  // a folder made here is recorded first, so an undo takes it away
  const auto makeFolder = [&](const QString& dir) {
    if (QFileInfo(dir).exists()) {
      return true;
    }
    if (!record(op(QStringLiteral("mkdir"), {}, dir))) {
      return false;
    }
    QDir().mkpath(dir);
    return true;
  };

  if (!data.isEmpty() && !makeFolder(data)) {
    return;
  }

  QString error;
  if (options.mode == Mode::Move) {
    progress.total = items.size();
    for (const auto& [from, to] : items) {
      if (progress.cancel) {
        rollback(Text::tr("Cancelled."));
        return;
      }
      setStep(Text::tr("Moving %1").arg(QFileInfo(from).fileName()));
      if (!record(op(QStringLiteral("move"), from, to))) {
        return;
      }
      if (!moveItem(from, to, &error)) {
        rollback(error);
        return;
      }
      ++progress.done;
    }
  } else if (options.mode == Mode::Copy) {
    setStep(Text::tr("Listing the files to copy"));
    std::vector<std::pair<QString, QString>> files;
    std::vector<std::uint64_t> sizes;
    std::uint64_t total = 0;
    for (const auto& [from, to] : items) {
      if (!record(op(QStringLiteral("copy"), from, to))) {
        return;
      }
      // a junction (mods kept on another drive) is copied through: its files
      // are what the instance needs
      if (QFileInfo(from).isDir()) {
        Walk w;
        w.collect = true;
        w.cancel  = &progress.cancel;
        walk(from, w);
        if (w.errors > 0 && !progress.cancel) {
          rollback(Text::tr("%n folders in %1 couldn't be read, so the copy wouldn't be "
                            "complete. The reason is in MO2's log.",
                            nullptr, static_cast<int>(w.errors))
                       .arg(QDir::toNativeSeparators(from)));
          return;
        }
        QDir().mkpath(to);
        for (const QString& d : w.dirs) {
          QDir().mkpath(to + QLatin1Char('/') + d);
        }
        for (const auto& f : w.list) {
          files.push_back({from + QLatin1Char('/') + f.relative, to + QLatin1Char('/') + f.relative});
          sizes.push_back(f.size);
          total += f.size;
        }
      } else if (QFileInfo(from).isFile()) {
        QDir().mkpath(QFileInfo(to).path());
        files.push_back({from, to});
        sizes.push_back(static_cast<std::uint64_t>(QFileInfo(from).size()));
        total += sizes.back();
      } else {
        rollback(Text::tr("%1 can't be read, so it can't be copied.")
                     .arg(QDir::toNativeSeparators(from)));
        return;
      }
    }
    if (progress.cancel) {
      rollback(Text::tr("Cancelled."));
      return;
    }
    if (total > freeSpace(data)) {
      rollback(Text::tr("Not enough free space in %1: %2 needed, %3 free.")
                   .arg(QDir::toNativeSeparators(data), localizedByteSize(total),
                        localizedByteSize(freeSpace(data))));
      return;
    }
    progress.total = total;
    setStep(Text::tr("Copying %n files", nullptr, static_cast<int>(files.size())));
    if (!copyFiles(files, sizes, progress, &error)) {
      rollback(progress.cancel ? Text::tr("Cancelled.") : error);
      return;
    }
  }

  // the archives, as the instance's downloads
  QHash<QString, QString> archiveName;  // old path (case-folded) -> new file name
  if (archivesToo) {
    setStep(Text::tr("Listing the Nolvus archives"));
    if (!makeFolder(downloads)) {
      return;
    }
    Walk w;
    w.collect = true;
    walk(p.archives, w);
    if (w.errors > 0) {
      rollback(Text::tr("%n folders in %1 couldn't be read, so the copy wouldn't be "
                        "complete. The reason is in MO2's log.",
                        nullptr, static_cast<int>(w.errors))
                   .arg(QDir::toNativeSeparators(p.archives)));
      return;
    }

    QSet<QString> taken;
    for (const QString& f : QDir(downloads).entryList(QDir::Files)) {
      taken.insert(f.toCaseFolded());
    }
    std::vector<std::pair<QString, QString>> files;
    std::vector<std::uint64_t> sizes;
    for (const auto& f : w.list) {
      if (f.relative.endsWith(QStringLiteral(".meta"), Qt::CaseInsensitive)) {
        continue;
      }
      QString name = QFileInfo(f.relative).fileName();
      for (int n = 2; taken.contains(name.toCaseFolded()); ++n) {
        const QFileInfo fi(QFileInfo(f.relative).fileName());
        name = QStringLiteral("%1 (%2).%3").arg(fi.completeBaseName()).arg(n).arg(fi.suffix());
      }
      taken.insert(name.toCaseFolded());
      const QString from = p.archives + QLatin1Char('/') + f.relative;
      archiveName.insert(from.toCaseFolded(), name);
      files.push_back({from, downloads + QLatin1Char('/') + name});
      sizes.push_back(f.size);
    }

    progress.done  = 0;
    progress.total = options.archives == Archives::Move ? files.size() : 0;
    if (options.archives == Archives::Move) {
      setStep(Text::tr("Moving %n archives", nullptr, static_cast<int>(files.size())));
      for (const auto& [from, to] : files) {
        if (progress.cancel) {
          rollback(Text::tr("Cancelled."));
          return;
        }
        if (!record(op(QStringLiteral("move"), from, to))) {
          return;
        }
        if (!moveItem(from, to, &error)) {
          rollback(error);
          return;
        }
        ++progress.done;
      }
    } else {
      std::uint64_t total = 0;
      for (const auto s : sizes) total += s;
      if (total > freeSpace(downloads)) {
        rollback(Text::tr("Not enough free space in %1: %2 needed, %3 free.")
                     .arg(QDir::toNativeSeparators(downloads), localizedByteSize(total),
                          localizedByteSize(freeSpace(downloads))));
        return;
      }
      for (const auto& [from, to] : files) {
        if (!record(op(QStringLiteral("created"), from, to))) {
          return;
        }
      }
      progress.total = total;
      setStep(Text::tr("Copying %n archives", nullptr, static_cast<int>(files.size())));
      if (!copyFiles(files, sizes, progress, &error)) {
        rollback(progress.cancel ? Text::tr("Cancelled.") : error);
        return;
      }
    }
  }

  // the mods' meta.ini name the archive they came from: a moved archive is
  // now in the downloads folder, by name; copied ones are found by their
  // .meta as installed
  setStep(Text::tr("Updating the mods' records"));
  const QString modsNow = inPlace ? p.mods : data + QStringLiteral("/mods");
  QSet<QString> installed;
  if (archivesToo && !modsNow.isEmpty()) {
    for (const QString& mod : QDir(modsNow).entryList(QDir::Dirs | QDir::NoDotAndDotDot)) {
      const QString metaPath = modsNow + QLatin1Char('/') + mod + QStringLiteral("/meta.ini");
      if (!QFileInfo(metaPath).isFile()) {
        continue;
      }
      QSettings meta(metaPath, QSettings::IniFormat);
      const QString file = meta.value(QStringLiteral("installationFile")).toString();
      const auto it = archiveName.constFind(cleanPath(file).toCaseFolded());
      if (it == archiveName.constEnd()) {
        continue;
      }
      installed.insert(it.value().toCaseFolded());
      if (options.archives == Archives::Move) {
        QJsonObject o = op(QStringLiteral("meta"), {}, metaPath);
        o.insert(QStringLiteral("old"), file);
        if (!record(o)) {
          return;
        }
        meta.setValue(QStringLiteral("installationFile"), it.value());
      }
    }
  }

  // a .meta for each archive: its Nexus mod, and the list as its group
  if (archivesToo) {
    for (const QString& name : archiveName) {
      const QString metaPath = downloads + QLatin1Char('/') + name + QStringLiteral(".meta");
      if (QFileInfo(metaPath).exists()) {
        continue;
      }
      if (!record(op(QStringLiteral("created"), {}, metaPath))) {
        return;
      }
      QString modName, version;
      int modId = 0;
      describeArchive(name, &modName, &modId, &version);
      QSettings meta(metaPath, QSettings::IniFormat);
      meta.setValue(QStringLiteral("gameName"), QStringLiteral("SkyrimSE"));
      meta.setValue(QStringLiteral("modID"), modId);
      meta.setValue(QStringLiteral("fileID"), 0);
      meta.setValue(QStringLiteral("name"), modName);
      meta.setValue(QStringLiteral("modName"), modName);
      if (!version.isEmpty()) {
        meta.setValue(QStringLiteral("version"), version);
      }
      meta.setValue(QStringLiteral("repository"), QStringLiteral("Nexus"));
      meta.setValue(QStringLiteral("installed"), installed.contains(name.toCaseFolded()));
      meta.setValue(QStringLiteral("sourceList"), info.name);
    }
  }

  // the instance's settings
  setStep(Text::tr("Writing the instance's settings"));
  if ((!linked && !record(op(QStringLiteral("created"), {}, iniPath))) ||
      !makeFolder(downloads) || !makeFolder(inst + QStringLiteral("/webcache"))) {
    return;
  }
  bool iniWritten = false;
  {
    const QSettings nolvusIni(QDir(info.mo2Path).filePath(QStringLiteral("ModOrganizer.ini")),
                              QSettings::IniFormat);
    QSettings ini(iniPath, QSettings::IniFormat);

    std::vector<std::pair<QString, QString>> map;
    if (!inPlace) {
      map = {{cleanPath(info.stockGame), data + QLatin1Char('/') + StockGameName},
             {cleanPath(info.modsPath) + QStringLiteral("/mods"), data + QStringLiteral("/mods")},
             {cleanPath(info.modsPath) + QStringLiteral("/overwrite"),
              data + QStringLiteral("/overwrite")},
             {cleanPath(info.installPath) + QStringLiteral("/TOOLS"),
              data + QLatin1Char('/') + ToolsName}};
    }
    const QString gamePath = inPlace ? cleanPath(info.stockGame)
                                     : data + QLatin1Char('/') + StockGameName;
    const QString base = inPlace ? cleanPath(info.modsPath) : data;

    if (!linked) {
      QString gameName = iniValue(nolvusIni, QStringLiteral("gameName"));
      ini.setValue(QStringLiteral("gameName"),
                   gameName.isEmpty() ? QStringLiteral("Skyrim Special Edition") : gameName);
      if (!info.profile.isEmpty()) {
        ini.setValue(QStringLiteral("selected_profile"), info.profile.toUtf8());
      }
      if (const QString edition = iniValue(nolvusIni, QStringLiteral("game_edition"));
          !edition.isEmpty()) {
        ini.setValue(QStringLiteral("game_edition"), edition);
      }
      ini.setValue(QStringLiteral("first_start"), false);
    }
    writeCarriedSettings(nolvusIni, ini);
    ini.setValue(QStringLiteral("gamePath"), QDir::toNativeSeparators(gamePath).toUtf8());
    ini.setValue(QStringLiteral("Settings/base_directory"), base);
    ini.setValue(QStringLiteral("Settings/download_directory"), downloads);
    ini.setValue(QStringLiteral("Settings/cache_directory"), inst + QStringLiteral("/webcache"));
    // a linked instance keeps its own profiles folder; a new one uses the
    // base directory's (the moved or copied profiles, or Nolvus' own)
    if (!linked) {
      ini.remove(QStringLiteral("Settings/profiles_directory"));
    }

    // a linked instance keeps the programs and plugin settings its user set
    // up, with their paths moved along; a new one gets Nolvus' own
    for (const QString& group : {QStringLiteral("customExecutables"), QStringLiteral("Plugins"),
                                 QStringLiteral("pluginBlacklist")}) {
      if (linked) {
        remapGroupInPlace(ini, group, map);
      } else {
        copyGroupRemapped(nolvusIni, ini, group, map);
      }
    }

    const QString s = QStringLiteral("Nolvus/");
    ini.remove(QStringLiteral("Nolvus"));
    ini.setValue(s + QStringLiteral("transferred"), true);
    ini.setValue(s + QStringLiteral("transferMode"), modeName);
    ini.setValue(s + QStringLiteral("transferredAt"), QDateTime::currentDateTime());
    ini.setValue(s + QStringLiteral("registerNxm"), options.registerNxm);
    ini.setValue(s + QStringLiteral("root"), info.root);
    ini.setValue(s + QStringLiteral("name"), info.name);
    ini.setValue(s + QStringLiteral("version"), info.version);
    ini.setValue(s + QStringLiteral("variant"), info.variant);
    ini.setValue(s + QStringLiteral("enb"), info.enb);
    ini.setValue(s + QStringLiteral("installPath"), info.installPath);
    ini.setValue(s + QStringLiteral("mo2Path"), info.mo2Path);
    ini.setValue(s + QStringLiteral("stockGame"), gamePath);
    ini.setValue(s + QStringLiteral("modsPath"), base);
    // archives left where they are stay listed read-only
    ini.setValue(s + QStringLiteral("archivePath"),
                 options.archives == Archives::Leave ? p.archives : QString());
    ini.sync();
    iniWritten = ini.status() == QSettings::NoError;
  }
  // past the settings' scope: nothing can write them again after the rollback
  if (!iniWritten) {
    rollback(Text::tr("Can't write %1").arg(QDir::toNativeSeparators(iniPath)));
    return;
  }

  if (!record(op(QStringLiteral("finished")))) {
    return;
  }
  log::info("Nolvus transfer: finished '{}'", QDir::toNativeSeparators(inst));
  {
    std::scoped_lock lock(progress.mutex);
    progress.instanceDir = inst;
  }
  finish({});
}

std::vector<QString> interruptedTransfers()
{
  std::vector<QString> list;
  const QDir root(InstanceManager::singleton().portableInstancesRootPath());
  for (const QString& d : root.entryList(QDir::Dirs | QDir::NoDotAndDotDot)) {
    const QString dir     = cleanPath(root.filePath(d));
    const auto entries    = Manifest::read(dir + QLatin1Char('/') + ManifestName);
    bool ended = entries.empty();
    for (const auto& e : entries) {
      const QString kind = e.value(QStringLiteral("op")).toString();
      ended |= kind == QLatin1String("finished") || kind == QLatin1String("undone");
    }
    // its settings were written (only "finished" is missing): it is used as an
    // instance, and its undo is the finished one's
    ended |= readLink(InstanceManager::singleton().iniPath(dir)).transferred;
    if (!ended) {
      list.push_back(dir);
    }
  }
  return list;
}

void retargetTransfer(const QString& oldDir, const QString& newDir)
{
  const QString path = newDir + QLatin1Char('/') + ManifestName;
  if (!QFileInfo(path).isFile()) {
    return;
  }
  const QString from = cleanPath(oldDir);
  const QString to   = cleanPath(newDir);
  QByteArray out;
  for (QJsonObject e : Manifest::read(path)) {
    for (const QString& field : {QStringLiteral("from"), QStringLiteral("to")}) {
      const QString value = cleanPath(e.value(field).toString());
      if (value.compare(from, Qt::CaseInsensitive) == 0 ||
          value.startsWith(from + QLatin1Char('/'), Qt::CaseInsensitive)) {
        e.insert(field, to + value.mid(from.size()));
      }
    }
    out += QJsonDocument(e).toJson(QJsonDocument::Compact) + "\n";
  }
  QSaveFile file(path);
  if (!file.open(QIODevice::WriteOnly) || file.write(out) != out.size() || !file.commit()) {
    log::error("Nolvus transfer: can't update the record in '{}' after a rename",
               QDir::toNativeSeparators(path));
  }
}

bool canUndoTransfer(const QString& instanceDir)
{
  const auto entries = Manifest::read(instanceDir + QLatin1Char('/') + ManifestName);
  bool moved = false, undone = false;
  for (const auto& e : entries) {
    const QString kind = e.value(QStringLiteral("op")).toString();
    moved |= kind == QLatin1String("move");
    undone |= kind == QLatin1String("undone");
  }
  return moved && !undone;
}

bool undoTransfer(const QString& instanceDir, QString* error, QWidget* parent)
{
  QString dummy;
  if (!error) {
    error = &dummy;
  }
  if (const auto active = InstanceManager::singleton().currentInstance();
      active && samePaths(active->directory(), instanceDir)) {
    *error = Text::tr("The instance in use can't be changed. Open another instance first.");
    return false;
  }

  const QString path = instanceDir + QLatin1Char('/') + ManifestName;
  auto entries       = Manifest::read(path);
  if (entries.empty()) {
    *error = Text::tr("There is no transfer to undo in %1.")
                 .arg(QDir::toNativeSeparators(instanceDir));
    return false;
  }
  const bool linked = transferWasLinked(entries, instanceDir);

  // the instance's own ini and folders stay (its settings, logs, the
  // downloads made since): only what came from Nolvus goes back
  // an interrupted transfer goes back entirely, copies included
  // its settings were written: done in effect, whatever the record lacks
  const bool finished =
      std::any_of(entries.begin(), entries.end(),
                  [](const QJsonObject& e) {
                    return e.value(QStringLiteral("op")).toString() ==
                           QLatin1String("finished");
                  }) ||
      readLink(InstanceManager::singleton().iniPath(instanceDir)).transferred;
  std::vector<QJsonObject> undo;
  for (const auto& e : entries) {
    const QString kind = e.value(QStringLiteral("op")).toString();
    if (!finished || kind == QLatin1String("move") || kind == QLatin1String("meta") ||
        (kind == QLatin1String("ini") && linked) ||
        (kind == QLatin1String("created") &&
         e.value(QStringLiteral("to")).toString().endsWith(QStringLiteral(".meta")))) {
      undo.push_back(e);
    }
  }
  const QStringList problems = applyUndo(undo, true, parent);

  Manifest manifest(path);
  manifest.open();
  // something is still where the transfer put it: the instance and its
  // record stay, so the undo can be tried again
  if (!problems.isEmpty()) {
    manifest.write(op(QStringLiteral("undo-partial")));
    *error = problems.join(QLatin1Char('\n'));
    return false;
  }

  manifest.write(op(QStringLiteral("undone")));
  if (!linked) {
    // not an instance any more: its mods went back to Nolvus
    const QString ini = InstanceManager::singleton().iniPath(instanceDir);
    QFile::remove(ini + QStringLiteral(".transfer-undone"));
    QFile::rename(ini, ini + QStringLiteral(".transfer-undone"));
  }

  // a transfer that never finished leaves nothing behind once put back
  manifest.close();
  if (!finished && !linked) {
    QFile::remove(path);
    QDir().rmdir(instanceDir);
  }
  log::info("Nolvus transfer undone for '{}'", QDir::toNativeSeparators(instanceDir));
  return true;
}

QString runTransferWizard(QWidget* parent, const TransferSource& source)
{
  using Mode     = TransferOptions::Mode;
  using Archives = TransferOptions::Archives;

  const auto& info  = source.info;
  const bool linked = !source.linkedInstance.isEmpty();
  const QString inst = transferInstanceDir(source);
  if (inst.isEmpty()) {
    QMessageBox::warning(parent, Text::tr("Transfer to MO2 Revamped"),
                         Text::tr("No free folder name for the instance."));
    return {};
  }

  FluentContentDialog dlg(
      parent, QStringLiteral("NolvusTransferDialog"),
      Text::tr("Transfer %1 to MO2 Revamped").arg(info.name),
      Text::tr("Makes a normal MO2 Revamped instance of %1 that owns its mods, profiles "
               "and stock game. Choose how:")
          .arg(info.name));

  auto* body = dlg.body();
  auto* modes = new QButtonGroup(&dlg);
  const auto addMode = [&](Mode mode, const QString& title, const QString& text) {
    auto* radio = new QRadioButton(title, &dlg);
    radio->setObjectName(QStringLiteral("nolvusTransferMode%1").arg(static_cast<int>(mode)));
    modes->addButton(radio, static_cast<int>(mode));
    body->addWidget(radio);
    auto* label = new QLabel(text, &dlg);
    label->setWordWrap(true);
    label->setContentsMargins(24, 0, 0, 6);
    label->setObjectName(QStringLiteral("fluentStatCaption"));
    body->addWidget(label);
    return radio;
  };
  addMode(Mode::Move, Text::tr("Move"),
          Text::tr("The folders are moved into the instance's data folder: seconds on the "
                   "same drive. The Nolvus Dashboard can't update this install any more."));
  addMode(Mode::Copy, Text::tr("Copy"),
          Text::tr("The folders are copied: Nolvus stays as it is and its updates only reach "
                   "its own copy. Needs the free space shown below."))
      ->setChecked(true);
  addMode(Mode::InPlace, Text::tr("Keep in place"),
          Text::tr("The instance uses the folders where they are and owns them: its profiles "
                   "are Nolvus' own, so Nolvus' MO2 and Dashboard see what you change."));

  auto* folderRow = new QHBoxLayout;
  auto* folderLabel = new QLabel(Text::tr("Data folder"), &dlg);
  auto* folder = new QLineEdit(QDir::toNativeSeparators(inst), &dlg);
  folder->setObjectName(QStringLiteral("nolvusTransferFolder"));
  folder->setAccessibleName(Text::tr("Data folder"));
  auto* browse = new QPushButton(Text::tr("Browse…"), &dlg);
  browse->setAutoDefault(false);
  folderRow->addWidget(folderLabel);
  folderRow->addWidget(folder, 1);
  folderRow->addWidget(browse);
  body->addLayout(folderRow);
  auto* sameDrive = new QPushButton(Text::tr("Use a folder on Nolvus' drive"), &dlg);
  sameDrive->setAutoDefault(false);
  body->addWidget(sameDrive, 0, Qt::AlignLeft);

  auto* archivesRow = new QHBoxLayout;
  archivesRow->addWidget(new QLabel(Text::tr("Nolvus archives"), &dlg));
  auto* archives = new QComboBox(&dlg);
  archives->setObjectName(QStringLiteral("nolvusTransferArchives"));
  archives->setAccessibleName(Text::tr("Nolvus archives"));
  archives->addItem(Text::tr("Leave them where they are (listed read-only)"),
                    static_cast<int>(Archives::Leave));
  archives->addItem(Text::tr("Move them into the instance's downloads"),
                    static_cast<int>(Archives::Move));
  archives->addItem(Text::tr("Copy them into the instance's downloads"),
                    static_cast<int>(Archives::Copy));
  archivesRow->addWidget(archives, 1);
  body->addLayout(archivesRow);
  archives->setEnabled(!info.archivePath.isEmpty());

  auto* saves = new QCheckBox(Text::tr("Bring the profiles' saves"), &dlg);
  saves->setChecked(true);
  saves->setVisible(!linked);
  body->addWidget(saves);
  auto* nxm = new QCheckBox(Text::tr("Let this instance handle Nexus links (nxm://)"), &dlg);
  nxm->setObjectName(QStringLiteral("nolvusTransferNxm"));
  body->addWidget(nxm);

  auto* sizesLabel = new QLabel(Text::tr("Measuring the folders…"), &dlg);
  sizesLabel->setWordWrap(true);
  sizesLabel->setObjectName(QStringLiteral("nolvusTransferSizes"));
  body->addWidget(sizesLabel);
  auto* status = new QLabel(&dlg);
  status->setWordWrap(true);
  status->setObjectName(QStringLiteral("nolvusTransferStatus"));
  status->setTextFormat(Qt::RichText);
  body->addWidget(status);

  auto* go     = dlg.addButton(Text::tr("Transfer"), true);
  auto* cancel = dlg.addButton(Text::tr("Cancel"));
  QObject::connect(go, &QPushButton::clicked, &dlg, &QDialog::accept);
  QObject::connect(cancel, &QPushButton::clicked, &dlg, &QDialog::reject);

  // sizes, measured off the GUI thread
  struct Measure
  {
    std::atomic_bool cancel{false}, done{false};
    TransferSizes sizes;
  };
  auto measure = std::make_shared<Measure>();
  std::thread measurer([measure, source] {
    measure->sizes = measureTransfer(source, measure->cancel);
    measure->done  = true;
  });

  const auto options = [&] {
    TransferOptions o;
    o.mode        = static_cast<Mode>(modes->checkedId());
    o.archives    = info.archivePath.isEmpty()
                        ? Archives::Leave
                        : static_cast<Archives>(archives->currentData().toInt());
    o.keepSaves   = saves->isChecked();
    o.registerNxm = nxm->isChecked();
    o.dataDir     = cleanPath(QDir::fromNativeSeparators(folder->text()));
    return o;
  };

  const auto update = [&] {
    const TransferOptions o = options();
    const bool inPlace      = o.mode == Mode::InPlace;
    folder->setEnabled(!inPlace);
    browse->setEnabled(!inPlace);
    sameDrive->setEnabled(!inPlace);
    saves->setEnabled(!inPlace);

    const bool known = measure->done;
    const auto& s    = measure->sizes;
    if (known) {
      sizesLabel->setText(
          Text::tr("Mods %1 · overwrite %2 · stock game %3 · tools %4 · profiles %5 "
                   "(saves %6) · archives %7")
              .arg(localizedByteSize(s.mods), localizedByteSize(s.overwrite),
                   localizedByteSize(s.stockGame), localizedByteSize(s.tools),
                   localizedByteSize(s.profiles), localizedByteSize(s.saves),
                   localizedByteSize(s.archives)));
    }

    QStringList lines;
    bool ok = true;
    const QString data = o.dataDir;
    const QString downloads =
        (inPlace ? inst : (data.isEmpty() ? inst : data)) + QStringLiteral("/downloads");

    if (!inPlace) {
      if (const QString bad = badDataFolder(source, data); !bad.isEmpty()) {
        lines << bad.toHtmlEscaped();
        ok = false;
      }
      for (const QString& sub : {QStringLiteral("mods"), QStringLiteral("overwrite"),
                                 StockGameName, ToolsName}) {
        if (QFileInfo(data + QLatin1Char('/') + sub).exists()) {
          lines << Text::tr("%1 already has a \"%2\" folder: choose an empty folder.")
                       .arg(QDir::toNativeSeparators(data), sub);
          ok = false;
        }
      }
      if (!linked && QFileInfo(data + QStringLiteral("/profiles")).exists()) {
        lines << Text::tr("%1 already has a \"%2\" folder: choose an empty folder.")
                     .arg(QDir::toNativeSeparators(data), QStringLiteral("profiles"));
        ok = false;
      }
    }

    std::uint64_t needed = 0;
    if (o.mode == Mode::Move) {
      if (!sameVolume(info.modsPath, data)) {
        lines << Text::tr("<b>%1 isn't on the same drive as Nolvus</b>: a move must stay on "
                          "one drive. Use a folder on Nolvus' drive, or copy.")
                     .arg(QDir::toNativeSeparators(data).toHtmlEscaped());
        ok = false;
      } else {
        lines << Text::tr("Same drive: the folders are renamed, which takes seconds.");
      }
      lines << Text::tr("<b>After a move, the Nolvus Dashboard can no longer update or "
                        "repair this install.</b> Instances can undo the move.");
    } else if (o.mode == Mode::Copy) {
      needed = s.mods + s.overwrite + s.stockGame + s.tools +
               (linked ? 0 : s.profiles - (o.keepSaves ? 0 : s.saves));
      lines << Text::tr("<b>After a copy, both exist:</b> Nolvus updates only reach "
                        "Nolvus' own copy, not this instance.");
    } else {
      lines << Text::tr("<b>Nolvus' MO2 and Dashboard use the same folders:</b> what you "
                        "change here, they see, and a Nolvus update changes this instance.");
    }
    if (o.archives == Archives::Copy) {
      needed += s.archives;
    } else if (o.archives == Archives::Move && !sameVolume(info.archivePath, downloads)) {
      lines << Text::tr("<b>The archives are on another drive</b> than %1: copy them, or "
                        "leave them where they are.")
                   .arg(QDir::toNativeSeparators(downloads).toHtmlEscaped());
      ok = false;
    }
    if (needed > 0 || o.archives == Archives::Copy || o.mode == Mode::Copy) {
      if (!known) {
        lines << Text::tr("Measuring how much space the copy needs…");
        ok = false;
      } else {
        const QString where = o.mode == Mode::Copy ? data : downloads;
        const std::uint64_t free = freeSpace(where);
        lines << Text::tr("Needs %1; %2 free on that drive.")
                     .arg(localizedByteSize(needed), localizedByteSize(free));
        if (needed > free) {
          lines << Text::tr("<b>Not enough free space.</b>");
          ok = false;
        }
      }
    }
    status->setText(lines.join(QStringLiteral("<br>")));
    go->setEnabled(ok);
  };

  // the archives follow the mode until the user picks: a move takes them along
  // (a rename on the same drive), a copy or keeping in place leaves them
  bool archivesPicked = false;
  QObject::connect(archives, &QComboBox::activated, &dlg, [&](int) { archivesPicked = true; });
  QObject::connect(modes, &QButtonGroup::idClicked, &dlg, [&](int id) {
    if (!archivesPicked && archives->isEnabled()) {
      const bool move = static_cast<Mode>(id) == Mode::Move &&
                        sameVolume(info.archivePath, cleanPath(QDir::fromNativeSeparators(
                                                         folder->text())));
      archives->setCurrentIndex(
          archives->findData(static_cast<int>(move ? Archives::Move : Archives::Leave)));
    }
    update();
  });
  QObject::connect(archives, &QComboBox::currentIndexChanged, &dlg, [&](int) { update(); });
  QObject::connect(saves, &QCheckBox::toggled, &dlg, [&](bool) { update(); });
  QObject::connect(folder, &QLineEdit::textChanged, &dlg, [&](const QString&) { update(); });
  QObject::connect(browse, &QPushButton::clicked, &dlg, [&] {
    const QString d = QFileDialog::getExistingDirectory(&dlg, Text::tr("Data folder"),
                                                        folder->text());
    if (!d.isEmpty()) {
      folder->setText(QDir::toNativeSeparators(
          QDir(d).filePath(MOBase::sanitizeFileName(info.name))));
    }
  });
  QObject::connect(sameDrive, &QPushButton::clicked, &dlg, [&] {
    // beside the Nolvus folder, on its drive
    const QString root = QFileInfo(cleanPath(info.root)).path();
    folder->setText(QDir::toNativeSeparators(
        QDir(root).filePath(QStringLiteral("MO2 Revamped data/") +
                            MOBase::sanitizeFileName(info.name))));
  });

  QTimer poll;
  QObject::connect(&poll, &QTimer::timeout, &dlg, [&] {
    if (measure->done) {
      poll.stop();
      update();
    }
  });
  poll.start(300);
  update();
  dlg.resize(680, dlg.sizeHint().height());

  const bool accepted = dlg.exec() == QDialog::Accepted;
  measure->cancel = true;
  measurer.join();
  if (!accepted) {
    return {};
  }

  // the transfer, with its progress
  const TransferOptions o = options();
  auto progress           = std::make_shared<TransferProgress>();
  std::thread worker([progress, source, o] {
    runTransfer(source, o, *progress);
  });

  QProgressDialog pd(Text::tr("Preparing"), Text::tr("Cancel"), 0, 1000, parent);
  pd.setWindowTitle(Text::tr("Transfer to MO2 Revamped"));
  pd.setWindowModality(Qt::WindowModal);
  pd.setMinimumDuration(0);
  pd.setAutoClose(false);
  pd.setAutoReset(false);
  QElapsedTimer elapsed;
  elapsed.start();
  QTimer tick;
  QEventLoop loop;
  // a cancel puts back what was done, which takes a while: the window stays
  QObject::connect(&pd, &QProgressDialog::canceled, &pd, [&] {
    progress->cancel = true;
    // not from inside the button's own signal
    QTimer::singleShot(0, &pd, [&pd] {
      if (auto* button = pd.findChild<QPushButton*>()) {
        button->setEnabled(false);
      }
      pd.show();
    });
  });
  QObject::connect(&tick, &QTimer::timeout, &pd, [&] {
    QString step;
    {
      std::scoped_lock lock(progress->mutex);
      step = progress->step;
    }
    const std::uint64_t done = progress->done, total = progress->total;
    QString text = step;
    if (total > 0) {
      pd.setValue(static_cast<int>(done * 1000 / total));
      // sizes for copies, counts for moves
      if (total > 100000) {
        const double rate = elapsed.elapsed() > 0 ? done * 1000.0 / elapsed.elapsed() : 0;
        text += QStringLiteral("\n") +
                Text::tr("%1 of %2").arg(localizedByteSize(done), localizedByteSize(total));
        if (rate > 0 && done < total) {
          const qint64 left = static_cast<qint64>((total - done) / rate);
          text += QStringLiteral(" · ") +
                  Text::tr("about %1 left")
                      .arg(QTime(0, 0).addSecs(static_cast<int>(left)).toString(
                          QStringLiteral("H:mm:ss")));
        }
      } else {
        text += QStringLiteral("\n") + Text::tr("%1 of %2").arg(done).arg(total);
      }
    }
    if (progress->cancel) {
      text = Text::tr("Cancelling: putting back what was done") + QStringLiteral("\n") + step;
    }
    pd.setLabelText(text);
    if (progress->finished) {
      loop.quit();
    }
  });
  tick.start(200);
  pd.show();
  loop.exec();
  tick.stop();
  pd.hide();
  worker.join();

  QString error, dir;
  {
    std::scoped_lock lock(progress->mutex);
    error = progress->error;
    dir   = progress->instanceDir;
  }
  if (progress->failed) {
    QMessageBox::warning(parent, Text::tr("Transfer to MO2 Revamped"),
                         Text::tr("The transfer didn't finish:\n%1").arg(error));
    return {};
  }

  QMessageBox::information(
      parent, Text::tr("Transfer to MO2 Revamped"),
      Text::tr("%1 is now a normal instance in %2, done in %3.")
          .arg(info.name, QDir::toNativeSeparators(dir),
               QTime(0, 0).addMSecs(static_cast<int>(elapsed.elapsed()))
                   .toString(QStringLiteral("H:mm:ss"))));
  return dir;
}

}  // namespace Nolvus
