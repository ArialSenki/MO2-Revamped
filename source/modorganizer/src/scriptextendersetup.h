#ifndef SCRIPTEXTENDERSETUP_H
#define SCRIPTEXTENDERSETUP_H

// MO2 Revamped: the game's script extender (SKSE, F4SE, NVSE, OBSE...). Its
// loader and DLLs have to sit next to the game's exe, where a mod can't put
// them: when an archive of the extender is installed, MO2 offers to copy those
// files into the game folder and installs the rest (Data\Scripts) as a mod
// that keeps the list of what it copied. Before the game starts through the
// loader, MO2 checks that the extender was made for the game's version.

#include <ifiletree.h>

#include <QCoreApplication>
#include <QDir>
#include <QStringList>

#include <memory>
#include <optional>
#include <vector>

class QDialog;
class QWidget;

namespace MOBase
{
class IPluginGame;
class ScriptExtender;
}  // namespace MOBase

class ScriptExtenderSetup
{
  Q_DECLARE_TR_FUNCTIONS(ScriptExtenderSetup)

public:
  // what an archive of the extender holds
  struct Layout
  {
    // the folder with the loader: the archive's root or its only folder
    std::shared_ptr<MOBase::IFileTree> base;

    // the exe and DLL files next to the loader, for the game folder
    std::vector<std::shared_ptr<const MOBase::FileTreeEntry>> gameFiles;

    // its Data folder, installed as a mod; null when there is none
    std::shared_ptr<MOBase::IFileTree> data;
  };

  enum class Choice
  {
    // the loader and DLLs into the game folder, Data as a mod
    GameFolder,
    // Data as a mod; the user copies the rest
    Myself,
    // the archive goes through the installers as before (Root Builder)
    RegularMod,
    Cancel,
  };

  // the start of the extender's file names: "skse64_" for skse64_loader.exe;
  // empty when the loader isn't named <prefix>loader.exe
  static QString filePrefix(const MOBase::ScriptExtender& extender);

  // its name for people: SKSE64, F4SE, NVSE...
  static QString displayName(const MOBase::ScriptExtender& extender);

  // the game versions in the names of the extender's runtime DLLs:
  // skse64_1_6_1170.dll is for 1.6.1170
  static QStringList runtimeVersions(const QStringList& fileNames, const QString& prefix);

  // whether one of the versions is the game's: 1.6.1170 for 1.6.1170.0, 1.4
  // (New Vegas) for 1.4.0.525, 1.2.416 (Oblivion) for 1.2.0.416
  static bool matchesGame(const QStringList& versions, const QString& gameVersion);

  // the extender's files in the game folder
  static QStringList installedFiles(const QDir& gameDir, const QString& prefix);

  // the archive's loader, at its root or in its only folder
  static std::optional<Layout> detect(std::shared_ptr<MOBase::IFileTree> tree,
                                      const MOBase::ScriptExtender& extender);

  static Choice ask(QWidget* parent, const MOBase::ScriptExtender& extender,
                    const MOBase::IPluginGame& game, const Layout& layout);

  // copies the extracted files (file name, extracted path) into the game
  // folder; the extender's files there that the new version doesn't have go
  // to the Recycle Bin. Returns an error for people, empty when it worked.
  static QString copyToGame(const QList<QPair<QString, QString>>& files,
                            const QDir& gameDir, const QString& prefix,
                            QStringList* removed);

  // after Myself, or when the copy failed (problem says why): which files go
  // where, with a button to the game folder
  static void showManualSteps(QWidget* parent, const MOBase::ScriptExtender& extender,
                              const QDir& gameDir, const QStringList& fileNames,
                              const QString& problem = {});

  // the record a mod keeps of the files copied into the game folder; other
  // mods' records for that folder go, as the files are this mod's now
  static void writeRecord(const QString& modPath, const QDir& gameDir,
                          const QStringList& fileNames);

  // the recorded files still in the game folder (absolute paths); none when
  // the record is for another game folder than the extender's
  static QStringList recordedFiles(const QString& modPath,
                                   const MOBase::ScriptExtender& extender);

  // the folder of the extender's loader: where its files go
  static QDir loaderDir(const MOBase::ScriptExtender& extender);

  // sends the files to the Recycle Bin; returns the ones that couldn't go
  static QStringList removeFiles(const QStringList& paths);

  // before the game starts through the loader: false when the extender is for
  // another game version and the user doesn't start it anyway
  static bool confirmLaunch(QWidget* parent, const MOBase::ScriptExtender& extender,
                            const MOBase::IPluginGame& game);

  // sizes a dialog with wrapped texts to the height they take at width
  static void fitHeight(QDialog& dialog, int width);

  // whether path is the extender's loader
  static bool isLoader(const QString& path, const MOBase::ScriptExtender& extender);
};

#endif  // SCRIPTEXTENDERSETUP_H
