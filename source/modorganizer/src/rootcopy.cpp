#include "rootcopy.h"

#include <QCryptographicHash>
#include <QDir>
#include <QDateTime>
#include <QDirIterator>
#include <QTimeZone>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QSettings>
#include <QtEndian>
#include <QtGlobal>

#include <algorithm>
#include <functional>
#include <set>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

namespace RootCopy
{

static Logger g_logger;

void setLogger(Logger logger)
{
  g_logger = std::move(logger);
}

void info(const QString& message)
{
  if (g_logger) {
    g_logger(false, message);
  } else {
    qInfo().noquote() << message;
  }
}

void warn(const QString& message)
{
  if (g_logger) {
    g_logger(true, message);
  } else {
    qWarning().noquote() << message;
  }
}

static QString native(const QString& path)
{
  return QDir::toNativeSeparators(path);
}

QString under(const QString& dir, const QString& rel)
{
  // a game at a drive root is "D:/" (or "D:\\"): its own separator is the one
  return dir.endsWith(QLatin1Char('/')) || dir.endsWith(QLatin1Char('\\'))
             ? dir + rel
             : dir + QLatin1Char('/') + rel;
}

// --------------------------------------------------------------------------
// PE files

namespace
{

// bounds-checked little-endian reads of a mapped file
class Reader
{
public:
  Reader(const uchar* data, qint64 size) : m_data(data), m_size(size) {}

  bool ok() const { return m_ok; }

  quint32 u16(qint64 offset)
  {
    if (offset < 0 || offset + 2 > m_size) {
      m_ok = false;
      return 0;
    }
    return qFromLittleEndian<quint16>(m_data + offset);
  }

  quint32 u32(qint64 offset)
  {
    if (offset < 0 || offset + 4 > m_size) {
      m_ok = false;
      return 0;
    }
    return qFromLittleEndian<quint32>(m_data + offset);
  }

  QString string(qint64 offset, int max = 512)
  {
    if (offset < 0 || offset >= m_size) {
      m_ok = false;
      return {};
    }
    QByteArray s;
    for (qint64 i = offset; i < m_size && s.size() < max; ++i) {
      if (m_data[i] == 0) {
        return QString::fromLatin1(s);
      }
      s.append(char(m_data[i]));
    }
    m_ok = false;
    return {};
  }

private:
  const uchar* m_data;
  qint64 m_size;
  bool m_ok = true;
};

struct Section
{
  QString name;
  quint32 va, virtualSize, rawPointer, rawSize;
};

}  // namespace

PeFile readPe(const QString& path)
{
  PeFile pe;

  QFile file(path);
  if (!file.open(QIODevice::ReadOnly)) {
    return pe;
  }
  const qint64 size = file.size();
  if (size < 0x40) {
    return pe;
  }
  const uchar* data = file.map(0, size);
  if (data == nullptr) {
    return pe;
  }

  Reader r(data, size);
  if (r.u16(0) != 0x5A4D) {  // MZ
    return pe;
  }
  const qint64 nt = r.u32(0x3C);
  if (r.u32(nt) != 0x00004550) {  // PE\0\0
    return pe;
  }

  const qint64 coff         = nt + 4;
  const quint32 numSections = r.u16(coff + 2);
  const quint32 optSize     = r.u16(coff + 16);
  const qint64 opt          = coff + 20;
  const quint32 magic       = r.u16(opt);
  if (!r.ok() || (magic != 0x10b && magic != 0x20b)) {
    return pe;
  }
  pe.is64 = (magic == 0x20b);

  const quint32 entry   = r.u32(opt + 16);
  const qint64 dirs     = opt + (pe.is64 ? 112 : 96);
  const quint32 numDirs = r.u32(opt + (pe.is64 ? 108 : 92));

  std::vector<Section> sections;
  const qint64 table = opt + optSize;
  for (quint32 i = 0; i < numSections && i < 96; ++i) {
    const qint64 s = table + qint64(i) * 40;
    Section sec;
    QByteArray name;
    for (int c = 0; c < 8; ++c) {
      if (s + c >= size || data[s + c] == 0) {
        break;
      }
      name.append(char(data[s + c]));
    }
    sec.name        = QString::fromLatin1(name);
    sec.virtualSize = r.u32(s + 8);
    sec.va          = r.u32(s + 12);
    sec.rawSize     = r.u32(s + 16);
    sec.rawPointer  = r.u32(s + 20);
    if (!r.ok()) {
      return pe;
    }
    sections.push_back(sec);
    pe.sections.append(sec.name);
  }

  auto toOffset = [&](quint32 rva) -> qint64 {
    for (const auto& sec : sections) {
      const quint32 span = std::max(sec.virtualSize, sec.rawSize);
      if (rva >= sec.va && rva < sec.va + span) {
        const quint32 delta = rva - sec.va;
        if (delta >= sec.rawSize) {
          return -1;
        }
        return qint64(sec.rawPointer) + delta;
      }
    }
    return -1;
  };

  pe.entryInSection = false;
  for (const auto& sec : sections) {
    const quint32 span = std::max(sec.virtualSize, sec.rawSize);
    if (entry >= sec.va && entry < sec.va + span) {
      pe.entryInSection = true;
      break;
    }
  }
  if (entry == 0) {
    // a DLL without an entry point
    pe.entryInSection = true;
  }

  // imports
  if (numDirs > 1) {
    const quint32 importRva = r.u32(dirs + 8);
    qint64 desc             = importRva ? toOffset(importRva) : -1;
    for (int i = 0; desc >= 0 && i < 4096; ++i, desc += 20) {
      const quint32 originalThunk = r.u32(desc);
      const quint32 nameRva       = r.u32(desc + 12);
      const quint32 firstThunk    = r.u32(desc + 16);
      if (!r.ok() || (nameRva == 0 && originalThunk == 0 && firstThunk == 0)) {
        break;
      }
      const qint64 nameOffset = toOffset(nameRva);
      if (nameOffset < 0) {
        break;
      }
      const QString name = r.string(nameOffset, 260);
      if (!r.ok()) {
        break;
      }
      pe.imports.append(name.toLower());
    }
  }

  // exports
  if (numDirs > 0) {
    const quint32 exportRva = r.u32(dirs);
    const qint64 exp        = exportRva ? toOffset(exportRva) : -1;
    if (exp >= 0) {
      const quint32 base      = r.u32(exp + 16);
      const quint32 functions = r.u32(exp + 20);
      const quint32 names     = r.u32(exp + 24);
      const quint32 functionsRva = r.u32(exp + 28);
      const quint32 namesRva     = r.u32(exp + 32);
      // an ordinal to import by: the first slot with a function (Base can be
      // an empty one)
      const qint64 functionTable = r.ok() ? toOffset(functionsRva) : -1;
      for (quint32 i = 0; functionTable >= 0 && i < functions && i < 65536; ++i) {
        const quint32 address = r.u32(functionTable + qint64(i) * 4);
        if (!r.ok()) {
          break;
        }
        if (address != 0) {
          pe.hasExports    = true;
          pe.exportOrdinal = base + i;
          break;
        }
      }
      if (pe.hasExports) {
        if (names > 0) {
          const qint64 nameTable = toOffset(namesRva);
          const qint64 first     = nameTable >= 0 ? toOffset(r.u32(nameTable)) : -1;
          if (first >= 0) {
            const QString name = r.string(first, 512);
            if (r.ok()) {
              pe.exportName = name;
            }
          }
        }
      }
    }
  }

  pe.valid = r.ok() || !pe.sections.isEmpty();
  return pe;
}

bool isSteamStub(const PeFile& pe)
{
  return pe.valid && (pe.sections.contains(QStringLiteral(".bind"), Qt::CaseInsensitive) ||
                      !pe.entryInSection);
}

QByteArray loweredBytes(const QString& path)
{
  QFile file(path);
  // a game's DLLs are a few MB; a 512 MB file is no DLL
  if (!file.open(QIODevice::ReadOnly) || file.size() > 512ll * 1024 * 1024) {
    return {};
  }
  QByteArray bytes = file.readAll();
  // ASCII letters to lower case; UTF-16's high bytes are 0 for those, so the
  // same buffer serves both
  for (char& c : bytes) {
    if (c >= 'A' && c <= 'Z') {
      c = char(c - 'A' + 'a');
    }
  }
  return bytes;
}

QStringList mentionedNames(const QByteArray& lowered, const QStringList& names)
{
  QStringList found;
  for (const QString& name : names) {
    const QByteArray ascii = name.toLower().toLatin1();
    QByteArray wide;
    for (char c : ascii) {
      wide.append(c);
      wide.append('\0');
    }
    if (lowered.contains(ascii) || lowered.contains(wide)) {
      found.append(name);
    }
  }
  return found;
}

QStringList mentionedNames(const QString& path, const QStringList& names)
{
  if (names.isEmpty()) {
    return {};
  }
  return mentionedNames(loweredBytes(path), names);
}

// --------------------------------------------------------------------------
// copy and restore
//
// Each step leaves whole files only: a file is copied to a temporary name
// next to its destination, its hash checked, and then renamed over the
// destination in one step. The manifest records each entry's state after
// the step finishes:
//
//   planned   nothing done yet (the game's file, if any, is in place)
//   backedUp  the game's file is in the backup folder, whole and checked;
//             it may still be in the game folder too
//   copied    the mod's file is in the game folder, whole and checked
//
// Recovery only removes a file whose hash is the copy's, only puts back a
// backup whose hash is the game file's, and leaves a file that is neither
// (something else changed it) where it is, with its backup, for the user.

static const QString ManifestName = QStringLiteral("manifest.json");
static const QString PartSuffix   = QStringLiteral(".mo2r-part");

QString manifestPath(const QString& stateDir)
{
  return stateDir + QStringLiteral("/") + ManifestName;
}

bool hasManifest(const QString& stateDir)
{
  return QFileInfo::exists(manifestPath(stateDir));
}

static QString fileHash(const QString& path)
{
  QFile f(path);
  if (!f.open(QIODevice::ReadOnly)) {
    return {};
  }
  QCryptographicHash hash(QCryptographicHash::Sha256);
  if (!hash.addData(&f)) {
    return {};
  }
  return QString::fromLatin1(hash.result().toHex());
}

// a game file or a copy from a read-only source can't be removed or replaced
static void makeWritable(const QString& path)
{
  const auto perms = QFile::permissions(path);
  if (!(perms & QFileDevice::WriteOwner)) {
    QFile::setPermissions(path, perms | QFileDevice::WriteOwner | QFileDevice::WriteUser);
  }
}

static bool removeFile(const QString& path)
{
  if (!QFileInfo::exists(path)) {
    return true;
  }
  makeWritable(path);
  return QFile::remove(path);
}

// programs and libraries never go to Overwrite: a changed one isn't a setting
static bool isProgramFile(const QString& path)
{
  static const QStringList extensions = {"exe", "dll", "asi", "sys", "ocx", "cpl"};
  if (extensions.contains(QFileInfo(path).suffix().toLower())) {
    return true;
  }
  QFile f(path);
  return f.open(QIODevice::ReadOnly) && f.read(2) == "MZ";
}

static QString partPath(const QString& destination)
{
  const QFileInfo fi(destination);
  return fi.absolutePath() + QStringLiteral("/.") + fi.fileName() + PartSuffix;
}

// copies from to a temporary name next to to, checks its hash, and renames it
// over to in one step; to is never left partial
static bool copyWhole(const QString& from, const QString& to, const QString& hash)
{
  const QString part = partPath(to);
  if (!QDir().mkpath(QFileInfo(to).absolutePath()) || !removeFile(part)) {
    return false;
  }
  if (!QFile::copy(from, part)) {
    return false;
  }
  makeWritable(part);
  if (fileHash(part) != hash) {
    removeFile(part);
    return false;
  }
  if (QFileInfo::exists(to)) {
    makeWritable(to);
  }
  const std::wstring source = QDir::toNativeSeparators(part).toStdWString();
  const std::wstring target = QDir::toNativeSeparators(to).toStdWString();
  if (!::MoveFileExW(source.c_str(), target.c_str(),
                     MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
    removeFile(part);
    return false;
  }
  return true;
}

static bool writeManifest(const QString& stateDir, const QJsonObject& manifest)
{
  if (!QDir().mkpath(stateDir)) {
    return false;
  }
  QSaveFile file(manifestPath(stateDir));
  if (!file.open(QIODevice::WriteOnly)) {
    return false;
  }
  file.write(QJsonDocument(manifest).toJson(QJsonDocument::Indented));
  return file.commit();
}

static QJsonObject readManifest(const QString& stateDir, bool* ok)
{
  *ok = false;
  QFile file(manifestPath(stateDir));
  if (!file.open(QIODevice::ReadOnly)) {
    return {};
  }
  QJsonParseError error;
  const auto doc = QJsonDocument::fromJson(file.readAll(), &error);
  if (error.error != QJsonParseError::NoError || !doc.isObject()) {
    return {};
  }
  *ok = true;
  return doc.object();
}

static QString backupPath(const QString& stateDir, const QString& rel)
{
  return stateDir + QStringLiteral("/files/") + rel;
}

bool gameDirWritable(const QString& gameDir)
{
  if (!QFileInfo(gameDir).isDir()) {
    return false;
  }
  QFile test(under(gameDir, QStringLiteral(".mo2r-rootcopy-test")));
  if (!test.open(QIODevice::WriteOnly)) {
    return false;
  }
  test.close();
  test.remove();
  return true;
}

namespace
{

// the manifest and its entries, saved after each step
class Manifest
{
public:
  Manifest(QString stateDir, QJsonObject object)
      : m_stateDir(std::move(stateDir)), m_object(std::move(object)),
        m_files(m_object.value("files").toArray())
  {}

  int size() const { return int(m_files.size()); }
  QJsonObject entry(int i) const { return m_files.at(i).toObject(); }
  QString state(int i) const { return entry(i).value("state").toString(); }

  void append(const QJsonObject& e) { m_files.append(e); }

  bool setState(int i, const QString& state)
  {
    QJsonObject e = entry(i);
    e.insert("state", state);
    m_files.replace(i, e);
    return save();
  }

  bool setHash(int i, const QString& hash)
  {
    QJsonObject e = entry(i);
    e.insert("hash", hash);
    m_files.replace(i, e);
    return save();
  }

  bool remove(int i)
  {
    m_files.removeAt(i);
    return save();
  }

  QJsonObject& object() { return m_object; }

  bool save()
  {
    m_object.insert("files", m_files);
    const bool ok = writeManifest(m_stateDir, m_object);
    if (!ok) {
      warn(QStringLiteral("root copy: can't write %1")
               .arg(native(manifestPath(m_stateDir))));
    }
    return ok;
  }

private:
  QString m_stateDir;
  QJsonObject m_object;
  QJsonArray m_files;
};

}  // namespace

// puts the game's file back from the backup (whole), then drops the backup
static bool restoreBackup(const QString& backup, const QString& target,
                          const QString& backupHash)
{
  if (!copyWhole(backup, target, backupHash)) {
    return false;
  }
  removeFile(backup);
  return true;
}

// the empty folders below root, deepest first
static void removeEmptyDirs(const QString& root)
{
  QStringList found;
  QDirIterator it(root, QDir::Dirs | QDir::NoDotAndDotDot | QDir::Hidden,
                  QDirIterator::Subdirectories);
  while (it.hasNext()) {
    found.append(it.next());
  }
  std::sort(found.begin(), found.end(), [](const QString& a, const QString& b) {
    return a.size() > b.size();
  });
  for (const QString& dir : found) {
    QDir().rmdir(dir);
  }
  QDir().rmdir(root);
}

namespace
{

// the sources' hashes by path, size and time, kept between runs: an
// unchanged mod file (a 15 MB exe, an ENB folder) isn't read again
class HashCache
{
public:
  explicit HashCache(QString path) : m_path(std::move(path))
  {
    if (m_path.isEmpty()) {
      return;
    }
    QFile file(m_path);
    if (file.open(QIODevice::ReadOnly)) {
      m_files = QJsonDocument::fromJson(file.readAll()).object();
    }
  }

  ~HashCache()
  {
    if (m_changed && !m_path.isEmpty()) {
      QDir().mkpath(QFileInfo(m_path).absolutePath());
      QSaveFile file(m_path);
      if (file.open(QIODevice::WriteOnly)) {
        file.write(QJsonDocument(m_files).toJson(QJsonDocument::Compact));
        file.commit();
      }
    }
  }

  // from the cache when the file is the same one: path, size, times and
  // NTFS file id (a reinstalled file of the same size and time, on exFAT's
  // 2 s or from a stamped archive, still differs in creation time or id)
  QString hash(const QString& path, bool* fromCache = nullptr)
  {
    if (fromCache) {
      *fromCache = false;
    }
    const QString key  = keyOf(path);
    const QJsonObject e = m_files.value(key).toObject();
    const QJsonObject now = identity(path);
    if (!m_path.isEmpty() && !now.isEmpty() && !e.value("hash").toString().isEmpty()) {
      QJsonObject known = e;
      known.remove("hash");
      if (known == now) {
        if (fromCache) {
          *fromCache = true;
        }
        return e.value("hash").toString();
      }
    }
    const QString h = fileHash(path);
    set(path, h);
    return h;
  }

  // the hash read from the file itself, replacing what the cache had
  void set(const QString& path, const QString& h)
  {
    if (h.isEmpty() || m_path.isEmpty()) {
      return;
    }
    QJsonObject e = identity(path);
    if (e.isEmpty()) {
      return;
    }
    e.insert("hash", h);
    m_files.insert(keyOf(path), e);
    m_changed = true;
  }

private:
  static QString keyOf(const QString& path)
  {
    return QDir::cleanPath(QFileInfo(path).absoluteFilePath()).toLower();
  }

  static QJsonObject identity(const QString& path)
  {
    const QFileInfo fi(path);
    if (!fi.isFile()) {
      return {};
    }
    QJsonObject id{
        {"size", fi.size()},
        {"mtime", fi.lastModified(QTimeZone::UTC).toMSecsSinceEpoch()},
        {"btime", fi.birthTime(QTimeZone::UTC).toMSecsSinceEpoch()}};
    const std::wstring native = QDir::toNativeSeparators(path).toStdWString();
    HANDLE h = ::CreateFileW(native.c_str(), 0,
                             FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                             nullptr, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, nullptr);
    if (h != INVALID_HANDLE_VALUE) {
      BY_HANDLE_FILE_INFORMATION info = {};
      if (::GetFileInformationByHandle(h, &info)) {
        id.insert("volume", qint64(info.dwVolumeSerialNumber));
        id.insert("index", QString::number((quint64(info.nFileIndexHigh) << 32) |
                                           info.nFileIndexLow));
      }
      ::CloseHandle(h);
    }
    return id;
  }

public:

private:
  QString m_path;
  QJsonObject m_files;
  bool m_changed = false;
};

// what other instances' manifests say they copied into gameDir: path in lower
// case -> (the copy's hash, that instance's state folder)
QMap<QString, QPair<QString, QString>> foreignCopies(const QStringList& stateDirs,
                                                     const QString& gameDir)
{
  QMap<QString, QPair<QString, QString>> found;
  for (const QString& dir : stateDirs) {
    bool ok              = false;
    const QJsonObject m  = readManifest(dir, &ok);
    if (!ok || QDir::cleanPath(m.value("gameDir").toString())
                       .compare(QDir::cleanPath(gameDir), Qt::CaseInsensitive) != 0) {
      continue;
    }
    for (const auto& v : m.value("files").toArray()) {
      const QJsonObject e = v.toObject();
      found.insert(e.value("path").toString().toLower(),
                   {e.value("hash").toString(), dir});
    }
  }
  return found;
}

// an entry a restore leaves for the user: its file is neither the copy nor
// the game's (and not a setting the program changed), or its backup is cut
bool isConflict(const QJsonObject& e, const QString& gameDir, const QString& stateDir)
{
  const QString rel        = e.value("path").toString();
  const QString target     = under(gameDir, rel);
  const QString saved      = backupPath(stateDir, rel);
  const bool hadBackup     = e.value("backup").toBool();
  const QString backupHash = e.value("backupHash").toString();
  const QString state      = e.value("state").toString(QStringLiteral("copied"));

  if (QFileInfo::exists(target)) {
    const QString current = fileHash(target);
    if (current == e.value("hash").toString() || (hadBackup && current == backupHash)) {
      return false;
    }
    return isProgramFile(target) || state != "copied" ||
           (hadBackup && !(QFileInfo::exists(saved) && fileHash(saved) == backupHash));
  }
  return hadBackup && QFileInfo::exists(saved) && fileHash(saved) != backupHash;
}

}  // namespace

bool anyManifest(const QString& base)
{
  for (const QString& dir : QDir(base).entryList(QDir::Dirs | QDir::NoDotAndDotDot)) {
    if (hasManifest(base + "/" + dir)) {
      return true;
    }
  }
  return false;
}

int settleConflicts(const QString& stateDir)
{
  bool ok            = false;
  QJsonObject object = readManifest(stateDir, &ok);
  if (!ok) {
    return 0;
  }
  const QString gameDir = object.value("gameDir").toString();
  const QString stamp =
      QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd-HHmmss"));
  const QString keep = stateDir + QStringLiteral("/conflicts/") + stamp;

  QJsonArray kept;
  int settled = 0;
  for (const auto& v : object.value("files").toArray()) {
    const QJsonObject e = v.toObject();
    if (!isConflict(e, gameDir, stateDir)) {
      kept.append(e);
      continue;
    }
    const QString rel    = e.value("path").toString();
    const QString saved  = backupPath(stateDir, rel);
    const QString target = under(gameDir, rel);
    if (QFileInfo::exists(saved)) {
      const QString dest = keep + "/" + rel;
      if (!copyWhole(saved, dest, fileHash(saved)) || !removeFile(saved)) {
        warn(QStringLiteral("root copy: can't move %1 to %2, the entry stays")
                 .arg(native(saved), native(dest)));
        kept.append(e);
        continue;
      }
      warn(QStringLiteral("root copy: %1 was changed by something else and stays as "
                          "it is; the game's earlier file is kept in %2")
               .arg(native(target), native(dest)));
    } else {
      warn(QStringLiteral("root copy: %1 was changed by something else and stays as "
                          "it is")
               .arg(native(target)));
    }
    ++settled;
  }

  if (settled == 0) {
    return 0;
  }
  if (kept.isEmpty()) {
    QFile::remove(manifestPath(stateDir));
    removeEmptyDirs(stateDir + QStringLiteral("/files"));
  } else {
    object.insert("files", kept);
    if (!writeManifest(stateDir, object)) {
      warn(QStringLiteral("root copy: can't write %1").arg(native(manifestPath(stateDir))));
    }
  }
  return settled;
}
Result deploy(const std::vector<File>& files, const QString& gameDir,
              const QString& stateDir, const QString& overwriteRoot,
              const DeployOptions& options)
{
  Result result;
  if (files.empty()) {
    return result;
  }

  if (!gameDirWritable(gameDir)) {
    result.notWritable = true;
    warn(QStringLiteral("root copy: the game folder %1 can't be written to; the "
                        "Root files that must be real stay VFS-only")
             .arg(native(gameDir)));
    return result;
  }

  // entries a restore left for the user are settled once nothing runs, so they
  // don't block their file for good
  if (!options.othersRunning && hasManifest(stateDir)) {
    result.settled = settleConflicts(stateDir);
  }

  // what other instances copied into this game folder
  const QMap<QString, QPair<QString, QString>> foreign =
      foreignCopies(options.otherStateDirs, gameDir);
  HashCache sources(options.hashCache);

  QJsonObject object;
  if (hasManifest(stateDir)) {
    bool ok = false;
    object  = readManifest(stateDir, &ok);
    if (!ok) {
      result.error = QStringLiteral("the manifest %1 can't be read")
                         .arg(native(manifestPath(stateDir)));
      warn(QStringLiteral("root copy: ") + result.error);
      return result;
    }
    if (QDir::cleanPath(object.value("gameDir").toString())
            .compare(QDir::cleanPath(gameDir), Qt::CaseInsensitive) != 0) {
      result.error = QStringLiteral("the manifest is for another game folder, %1")
                         .arg(native(object.value("gameDir").toString()));
      warn(QStringLiteral("root copy: ") + result.error);
      return result;
    }
  } else {
    object.insert("version", 2);
    object.insert("gameDir", QDir::cleanPath(gameDir));
    object.insert("overwriteRoot", QDir::cleanPath(overwriteRoot));
  }

  Manifest manifest(stateDir, object);

  QMap<QString, QString> existing;
  for (int i = 0; i < manifest.size(); ++i) {
    existing.insert(manifest.entry(i).value("path").toString().toLower(),
                    manifest.entry(i).value("hash").toString());
  }
  QJsonArray dirs = object.value("dirs").toArray();
  std::set<QString> knownDirs;
  for (const auto& v : dirs) {
    knownDirs.insert(v.toString().toLower());
  }

  std::vector<int> added;
  std::set<QString> planned;

  for (const auto& f : files) {
    const QString rel = QDir::cleanPath(f.relativePath);
    if (rel.isEmpty() || rel == "." || rel.startsWith("..") || QDir::isAbsolutePath(rel) ||
        !planned.insert(rel.toLower()).second) {
      continue;
    }
    const QString target = under(gameDir, rel);
    bool cached    = false;
    QString hash   = sources.hash(f.source, &cached);
    if (hash.isEmpty()) {
      warn(QStringLiteral("root copy: can't read %1, not copied").arg(native(f.source)));
      ++result.failed;
      result.failedFiles.append(rel);
      continue;
    }

    if (existing.contains(rel.toLower())) {
      if (existing.value(rel.toLower()) != hash) {
        warn(QStringLiteral("root copy: %1 is already copied for a program that still "
                            "runs, from another file; kept as it is")
                 .arg(rel));
      }
      continue;
    }

    QString backupHash;
    if (QFileInfo::exists(target)) {
      backupHash = fileHash(target);
      // a cached hash that says "the same" is checked against the file itself
      if (backupHash == hash && cached) {
        hash = fileHash(f.source);
        sources.set(f.source, hash);
      }
      if (backupHash == hash) {
        info(QStringLiteral("root copy: %1 is already the same in the game folder, "
                            "left as it is")
                 .arg(rel));
        continue;
      }
      if (backupHash.isEmpty()) {
        warn(QStringLiteral("root copy: can't read the game's %1, not copied")
                 .arg(native(target)));
        ++result.failed;
        result.failedFiles.append(rel);
        continue;
      }
      // another instance's copy, not the game's file: that instance restores it
      const auto other = foreign.find(rel.toLower());
      if (other != foreign.end() && other->first == backupHash) {
        warn(QStringLiteral("root copy: %1 in the game folder is another instance's copy "
                            "(%2); left to it, not copied")
                 .arg(rel, native(manifestPath(other->second))));
        ++result.foreign;
        result.foreignFiles.append(rel);
        result.foreignStates.append(other->second);
        continue;
      }
    }

    // the folders this file needs that the game folder doesn't have yet, so
    // the restore takes them out again
    QStringList parents;
    for (QString dir = QFileInfo(rel).path(); dir != "." && !dir.isEmpty();
         dir         = QFileInfo(dir).path()) {
      parents.prepend(dir);
    }
    for (const QString& dir : parents) {
      if (!QFileInfo(under(gameDir, dir)).isDir() &&
          knownDirs.insert(dir.toLower()).second) {
        dirs.append(dir);
      }
    }

    QJsonObject entry;
    entry.insert("path", rel);
    entry.insert("source", f.source);
    entry.insert("hash", hash);
    entry.insert("backup", !backupHash.isEmpty());
    entry.insert("backupHash", backupHash);
    entry.insert("size", QFileInfo(f.source).size());
    entry.insert("backupSize", backupHash.isEmpty() ? qint64(-1) : QFileInfo(target).size());
    entry.insert("reason", f.reason);
    entry.insert("state", "planned");
    added.push_back(manifest.size());
    manifest.append(entry);
  }

  if (added.empty()) {
    return result;
  }

  // written before anything changes: a crash from here on is undone from it
  manifest.object().insert("dirs", dirs);
  if (!manifest.save()) {
    result.error =
        QStringLiteral("can't write %1").arg(native(manifestPath(stateDir)));
    warn(QStringLiteral("root copy: nothing copied"));
    return result;
  }

  // the entries that end up not done, dropped from the manifest at the end
  // (only when their game file is back in place)
  std::vector<int> undone;

  for (const int i : added) {
    const QJsonObject e      = manifest.entry(i);
    const QString rel        = e.value("path").toString();
    const QString source     = e.value("source").toString();
    const QString target     = under(gameDir, rel);
    const QString hash       = e.value("hash").toString();
    const bool backup        = e.value("backup").toBool();
    const QString backupHash = e.value("backupHash").toString();
    const QString saved      = backupPath(stateDir, rel);

    if (backup) {
      // copied and checked first; the game's file goes only after that
      if (!copyWhole(target, saved, backupHash)) {
        warn(QStringLiteral("root copy: can't back up %1, not copied").arg(native(target)));
        ++result.failed;
        result.failedFiles.append(rel);
        undone.push_back(i);
        continue;
      }
      if (!manifest.setState(i, "backedUp")) {
        result.error = QStringLiteral("can't write the manifest");
        break;
      }
      ++result.backedUp;
      info(QStringLiteral("root copy: backed up %1 to %2")
               .arg(native(target), native(saved)));
    }

    bool copied = copyWhole(source, target, hash);
    if (!copied) {
      // a cached hash for a file that changed without its size, times or id:
      // read again, recorded, and tried once more
      const QString fresh = fileHash(source);
      if (!fresh.isEmpty() && fresh != hash) {
        sources.set(source, fresh);
        info(QStringLiteral("root copy: %1 changed since its hash was kept, read again")
                 .arg(native(source)));
        if (manifest.setHash(i, fresh)) {
          copied = copyWhole(source, target, fresh);
        }
      }
    }
    if (!copied) {
      warn(QStringLiteral("root copy: can't copy %1 to %2 (in use?)")
               .arg(native(source), native(target)));
      ++result.failed;
      result.failedFiles.append(rel);
      // the game's file is still in place (the copy never replaced it); the
      // backup isn't needed
      if (backup && fileHash(target) == backupHash) {
        removeFile(saved);
        undone.push_back(i);
      } else if (!backup) {
        undone.push_back(i);
      }
      // else: kept in the manifest, recovery sorts it out
      continue;
    }

    if (!manifest.setState(i, "copied")) {
      result.error = QStringLiteral("can't write the manifest");
      break;
    }
    ++result.copied;
    info(QStringLiteral("root copy: copied %1 to %2").arg(native(source), native(target)));
  }

  // the entries not done, highest index first so the others keep theirs
  std::sort(undone.begin(), undone.end(), std::greater<int>());
  for (const int i : undone) {
    if (!manifest.remove(i)) {
      result.error = QStringLiteral("can't write the manifest");
      break;
    }
  }

  return result;
}

Result restore(const QString& stateDir)
{
  Result result;
  bool ok = false;
  QJsonObject object = readManifest(stateDir, &ok);
  if (!ok) {
    if (hasManifest(stateDir)) {
      result.error = QStringLiteral("the manifest %1 can't be read")
                         .arg(native(manifestPath(stateDir)));
      warn(QStringLiteral("root copy: ") + result.error);
    }
    return result;
  }

  const QString gameDir       = object.value("gameDir").toString();
  const QString overwriteRoot = object.value("overwriteRoot").toString();
  const QJsonArray entries    = object.value("files").toArray();
  QJsonArray remaining;

  // the last copied first, as a stack
  for (auto i = entries.size() - 1; i >= 0; --i) {
    const QJsonObject e      = entries.at(i).toObject();
    const QString rel        = e.value("path").toString();
    const QString target     = under(gameDir, rel);
    const QString hash       = e.value("hash").toString();
    const bool hadBackup     = e.value("backup").toBool();
    const QString backupHash = e.value("backupHash").toString();
    // a manifest without states (version 1) is read as fully copied
    const QString state      = e.value("state").toString(QStringLiteral("copied"));
    const QString saved      = backupPath(stateDir, rel);

    // what a step cut short left behind: never whole, never used
    removeFile(partPath(target));
    removeFile(partPath(saved));

    // sizes first: a file of neither size isn't read to be told apart
    const qint64 size       = e.value("size").toInteger(-1);
    const qint64 backupSize = e.value("backupSize").toInteger(-1);
    const bool backupThere  = hadBackup && QFileInfo::exists(saved);
    const bool backupWhole  = backupThere &&
                             (backupSize < 0 || QFileInfo(saved).size() == backupSize) &&
                             fileHash(saved) == backupHash;
    bool done               = true;

    if (QFileInfo::exists(target)) {
      const qint64 targetSize = QFileInfo(target).size();
      const bool maybeKnown   = size < 0 || targetSize == size ||
                              (hadBackup && (backupSize < 0 || targetSize == backupSize));
      const QString current   = maybeKnown ? fileHash(target) : QString();
      if (current == hash && hadBackup) {
        // ours over the game's: the game's file renamed back over it in one step
        if (backupWhole && restoreBackup(saved, target, backupHash)) {
          ++result.removed;
          ++result.restored;
          info(QStringLiteral("root copy: restored %1 from the backup").arg(native(target)));
        } else if (!backupWhole) {
          warn(QStringLiteral("root copy: the backup %1 of %2 isn't whole; the copy is "
                              "left in place")
                   .arg(native(saved), native(target)));
          done = false;
          ++result.conflicts;
          result.conflictFiles.append(native(target));
        } else {
          warn(QStringLiteral("root copy: can't restore %1 from %2 (in use?), left for "
                              "later")
                   .arg(native(target), native(saved)));
          done = false;
          ++result.pending;
        }
      } else if (current == hash) {
        // ours, where the game had none
        if (removeFile(target)) {
          ++result.removed;
          info(QStringLiteral("root copy: removed %1").arg(native(target)));
        } else {
          warn(QStringLiteral("root copy: can't remove %1 (in use?), left for later")
                   .arg(native(target)));
          done = false;
          ++result.pending;
        }
      } else if (hadBackup && current == backupHash) {
        // the game's own file never left (or is back): the backup isn't needed
        removeFile(saved);
      } else if (!hadBackup && state == "copied" && !isProgramFile(target) &&
                 !overwriteRoot.isEmpty()) {
        // a setting the program changed (enblocal.ini): kept in Overwrite,
        // where it's the highest priority Root file the next time
        const QString kept = overwriteRoot + "/" + rel;
        if (copyWhole(target, kept, fileHash(target)) && removeFile(target)) {
          ++result.kept;
          info(QStringLiteral("root copy: %1 was changed while the program ran, moved "
                              "to %2")
                   .arg(native(target), native(kept)));
        } else {
          warn(QStringLiteral("root copy: %1 was changed while the program ran and "
                              "can't be moved to %2, left for later")
                   .arg(native(target), native(kept)));
          done = false;
          ++result.pending;
        }
      } else if (hadBackup && state == "copied" && !isProgramFile(target) &&
                 backupWhole && !overwriteRoot.isEmpty()) {
        // the same, for a setting that replaced one of the game's
        const QString kept = overwriteRoot + "/" + rel;
        if (copyWhole(target, kept, fileHash(target)) &&
            restoreBackup(saved, target, backupHash)) {
          ++result.kept;
          ++result.restored;
          info(QStringLiteral("root copy: %1 was changed while the program ran, moved "
                              "to %2; the game's own restored")
                   .arg(native(target), native(kept)));
        } else {
          warn(QStringLiteral("root copy: %1 was changed while the program ran and "
                              "can't be moved to %2, left for later")
                   .arg(native(target), native(kept)));
          done = false;
          ++result.pending;
        }
      } else {
        // neither the copy nor the game's file (a game update, another tool):
        // left as it is, with the backup, for the user to check
        warn(QStringLiteral("root copy: %1 is neither the copied file nor the game's "
                            "own; left as it is%2")
                 .arg(native(target),
                      backupThere ? QStringLiteral(", the game's own is kept in %1")
                                        .arg(native(saved))
                                  : QString()));
        done = false;
        ++result.conflicts;
        result.conflictFiles.append(native(target));
      }
    } else if (hadBackup) {
      // the copy never arrived or is gone: the game's file back, if the backup
      // is whole; a backup step cut short means the game's file never left
      if (backupWhole) {
        if (restoreBackup(saved, target, backupHash)) {
          ++result.restored;
          info(QStringLiteral("root copy: restored %1 from the backup").arg(native(target)));
        } else {
          warn(QStringLiteral("root copy: can't restore %1 from %2, left for later")
                   .arg(native(target), native(saved)));
          done = false;
          ++result.pending;
        }
      } else if (backupThere) {
        warn(QStringLiteral("root copy: %1 is missing and its backup %2 isn't whole; "
                            "left as it is")
                 .arg(native(target), native(saved)));
        done = false;
        ++result.conflicts;
        result.conflictFiles.append(native(target));
      } else if (state != "planned") {
        warn(QStringLiteral("root copy: %1 and its backup are both missing").arg(rel));
        ++result.conflicts;
        result.conflictFiles.append(native(target));
      }
    }

    if (!done) {
      remaining.prepend(e);
    }
  }

  // the folders the copies made, deepest first; rmdir() only takes empty ones
  QStringList dirs;
  for (const auto& v : object.value("dirs").toArray()) {
    dirs.append(v.toString());
  }
  std::sort(dirs.begin(), dirs.end(), [](const QString& a, const QString& b) {
    return a.size() > b.size();
  });
  for (const QString& dir : dirs) {
    QDir().rmdir(under(gameDir, dir));
  }

  if (remaining.isEmpty()) {
    if (!QFile::remove(manifestPath(stateDir))) {
      result.error = QStringLiteral("can't remove %1").arg(native(manifestPath(stateDir)));
      warn(QStringLiteral("root copy: ") + result.error);
    }
    removeEmptyDirs(stateDir + QStringLiteral("/files"));
    QDir().rmdir(stateDir);
    info(QStringLiteral("root copy: the game folder %1 is as it was (%2 removed, %3 "
                        "restored, %4 kept in Overwrite)")
             .arg(native(gameDir))
             .arg(result.removed)
             .arg(result.restored)
             .arg(result.kept));
  } else {
    object.insert("files", remaining);
    if (!writeManifest(stateDir, object)) {
      result.error = QStringLiteral("can't write %1").arg(native(manifestPath(stateDir)));
      warn(QStringLiteral("root copy: ") + result.error);
    }
    warn(QStringLiteral("root copy: %1 files in %2 aren't restored yet (%3 in use, %4 "
                        "changed by something else)")
             .arg(remaining.size())
             .arg(native(gameDir))
             .arg(result.pending)
             .arg(result.conflicts));
  }

  return result;
}

bool programsRunning(int activeLaunches, std::vector<void*>& handles,
                     qint64 msSinceLastStart, qint64 graceMs,
                     const std::function<std::optional<bool>()>& vfsQuery)
{
  if (activeLaunches > 0) {
    return true;
  }

  bool alive = false;
  for (auto it = handles.begin(); it != handles.end();) {
    if (::WaitForSingleObject(static_cast<HANDLE>(*it), 0) == WAIT_TIMEOUT) {
      alive = true;
      ++it;
    } else {
      ::CloseHandle(static_cast<HANDLE>(*it));
      it = handles.erase(it);
    }
  }
  if (alive || (msSinceLastStart >= 0 && msSinceLastStart < graceMs)) {
    return true;
  }

  const std::optional<bool> listed = vfsQuery ? vfsQuery() : std::optional<bool>();
  return !listed.has_value() || *listed;
}

void closeEnded(std::vector<void*>& handles)
{
  for (auto it = handles.begin(); it != handles.end();) {
    if (::WaitForSingleObject(static_cast<HANDLE>(*it), 0) != WAIT_TIMEOUT) {
      ::CloseHandle(static_cast<HANDLE>(*it));
      it = handles.erase(it);
    } else {
      ++it;
    }
  }
}

bool endedWithin(void* process, qint64 ms)
{
  FILETIME created, exited, kernel, user;
  if (process == nullptr ||
      ::WaitForSingleObject(static_cast<HANDLE>(process), 0) != WAIT_OBJECT_0 ||
      !::GetProcessTimes(static_cast<HANDLE>(process), &created, &exited, &kernel, &user)) {
    return false;
  }
  auto ticks = [](const FILETIME& t) {
    return (qint64(t.dwHighDateTime) << 32) | t.dwLowDateTime;
  };
  // 100 ns units
  return (ticks(exited) - ticks(created)) / 10000 < ms;
}

QStringList instanceStateDirs(const std::vector<QString>& instancePaths,
                              const QString& ownBase)
{
  QStringList found;
  const QString own = QDir::cleanPath(ownBase);
  for (const QString& path : instancePaths) {
    // the instance's base folder as Settings reads it: [Settings]
    // base_directory of its ModOrganizer.ini, the instance folder by default
    const QSettings ini(path + QStringLiteral("/ModOrganizer.ini"), QSettings::IniFormat);
    const QString instanceBase = QDir::fromNativeSeparators(
        ini.value(QStringLiteral("Settings/base_directory"), path).toString());
    const QString base = QDir::cleanPath(instanceBase + QStringLiteral("/rootcopy-backup"));
    if (base.compare(own, Qt::CaseInsensitive) == 0 || !QFileInfo(base).isDir()) {
      continue;
    }
    for (const QString& dir : QDir(base).entryList(QDir::Dirs | QDir::NoDotAndDotDot)) {
      found.append(base + "/" + dir);
    }
  }
  return found;
}

}  // namespace RootCopy