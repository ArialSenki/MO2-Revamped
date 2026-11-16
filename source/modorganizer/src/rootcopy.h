#ifndef MODORGANIZER_ROOTCOPY_H
#define MODORGANIZER_ROOTCOPY_H

#include <QString>
#include <QStringList>
#include <functional>
#include <optional>
#include <vector>

// MO2 Revamped: the copy fallback for the files of the mods' Root/ folders that
// must be real files in the game folder and can't be given to the game another
// way (see rootlaunch.h for when): copied in for a run and taken out after it,
// as Root Builder's copy mode (which Wabbajack lists use) does. A game file a
// copy replaces is moved to a backup folder first, and a manifest written
// before anything is copied lists both, so a crash can be undone on the next
// start. Also the PE reading the launch decisions use.
namespace RootCopy
{

struct File
{
  // in the game folder, with '/'
  QString relativePath;
  // absolute
  QString source;
  // why it must be a real file, for the log
  QString reason;
};

struct Result
{
  int copied   = 0;
  int backedUp = 0;
  int removed  = 0;
  int restored = 0;
  // changed while the program ran: moved to Overwrite/Root
  int kept = 0;
  // left in the manifest for later (a file in use)
  int pending = 0;
  // left as they are because they are neither the copy nor the game's file
  // (something else changed them); their backups are kept, the user is told
  int conflicts = 0;
  QStringList conflictFiles;
  // planned files that couldn't be copied
  int failed = 0;
  // the game folder can't be written to: nothing was copied
  bool notWritable = false;
  QString error;
};

// info or warning; the default writes to qInfo()/qWarning()
using Logger = std::function<void(bool warning, const QString& message)>;
void setLogger(Logger logger);
void info(const QString& message);
void warn(const QString& message);

// what the launch decisions need from a PE file (exe or DLL)
struct PeFile
{
  bool valid = false;
  bool is64  = false;
  // the import table's DLL names, in lower case
  QStringList imports;
  // section names
  QStringList sections;
  // the entry point lies in one of the sections (a packed or wrapped exe's
  // may not)
  bool entryInSection = true;
  // something to import from it: the first exported name, or else an ordinal
  QString exportName;
  quint32 exportOrdinal = 0;
  bool hasExports       = false;
};

PeFile readPe(const QString& path);

// Steam's DRM wrapper (SteamStub): its .bind section, or an entry point outside
// the sections. The import table seen before it ran is the wrapper's
bool isSteamStub(const PeFile& pe);

// which of names (in lower case) the file's bytes contain, as ASCII or UTF-16,
// ignoring case: the DLLs a module may load by name itself
QStringList mentionedNames(const QString& path, const QStringList& names);

// copies the files into gameDir; stateDir holds the manifest and the backups
// (<instance>/rootcopy-backup/<game>), overwriteRoot is where a file the
// program changed goes on restore. With a manifest already there (another
// program still runs), only the files it doesn't have are added
Result deploy(const std::vector<File>& files, const QString& gameDir,
              const QString& stateDir, const QString& overwriteRoot);

// takes the copies out and puts the backups back, from stateDir's manifest
Result restore(const QString& stateDir);

QString manifestPath(const QString& stateDir);
bool hasManifest(const QString& stateDir);

// whether the copies must stay because something may still use them:
//  - a launch between its copies and its start (activeLaunches);
//  - a process MO2 started that is alive (dead ones are closed and dropped
//    from handles);
//  - a start less than graceMs ago (a loader that exited once the game is up,
//    before usvfs lists the game); msSinceLastStart < 0: none;
//  - a program the VFS lists; vfsQuery returns nullopt when it can't tell,
//    which counts as running so an error never takes the copies out
bool programsRunning(int activeLaunches, std::vector<void*>& handles,
                     qint64 msSinceLastStart, qint64 graceMs,
                     const std::function<std::optional<bool>()>& vfsQuery);

}  // namespace RootCopy

#endif  // MODORGANIZER_ROOTCOPY_H
