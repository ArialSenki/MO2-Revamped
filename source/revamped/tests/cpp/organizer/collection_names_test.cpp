// MO2 Revamped: tests for the collection install's name and path helpers (not
// shipped): makeFolderName, versionKey and staysInside (collectionnames.cpp),
// isSafeRelativePath (collectionfiles.cpp) and the collection link parser
// (collectionmanifest.cpp).
// Build and run: run.ps1 in this folder, or from a Developer prompt with the
// Qt and uibase paths of run.ps1:
//   cl /nologo /std:c++20 /EHsc /permissive- /utf-8 /MD /DUNICODE /D_UNICODE /DNOMINMAX
//      /I<qt>\include /I<qt>\include\QtCore /I<src> /I<uibase>\src collection_names_test.cpp
//      <src>\collectionnames.cpp <src>\collectionfiles.cpp <src>\collectionmanifest.cpp
//      /link <qt>\lib\Qt6Core.lib <uibase build>\uibase.lib
//   collection_names_test <work folder>
#include "collectionfiles.h"
#include "collectionmanifest.h"
#include "collectionnames.h"

#include <QDir>
#include <QFile>
#include <QRegularExpression>
#include <QString>

#include <cstdio>
#include <cstdlib>
#include <filesystem>

using namespace Collections;
namespace fs = std::filesystem;

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
#define CHECK_EQ(a, b)                                                                     \
  do {                                                                                     \
    const QString got_ = (a);                                                              \
    const QString want_ = (b);                                                             \
    check(got_ == want_, #a " == " #b, __LINE__,                                           \
          QStringLiteral("(got \"%1\", want \"%2\")").arg(got_, want_));                   \
  } while (false)

// what makeFolderName gives for name, "<false>" when it refuses
QString folder(const QString& name)
{
  QString n = name;
  return makeFolderName(n) ? n : QStringLiteral("<false>");
}

void checkDevice(const QString& name, const QString& prefixed, int line)
{
  const QString got = folder(name);
  check(got == "<false>" || got == prefixed, "device name", line, name + " -> " + got);
}

// whatever makeFolderName accepts is a folder name Windows takes as it is
bool safeFolder(const QString& name)
{
  QString n = name;
  if (!makeFolderName(n)) {
    return n.isEmpty();
  }
  static const QRegularExpression device(
      QStringLiteral("^(con|prn|aux|nul|com[0-9\u00b9\u00b2\u00b3]|lpt[0-9\u00b9\u00b2\u00b3]|"
                     "conin\\$|conout\\$|clock\\$)$"),
      QRegularExpression::CaseInsensitiveOption);
  return !n.isEmpty() && !n.contains(u'/') && !n.contains(u'\\') && !n.endsWith(u'.') &&
         !n.endsWith(u' ') && n != u"." && n != u".." && isSafeRelativePath(n) &&
         !device.match(n.section(u'.', 0, 0).trimmed()).hasMatch();
}

void testMakeFolderName()
{
  CHECK_EQ(folder("SkyUI"), "SkyUI");
  CHECK_EQ(folder("  SkyUI  "), "SkyUI");
  CHECK_EQ(folder("Sky\tUI"), "Sky UI");
  CHECK_EQ(folder(""), "<false>");
  CHECK_EQ(folder("   "), "<false>");
  CHECK_EQ(folder("..."), "<false>");
  CHECK_EQ(folder(".."), "<false>");
  CHECK_EQ(folder("."), "<false>");
  CHECK_EQ(folder("?*<>|"), "<false>");

  // traversal: the separators go, what's left is a plain name
  CHECK_EQ(folder("../evil"), "..evil");
  CHECK_EQ(folder("..\\..\\Windows"), "....Windows");
  CHECK_EQ(folder("C:\\Windows"), "CWindows");

  // trailing dots and spaces, also left after characters were dropped
  CHECK_EQ(folder("J.R.R.?"), "J.R.R");
  CHECK_EQ(folder("Name. . ."), "Name");
  CHECK_EQ(folder("Name.?"), "Name");
  CHECK_EQ(folder("Name \u00a0"), "Name");

  // device names, which uibase's fixDirectoryName refuses in any case: kept
  // with "_" in front, as a collection installed them before (a refused name
  // became "Mod N", and its folder would change at the next update)
#define CHECK_DEVICE(a, b) checkDevice((a), (b), __LINE__)
  CHECK_EQ(folder("CON"), "_CON");
  CHECK_DEVICE("con", "_con");
  CHECK_EQ(folder("Nul.txt"), "_Nul.txt");
  CHECK_EQ(folder("COM1"), "_COM1");
  CHECK_DEVICE("com1", "_com1");
  CHECK_EQ(folder("COM\u00b9"), "_COM\u00b9");
  CHECK_EQ(folder("LPT9.esp"), "_LPT9.esp");
  CHECK_EQ(folder("Aux. Armors"), "_Aux. Armors");
  CHECK_DEVICE("AUX .", "_AUX");
  CHECK_EQ(folder("CONIN$"), "_CONIN$");
  CHECK_EQ(folder("Console Commands"), "Console Commands");

  // an 8.3 short name stands for another folder
  CHECK_EQ(folder("SKYUI~1"), "SKYUI-1");
  CHECK_EQ(folder("SkyUI~12.esp"), "SkyUI-12.esp");
  CHECK_EQ(folder("LongerName~1"), "LongerName~1");

  // unicode and emoji stay
  CHECK_EQ(folder(QString::fromUtf8("Ñandú 🎉 Mod")), QString::fromUtf8("Ñandú 🎉 Mod"));
  CHECK_EQ(folder(QString::fromUtf8("日本語の名前")), QString::fromUtf8("日本語の名前"));
  CHECK_EQ(folder(QString::fromUtf8("🎉")), QString::fromUtf8("🎉"));

  // control characters can't be in a folder name
  CHECK_EQ(folder(QString("a") + QChar(1) + "b"), "<false>");

  // whatever comes out is safe
  const QStringList odd = {
      "CON",        "con.",       "nul ",        "COM0",      "lpt\u00b2.txt", "clock$",
      ".hidden",    "a..",        "  ..  ",      "a/b",       "a\\b",          "a:b",
      "x~1",        "ABCDEF~123", "~",           "A.B.C.",    "\u3000name\u3000",
      QString(300, 'x'),          QString::fromUtf8("🎉.🎉."), QString::fromUtf8("é~1.txt"),
      "CON\u00a0",  "prn.txt.",   "a\"b",        "Mod (1)",   "-",             "_"};
  for (const QString& s : odd) {
    check(safeFolder(s), "safeFolder", __LINE__, s);
  }
}

void testVersionKey()
{
  CHECK_EQ(versionKey("1.6.1170.0"), "1.6.1170");
  CHECK_EQ(versionKey("1.6.1170"), "1.6.1170");
  CHECK_EQ(versionKey("2.5.0.0"), "2.5");
  CHECK_EQ(versionKey("2.5"), "2.5");
  CHECK_EQ(versionKey("v1.10"), "1.10");
  CHECK_EQ(versionKey("01.02.03"), "1.2.3");
  CHECK_EQ(versionKey("1.0.0"), "1");
  CHECK_EQ(versionKey("0"), "0");
  CHECK_EQ(versionKey("0.0.0.0"), "0");
  CHECK_EQ(versionKey(""), "");
  CHECK_EQ(versionKey("abc"), "");
  CHECK_EQ(versionKey("1.6.640.0 (Steam)"), "1.6.640");
  CHECK(versionKey("1.6.1170.0") != versionKey("1.6.1179.0"));
  CHECK(versionKey("1.6.1170.0") == versionKey("1.6.1170.1"));
}

void testStaysInside(const fs::path& work)
{
  const fs::path base = work / "base";
  fs::create_directories(base / "sub");
  fs::create_directories(work / "base-evil");

  CHECK(staysInside(base, base / "a.txt"));
  CHECK(staysInside(base, base / "sub" / "a.txt"));
  CHECK(staysInside(base, base / "new" / "deeper" / "a.txt"));
  CHECK(staysInside(base, base / "sub" / ".." / "a.txt"));
  CHECK(!staysInside(base, base / ".." / "a.txt"));
  CHECK(!staysInside(base, base / "sub" / ".." / ".." / "a.txt"));
  CHECK(!staysInside(base, base / "..\\..\\a.txt"));
  CHECK(!staysInside(base, work / "base-evil" / "a.txt"));
  CHECK(!staysInside(base, base / "C:\\Windows\\a.txt"));
  CHECK(!staysInside(base, base / "\\Windows\\a.txt"));
  CHECK(!staysInside(base, fs::path(L"\\\\server\\share\\a.txt")));
  CHECK(staysInside(base, base / QString::fromUtf8("ñ 🎉.txt").toStdWString()));

  // the case Windows ignores
  CHECK(staysInside(base, fs::path(QString::fromStdWString(base.wstring()).toUpper().toStdWString()) /
                              "a.txt"));

  // a junction inside that points outside is resolved
  const fs::path junction = base / "jn";
  const std::wstring cmd  = L"cmd /c mklink /J \"" + junction.wstring() + L"\" \"" +
                           (work / "base-evil").wstring() + L"\" >nul";
  if (_wsystem(cmd.c_str()) == 0) {
    CHECK(!staysInside(base, junction / "a.txt"));
  } else {
    std::printf("note: no junction made, skipped that check\n");
  }
}

void testSafeRelativePath()
{
  CHECK(isSafeRelativePath("a/b.txt"));
  CHECK(isSafeRelativePath(QString::fromUtf8("Ñ/🎉.dds")));
  CHECK(isSafeRelativePath("textures/readme~1.txt"));
  CHECK(!isSafeRelativePath(""));
  CHECK(!isSafeRelativePath("../a"));
  CHECK(!isSafeRelativePath("a/../b"));
  CHECK(!isSafeRelativePath("a//b"));
  CHECK(!isSafeRelativePath("/a"));
  CHECK(!isSafeRelativePath("a/"));
  CHECK(!isSafeRelativePath("C:/a"));
  CHECK(!isSafeRelativePath("C:a"));
  CHECK(!isSafeRelativePath("a\\b"));
  CHECK(!isSafeRelativePath("a./b"));
  CHECK(!isSafeRelativePath("a /b"));
  CHECK(!isSafeRelativePath("a/b."));
  CHECK(!isSafeRelativePath("nul"));
  CHECK(!isSafeRelativePath("a/NUL.txt"));
  CHECK(!isSafeRelativePath("a/com\u00b3.dds"));
  CHECK(!isSafeRelativePath("conout$"));
  CHECK(!isSafeRelativePath("SKYUI~1/a.esp"));
  CHECK(!isSafeRelativePath("a/b:stream"));
  CHECK(!isSafeRelativePath(QString("a") + QChar(0x1f)));
  CHECK(isSafeRelativePath("console/a.txt"));
}

void testCollectionLinks()
{
  auto link = parseCollectionLink("nxm://skyrimspecialedition/collections/qdurkx/revisions/12");
  CHECK(link && link->game == "skyrimspecialedition" && link->slug == "qdurkx" &&
        link->revision == 12);

  link = parseCollectionLink("  NXM://SkyrimSE/collections/Ab_c-1/revisions/3?key=x  ");
  CHECK(link && link->game == "SkyrimSE" && link->slug == "Ab_c-1" && link->revision == 3);

  link = parseCollectionLink("nxm://stardewvalley/collections/abcd/revisions/7/");
  CHECK(link && link->revision == 7);

  // bad revisions and ids
  CHECK(!parseCollectionLink("nxm://skyrim/collections/abc/revisions/"));
  CHECK(!parseCollectionLink("nxm://skyrim/collections/abc/revisions/x"));
  CHECK(!parseCollectionLink("nxm://skyrim/collections/abc/revisions/-1"));
  CHECK(!parseCollectionLink("nxm://skyrim/collections/abc/revisions/12abc"));
  CHECK(!parseCollectionLink("nxm://skyrim/collections/a.b/revisions/1"));
  CHECK(!parseCollectionLink("nxm://skyrim/collections/../revisions/1"));
  CHECK(!parseCollectionLink("nxm://skyrim/collections/a%2F../revisions/1"));
  CHECK(!parseCollectionLink("nxm:///collections/abc/revisions/1"));
  CHECK(!parseCollectionLink("nxm://skyrim//collections/abc/revisions/1"));
  // a revision that isn't a number Nexus has: no link, not revision 0
  link = parseCollectionLink("nxm://skyrim/collections/abcd/revisions/99999999999");
  check(!link || link->revision > 0, "overflowing revision", __LINE__,
        link ? QString::number(link->revision) : QString());
  link = parseCollectionLink("nxm://skyrim/collections/abcd/revisions/0");
  check(!link || link->revision > 0, "revision 0", __LINE__,
        link ? QString::number(link->revision) : QString());

  CHECK(isCollectionNxmLink("nxm://skyrim/collections/abc/revisions/1"));
  CHECK(!isCollectionNxmLink("nxm://skyrim/mods/123/files/456"));

  link = parseCollectionLink("https://www.nexusmods.com/games/skyrimspecialedition/collections/qdurkx");
  CHECK(link && link->game == "skyrimspecialedition" && link->slug == "qdurkx" &&
        link->revision == -1);
  link = parseCollectionLink("HTTPS://NEXUSMODS.COM/skyrim/collections/abc/revisions/5?tab=mods");
  CHECK(link && link->game == "skyrim" && link->slug == "abc" && link->revision == 5);
  link = parseCollectionLink("next.nexusmods.com/skyrim/collections/abc#mods");
  CHECK(link && link->slug == "abc");
  link = parseCollectionLink("https://www.nexusmods.com/games/skyrim/collections/abcd/revisions/99999999999");
  check(!link || link->revision > 0 || link->revision == -1, "overflowing web revision",
        __LINE__, link ? QString::number(link->revision) : QString());

  CHECK(!parseCollectionLink("https://evilnexusmods.com/skyrim/collections/abc"));
  CHECK(!parseCollectionLink("https://nexusmods.com.evil.com/skyrim/collections/abc"));
  CHECK(!parseCollectionLink("https://user@nexusmods.com/skyrim/collections/abc"));
  CHECK(!parseCollectionLink("https://www.nexusmods.com/games/skyrim/collections/a%2F.."));
  CHECK(!parseCollectionLink("ftp://nexusmods.com/skyrim/collections/abc"));

  // bare slugs
  link = parseCollectionLink("qdurkx");
  CHECK(link && link->game.isEmpty() && link->slug == "qdurkx" && link->revision == -1);
  CHECK(!parseCollectionLink("abc"));
  CHECK(!parseCollectionLink(""));
  CHECK(!parseCollectionLink("   "));
  CHECK(!parseCollectionLink(QString(17, 'a')));
  CHECK(!parseCollectionLink(QString::fromUtf8("ñandú1")));

  CHECK(isValidSlug("abc-_1"));
  CHECK(!isValidSlug(""));
  CHECK(!isValidSlug("a b"));
  CHECK(!isValidSlug("../a"));
  CHECK(!isValidSlug(QString(65, 'a')));
  CHECK(isValidSlug(QString(64, 'a')));
}

}  // namespace

int main(int argc, char** argv)
{
  const fs::path work =
      argc > 1 ? fs::path(QString::fromLocal8Bit(argv[1]).toStdWString())
               : fs::temp_directory_path() / "mo2r-collection-names-test";
  std::error_code ec;
  fs::remove_all(work, ec);
  fs::create_directories(work);

  testMakeFolderName();
  testVersionKey();
  testStaysInside(work);
  testSafeRelativePath();
  testCollectionLinks();

  std::printf("collection_names_test: %d passed, %d failed\n", passed, failed);
  return failed == 0 ? 0 : 1;
}
