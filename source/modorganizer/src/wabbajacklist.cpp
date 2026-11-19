#include "wabbajacklist.h"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>

#include <archive.h>
#include <iplugingame.h>
#include <log.h>

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSet>
#include <QTemporaryDir>

#include <cstring>

// boost::json, header-only: its sources are compiled here, once. It uses only
// Boost.Container's memory_resource class, which needs none of that library's
// compiled parts, so nothing is linked for it
#define BOOST_CONTAINER_NO_LIB
#include <boost/json/src.hpp>

using namespace MOBase;
namespace json = boost::json;

namespace Wabbajack
{

// ---- xxHash64 -------------------------------------------------------------

namespace
{

constexpr uint64_t Prime1 = 0x9E3779B185EBCA87ULL;
constexpr uint64_t Prime2 = 0xC2B2AE3D27D4EB4FULL;
constexpr uint64_t Prime3 = 0x165667B19E3779F9ULL;
constexpr uint64_t Prime4 = 0x85EBCA77C2B2AE63ULL;
constexpr uint64_t Prime5 = 0x27D4EB2F165667C5ULL;

inline uint64_t rotl(uint64_t x, int r)
{
  return (x << r) | (x >> (64 - r));
}

inline uint64_t read64(const unsigned char* p)
{
  uint64_t v;
  std::memcpy(&v, p, 8);  // x64 is little-endian, as xxHash wants
  return v;
}

inline uint32_t read32(const unsigned char* p)
{
  uint32_t v;
  std::memcpy(&v, p, 4);
  return v;
}

inline uint64_t xxRound(uint64_t acc, uint64_t input)
{
  acc += input * Prime2;
  acc = rotl(acc, 31);
  return acc * Prime1;
}

inline uint64_t mergeRound(uint64_t acc, uint64_t val)
{
  acc ^= xxRound(0, val);
  return acc * Prime1 + Prime4;
}

}  // namespace

XxHash64::XxHash64()
{
  m_v[0] = Prime1 + Prime2;
  m_v[1] = Prime2;
  m_v[2] = 0;
  m_v[3] = 0 - Prime1;
}

void XxHash64::update(const void* data, std::size_t size)
{
  auto* p   = static_cast<const unsigned char*>(data);
  auto* end = p + size;
  m_total += size;

  if (m_buffered + size < 32) {
    std::memcpy(m_buffer + m_buffered, p, size);
    m_buffered += size;
    return;
  }

  if (m_buffered > 0) {
    const std::size_t fill = 32 - m_buffered;
    std::memcpy(m_buffer + m_buffered, p, fill);
    p += fill;
    for (int i = 0; i < 4; ++i) {
      m_v[i] = xxRound(m_v[i], read64(m_buffer + i * 8));
    }
    m_buffered = 0;
  }

  uint64_t v0 = m_v[0], v1 = m_v[1], v2 = m_v[2], v3 = m_v[3];
  while (end - p >= 32) {
    v0 = xxRound(v0, read64(p));
    v1 = xxRound(v1, read64(p + 8));
    v2 = xxRound(v2, read64(p + 16));
    v3 = xxRound(v3, read64(p + 24));
    p += 32;
  }
  m_v[0] = v0;
  m_v[1] = v1;
  m_v[2] = v2;
  m_v[3] = v3;

  m_buffered = std::size_t(end - p);
  if (m_buffered > 0) {
    std::memcpy(m_buffer, p, m_buffered);
  }
}

uint64_t XxHash64::digest() const
{
  uint64_t h;
  if (m_total >= 32) {
    h = rotl(m_v[0], 1) + rotl(m_v[1], 7) + rotl(m_v[2], 12) + rotl(m_v[3], 18);
    for (int i = 0; i < 4; ++i) {
      h = mergeRound(h, m_v[i]);
    }
  } else {
    h = m_v[2] + Prime5;  // the seed, 0
  }
  h += m_total;

  const unsigned char* p   = m_buffer;
  const unsigned char* end = m_buffer + m_buffered;
  while (end - p >= 8) {
    h ^= xxRound(0, read64(p));
    h = rotl(h, 27) * Prime1 + Prime4;
    p += 8;
  }
  if (end - p >= 4) {
    h ^= uint64_t(read32(p)) * Prime1;
    h = rotl(h, 23) * Prime2 + Prime3;
    p += 4;
  }
  while (p < end) {
    h ^= uint64_t(*p) * Prime5;
    h = rotl(h, 11) * Prime1;
    ++p;
  }

  h ^= h >> 33;
  h *= Prime2;
  h ^= h >> 29;
  h *= Prime3;
  h ^= h >> 32;
  return h;
}

uint64_t XxHash64::of(const void* data, std::size_t size)
{
  XxHash64 h;
  h.update(data, size);
  return h.digest();
}

uint64_t hashFromBase64(QStringView text, bool* ok)
{
  const QByteArray bytes = QByteArray::fromBase64(text.toLatin1());
  if (bytes.size() != 8) {
    if (ok != nullptr) {
      *ok = false;
    }
    return 0;
  }
  uint64_t v;
  std::memcpy(&v, bytes.constData(), 8);
  if (ok != nullptr) {
    *ok = true;
  }
  return v;
}

QString hashToBase64(uint64_t hash)
{
  return QString::fromLatin1(
      QByteArray(reinterpret_cast<const char*>(&hash), 8).toBase64());
}

LowPriority::LowPriority(Mode mode) : m_mode(mode)
{
  if (mode == Mode::Background) {
    // fails when the thread already is in background mode: nothing to undo then
    m_set = ::SetThreadPriority(::GetCurrentThread(), THREAD_MODE_BACKGROUND_BEGIN) != 0;
  } else {
    m_previous = ::GetThreadPriority(::GetCurrentThread());
    m_set      = m_previous != THREAD_PRIORITY_ERROR_RETURN &&
            ::SetThreadPriority(::GetCurrentThread(), THREAD_PRIORITY_BELOW_NORMAL) != 0;
  }
}

LowPriority::~LowPriority()
{
  // the pool thread goes back as it came, for the next work it gets
  if (!m_set) {
    return;
  }
  if (m_mode == Mode::Background) {
    ::SetThreadPriority(::GetCurrentThread(), THREAD_MODE_BACKGROUND_END);
  } else {
    ::SetThreadPriority(::GetCurrentThread(), m_previous);
  }
}

std::optional<uint64_t> copyFile(const QString& source, const QString& target,
                                 const std::atomic_bool* cancel)
{
  QFile in(source);
  QFile out(target);
  if (!in.open(QIODevice::ReadOnly) || !out.open(QIODevice::WriteOnly)) {
    return std::nullopt;
  }
  XxHash64 h;
  QByteArray buffer(4 * 1024 * 1024, Qt::Uninitialized);
  for (;;) {
    if (cancel != nullptr && cancel->load()) {
      return std::nullopt;
    }
    const qint64 n = in.read(buffer.data(), buffer.size());
    if (n < 0) {
      return std::nullopt;
    }
    if (n == 0) {
      break;
    }
    if (out.write(buffer.constData(), n) != n) {
      return std::nullopt;
    }
    h.update(buffer.constData(), std::size_t(n));
  }
  if (!out.flush()) {
    return std::nullopt;
  }
  return h.digest();
}

std::optional<uint64_t> hashFile(const QString& path, const std::atomic_bool* cancel,
                                 const std::function<void(qint64)>& progress)
{
  QFile f(path);
  if (!f.open(QIODevice::ReadOnly)) {
    return std::nullopt;
  }

  XxHash64 h;
  QByteArray buffer(4 * 1024 * 1024, Qt::Uninitialized);
  qint64 done = 0;
  for (;;) {
    if (cancel != nullptr && cancel->load()) {
      return std::nullopt;
    }
    const qint64 n = f.read(buffer.data(), buffer.size());
    if (n < 0) {
      return std::nullopt;
    }
    if (n == 0) {
      break;
    }
    h.update(buffer.constData(), std::size_t(n));
    done += n;
    if (progress) {
      progress(done);
    }
  }
  return h.digest();
}

// ---- types ----------------------------------------------------------------

namespace
{

// "FromArchive, Wabbajack.Lib", "FromArchive", "MediaFireDownloader+State, ..."
QString baseType(QString t)
{
  const int comma = t.indexOf(u',');
  if (comma >= 0) {
    t.truncate(comma);
  }
  t = t.trimmed();
  if (t.endsWith(u"+State")) {
    t.chop(6);
  }
  return t;
}

DirectiveType directiveType(const QString& t)
{
  static const QHash<QString, DirectiveType> types = {
      {"FromArchive", DirectiveType::FromArchive},
      {"PatchedFromArchive", DirectiveType::PatchedFromArchive},
      {"TransformedTexture", DirectiveType::TransformedTexture},
      {"InlineFile", DirectiveType::InlineFile},
      {"RemappedInlineFile", DirectiveType::RemappedInlineFile},
      {"ArchiveMeta", DirectiveType::ArchiveMeta},
      {"PropertyFile", DirectiveType::PropertyFile},
      {"CreateBSA", DirectiveType::CreateBSA},
      {"MergedPatch", DirectiveType::MergedPatch},
  };
  return types.value(t, DirectiveType::Unknown);
}

Source sourceType(const QString& t)
{
  static const QHash<QString, Source> types = {
      {"NexusDownloader", Source::Nexus},
      {"Nexus", Source::Nexus},
      {"GameFileSourceDownloader", Source::GameFile},
      {"GameFileSource", Source::GameFile},
      {"HttpDownloader", Source::Http},
      {"HTTPDownloader", Source::Http},
      {"Http", Source::Http},
      {"WabbajackCDNDownloader", Source::WabbajackCdn},
      {"WabbajackCDN", Source::WabbajackCdn},
      {"GoogleDriveDownloader", Source::GoogleDrive},
      {"GoogleDrive", Source::GoogleDrive},
      {"MegaDownloader", Source::Mega},
      {"Mega", Source::Mega},
      {"MediaFireDownloader", Source::MediaFire},
      {"MediaFire", Source::MediaFire},
      {"ModDBDownloader", Source::ModDB},
      {"ModDB", Source::ModDB},
      {"LoversLabOAuthDownloader", Source::LoversLab},
      {"LoversLabDownloader", Source::LoversLab},
      {"LoversLab", Source::LoversLab},
      {"VectorPlexusOAuthDownloader", Source::LoversLab},
      {"VectorPlexusDownloader", Source::LoversLab},
      {"VectorPlexus", Source::LoversLab},
      {"TESAllianceDownloader", Source::LoversLab},
      {"TESAlliance", Source::LoversLab},
      {"ManualDownloader", Source::Manual},
      {"Manual", Source::Manual},
      {"BethesdaNetDownloader", Source::Bethesda},
      {"Bethesda", Source::Bethesda},
  };
  return types.value(t, Source::Unknown);
}

// ---- reading the JSON as a stream -----------------------------------------
//
// boost::json's basic_parser calls the handler for every token. The values at
// the top of the list (Name, Author...) and each element of Archives and
// Directives are built one at a time with a value_stack and handed over, so
// at no time is more than one element held as JSON.

class StreamHandler
{
public:
  static constexpr std::size_t max_array_size  = std::size_t(-1);
  static constexpr std::size_t max_object_size = std::size_t(-1);
  static constexpr std::size_t max_string_size = std::size_t(-1);
  static constexpr std::size_t max_key_size    = std::size_t(-1);

  std::function<void(std::string_view key, json::value&&)> onTop;
  std::function<void(json::value&&)> onArchive;
  std::function<void(json::value&&)> onDirective;

  bool on_document_begin(json::error_code&) { return true; }
  bool on_document_end(json::error_code&) { return true; }

  bool on_object_begin(json::error_code&) { return begin(); }
  bool on_array_begin(json::error_code& ec)
  {
    // Archives: [ ... ] and Directives: [ ... ] are walked, not built
    if (m_depth == 1 && (m_key == "Archives" || m_key == "Directives")) {
      m_list = m_key == "Archives" ? List::Archives : List::Directives;
      ++m_depth;
      return true;
    }
    return begin();
  }

  bool on_object_end(std::size_t n, json::error_code&)
  {
    return end([&] {
      m_stack.push_object(n);
    });
  }

  bool on_array_end(std::size_t n, json::error_code&)
  {
    if (m_depth == 2 && m_list != List::None) {
      // the end of Archives or Directives
      m_list = List::None;
      --m_depth;
      return true;
    }
    return end([&] {
      m_stack.push_array(n);
    });
  }

  bool on_key_part(json::string_view s, std::size_t, json::error_code&)
  {
    if (m_depth == 1) {
      newKey();
      m_key.append(s.data(), s.size());
    } else if (building()) {
      m_stack.push_chars(s);
    }
    return true;
  }

  bool on_key(json::string_view s, std::size_t, json::error_code&)
  {
    if (m_depth == 1) {
      newKey();
      m_key.append(s.data(), s.size());
      m_haveKey = true;
    } else if (building()) {
      m_stack.push_key(s);
    }
    return true;
  }

  bool on_string_part(json::string_view s, std::size_t, json::error_code&)
  {
    if (building() || topScalar()) {
      if (topScalar() && !m_scalarStarted) {
        m_stack.reset();
        m_scalarStarted = true;
      }
      m_stack.push_chars(s);
    }
    return true;
  }

  bool on_string(json::string_view s, std::size_t, json::error_code&)
  {
    return scalar([&] {
      m_stack.push_string(s);
    });
  }

  bool on_number_part(json::string_view, json::error_code&) { return true; }
  bool on_int64(int64_t i, json::string_view, json::error_code&)
  {
    return scalar([&] {
      m_stack.push_int64(i);
    });
  }
  bool on_uint64(uint64_t u, json::string_view, json::error_code&)
  {
    return scalar([&] {
      m_stack.push_uint64(u);
    });
  }
  bool on_double(double d, json::string_view, json::error_code&)
  {
    return scalar([&] {
      m_stack.push_double(d);
    });
  }
  bool on_bool(bool b, json::error_code&)
  {
    return scalar([&] {
      m_stack.push_bool(b);
    });
  }
  bool on_null(json::error_code&)
  {
    return scalar([&] {
      m_stack.push_null();
    });
  }
  bool on_comment_part(json::string_view, json::error_code&) { return true; }
  bool on_comment(json::string_view, json::error_code&) { return true; }

private:
  enum class List
  {
    None,
    Archives,
    Directives
  };

  json::value_stack m_stack;
  std::size_t m_depth = 0;  // containers open
  std::string m_key;        // the key at the top of the list
  bool m_haveKey       = false;
  bool m_scalarStarted = false;
  List m_list          = List::None;

  // a key at the top after a value: the key before it is done with
  void newKey()
  {
    if (m_haveKey) {
      m_key.clear();
      m_haveKey = false;
    }
  }

  // the depth an element being built starts at
  std::size_t baseDepth() const { return m_list != List::None ? 2 : 1; }
  bool building() const { return m_depth > baseDepth(); }
  bool topScalar() const { return m_depth == baseDepth() && m_depth >= 1; }

  bool begin()
  {
    if (m_depth == 0) {
      // the list itself
      m_depth = 1;
      return true;
    }
    if (m_depth == baseDepth()) {
      m_stack.reset();
    }
    ++m_depth;
    return true;
  }

  template <class Push>
  bool end(Push push)
  {
    if (m_depth == 1) {
      // the end of the list
      m_depth = 0;
      return true;
    }
    push();
    --m_depth;
    if (m_depth == baseDepth()) {
      deliver();
    }
    return true;
  }

  template <class Push>
  bool scalar(Push push)
  {
    if (building()) {
      push();
    } else if (topScalar()) {
      if (!m_scalarStarted) {
        m_stack.reset();
      }
      push();
      m_scalarStarted = false;
      deliver();
    }
    return true;
  }

  void deliver()
  {
    json::value v = m_stack.release();
    if (m_list == List::Archives) {
      if (onArchive) {
        onArchive(std::move(v));
      }
    } else if (m_list == List::Directives) {
      if (onDirective) {
        onDirective(std::move(v));
      }
    } else {
      if (onTop) {
        onTop(m_key, std::move(v));
      }
      m_key.clear();
      m_haveKey = false;
    }
  }
};

QString str(const json::object& o, std::string_view key)
{
  const json::value* v = o.if_contains(key);
  if (v == nullptr) {
    return {};
  }
  if (v->is_string()) {
    const auto& s = v->get_string();
    return QString::fromUtf8(s.data(), qsizetype(s.size()));
  }
  if (v->is_int64()) {
    return QString::number(v->get_int64());
  }
  if (v->is_uint64()) {
    return QString::number(v->get_uint64());
  }
  return {};
}

qint64 num(const json::object& o, std::string_view key)
{
  const json::value* v = o.if_contains(key);
  if (v == nullptr) {
    return 0;
  }
  if (v->is_int64()) {
    return v->get_int64();
  }
  if (v->is_uint64()) {
    return qint64(v->get_uint64());
  }
  if (v->is_double()) {
    return qint64(v->get_double());
  }
  if (v->is_string()) {
    return str(o, key).toLongLong();
  }
  return 0;
}

// a hash the list gives; *bad set when it gives one that isn't (not a string,
// or not 8 bytes of Base64). Absent or null: 0, nothing to check, as before
uint64_t hashOf(const json::object& o, std::string_view key, bool* bad)
{
  const json::value* v = o.if_contains(key);
  if (v == nullptr || v->is_null()) {
    return 0;
  }
  bool ok = false;
  const uint64_t h =
      v->is_string() ? hashFromBase64(QString::fromUtf8(v->get_string().data(),
                                                        qsizetype(v->get_string().size())),
                                      &ok)
                     : 0;
  if (!ok) {
    *bad = true;
  }
  return h;
}

QByteArray compact(const json::object& o, std::initializer_list<std::string_view> keys)
{
  json::object out;
  for (auto k : keys) {
    if (const json::value* v = o.if_contains(k)) {
      out[k] = *v;
    }
  }
  if (out.empty()) {
    return {};
  }
  const std::string s = json::serialize(out);
  return QByteArray(s.data(), qsizetype(s.size()));
}

Archive toArchive(const json::object& o)
{
  Archive a;
  a.hash = hashOf(o, "Hash", &a.badHash);
  a.name = str(o, "Name");
  a.size = num(o, "Size");
  a.meta = str(o, "Meta");

  const json::value* sv = o.if_contains("State");
  if (sv == nullptr || !sv->is_object()) {
    return a;
  }
  const json::object& s = sv->get_object();
  a.sourceType          = baseType(str(s, "$type"));
  a.source              = sourceType(a.sourceType);

  switch (a.source) {
  case Source::Nexus:
    a.nexusGame = str(s, "GameName");
    if (a.nexusGame.isEmpty()) {
      a.nexusGame = str(s, "Game");
    }
    a.modId   = num(s, "ModID");
    a.fileId  = num(s, "FileID");
    a.modName = str(s, "Name");
    a.author  = str(s, "Author");
    a.version = str(s, "Version");
    break;
  case Source::GameFile:
    a.game        = str(s, "Game");
    a.gameFile    = str(s, "GameFile");
    a.gameVersion = str(s, "GameVersion");
    break;
  case Source::GoogleDrive:
    a.url = str(s, "Id");
    break;
  case Source::LoversLab:
    a.url = str(s, "IPS4Url");
    if (a.url.isEmpty()) {
      a.url = str(s, "URL");
    }
    a.modName = str(s, "Name");
    a.author  = str(s, "Author");
    a.version = str(s, "Version");
    break;
  case Source::Manual:
    a.url    = str(s, "Url");
    a.prompt = str(s, "Prompt");
    break;
  case Source::Bethesda:
    a.modName = str(s, "Name");
    break;
  default:
    a.url = str(s, "Url");
    if (a.url.isEmpty()) {
      a.url = str(s, "URL");
    }
    if (a.source == Source::Http) {
      if (const json::value* h = s.if_contains("Headers"); h != nullptr && h->is_array()) {
        for (const auto& v : h->get_array()) {
          if (v.is_string()) {
            a.headers.append(
                QString::fromUtf8(v.get_string().data(), qsizetype(v.get_string().size())));
          }
        }
      }
    }
    break;
  }
  return a;
}

Directive toDirective(const json::object& o)
{
  Directive d;
  d.type = directiveType(baseType(str(o, "$type")));
  d.hash = hashOf(o, "Hash", &d.badHash);
  d.size = num(o, "Size");
  d.to   = str(o, "To");
  d.to.replace(u'/', u'\\');

  if (const json::value* p = o.if_contains("ArchiveHashPath"); p != nullptr && p->is_array()) {
    const json::array& parts = p->get_array();
    for (std::size_t i = 0; i < parts.size(); ++i) {
      if (!parts[i].is_string()) {
        // the archive's hash must be one: without it the file can't be found
        if (i == 0) {
          d.badHash = true;
        }
        continue;
      }
      const auto& s = parts[i].get_string();
      const QString part = QString::fromUtf8(s.data(), qsizetype(s.size()));
      if (i == 0) {
        bool ok       = false;
        d.archiveHash = hashFromBase64(part, &ok);
        if (!ok) {
          d.badHash = true;
        }
      } else {
        d.archivePath.append(part);
      }
    }
  }

  switch (d.type) {
  case DirectiveType::InlineFile:
  case DirectiveType::RemappedInlineFile:
  case DirectiveType::ArchiveMeta:
  case DirectiveType::PropertyFile:
    d.dataId = str(o, "SourceDataID");
    break;
  case DirectiveType::PatchedFromArchive:
    d.dataId = str(o, "PatchID");
    break;
  case DirectiveType::MergedPatch:
    d.dataId = str(o, "PatchID");
    d.extra  = compact(o, {"Sources"});
    break;
  case DirectiveType::CreateBSA:
    d.dataId = str(o, "TempID");
    d.extra  = compact(o, {"State", "FileStates"});
    break;
  case DirectiveType::TransformedTexture:
    d.extra = compact(o, {"ImageState"});
    break;
  default:
    break;
  }
  return d;
}

// the part of a relative path before its first backslash, lowercase
QString firstPart(const QString& path, QString* second = nullptr)
{
  const int slash = path.indexOf(u'\\');
  if (slash < 0) {
    if (second != nullptr) {
      second->clear();
    }
    return {};
  }
  if (second != nullptr) {
    const int next = path.indexOf(u'\\', slash + 1);
    *second        = next < 0 ? path.mid(slash + 1) : path.mid(slash + 1, next - slash - 1);
  }
  return path.left(slash).toLower();
}

// takes "modlist" (or "modlist.json") and the image out of the zip
bool extractEntries(const QString& path, const QString& into, QString* listFile,
                    QString* imageFile, QString* error)
{
  std::unique_ptr<::Archive> archive = CreateArchive();
  if (!archive->isValid()) {
    *error = QCoreApplication::translate("Wabbajack", "the archive library couldn't be loaded");
    return false;
  }
  if (!archive->open(path.toStdWString(), {})) {
    *error = QCoreApplication::translate("Wabbajack", "it couldn't be opened as a zip file");
    return false;
  }

  FileData* list     = nullptr;
  FileData* oldList  = nullptr;
  FileData* image    = nullptr;
  for (FileData* f : archive->getFileList()) {
    if (f->isDirectory()) {
      continue;
    }
    const QString name = QString::fromStdWString(f->getArchiveFilePath());
    if (name.compare(u"modlist", Qt::CaseInsensitive) == 0) {
      list = f;
    } else if (name.compare(u"modlist.json", Qt::CaseInsensitive) == 0) {
      oldList = f;
    } else if (name.compare(u"modlist-image.png", Qt::CaseInsensitive) == 0) {
      image = f;
    }
  }
  if (list == nullptr) {
    list = oldList;
  }
  if (list == nullptr) {
    archive->close();
    *error = QCoreApplication::translate("Wabbajack",
                                         "it has no modlist inside: it isn't a Wabbajack list");
    return false;
  }

  // MO2 Revamped: the biggest real lists' modlist is a few hundred MB; a far
  // larger one (a zip bomb) isn't written out
  constexpr uint64_t MaxModlistSize = uint64_t(4) << 30;
  if (list->getSize() > MaxModlistSize) {
    archive->close();
    *error = QCoreApplication::translate("Wabbajack", "its modlist is too large");
    return false;
  }
  list->addOutputFilePath(L"modlist");
  if (image != nullptr && image->getSize() > (uint64_t(64) << 20)) {
    image = nullptr;
  }
  if (image != nullptr) {
    image->addOutputFilePath(L"modlist-image.png");
  }
  const bool ok = archive->extract(QDir::toNativeSeparators(into).toStdWString(), {}, {}, {});
  archive->close();
  if (!ok) {
    *error = QCoreApplication::translate("Wabbajack", "its modlist couldn't be extracted");
    return false;
  }
  *listFile  = QDir(into).filePath("modlist");
  *imageFile = image != nullptr ? QDir(into).filePath("modlist-image.png") : QString();
  return true;
}

}  // namespace

bool isListFile(const QString& path)
{
  return path.endsWith(u".wabbajack", Qt::CaseInsensitive) && QFileInfo(path).isFile();
}

qint64 ModList::downloadSize() const
{
  qint64 total = 0;
  for (const Archive& a : archives) {
    if (a.source != Source::GameFile) {
      total += a.size;
    }
  }
  return total;
}

std::optional<ModList> readModList(const QString& path, QString* error,
                                   bool keepDirectives, const std::atomic_bool* cancel)
{
  QString dummy;
  if (error == nullptr) {
    error = &dummy;
  }

  QTemporaryDir temp;
  if (!temp.isValid()) {
    *error = QCoreApplication::translate("Wabbajack", "no temporary folder could be made");
    return std::nullopt;
  }

  QString listFile, imageFile;
  if (!extractEntries(path, temp.path(), &listFile, &imageFile, error)) {
    return std::nullopt;
  }

  ModList list;
  QSet<QString> unknown;

  // what is needed after the whole list is read: which archives are Mod
  // Organizer's own, known only once every directive has been seen
  struct Use
  {
    uint64_t archive;
    qint64 size;
  };
  std::vector<Use> uses;
  QSet<uint64_t> modOrganizerArchives;
  struct PluginUse
  {
    uint64_t archive;
    QString name;
  };
  std::vector<PluginUse> plugins;
  QSet<QString> profiles;

  StreamHandler* handler = nullptr;
  json::parse_options options;
  options.max_depth          = 256;
  options.allow_invalid_utf8 = true;
  json::basic_parser<StreamHandler> parser(options);
  handler = &parser.handler();

  handler->onTop = [&](std::string_view key, json::value&& v) {
    auto text = [&] {
      if (v.is_string()) {
        const auto& s = v.get_string();
        return QString::fromUtf8(s.data(), qsizetype(s.size()));
      }
      return QString();
    };
    if (key == "Name") {
      list.name = text();
    } else if (key == "Author") {
      list.author = text();
    } else if (key == "Description") {
      list.description = text();
    } else if (key == "Version") {
      list.version = text();
    } else if (key == "WabbajackVersion") {
      list.wabbajackVersion = text();
    } else if (key == "GameType") {
      list.game = text();
    } else if (key == "Website") {
      list.website = text();
    } else if (key == "Readme") {
      list.readme = text();
    } else if (key == "Community") {
      list.community = text();
    } else if (key == "IsNSFW") {
      list.nsfw = v.is_bool() && v.get_bool();
    }
  };

  handler->onArchive = [&](json::value&& v) {
    if (!v.is_object()) {
      return;
    }
    Archive a = toArchive(v.get_object());
    if (a.source == Source::Unknown) {
      unknown.insert(a.sourceType.isEmpty() ? QStringLiteral("?") : a.sourceType);
    }
    list.archiveByHash.insert(a.hash, int(list.archives.size()));
    list.archives.push_back(std::move(a));
  };

  handler->onDirective = [&](json::value&& v) {
    if (!v.is_object()) {
      return;
    }
    const json::object& o = v.get_object();
    Directive d           = toDirective(o);
    if (d.type == DirectiveType::Unknown) {
      const QString type = baseType(str(o, "$type"));
      unknown.insert(type.isEmpty() ? QStringLiteral("?") : type);
    }

    ++list.directiveCount;
    TypeCount& c = list.byType[int(d.type)];
    ++c.count;
    c.size += d.size;

    if (d.archiveHash != 0) {
      uses.push_back({d.archiveHash, d.size});
      if (d.to.compare(u"ModOrganizer.exe", Qt::CaseInsensitive) == 0) {
        modOrganizerArchives.insert(d.archiveHash);
      }
    }

    QString second;
    const QString first = firstPart(d.to, &second);
    // profiles\<name>\<file>: a file straight in profiles\ names no profile
    if (first == u"profiles" && !second.isEmpty() && d.to.count(u'\\') >= 2) {
      profiles.insert(second);
    } else if (first == u"plugins" && !second.isEmpty()) {
      plugins.push_back({d.archiveHash, second});
    }

    // what ends on disk: not the .meta files (they go with the downloads)
    if (d.type != DirectiveType::ArchiveMeta) {
      list.installSize += d.size;
    }

    if (keepDirectives) {
      list.directives.push_back(std::move(d));
    }
  };

  QFile f(listFile);
  if (!f.open(QIODevice::ReadOnly)) {
    *error = QCoreApplication::translate("Wabbajack", "its modlist couldn't be read");
    return std::nullopt;
  }

  QByteArray chunk(4 * 1024 * 1024, Qt::Uninitialized);
  bool first = true;
  json::error_code ec;
  for (;;) {
    if (cancel != nullptr && cancel->load()) {
      *error = QStringLiteral("cancelled");
      return std::nullopt;
    }
    const qint64 n = f.read(chunk.data(), chunk.size());
    if (n < 0) {
      *error = QCoreApplication::translate("Wabbajack", "its modlist couldn't be read");
      return std::nullopt;
    }
    const char* data = chunk.constData();
    std::size_t size = std::size_t(n);
    if (first) {
      first = false;
      if (size >= 3 && std::memcmp(data, "\xEF\xBB\xBF", 3) == 0) {
        data += 3;
        size -= 3;
      }
    }
    const bool more = n > 0;
    parser.write_some(more, data, size, ec);
    if (ec) {
      *error = QCoreApplication::translate("Wabbajack", "its modlist is not valid JSON (%1)")
                   .arg(QString::fromStdString(ec.message()));
      return std::nullopt;
    }
    if (!more) {
      break;
    }
  }
  f.close();

  if (imageFile.isEmpty() == false) {
    QFile img(imageFile);
    if (img.open(QIODevice::ReadOnly) && img.size() < 32 * 1024 * 1024) {
      list.image = img.readAll();
    }
  }

  // Mod Organizer's own archives, and what the directives take from each
  for (const uint64_t h : modOrganizerArchives) {
    const int i = list.archiveByHash.value(h, -1);
    if (i >= 0) {
      list.archives[std::size_t(i)].isModOrganizer = true;
    }
  }
  for (const Use& u : uses) {
    const int i = list.archiveByHash.value(u.archive, -1);
    if (i >= 0) {
      ++list.archives[std::size_t(i)].uses;
    }
    if (modOrganizerArchives.contains(u.archive)) {
      ++list.modOrganizerFiles.count;
      list.modOrganizerFiles.size += u.size;
      list.installSize -= u.size;
    }
  }
  if (keepDirectives) {
    for (Directive& d : list.directives) {
      d.modOrganizerFile = d.archiveHash != 0 && modOrganizerArchives.contains(d.archiveHash);
    }
  }

  // the files that go into BSAs are written once, as the BSA, and the BSA's own
  // size is 0 in the list: they stay counted, the BSA is not counted again
  QSet<QString> pluginNames;
  for (const PluginUse& p : plugins) {
    if (!modOrganizerArchives.contains(p.archive)) {
      pluginNames.insert(p.name);
    }
  }
  list.extraPlugins = pluginNames.values();
  list.extraPlugins.sort(Qt::CaseInsensitive);
  list.profiles = profiles.values();
  list.profiles.sort(Qt::CaseInsensitive);
  list.unknownTypes = unknown.values();
  list.unknownTypes.sort();

  log::debug("wabbajack: read '{}': {} archives, {} directives", list.name,
             list.archives.size(), list.directiveCount);
  return list;
}

// ---- games ----------------------------------------------------------------

namespace
{

struct GameRow
{
  const char16_t* wabbajack;
  const char16_t* nexus;
  const char16_t* mo2;
};

// from Wabbajack's GameRegistry
constexpr GameRow Games[] = {
    {u"Morrowind", u"morrowind", u"Morrowind"},
    {u"Oblivion", u"oblivion", u"Oblivion"},
    {u"Fallout3", u"fallout3", u"Fallout 3"},
    {u"FalloutNewVegas", u"newvegas", u"New Vegas"},
    {u"Skyrim", u"skyrim", u"Skyrim"},
    {u"SkyrimSpecialEdition", u"skyrimspecialedition", u"Skyrim Special Edition"},
    {u"Fallout4", u"fallout4", u"Fallout 4"},
    {u"SkyrimVR", u"skyrimspecialedition", u"Skyrim VR"},
    {u"Enderal", u"enderal", u"Enderal"},
    {u"EnderalSpecialEdition", u"enderalspecialedition", u"Enderal Special Edition"},
    {u"Fallout4VR", u"fallout4", u"Fallout 4 VR"},
    {u"DarkestDungeon", u"darkestdungeon", u"Darkest Dungeon"},
    {u"Dishonored", u"dishonored", u"Dishonored"},
    {u"Witcher", u"witcher", u"The Witcher: Enhanced Edition"},
    {u"Witcher3", u"witcher3", u"The Witcher 3: Wild Hunt"},
    {u"StardewValley", u"stardewvalley", u"Stardew Valley"},
    {u"KingdomComeDeliverance", u"kingdomcomedeliverance", u"Kingdom Come: Deliverance"},
    {u"MechWarrior5Mercenaries", u"mechwarrior5mercenaries", u"Mechwarrior 5: Mercenaries"},
    {u"NoMansSky", u"nomanssky", u"No Man's Sky"},
    {u"DragonAgeOrigins", u"dragonage", u"Dragon Age: Origins"},
    {u"DragonAge2", u"dragonage2", u"Dragon Age 2"},
    {u"DragonAgeInquisition", u"dragonageinquisition", u"Dragon Age: Inquisition"},
    {u"KerbalSpaceProgram", u"kerbalspaceprogram", u"Kerbal Space Program"},
    {u"Terraria", u"", u"Terraria"},
    {u"Cyberpunk2077", u"cyberpunk2077", u"Cyberpunk 2077"},
    {u"Sims4", u"thesims4", u"The Sims 4"},
    {u"DragonsDogma", u"dragonsdogma", u"Dragon's Dogma: Dark Arisen"},
    {u"KarrynsPrison", u"", u"Karryn's Prison"},
    {u"Valheim", u"valheim", u"Valheim"},
    {u"MountAndBlade2Bannerlord", u"mountandblade2bannerlord", u"Mount & Blade II: Bannerlord"},
    {u"FinalFantasy7Remake", u"finalfantasy7remake", u"FINAL FANTASY VII REMAKE INTERGRADE"},
    {u"BaldursGate3", u"baldursgate3", u"Baldur's Gate 3"},
    {u"Starfield", u"starfield", u"Starfield"},
    {u"Stalker2", u"stalker2heartofchornobyl", u"S.T.A.L.K.E.R. 2"},
    {u"SevenDaysToDie", u"7daystodie", u"7 Days to Die"},
    {u"OblivionRemastered", u"oblivionremastered", u"Oblivion Remastered"},
    {u"Fallout76", u"fallout76", u"Fallout 76"},
    {u"Fallout4London", u"fallout4london", u"Fallout 4 London"},
    {u"Warhammer40kDarktide", u"warhammer40kdarktide", u"Warhammer 40,000: Darktide"},
    {u"Kotor2", u"kotor2", u"STAR WARS Knights of the Old Republic II The Sith Lords"},
    {u"VtMB", u"vampirebloodlines", u"Vampire - The Masquerade: Bloodlines"},
    {u"VtMB2", u"vtmbloodlines2", u"Vampire: The Masquerade - Bloodlines 2"},
    {u"AliceMadnessReturns", u"alicemadnessreturns", u"Alice: Madness Returns"},
    {u"AmericanMcGeesAlice", u"americanmcgeesalice", u"American McGee's Alice"},
    {u"KingdomComeDeliverance2", u"kingdomcomedeliverance2", u"Kingdom Come: Deliverance II"},
    {u"DragonsDogma2", u"dragonsdogma2", u"Dragon's Dogma 2"},
    {u"NieRAutomata", u"nierautomata", u"NieR:Automata"},
    {u"DuneAwakening", u"duneawakening", u"Dune: Awakening"},
    {u"ModdingTools", u"site", u"Modding Tools"},
};

}  // namespace

std::optional<GameInfo> gameInfo(const QString& wabbajackName)
{
  for (const GameRow& g : Games) {
    if (wabbajackName.compare(QString(g.wabbajack), Qt::CaseInsensitive) == 0) {
      return GameInfo{QString(g.wabbajack), QString(g.nexus), QString(g.mo2)};
    }
  }
  return std::nullopt;
}

IPluginGame* findGamePlugin(const QString& wabbajackName,
                            const std::vector<IPluginGame*>& plugins)
{
  const auto info = gameInfo(wabbajackName);
  if (!info) {
    return nullptr;
  }
  // by the name MO2's plugins give the game: VR and the flat game share their
  // Nexus site, so that comes second
  for (IPluginGame* g : plugins) {
    if (g->gameName().compare(info->mo2Name, Qt::CaseInsensitive) == 0) {
      return g;
    }
  }
  const bool vr = info->wabbajackName.endsWith(u"VR");
  for (IPluginGame* g : plugins) {
    if (!info->nexusDomain.isEmpty() &&
        g->gameNexusName().compare(info->nexusDomain, Qt::CaseInsensitive) == 0 &&
        g->gameName().contains(u"VR") == vr) {
      return g;
    }
  }
  return nullptr;
}

}  // namespace Wabbajack
