#include "wabbajackinstaller.h"

#include "collectionfiles.h"

#include <archive.h>
#include <libbsarch.h>
#include <log.h>

#include <QDir>
#include <QDirIterator>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QRegularExpression>
#include <QSet>

#include <cstring>
#include <map>
#include <memory>
#include <vector>

using namespace MOBase;

namespace Wabbajack
{

namespace
{

constexpr char StateName[] = ".mo2r-install";

// how many failures are listed by name; the rest are counted
constexpr int ListedFailures = 200;

QString tr(const char* text, const char* disambiguation = nullptr, int n = -1)
{
  return QCoreApplication::translate("Wabbajack::Installer", text, disambiguation, n);
}

// files Wabbajack itself doesn't hold to the list's hash: lists compiled before
// modlist.txt's hash was fixed, and the screen size it writes into the prefs
bool isKnownModified(const QString& to)
{
  const QString name = QFileInfo(QString(to).replace(u'\\', u'/')).fileName().toLower();
  return name == u"modlist.txt" || name == u"skyrimprefs.ini";
}

// a path inside an archive, as the list writes it and as archives list it:
// backslashes, no leading one, lowercase
QString innerKey(QString path)
{
  path.replace(u'/', u'\\');
  while (path.startsWith(u'\\')) {
    path.remove(0, 1);
  }
  return path.toLower();
}

// the id of a blob the list carries: a plain name, never a path
bool isPlainId(const QString& id)
{
  return !id.isEmpty() && !id.contains(u'/') && !id.contains(u'\\') && !id.contains(u':') &&
         id != u"." && id != u"..";
}

// owns a libbsarch archive and entry list for the length of a scope
struct BsaHandle
{
  bsa_archive_t archive = bsa_create();
  ~BsaHandle() { bsa_free(archive); }
};
struct BsaEntries
{
  bsa_entry_list_t list = bsa_entry_list_create();
  ~BsaEntries() { bsa_entry_list_free(list); }
};

bool isBethesdaArchive(const QString& path)
{
  const QString lower = path.toLower();
  return lower.endsWith(u".bsa") || lower.endsWith(u".ba2");
}

// the 7-bit encoded length of a .NET string, as BinaryReader reads it
bool readDotNetString(QFile& f, QByteArray* out)
{
  quint32 length = 0;
  for (int shift = 0; shift < 35; shift += 7) {
    char c;
    if (!f.getChar(&c)) {
      return false;
    }
    length |= quint32(uchar(c) & 0x7F) << shift;
    if ((uchar(c) & 0x80) == 0) {
      // the name of a hash algorithm: anything long is a damaged patch
      if (length > 256) {
        return false;
      }
      *out = f.read(qint64(length));
      return out->size() == qint64(length);
    }
  }
  return false;
}

template <class T>
bool readValue(QFile& f, T* v)
{
  return f.read(reinterpret_cast<char*>(v), sizeof(T)) == qint64(sizeof(T));
}

// an OctoDiff delta (Octopus Deploy's format, which Wabbajack uses):
// "OCTODELTA", version 1, the hash's algorithm and the basis' hash, ">>>",
// then until the end copy(start, length) from the basis or data(length)
bool applyOctoDiff(const QString& basisPath, QFile& delta, const QString& outPath,
                   qint64 expected, QString* error)
{
  QFile basis(basisPath);
  if (!basis.open(QIODevice::ReadOnly)) {
    *error = tr("the file to patch can't be read");
    return false;
  }
  QByteArray magic = delta.read(9);
  char version     = 0;
  QByteArray algorithm;
  qint32 hashLength = 0;
  if (magic != "OCTODELTA" || !delta.getChar(&version) || version != 1 ||
      !readDotNetString(delta, &algorithm) || !readValue(delta, &hashLength) ||
      hashLength < 0 || hashLength > 1024) {
    *error = tr("the patch is damaged");
    return false;
  }
  delta.read(hashLength);
  if (delta.read(3) != ">>>") {
    *error = tr("the patch is damaged");
    return false;
  }

  QSaveFile out(outPath);
  if (!out.open(QIODevice::WriteOnly)) {
    *error = tr("the patched file can't be written");
    return false;
  }
  QByteArray buffer;
  // never more than the file the list names: a damaged patch can't fill the disk
  qint64 written = 0;
  while (!delta.atEnd()) {
    char command;
    if (!delta.getChar(&command)) {
      break;
    }
    if (uchar(command) == 0x60) {
      qint64 start = 0, length = 0;
      if (!readValue(delta, &start) || !readValue(delta, &length) || start < 0 ||
          length < 0 || start > basis.size() || length > basis.size() - start ||
          (expected > 0 && length > expected - written) || !basis.seek(start)) {
        *error = tr("the patch doesn't fit the file it patches");
        return false;
      }
      while (length > 0) {
        buffer = basis.read(qMin<qint64>(length, 4 * 1024 * 1024));
        if (buffer.isEmpty() || out.write(buffer) != buffer.size()) {
          *error = tr("the patched file can't be written");
          return false;
        }
        length -= buffer.size();
        written += buffer.size();
      }
    } else if (uchar(command) == 0x80) {
      qint64 length = 0;
      if (!readValue(delta, &length) || length < 0 ||
          (expected > 0 && length > expected - written)) {
        *error = tr("the patch is damaged");
        return false;
      }
      while (length > 0) {
        buffer = delta.read(qMin<qint64>(length, 4 * 1024 * 1024));
        if (buffer.isEmpty() || out.write(buffer) != buffer.size()) {
          *error = tr("the patch is damaged");
          return false;
        }
        length -= buffer.size();
        written += buffer.size();
      }
    } else {
      *error = tr("the patch is damaged");
      return false;
    }
  }
  if (!out.commit()) {
    *error = tr("the patched file can't be written");
    return false;
  }
  return true;
}

bool applyPatch(const QString& basisPath, const QString& patchPath, const QString& outPath,
                qint64 expected, QString* error)
{
  QFile delta(patchPath);
  if (!delta.open(QIODevice::ReadOnly)) {
    *error = tr("the list's patch isn't in it");
    return false;
  }
  const QByteArray head = delta.peek(9);
  if (head.startsWith("OCTODELTA")) {
    return applyOctoDiff(basisPath, delta, outPath, expected, error);
  }
  if (head.startsWith("BSDIFF40")) {
    // old lists: the collections' bspatch, whole files in memory
    QFile basis(basisPath);
    if (!basis.open(QIODevice::ReadOnly) || basis.size() > Collections::MaxPatchedSize) {
      *error = tr("the file to patch can't be read");
      return false;
    }
    QByteArray result;
    QString why;
    if (!Collections::bspatch(basis.readAll(), delta.readAll(), result, &why)) {
      *error = why;
      return false;
    }
    QSaveFile out(outPath);
    if (!out.open(QIODevice::WriteOnly) || out.write(result) != result.size() ||
        !out.commit()) {
      *error = tr("the patched file can't be written");
      return false;
    }
    return true;
  }
  *error = tr("the patch has a format MO2 Revamped doesn't know");
  return false;
}

QString bsaMessage(const bsa_result_message_t& m)
{
  return QString::fromWCharArray(m.text);
}

// the width, height and mipmaps of a DDS file, for a BA2 of textures
struct DdsInfo
{
  QHash<QString, QString> files;  // path inside the archive (lowercase) -> source
};

void __stdcall ddsInfo(bsa_archive_t, const wchar_t* path, bsa_dds_info_t* info, void* context)
{
  const auto* c = static_cast<const DdsInfo*>(context);
  info->width = info->height = info->mipmaps = 0;
  QFile f(c->files.value(innerKey(QString::fromWCharArray(path))));
  if (!f.open(QIODevice::ReadOnly)) {
    return;
  }
  const QByteArray h = f.read(32);
  if (h.size() < 32 || !h.startsWith("DDS ")) {
    return;
  }
  quint32 height, width, mips;
  std::memcpy(&height, h.constData() + 12, 4);
  std::memcpy(&width, h.constData() + 16, 4);
  std::memcpy(&mips, h.constData() + 28, 4);
  info->width   = width;
  info->height  = height;
  info->mipmaps = qMax<quint32>(1, mips);
}

class Installer
{
public:
  Installer(const InstallPlan& plan, const std::atomic_bool& cancel,
            const InstallProgress& progress)
      : m_plan(plan), m_cancel(cancel), m_progress(progress)
  {}

  InstallResult run();

private:
  struct Item
  {
    const Directive* d = nullptr;
    QStringList rest;  // the paths still to go inside archives
    QString out;       // where this level's file goes
  };

  const InstallPlan& m_plan;
  const std::atomic_bool& m_cancel;
  const InstallProgress& m_progress;
  InstallResult m_result;
  std::optional<ModList> m_list;

  QString m_state;   // <install>/.mo2r-install
  QString m_inline;  // the list's own files, taken out of the .wabbajack
  QString m_stage;   // temporary files
  int m_staged = 0;
  qint64 m_total = 1;
  qint64 m_done  = 0;
  QElapsedTimer m_shown;
  QSet<QString> m_plugins;

  bool cancelled() const { return m_cancel.load(); }
  void report(const QString& text, bool force = false);
  void fail(const Directive& d, const QString& why);
  QString stageFile();

  // where a directive's file goes; empty when it isn't written (Mod
  // Organizer's own, or a path that would lead out of the folder)
  QString targetOf(const Directive& d, bool* skipped);

  bool extractInline();
  void installArchives();
  void processArchive(const QString& path, std::vector<Item> items, int depth);
  void finishItem(const Item& item);
  void installInline();
  void buildBsas();
  bool verify(const Directive& d, const QString& path);
};

void Installer::report(const QString& text, bool force)
{
  if (!m_progress || (!force && m_shown.isValid() && m_shown.elapsed() < 250)) {
    return;
  }
  m_shown.restart();
  const qint64 permille = m_done * 1000 / qMax<qint64>(1, m_total);
  m_progress(text, int(qBound<qint64>(qint64(0), permille, qint64(1000))));
}

void Installer::fail(const Directive& d, const QString& why)
{
  ++m_result.failed;
  if (m_result.failures.size() < ListedFailures) {
    m_result.failures.append(QStringLiteral("%1: %2").arg(d.to, why));
  }
  log::warn("wabbajack install: {}: {}", d.to, why);
}

QString Installer::stageFile()
{
  return QDir(m_stage).filePath(QString::number(++m_staged));
}

QString Installer::targetOf(const Directive& d, bool* skipped)
{
  *skipped = false;
  if (d.modOrganizerFile) {
    *skipped = true;
    return {};
  }
  const QString to    = QString(d.to).replace(u'/', u'\\');
  const int slash     = to.indexOf(u'\\');
  const QString first = (slash < 0 ? to : to.left(slash)).toLower();

  if (slash < 0) {
    // the root of a Wabbajack install is Mod Organizer's own folder: its files
    // there are MO2's (MO2 Revamped is the one that runs)
    const QString suffix = QFileInfo(first).suffix();
    if (first == u"modorganizer.ini" || first == u"portable.txt" ||
        first == u"nxmhandler.ini" || suffix == u"exe" || suffix == u"dll") {
      *skipped = true;
      return {};
    }
  }
  if (first == u"plugins" && slash > 0) {
    // an MO2 plugin the list adds: set aside, offered at the end
    const QString rest = to.mid(slash + 1);
    m_plugins.insert(rest.section(u'\\', 0, 0));
    return installPath(QDir(m_state).filePath("plugins"), rest);
  }
  return installPath(m_plan.installFolder, to);
}

bool Installer::verify(const Directive& d, const QString& path)
{
  if (d.hash == 0) {
    if (!QFileInfo(path).isFile()) {
      fail(d, tr("it wasn't written"));
      return false;
    }
    return true;
  }
  const auto hash = hashFile(path, &m_cancel);
  if (!hash) {
    if (!cancelled()) {
      fail(d, tr("it wasn't written"));
    }
    return false;
  }
  if (*hash != d.hash) {
    fail(d, tr("its hash isn't the list's"));
    return false;
  }
  return true;
}

bool Installer::extractInline()
{
  QSet<QString> wanted;
  for (const Directive& d : m_list->directives) {
    switch (d.type) {
    case DirectiveType::InlineFile:
    case DirectiveType::RemappedInlineFile:
    case DirectiveType::PatchedFromArchive:
      if (!d.dataId.isEmpty()) {
        wanted.insert(d.dataId.toLower());
      }
      break;
    default:
      break;
    }
  }
  if (wanted.isEmpty()) {
    return true;
  }

  report(tr("Taking the list's own files out of it…"), true);
  std::unique_ptr<::Archive> archive = CreateArchive();
  if (!archive->isValid() || !archive->open(m_plan.listPath.toStdWString(), {})) {
    m_result.error = tr("The list's file can't be opened.");
    return false;
  }
  for (FileData* f : archive->getFileList()) {
    const QString name = QString::fromStdWString(f->getArchiveFilePath());
    // the ids are plain names; nothing with a folder is one of them
    if (!f->isDirectory() && !name.contains(u'\\') && !name.contains(u'/') &&
        wanted.contains(name.toLower())) {
      f->addOutputFilePath(
          QDir::toNativeSeparators(QDir(m_inline).filePath(name.toLower())).toStdWString());
    }
  }
  const bool ok = archive->extract(QDir::toNativeSeparators(m_inline).toStdWString(), {}, {}, {});
  archive->close();
  if (!ok) {
    m_result.error = tr("The list's own files can't be taken out of it.");
    return false;
  }
  return true;
}

void Installer::installArchives()
{
  // the directives that take a file out of an archive, by archive
  std::map<uint64_t, std::vector<Item>> byArchive;
  std::map<QString, int> unsupported;
  for (const Directive& d : m_list->directives) {
    if (d.type == DirectiveType::TransformedTexture || d.type == DirectiveType::MergedPatch) {
      ++unsupported[d.type == DirectiveType::TransformedTexture ? QStringLiteral("TransformedTexture")
                                                                 : QStringLiteral("MergedPatch")];
      m_done += d.size;
      continue;
    }
    if (d.type != DirectiveType::FromArchive && d.type != DirectiveType::PatchedFromArchive) {
      continue;
    }
    bool skipped       = false;
    const QString path = targetOf(d, &skipped);
    if (skipped) {
      ++m_result.modOrganizer;
      m_done += d.size;
      continue;
    }
    if (path.isEmpty()) {
      fail(d, tr("its path leads out of the install folder"));
      continue;
    }
    byArchive[d.archiveHash].push_back(Item{&d, d.archivePath, path});
  }
  for (const auto& [type, count] : unsupported) {
    m_result.unsupported.append(QStringLiteral("%1 (%2)").arg(type).arg(count));
  }

  for (auto& [hash, items] : byArchive) {
    if (cancelled()) {
      return;
    }
    const QString source = m_plan.archives.value(hash);
    const int index      = m_list->archiveByHash.value(hash, -1);
    const QString name =
        index >= 0 ? m_list->archives[std::size_t(index)].name : hashToBase64(hash);
    if (source.isEmpty() || !QFileInfo(source).isFile()) {
      for (const Item& item : items) {
        fail(*item.d, tr("its archive %1 isn't there").arg(name));
        m_done += item.d->size;
      }
      continue;
    }
    report(tr("Installing from %1…").arg(name), true);
    // the game's own BSAs are taken by their name; downloads too
    processArchive(source, std::move(items), 0);
  }
}

void Installer::processArchive(const QString& path, std::vector<Item> items, int depth)
{
  if (depth > 4) {
    for (const Item& item : items) {
      fail(*item.d, tr("its archive is inside too many others"));
    }
    return;
  }

  // the archive's file itself (a game file taken whole)
  struct Need
  {
    std::vector<Item*> leaves;   // this level's file is theirs (or patched for them)
    std::vector<Item*> deeper;   // inside an archive at this path
    QString name;                // as the list writes it
    std::vector<QString> outputs;
    QString innerArchive;        // where an archive inside goes
  };
  std::map<QString, Need> needs;
  std::vector<Item*> whole;
  for (Item& item : items) {
    if (item.rest.isEmpty()) {
      whole.push_back(&item);
      continue;
    }
    Need& n = needs[innerKey(item.rest.front())];
    n.name  = item.rest.front();
    (item.rest.size() == 1 ? n.leaves : n.deeper).push_back(&item);
  }

  for (Item* item : whole) {
    if (cancelled()) {
      return;
    }
    if (item->d->type == DirectiveType::PatchedFromArchive) {
      finishItem(Item{item->d, {}, path});
      continue;
    }
    QDir().mkpath(QFileInfo(item->out).absolutePath());
    QFile::remove(item->out);
    if (!QFile::copy(path, item->out)) {
      fail(*item->d, tr("it can't be copied"));
    } else if (verify(*item->d, item->out)) {
      ++m_result.written;
    }
    m_done += item->d->size;
    report(QFileInfo(item->out).fileName());
  }
  if (needs.empty()) {
    return;
  }

  // where each file goes: the target itself, or a temporary file to patch or
  // to open as an archive
  for (auto& [key, n] : needs) {
    for (Item* leaf : n.leaves) {
      if (leaf->d->type == DirectiveType::PatchedFromArchive) {
        leaf->out = stageFile();
        n.outputs.push_back(leaf->out);
      } else {
        QDir().mkpath(QFileInfo(leaf->out).absolutePath());
        n.outputs.push_back(leaf->out);
      }
    }
    if (!n.deeper.empty()) {
      n.innerArchive = stageFile() + QStringLiteral(".") + QFileInfo(n.name).suffix();
      n.outputs.push_back(n.innerArchive);
    }
  }

  QSet<QString> found;
  QHash<QString, QString> reasons;  // why an entry couldn't be taken out
  if (isBethesdaArchive(path)) {
    BsaHandle handle;
    bsa_archive_t archive = handle.archive;
    const bsa_result_message_t loaded =
        bsa_load_from_file(archive, QDir::toNativeSeparators(path).toStdWString().c_str());
    if (loaded.code != BSA_RESULT_NONE) {
      for (const Item& item : items) {
        if (!item.rest.isEmpty()) {
          fail(*item.d, tr("its archive can't be read: %1").arg(bsaMessage(loaded)));
        }
      }
      return;
    }
    for (auto& [key, n] : needs) {
      if (cancelled()) {
        break;
      }
      const std::wstring inner = QString(n.name).replace(u'/', u'\\').toStdWString();
      const std::wstring first = QDir::toNativeSeparators(n.outputs.front()).toStdWString();
      QDir().mkpath(QFileInfo(n.outputs.front()).absolutePath());
      const bsa_result_message_t r = bsa_extract_file(archive, inner.c_str(), first.c_str());
      if (r.code != BSA_RESULT_NONE) {
        reasons.insert(key, bsaMessage(r));
        continue;
      }
      for (std::size_t i = 1; i < n.outputs.size(); ++i) {
        QDir().mkpath(QFileInfo(n.outputs[i]).absolutePath());
        QFile::remove(n.outputs[i]);
        QFile::copy(n.outputs.front(), n.outputs[i]);
      }
      found.insert(key);
    }
    bsa_close(archive);
  } else {
    std::unique_ptr<::Archive> archive = CreateArchive();
    if (!archive->isValid() || !archive->open(path.toStdWString(), {})) {
      for (const Item& item : items) {
        if (!item.rest.isEmpty()) {
          fail(*item.d, tr("its archive can't be opened"));
        }
      }
      return;
    }
    for (FileData* f : archive->getFileList()) {
      if (f->isDirectory()) {
        continue;
      }
      const QString key = innerKey(QString::fromStdWString(f->getArchiveFilePath()));
      const auto it     = needs.find(key);
      if (it == needs.end() || found.contains(key)) {
        continue;
      }
      for (const QString& out : it->second.outputs) {
        f->addOutputFilePath(QDir::toNativeSeparators(out).toStdWString());
      }
      found.insert(key);
    }
    ::Archive* raw = archive.get();
    auto progress  = [this, raw](::Archive::ProgressType, uint64_t, uint64_t) {
      // a large archive takes minutes: stopping doesn't wait for its end
      if (cancelled()) {
        raw->cancel();
      }
    };
    if (!archive->extract(QDir::toNativeSeparators(m_plan.installFolder).toStdWString(),
                          progress, {}, {})) {
      log::warn("wabbajack install: extracting {} reported an error", path);
    }
    archive->close();
  }

  for (auto& [key, n] : needs) {
    if (cancelled()) {
      return;
    }
    if (!found.contains(key)) {
      const QString why = reasons.contains(key)
                              ? tr("%1 can't be taken out of its archive: %2")
                                    .arg(n.name, reasons.value(key))
                              : tr("%1 isn't in its archive").arg(n.name);
      for (Item* item : n.leaves) {
        fail(*item->d, why);
        m_done += item->d->size;
      }
      for (Item* item : n.deeper) {
        fail(*item->d, why);
        m_done += item->d->size;
      }
      continue;
    }
    for (Item* leaf : n.leaves) {
      finishItem(*leaf);
    }
    if (!n.deeper.empty()) {
      std::vector<Item> inner;
      for (Item* item : n.deeper) {
        Item next = *item;
        next.rest.removeFirst();
        inner.push_back(std::move(next));
      }
      // its own outputs: the targets the directives named
      for (Item& item : inner) {
        bool skipped = false;
        item.out     = targetOf(*item.d, &skipped);
      }
      processArchive(n.innerArchive, std::move(inner), depth + 1);
      QFile::remove(n.innerArchive);
    }
  }
}

void Installer::finishItem(const Item& item)
{
  const Directive& d = *item.d;
  if (d.type == DirectiveType::PatchedFromArchive) {
    bool skipped         = false;
    const QString target = targetOf(d, &skipped);
    QString why;
    QDir().mkpath(QFileInfo(target).absolutePath());
    const bool patched =
        isPlainId(d.dataId) &&
        applyPatch(item.out, QDir(m_inline).filePath(d.dataId.toLower()), target, d.size, &why);
    if (!isPlainId(d.dataId)) {
      why = tr("the list's patch isn't in it");
    }
    // the temporary copy only: a whole game file patched stays as it is
    if (item.out.startsWith(m_stage)) {
      QFile::remove(item.out);
    }
    if (!patched) {
      fail(d, why);
    } else if (verify(d, target)) {
      ++m_result.written;
    }
  } else if (verify(d, item.out)) {
    ++m_result.written;
  }
  m_done += d.size;
  report(QFileInfo(d.to).fileName());
}

void Installer::installInline()
{
  report(tr("Writing the list's own files…"), true);
  const QString install = QDir::toNativeSeparators(m_plan.installFolder);
  const QString game    = QDir::toNativeSeparators(m_plan.gameFolder);
  const QString downloads = QDir::toNativeSeparators(m_plan.downloadsFolder);

  for (const Directive& d : m_list->directives) {
    if (cancelled()) {
      return;
    }
    if (d.type != DirectiveType::InlineFile && d.type != DirectiveType::RemappedInlineFile) {
      continue;
    }
    QFile source(isPlainId(d.dataId) ? QDir(m_inline).filePath(d.dataId.toLower())
                                     : QString());
    if (!isPlainId(d.dataId) || !source.open(QIODevice::ReadOnly)) {
      fail(d, tr("the list doesn't carry it"));
      continue;
    }
    QByteArray data = source.readAll();
    if (d.type == DirectiveType::RemappedInlineFile) {
      // the list's places for the game, the install and the downloads
      QString text = QString::fromUtf8(data);
      auto put = [&](const char* name, const QString& path) {
        const QString base = QStringLiteral("{--||%1_PATH_MAGIC_").arg(QLatin1String(name));
        text.replace(base + QStringLiteral("BACK||--}"), path);
        text.replace(base + QStringLiteral("DOUBLE_BACK||--}"),
                     QString(path).replace(u'\\', QStringLiteral("\\\\")));
        text.replace(base + QStringLiteral("FORWARD||--}"),
                     QString(path).replace(u'\\', u'/'));
      };
      put("GAME", game);
      put("MO2", install);
      put("DOWNLOAD", downloads);
      data = text.toUtf8();
    }

    const QString to = QString(d.to).replace(u'/', u'\\');
    if (to.compare(u"ModOrganizer.ini", Qt::CaseInsensitive) == 0) {
      // the instance's settings take what they need from it
      m_result.modOrganizerIni = data;
      m_done += d.size;
      continue;
    }
    bool skipped         = false;
    const QString target = targetOf(d, &skipped);
    if (skipped) {
      ++m_result.modOrganizer;
      m_done += d.size;
      continue;
    }
    if (target.isEmpty()) {
      fail(d, tr("its path leads out of the install folder"));
      continue;
    }
    QDir().mkpath(QFileInfo(target).absolutePath());
    QSaveFile out(target);
    if (!out.open(QIODevice::WriteOnly) || out.write(data) != data.size() || !out.commit()) {
      fail(d, tr("it can't be written"));
      continue;
    }
    // remapped files hold this install's paths: no hash of the list's fits
    if (d.type == DirectiveType::RemappedInlineFile) {
      ++m_result.written;
    } else if (d.hash == 0 || isKnownModified(d.to) ||
               XxHash64::of(data.constData(), std::size_t(data.size())) == d.hash) {
      ++m_result.written;
    } else {
      fail(d, tr("its hash isn't the list's"));
    }
    m_done += d.size;
    report(QFileInfo(target).fileName());
  }
}

void Installer::buildBsas()
{
  int built = 0;
  for (const Directive& d : m_list->directives) {
    if (cancelled()) {
      return;
    }
    if (d.type != DirectiveType::CreateBSA) {
      continue;
    }
    const QJsonObject extra = QJsonDocument::fromJson(d.extra).object();
    const QJsonObject state = extra.value("State").toObject();
    const QString type      = state.value("$type").toString().section(u',', 0, 0).trimmed();
    const QString root =
        installPath(m_plan.installFolder, QStringLiteral("TEMP_BSA_FILES\\") + d.dataId);
    bool skipped         = false;
    const QString target = targetOf(d, &skipped);
    if (skipped) {
      continue;
    }
    if (target.isEmpty() || root.isEmpty()) {
      fail(d, tr("its path leads out of the install folder"));
      continue;
    }
    report(tr("Building %1…").arg(QFileInfo(target).fileName()), true);

    bsa_archive_type_t archiveType = baNone;
    bool textures                  = false;
    if (type.startsWith(u"BSAState")) {
      const int version = state.value("Version").toInt();
      archiveType = version == 105 ? baSSE : version == 103 ? baTES4 : baFO3;
    } else if (type.startsWith(u"BA2State")) {
      // Type: 0/GNRL general, 1/DX10 textures (written as a number or a name)
      const QJsonValue kind = state.value("Type");
      textures = kind.toInt(-1) == 1 || kind.toString().compare(u"DX10", Qt::CaseInsensitive) == 0;
      // versions 2 and 3 are Starfield's
      const int version   = state.value("Version").toInt();
      const bool starfield = version == 2 || version == 3;
      archiveType = starfield ? (textures ? baSFdds : baSF) : (textures ? baFO4dds : baFO4);
    } else if (type.startsWith(u"TES3State")) {
      archiveType = baTES3;
    }
    if (archiveType == baNone) {
      fail(d, tr("the archive's kind (%1) isn't known").arg(type));
      continue;
    }

    DdsInfo dds;
    BsaEntries entryList;
    bsa_entry_list_t entries = entryList.list;
    std::vector<std::pair<std::wstring, std::wstring>> files;
    bool missing = false;
    for (const QJsonValue& v : extra.value("FileStates").toArray()) {
      const QString inner = v.toObject().value("Path").toString().replace(u'/', u'\\');
      const QString source = installPath(root, inner);
      if (source.isEmpty() || !QFileInfo(source).isFile()) {
        missing = true;
        break;
      }
      dds.files.insert(innerKey(inner), source);
      bsa_entry_list_add(entries, inner.toStdWString().c_str());
      files.emplace_back(QDir::toNativeSeparators(root).toStdWString(),
                         QDir::toNativeSeparators(source).toStdWString());
    }
    if (missing) {
      fail(d, tr("a file that goes into it wasn't installed"));
      continue;
    }

    QDir().mkpath(QFileInfo(target).absolutePath());
    QFile::remove(target);
    BsaHandle handle;
    bsa_archive_t archive = handle.archive;
    if (textures) {
      bsa_file_dds_info_callback_set(archive, &ddsInfo, &dds);
    }
    bsa_result_message_t r = bsa_create_archive(
        archive, QDir::toNativeSeparators(target).toStdWString().c_str(), archiveType, entries);
    if (r.code == BSA_RESULT_NONE && type.startsWith(u"BSAState")) {
      const quint32 archiveFlags = quint32(state.value("ArchiveFlags").toDouble());
      bsa_archive_flags_set(archive, archiveFlags);
      bsa_file_flags_set(archive, quint32(state.value("FileFlags").toDouble()));
      bsa_compress_set(archive, (archiveFlags & 0x4) != 0);
    } else if (r.code == BSA_RESULT_NONE) {
      bsa_compress_set(archive, true);
    }
    for (const auto& [rootDir, source] : files) {
      if (r.code != BSA_RESULT_NONE) {
        break;
      }
      r = bsa_add_file_from_disk_root(archive, rootDir.c_str(), source.c_str());
    }
    if (r.code == BSA_RESULT_NONE) {
      r = bsa_save(archive);
    }
    if (r.code != BSA_RESULT_NONE) {
      QFile::remove(target);
      fail(d, tr("it can't be built: %1").arg(bsaMessage(r)));
      continue;
    }
    // Wabbajack doesn't check a built archive's hash either: its files were
    ++m_result.written;
    ++built;
    m_done += QFileInfo(target).size();
  }
  if (built > 0) {
    m_result.notes.append(tr("%n BSA archives built.", nullptr, built));
  }
}

InstallResult Installer::run()
{
  m_state  = installStateFolder(m_plan.installFolder);
  m_inline = QDir(m_state).filePath("inline");
  m_stage  = QDir(m_state).filePath("stage");
  if (!QDir().mkpath(m_inline) || !QDir().mkpath(m_stage)) {
    m_result.error = tr("The install folder can't be written.");
    return m_result;
  }

  report(tr("Reading the list…"), true);
  QString error;
  try {
    m_list = readModList(m_plan.listPath, &error, true, &m_cancel);
  } catch (const std::exception& e) {
    error = QString::fromLocal8Bit(e.what());
  }
  if (!m_list) {
    m_result.cancelled = cancelled();
    m_result.error     = tr("The list can't be read: %1").arg(error);
    return m_result;
  }
  m_result.profiles = m_list->profiles;
  m_total           = 0;
  for (const Directive& d : m_list->directives) {
    m_total += d.size;
  }

  try {
    if (extractInline()) {
      installArchives();
      if (!cancelled()) {
        installInline();
      }
      if (!cancelled()) {
        buildBsas();
      }
    }
  } catch (const std::bad_alloc&) {
    m_result.error = tr("Not enough memory.");
  } catch (const std::exception& e) {
    m_result.error = QString::fromLocal8Bit(e.what());
  }

  // the temporary files, and the files gathered for the BSAs
  QDir(m_stage).removeRecursively();
  QDir(m_inline).removeRecursively();
  if (!cancelled()) {
    const QString gathered = installPath(m_plan.installFolder, QStringLiteral("TEMP_BSA_FILES"));
    if (!gathered.isEmpty()) {
      QDir(gathered).removeRecursively();
    }
  }

  m_result.plugins = m_plugins.values();
  m_result.plugins.sort(Qt::CaseInsensitive);
  m_result.cancelled = cancelled();
  m_result.ok        = !m_result.cancelled && m_result.error.isEmpty();
  if (m_result.ok) {
    // a finished install: its folder isn't one to go on in
    QSaveFile finished(QDir(m_state).filePath("finished"));
    if (finished.open(QIODevice::WriteOnly)) {
      finished.write(m_list->name.toUtf8());
      finished.commit();
    }
    m_done = m_total;
  }
  report(tr("Done."), true);
  return m_result;
}

}  // namespace

QString installStateFolder(const QString& installFolder)
{
  return QDir(installFolder).filePath(QString::fromLatin1(StateName));
}

bool isFinishedInstall(const QString& installFolder)
{
  return QFileInfo::exists(QDir(installStateFolder(installFolder)).filePath("finished"));
}

QString installPath(const QString& installFolder, const QString& relative)
{
  QString r = QString(relative).replace(u'\\', u'/');
  if (r.isEmpty() || QDir::isAbsolutePath(r) || r.contains(u':') || r.startsWith(u'/')) {
    return {};
  }
  static const QRegularExpression device(
      QStringLiteral("^(con|prn|aux|nul|com[0-9]|lpt[0-9])(\\..*)?$"),
      QRegularExpression::CaseInsensitiveOption);
  const QStringList parts = r.split(u'/');
  if (parts.front().compare(QString::fromLatin1(StateName), Qt::CaseInsensitive) == 0) {
    return {};
  }
  for (const QString& part : parts) {
    if (part == u".." || device.match(part.trimmed()).hasMatch()) {
      return {};
    }
  }
  const QString base = QDir::cleanPath(installFolder);
  const QString full = QDir::cleanPath(base + u'/' + r);
  if (!full.startsWith(base + u'/', Qt::CaseInsensitive)) {
    return {};
  }
  return full;
}

InstallResult install(const InstallPlan& plan, const std::atomic_bool& cancel,
                      const InstallProgress& progress)
{
  Installer installer(plan, cancel, progress);
  try {
    return installer.run();
  } catch (const std::exception& e) {
    InstallResult r;
    r.error = QString::fromLocal8Bit(e.what());
    return r;
  } catch (...) {
    InstallResult r;
    r.error = QStringLiteral("unknown error");
    return r;
  }
}

}  // namespace Wabbajack
