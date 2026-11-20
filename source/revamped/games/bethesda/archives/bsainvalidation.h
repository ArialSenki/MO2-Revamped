// MO2 Revamped: Bethesda game framework, BSA (archive) invalidation.
// Copyright (C) 2026 MO2 Revamped. GPL-3.0-or-later, like Mod Organizer 2.
//
// MO2's BSAInvalidation feature for Oblivion, Fallout 3 and New Vegas. With
// the profile's invalidation on: bInvalidateOlderFiles=1, the dummy BSA
// first in the archive list and in Data, and an empty SInvalidationFile. Off:
// the dummy BSA removed from both, SInvalidationFile back to
// ArchiveInvalidation.txt. The same INI keys and values MO2 always wrote.
#pragma once

#include <bsainvalidation.h>

namespace MOBase
{
class DataArchives;
}

namespace mo2r::bethesda
{

class BethesdaGame;

class DummyBsaInvalidation : public MOBase::BSAInvalidation
{
public:
  DummyBsaInvalidation(MOBase::DataArchives* archives, const BethesdaGame* game)
      : m_Archives(archives), m_Game(game)
  {}

  bool isInvalidationBSA(const QString& bsaName) override;
  void deactivate(MOBase::IProfile* profile) override { prepareProfile(profile); }
  void activate(MOBase::IProfile* profile) override { prepareProfile(profile); }
  // true if anything changed
  bool prepareProfile(MOBase::IProfile* profile) override;

private:
  QString bsaName() const;

  MOBase::DataArchives* m_Archives;
  const BethesdaGame* m_Game;
};

}  // namespace mo2r::bethesda
