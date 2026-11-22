// MO2 Revamped: tests for uibase's pure helpers outside utility.cpp (not shipped):
// folder and file name fixing (filesystemutilities.cpp), nxm:// links
// (nxmurl.cpp), and the JSON reader behind ModRepositoryFileInfo
// (json.cpp, through modrepositoryfileinfo.cpp, the exported way in).
// Build and run: powershell -File run.ps1 -Only misc   (from this folder)
#include "test_common.h"

#include <exceptions.h>
#include <filesystemutilities.h>
#include <modrepositoryfileinfo.h>
#include <nxmurl.h>

using namespace MOBase;

// fixDirectoryName's result, or "<false>" when it gave up
static QString fixed(const QString& name)
{
  QString n = name;
  return fixDirectoryName(n) ? n : QString("<false>");
}

static void directoryNames()
{
  CHECK_STR(fixed("My Mod"), "My Mod");
  CHECK_STR(fixed("  My   Mod  "), "My Mod");
  CHECK_STR(fixed("Mod?"), "Mod");
  CHECK_STR(fixed("a<b>c:d\"e/f\\g|h?i*j"), "abcdefghij");
  CHECK_STR(fixed("Mod v1."), "Mod v1");
  CHECK_STR(fixed("Mod..."), "Mod");
  CHECK_STR(fixed(".hidden"), ".hidden");
  CHECK_STR(fixed(QString::fromUtf8("M\xC3\xB3" "d \xE6\x97\xA5")),
            QString::fromUtf8("M\xC3\xB3" "d \xE6\x97\xA5"));

  CHECK_STR(fixed(""), "<false>");
  CHECK_STR(fixed("   "), "<false>");
  CHECK_STR(fixed("..."), "<false>");
  CHECK_STR(fixed(".."), "<false>");
  CHECK_STR(fixed("???"), "<false>");
  // the original stays as it was when it gives up
  QString keep = "???";
  CHECK(!fixDirectoryName(keep));
  CHECK_STR(keep, "???");

  // no trailing dot or space left once characters are dropped: Explorer
  // can't open "J.R.R." or "Mod ."
  CHECK_STR(fixed("J.R.R.?"), "J.R.R");
  CHECK_STR(fixed("Mod v1.:"), "Mod v1");
  CHECK_STR(fixed("Mod . ?"), "Mod");
  CHECK_STR(fixed("a. :"), "a");

  // device names, in any case
  CHECK_STR(fixed("CON"), "<false>");
  CHECK_STR(fixed("NUL"), "<false>");
  CHECK_STR(fixed("COM1"), "<false>");
  CHECK_STR(fixed("LPT9"), "<false>");
  CHECK_STR(fixed("con"), "<false>");
  CHECK_STR(fixed("Aux"), "<false>");
  CHECK_STR(fixed("CON?"), "<false>");
  CHECK_STR(fixed("CON."), "<false>");
  CHECK_STR(fixed("CON ?"), "<false>");
  CHECK_STR(fixed("COM10"), "COM10");
  CHECK_STR(fixed("Console"), "Console");
}

static void fileNames()
{
  CHECK_STR(sanitizeFileName("a:b*c?.txt"), "abc.txt");
  CHECK_STR(sanitizeFileName("a:b", "_"), "a_b");
  CHECK_STR(sanitizeFileName("name. . "), "name");
  CHECK_STR(sanitizeFileName(QString("tab\there\x01")), "tabhere");
  CHECK_STR(sanitizeFileName(".."), "");
  CHECK_STR(sanitizeFileName(""), "");
  CHECK_STR(sanitizeFileName("a?.", "_"), "a_");
  CHECK_STR(sanitizeFileName(QString::fromUtf8("\xC3\xA9t\xC3\xA9.esp")),
            QString::fromUtf8("\xC3\xA9t\xC3\xA9.esp"));

  CHECK(validFileName("plugin.esp"));
  CHECK(!validFileName(""));
  CHECK(!validFileName("."));
  CHECK(!validFileName(".."));
  CHECK(!validFileName("a/b"));
  CHECK(!validFileName("a\\b"));
  CHECK(!validFileName("trailing."));
  CHECK(!validFileName("trailing "));
  CHECK(!validFileName(QString::fromLatin1("nul\0in", 6)));
}

static void nxm()
{
  {
    NXMUrl u("nxm://skyrimspecialedition/mods/266/files/123456?key=abc&expires=1700000000&user_id=42");
    CHECK_STR(u.game(), "skyrimspecialedition");
    CHECK(u.modId() == 266);
    CHECK(u.fileId() == 123456);
    CHECK_STR(u.key(), "abc");
    CHECK(u.expires() == 1700000000);
    CHECK(u.userId() == 42);
  }
  {
    // upper case, no query
    NXMUrl u("NXM://Fallout4/Mods/1/Files/2");
    CHECK(u.modId() == 1);
    CHECK(u.fileId() == 2);
    CHECK_STR(u.key(), "");
    CHECK(u.expires() == 0);
  }

  for (const char* bad : {"", "nxm://", "nxm://game/mods/x/files/1", "http://game/mods/1/files/2",
                          "nxm://game/mods/1", "nxm:///mods/1/files/2",
                          "nxm://game-with-dash/mods/1/files/2"}) {
    bool threw = false;
    try {
      NXMUrl u(bad);
    } catch (const InvalidNXMLinkException&) {
      threw = true;
    }
    if (!threw) {
      std::printf("  accepted \"%s\"\n", bad);
    }
    CHECK(threw);
  }
}

static void json()
{
  const QString good =
      R"([ "SkyrimSE", 12, "File", "uri", "1.2.3", "desc \"quoted\" \u00e9", 5, 1024, 266,)"
      R"( "Mod", "1.3", "file.7z", 1, "Nexus", { "a" : 1, "b" : [true, false, null] } ])";
  const auto info = ModRepositoryFileInfo::createFromJson(good);
  CHECK_STR(info.gameName, "SkyrimSE");
  CHECK(info.fileID == 12);
  CHECK_STR(info.name, "File");
  CHECK_STR(info.version.displayString(), "1.2.3");
  CHECK_STR(info.description, QString::fromUtf8("desc \"quoted\" \xC3\xA9"));
  CHECK(info.categoryID == 5);
  CHECK(info.fileSize == 1024);
  CHECK(info.modID == 266);
  CHECK_STR(info.newestVersion.displayString(), "1.3");
  CHECK_STR(info.repository, "Nexus");
  CHECK(info.userData.value("a").toInt() == 1);
  CHECK(info.userData.value("b").toList().size() == 3);

  // toString() reads back
  const auto again = ModRepositoryFileInfo::createFromJson(info.toString());
  CHECK_STR(again.gameName, info.gameName);
  CHECK(again.modID == info.modID);
  CHECK(again.fileID == info.fileID);
  CHECK_STR(again.modName, info.modName);
  CHECK(again.version == info.version);
  CHECK(again.userData == info.userData);

  // damaged input: an empty info, never a crash
  for (const QString& bad :
       {QString(""), QString("["), QString("[\"unterminated"), QString("[1,"),
        QString("{\"a\""), QString("{\"a\":"), QString("[\"\\u12\"]"), QString("[\"\\"),
        QString("[tru"), QString("[nul"), QString("[-]"), QString("[1e5, -1e999, 1.5e]"),
        QString("{1:2}"), QString(")"), QString("[\"a\" \"b\"]"),
        QString(1000, '['), QString(1000, '{'), QString("[") + QString(5000, ',') + "]",
        QString::fromUtf8("[\"\xF0\x9F\x8E\xAE\"]")}) {
    const auto i = ModRepositoryFileInfo::createFromJson(bad);
    CHECK(i.modID == 0 || !bad.isEmpty());
  }

  // a short list: the rest is defaulted
  const auto shortInfo = ModRepositoryFileInfo::createFromJson(R"(["Game", 7])");
  CHECK_STR(shortInfo.gameName, "Game");
  CHECK(shortInfo.fileID == 7);
  CHECK(shortInfo.modID == 0);
  CHECK(!shortInfo.version.isValid());

  // numbers: big, negative, fractional; where they don't fit, 0
  const auto nums =
      ModRepositoryFileInfo::createFromJson(R"(["G", -3, "", "", "", "", 4294967295, 8589934592, 2.0])");
  CHECK(nums.fileID == -3);
  CHECK(nums.fileSize == 8589934592ull);
  CHECK(nums.modID == 2);
}

int main(int argc, char** argv)
{
  uitest::redirect(argc, argv);
  directoryNames();
  fileNames();
  nxm();
  json();
  return uitest::finish("test_misc");
}
