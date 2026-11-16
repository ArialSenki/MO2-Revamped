#ifndef WABBAJACKINSTALLER_H
#define WABBAJACKINSTALLER_H

// MO2 Revamped: installing a Wabbajack list into a folder, from the archives
// its download queue verified.
//
// What the list's directives say is done, file by file, and every file written
// is checked against the list's xxHash64:
//  - FromArchive: a file out of an archive (7z, zip, rar...), out of a BSA or
//    BA2 (the game's own ones too), or out of an archive inside another one;
//    a game file taken whole.
//  - PatchedFromArchive: the same, then the list's patch (OctoDiff; BSDiff for
//    old lists).
//  - InlineFile and RemappedInlineFile: the files the list carries; the second
//    with the paths of this install written in.
//  - CreateBSA: BSA and BA2 archives built from the files gathered for them,
//    with libbsarch.
//  - TransformedTexture: a texture made smaller or recompressed, as the list's
//    author did, with DirectXTex (not checked by hash: the result of a
//    compression isn't the same byte for byte, Wabbajack doesn't check it either).
// Then the screen's size goes into the profiles' prefs, as Wabbajack does.
// Mod Organizer's own files (the list brings its own MO2) are never written;
// the list's ModOrganizer.ini is handed back, its paths written in, for the
// instance to take what it needs. MO2 plugins the list adds go aside, to be
// offered, not into MO2 Revamped's own plugins.
// Nothing outside the install folder is written; the game's and the downloads'
// files are only read.

#include "wabbajacklist.h"

#include <QHash>
#include <QString>
#include <QStringList>

#include <atomic>
#include <functional>

namespace Wabbajack
{

struct InstallPlan
{
  QString listPath;
  QString installFolder;
  QString downloadsFolder;
  QString gameFolder;  // empty when the game isn't installed
  // the file of each archive, by its hash: downloads and game files
  QHash<uint64_t, QString> archives;
  // the screen the game's prefs get, as Wabbajack writes it; 0 leaves them
  int screenWidth  = 0;
  int screenHeight = 0;
  // MO2 maps the mods' Root/ folders to the game folder itself (no Root Builder)
  bool rootHandledByMo2 = true;
};

struct InstallResult
{
  bool ok        = false;
  bool cancelled = false;
  QString error;  // why it couldn't go on

  int written       = 0;  // files written and verified
  int modOrganizer  = 0;  // Mod Organizer's own files, left out
  int failed        = 0;  // written but not the list's, or not written
  QStringList failures;   // the first of them, with the reason
  QStringList unsupported;  // directive types left out, with how many
  QStringList plugins;      // MO2 plugins the list adds, set aside
  QStringList notes;
  // the list's ModOrganizer.ini with this install's paths, empty when none
  QByteArray modOrganizerIni;
  QStringList profiles;
};

// progress: a line for the user, and how far it is (0..1000)
using InstallProgress = std::function<void(const QString& text, int permille)>;

InstallResult install(const InstallPlan& plan, const std::atomic_bool& cancel,
                      const InstallProgress& progress);

// a path of the list ("mods\X\a.esp") under the install folder; empty when it
// would lead out of it
QString installPath(const QString& installFolder, const QString& relative);

// the folder where an install keeps its own files (temporary ones, the MO2
// plugins set aside)
QString installStateFolder(const QString& installFolder);

// an install that finished there: its folder isn't one to install into again
bool isFinishedInstall(const QString& installFolder);

}  // namespace Wabbajack

#endif  // WABBAJACKINSTALLER_H
