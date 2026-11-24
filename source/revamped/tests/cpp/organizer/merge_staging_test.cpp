// MO2 Revamped: tests for MergeStaging::apply (mergestaging.cpp): the files of
// a merge move from the folder they were extracted to into the installed mod,
// and a move that can't finish leaves the mod as it was.
// Build and run: run.ps1 in this folder.
//   merge_staging_test <work folder>
#include "mergestaging.h"

#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QMap>
#include <QString>

#include <cstdio>

namespace
{
int g_passed = 0;
int g_failed = 0;

void check(bool ok, const char* what, int line)
{
  if (ok) {
    ++g_passed;
  } else {
    ++g_failed;
    std::printf("FAIL line %d: %s\n", line, what);
  }
}
#define CHECK(x) check((x), #x, __LINE__)

void write(const QString& path, const QByteArray& bytes)
{
  QDir().mkpath(QFileInfo(path).absolutePath());
  QFile f(path);
  (void)f.open(QIODevice::WriteOnly);
  f.write(bytes);
}

// every file and folder under root, with the files' contents
QMap<QString, QByteArray> snapshot(const QString& root)
{
  QMap<QString, QByteArray> all;
  QDirIterator it(root, QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden,
                  QDirIterator::Subdirectories);
  while (it.hasNext()) {
    it.next();
    const QString rel = QDir(root).relativeFilePath(it.filePath());
    if (it.fileInfo().isDir()) {
      all.insert(rel + "/", {});
    } else {
      QFile f(it.filePath());
      (void)f.open(QIODevice::ReadOnly);
      all.insert(rel, f.readAll());
    }
  }
  return all;
}

struct Case
{
  QString staged, mod, backup;
  Case(const QString& work, const QString& name)
      : staged(work + "/" + name + "/slot/new"), mod(work + "/" + name + "/mods/Mod"),
        backup(work + "/" + name + "/slot/old")
  {
    // the installed mod
    write(mod + "/Mod.esp", "old plugin");
    write(mod + "/meta.ini", "[General]\nversion=1.0\n");
    write(mod + "/textures/a.dds", "old a");
    write(mod + "/textures/keep/only-old.dds", "only old");
    QDir().mkpath(mod + "/empty-old");
    QDir().mkpath(staged);
  }
};

void testMerge(const QString& work)
{
  Case c(work, "merge");
  write(c.staged + "/Mod.esp", "new plugin");
  write(c.staged + "/textures/a.dds", "new a");
  write(c.staged + "/textures/b.dds", "new b");
  write(c.staged + "/meshes/deep/er/m.nif", "new mesh");
  QDir().mkpath(c.staged + "/empty-new");

  const auto r = MergeStaging::apply(c.staged, c.mod, c.backup);
  CHECK(r.ok);
  CHECK(r.moved == 4);
  CHECK(r.replaced == 2);
  const auto mod = snapshot(c.mod);
  CHECK(mod.value("Mod.esp") == "new plugin");
  CHECK(mod.value("textures/a.dds") == "new a");
  CHECK(mod.value("textures/b.dds") == "new b");
  CHECK(mod.value("meshes/deep/er/m.nif") == "new mesh");
  CHECK(mod.value("textures/keep/only-old.dds") == "only old");
  CHECK(mod.value("meta.ini") == "[General]\nversion=1.0\n");
  CHECK(mod.contains("empty-new/"));
  CHECK(mod.contains("empty-old/"));
  // what it replaced waits in the backup until the caller drops it
  const auto old = snapshot(c.backup);
  CHECK(old.value("Mod.esp") == "old plugin");
  CHECK(old.value("textures/a.dds") == "old a");
}

void testCaseOnly(const QString& work)
{
  // the archive spells a name in another case: it is the same file
  Case c(work, "case");
  write(c.staged + "/Textures/A.DDS", "new a");
  const auto r = MergeStaging::apply(c.staged, c.mod, c.backup);
  CHECK(r.ok);
  CHECK(r.replaced == 1);
  const QStringList files = QDir(c.mod + "/textures").entryList(QDir::Files);
  CHECK(files.size() == 1);
  QFile f(c.mod + "/textures/a.dds");
  CHECK(f.open(QIODevice::ReadOnly) && f.readAll() == "new a");
}

// a folder in the mod where the archive has a file: that one can't be moved
void testUndo(const QString& work, const QString& name, const QString& blocked)
{
  Case c(work, name);
  write(c.mod + "/" + blocked + "/inside/x.txt", "x");
  write(c.staged + "/Mod.esp", "new plugin");
  write(c.staged + "/textures/a.dds", "new a");
  write(c.staged + "/textures/b.dds", "new b");
  write(c.staged + "/meshes/deep/m.nif", "new mesh");
  write(c.staged + "/zz/last.txt", "new last");
  write(c.staged + "/" + blocked, "a file where the mod has a folder");
  const auto before       = snapshot(c.mod);
  const auto stagedBefore = snapshot(c.staged);

  const auto r = MergeStaging::apply(c.staged, c.mod, c.backup);
  CHECK(!r.ok);
  CHECK(r.undone);
  CHECK(QFileInfo(r.failed).fileName() == QFileInfo(blocked).fileName());
  // the mod exactly as it was: no new file, no new folder, every old file back
  CHECK(snapshot(c.mod) == before);
  // and the new files are all still where they were extracted
  CHECK(snapshot(c.staged) == stagedBefore);
  // only the folders made for the replaced files may remain in the backup
  const auto backup = snapshot(c.backup);
  for (auto it = backup.cbegin(); it != backup.cend(); ++it) {
    CHECK(it.key().endsWith('/'));
  }
}

void testLocked(const QString& work)
{
  // a file of the mod that another program holds open, as the game would
  Case c(work, "locked");
  write(c.staged + "/Mod.esp", "new plugin");
  write(c.staged + "/textures/a.dds", "new a");
  write(c.staged + "/textures/b.dds", "new b");
  const auto before = snapshot(c.mod);
  QFile held(c.mod + "/textures/a.dds");
  CHECK(held.open(QIODevice::ReadOnly));
  const auto r = MergeStaging::apply(c.staged, c.mod, c.backup);
  held.close();
  if (r.ok) {
    // the system let the open file be renamed: nothing to undo here
    std::printf("the open file could be moved, the locked case was not tested\n");
    return;
  }
  CHECK(r.undone);
  CHECK(snapshot(c.mod) == before);
}

// the state a merge is in when the program dies while the files are moving
void stopPartway(const Case& c)
{
  write(c.staged + "/Mod.esp", "new plugin");
  write(c.staged + "/textures/a.dds", "new a");
  write(c.staged + "/textures/b.dds", "new b");
  write(c.staged + "/meshes/deep/m.nif", "new mesh");
  write(c.staged + "/meshes/later.nif", "new later");
  CHECK(MergeStaging::writePlan(c.staged, c.staged + "/../moving.txt"));
  // one replaced and one added file are in the mod
  QDir().mkpath(c.backup + "/textures");
  CHECK(QFile::rename(c.mod + "/textures/a.dds", c.backup + "/textures/a.dds"));
  CHECK(QFile::rename(c.staged + "/textures/a.dds", c.mod + "/textures/a.dds"));
  QDir().mkpath(c.mod + "/meshes/deep");
  CHECK(QFile::rename(c.staged + "/meshes/deep/m.nif", c.mod + "/meshes/deep/m.nif"));
  // and one was set aside when it stopped, its new version not in yet
  CHECK(QFile::rename(c.mod + "/Mod.esp", c.backup + "/Mod.esp"));
}

void testUndoInterrupted(const QString& work)
{
  Case c(work, "died-undo");
  const auto before = snapshot(c.mod);
  stopPartway(c);
  CHECK(snapshot(c.mod) != before);
  CHECK(MergeStaging::undoInterrupted(c.staged + "/../moving.txt", c.staged, c.mod, c.backup));
  CHECK(snapshot(c.mod) == before);

  // a plan with names that leave the mod touches nothing out there
  Case d(work, "died-plan");
  write(work + "/died-plan/mods/Other/keep.txt", "other mod");
  write(d.staged + "/../moving.txt", "../Other/keep.txt\nC:/Windows/x\n\nMod.esp\n");
  const auto all = snapshot(work + "/died-plan/mods");
  MergeStaging::undoInterrupted(d.staged + "/../moving.txt", d.staged, d.mod, d.backup);
  CHECK(snapshot(work + "/died-plan/mods").value("Other/keep.txt") == "other mod");
  CHECK(snapshot(work + "/died-plan/mods").size() == all.size() - 1);  // Mod.esp: in the plan
  CHECK(!MergeStaging::undoInterrupted(d.staged + "/../none.txt", d.staged, d.mod, d.backup));
}

void testFinishInterrupted(const QString& work)
{
  Case c(work, "died-finish");
  stopPartway(c);
  const auto r = MergeStaging::apply(c.staged, c.mod, c.backup);
  CHECK(r.ok);
  CHECK(r.moved == 3);
  const auto mod = snapshot(c.mod);
  CHECK(mod.value("Mod.esp") == "new plugin");
  CHECK(mod.value("textures/a.dds") == "new a");
  CHECK(mod.value("textures/b.dds") == "new b");
  CHECK(mod.value("meshes/deep/m.nif") == "new mesh");
  CHECK(mod.value("meshes/later.nif") == "new later");
  CHECK(mod.value("textures/keep/only-old.dds") == "only old");
}

void testNothing(const QString& work)
{
  Case c(work, "nothing");
  const auto before = snapshot(c.mod);
  const auto r      = MergeStaging::apply(c.staged, c.mod, c.backup);
  CHECK(r.ok);
  CHECK(r.moved == 0);
  CHECK(snapshot(c.mod) == before);
}
}  // namespace

int main(int argc, char** argv)
{
  if (argc < 2) {
    std::printf("usage: merge_staging_test <work folder>\n");
    return 2;
  }
  QString work = QDir::fromNativeSeparators(QString::fromLocal8Bit(argv[1]));
  QDir(work).removeRecursively();
  QDir().mkpath(work);
  work = QFileInfo(work).absoluteFilePath();

  testMerge(work);
  testCaseOnly(work);
  // stopped at the first file, in the middle and at the last one
  testUndo(work, "undo-first", "AAA-first");
  testUndo(work, "undo-middle", "textures/blocked.dds");
  testUndo(work, "undo-last", "zzz/last");
  testLocked(work);
  testUndoInterrupted(work);
  testFinishInterrupted(work);
  testNothing(work);

  std::printf("merge_staging_test: %d passed, %d failed\n", g_passed, g_failed);
  return g_failed == 0 ? 0 : 1;
}
