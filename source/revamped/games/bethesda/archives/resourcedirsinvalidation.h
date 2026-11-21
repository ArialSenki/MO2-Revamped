// MO2 Revamped: Bethesda game framework, archive invalidation without a BSA.
// Copyright (C) 2026 MO2 Revamped. GPL-3.0-or-later, like Mod Organizer 2.
//
// MO2's BSAInvalidation feature for Fallout 4: no dummy archive. With the
// profile's invalidation on, the definition's resourceDirsIni gets
// bInvalidateOlderFiles=1 and an empty sResourceDataDirsFinal (the game then
// lets loose files win over its archives everywhere, not only in STRINGS\);
// with it off nothing is written, as MO2's game_fallout4 did. No archive is
// ever an invalidation BSA.
#pragma once

#include <bsainvalidation.h>

namespace mo2r::bethesda
{

class BethesdaGame;

class ResourceDirsInvalidation : public MOBase::BSAInvalidation
{
public:
  explicit ResourceDirsInvalidation(const BethesdaGame* game) : m_Game(game) {}

  bool isInvalidationBSA(const QString&) override { return false; }
  void deactivate(MOBase::IProfile* profile) override { prepareProfile(profile); }
  void activate(MOBase::IProfile* profile) override { prepareProfile(profile); }
  // true if the INI changed
  bool prepareProfile(MOBase::IProfile* profile) override;

private:
  const BethesdaGame* m_Game;
};

}  // namespace mo2r::bethesda
