// MO2 Revamped: tests for the Wabbajack installer's zEdit merges (MergedPatch)
// and the pieces they use (wabbajackfiles.cpp, wabbajackhash.cpp; not shipped).
// A merge is built as Wabbajack builds it: the sources joined end to end in
// the list's order, then an OctoDiff delta applied. The deltas here are written
// by a small OctoDiff writer of the test's own (Octopus Deploy's
// BinaryDeltaWriter format: "OCTODELTA", version 1, the hash algorithm's name,
// the basis' hash, ">>>", then copy and data commands), the copies found as
// OctoDiff finds them: the basis cut into fixed chunks, the new file searched
// for them.
// Build and run: run.ps1 in this folder, or from a Developer prompt with the
// Qt and uibase paths of run.ps1:
//   cl /nologo /std:c++20 /EHsc /permissive- /utf-8 /MD /DUNICODE /D_UNICODE /DNOMINMAX
//      /I<qt>\include /I<qt>\include\QtCore /I<src> /I<uibase>\src wabbajack_merge_test.cpp
//      <src>\wabbajackfiles.cpp <src>\wabbajackhash.cpp <src>\collectionfiles.cpp
//      /link <qt>\lib\Qt6Core.lib <uibase build>\uibase.lib
//   wabbajack_merge_test <work folder>
#include "wabbajackfiles.h"
#include "wabbajacklist.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QString>

#include <atomic>
#include <cstdio>
#include <cstring>
#include <utility>

using namespace Wabbajack;

namespace
{
int passed = 0;
int failed = 0;

void check(bool ok, const char* what, int line, const QString& detail = {})
{
  if (ok) {
    ++passed;
  } else {
    ++failed;
    std::printf("FAIL line %d: %s %s\n", line, what, qUtf8Printable(detail));
  }
}

#define CHECK(x) check((x), #x, __LINE__)
#define CHECK_MSG(x, detail) check((x), #x, __LINE__, (detail))

// ---- an OctoDiff writer ------------------------------------------------------

void putInt64(QByteArray& out, qint64 v)
{
  out.append(reinterpret_cast<const char*>(&v), 8);
}

// a delta from basis to target, as OctoDiff's BinaryDeltaWriter writes it.
// chunk: the signature's chunk size (OctoDiff's default is 2048; small here so
// the small files of the tests have several)
QByteArray octoDelta(const QByteArray& basis, const QByteArray& target, int chunk = 16)
{
  QByteArray out("OCTODELTA");
  out.append(char(1));  // version
  const QByteArray algorithm("SHA1");
  out.append(char(algorithm.size()));  // .NET's 7-bit length, short
  out.append(algorithm);
  const QByteArray basisHash = QCryptographicHash::hash(basis, QCryptographicHash::Sha1);
  const qint32 hashLength    = qint32(basisHash.size());
  out.append(reinterpret_cast<const char*>(&hashLength), 4);
  out.append(basisHash);
  out.append(">>>");

  // the basis' chunks by their content (the first offset of each)
  QHash<QByteArray, qint64> chunks;
  for (qint64 at = 0; at + chunk <= basis.size(); at += chunk) {
    const QByteArray c = basis.mid(at, chunk);
    if (!chunks.contains(c)) {
      chunks.insert(c, at);
    }
  }

  QByteArray pending;  // new data not written yet
  qint64 copyStart = -1, copyLength = 0;
  auto flushData = [&] {
    if (!pending.isEmpty()) {
      out.append(char(0x80));
      putInt64(out, pending.size());
      out.append(pending);
      pending.clear();
    }
  };
  auto flushCopy = [&] {
    if (copyLength > 0) {
      out.append(char(0x60));
      putInt64(out, copyStart);
      putInt64(out, copyLength);
      copyStart  = -1;
      copyLength = 0;
    }
  };
  qint64 i = 0;
  while (i < target.size()) {
    const auto it = i + chunk <= target.size() ? chunks.constFind(target.mid(i, chunk))
                                                : chunks.constEnd();
    if (it != chunks.constEnd()) {
      flushData();
      if (copyLength > 0 && copyStart + copyLength == it.value()) {
        copyLength += chunk;  // the next chunk of the same run
      } else {
        flushCopy();
        copyStart  = it.value();
        copyLength = chunk;
      }
      i += chunk;
    } else {
      flushCopy();
      pending.append(target.at(i));
      ++i;
    }
  }
  flushCopy();
  flushData();
  return out;
}

// ---- the files of a test -------------------------------------------------------

bool write(const QString& path, const QByteArray& data)
{
  QDir().mkpath(QFileInfo(path).absolutePath());
  QFile f(path);
  return f.open(QIODevice::WriteOnly) && f.write(data) == data.size();
}

QByteArray read(const QString& path)
{
  QFile f(path);
  return f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray();
}

QByteArray sourcesJson(const QList<QPair<QString, QByteArray>>& sources)
{
  QJsonArray list;
  for (const auto& [path, content] : sources) {
    QJsonObject o;
    o.insert("RelativePath", path);
    o.insert("Hash", hashToBase64(XxHash64::of(content.constData(), std::size_t(content.size()))));
    list.append(o);
  }
  QJsonObject root;
  root.insert("Sources", list);
  return QJsonDocument(root).toJson(QJsonDocument::Compact);
}

QByteArray plugin(const char* name, int records)
{
  // something like a plugin: a header and records, different per plugin
  QByteArray data("TES4");
  data.append(QByteArray(20, '\0'));
  for (int r = 0; r < records; ++r) {
    data.append(QStringLiteral("RECORD %1 of %2 with some data;").arg(r).arg(name).toUtf8());
  }
  return data;
}

struct Fixture
{
  QString install;
  QString patch;
  QString joined;
  QString target;
  QByteArray a, b, c;  // the sources' contents
  QByteArray merged;   // what the merge must be

  // the list's sources in the list's order: a, b, c
  QList<QPair<QString, QByteArray>> sources() const
  {
    return {{QStringLiteral("mods\\Mod A\\A.esp"), a},
            {QStringLiteral("mods\\Mod B\\B.esp"), b},
            {QStringLiteral("mods\\Mod C\\C.esm"), c}};
  }

  MergeRequest request() const
  {
    MergeRequest r;
    r.sources    = sourcesJson(sources());
    r.patchPath  = patch;
    r.joinedPath = joined;
    r.target     = target;
    r.hash       = XxHash64::of(merged.constData(), std::size_t(merged.size()));
    r.size       = merged.size();
    return r;
  }
};

Fixture makeFixture(const QString& work, const char* name)
{
  Fixture f;
  f.install = QDir(work).filePath(QString::fromLatin1(name));
  QDir(f.install).removeRecursively();
  QDir().mkpath(f.install);
  const QString state = installStateFolder(f.install);
  f.patch             = QDir(state).filePath("inline/patch-id");
  f.joined            = QDir(state).filePath("stage/1");
  QDir().mkpath(QDir(state).filePath("stage"));
  f.target = installPath(f.install, QStringLiteral("mods\\zMerge\\zMerge.esp"));

  f.a = plugin("A", 40);
  f.b = plugin("B", 25);
  f.c = plugin("C", 10);
  // the merge: most of A and B, a little of C, new records zEdit wrote
  f.merged = QByteArray("TES4") + QByteArray(20, '\x01') + f.a.mid(24, 600) +
             QByteArray("NEW RECORD written by the merge;") + f.b.mid(24) +
             f.c.mid(100, 64) + QByteArray("THE END");

  for (const auto& [path, content] : f.sources()) {
    write(installPath(f.install, path), content);
  }
  write(f.patch, octoDelta(f.a + f.b + f.c, f.merged));
  return f;
}

MergeSourcePath inInstall(const QString& install)
{
  return [install](const QString& relative, QString*) {
    return installPath(install, relative);
  };
}

// ---- tests ---------------------------------------------------------------------

void testHash()
{
  // xxHash64's own vectors (seed 0), and Wabbajack's Base64 of it
  CHECK(XxHash64::of("", 0) == 0xEF46DB3751D8E999ULL);
  CHECK(XxHash64::of("abc", 3) == 0x44BC2CF5AD770999ULL);
  bool ok = false;
  CHECK(hashFromBase64(hashToBase64(0x0123456789ABCDEFULL), &ok) == 0x0123456789ABCDEFULL);
  CHECK(ok);
}

void testPatchAlone(const QString& work)
{
  // the writer and the applier agree, whole file and with nothing in common
  const QString dir = QDir(work).filePath("patch");
  QDir(dir).removeRecursively();
  QDir().mkpath(dir);
  const QByteArray basis  = plugin("basis", 30);
  const QByteArray target = basis.mid(50, 300) + "inserted" + basis.left(48) + basis.mid(400);
  write(dir + "/basis", basis);
  write(dir + "/delta", octoDelta(basis, target));
  QString why;
  CHECK_MSG(applyPatch(dir + "/basis", dir + "/delta", dir + "/out", target.size(), &why), why);
  CHECK(read(dir + "/out") == target);

  write(dir + "/delta2", octoDelta(QByteArray(), "only new data"));
  CHECK(applyPatch(dir + "/basis", dir + "/delta2", dir + "/out2", 0, &why));
  CHECK(read(dir + "/out2") == "only new data");

  // a damaged delta: refused, nothing written
  QByteArray bad = octoDelta(basis, target);
  bad[bad.size() - 9] = char(0x42);
  write(dir + "/bad", bad.left(bad.size() - 8));
  QFile::remove(dir + "/out3");
  CHECK(!applyPatch(dir + "/basis", dir + "/bad", dir + "/out3", 0, &why));
  CHECK(!QFileInfo::exists(dir + "/out3"));

  // a copy out of the basis
  QByteArray outside("OCTODELTA");
  outside.append(char(1)).append(char(4)).append("SHA1");
  const qint32 zero = 0;
  outside.append(reinterpret_cast<const char*>(&zero), 4).append(">>>").append(char(0x60));
  putInt64(outside, basis.size() - 4);
  putInt64(outside, 100);
  write(dir + "/outside", outside);
  CHECK(!applyPatch(dir + "/basis", dir + "/outside", dir + "/out4", 0, &why));
}

void testMerge(const QString& work)
{
  Fixture f = makeFixture(work, "merge");
  QString why;
  CHECK_MSG(buildMergedPatch(f.request(), inInstall(f.install), nullptr, &why), why);
  CHECK(read(f.target) == f.merged);
  CHECK(XxHash64::of(f.merged.constData(), std::size_t(f.merged.size())) ==
        *hashFile(f.target));
  // the joined sources are a temporary file
  CHECK(!QFileInfo::exists(f.joined));

  // a second install over the first (going on after a stop): built again, the
  // same; a broken file left at the target doesn't stay
  write(f.target, "a half-written merge from before");
  CHECK_MSG(buildMergedPatch(f.request(), inInstall(f.install), nullptr, &why), why);
  CHECK(read(f.target) == f.merged);

  // forward slashes in the list's paths are the same paths
  MergeRequest slashes = f.request();
  slashes.sources      = QByteArray(slashes.sources).replace("\\\\", "/");
  CHECK_MSG(buildMergedPatch(slashes, inInstall(f.install), nullptr, &why), why);
  CHECK(read(f.target) == f.merged);
}

void testOrder(const QString& work)
{
  // the list's order is the order joined: the same sources in another order
  // don't make the merge
  Fixture f       = makeFixture(work, "order");
  MergeRequest r  = f.request();
  auto reversed   = f.sources();
  std::swap(reversed[0], reversed[2]);
  r.sources = sourcesJson(reversed);
  QString why;
  CHECK(!buildMergedPatch(r, inInstall(f.install), nullptr, &why));
  CHECK(!QFileInfo::exists(f.target));
  CHECK_MSG(!why.isEmpty(), why);
}

void testMissingSource(const QString& work)
{
  Fixture f = makeFixture(work, "missing");
  QFile::remove(installPath(f.install, QStringLiteral("mods\\Mod B\\B.esp")));
  QString why;
  CHECK(!buildMergedPatch(f.request(), inInstall(f.install), nullptr, &why));
  CHECK_MSG(why.contains(QStringLiteral("mods\\Mod B\\B.esp")), why);
  CHECK_MSG(why.contains(QStringLiteral("isn't there")), why);
  CHECK(!QFileInfo::exists(f.target));
  CHECK(!QFileInfo::exists(f.joined));
}

void testWrongHash(const QString& work)
{
  // the list's hash of the merge isn't what the patch makes: nothing stays
  Fixture f      = makeFixture(work, "wronghash");
  MergeRequest r = f.request();
  r.hash ^= 1;
  QString why;
  CHECK(!buildMergedPatch(r, inInstall(f.install), nullptr, &why));
  CHECK_MSG(why.contains(QStringLiteral("hash")), why);
  CHECK(!QFileInfo::exists(f.target));

  // the list's size isn't the merge's
  r      = f.request();
  r.size = f.merged.size() + 1;
  CHECK(!buildMergedPatch(r, inInstall(f.install), nullptr, &why));
  CHECK_MSG(why.contains(QStringLiteral("size")), why);
  CHECK(!QFileInfo::exists(f.target));

  // a source that isn't the list's (another version of the mod): the merge
  // fails and the message names that source
  write(installPath(f.install, QStringLiteral("mods\\Mod A\\A.esp")), plugin("other A", 40));
  CHECK(!buildMergedPatch(f.request(), inInstall(f.install), nullptr, &why));
  CHECK_MSG(why.contains(QStringLiteral("mods\\Mod A\\A.esp")), why);
  CHECK_MSG(!why.contains(QStringLiteral("mods\\Mod B\\B.esp")), why);
  CHECK(!QFileInfo::exists(f.target));
}

void testOutside(const QString& work)
{
  Fixture f = makeFixture(work, "outside");
  // a file next to the install folder, which a source mustn't reach
  write(QDir(work).filePath("secret.esp"), f.a);

  const char* bad[] = {"..\\secret.esp",
                       "mods\\..\\..\\secret.esp",
                       "C:\\Windows\\win.ini",
                       "\\\\server\\share\\a.esp",
                       "/secret.esp",
                       ".mo2r-install\\inline\\patch-id",
                       "mods\\NUL.esp"};
  for (const char* path : bad) {
    MergeRequest r = f.request();
    QJsonObject root = QJsonDocument::fromJson(r.sources).object();
    QJsonArray list  = root.value("Sources").toArray();
    QJsonObject first = list.at(0).toObject();
    first.insert("RelativePath", QString::fromLatin1(path));
    list.replace(0, first);
    root.insert("Sources", list);
    r.sources = QJsonDocument(root).toJson(QJsonDocument::Compact);
    QString why;
    CHECK_MSG(!buildMergedPatch(r, inInstall(f.install), nullptr, &why), QString::fromLatin1(path));
    CHECK_MSG(why.contains(QStringLiteral("leads out of the install folder")), why);
    CHECK(!QFileInfo::exists(f.target));
  }

  // a resolver's own reason is kept (Mod Organizer's own files, say)
  QString why;
  auto refuse = [](const QString& relative, QString* reason) {
    *reason = QStringLiteral("refused %1").arg(relative);
    return QString();
  };
  CHECK(!buildMergedPatch(f.request(), refuse, nullptr, &why));
  CHECK_MSG(why == QStringLiteral("refused mods\\Mod A\\A.esp"), why);

  // the merge's own place: the installer's paths refuse leading out, too
  CHECK(installPath(f.install, QStringLiteral("..\\zMerge.esp")).isEmpty());
  CHECK(installPath(f.install, QStringLiteral("D:\\zMerge.esp")).isEmpty());
  CHECK(installPath(f.install, QStringLiteral(".mo2r-install\\zMerge.esp")).isEmpty());
  CHECK(!installPath(f.install, QStringLiteral("mods\\zMerge\\zMerge.esp")).isEmpty());
}

void testListData(const QString& work)
{
  Fixture f = makeFixture(work, "listdata");
  QString why;
  std::vector<MergeSource> sources;

  CHECK(readMergeSources(f.request().sources, &sources, &why));
  CHECK(sources.size() == 3);
  CHECK(sources.size() == 3 && sources[1].relativePath == QStringLiteral("mods\\Mod B\\B.esp"));
  CHECK(sources.size() == 3 &&
        sources[1].hash == XxHash64::of(f.b.constData(), std::size_t(f.b.size())));

  // no sources, or one without a path: refused
  CHECK(!readMergeSources(QByteArray(R"({"Sources":[]})"), &sources, &why));
  CHECK(!readMergeSources(QByteArray(), &sources, &why));
  CHECK(!readMergeSources(QByteArray(R"({"Sources":[{"Hash":"AAAAAAAAAAA="}]})"), &sources,
                          &why));
  MergeRequest r = f.request();
  r.sources      = QByteArray(R"({"Sources":[]})");
  CHECK(!buildMergedPatch(r, inInstall(f.install), nullptr, &why));
  CHECK(!QFileInfo::exists(f.target));

  // a source without a hash is joined all the same
  r         = f.request();
  r.sources = QByteArray(R"({"Sources":[{"RelativePath":"mods\\Mod A\\A.esp"},)"
                         R"({"RelativePath":"mods\\Mod B\\B.esp","Hash":null},)"
                         R"({"RelativePath":"mods\\Mod C\\C.esm","Hash":"not a hash"}]})");
  CHECK_MSG(buildMergedPatch(r, inInstall(f.install), nullptr, &why), why);
  CHECK(read(f.target) == f.merged);

  // the list's patch isn't there
  r           = f.request();
  r.patchPath = QDir(f.install).filePath("no-such-patch");
  CHECK(!buildMergedPatch(r, inInstall(f.install), nullptr, &why));
  CHECK(!QFileInfo::exists(f.target));

  // an old list's BSDiff isn't an OctoDiff: still a patch format it knows; a
  // file that is neither is refused
  write(f.patch, "NOTAPATCH at all");
  CHECK(!buildMergedPatch(f.request(), inInstall(f.install), nullptr, &why));
  CHECK_MSG(why.contains(QStringLiteral("format")), why);
}

void testCancel(const QString& work)
{
  Fixture f = makeFixture(work, "cancel");
  std::atomic_bool cancel{true};
  QString why;
  CHECK(!buildMergedPatch(f.request(), inInstall(f.install), &cancel, &why));
  CHECK(!QFileInfo::exists(f.target));
  CHECK(!QFileInfo::exists(f.joined));
}

}  // namespace

int main(int argc, char** argv)
{
  const QString work = argc > 1 ? QString::fromLocal8Bit(argv[1])
                                : QDir::temp().filePath(QStringLiteral("mo2r-wabbajack-merge"));
  QDir(work).removeRecursively();
  QDir().mkpath(work);

  testHash();
  testPatchAlone(work);
  testMerge(work);
  testOrder(work);
  testMissingSource(work);
  testWrongHash(work);
  testOutside(work);
  testListData(work);
  testCancel(work);

  std::printf("wabbajack_merge_test: %d passed, %d failed\n", passed, failed);
  return failed == 0 ? 0 : 1;
}
