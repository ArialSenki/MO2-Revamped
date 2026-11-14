#ifndef COLLECTIONMANIFEST_H
#define COLLECTIONMANIFEST_H

// MO2 Revamped: Nexus collections. What a collection's collection.json says
// (the format Vortex writes, see Vortex's collections/types/ICollection.ts),
// collection links, and the stable orders built from the collection's rules.
// Qt Core only, so it can be tested on its own.

#include <QByteArray>
#include <QJsonValue>
#include <QString>
#include <QStringList>
#include <functional>
#include <optional>
#include <utility>
#include <vector>

namespace Collections
{

// a collection on Nexus; revision -1 is the latest published one
struct CollectionLink
{
  QString game;  // Nexus domain ("stardewvalley"); empty when the text had none
  QString slug;
  int revision = -1;
};

// a collection's slug: letters, digits, "_" and "-" only; it names files
bool isValidSlug(const QString& slug);

// nxm://<game>/collections/<slug>/revisions/<n>
bool isCollectionNxmLink(const QString& url);

// the nxm link above, a collection page on the website
// (https://www.nexusmods.com/games/<game>/collections/<slug>, the older
// next.nexusmods.com/<game>/collections/<slug> and nexusmods.com/<game>/...)
// or a bare slug; nothing for anything else
std::optional<CollectionLink> parseCollectionLink(const QString& text);

enum class SourceType
{
  Nexus,
  Bundle,
  Direct,
  Browse,
  Manual,
  Unknown
};

SourceType sourceTypeFromString(const QString& s);
QString sourceTypeToString(SourceType t);

// a reference in a mod rule (Vortex's IModReference); any of these may be empty
struct ModReference
{
  int modId  = 0;
  int fileId = 0;
  QString tag;
  QString md5;
  QString logicalFileName;
  QString fileExpression;
  QString versionMatch;
  QString description;
};

struct CollectionMod
{
  int index = -1;  // in mods[]
  QString name;
  QString version;
  QString author;
  QString domainName;
  bool optional = false;
  int phase     = 0;

  SourceType type = SourceType::Unknown;
  int modId       = 0;
  int fileId      = 0;
  QString url;
  QString sourceInstructions;
  QString md5;
  QString logicalFilename;
  QString fileExpression;
  QString tag;
  QString updatePolicy;
  qint64 fileSize = 0;

  // the instructions the curator wrote for this mod
  QString instructions;

  // the category the curator's Vortex had for it (details.category), a name
  QString category;

  // the installer choices, as collection.json has them (null when none)
  QJsonValue choices;
  bool hasPatches       = false;
  bool hasHashes        = false;
  bool hasFileOverrides = false;

  // the choices as compact JSON, empty when there are none
  QString choicesJson() const;
};

struct ModRule
{
  QString type;  // before, after, requires, conflicts, recommends, provides
  ModReference source;
  ModReference reference;
};

struct PluginEntry
{
  QString name;
  bool enabled = true;
};

struct PluginRule
{
  QString name;
  QStringList after;
};

struct CollectionManifest
{
  QString author;
  QString authorUrl;
  QString name;
  QString description;
  QString installInstructions;
  QString domainName;
  QStringList gameVersions;

  std::vector<CollectionMod> mods;
  std::vector<ModRule> modRules;

  // Bethesda games; hasPlugins is false when collection.json has no list
  bool hasPlugins = false;
  std::vector<PluginEntry> plugins;
  std::vector<PluginRule> pluginRules;

  bool recommendNewProfile = false;

  // nothing, with the reason in error, when the text isn't a collection
  static std::optional<CollectionManifest> parse(const QByteArray& json,
                                                 QString* error = nullptr);

  // the indexes of mods[] by phase, then in their order
  std::vector<int> installOrder() const;

  // the mod a rule's reference names, -1 when none does
  int findMod(const ModReference& ref) const;
};

// a stable topological sort: nodes keep their order unless an edge (a, b),
// "a before b", moves them. A cycle is broken inside its strongly connected
// component only: at the component's earliest node, by dropping the edges
// into it from the component, which are listed in broken.
struct SortResult
{
  std::vector<int> order;
  std::vector<std::pair<int, int>> broken;
  // the edges the order keeps: all of them but the broken ones
  std::vector<std::pair<int, int>> kept;
};

SortResult stableTopologicalSort(const std::vector<int>& nodes,
                                 const std::vector<std::pair<int, int>>& edges);

// the mods given (indexes of mods[], in their current order) sorted by the
// before/after mod rules among them
SortResult sortModsByRules(const CollectionManifest& manifest,
                           const std::vector<int>& mods);

// the sorted mods grouped by category: each group where its first mod is, its
// mods in their order. grouped is false, and order the one given, when the
// grouping would break one of the kept edges. category() gives a node's
// group, empty for none (those make a group of their own, named empty)
struct GroupResult
{
  bool grouped = false;
  std::vector<std::pair<QString, std::vector<int>>> groups;
  std::vector<int> order;
};

GroupResult groupByCategory(const SortResult& sorted,
                            const std::function<QString(int)>& category);

// the plugins given, in their current order, sorted by the rules' "after"
// lists; names compare without case. cycles gets "a > b" for each rule that
// was dropped.
QStringList sortPluginsByRules(const QStringList& plugins,
                               const std::vector<PluginRule>& rules,
                               QStringList* cycles = nullptr);

}  // namespace Collections

#endif  // COLLECTIONMANIFEST_H
