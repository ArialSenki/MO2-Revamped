#ifndef MODORGANIZER_ROOTLAUNCH_H
#define MODORGANIZER_ROOTLAUNCH_H

#include "importpatch.h"
#include "rootcopy.h"

#include <QString>
#include <QStringList>
#include <vector>

// MO2 Revamped: how the files of the mods' Root/ folders that the VFS can't
// serve reach a program MO2 starts, keeping the game folder as it is wherever
// that is reliable (rules and reasons are logged):
//
//  - a script extender's loader in Root/ (skse64_loader.exe, nvse_loader.exe):
//    the game exe is started instead, with the extender's runtime DLL for the
//    game's version as a usvfs forced load, which usvfs loads before the game's
//    entry point. Not when the game exe is SteamStub-wrapped (its code isn't
//    unpacked yet), when no runtime DLL matches the game's version, or when the
//    loader calls the DLL's Start<SE> export after injecting it (a load alone
//    doesn't start it): then the copy fallback, and the loader runs;
//  - DLLs the game exe imports that a mod's Root/ has (ENB/ReShade d3d11.dll,
//    DXVK's d3d9.dll, NVHR's d3dx9_38.dll): the import table is rewritten in
//    memory to load them from the mod (importpatch.h), with the Root DLLs they
//    import or name first. Not when the exe is SteamStub-wrapped (the import
//    table seen is the wrapper's), when the files ENB/ReShade read next to
//    their own DLL come from another mod, or when a path can't be given to the
//    loader: then the copy fallback;
//  - a program only in Root/ runs from the mod's file with the game folder as
//    its working folder (MO2 starts a program in the mods folder through the
//    VFS); a program in the game folder MO2 starts gets the same import table
//    treatment as the game;
//  - a game exe replaced by a mod (FNV4GB's FalloutNV.exe), and a program next
//    to the game exe that names it (a launcher that may start it): the copy
//    fallback (rootcopy.h).
namespace RootLaunch
{

struct Input
{
  // lowest priority first; Overwrite/Root last
  QStringList rootFolders;
  QString gameDir;
  // relative to gameDir (IPluginGame::binaryName())
  QString gameBinary;
  // IPluginGame::gameVersion()
  QString gameVersion;
  // the program to start, absolute, and its arguments
  QString runBinary;
  QString runArguments;
  // MO2 Revamped: DLLs the game plugin has usvfs load (forced loads), relative
  // to gameDir and in lower case (bin/x64/version.dll): left to the VFS,
  // which serves them under the game folder's path, as they expect
  QStringList forcedByGame;
};

struct Plan
{
  // a program to start instead (the game exe for an extender's loader, the
  // loader's copy, or a mod's Root file for a program only there); empty:
  // unchanged
  QString runBinary;
  // its working folder; empty: the program's own folder
  QString runDirectory;

  // the extender's runtime DLL, as a forced load for the game exe
  QString forcedLoadProcess;
  QString forcedLoadLibrary;

  // MO2 Revamped: the extender's loader starts the game, but none of its
  // runtime DLLs (fose_1_7.dll) is for the game's version
  QStringList extenderMismatchDlls;

  // the import table patch of the started process
  std::vector<ImportPatch::Redirect> redirects;
  std::vector<ImportPatch::Preload> preloads;

  // copied before the start
  std::vector<RootCopy::File> copies;
  // copied when the import patch fails, before the program starts again
  std::vector<RootCopy::File> patchFallback;

  bool patches() const { return !redirects.empty() || !preloads.empty(); }
};

Plan plan(const Input& input);

// whether a program can need anything from Root: one in the game folder, or
// one in a mod or Overwrite (a Root-only program); a tool in its own folder
// (LOOT, xEdit) never does, and isn't planned
bool concerns(const QString& binary, const QString& gameDir, const QString& modsDir,
              const QString& overwriteDir);

}  // namespace RootLaunch

#endif  // MODORGANIZER_ROOTLAUNCH_H
