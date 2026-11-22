// MO2 Revamped: tests for uibase's utility.cpp/.h helpers (not shipped).
// Strings and joins, natural sorting, text decoding, the localised size, speed
// and time strings, Windows error messages, and the shell helpers that don't
// launch anything (Result, CreateDirectories, Rename, Delete, recursive
// deletes). Nothing here needs a QApplication, and no path used here reaches
// reportError(), which would show a message box.
// Build and run: powershell -File run.ps1 -Only utility   (from this folder)
#include "test_common.h"

#include <utility.h>

#include <QFile>
#include <QLocale>
#include <QStringConverter>
#include <QStringEncoder>

#include <algorithm>
#include <cwctype>
#include <set>
#include <vector>

using namespace MOBase;

static bool writeBytes(const QString& path, const QByteArray& data)
{
  QFile f(path);
  return f.open(QIODevice::WriteOnly) && f.write(data) == data.size();
}

static void strings()
{
  const QString text = QString::fromUtf8("caf\xC3\xA9 \xE6\x97\xA5\xE6\x9C\xAC \xF0\x9F\x8E\xAE");
  CHECK(ToQString(ToWString(text)) == text);
  CHECK(ToQString(ToString(text)) == text);
  CHECK(ToString(text) == std::string("caf\xC3\xA9 \xE6\x97\xA5\xE6\x9C\xAC \xF0\x9F\x8E\xAE"));
  CHECK(ToString(QString()).empty());
  CHECK(ToWString(QString()).empty());
  CHECK(ToQString(std::wstring(L"abc")) == "abc");
  CHECK(ToString(QString("plain"), false) == "plain");

  SYSTEMTIME st{};
  st.wYear  = 2026;
  st.wMonth = 11;
  st.wDay   = 22;
  st.wHour  = 13;
  CHECK(ToString(st).contains("2026"));

  CHECK(isOneOf(3, {1, 2, 3}));
  CHECK(!isOneOf(4, {1, 2, 3}));
}

static void joins()
{
  const std::vector<int> v{1, 2, 3};
  CHECK_STR(VectorJoin(v, ", "), "1, 2, 3");
  CHECK_STR(VectorJoin(v, ", ", 2), "1, 2, ...");
  CHECK_STR(VectorJoin(v, ", ", 3), "1, 2, 3");
  CHECK_STR(VectorJoin(std::vector<int>{}, ", "), "");

  const std::set<QString> s{"a", "b", "c"};
  CHECK_STR(SetJoin(s, ", "), "a, b, c");
  CHECK_STR(SetJoin(s, ", ", 3), "a, b, c");
  // the maximum is honoured: two items, then "..."
  CHECK_STR(SetJoin(s, ", ", 2), "a, b, ...");
  CHECK_STR(SetJoin(s, ", ", 1), "a, ...");
  CHECK_STR(SetJoin(std::set<QString>{}, ", "), "");

  // ConvertList returns the converted list
  const QVariantList variants{1, 2, 3};
  const QList<int> ints = ConvertList<int>(variants);
  CHECK(ints == QList<int>({1, 2, 3}));
  bool threw = false;
  try {
    ConvertList<QVariantMap>(QVariantList{QString("x")});
  } catch (const MOBase::Exception&) {
    threw = true;
  }
  CHECK(threw);
}

static void naturalSort()
{
  CHECK(naturalCompare("file2", "file10") < 0);
  CHECK(naturalCompare("file10", "file2") > 0);
  CHECK(naturalCompare("abc", "ABC") == 0);
  CHECK(naturalCompare("abc", "ABC", Qt::CaseSensitive) != 0);
  CHECK(naturalCompare("", "a") < 0);

  QStringList list{"Mod 10", "mod 9", "Mod 1", "a"};
  std::sort(list.begin(), list.end(), NaturalSort());
  CHECK(list == QStringList({"a", "Mod 1", "mod 9", "Mod 10"}));
}

// the encoding reported for a decoded text writes the same bytes back: that's
// what the text editor saves with
static bool roundTrips(const QByteArray& data)
{
  QString encoding;
  const QString text = decodeTextData(data, &encoding);
  const auto codec   = QStringConverter::encodingForName(encoding.toUtf8());
  if (!codec) {
    std::printf("  no codec for \"%s\"\n", encoding.toUtf8().constData());
    return false;
  }
  QStringEncoder enc(*codec);
  QByteArray back = enc.encode(text);
  if (back == data) {
    return true;
  }
  // a BOM that was there is written back by the editor's encoder
  return data.endsWith(back) && data.size() - back.size() <= 3;
}

static void decoding()
{
  QString enc;

  CHECK_STR(decodeTextData("hello", &enc), "hello");
  CHECK_STR(enc, "UTF-8");
  CHECK_STR(decodeTextData("", &enc), "");

  // UTF-8 with a BOM: the BOM isn't in the text
  CHECK_STR(decodeTextData("\xEF\xBB\xBFh\xC3\xA9", &enc), QString::fromUtf8("h\xC3\xA9"));
  CHECK_STR(enc, "UTF-8");

  // UTF-16 with BOMs
  CHECK_STR(decodeTextData(QByteArray("\xFF\xFEh\0i\0", 6), &enc), "hi");
  CHECK_STR(enc, "UTF-16LE");
  CHECK_STR(decodeTextData(QByteArray("\xFE\xFF\0h\0i", 6), &enc), "hi");
  CHECK_STR(enc, "UTF-16BE");

  // UTF-16LE without a BOM
  CHECK_STR(decodeTextData(QByteArray("[\0G\0e\0n\0e\0r\0a\0l\0]\0", 18), &enc), "[General]");
  CHECK_STR(enc, "UTF-16LE");

  // UTF-8 with a stray NUL stays UTF-8
  CHECK(decodeTextData(QByteArray("abc\0def", 7), &enc).startsWith("abc"));
  CHECK_STR(enc, "UTF-8");

  // an ANSI file (not UTF-8): the text is decoded with the system code page,
  // and the encoding reported for it must save the same bytes back, not UTF-8
  const QByteArray ansi("caf\xE9 cr\xE8me\r\n");
  const QString decoded = decodeTextData(ansi, &enc);
  CHECK(decoded.startsWith("caf"));
  CHECK(decoded.size() == ansi.size());
  CHECK(enc != "UTF-8");
  CHECK(roundTrips(ansi));

  for (const QByteArray& data :
       {QByteArray("plain"), QByteArray("\xEF\xBB\xBFwith bom"),
        QByteArray("\xFF\xFEh\0i\0", 6), QByteArray("[\0G\0]\0", 6)}) {
    CHECK(roundTrips(data));
  }

  // readFileText
  const QString dir = uitest::scratchDir("decode");
  CHECK(writeBytes(dir + "/a.txt", "\xEF\xBB\xBFline"));
  CHECK_STR(readFileText(dir + "/a.txt", &enc), "line");
  CHECK_STR(enc, "UTF-8");
  CHECK_STR(readFileText(dir + "/missing.txt"), "");
  CHECK(readFileText(dir + "/missing.txt").isNull());
}

static void localized()
{
  const QLocale previous;
  QLocale::setDefault(QLocale::c());

  // as localizedByteSizeTests() in utility.cpp, which breaks into a debugger
  CHECK_STR(localizedByteSize(0), "0 B");
  CHECK_STR(localizedByteSize(1023), "1023 B");
  CHECK_STR(localizedByteSize(1024), "1.00 KB");
  CHECK_STR(localizedByteSize(2047), "1.99 KB");
  CHECK_STR(localizedByteSize(1048575), "1023.99 KB");
  CHECK_STR(localizedByteSize(1048576), "1.00 MB");
  CHECK_STR(localizedByteSize(1073741824), "1.00 GB");
  CHECK_STR(localizedByteSize(1099511627776), "1.00 TB");
  CHECK_STR(localizedByteSize(2759774185818), "2.51 TB");
  CHECK(localizedByteSize(~0ull).endsWith(" TB"));
  CHECK_STR(localizedByteSpeed(0), "0 B/s");
  CHECK_STR(localizedByteSpeed(1536), "1.50 KB/s");

  QLocale::setDefault(QLocale(QLocale::Spanish, QLocale::Spain));
  CHECK_STR(localizedByteSize(2048), "2,00 KB");
  CHECK_STR(localizedByteSize(1048575), "1023,99 KB");
  QLocale::setDefault(previous);

  CHECK_STR(localizedTimeRemaining(0), "0s");
  CHECK_STR(localizedTimeRemaining(999), "0s");
  CHECK_STR(localizedTimeRemaining(59999), "59s");
  CHECK_STR(localizedTimeRemaining(60000), "1:00m");
  CHECK_STR(localizedTimeRemaining(61000), "1:01m");
  CHECK_STR(localizedTimeRemaining(3599999), "59:59m");
  CHECK_STR(localizedTimeRemaining(3600000), "01:00:00h");
  CHECK_STR(localizedTimeRemaining(36000000 + 61000), "10:01:01h");
  // the largest: 1193 h 2 min 47 s
  CHECK_STR(localizedTimeRemaining(0xFFFFFFFFu), "1193:02:47h");
}

static void messages()
{
  const std::wstring m = formatSystemMessage(ERROR_FILE_NOT_FOUND);
  CHECK(m.ends_with(L"(0x2)"));
  CHECK(m.find(L' ') != std::wstring::npos);  // words not run together
  CHECK(m.size() > 6 && !std::iswspace(m[0]) && m.find(L"\r") == std::wstring::npos &&
        m.find(L"\n") == std::wstring::npos);

  CHECK(formatSystemMessage(static_cast<DWORD>(0x20001234u)) == L"0x20001234");
  CHECK(formatSystemMessage(E_OUTOFMEMORY).ends_with(L"(0x8007000e)"));
  CHECK(formatNtMessage(static_cast<NTSTATUS>(0xC0000005)).ends_with(L"(0xc0000005)"));
  CHECK(formatNtMessage(static_cast<NTSTATUS>(0xC0000005)).size() > 14);
  CHECK(windowsErrorString(ERROR_ACCESS_DENIED).endsWith("(0x5)"));
}

static void results()
{
  using shell::Result;

  {
    Result r = Result::makeFailure(ERROR_ACCESS_DENIED);
    CHECK(!r.success());
    CHECK(!r);
    CHECK(r.error() == ERROR_ACCESS_DENIED);
    CHECK(r.message().endsWith("(0x5)"));
    CHECK_STR(r.toString(), r.message());
    CHECK(r.processHandle() == INVALID_HANDLE_VALUE);
  }
  {
    Result r = Result::makeFailure(ERROR_ACCESS_DENIED, "custom");
    CHECK_STR(r.message(), "custom");
  }
  {
    Result r = Result::makeSuccess();
    CHECK(r.success());
    CHECK(static_cast<bool>(r));
    CHECK(r.error() == ERROR_SUCCESS);
    CHECK(r.processHandle() == INVALID_HANDLE_VALUE);
    CHECK(r.stealProcessHandle() == INVALID_HANDLE_VALUE);
  }
  {
    // the handle is closed with the Result, unless it was taken
    HANDLE ev = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    {
      Result r = Result::makeSuccess(ev);
      CHECK(r.processHandle() == ev);
      Result moved = std::move(r);
      CHECK(moved.processHandle() == ev);
    }
    DWORD flags = 0;
    CHECK(!GetHandleInformation(ev, &flags));

    HANDLE ev2 = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    HANDLE taken = nullptr;
    {
      Result r = Result::makeSuccess(ev2);
      taken    = r.stealProcessHandle();
      CHECK(r.processHandle() == INVALID_HANDLE_VALUE);
    }
    CHECK(taken == ev2);
    CHECK(GetHandleInformation(ev2, &flags) != 0);
    CloseHandle(ev2);
  }

  CHECK_STR(shell::formatError(ERROR_FILE_NOT_FOUND), "The specified file was not found");
  CHECK_STR(shell::formatError(0), "The operating system is out of memory or resources");
  CHECK_STR(shell::formatError(12345), "Unknown error 12345");
  CHECK_STR(shell::formatError(-1), "Unknown error -1");
}

static void shellFiles()
{
  const QString dir = uitest::scratchDir("shell");

  // nested directories in one go, from a QDir path ("C:/a/b/c"); again: already there
  const QString nested = dir + "/a/b/c";
  {
    auto r = shell::CreateDirectories(QDir(nested));
    if (!r.success()) {
      std::printf("  CreateDirectories: %s\n", r.toString().toUtf8().constData());
    }
    CHECK(r.success());
  }
  CHECK(shell::CreateDirectories(QDir(QDir::toNativeSeparators(dir + "/n1/n2"))).success());
  CHECK(QDir(dir + "/n1/n2").exists());
  {
    // a relative path is refused, not made in the working directory (a split
    // network path's "NAS/Games" made "<MO2>\NAS\Games")
    auto relative = shell::CreateDirectories(QDir("mo2r-relative-test/x"));
    CHECK(!relative.success());
    CHECK(relative.error() == ERROR_BAD_PATHNAME);
    CHECK(!QDir("mo2r-relative-test").exists());
  }
  CHECK(QDir(nested).exists());
  {
    auto again = shell::CreateDirectories(QDir(nested));
    CHECK(!again.success());
    CHECK(again.error() == ERROR_ALREADY_EXISTS);
  }

  // rename and delete, with forward slashes and relative-looking paths made absolute
  CHECK(writeBytes(dir + "/one.txt", "1"));
  CHECK(shell::Rename(QFileInfo(dir + "/one.txt"), QFileInfo(dir + "/two.txt")).success());
  CHECK(!QFile::exists(dir + "/one.txt"));
  CHECK(QFile::exists(dir + "/two.txt"));
  {
    auto r = shell::Rename(QFileInfo(dir + "/one.txt"), QFileInfo(dir + "/three.txt"));
    CHECK(!r.success());
    CHECK(r.error() == ERROR_FILE_NOT_FOUND);
  }
  // no overwrite
  CHECK(writeBytes(dir + "/four.txt", "4"));
  {
    auto r = shell::Rename(QFileInfo(dir + "/four.txt"), QFileInfo(dir + "/two.txt"), false);
    CHECK(!r.success());
    CHECK(r.error() == ERROR_ALREADY_EXISTS);
  }
  CHECK(shell::Delete(QFileInfo(dir + "/two.txt")).success());
  CHECK(!QFile::exists(dir + "/two.txt"));
  {
    auto r = shell::Delete(QFileInfo(dir + "/two.txt"));
    CHECK(!r.success());
    CHECK(r.error() == ERROR_FILE_NOT_FOUND);
  }

  // a file name past MAX_PATH works through the \\?\ prefix
  QString longDir = dir;
  while (longDir.size() < 300) {
    longDir += "/" + QString(40, 'x');
  }
  // (SHCreateDirectory itself stops at 248 characters: Qt makes the folders)
  CHECK(QDir().mkpath(longDir));
  CHECK(writeBytes(longDir + "/f.txt", "f"));
  CHECK(shell::Rename(QFileInfo(longDir + "/f.txt"), QFileInfo(longDir + "/g.txt")).success());
  CHECK(shell::Delete(QFileInfo(longDir + "/g.txt")).success());

  // paths on a network share (\\server\share\...), here the admin share of
  // this machine: \\?\ needs the UNC form, \\?\UNC\server\share
  const QString native = QDir::toNativeSeparators(dir);
  if (native.size() > 2 && native[1] == ':') {
    const QString unc = QString("\\\\localhost\\%1$%2").arg(native[0]).arg(native.mid(2));
    if (QFileInfo(unc).isDir()) {
      CHECK(writeBytes(dir + "/net.txt", "n"));
      CHECK(shell::Rename(QFileInfo(unc + "\\net.txt"), QFileInfo(unc + "\\net2.txt")).success());
      CHECK(QFile::exists(dir + "/net2.txt"));
      CHECK(shell::Delete(QFileInfo(unc + "\\net2.txt")).success());
      CHECK(!QFile::exists(dir + "/net2.txt"));
    } else {
      std::printf("SKIP: %s isn't reachable\n", unc.toUtf8().constData());
    }
  }

  // recursive delete of a tree with files
  CHECK(writeBytes(dir + "/a/b/c/x.txt", "x"));
  CHECK(writeBytes(dir + "/a/y.txt", "y"));
  CHECK(shell::DeleteDirectoryRecursive(QDir(dir + "/a")).success());
  CHECK(!QDir(dir + "/a").exists());

  // copyDir and removeDir (their success paths; the failures report with a box)
  CHECK(QDir().mkpath(dir + "/src/sub"));
  CHECK(writeBytes(dir + "/src/f1.txt", "1"));
  CHECK(writeBytes(dir + "/src/sub/f2.txt", "2"));
  CHECK(copyDir(dir + "/src", dir + "/dst", false));
  CHECK(QFile::exists(dir + "/dst/f1.txt"));
  CHECK(QFile::exists(dir + "/dst/sub/f2.txt"));
  CHECK(!copyDir(dir + "/src", dir + "/dst", false));  // exists, no merge
  CHECK(copyDir(dir + "/src", dir + "/dst", true));
  CHECK(!copyDir(dir + "/nothing", dir + "/dst2", true));
  CHECK(removeDir(dir + "/dst"));
  CHECK(!QDir(dir + "/dst").exists());

  // copyFileRecursive makes the folders on the way
  CHECK(copyFileRecursive(dir + "/src/f1.txt", dir, "deep/er/f1.txt"));
  CHECK(QFile::exists(dir + "/deep/er/f1.txt"));
  CHECK(moveFileRecursive(dir + "/deep/er/f1.txt", dir, "moved/f1.txt"));
  CHECK(QFile::exists(dir + "/moved/f1.txt"));
  CHECK(!QFile::exists(dir + "/deep/er/f1.txt"));

  // removeOldFiles keeps the newest
  CHECK(QDir().mkpath(dir + "/logs"));
  for (int i = 0; i < 5; ++i) {
    const QString p = dir + QString("/logs/log%1.txt").arg(i);
    CHECK(writeBytes(p, "l"));
    QFile f(p);
    (void)f.open(QIODevice::ReadWrite);
    f.setFileTime(QDateTime::currentDateTime().addSecs(-100 + i * 10),
                  QFileDevice::FileModificationTime);
  }
  CHECK(writeBytes(dir + "/logs/other.dat", "o"));
  removeOldFiles(dir + "/logs", "log*.txt", 2, QDir::Time | QDir::Reversed);
  CHECK(QDir(dir + "/logs").entryList(QStringList("log*.txt"), QDir::Files).size() == 2);
  CHECK(QFile::exists(dir + "/logs/log4.txt"));
  CHECK(QFile::exists(dir + "/logs/log3.txt"));
  CHECK(QFile::exists(dir + "/logs/other.dat"));
  removeOldFiles(dir + "/logs", "log*.txt", 10);
  CHECK(QDir(dir + "/logs").entryList(QStringList("log*.txt"), QDir::Files).size() == 2);
}

static void folders()
{
  CHECK(getKnownFolder(FOLDERID_Windows).exists());
  CHECK(!getOptionalKnownFolder(FOLDERID_Windows).isEmpty());
  const KNOWNFOLDERID bogus = {0x12345678, 0x1234, 0x1234, {1, 2, 3, 4, 5, 6, 7, 8}};
  CHECK(getOptionalKnownFolder(bogus).isEmpty());
  bool threw = false;
  try {
    getKnownFolder(bogus, "bogus");
  } catch (const std::runtime_error&) {
    threw = true;
  }
  CHECK(threw);
}

static void guards()
{
  int calls = 0;
  {
    Guard g([&] {
      ++calls;
    });
  }
  CHECK(calls == 1);
  {
    Guard g([&] {
      ++calls;
    });
    g.kill();
  }
  CHECK(calls == 1);
  {
    TimeThis t("test");
    t.stop();
    t.stop();
  }
  CHECK(true);
}

int main(int argc, char** argv)
{
  uitest::redirect(argc, argv);
  strings();
  joins();
  naturalSort();
  decoding();
  localized();
  messages();
  results();
  shellFiles();
  folders();
  guards();
  return uitest::finish("test_utility");
}
