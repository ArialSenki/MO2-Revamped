#ifndef MODORGANIZER_IMPORTPATCH_H
#define MODORGANIZER_IMPORTPATCH_H

#include <QString>
#include <QStringList>
#include <vector>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

// MO2 Revamped: the DLLs a game exe imports are loaded by Windows before usvfs
// hooks the process, so the VFS can't give it a proxy DLL (ENB's d3d11.dll,
// DXVK's d3d9.dll) from a mod's Root/. Instead, while the process is still
// suspended, its import table is rewritten in memory (the technique of
// Microsoft Detours' DetourCreateProcessWithDll): a new copy of the import
// descriptors, in memory allocated after the image, names those DLLs by the
// full path of the mod's file, and DLLs they need from a mod are imported
// first by full path so a load by name finds them already there. Nothing is
// written to the game folder. Works for 32-bit (WOW64) and 64-bit targets.
namespace ImportPatch
{

struct Redirect
{
  // as the import table names it, any case
  QString dllName;
  // the mod's file
  QString path;
};

struct Preload
{
  QString path;
  // what to import from it: a name, or else the ordinal
  QString exportName;
  quint32 exportOrdinal = 0;
};

// the path as the loader reads an import name (ANSI): the path itself when
// it's ASCII, else its 8.3 short name, else empty
QString importablePath(const QString& path);

// rewrites the import table of the main image of a process created suspended
// that hasn't run yet; on failure nothing in the process is changed
bool patchSuspendedProcess(HANDLE process, const std::vector<Redirect>& redirects,
                           const std::vector<Preload>& preloads, QString* error);

}  // namespace ImportPatch

#endif  // MODORGANIZER_IMPORTPATCH_H
