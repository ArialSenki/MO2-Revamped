// MO2 Revamped: Bethesda game framework, the script extender warnings.
// Copyright (C) 2026 MO2 Revamped. GPL-3.0-or-later, like Mod Organizer 2.
#include "extenderdiagnose.h"

#include "core/bethesdagame.h"
#include "extender/extenderchecks.h"

#include <imoinfo.h>

namespace mo2r::bethesda
{

ExtenderDiagnose::ExtenderDiagnose(const BethesdaGame* game, unsigned int runtimeKey,
                                   unsigned int libraryKey, const ExtenderProblemTexts& texts,
                                   QStringView setting)
    : m_Game(game), m_RuntimeKey(runtimeKey), m_LibraryKey(libraryKey), m_Texts(texts),
      m_Setting(setting)
{}

void ExtenderDiagnose::addActiveProblems(std::vector<unsigned int>& problems) const
{
  MOBase::IOrganizer* organizer = m_Game->organizer();
  if (organizer == nullptr || organizer->managedGame() != m_Game ||
      !organizer->pluginSetting(m_Game->name(), m_Setting.toString()).toBool()) {
    return;
  }
  if (extenderRuntimeMissing(m_Game, nullptr)) {
    problems.push_back(m_RuntimeKey);
  }
  if (addressLibraryMissing(m_Game, nullptr)) {
    problems.push_back(m_LibraryKey);
  }
}

QString ExtenderDiagnose::shortDescription(unsigned int key) const
{
  if (key == m_RuntimeKey) {
    return m_Game->translate(m_Texts.runtimeShort);
  }
  if (key == m_LibraryKey) {
    return m_Game->translate(m_Texts.libraryShort);
  }
  return {};
}

QString ExtenderDiagnose::fullDescription(unsigned int key) const
{
  QString expected;
  if (key == m_RuntimeKey) {
    extenderRuntimeMissing(m_Game, &expected);
    return m_Game->translate(m_Texts.runtimeFull).arg(expected, m_Game->gameVersion());
  }
  if (key == m_LibraryKey) {
    addressLibraryMissing(m_Game, &expected);
    return m_Game->translate(m_Texts.libraryFull).arg(expected, m_Game->gameVersion());
  }
  return {};
}

}  // namespace mo2r::bethesda
