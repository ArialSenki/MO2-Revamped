// MO2 Revamped: Bethesda game framework, script extender checks.
// Copyright (C) 2026 MO2 Revamped. GPL-3.0-or-later, like Mod Organizer 2.
// The runtime and Address Library file names are the extenders' own (SKSE,
// F4SE) and meh321's (Address Library: "versionlib-" for Skyrim SE 1.6, the
// Anniversary Edition, "version-" before it).
#include "extenderchecks.h"

#include "core/bethesdagame.h"

#include <igamefeatures.h>
#include <imodinterface.h>
#include <imodlist.h>
#include <imoinfo.h>
#include <scriptextender.h>

#include <QFileInfo>
#include <QStringList>

namespace mo2r::bethesda
{

QString joinedVersion(const QString& version, int parts, const QString& separator)
{
  // MO2's placeholder for an exe without a usable version
  if (version.isEmpty() || version.startsWith(u"1.0.0")) {
    return {};
  }
  QStringList numbers = version.split('.', Qt::SkipEmptyParts);
  if (numbers.isEmpty()) {
    return {};
  }
  while (numbers.size() < parts) {
    numbers.append("0");
  }
  return numbers.mid(0, parts).join(separator);
}

bool extenderRuntimeMissing(const BethesdaGame* game, QString* expected)
{
  const QString prefix = game->definition().extender.runtimePrefix.toString();
  MOBase::IOrganizer* organizer = game->organizer();
  if (prefix.isEmpty() || organizer == nullptr) {
    return false;
  }
  const auto extender = organizer->gameFeatures()->gameFeature<MOBase::ScriptExtender>();
  if (extender == nullptr || !extender->isInstalled()) {
    return false;
  }
  const QString version = joinedVersion(game->gameVersion(), 3, "_");
  if (version.isEmpty()) {
    return false;
  }
  const QString runtime = prefix + version + ".dll";
  if (expected != nullptr) {
    *expected = runtime;
  }
  if (game->gameDirectory().exists(runtime)) {
    return false;
  }
  // an active mod's Root/ folder puts it in the game folder at launch
  MOBase::IModList* mods = organizer->modList();
  for (const QString& name : mods->allModsByProfilePriority()) {
    if ((mods->state(name) & MOBase::IModList::STATE_ACTIVE) == 0) {
      continue;
    }
    const MOBase::IModInterface* mod = mods->getMod(name);
    if (mod != nullptr && QFileInfo::exists(mod->absolutePath() + "/Root/" + runtime)) {
      return false;
    }
  }
  return true;
}

bool addressLibraryMissing(const BethesdaGame* game, QString* expected)
{
  const QString folder = game->definition().extender.addressLibraryFolder.toString();
  MOBase::IOrganizer* organizer = game->organizer();
  if (folder.isEmpty() || organizer == nullptr) {
    return false;
  }
  const QString version = joinedVersion(game->gameVersion(), 4, "-");
  if (version.isEmpty()) {
    return false;
  }
  // a DLL there doesn't mean it needs the Address Library (many don't): only
  // an Address Library for other versions says the list uses it
  if (organizer->findFiles(folder, QStringList{"*.dll"}).isEmpty()) {
    return false;
  }
  QStringList prefixes;
  for (const QStringView p : game->definition().extender.addressLibraryPrefixes) {
    prefixes.append(p.toString());
  }
  if (prefixes.isEmpty()) {
    prefixes = {"versionlib-", "version-"};
  }
  QStringList anyVersion;
  QStringList names;
  for (const QString& p : prefixes) {
    anyVersion.append(p + "*.bin");
    names.append(p + version + ".bin");
  }
  if (organizer->findFiles(folder, anyVersion).isEmpty()) {
    return false;
  }
  if (expected != nullptr) {
    *expected = folder + "/" + names.first();
  }
  return organizer->findFiles(folder, names).isEmpty();
}

}  // namespace mo2r::bethesda
