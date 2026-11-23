#include "wabbajackfiles.h"

#include "collectionfiles.h"
#include "wabbajacklist.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QSaveFile>
#include <QStringList>

namespace Wabbajack
{

namespace
{

constexpr char StateName[] = ".mo2r-install";

// the installer's own context: its messages are translated once
QString tr(const char* text, const char* disambiguation = nullptr, int n = -1)
{
  return QCoreApplication::translate("Wabbajack::Installer", text, disambiguation, n);
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

}  // namespace

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

bool readMergeSources(const QByteArray& extra, std::vector<MergeSource>* sources,
                      QString* error)
{
  sources->clear();
  const QJsonArray list = QJsonDocument::fromJson(extra).object().value("Sources").toArray();
  for (const QJsonValue& v : list) {
    const QJsonObject o = v.toObject();
    MergeSource s;
    s.relativePath = o.value("RelativePath").toString().replace(u'/', u'\\');
    if (s.relativePath.isEmpty()) {
      sources->clear();
      *error = tr("the list doesn't say which files it merges");
      return false;
    }
    // a hash that isn't one only loses the check of that source (Wabbajack
    // doesn't check the sources before patching either)
    s.hash = hashFromBase64(o.value("Hash").toString());
    sources->push_back(std::move(s));
  }
  if (sources->empty()) {
    *error = tr("the list doesn't say which files it merges");
    return false;
  }
  return true;
}

bool buildMergedPatch(const MergeRequest& request, const MergeSourcePath& sourcePath,
                      const std::atomic_bool* cancel, QString* error)
{
  auto cancelled = [cancel] {
    return cancel != nullptr && cancel->load();
  };
  // a merged plugin of an earlier try (or a broken one) doesn't stay as if built
  QFile::remove(request.target);

  std::vector<MergeSource> sources;
  if (!readMergeSources(request.sources, &sources, error)) {
    return false;
  }

  // the sources, in the list's order: all there before anything is written
  QStringList paths;
  for (const MergeSource& s : sources) {
    QString why;
    const QString path = sourcePath(s.relativePath, &why);
    if (path.isEmpty()) {
      *error = why.isEmpty() ? tr("its source %1 leads out of the install folder")
                                   .arg(s.relativePath)
                             : why;
      return false;
    }
    if (!QFileInfo(path).isFile()) {
      *error = tr("its source %1 isn't there (it wasn't installed)").arg(s.relativePath);
      return false;
    }
    paths.append(path);
  }

  // joined end to end, each hashed on the way: when the result isn't the
  // list's, the sources that aren't are named (as Wabbajack logs them)
  QStringList different;
  {
    QFile joined(request.joinedPath);
    if (!joined.open(QIODevice::WriteOnly)) {
      *error = tr("a temporary file can't be written");
      return false;
    }
    QByteArray buffer(4 * 1024 * 1024, Qt::Uninitialized);
    for (std::size_t i = 0; i < sources.size(); ++i) {
      QFile in(paths[qsizetype(i)]);
      if (!in.open(QIODevice::ReadOnly)) {
        joined.close();
        QFile::remove(request.joinedPath);
        *error = tr("its source %1 can't be read").arg(sources[i].relativePath);
        return false;
      }
      XxHash64 h;
      for (;;) {
        if (cancelled()) {
          joined.close();
          QFile::remove(request.joinedPath);
          return false;
        }
        const qint64 n = in.read(buffer.data(), buffer.size());
        if (n < 0) {
          joined.close();
          QFile::remove(request.joinedPath);
          *error = tr("its source %1 can't be read").arg(sources[i].relativePath);
          return false;
        }
        if (n == 0) {
          break;
        }
        if (joined.write(buffer.constData(), n) != n) {
          joined.close();
          QFile::remove(request.joinedPath);
          *error = tr("a temporary file can't be written");
          return false;
        }
        h.update(buffer.constData(), std::size_t(n));
      }
      if (sources[i].hash != 0 && h.digest() != sources[i].hash) {
        different.append(sources[i].relativePath);
      }
    }
    if (!joined.flush()) {
      joined.close();
      QFile::remove(request.joinedPath);
      *error = tr("a temporary file can't be written");
      return false;
    }
  }

  auto withSources = [&](const QString& why) {
    if (different.isEmpty()) {
      return why;
    }
    return tr("%1 (sources that aren't the list's: %2)")
        .arg(why, different.join(QStringLiteral(", ")));
  };

  QString why;
  QDir().mkpath(QFileInfo(request.target).absolutePath());
  const bool patched = applyPatch(request.joinedPath, request.patchPath, request.target,
                                  request.size, &why);
  QFile::remove(request.joinedPath);
  if (!patched) {
    QFile::remove(request.target);
    *error = withSources(why);
    return false;
  }
  if (cancelled()) {
    QFile::remove(request.target);
    return false;
  }

  // the result, always: its size, then its hash
  const QFileInfo built(request.target);
  if (!built.isFile()) {
    *error = tr("it wasn't written");
    return false;
  }
  if (request.size > 0 && built.size() != request.size) {
    QFile::remove(request.target);
    *error = withSources(tr("its size isn't the list's"));
    return false;
  }
  if (request.hash != 0) {
    const auto hash = hashFile(request.target, cancel);
    if (!hash) {
      QFile::remove(request.target);
      if (!cancelled()) {
        *error = tr("it wasn't written");
      }
      return false;
    }
    if (*hash != request.hash) {
      QFile::remove(request.target);
      *error = withSources(tr("its hash isn't the list's"));
      return false;
    }
  }
  return true;
}

QString installStateFolder(const QString& installFolder)
{
  return QDir(installFolder).filePath(QString::fromLatin1(StateName));
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

}  // namespace Wabbajack
