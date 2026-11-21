// MO2 Revamped: Bethesda game framework, a game described as data.
// Copyright (C) 2026 MO2 Revamped. GPL-3.0-or-later, like Mod Organizer 2.
//
// Each game plugin is mostly one constexpr GameDefinition; BethesdaGame
// implements MO2's IPluginGame from it, and the feature modules (plugins,
// saves, extender, archives, ini, content) read their part of it. A game
// overrides a BethesdaGame function only where its behaviour can't be said
// as data.
//
// The identity fields can never change once a game has shipped: instances
// store the game by gameName, plugin settings by pluginName, and Nexus, Steam,
// Wabbajack and collections use the short names and IDs.
#pragma once

#include <QStringView>

#include <cstdint>
#include <span>

namespace mo2r::bethesda
{

using Names = std::span<const QStringView>;

enum class Store
{
  Bethesda,  // HKLM\<bethesdaKey> "Installed Path"
  Steam,     // the Steam library that has steamAppId
  Gog,       // HKLM\Software\GOG.com\Games\<gogId> "path"
  Epic       // the Epic Games launcher's manifests
};

enum class LoadOrderKind
{
  FileTime,       // plugins.txt lists the active plugins; the order is file times
  PluginsTxtStar  // plugins.txt lists all, '*' marks the active ones
};

enum class SaveFormat
{
  None,       // not read: the Saves tab stays empty
  Fallout3,   // FO3SAVEGAME as Fallout 3 writes it
  FalloutNV,  // FO3SAVEGAME with New Vegas' language block
  SkyrimSE,   // TESV_SAVEGAME, the body LZ4 or zlib (Skyrim 2011's too)
  Fallout4    // FO4_SAVEGAME
};

// a file in the game folder that tells the variant (store) the game is from
struct VariantMarker
{
  QStringView file;
  QStringView variant;
};

// a variant whose My Games and AppData folders have another name
struct VariantFolder
{
  QStringView variant;
  QStringView folder;
};

// a Steam app the game may be installed as, besides the main one (Fallout 3's
// first release next to its Game of the Year edition)
struct SteamApp
{
  QStringView appId;
  QStringView folder;  // under steamapps/common
};

struct StoreLocations
{
  std::span<const Store> order;  // tried in this order; the first found wins
  QStringView bethesdaKey;
  QStringView steamAppId;
  QStringView steamFolder;  // under steamapps/common
  // tried after steamAppId, in this order
  std::span<const SteamApp> moreSteamApps;
  // not empty: if no Steam library lists the app, a library's
  // steamapps/common/<steamFolder> with this file is taken too (MO2's
  // findSteamGame, how Fallout 4's plugin looked)
  QStringView steamValidFile;
  Names gogIds;  // tried in this order
  Names epicAppNames;
  // the Epic install location holds the game in a subfolder matching this
  QStringView epicSubfolder;
};

struct ExtenderDefinition
{
  QStringView loader;           // in the game folder: "nvse_loader.exe"
  QStringView pluginPath;       // in Data: "nvse/plugins"
  QStringView coSaveExtension;  // next to each save: "nvse"
  // the runtime DLL the loader looks for is this + the game version
  // ("skse64_" -> skse64_1_6_1170.dll); empty: no check
  QStringView runtimePrefix;
  // in Data: where the Address Library for the extender's plugins goes
  // ("SKSE/Plugins"); empty: no check
  QStringView addressLibraryFolder;
  // the Address Library's file names start with one of these, the first is
  // the one named in the warning ("version-" for F4SE); empty: "versionlib-"
  // (Skyrim SE 1.6) and "version-"
  Names addressLibraryPrefixes;
};

struct ArchiveDefinition
{
  QStringView iniFile;  // the profile or My Games INI with the [Archive] lists
  QStringView listKey;  // "SArchiveList"
  int maxListLength;    // characters read from each key
  // a second key the list goes on in once it's longer than splitLength
  // (Skyrim SE: SResourceArchiveList2 after 255 characters); empty: one key
  QStringView listKey2;
  int splitLength;
  Names vanilla;
  QStringView invalidationBsa;      // empty: no BSA invalidation
  std::uint32_t invalidationBsaVersion;  // 0x67 Oblivion, 0x68 the others
  // not empty: invalidation without a BSA (Fallout 4): bInvalidateOlderFiles=1
  // and an empty sResourceDataDirsFinal in this INI
  QStringView resourceDirsIni;
};

// MO2's content ids for Bethesda games (the mod list's content column and
// filters); the values are MO2's and must stay
enum class Content : int
{
  Plugin,
  Optional,
  Texture,
  Mesh,
  Bsa,
  Interface,
  Sound,
  Script,
  ExtenderPlugin,
  ExtenderFiles,
  SkyProc,
  Mcm,
  Ini,
  FaceGen,
  ModGroup,
  Material,  // Fallout 4's (its old plugin's CONTENT_NEXT_VALUE)
  Count
};

constexpr std::uint32_t contentBit(Content c)
{
  return 1u << static_cast<int>(c);
}

struct DataLayout
{
  Names folders;     // a mod with one of these at its top is data
  Names extensions;  // or with a file of one of these
  std::uint32_t disabledContents;  // contentBit()s the game has no use for
  bool materialsContent;  // Content::Material is listed (Fallout 4)
};

// an official plugin shown as a mod under another name (Fallout 4's DLC)
struct ModDisplayName
{
  QStringView mod;  // the plugin's name without its extension
  QStringView name;
};

struct UnmanagedDefinition
{
  QStringView archiveExtension;  // the mods' archives; ".bsa" when empty
  std::span<const ModDisplayName> displayNames;  // matched ignoring case
};

enum class ExecutableKind
{
  GameFolder,      // binary is relative to the game folder
  Launcher,        // getLauncherName(), in the game folder
  ScriptExtender,  // the script extender's loader
  Loot             // LOOT, wherever it's installed
};

struct ExecutableDefinition
{
  QStringView title;
  QStringView binary;
  ExecutableKind kind;
  QStringView argument;
  bool epicPortal;  // gets -EpicPortal on the Epic variant
  bool notOnEpic;   // left out on the Epic variant
  QStringView steamAppId = {};  // Steam starts it as this app (the Creation Kit)
};

struct BoolSetting
{
  QStringView key;
  const char* description;  // source text, translated in the game's context
  bool defaultValue;
};

struct GameDefinition
{
  // --- identity (see the header comment) ---
  QStringView pluginName;  // name(): "Fallout NV Support Plugin"
  // the context of the plugin's translations, the old plugin's class name, so
  // its .qm files still apply
  const char* translationContext;
  QStringView displayName;  // in "%1 support" and "Adds support for %1."
  QStringView author;
  int version[3];
  QStringView gameName;
  QStringView shortName;
  Names validShortNames;
  QStringView nexusName;
  int nexusGameId;
  int nexusModOrganizerId;
  Names variants;
  std::span<const VariantMarker> variantMarkers;  // the first found wins
  QStringView defaultVariant;                     // when no marker is found

  // --- files ---
  QStringView binary;
  QStringView launcher;
  QStringView dataFolder;     // in the game folder
  QStringView myGamesFolder;  // also the AppData folder with plugins.txt
  std::span<const VariantFolder> variantFolders;
  // the profile's plugin lists are also mapped to the myGamesFolder AppData
  // folder when the variant has its own (New Vegas' Epic edition); otherwise
  // only to the variant's
  bool listsAlsoInDefaultAppData;
  // a new profile gets the AppData loadorder.txt too, not only plugins.txt
  // (Fallout 3)
  bool profileGetsLoadOrderTxt;
  Names iniFiles;            // the first is the game's main INI
  QStringView defaultIni;    // in the game folder: the main INI's defaults
  Names primaryPlugins;
  Names dlcPlugins;
  // in the game folder: the Creation Club plugins, one per line; they load
  // after the primary ones and count as such ("Skyrim.ccc"); empty: none
  QStringView ccFile;
  LoadOrderKind loadOrder;
  bool lightPlugins;  // .esl and the ESL flag (Skyrim SE, Fallout 4)
  QStringView lootSortingSetting;  // LOOT sorting only when this setting is on

  // --- saves ---
  SaveFormat saveFormat;
  QStringView saveExtension;
  QStringView localSavesIni;  // where sLocalSavePath goes for profile saves

  // --- features ---
  ExtenderDefinition extender;
  ArchiveDefinition archives;
  DataLayout dataLayout;
  UnmanagedDefinition unmanaged;
  std::span<const ExecutableDefinition> executables;
  StoreLocations stores;
  std::span<const BoolSetting> settings;
};

}  // namespace mo2r::bethesda
