#include <ifiletree.h>

#include "gamebryomoddatachecker.h"

/**
 * @return the list of possible folder names in data.
 */
auto GamebryoModDataChecker::possibleFolderNames() const -> const FileNameSet&
{
  static FileNameSet result{"fonts",
                            "interface",
                            "menus",
                            "meshes",
                            "music",
                            "scripts",
                            "shaders",
                            "sound",
                            "strings",
                            "textures",
                            "trees",
                            "video",
                            "facegen",
                            "materials",
                            "skse",
                            "obse",
                            "mwse",
                            "nvse",
                            "fose",
                            "f4se",
                            "distantlod",
                            "asi",
                            "SkyProc Patchers",
                            "Tools",
                            "MCM",
                            "icons",
                            "bookart",
                            "distantland",
                            "mits",
                            "splash",
                            "dllplugins",
                            "CalienteTools",
                            "NetScriptFramework",
                            "shadersfx"};
  return result;
}

/**
 * @return the extensions of possible files in data.
 */
auto GamebryoModDataChecker::possibleFileExtensions() const -> const FileNameSet&
{
  static FileNameSet result{"esp", "esm", "esl", "bsa", "ba2", "modgroups", "ini"};
  return result;
}

GamebryoModDataChecker::GamebryoModDataChecker(const GameGamebryo* game) : m_Game(game)
{}

GamebryoModDataChecker::CheckReturn GamebryoModDataChecker::dataLooksValid(
    std::shared_ptr<const MOBase::IFileTree> fileTree) const
{
  auto& folders  = possibleFolderNames();
  auto& suffixes = possibleFileExtensions();
  for (auto entry : *fileTree) {
    if (entry->isDir()) {
      // MO2 Revamped: Root/ is Root Builder's layout (files for the game's own
      // folder, as Wabbajack lists install them), which MO2 Revamped maps to
      // the game's folder; for every game of this family, whatever its list.
      // Not with a Data/ next to it: that archive still needs its data folder
      // chosen (Quick Install would take it as it is)
      if (entry->compare(QStringLiteral("Root")) == 0 &&
          fileTree->find(QStringLiteral("Data"), MOBase::IFileTree::DIRECTORY) ==
              nullptr) {
        return CheckReturn::VALID;
      }
      if (folders.count(entry->name()) > 0) {
        return CheckReturn::VALID;
      }
    } else {
      if (suffixes.count(entry->suffix()) > 0) {
        return CheckReturn::VALID;
      }
    }
  }
  return CheckReturn::INVALID;
}
