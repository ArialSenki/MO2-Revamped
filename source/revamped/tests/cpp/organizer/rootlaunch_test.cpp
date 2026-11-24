// MO2 Revamped: tests for the Root launch planning (rootlaunch.cpp, not
// shipped): which Root files a started program gets by a forced load, by the
// import table patch or by the copy fallback, on folders made for each case.
// Also RootCopy::readPe on files that aren't PE files.
// Build and run: run.ps1 in this folder, which also builds the stand-in game
// exe and proxy DLL in fixtures\; or from a Developer prompt with the Qt paths
// of run.ps1:
//   cl /nologo /std:c++20 /EHsc /permissive- /utf-8 /MD /DUNICODE /D_UNICODE /DNOMINMAX
//      /I<qt>\include /I<qt>\include\QtCore /I<src> rootlaunch_test.cpp
//      <src>\rootlaunch.cpp <src>\rootcopy.cpp <src>\importpatch.cpp
//      /link <qt>\lib\Qt6Core.lib advapi32.lib
//   rootlaunch_test <folder with game.exe and d3d11.dll> <work folder>
#include "rootcopy.h"
#include "rootlaunch.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QString>

#include <cstdio>

#include <windows.h>

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
    const QString got_  = (a);                                                             \
    const QString want_ = (b);                                                             \
    check(got_.compare(want_, Qt::CaseInsensitive) == 0, #a " == " #b, __LINE__,           \
          QStringLiteral("(got \"%1\", want \"%2\")").arg(got_, want_));                   \
  } while (false)

QString g_fixtures;
QString g_work;
int g_case = 0;

void write(const QString& path, const QByteArray& bytes)
{
  QDir().mkpath(QFileInfo(path).absolutePath());
  QFile f(path);
  if (!f.open(QIODevice::WriteOnly) || f.write(bytes) != bytes.size()) {
    std::printf("can't write %s\n", qUtf8Printable(path));
  }
}

void copy(const QString& fixture, const QString& to, const QByteArray& append = {})
{
  QFile f(g_fixtures + "/" + fixture);
  if (!f.open(QIODevice::ReadOnly)) {
    std::printf("missing fixture %s\n", qUtf8Printable(fixture));
    return;
  }
  write(to, f.readAll() + append);
}

QString nat(const QString& path)
{
  return QDir::toNativeSeparators(path);
}

// a game folder with the stand-in game.exe (it imports d3d11.dll) and the
// Root folders of mods A and B (B wins)
struct Setup
{
  QString base, game, rootA, rootB;

  Setup()
  {
    base  = QStringLiteral("%1/case%2").arg(g_work).arg(++g_case);
    game  = base + "/game";
    rootA = base + "/mods/A/Root";
    rootB = base + "/mods/B/Root";
    QDir(base).removeRecursively();
    copy("game.exe", game + "/game.exe");
    QDir().mkpath(rootA);
    QDir().mkpath(rootB);
  }

  RootLaunch::Input input(const QString& run, const QString& version = "1.6.1170.0") const
  {
    RootLaunch::Input in;
    in.rootFolders = {rootA, rootB};
    in.gameDir     = game;
    in.gameBinary  = "game.exe";
    in.gameVersion = version;
    in.runBinary   = run;
    return in;
  }
};

bool hasCopy(const RootLaunch::Plan& p, const QString& rel)
{
  for (const auto& f : p.copies) {
    if (f.relativePath.compare(rel, Qt::CaseInsensitive) == 0) {
      return true;
    }
  }
  return false;
}

QString copyList(const RootLaunch::Plan& p)
{
  QStringList l;
  for (const auto& f : p.copies) {
    l.append(f.relativePath);
  }
  return l.join(", ");
}

void testNothing()
{
  Setup s;
  auto in        = s.input(s.game + "/game.exe");
  in.rootFolders = {};
  const auto p   = RootLaunch::plan(in);
  CHECK(p.runBinary.isEmpty() && p.copies.empty() && !p.patches() &&
        p.forcedLoadLibrary.isEmpty());

  // empty Root folders: nothing either
  const auto q = RootLaunch::plan(s.input(s.game + "/game.exe"));
  CHECK(q.runBinary.isEmpty() && q.copies.empty() && !q.patches() &&
        q.forcedLoadLibrary.isEmpty());
}

void testExtender()
{
  {
    Setup s;
    write(s.rootA + "/skse64_loader.exe", "MZ loader");
    write(s.rootA + "/skse64_1_6_1170.dll", "MZ runtime");
    const auto p = RootLaunch::plan(s.input(s.game + "/skse64_loader.exe"));
    CHECK_EQ(p.forcedLoadProcess, "game.exe");
    CHECK_EQ(p.forcedLoadLibrary, nat(s.rootA + "/skse64_1_6_1170.dll"));
    CHECK_EQ(p.runBinary, s.game + "/game.exe");
    check(p.copies.empty(), "no copies", __LINE__, copyList(p));

    // started from the mod's own Root file, with backslashes
    auto in      = s.input(nat(s.rootA + "/skse64_loader.exe"));
    in.rootFolders = {nat(s.rootA), nat(s.rootB)};
    const auto q = RootLaunch::plan(in);
    CHECK_EQ(q.forcedLoadLibrary, nat(s.rootA + "/skse64_1_6_1170.dll"));
    CHECK_EQ(q.runBinary, s.game + "/game.exe");

    // the game exe started by itself stays without the extender
    const auto g = RootLaunch::plan(s.input(s.game + "/game.exe"));
    CHECK(g.forcedLoadLibrary.isEmpty());

    // another game version: the loader runs, copied with its DLL
    const auto m = RootLaunch::plan(s.input(s.game + "/skse64_loader.exe", "1.6.640.0"));
    CHECK(m.forcedLoadLibrary.isEmpty());
    CHECK(m.extenderMismatchDlls.contains("skse64_1_6_1170.dll", Qt::CaseInsensitive));
    check(hasCopy(m, "skse64_loader.exe") && hasCopy(m, "skse64_1_6_1170.dll"),
          "loader copied", __LINE__, copyList(m));
  }
  {
    // B's runtime wins over A's of the same name
    Setup s;
    write(s.rootA + "/skse64_loader.exe", "MZ loader");
    write(s.rootA + "/skse64_1_6_1170.dll", "MZ runtime A");
    write(s.rootB + "/SKSE64_1_6_1170.DLL", "MZ runtime B");
    const auto p = RootLaunch::plan(s.input(s.game + "/skse64_loader.exe"));
    CHECK_EQ(p.forcedLoadLibrary, nat(s.rootB + "/SKSE64_1_6_1170.DLL"));
  }
  {
    // a loader that calls StartSKSE itself: the copy fallback
    Setup s;
    write(s.rootA + "/skse64_loader.exe", "MZ ... StartSKSE ...");
    write(s.rootA + "/skse64_1_6_1170.dll", "MZ runtime");
    const auto p = RootLaunch::plan(s.input(s.game + "/skse64_loader.exe"));
    CHECK(p.forcedLoadLibrary.isEmpty());
    check(hasCopy(p, "skse64_loader.exe"), "loader copied", __LINE__, copyList(p));
  }
  {
    // nvse_loader -editor starts GECK, not the game
    Setup s;
    write(s.rootA + "/nvse_loader.exe", "MZ loader");
    write(s.rootA + "/nvse_1_4.dll", "MZ runtime");
    auto in         = s.input(s.game + "/nvse_loader.exe", "1.4.0.525");
    const auto plain = RootLaunch::plan(in);
    CHECK_EQ(plain.forcedLoadLibrary, nat(s.rootA + "/nvse_1_4.dll"));
    in.runArguments = "-editor";
    const auto p    = RootLaunch::plan(in);
    CHECK(p.forcedLoadLibrary.isEmpty());
    in.runArguments = "/AltExe other.exe";
    CHECK(RootLaunch::plan(in).forcedLoadLibrary.isEmpty());
    in.runArguments = "-editorial";
    CHECK(!RootLaunch::plan(in).forcedLoadLibrary.isEmpty());
    // the short name is for 1.4.0.525 only
    CHECK(RootLaunch::plan(s.input(s.game + "/nvse_loader.exe", "1.4.0.524"))
              .forcedLoadLibrary.isEmpty());
  }
  {
    // obse_1_2_416.dll for 1.2.0.416; fose_1_7.dll for 1.7.0.3 only
    Setup s;
    write(s.rootA + "/obse_loader.exe", "MZ loader");
    write(s.rootA + "/obse_1_2_416.dll", "MZ runtime");
    write(s.rootA + "/fose_loader.exe", "MZ loader");
    write(s.rootA + "/fose_1_7.dll", "MZ runtime");
    CHECK_EQ(RootLaunch::plan(s.input(s.game + "/obse_loader.exe", "1.2.0.416")).forcedLoadLibrary,
             nat(s.rootA + "/obse_1_2_416.dll"));
    CHECK_EQ(RootLaunch::plan(s.input(s.game + "/fose_loader.exe", "1.7.0.3")).forcedLoadLibrary,
             nat(s.rootA + "/fose_1_7.dll"));
    CHECK(RootLaunch::plan(s.input(s.game + "/fose_loader.exe", "1.7.0.4"))
              .forcedLoadLibrary.isEmpty());
    // no version from the game plugin: the only candidate
    CHECK_EQ(RootLaunch::plan(s.input(s.game + "/obse_loader.exe", "")).forcedLoadLibrary,
             nat(s.rootA + "/obse_1_2_416.dll"));
    // a version that isn't numbers is no version: the only candidate too
    CHECK_EQ(RootLaunch::plan(s.input(s.game + "/fose_loader.exe", "abc")).forcedLoadLibrary,
             nat(s.rootA + "/fose_1_7.dll"));
  }
  {
    // the longest match: skse64_1_6.dll and skse64_1_6_1170.dll
    Setup s;
    write(s.rootA + "/skse64_loader.exe", "MZ loader");
    write(s.rootA + "/skse64_1_6.dll", "MZ short");
    write(s.rootA + "/skse64_1_6_1170.dll", "MZ long");
    CHECK_EQ(RootLaunch::plan(s.input(s.game + "/skse64_loader.exe")).forcedLoadLibrary,
             nat(s.rootA + "/skse64_1_6_1170.dll"));
    // and a name that only looks like one
    write(s.rootB + "/skse64_1_6_1170_x.dll", "MZ");
    write(s.rootB + "/skse64_steam_loader.dll", "MZ");
    CHECK_EQ(RootLaunch::plan(s.input(s.game + "/skse64_loader.exe")).forcedLoadLibrary,
             nat(s.rootA + "/skse64_1_6_1170.dll"));
  }
  {
    // no runtime at all
    Setup s;
    write(s.rootA + "/skse64_loader.exe", "MZ loader");
    const auto p = RootLaunch::plan(s.input(s.game + "/skse64_loader.exe"));
    CHECK(p.forcedLoadLibrary.isEmpty() && p.extenderMismatchDlls.isEmpty());
    check(hasCopy(p, "skse64_loader.exe"), "loader copied", __LINE__, copyList(p));
  }
}

void testProxy()
{
  {
    Setup s;
    copy("d3d11.dll", s.rootA + "/d3d11.dll");
    const auto p = RootLaunch::plan(s.input(s.game + "/game.exe"));
    check(p.redirects.size() == 1 && p.redirects[0].dllName.compare("d3d11.dll", Qt::CaseInsensitive) == 0 &&
              p.redirects[0].path == nat(s.rootA + "/d3d11.dll"),
          "d3d11.dll redirected", __LINE__, QString::number(p.redirects.size()));
    check(p.copies.empty(), "no copies", __LINE__, copyList(p));
    CHECK(p.patchFallback.size() == 1);
    CHECK(p.runBinary.isEmpty());

    // its ini from the same mod comes with it in the fallback
    write(s.rootA + "/d3d11.ini", "[x]");
    const auto q = RootLaunch::plan(s.input(s.game + "/game.exe"));
    CHECK(q.redirects.size() == 1 && q.patchFallback.size() == 2);

    // from another mod: the DLL would read the other mod's file next to the
    // game exe, so both are copied
    QFile::remove(s.rootA + "/d3d11.ini");
    write(s.rootB + "/d3d11.ini", "[x]");
    const auto r = RootLaunch::plan(s.input(s.game + "/game.exe"));
    CHECK(r.redirects.empty());
    check(hasCopy(r, "d3d11.dll") && hasCopy(r, "d3d11.ini"), "copied with its ini",
          __LINE__, copyList(r));

    // a DLL the game plugin loads itself is left to it
    QFile::remove(s.rootB + "/d3d11.ini");
    auto in         = s.input(s.game + "/game.exe");
    in.forcedByGame = {"d3d11.dll"};
    const auto f    = RootLaunch::plan(in);
    CHECK(f.redirects.empty() && f.copies.empty());
  }
  {
    // ENB: its files in another mod, a folder of it too
    Setup s;
    copy("d3d11.dll", s.rootA + "/d3d11.dll", QByteArray("\0enbseries.ini\0", 15));
    write(s.rootA + "/enbseries.ini", "[x]");
    write(s.rootA + "/enbseries/effect.fx", "x");
    const auto p = RootLaunch::plan(s.input(s.game + "/game.exe"));
    CHECK(p.redirects.size() == 1);
    write(s.rootB + "/enblocal.ini", "[x]");
    const auto q = RootLaunch::plan(s.input(s.game + "/game.exe"));
    CHECK(q.redirects.empty());
    check(hasCopy(q, "d3d11.dll") && hasCopy(q, "enblocal.ini") && hasCopy(q, "enbseries.ini") &&
              hasCopy(q, "enbseries/effect.fx"),
          "ENB copied", __LINE__, copyList(q));
  }
  {
    // not a DLL: copied (Windows loads whatever it is)
    Setup s;
    write(s.rootA + "/d3d11.dll", "not a PE file");
    const auto p = RootLaunch::plan(s.input(s.game + "/game.exe"));
    CHECK(p.redirects.empty());
    check(hasCopy(p, "d3d11.dll"), "copied", __LINE__, copyList(p));
  }
}

void testPrograms()
{
  {
    // a program only in a mod's Root runs from there, in the game folder
    Setup s;
    write(s.rootA + "/tool.exe", "MZ tool");
    const auto p = RootLaunch::plan(s.input(s.game + "/tool.exe"));
    CHECK_EQ(p.runBinary, s.rootA + "/tool.exe");
    CHECK_EQ(QDir::cleanPath(p.runDirectory), s.game);
    CHECK(p.copies.empty());

    // in a subfolder
    write(s.rootB + "/tools/x/tool2.exe", "MZ tool");
    const auto q = RootLaunch::plan(s.input(s.game + "/tools/x/tool2.exe"));
    CHECK_EQ(q.runBinary, s.rootB + "/tools/x/tool2.exe");
    CHECK_EQ(q.runDirectory, s.game + "/tools/x");
  }
  {
    // a launcher next to the game exe that names it: the proxies are copied
    Setup s;
    write(s.game + "/launcher.exe", "MZ ... GAME.EXE ...");
    copy("d3d11.dll", s.rootA + "/d3d11.dll");
    const auto p = RootLaunch::plan(s.input(s.game + "/launcher.exe"));
    check(hasCopy(p, "d3d11.dll"), "proxy copied", __LINE__, copyList(p));
    CHECK(p.redirects.empty());
  }
  {
    // a mod's game exe (FNV4GB): copied, with what it imports
    Setup s;
    copy("game.exe", s.rootA + "/game.exe");
    copy("d3d11.dll", s.rootB + "/d3d11.dll");
    const auto p = RootLaunch::plan(s.input(s.game + "/game.exe"));
    check(hasCopy(p, "game.exe") && hasCopy(p, "d3d11.dll"), "exe copied", __LINE__,
          copyList(p));
    CHECK(p.redirects.empty());
  }
  {
    // the game exe in a subfolder (bin/x64/game.exe)
    Setup s;
    copy("game.exe", s.game + "/bin/x64/game.exe");
    copy("d3d11.dll", s.rootA + "/bin/x64/d3d11.dll");
    auto in       = s.input(s.game + "/bin/x64/game.exe");
    in.gameBinary = "bin\\x64\\game.exe";
    const auto p  = RootLaunch::plan(in);
    check(p.redirects.size() == 1 && p.redirects[0].path == nat(s.rootA + "/bin/x64/d3d11.dll"),
          "redirected in a subfolder", __LINE__, QString::number(p.redirects.size()));
    in.forcedByGame = {"bin/x64/d3d11.dll"};
    CHECK(RootLaunch::plan(in).redirects.empty());
  }
}

void testConcerns()
{
  using RootLaunch::concerns;
  const QString game = "C:/Games/Skyrim", mods = "C:/MO2/mods", ow = "C:/MO2/overwrite";
  CHECK(concerns("C:/Games/Skyrim/SkyrimSE.exe", game, mods, ow));
  CHECK(concerns("c:\\games\\skyrim\\skse64_loader.exe", game, mods, ow));
  CHECK(concerns("C:/Games/Skyrim/bin/x.exe", game + "/", mods, ow));
  CHECK(concerns("C:/MO2/mods/A/Root/tool.exe", game, mods, ow));
  CHECK(concerns("C:/MO2/overwrite/Root/tool.exe", game, mods, ow));
  CHECK(!concerns("C:/Games/Skyrim2/x.exe", game, mods, ow));
  CHECK(!concerns("C:/Games/Skyrim", game, mods, ow));
  CHECK(!concerns("C:/Tools/LOOT/loot.exe", game, mods, ow));
  CHECK(!concerns("C:/Games/Skyrim/../LOOT/loot.exe", game, mods, ow));
  CHECK(!concerns("", game, mods, ow));
  CHECK(!concerns("C:/Games/Skyrim/x.exe", "", "", ""));
  CHECK(concerns(QString::fromUtf8("D:/Juegos/Ñandú 🎉/x.exe"),
                 QString::fromUtf8("D:/Juegos/Ñandú 🎉"), mods, ow));

  // a game at a drive root: "D:/" became "D://" and nothing was under it
  CHECK(concerns("D:/game.exe", "D:/", mods, ow));
  CHECK(concerns("d:\\bin\\game.exe", "D:\\", mods, ow));
  CHECK(concerns("D:/game.exe", "D:", mods, ow));
  CHECK(!concerns("E:/game.exe", "D:/", mods, ow));
  CHECK(!concerns("D:/", "D:/", mods, ow));
  CHECK(!concerns("D:", "D:/", mods, ow));
  // mods and Overwrite at a drive root too
  CHECK(concerns("M:/A/Root/tool.exe", game, "M:/", ow));
  CHECK(concerns("O:/Root/tool.exe", game, mods, "O:\\"));
}

// a drive letter that isn't in use: the game folder of the drive-root plan
QString freeDrive()
{
  const DWORD used = GetLogicalDrives();
  for (int i = 25; i >= 3; --i) {
    if (!(used & (1u << i))) {
      return QString(QChar(u'A' + i)) + ":/";
    }
  }
  return QString();
}

void testDriveRoot()
{
  // the plan for a game at a drive root: a program only in a mod's Root runs
  // from there with the drive root as its working folder (the game folder
  // itself needn't exist for that)
  const QString drive = freeDrive();
  if (drive.isEmpty()) {
    std::printf("no free drive letter, drive root plan not tested\n");
    return;
  }
  Setup s;
  write(s.rootA + "/tool.exe", "MZ tool");
  write(s.rootB + "/tools/tool2.exe", "MZ tool");
  for (const QString& gameDir : {drive, QString(drive).replace('/', '\\'), drive.left(2)}) {
    auto in    = s.input(drive + "tool.exe");
    in.gameDir = gameDir;
    const auto p = RootLaunch::plan(in);
    check(p.runBinary.compare(s.rootA + "/tool.exe", Qt::CaseInsensitive) == 0,
          "root program at a drive root", __LINE__, gameDir + ": " + p.runBinary);
    CHECK_EQ(QDir::cleanPath(p.runDirectory), drive);

    // in a subfolder
    auto sub    = s.input(drive + "tools/tool2.exe");
    sub.gameDir = gameDir;
    const auto q = RootLaunch::plan(sub);
    check(q.runBinary.compare(s.rootB + "/tools/tool2.exe", Qt::CaseInsensitive) == 0,
          "Root program in a subfolder at a drive root", __LINE__, gameDir + ": " + q.runBinary);
    CHECK_EQ(q.runDirectory, drive + "tools");
  }
}

void testUnder()
{
  // a file's path in the game folder, also with the game at a drive root
  CHECK(RootCopy::under("D:/Games/Game", "bin/a.dll") == "D:/Games/Game/bin/a.dll");
  CHECK(RootCopy::under("D:/", "bin/a.dll") == "D:/bin/a.dll");
  CHECK(RootCopy::under("D:\\", "a.dll") == "D:\\a.dll");
  CHECK(RootCopy::under("D:/Games/Game/", "a.dll") == "D:/Games/Game/a.dll");
}

void testReadPe()
{
  const QString dir = g_work + "/pe";
  QDir().mkpath(dir);
  // a real one
  const auto game = RootCopy::readPe(g_fixtures + "/game.exe");
  CHECK(game.valid && game.is64 && game.imports.contains("d3d11.dll"));
  CHECK(!RootCopy::isSteamStub(game));
  const auto dll = RootCopy::readPe(g_fixtures + "/d3d11.dll");
  CHECK(dll.valid && dll.hasExports && dll.exportName == "ProxyExport");

  // what isn't one
  CHECK(!RootCopy::readPe(dir + "/missing.dll").valid);
  write(dir + "/empty.dll", "");
  CHECK(!RootCopy::readPe(dir + "/empty.dll").valid);
  write(dir + "/mz.dll", "MZ");
  CHECK(!RootCopy::readPe(dir + "/mz.dll").valid);
  QByteArray junk(4096, '\xff');
  junk[0] = 'M';
  junk[1] = 'Z';
  write(dir + "/junk.dll", junk);
  CHECK(!RootCopy::readPe(dir + "/junk.dll").valid);
  CHECK(!RootCopy::readPe(dir).valid);

  // a real one cut short at every 61 bytes: never a crash
  QFile f(g_fixtures + "/game.exe");
  CHECK(f.open(QIODevice::ReadOnly));
  const QByteArray whole = f.readAll();
  int runs              = 0;
  for (qsizetype n = 0; n < whole.size(); n += 61) {
    write(dir + "/cut.exe", whole.left(n));
    const auto pe = RootCopy::readPe(dir + "/cut.exe");
    if (pe.valid) {
      for (const QString& i : pe.imports) {
        if (i.isEmpty()) {
          check(false, "empty import name", __LINE__, QString::number(n));
          break;
        }
      }
    }
    ++runs;
  }
  CHECK(runs > 10);

  // the same with bytes of its headers changed
  for (int at = 0; at < 1024 && at < whole.size(); at += 7) {
    QByteArray bad = whole;
    bad[at]        = char(bad[at] ^ 0x5a);
    write(dir + "/bad.exe", bad);
    (void)RootCopy::readPe(dir + "/bad.exe");
  }
  CHECK(true);
}

}  // namespace

int main(int argc, char** argv)
{
  if (argc < 3) {
    std::printf("usage: rootlaunch_test <fixtures folder> <work folder>\n");
    return 2;
  }
  g_fixtures = QDir::fromNativeSeparators(QString::fromLocal8Bit(argv[1]));
  g_work     = QDir::fromNativeSeparators(QString::fromLocal8Bit(argv[2]));
  QDir(g_work).removeRecursively();
  QDir().mkpath(g_work);
  g_work = QFileInfo(g_work).absoluteFilePath();
  RootCopy::setLogger([](bool, const QString&) {});

  testNothing();
  testExtender();
  testProxy();
  testPrograms();
  testConcerns();
  testDriveRoot();
  testUnder();
  testReadPe();

  std::printf("rootlaunch_test: %d passed, %d failed\n", passed, failed);
  return failed == 0 ? 0 : 1;
}
