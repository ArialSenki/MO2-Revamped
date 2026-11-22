// MO2 Revamped: Bethesda game framework, what a mod's data looks like.
// Copyright (C) 2026 MO2 Revamped. GPL-3.0-or-later, like Mod Organizer 2.
// The content ids, names and icons, and the folders behind each, are MO2's
// (game_gamebryo's GamebryoModDataContent): the mod list and its saved
// filters use them.
#include "datachecker.h"

#include "core/gamedefinition.h"

#include <igamefeatures.h>
#include <scriptextender.h>

#include <QtGlobal>

namespace mo2r::bethesda
{

namespace
{
  template <typename Set>
  Set toSet(Names names)
  {
    Set out;
    for (const QStringView n : names) {
      out.insert(n.toString());
    }
    return out;
  }

  constexpr int id(Content c)
  {
    return static_cast<int>(c);
  }
}  // namespace

BethesdaDataChecker::BethesdaDataChecker(const GameDefinition& definition)
    : m_Folders(toSet<decltype(m_Folders)>(definition.dataLayout.folders)),
      m_Extensions(toSet<decltype(m_Extensions)>(definition.dataLayout.extensions)),
      m_FixablePrefix(definition.dataLayout.fixablePrefix.toString()),
      m_FixFolder(definition.dataLayout.fixFolder.toString())
{}

BethesdaDataChecker::CheckReturn BethesdaDataChecker::dataLooksValid(
    std::shared_ptr<const MOBase::IFileTree> fileTree) const
{
  for (const auto& entry : *fileTree) {
    if (entry->isDir()) {
      // MO2 Revamped maps Root/ to the game folder (Wabbajack lists install
      // that way); with a Data/ next to it the archive still needs its data
      // folder chosen
      if (entry->compare(QStringLiteral("Root")) == 0 &&
          fileTree->find(QStringLiteral("Data"), MOBase::IFileTree::DIRECTORY) == nullptr) {
        return CheckReturn::VALID;
      }
      if (m_Folders.count(entry->name()) > 0) {
        return CheckReturn::VALID;
      }
    } else if (m_Extensions.count(entry->suffix()) > 0) {
      return CheckReturn::VALID;
    }
  }
  if (m_FixablePrefix.isEmpty()) {
    return CheckReturn::INVALID;
  }
  // only files, each starting with the prefix (an empty archive too, as MO2's
  // Oblivion plugin had it)
  for (const auto& entry : *fileTree) {
    if (entry->isDir() || !entry->name().startsWith(m_FixablePrefix, Qt::CaseInsensitive)) {
      return CheckReturn::INVALID;
    }
  }
  return CheckReturn::FIXABLE;
}

std::shared_ptr<MOBase::IFileTree>
BethesdaDataChecker::fix(std::shared_ptr<MOBase::IFileTree> fileTree) const
{
  if (m_FixFolder.isEmpty()) {
    return fileTree;
  }
  auto data   = fileTree->createOrphanTree();
  auto folder = data->addDirectory(m_FixFolder);
  folder->merge(fileTree);
  return data;
}

BethesdaDataContent::BethesdaDataContent(const GameDefinition& definition,
                                         const MOBase::IGameFeatures* gameFeatures)
    : m_Disabled(definition.dataLayout.disabledContents |
                 (definition.dataLayout.materialsContent ? 0u
                                                         : contentBit(bethesda::Content::Material))),
      m_GameFeatures(gameFeatures)
{}

bool BethesdaDataContent::enabled(int content) const
{
  return (m_Disabled & (1u << content)) == 0;
}

std::vector<BethesdaDataContent::Content> BethesdaDataContent::getAllContents() const
{
  // MO2's order in the content column
  const std::vector<Content> all{
      {id(bethesda::Content::Plugin), QT_TR_NOOP("Plugins (ESP/ESM/ESL)"),
       ":/MO/gui/content/plugin"},
      {id(bethesda::Content::Optional), QT_TR_NOOP("Optional Plugins"), "", true},
      {id(bethesda::Content::Interface), QT_TR_NOOP("Interface"), ":/MO/gui/content/interface"},
      {id(bethesda::Content::Mesh), QT_TR_NOOP("Meshes"), ":/MO/gui/content/mesh"},
      {id(bethesda::Content::Bsa), QT_TR_NOOP("Bethesda Archive"), ":/MO/gui/content/bsa"},
      {id(bethesda::Content::Script), QT_TR_NOOP("Scripts (Papyrus)"), ":/MO/gui/content/script"},
      {id(bethesda::Content::ExtenderPlugin), QT_TR_NOOP("Script Extender Plugin"),
       ":/MO/gui/content/skse"},
      {id(bethesda::Content::ExtenderFiles), QT_TR_NOOP("Script Extender Files"), "", true},
      {id(bethesda::Content::SkyProc), QT_TR_NOOP("SkyProc Patcher"), ":/MO/gui/content/skyproc"},
      {id(bethesda::Content::Sound), QT_TR_NOOP("Sound or Music"), ":/MO/gui/content/sound"},
      {id(bethesda::Content::Texture), QT_TR_NOOP("Textures"), ":/MO/gui/content/texture"},
      {id(bethesda::Content::Mcm), QT_TR_NOOP("MCM Configuration"), ":/MO/gui/content/menu"},
      {id(bethesda::Content::Ini), QT_TR_NOOP("INI Files"), ":/MO/gui/content/inifile"},
      {id(bethesda::Content::FaceGen), QT_TR_NOOP("FaceGen Data"), ":/MO/gui/content/facegen"},
      {id(bethesda::Content::ModGroup), QT_TR_NOOP("ModGroup Files"),
       ":/MO/gui/content/modgroup"},
      // Fallout 4's, last; its old plugin never had the name translated
      {id(bethesda::Content::Material), "Materials", ":/MO/gui/content/material"}};
  std::vector<Content> out;
  for (const Content& c : all) {
    if (enabled(c.id())) {
      out.push_back(c);
    }
  }
  return out;
}

std::vector<int> BethesdaDataContent::getContentsFor(
    std::shared_ptr<const MOBase::IFileTree> fileTree) const
{
  using C = bethesda::Content;
  const auto on = [this](C c) {
    return enabled(id(c));
  };
  std::vector<int> contents;
  for (const auto& e : *fileTree) {
    if (e->isFile()) {
      const QString suffix = e->suffix().toLower();
      if (on(C::Plugin) && (suffix == "esp" || suffix == "esm" || suffix == "esl")) {
        contents.push_back(id(C::Plugin));
      } else if (on(C::Bsa) && (suffix == "bsa" || suffix == "ba2")) {
        contents.push_back(id(C::Bsa));
      } else if (on(C::Ini) && suffix == "ini" && e->compare("meta.ini") != 0) {
        contents.push_back(id(C::Ini));
      } else if (on(C::ModGroup) && suffix == "modgroups") {
        contents.push_back(id(C::ModGroup));
      }
    } else {
      if (on(C::Texture) && (e->compare("textures") == 0 || e->compare("icons") == 0 ||
                             e->compare("bookart") == 0)) {
        contents.push_back(id(C::Texture));
      } else if (on(C::Mesh) && e->compare("meshes") == 0) {
        contents.push_back(id(C::Mesh));
      } else if (on(C::Interface) &&
                 (e->compare("interface") == 0 || e->compare("menus") == 0)) {
        contents.push_back(id(C::Interface));
      } else if (on(C::Sound) && (e->compare("music") == 0 || e->compare("sound") == 0)) {
        contents.push_back(id(C::Sound));
      } else if (on(C::Script) && e->compare("scripts") == 0) {
        contents.push_back(id(C::Script));
      } else if (on(C::SkyProc) && e->compare("SkyProc Patchers") == 0) {
        contents.push_back(id(C::SkyProc));
      } else if (on(C::Mcm) && e->compare("MCM") == 0) {
        contents.push_back(id(C::Mcm));
      } else if (on(C::Optional) && e->compare("Optional") == 0 && e->astree()->size() > 0) {
        contents.push_back(id(C::Optional));
      }
    }
  }

  if (on(C::FaceGen) && (fileTree->findDirectory("meshes/actors/character/facegendata") ||
                         fileTree->findDirectory("textures/actors/character/facegendata"))) {
    contents.push_back(id(C::FaceGen));
  }

  if (const auto extender = m_GameFeatures->gameFeature<MOBase::ScriptExtender>()) {
    if (const auto folder = fileTree->findDirectory(extender->PluginPath())) {
      if (on(C::ExtenderFiles)) {
        contents.push_back(id(C::ExtenderFiles));
      }
      if (on(C::ExtenderPlugin)) {
        for (const auto& f : *folder) {
          if (f->hasSuffix("dll")) {
            contents.push_back(id(C::ExtenderPlugin));
            break;
          }
        }
      }
    }
  }

  if (on(C::Material)) {
    for (const auto& e : *fileTree) {
      if (e->compare("materials") == 0) {
        contents.push_back(id(C::Material));
        break;
      }
    }
  }
  return contents;
}

}  // namespace mo2r::bethesda
