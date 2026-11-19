#include "wabbajackinstaller.h"

#include "collectionfiles.h"

#include <DirectXTex.h>
#include <archive.h>
#include <libbsarch.h>
#include <log.h>
#include <utility.h>

#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QRegularExpression>
#include <QSet>
#include <QStorageInfo>

#include <omp.h>

#include <algorithm>
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
// the failures the summary names one by one; a full drive or a broken list
// fails every file, and hundreds of thousands of lines help nobody
constexpr int KeptFailures = 5000;
// under this much free space the install folder's drive counts as full
constexpr qint64 FullDrive = 64ll * 1024 * 1024;
// a list's own file read whole (an InlineFile or RemappedInlineFile)
constexpr qint64 MaxInlineFileSize = qint64(2) << 30;

QString tr(const char* text, const char* disambiguation = nullptr, int n = -1)
{
  return QCoreApplication::translate("Wabbajack::Installer", text, disambiguation, n);
}

// files Wabbajack itself doesn't hold to the list's hash: lists compiled before
// modlist.txt's hash was fixed, and the screen size it writes into the prefs.
// Only a profile's own: the same names elsewhere are checked
bool isKnownModified(const QString& to)
{
  const QString path = QString(to).replace(u'\\', u'/').toLower();
  if (!path.startsWith(u"profiles/")) {
    return false;
  }
  const QString name = QFileInfo(path).fileName();
  return name == u"modlist.txt" || name.endsWith(u"prefs.ini");
}

// the name a directive type is reported by when it isn't installed
QString typeName(DirectiveType type)
{
  switch (type) {
  case DirectiveType::ArchiveMeta:
    return QStringLiteral("ArchiveMeta");
  case DirectiveType::PropertyFile:
    return QStringLiteral("PropertyFile");
  case DirectiveType::MergedPatch:
    return QStringLiteral("MergedPatch");
  default:
    return QStringLiteral("Unknown");
  }
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
  if (expected <= 0) {
    expected = Collections::MaxPatchedSize;
  }
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
          length > expected - written || !basis.seek(start)) {
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
      if (!readValue(delta, &length) || length < 0 || length > expected - written) {
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
    if (delta.size() > Collections::MaxPatchedSize) {
      *error = tr("the patch is damaged");
      return false;
    }
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

// a texture made as the list's author made it: resized, its mipmaps made again
// and compressed to the format the list names
bool transformTexture(const QString& source, const QString& target, const QJsonObject& state,
                      QString* error)
{
  using namespace DirectX;
  static const QHash<QString, int> formats = {
      {"BC1_UNORM", 71},       {"BC1_UNORM_SRGB", 72}, {"BC2_UNORM", 74},
      {"BC2_UNORM_SRGB", 75},  {"BC3_UNORM", 77},      {"BC3_UNORM_SRGB", 78},
      {"BC4_UNORM", 80},       {"BC4_SNORM", 81},      {"BC5_UNORM", 83},
      {"BC5_SNORM", 84},       {"BC6H_UF16", 95},      {"BC6H_SF16", 96},
      {"BC7_UNORM", 98},       {"BC7_UNORM_SRGB", 99}, {"R8G8B8A8_UNORM", 28},
      {"R8G8B8A8_UNORM_SRGB", 29}, {"B8G8R8A8_UNORM", 87}, {"B8G8R8X8_UNORM", 88},
      {"B8G8R8A8_UNORM_SRGB", 91}, {"B5G6R5_UNORM", 85}, {"B5G5R5A1_UNORM", 86},
      {"R8_UNORM", 61},          {"A8_UNORM", 65},       {"R8G8_UNORM", 49},
      {"BC1_TYPELESS", 70},      {"BC2_TYPELESS", 73},   {"BC3_TYPELESS", 76},
      {"BC4_TYPELESS", 79},      {"BC5_TYPELESS", 82},   {"BC7_TYPELESS", 97},
      {"R8G8B8A8_TYPELESS", 27}, {"B8G8R8A8_TYPELESS", 90}};
  const QJsonValue f = state.value("Format");
  QString name       = f.toString().toUpper();
  if (name.startsWith(u"DXGI_FORMAT_")) {
    name = name.mid(12);
  }
  const int formatCode = f.isString() ? formats.value(name, 0) : f.toInt(0);
  const auto format    = DXGI_FORMAT(formatCode);
  const int width      = state.value("Width").toInt();
  const int height     = state.value("Height").toInt();
  const int mips       = state.value("MipLevels").toInt(0);  // 0: the whole chain
  if (format == DXGI_FORMAT_UNKNOWN) {
    *error = tr("the texture's format isn't known");
    return false;
  }
  if (width <= 0 || height <= 0 || width > 16384 || height > 16384) {
    *error = tr("the texture's size isn't possible");
    return false;
  }

  TexMetadata meta;
  ScratchImage image;
  if (FAILED(LoadFromDDSFile(source.toStdWString().c_str(), DDS_FLAGS_NONE, &meta, image))) {
    *error = tr("the texture can't be read");
    return false;
  }
  if (meta.arraySize != 1 || meta.depth != 1 || meta.IsCubemap()) {
    *error = tr("the texture's shape isn't supported");
    return false;
  }

  // the largest image, uncompressed: sRGB stays sRGB, HDR (BC6H) stays in floats
  ScratchImage plain;
  const Image* top = image.GetImage(0, 0, 0);
  if (IsCompressed(meta.format)) {
    const bool hdr = meta.format == DXGI_FORMAT_BC6H_UF16 ||
                     meta.format == DXGI_FORMAT_BC6H_SF16 ||
                     format == DXGI_FORMAT_BC6H_UF16 || format == DXGI_FORMAT_BC6H_SF16;
    const DXGI_FORMAT to = hdr ? DXGI_FORMAT_R16G16B16A16_FLOAT
                         : IsSRGB(meta.format) ? DXGI_FORMAT_R8G8B8A8_UNORM_SRGB
                                               : DXGI_FORMAT_R8G8B8A8_UNORM;
    if (FAILED(Decompress(*top, to, plain))) {
      *error = tr("the texture can't be read");
      return false;
    }
    image.Release();  // the source isn't needed any more
    top = plain.GetImage(0, 0, 0);
  }

  // DirectXTex's own filters: WIC's would need COM on this thread
  const TEX_FILTER_FLAGS filter = TEX_FILTER_CUBIC | TEX_FILTER_FORCE_NON_WIC;
  ScratchImage resized;
  if (FAILED(Resize(*top, std::size_t(width), std::size_t(height), filter, resized))) {
    *error = tr("the texture can't be resized");
    return false;
  }
  plain.Release();
  image.Release();

  // as many levels as the list says, never more than the size allows
  std::size_t possible = 1;
  for (int side = std::max(width, height); side > 1; side /= 2) {
    ++possible;
  }
  const std::size_t levels = mips <= 0 ? possible : std::min<std::size_t>(std::size_t(mips), possible);
  ScratchImage mipped;
  if (levels <= 1) {
    mipped = std::move(resized);
  } else if (FAILED(GenerateMipMaps(*resized.GetImage(0, 0, 0), filter, levels, mipped))) {
    *error = tr("the texture's mipmaps can't be made");
    return false;
  }

  ScratchImage done;
  HRESULT hr;
  if (IsCompressed(format)) {
    // BC7's full search takes minutes a texture: its quick mode, as lists' tools use
    const TEX_COMPRESS_FLAGS quick = TEX_COMPRESS_BC7_QUICK;
    // its threads run at normal priority whatever the install's is: half the
    // processor at most, the rest for what the user does meanwhile (a game)
    struct Threads
    {
      int before = omp_get_max_threads();
      Threads() { omp_set_num_threads(std::max(1, before / 2)); }
      ~Threads() { omp_set_num_threads(before); }
    } threads;
    hr = Compress(mipped.GetImages(), mipped.GetImageCount(), mipped.GetMetadata(), format,
                  quick | TEX_COMPRESS_PARALLEL, TEX_THRESHOLD_DEFAULT, done);
    if (FAILED(hr)) {
      hr = Compress(mipped.GetImages(), mipped.GetImageCount(), mipped.GetMetadata(), format,
                    quick, TEX_THRESHOLD_DEFAULT, done);
    }
  } else if (mipped.GetMetadata().format != format) {
    hr = Convert(mipped.GetImages(), mipped.GetImageCount(), mipped.GetMetadata(), format,
                 TEX_FILTER_DEFAULT, TEX_THRESHOLD_DEFAULT, done);
  } else {
    done = std::move(mipped);
    hr   = S_OK;
  }
  if (FAILED(hr)) {
    *error = tr("the texture can't be compressed");
    return false;
  }
  QFile::remove(target);
  if (FAILED(SaveToDDSFile(done.GetImages(), done.GetImageCount(), done.GetMetadata(),
                           DDS_FLAGS_NONE, target.toStdWString().c_str()))) {
    *error = tr("the texture can't be written");
    return false;
  }
  return true;
}

// a key of a section of an ini set, when the file has it; the bytes it doesn't
// touch stay as they are (read as Latin-1)
bool setIniValue(QString& text, const QString& section, const QString& key, const QString& value)
{
  const QStringList lines = text.split(u'\n');
  QStringList out;
  bool inSection = false, changed = false;
  bool first = true;
  for (QString line : lines) {
    QString trimmed = line.trimmed();
    if (first) {
      // read as Latin-1, a UTF-8 BOM is these three characters
      first = false;
      if (trimmed.startsWith(QStringLiteral("\u00EF\u00BB\u00BF"))) {
        trimmed = trimmed.mid(3);
      }
    }
    if (trimmed.startsWith(u'[')) {
      // "[Display] ; a note" is the section too
      const int end = trimmed.indexOf(u']');
      inSection     = end > 0 && trimmed.mid(1, end - 1).trimmed().compare(section, Qt::CaseInsensitive) == 0;
    } else if (inSection) {
      const int eq = line.indexOf(u'=');
      if (eq > 0 && line.left(eq).trimmed().compare(key, Qt::CaseInsensitive) == 0) {
        const bool cr          = line.endsWith(u'\r');
        const QString replaced = line.left(eq + 1) + value + (cr ? QStringLiteral("\r") : QString());
        if (replaced != line) {
          line    = replaced;
          changed = true;
        }
      }
    }
    out.append(line);
  }
  if (changed) {
    text = out.join(u'\n');
  }
  return changed;
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
    // its hash already checked while it was copied: no second read
    bool verified = false;
    // already failed (and counted) while it was copied
    bool done = false;
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
  int m_textures = 0;
  // folders made already: a list writes hundreds of thousands of files into
  // far fewer folders
  QSet<QString> m_folders;

  // the install folder's drive filled up: everything stops as for a cancel,
  // with m_result.error saying why
  bool m_driveFull = false;

  bool cancelled() const { return m_cancel.load() || m_driveFull; }
  // after a failure: a full drive stops the install instead of failing every
  // file that is left, for hours
  void checkDrive();
  void report(const QString& text, bool force = false);
  void fail(const Directive& d, const QString& why);
  QString stageFile();
  // the folder a file goes into, made once
  void makeFolderFor(const QString& file);
  // a file copied whole and checked on the way; the bad copy removed
  bool copyChecked(const Directive& d, const QString& source, const QString& target);

  // where a directive's file goes; empty when it isn't written (Mod
  // Organizer's own, or a path that would lead out of the folder)
  QString targetOf(const Directive& d, bool* skipped);

  bool extractInline();
  void installArchives();
  void processArchive(const QString& path, std::vector<Item> items, int depth);
  void finishItem(const Item& item);
  void installInline();
  void buildBsas();
  void setScreenSize();
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
  // the summary keeps the first thousands; a broken list fails every file,
  // so the log keeps the first and run() the total
  if (m_result.failures.size() < KeptFailures) {
    m_result.failures.append(QStringLiteral("%1: %2").arg(d.to, why));
  }
  if (m_result.failed <= ListedFailures) {
    log::warn("wabbajack install: {}: {}", d.to, why);
  }
  if (m_result.failed <= 20 || m_result.failed % 50 == 0) {
    checkDrive();
  }
}

void Installer::checkDrive()
{
  if (m_driveFull) {
    return;
  }
  const QStorageInfo storage(m_plan.installFolder);
  if (!storage.isValid() || storage.bytesAvailable() >= FullDrive) {
    return;
  }
  m_driveFull    = true;
  m_result.error = tr("The install folder's drive is full (%1 free), so the install stopped. "
                      "Free some space and install again into the same folder: the files "
                      "already installed and checked stay.")
                       .arg(localizedByteSize(storage.bytesAvailable()));
  log::error("wabbajack install: the drive of {} is full, the install stops",
             m_plan.installFolder);
}

QString Installer::stageFile()
{
  return QDir(m_stage).filePath(QString::number(++m_staged));
}

void Installer::makeFolderFor(const QString& file)
{
  const QString folder = QFileInfo(file).absolutePath();
  if (!m_folders.contains(folder) && QDir().mkpath(folder)) {
    m_folders.insert(folder);
  }
}

bool Installer::copyChecked(const Directive& d, const QString& source, const QString& target)
{
  const auto hash = copyFile(source, target, &m_cancel);
  if (!hash) {
    QFile::remove(target);
    if (!cancelled()) {
      fail(d, tr("it can't be copied"));
    }
    return false;
  }
  if (d.hash != 0 && *hash != d.hash) {
    QFile::remove(target);
    fail(d, tr("its hash isn't the list's"));
    return false;
  }
  if (d.hash == 0 && !QFileInfo(target).isFile()) {
    fail(d, tr("it wasn't written"));
    return false;
  }
  return true;
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
    // a file cut short (a full drive) or wrong doesn't stay as if installed
    QFile::remove(path);
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
    // the types installed here, by installInline and by buildBsas; any other
    // is reported, its size counted so the progress still reaches the end
    if (d.type == DirectiveType::ArchiveMeta || d.type == DirectiveType::PropertyFile) {
      // the archives' .meta files go with the downloads, and the property files
      // are the list's banner and readme: nothing of the install
      m_done += d.size;
      continue;
    }
    if (d.type != DirectiveType::FromArchive && d.type != DirectiveType::PatchedFromArchive &&
        d.type != DirectiveType::TransformedTexture && d.type != DirectiveType::InlineFile &&
        d.type != DirectiveType::RemappedInlineFile && d.type != DirectiveType::CreateBSA) {
      ++unsupported[typeName(d.type)];
      m_done += d.size;
      continue;
    }
    if (d.type != DirectiveType::FromArchive && d.type != DirectiveType::PatchedFromArchive &&
        d.type != DirectiveType::TransformedTexture) {
      continue;
    }
    bool skipped       = false;
    const QString path = targetOf(d, &skipped);
    if (skipped) {
      ++m_result.modOrganizer;
      m_done += d.size;
      continue;
    }
    if (d.badHash) {
      fail(d, tr("the list gives it a hash that isn't one"));
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
    if (index >= 0 && m_list->archives[std::size_t(index)].badHash) {
      // its download couldn't be checked against the list
      for (const Item& item : items) {
        fail(*item.d, tr("the list gives its archive %1 a hash that isn't one").arg(name));
        m_done += item.d->size;
      }
      continue;
    }
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
      m_done += item.d->size;
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
    if (item->d->type == DirectiveType::PatchedFromArchive ||
        item->d->type == DirectiveType::TransformedTexture) {
      finishItem(Item{item->d, {}, path});
      continue;
    }
    makeFolderFor(item->out);
    QFile::remove(item->out);
    if (copyChecked(*item->d, path, item->out)) {
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
      if (leaf->d->type == DirectiveType::PatchedFromArchive ||
          leaf->d->type == DirectiveType::TransformedTexture) {
        leaf->out = stageFile();
        n.outputs.push_back(leaf->out);
      } else {
        makeFolderFor(leaf->out);
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
  bool extractFailed = false;       // 7z said something went wrong
  if (isBethesdaArchive(path)) {
    BsaHandle handle;
    bsa_archive_t archive = handle.archive;
    const bsa_result_message_t loaded =
        bsa_load_from_file(archive, QDir::toNativeSeparators(path).toStdWString().c_str());
    if (loaded.code != BSA_RESULT_NONE) {
      for (const Item& item : items) {
        if (!item.rest.isEmpty()) {
          fail(*item.d, tr("its archive can't be read: %1").arg(bsaMessage(loaded)));
          m_done += item.d->size;
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
      makeFolderFor(n.outputs.front());
      const bsa_result_message_t r = bsa_extract_file(archive, inner.c_str(), first.c_str());
      if (r.code != BSA_RESULT_NONE) {
        reasons.insert(key, bsaMessage(r));
        continue;
      }
      // the other outputs are copies, hashed on the way: the hash of the
      // extracted file, so the files taken as they are need no second read.
      // outputs[i] is leaves[i]'s, the archive inside comes last
      std::optional<uint64_t> content;
      for (std::size_t i = 1; i < n.outputs.size(); ++i) {
        makeFolderFor(n.outputs[i]);
        QFile::remove(n.outputs[i]);
        const auto hash = copyFile(n.outputs.front(), n.outputs[i], &m_cancel);
        if (hash) {
          content = hash;
          continue;
        }
        QFile::remove(n.outputs[i]);
        if (i < n.leaves.size() && !cancelled()) {
          fail(*n.leaves[i]->d, tr("it can't be copied"));
          m_done += n.leaves[i]->d->size;
          n.leaves[i]->done = true;
        }
      }
      for (std::size_t i = 0; content && i < n.leaves.size(); ++i) {
        Item* leaf = n.leaves[i];
        if (leaf->done || leaf->d->type != DirectiveType::FromArchive) {
          continue;  // a staged file is patched or made into a texture first
        }
        if (leaf->d->hash == 0 || *content == leaf->d->hash) {
          leaf->verified = true;
        } else {
          // the extracted one stays as before; a bad copy goes
          if (i > 0) {
            QFile::remove(leaf->out);
          }
          fail(*leaf->d, tr("its hash isn't the list's"));
          m_done += leaf->d->size;
          leaf->done = true;
        }
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
          m_done += item.d->size;
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
      extractFailed = true;
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
      if (leaf->done) {
        continue;
      }
      // a texture isn't checked by hash: a source 7z didn't write fails here,
      // not as a texture that can't be read
      if (extractFailed && leaf->d->type == DirectiveType::TransformedTexture &&
          !QFileInfo(leaf->out).isFile()) {
        fail(*leaf->d, tr("%1 couldn't be taken out of its archive").arg(n.name));
        m_done += leaf->d->size;
        continue;
      }
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
    makeFolderFor(target);
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
  } else if (d.type == DirectiveType::TransformedTexture) {
    bool skipped         = false;
    const QString target = targetOf(d, &skipped);
    makeFolderFor(target);
    QString why;
    const QJsonObject state = QJsonDocument::fromJson(d.extra).object().value("ImageState").toObject();
    const bool made         = transformTexture(item.out, target, state, &why);
    if (item.out.startsWith(m_stage)) {
      QFile::remove(item.out);
    }
    if (made) {
      ++m_result.written;
      ++m_textures;
    } else {
      fail(d, why);
    }
  } else if (item.verified || verify(d, item.out)) {
    ++m_result.written;
  }
  m_done += d.size;
  report(QFileInfo(d.to).fileName());
}

void Installer::setScreenSize()
{
  if (m_plan.screenWidth <= 0 || m_plan.screenHeight <= 0) {
    return;
  }
  const QString width  = QString::number(m_plan.screenWidth);
  const QString height = QString::number(m_plan.screenHeight);
  auto rewrite         = [](const QString& path, const std::function<bool(QString&)>& edit) {
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) {
      return;
    }
    QString text = QString::fromLatin1(f.readAll());
    f.close();
    if (!edit(text)) {
      return;
    }
    QSaveFile out(path);
    if (out.open(QIODevice::WriteOnly)) {
      out.write(text.toLatin1());
      out.commit();
    }
  };

  // the files are the list's own: found among its directives, not by walking
  // the mods' folders (hundreds of thousands of files)
  QStringList prefs, tweaks;
  QSet<QString> seen;
  for (const Directive& d : m_list->directives) {
    if (d.modOrganizerFile) {
      continue;
    }
    const QString to    = QString(d.to).replace(u'/', u'\\').toLower();
    const QString first = to.section(u'\\', 0, 0);
    const QString name  = to.section(u'\\', -1);
    const bool prefsFile =
        first == u"profiles" && (name.endsWith(u"prefs.ini") || name == u"oblivion.ini");
    const bool tweaksFile = first == u"mods" && name == u"ssedisplaytweaks.ini";
    if ((!prefsFile && !tweaksFile) || seen.contains(to)) {
      continue;
    }
    seen.insert(to);
    const QString path = installPath(m_plan.installFolder, d.to);
    if (!path.isEmpty()) {
      (prefsFile ? prefs : tweaks).append(path);
    }
  }

  // the profiles' prefs (SkyrimPrefs.ini, Fallout4Prefs.ini...) and Oblivion.ini
  for (const QString& path : prefs) {
    rewrite(path, [&](QString& text) {
      // both or neither: a prefs file without them keeps the game's default
      if (!text.contains(u"iSize W", Qt::CaseInsensitive) ||
          !text.contains(u"iSize H", Qt::CaseInsensitive)) {
        return false;
      }
      const bool w = setIniValue(text, QStringLiteral("Display"), QStringLiteral("iSize W"), width);
      const bool h = setIniValue(text, QStringLiteral("Display"), QStringLiteral("iSize H"), height);
      return w || h;
    });
  }
  // SSE Display Tweaks, in whichever mod it is
  for (const QString& path : tweaks) {
    rewrite(path, [&](QString& text) {
      return setIniValue(text, QStringLiteral("Render"), QStringLiteral("Resolution"),
                         width + u'x' + height);
    });
  }
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
    // MO2 Revamped: read whole, so not without end (the list's own files are
    // settings and small generated files)
    if (source.size() > MaxInlineFileSize) {
      fail(d, tr("the list's own copy is too large"));
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
    if (d.badHash) {
      fail(d, tr("the list gives it a hash that isn't one"));
      m_done += d.size;
      continue;
    }
    if (target.isEmpty()) {
      fail(d, tr("its path leads out of the install folder"));
      continue;
    }
    makeFolderFor(target);
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
    if (d.badHash) {
      fail(d, tr("the list gives it a hash that isn't one"));
      m_done += d.size;
      continue;
    }
    if (target.isEmpty() || root.isEmpty()) {
      fail(d, tr("its path leads out of the install folder"));
      m_done += d.size;
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
      m_done += d.size;
      continue;
    }

    // libbsarch compresses all the files of an archive or none: all only when
    // the list compresses every one. Sounds the author kept uncompressed (the
    // games don't play them compressed) stay so, the archive a little larger
    const bool archiveCompressed =
        type.startsWith(u"BSAState") ? (quint32(state.value("ArchiveFlags").toDouble()) & 0x4) != 0
                                     : true;
    bool allCompressed = archiveCompressed;

    // the files in one pass: whether each is compressed, and where it is
    DdsInfo dds;
    BsaEntries entryList;
    bsa_entry_list_t entries = entryList.list;
    std::vector<std::pair<std::wstring, std::wstring>> files;
    bool missing = false;
    const QJsonArray fileStates = extra.value("FileStates").toArray();
    for (const QJsonValue& v : fileStates) {
      const QJsonObject f = v.toObject();
      const bool compressed =
          type.startsWith(u"BSAState")
              ? archiveCompressed != f.value("FlipCompression").toBool(false)
              : f.value("Compressed").toBool(true);
      if (!compressed) {
        allCompressed = false;
      }
      const QString inner  = f.value("Path").toString().replace(u'/', u'\\');
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
      m_done += d.size;
      continue;
    }

    makeFolderFor(target);
    QFile::remove(target);
    BsaHandle handle;
    bsa_archive_t archive = handle.archive;
    if (textures) {
      bsa_file_dds_info_callback_set(archive, &ddsInfo, &dds);
    }
    bsa_result_message_t r = bsa_create_archive(
        archive, QDir::toNativeSeparators(target).toStdWString().c_str(), archiveType, entries);
    if (r.code == BSA_RESULT_NONE && type.startsWith(u"BSAState")) {
      quint32 archiveFlags = quint32(state.value("ArchiveFlags").toDouble());
      if (!allCompressed) {
        archiveFlags &= ~quint32(0x4);  // its files stored as they are
      }
      bsa_archive_flags_set(archive, archiveFlags);
      bsa_file_flags_set(archive, quint32(state.value("FileFlags").toDouble()));
      bsa_compress_set(archive, allCompressed);
    } else if (r.code == BSA_RESULT_NONE) {
      // textures (DX10) always compressed, as Wabbajack's own
      bsa_compress_set(archive, textures || allCompressed);
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
      m_done += d.size;
      continue;
    }
    // Wabbajack doesn't check a built archive's hash either: its files were.
    // Only the list's size counts: the built file's isn't in the total
    ++m_result.written;
    ++built;
    m_done += d.size;
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
      if (!cancelled()) {
        setScreenSize();
      }
      if (m_textures > 0) {
        m_result.notes.append(tr("%n textures resized.", nullptr, m_textures));
      }
      // folders the author leaves to be copied by hand (into the game's folder,
      // usually), as the list's guide says: Wabbajack doesn't copy them either
      const QStringList byHand = byHandFolders(m_plan.installFolder);
      if (!byHand.isEmpty()) {
        m_result.notes.append(
            tr("The list leaves files for you to copy by hand, as its guide says: %1.")
                .arg(byHand.join(QStringLiteral(", "))));
      }
      // Root Builder's layout: a mod's Root/ goes to the game's folder. When
      // MO2 maps the Root folders itself, its programs and DLLs (nvse_loader.exe,
      // d3d9.dll) are handled when a program starts (forced loads, the import
      // table, or a copy put back afterwards); otherwise they're the user's to
      // place. The summary names those mods
      QStringList rootMods;
      const QDir mods(installPath(m_plan.installFolder, QStringLiteral("mods")));
      for (const QString& mod : mods.entryList(QDir::Dirs | QDir::NoDotAndDotDot)) {
        const QDir modDir(mods.filePath(mod));
        for (const QString& sub : modDir.entryList(QDir::Dirs | QDir::NoDotAndDotDot)) {
          if (sub.compare(u"Root", Qt::CaseInsensitive) != 0) {
            continue;
          }
          const QStringList binaries = QDir(modDir.filePath(sub))
                                           .entryList({QStringLiteral("*.exe"),
                                                       QStringLiteral("*.dll")},
                                                      QDir::Files);
          if (!binaries.isEmpty()) {
            rootMods.append(mod);
          }
        }
      }
      if (!rootMods.isEmpty() && m_plan.rootHandledByMo2) {
        m_result.notes.append(
            tr("These mods have programs or DLLs for the game's own folder (in their "
               "Root folder): %1. MO2 Revamped puts them in place each time the game "
               "starts and takes them out when it closes; nothing has to be copied by "
               "hand.")
                .arg(rootMods.join(QStringLiteral(", "))));
      } else if (!rootMods.isEmpty()) {
        m_result.notes.append(
            tr("These mods have programs or DLLs for the game's own folder (in their "
               "Root folder): %1. They have to be put in the game's folder, with Root "
               "Builder or by hand.")
                .arg(rootMods.join(QStringLiteral(", "))));
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
  // with files that failed, the BSAs' gathered files stay for the next try
  if (!cancelled() && m_result.failed == 0) {
    const QString gathered = installPath(m_plan.installFolder, QStringLiteral("TEMP_BSA_FILES"));
    if (!gathered.isEmpty()) {
      QDir(gathered).removeRecursively();
    }
  }

  m_result.plugins = m_plugins.values();
  m_result.plugins.sort(Qt::CaseInsensitive);
  if (m_result.failed > m_result.failures.size()) {
    m_result.failures.append(tr("… and %n more files.", nullptr,
                                m_result.failed - int(m_result.failures.size())));
  }
  m_result.cancelled = m_cancel.load();
  m_result.ok        = !m_result.cancelled && m_result.error.isEmpty();
  if (m_result.ok && m_result.failed == 0) {
    // a finished install: its folder isn't one to go on in
    QSaveFile finished(QDir(m_state).filePath("finished"));
    if (finished.open(QIODevice::WriteOnly)) {
      finished.write(m_list->name.toUtf8());
      finished.commit();
    }
  }
  if (m_result.ok) {
    m_done = m_total;
  }
  if (m_result.failed > ListedFailures) {
    log::warn("wabbajack install: {} files failed in all, the first {} logged",
              m_result.failed, ListedFailures);
  } else if (m_result.failed > 0) {
    log::warn("wabbajack install: {} files failed", m_result.failed);
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

QStringList byHandFolders(const QString& installFolder)
{
  QStringList result;
  for (const QString& dir :
       QDir(installFolder).entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name)) {
    if (dir.startsWith(u"__") || dir.compare(u"Game Folder Files", Qt::CaseInsensitive) == 0 ||
        dir.compare(u"Manual Game Files", Qt::CaseInsensitive) == 0) {
      result.append(dir);
    }
  }
  return result;
}

QString installPath(const QString& installFolder, const QString& relative)
{
  QString r = QString(relative).replace(u'\\', u'/');
  if (r.isEmpty() || QDir::isAbsolutePath(r) || r.contains(u':') || r.startsWith(u'/')) {
    return {};
  }
  // MO2 Revamped: and the collections' rules: no characters Windows refuses,
  // no part ending in a dot or a space, no CONIN$, CONOUT$, CLOCK$ or COM¹
  if (!Collections::isSafeRelativePath(r)) {
    return {};
  }
  static const QRegularExpression device(
      QStringLiteral("^(con|prn|aux|nul|com[0-9]|lpt[0-9])(\\..*)?$"),
      QRegularExpression::CaseInsensitiveOption);
  // an 8.3 short name (MO2R-I~1) reaches a folder by another name: the
  // install's own one among them
  static const QRegularExpression shortName(
      QStringLiteral(R"(^[^.~\s]{1,6}~[0-9]{1,6}(\.[^.]{0,3})?$)"),
      QRegularExpression::CaseInsensitiveOption);
  const QStringList parts = r.split(u'/');
  if (parts.front().compare(QString::fromLatin1(StateName), Qt::CaseInsensitive) == 0) {
    return {};
  }
  for (const QString& part : parts) {
    if (part == u".." || device.match(part.trimmed()).hasMatch() ||
        shortName.match(part).hasMatch()) {
      return {};
    }
  }
  QString base = QDir::cleanPath(installFolder);
  if (!base.endsWith(u'/')) {
    base += u'/';
  }
  const QString full = QDir::cleanPath(base + r);
  if (!full.startsWith(base, Qt::CaseInsensitive)) {
    return {};
  }
  // "./.mo2r-install/..." is the install's own folder too
  const QString inside = full.mid(base.size());
  if (inside.section(u'/', 0, 0).compare(QString::fromLatin1(StateName), Qt::CaseInsensitive) ==
      0) {
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
