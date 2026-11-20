// MO2 Revamped: Bethesda game framework, what a mod's data looks like.
// Copyright (C) 2026 MO2 Revamped. GPL-3.0-or-later, like Mod Organizer 2.
//
// MO2's ModDataChecker (is an archive's top level the game's Data folder?)
// and ModDataContent (the mod list's content column), from the game's
// DataLayout.
#pragma once

#include <ifiletree.h>
#include <moddatachecker.h>
#include <moddatacontent.h>

#include <set>

namespace MOBase
{
class IGameFeatures;
}

namespace mo2r::bethesda
{

struct GameDefinition;

class BethesdaDataChecker : public MOBase::ModDataChecker
{
public:
  explicit BethesdaDataChecker(const GameDefinition& definition);

  // valid with a known folder or file type at the top, or with a Root/ folder
  // (files for the game's own folder, Root Builder's layout) and no Data/
  CheckReturn
  dataLooksValid(std::shared_ptr<const MOBase::IFileTree> fileTree) const override;

private:
  std::set<QString, MOBase::FileNameComparator> m_Folders;
  std::set<QString, MOBase::FileNameComparator> m_Extensions;
};

class BethesdaDataContent : public MOBase::ModDataContent
{
public:
  BethesdaDataContent(const GameDefinition& definition,
                      const MOBase::IGameFeatures* gameFeatures);

  std::vector<Content> getAllContents() const override;
  std::vector<int>
  getContentsFor(std::shared_ptr<const MOBase::IFileTree> fileTree) const override;

private:
  bool enabled(int content) const;

  std::uint32_t m_Disabled;
  const MOBase::IGameFeatures* m_GameFeatures;
};

}  // namespace mo2r::bethesda
