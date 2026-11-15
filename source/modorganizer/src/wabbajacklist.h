#ifndef WABBAJACKLIST_H
#define WABBAJACKLIST_H

// MO2 Revamped: reading a Wabbajack modlist (.wabbajack).
//
// A .wabbajack is a zip holding "modlist" (the list as JSON), the gallery
// image and the files the list carries itself (named by GUID). The JSON names
// the archives the list is built from (where each one comes from, its
// xxHash64 and size) and the directives that build the install folder from
// them. The format is Wabbajack's (wabbajack-tools/wabbajack, GPLv3); this is
// an implementation of our own that reads it.
//
// The JSON of a large list is hundreds of MB, so it is read as a stream and
// kept in compact structures; the files the list carries stay in the zip.

#include <QByteArray>
#include <QCoreApplication>
#include <QHash>
#include <QString>
#include <QStringList>

#include <atomic>
#include <cstdint>
#include <functional>
#include <optional>
#include <vector>

namespace MOBase
{
class IPluginGame;
}

namespace Wabbajack
{

// ---- hashes ---------------------------------------------------------------

// xxHash64, seed 0, as Wabbajack uses for every file
class XxHash64
{
public:
  XxHash64();
  void update(const void* data, std::size_t size);
  uint64_t digest() const;

  static uint64_t of(const void* data, std::size_t size);

private:
  uint64_t m_v[4];
  uint64_t m_total = 0;
  unsigned char m_buffer[32];
  std::size_t m_buffered = 0;
};

// Wabbajack writes a hash as the Base64 of its 8 bytes, little-endian;
// 0 when the text isn't one
uint64_t hashFromBase64(QStringView text, bool* ok = nullptr);
QString hashToBase64(uint64_t hash);

// the hash of a whole file; nullopt when it can't be read or when cancel is set.
// progress gets the bytes read so far
std::optional<uint64_t> hashFile(const QString& path,
                                 const std::atomic_bool* cancel = nullptr,
                                 const std::function<void(qint64)>& progress = {});

// ---- the list -------------------------------------------------------------

// a path to a .wabbajack file that exists (from the command line, say)
bool isListFile(const QString& path);

enum class Source
{
  Nexus,
  GameFile,  // taken from the game's own folder, by hash
  Http,
  WabbajackCdn,
  GoogleDrive,
  Mega,
  MediaFire,
  ModDB,
  LoversLab,  // and the other IPS4 sites (Vector Plexus, TES Alliance)
  Manual,
  Bethesda,
  Unknown
};

// whether MO2 Revamped can get the archive by itself (once the phase that
// does it is there), or the user has to fetch it in a browser
bool isAutomatic(Source s);

struct Archive
{
  uint64_t hash = 0;
  QString name;  // the file name in the downloads folder
  qint64 size = 0;
  QString meta;  // the .meta the list writes for it

  Source source = Source::Unknown;
  QString sourceType;  // the $type as written, for the unknown ones

  // Nexus
  QString nexusGame;  // Wabbajack's game name (GameName)
  qint64 modId  = 0;
  qint64 fileId = 0;
  QString modName;
  QString author;
  QString version;

  // the URL for Http, the CDN, Mega, MediaFire, ModDB, Manual, the IPS4 sites;
  // the file id for Google Drive
  QString url;
  QString prompt;  // Manual: the author's words
  QStringList headers;  // Http: "Name: value" the site wants

  // a game file
  QString game;
  QString gameFile;  // relative to the game's folder, e.g. Data\Skyrim.esm
  QString gameVersion;

  // the archive is a release of Mod Organizer itself (it puts ModOrganizer.exe
  // in the root): MO2 Revamped never installs from it
  bool isModOrganizer = false;

  // how many directives take something from it
  int uses = 0;
};

enum class DirectiveType
{
  FromArchive,
  PatchedFromArchive,
  TransformedTexture,
  InlineFile,
  RemappedInlineFile,
  ArchiveMeta,
  PropertyFile,
  CreateBSA,
  MergedPatch,
  Unknown
};

constexpr int DirectiveTypeCount = int(DirectiveType::Unknown) + 1;

struct Directive
{
  DirectiveType type = DirectiveType::Unknown;
  uint64_t hash      = 0;
  qint64 size        = 0;
  QString to;  // relative to the install folder, with backslashes

  // FromArchive and the types built on it: the archive's hash and the path
  // inside it, then inside an archive in it, and so on
  uint64_t archiveHash = 0;
  QStringList archivePath;

  // InlineFile, RemappedInlineFile, ArchiveMeta, PropertyFile: SourceDataID;
  // PatchedFromArchive and MergedPatch: PatchID; CreateBSA: TempID
  QString dataId;
  uint64_t fromHash = 0;  // PatchedFromArchive: the file the patch applies to

  // CreateBSA's State and FileStates, TransformedTexture's ImageState and
  // MergedPatch's Sources, as written (compact JSON), for the phases that use them
  QByteArray extra;

  // a file of Mod Organizer itself: never installed (see Archive::isModOrganizer)
  bool modOrganizerFile = false;
};

struct TypeCount
{
  int count  = 0;
  qint64 size = 0;
};

struct ModList
{
  QString name;
  QString author;
  QString description;
  QString version;
  QString wabbajackVersion;
  QString game;  // Wabbajack's name, e.g. SkyrimSpecialEdition
  QStringList otherGames;
  QString website;
  QString readme;
  QString community;
  bool nsfw = false;
  QByteArray image;  // modlist-image.png, empty when there is none

  std::vector<Archive> archives;
  QHash<uint64_t, int> archiveByHash;  // hash -> index in archives

  // only when read with keepDirectives
  std::vector<Directive> directives;

  // counted while reading, kept or not
  TypeCount byType[DirectiveTypeCount];
  int directiveCount = 0;
  // the directives that install Mod Organizer itself (not installed by us)
  TypeCount modOrganizerFiles;
  // what the other directives write, about what the install takes on disk
  // (the files that go into BSAs are counted once, for the BSA)
  qint64 installSize = 0;
  // the profiles the list brings (profiles\<name>\...)
  QStringList profiles;
  // MO2 plugins the list adds from archives other than Mod Organizer's own
  QStringList extraPlugins;
  // directive and source types this doesn't know
  QStringList unknownTypes;

  qint64 downloadSize() const;
};

// reads a .wabbajack; nullopt and the reason in *error when it can't.
// The directives are kept only when keepDirectives (the installer needs them;
// showing the list needs only the counts)
// setting cancel stops it at the next block of the JSON
std::optional<ModList> readModList(const QString& path, QString* error,
                                   bool keepDirectives            = false,
                                   const std::atomic_bool* cancel = nullptr);

// ---- games ----------------------------------------------------------------

struct GameInfo
{
  QString wabbajackName;  // SkyrimSpecialEdition
  QString nexusDomain;    // skyrimspecialedition
  QString mo2Name;        // Skyrim Special Edition, as MO2's game plugins call it
};

// what Wabbajack knows of a game; nullopt for a name it doesn't have
std::optional<GameInfo> gameInfo(const QString& wabbajackName);

// the MO2 game plugin for a list's game, or nullptr
MOBase::IPluginGame* findGamePlugin(const QString& wabbajackName,
                                    const std::vector<MOBase::IPluginGame*>& plugins);

}  // namespace Wabbajack

#endif  // WABBAJACKLIST_H
