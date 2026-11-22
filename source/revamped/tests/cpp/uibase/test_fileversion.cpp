// MO2 Revamped: tests for uibase's getFileVersion() and getProductVersion()
// (utility.cpp; not shipped) on crafted and truncated PE files.
// The files are copies of Windows' version.dll with their version resource
// damaged in place: block lengths, the fixed info, the translation and the
// ProductVersion string; and cut at many lengths. Each call must return a
// version or "", and never crash or read past what it was given.
// Build and run: powershell -File run.ps1 -Only fileversion   (from this folder)
#include "test_common.h"

#include <utility.h>

#include <QFile>
#include <QRegularExpression>

#include <format>
#include <vector>

using namespace MOBase;

static QByteArray readAll(const QString& path)
{
  QFile f(path);
  return f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray();
}

static bool writeAll(const QString& path, const QByteArray& data)
{
  QFile f(path);
  return f.open(QIODevice::WriteOnly) && f.write(data) == data.size();
}

// offset of a UTF-16 key in the file, -1 if missing
static qsizetype findKey(const QByteArray& data, const char16_t* key, qsizetype from = 0)
{
  const QByteArray needle(reinterpret_cast<const char*>(key),
                          static_cast<qsizetype>(std::char_traits<char16_t>::length(key) * 2));
  return data.indexOf(needle, from);
}

static void putWord(QByteArray& data, qsizetype at, quint16 v)
{
  data[at]     = static_cast<char>(v & 0xFF);
  data[at + 1] = static_cast<char>(v >> 8);
}

static quint16 getWord(const QByteArray& data, qsizetype at)
{
  return static_cast<quint16>(static_cast<quint8>(data[at]) |
                              (static_cast<quint8>(data[at + 1]) << 8));
}

// what the version API itself answers for a file, for the expected values
struct Expected
{
  QString file, product;
};

static Expected query(const QString& path)
{
  Expected e;
  const std::wstring w = QDir::toNativeSeparators(path).toStdWString();
  DWORD h       = 0;
  const DWORD n = GetFileVersionInfoSizeW(w.c_str(), &h);
  if (n == 0) {
    return e;
  }
  std::vector<char> buf(n);
  if (!GetFileVersionInfoW(w.c_str(), 0, n, buf.data())) {
    return e;
  }
  VS_FIXEDFILEINFO* fi = nullptr;
  UINT len             = 0;
  if (VerQueryValueW(buf.data(), L"\\", reinterpret_cast<void**>(&fi), &len) && fi &&
      len >= sizeof(*fi)) {
    e.file = QString("%1.%2.%3.%4")
                 .arg(HIWORD(fi->dwFileVersionMS))
                 .arg(LOWORD(fi->dwFileVersionMS))
                 .arg(HIWORD(fi->dwFileVersionLS))
                 .arg(LOWORD(fi->dwFileVersionLS));
  }
  WORD* tr = nullptr;
  if (VerQueryValueW(buf.data(), L"\\VarFileInfo\\Translation", reinterpret_cast<void**>(&tr),
                     &len) &&
      tr && len >= 4) {
    const auto q =
        std::format(L"\\StringFileInfo\\{:04x}{:04x}\\ProductVersion", tr[0], tr[1]);
    wchar_t* s = nullptr;
    if (VerQueryValueW(buf.data(), q.c_str(), reinterpret_cast<void**>(&s), &len) && s &&
        len > 0) {
      // where the answer ends, against the buffer the API was given
      const char* end = reinterpret_cast<const char*>(s) + len * sizeof(wchar_t);
      if (end > buf.data() + buf.size()) {
        std::printf("  note: VerQueryValue's ProductVersion runs %lld bytes past the buffer\n",
                    static_cast<long long>(end - (buf.data() + buf.size())));
      }
      e.product = QString::fromWCharArray(s, static_cast<qsizetype>(wcsnlen(s, len)));
    }
  }
  return e;
}

static int crafted = 0;

// writes a damaged copy and checks both calls survive it; returns what they gave
static Expected tryCopy(const QString& dir, const QByteArray& data)
{
  const QString path = dir + QString("/crafted%1.dll").arg(crafted++);
  writeAll(path, data);
  Expected got;
  got.file    = getFileVersion(path);
  got.product = getProductVersion(path);
  // never anything but "" or a version
  static const QRegularExpression dotted("^(\\d+\\.\\d+\\.\\d+\\.\\d+)?$");
  CHECK(dotted.match(got.file).hasMatch());
  return got;
}

int main(int argc, char** argv)
{
  uitest::redirect(argc, argv);

  const QString dir  = uitest::scratchDir("fileversion");
  const QString good = dir + "/good.dll";
  const QByteArray original = readAll(QDir::fromNativeSeparators(
      qEnvironmentVariable("SystemRoot", "C:\\Windows") + "\\System32\\version.dll"));
  CHECK(!original.isEmpty());
  CHECK(writeAll(good, original));

  // the good copy
  const Expected want = query(good);
  CHECK(!want.file.isEmpty());
  CHECK(!want.product.isEmpty());
  CHECK_STR(getFileVersion(good), want.file);
  CHECK_STR(getProductVersion(good), want.product);

  // path spellings: native separators, a relative path, an existing \\?\ prefix
  CHECK_STR(getFileVersion(QDir::toNativeSeparators(good)), want.file);
  CHECK_STR(getFileVersion(QDir::current().relativeFilePath(good)), want.file);
  CHECK_STR(getFileVersion("\\\\?\\" + QDir::toNativeSeparators(good)), want.file);
  CHECK_STR(getProductVersion("\\\\?\\" + QDir::toNativeSeparators(good)), want.product);

  // on a network share: \\localhost\C$\...
  const QString native = QDir::toNativeSeparators(good);
  if (native.size() > 2 && native[1] == ':') {
    const QString unc = QString("\\\\localhost\\%1$%2").arg(native[0]).arg(native.mid(2));
    if (QFileInfo::exists(unc)) {
      CHECK_STR(getFileVersion(unc), want.file);
      CHECK_STR(getProductVersion(unc), want.product);
      CHECK_STR(getFileVersion(QDir::fromNativeSeparators(unc)), want.file);
    } else {
      std::printf("SKIP: %s isn't reachable\n", unc.toUtf8().constData());
    }
  }

  // not a file, nothing there, not a PE
  CHECK_STR(getFileVersion(dir + "/missing.dll"), "");
  CHECK_STR(getProductVersion(dir + "/missing.dll"), "");
  CHECK_STR(getFileVersion(dir), "");
  CHECK_STR(getProductVersion(""), "");
  CHECK(writeAll(dir + "/text.dll", "MZ this is not a program"));
  CHECK_STR(getFileVersion(dir + "/text.dll"), "");
  CHECK_STR(getProductVersion(dir + "/text.dll"), "");
  CHECK(writeAll(dir + "/empty.dll", ""));
  CHECK_STR(getFileVersion(dir + "/empty.dll"), "");

  // cut at many lengths
  for (qsizetype n : {qsizetype(1), qsizetype(2), qsizetype(64), qsizetype(0x3c),
                      qsizetype(0x40), qsizetype(0x100), qsizetype(0x200), qsizetype(0x400),
                      qsizetype(0x1000)}) {
    tryCopy(dir, original.left(n));
  }
  for (qsizetype n = original.size() / 2; n < original.size(); n += original.size() / 40) {
    tryCopy(dir, original.left(n));
  }

  // the version resource, located by its key
  const qsizetype root = findKey(original, u"VS_VERSION_INFO");
  CHECK(root > 6);
  if (root <= 6) {
    return uitest::finish("test_fileversion");
  }
  const qsizetype rootStart = root - 6;  // wLength, wValueLength, wType, then the key

  // the resource cut just after its key or in the middle of the fixed info
  tryCopy(dir, original.left(root + 32));
  tryCopy(dir, original.left(root + 60));

  // no fixed info: wValueLength 0
  {
    QByteArray d = original;
    putWord(d, rootStart + 2, 0);
    const Expected got = tryCopy(dir, d);
    CHECK_STR(got.file, "");
  }
  // a short fixed info: wValueLength 8
  {
    QByteArray d = original;
    putWord(d, rootStart + 2, 8);
    const Expected got = tryCopy(dir, d);
    CHECK_STR(got.file, "");
  }
  // the fixed info's signature broken
  {
    QByteArray d       = original;
    const qsizetype fi = d.indexOf(QByteArray("\xBD\x04\xEF\xFE", 4), root);
    CHECK(fi > root);
    d[fi] = 0;
    tryCopy(dir, d);
  }
  // the root's length: 0, tiny, and far past the resource
  for (quint16 len : {quint16(0), quint16(6), quint16(40), quint16(0xFFFF)}) {
    QByteArray d = original;
    putWord(d, rootStart, len);
    tryCopy(dir, d);
  }

  // no translation (its key renamed): no product version
  const qsizetype trans = findKey(original, u"Translation", root);
  CHECK(trans > root);
  {
    QByteArray d = original;
    d[trans + 2] = 'X';
    const Expected got = tryCopy(dir, d);
    CHECK_STR(got.product, "");
    CHECK_STR(got.file, want.file);
  }
  // a translation shorter than its two WORDs
  for (quint16 len : {quint16(0), quint16(2), quint16(3)}) {
    QByteArray d = original;
    putWord(d, trans - 6 + 2, len);
    const Expected got = tryCopy(dir, d);
    CHECK_STR(got.product, "");
  }
  // a translation naming a language that isn't there
  {
    QByteArray d       = original;
    // the block's 6-byte header, "Translation\0" (24 bytes), padding to 4: the value
    const qsizetype at = trans + 26;
    putWord(d, at, 0x1234);
    const Expected got = tryCopy(dir, d);
    CHECK_STR(got.product, "");
  }

  // the ProductVersion string damaged
  const qsizetype pv = findKey(original, u"ProductVersion", root);
  CHECK(pv > root);
  const qsizetype pvStart = pv - 6;
  const quint16 pvValueLength = getWord(original, pvStart + 2);
  CHECK(pvValueLength > 1);
  {
    // no value
    QByteArray d = original;
    putWord(d, pvStart + 2, 0);
    const Expected got = tryCopy(dir, d);
    CHECK_STR(got.product, "");
  }
  {
    // its terminator gone, the length past the resource: bounded, no crash
    QByteArray d = original;
    putWord(d, pvStart + 2, 0x7FFF);
    putWord(d, pvStart, 0xFFFF);
    // the 6-byte header and "ProductVersion\0" (30 bytes) end aligned: the value
    const qsizetype value = pv + 30;
    for (qsizetype i = value; i < value + pvValueLength * 2 + 8 && i + 1 < d.size(); i += 2) {
      if (getWord(d, i) == 0) {
        putWord(d, i, 'x');
      }
    }
    tryCopy(dir, d);
    // says whether the API's answer itself runs past its buffer
    query(dir + QString("/crafted%1.dll").arg(crafted - 1));
  }
  {
    // a length of 1: only the terminator, or one character
    QByteArray d = original;
    putWord(d, pvStart + 2, 1);
    const Expected got = tryCopy(dir, d);
    CHECK(got.product.size() <= 1);
  }
  {
    // the key unterminated into its value
    QByteArray d = original;
    putWord(d, pv + 28, 'n');
    const Expected got = tryCopy(dir, d);
    CHECK_STR(got.product, "");
  }

  // a byte flipped at every 4th position across the resource: none may crash
  const qsizetype resEnd = std::min<qsizetype>(original.size(), rootStart + getWord(original, rootStart));
  for (qsizetype i = rootStart; i < resEnd; i += 4) {
    QByteArray d = original;
    d[i]         = static_cast<char>(d[i] ^ 0xFF);
    tryCopy(dir, d);
  }

  std::printf("%d crafted files\n", crafted);
  return uitest::finish("test_fileversion");
}
