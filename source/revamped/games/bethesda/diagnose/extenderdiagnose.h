// MO2 Revamped: Bethesda game framework, the script extender warnings.
// Copyright (C) 2026 MO2 Revamped. GPL-3.0-or-later, like Mod Organizer 2.
//
// The part of a game plugin's IPluginDiagnose that reports extenderchecks'
// two problems: a script extender that doesn't match the game version and an
// Address Library left behind by a game update. Each game keeps its old
// plugin's problem keys and texts (source texts in the game's translation
// context, so its old .qm files still translate them); the game's own
// problems, if any, go next to these.
#pragma once

#include <QString>
#include <QStringView>

#include <vector>

namespace mo2r::bethesda
{

class BethesdaGame;

// source texts; each full text has %1 for the file expected and %2 for the
// game version
struct ExtenderProblemTexts
{
  const char* runtimeShort;
  const char* runtimeFull;
  const char* libraryShort;
  const char* libraryFull;
};

class ExtenderDiagnose
{
public:
  // setting: the game's bool setting that turns the checks on
  ExtenderDiagnose(const BethesdaGame* game, unsigned int runtimeKey, unsigned int libraryKey,
                   const ExtenderProblemTexts& texts,
                   QStringView setting = u"script_extender_checks");

  // the keys of the problems found, when the game is the one MO2 manages and
  // the setting is on
  void addActiveProblems(std::vector<unsigned int>& problems) const;

  // empty for a key that isn't one of these two
  QString shortDescription(unsigned int key) const;
  QString fullDescription(unsigned int key) const;

private:
  const BethesdaGame* m_Game;
  unsigned int m_RuntimeKey;
  unsigned int m_LibraryKey;
  ExtenderProblemTexts m_Texts;
  QStringView m_Setting;
};

}  // namespace mo2r::bethesda
