#include "collectionmanifest.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QSet>
#include <algorithm>
#include <map>
#include <set>

namespace Collections
{

namespace
{

const QRegularExpression& nxmCollectionExp()
{
  static const QRegularExpression exp(
      QStringLiteral("^nxm://([A-Za-z0-9_-]+)/collections/([A-Za-z0-9_-]+)/revisions/"
                     "(\\d+)(?:[/?#].*)?$"),
      QRegularExpression::CaseInsensitiveOption);
  return exp;
}

// numbers in collection.json and in rule references are sometimes strings
int toInt(const QJsonValue& v)
{
  if (v.isDouble()) {
    return v.toInt();
  }
  if (v.isString()) {
    return v.toString().toInt();
  }
  return 0;
}

qint64 toInt64(const QJsonValue& v)
{
  if (v.isDouble()) {
    return static_cast<qint64>(v.toDouble());
  }
  if (v.isString()) {
    return v.toString().toLongLong();
  }
  return 0;
}

bool hasContent(const QJsonValue& v)
{
  if (v.isObject()) {
    return !v.toObject().isEmpty();
  }
  if (v.isArray()) {
    return !v.toArray().isEmpty();
  }
  return !v.isNull() && !v.isUndefined();
}

ModReference parseReference(const QJsonObject& o)
{
  ModReference r;
  r.tag             = o.value("tag").toString();
  r.md5             = o.value("fileMD5").toString();
  r.logicalFileName = o.value("logicalFileName").toString();
  r.fileExpression  = o.value("fileExpression").toString();
  r.versionMatch    = o.value("versionMatch").toString();
  r.description     = o.value("description").toString();

  const QJsonObject repo = o.value("repo").toObject();
  if (!repo.isEmpty()) {
    r.modId  = toInt(repo.value("modId"));
    r.fileId = toInt(repo.value("fileId"));
  }
  return r;
}

// a plugin rule's "after" entry: a name or {name, display}
QString pluginRefName(const QJsonValue& v)
{
  if (v.isString()) {
    return v.toString();
  }
  return v.toObject().value("name").toString();
}

}  // namespace

bool isValidSlug(const QString& slug)
{
  static const QRegularExpression exp(QStringLiteral("^[A-Za-z0-9_-]+$"));
  return slug.size() <= 64 && exp.match(slug).hasMatch();
}

bool isCollectionNxmLink(const QString& url)
{
  return nxmCollectionExp().match(url.trimmed()).hasMatch();
}

std::optional<CollectionLink> parseCollectionLink(const QString& text)
{
  const QString t = text.trimmed();
  if (t.isEmpty()) {
    return {};
  }

  if (auto m = nxmCollectionExp().match(t); m.hasMatch()) {
    return CollectionLink{m.captured(1), m.captured(2), m.captured(3).toInt()};
  }

  // the website: nexusmods.com/games/<game>/collections/<slug> and the older
  // next.nexusmods.com/<game>/collections/<slug>, with or without the scheme
  static const QRegularExpression web(
      QStringLiteral("^(?:https?://)?(?:www\\.|next\\.)?nexusmods\\.com/"
                     "(?:games/)?([A-Za-z0-9_-]+)/collections/([A-Za-z0-9_-]+)"
                     "(?:/revisions/(\\d+))?(?:[/?#].*)?$"),
      QRegularExpression::CaseInsensitiveOption);
  if (auto m = web.match(t); m.hasMatch()) {
    CollectionLink link{m.captured(1), m.captured(2), -1};
    if (!m.captured(3).isEmpty()) {
      link.revision = m.captured(3).toInt();
    }
    return link;
  }

  // a bare slug, for the managed game
  static const QRegularExpression slug(QStringLiteral("^[A-Za-z0-9]{4,16}$"));
  if (slug.match(t).hasMatch()) {
    return CollectionLink{QString(), t, -1};
  }

  return {};
}

SourceType sourceTypeFromString(const QString& s)
{
  const QString l = s.toLower();
  if (l == "nexus") {
    return SourceType::Nexus;
  } else if (l == "bundle") {
    return SourceType::Bundle;
  } else if (l == "direct") {
    return SourceType::Direct;
  } else if (l == "browse") {
    return SourceType::Browse;
  } else if (l == "manual") {
    return SourceType::Manual;
  }
  return SourceType::Unknown;
}

QString sourceTypeToString(SourceType t)
{
  switch (t) {
  case SourceType::Nexus:
    return QStringLiteral("nexus");
  case SourceType::Bundle:
    return QStringLiteral("bundle");
  case SourceType::Direct:
    return QStringLiteral("direct");
  case SourceType::Browse:
    return QStringLiteral("browse");
  case SourceType::Manual:
    return QStringLiteral("manual");
  case SourceType::Unknown:
    break;
  }
  return QStringLiteral("unknown");
}

QString CollectionMod::choicesJson() const
{
  if (!hasContent(choices)) {
    return {};
  }
  if (choices.isObject()) {
    return QString::fromUtf8(
        QJsonDocument(choices.toObject()).toJson(QJsonDocument::Compact));
  }
  if (choices.isArray()) {
    return QString::fromUtf8(
        QJsonDocument(choices.toArray()).toJson(QJsonDocument::Compact));
  }
  return {};
}

std::optional<CollectionManifest> CollectionManifest::parse(const QByteArray& json,
                                                            QString* error)
{
  QJsonParseError pe;
  const QJsonDocument doc = QJsonDocument::fromJson(json, &pe);
  if (pe.error != QJsonParseError::NoError) {
    if (error) {
      *error = pe.errorString();
    }
    return {};
  }
  if (!doc.isObject()) {
    if (error) {
      *error = QStringLiteral("not an object");
    }
    return {};
  }

  const QJsonObject root = doc.object();
  if (!root.value("info").isObject() || !root.value("mods").isArray()) {
    if (error) {
      *error = QStringLiteral("no info or mods");
    }
    return {};
  }

  CollectionManifest c;

  const QJsonObject info = root.value("info").toObject();
  c.author               = info.value("author").toString();
  c.authorUrl            = info.value("authorUrl").toString();
  c.name                 = info.value("name").toString();
  c.description          = info.value("description").toString();
  c.installInstructions  = info.value("installInstructions").toString();
  c.domainName           = info.value("domainName").toString();
  for (const auto& v : info.value("gameVersions").toArray()) {
    c.gameVersions.append(v.toString());
  }

  const QJsonArray mods = root.value("mods").toArray();
  c.mods.reserve(mods.size());
  for (int i = 0; i < mods.size(); ++i) {
    const QJsonObject m = mods[i].toObject();
    const QJsonObject s = m.value("source").toObject();

    CollectionMod mod;
    mod.index      = i;
    mod.name       = m.value("name").toString().trimmed();
    mod.version    = m.value("version").toString();
    mod.author     = m.value("author").toString();
    mod.domainName = m.value("domainName").toString();
    mod.optional   = m.value("optional").toBool();
    mod.phase      = toInt(m.value("phase"));

    mod.type               = sourceTypeFromString(s.value("type").toString());
    mod.modId              = toInt(s.value("modId"));
    mod.fileId             = toInt(s.value("fileId"));
    mod.url                = s.value("url").toString();
    mod.sourceInstructions = s.value("instructions").toString();
    mod.md5                = s.value("md5").toString();
    mod.logicalFilename    = s.value("logicalFilename").toString();
    mod.fileExpression     = s.value("fileExpression").toString();
    mod.tag                = s.value("tag").toString();
    mod.updatePolicy       = s.value("updatePolicy").toString();
    mod.fileSize           = toInt64(s.value("fileSize"));

    mod.instructions     = m.value("instructions").toString();
    mod.category         = m.value("details").toObject().value("category").toString().trimmed();
    mod.choices          = m.value("choices");
    mod.hasPatches       = hasContent(m.value("patches"));
    mod.hasHashes        = hasContent(m.value("hashes"));
    mod.hasFileOverrides = hasContent(m.value("fileOverrides"));

    if (mod.name.isEmpty()) {
      mod.name = !mod.logicalFilename.isEmpty() ? mod.logicalFilename
                                                : QStringLiteral("Mod %1").arg(i + 1);
    }

    c.mods.push_back(std::move(mod));
  }

  for (const auto& v : root.value("modRules").toArray()) {
    const QJsonObject r = v.toObject();
    ModRule rule;
    rule.type      = r.value("type").toString();
    rule.source    = parseReference(r.value("source").toObject());
    rule.reference = parseReference(r.value("reference").toObject());
    c.modRules.push_back(std::move(rule));
  }

  if (root.value("plugins").isArray()) {
    c.hasPlugins = true;
    for (const auto& v : root.value("plugins").toArray()) {
      const QJsonObject p = v.toObject();
      PluginEntry e;
      e.name    = p.value("name").toString();
      e.enabled = p.value("enabled").toBool(true);
      if (!e.name.isEmpty()) {
        c.plugins.push_back(e);
      }
    }
  }

  for (const auto& v :
       root.value("pluginRules").toObject().value("plugins").toArray()) {
    const QJsonObject p = v.toObject();
    PluginRule r;
    r.name = p.value("name").toString();
    for (const auto& a : p.value("after").toArray()) {
      const QString n = pluginRefName(a);
      if (!n.isEmpty()) {
        r.after.append(n);
      }
    }
    if (!r.name.isEmpty() && !r.after.isEmpty()) {
      c.pluginRules.push_back(std::move(r));
    }
  }

  c.recommendNewProfile =
      root.value("collectionConfig").toObject().value("recommendNewProfile").toBool();

  return c;
}

std::vector<int> CollectionManifest::installOrder() const
{
  std::vector<int> order;
  order.reserve(mods.size());
  for (const auto& m : mods) {
    order.push_back(m.index);
  }
  std::stable_sort(order.begin(), order.end(), [this](int a, int b) {
    return mods[a].phase < mods[b].phase;
  });
  return order;
}

int CollectionManifest::findMod(const ModReference& ref) const
{
  if (ref.fileId > 0) {
    for (const auto& m : mods) {
      if (m.fileId == ref.fileId && (ref.modId <= 0 || m.modId == ref.modId)) {
        return m.index;
      }
    }
  }

  if (!ref.tag.isEmpty()) {
    for (const auto& m : mods) {
      if (m.tag == ref.tag) {
        return m.index;
      }
    }
  }

  if (!ref.md5.isEmpty()) {
    for (const auto& m : mods) {
      if (m.md5.compare(ref.md5, Qt::CaseInsensitive) == 0) {
        return m.index;
      }
    }
  }

  if (!ref.fileExpression.isEmpty()) {
    for (const auto& m : mods) {
      if (!m.fileExpression.isEmpty() &&
          m.fileExpression.compare(ref.fileExpression, Qt::CaseInsensitive) == 0) {
        return m.index;
      }
    }
  }

  // another version of the same file: the one with the version asked for, or
  // the only one
  if (!ref.logicalFileName.isEmpty()) {
    int found = -1;
    for (const auto& m : mods) {
      if (m.logicalFilename.compare(ref.logicalFileName, Qt::CaseInsensitive) == 0) {
        if (!ref.versionMatch.isEmpty() && m.version == ref.versionMatch) {
          return m.index;
        }
        if (found < 0) {
          found = m.index;
        }
      }
    }
    if (found >= 0) {
      return found;
    }

    // a bundled mod is referred to by its name
    for (const auto& m : mods) {
      if (m.name.compare(ref.logicalFileName, Qt::CaseInsensitive) == 0) {
        return m.index;
      }
    }
  }

  return -1;
}

namespace
{

// the strongly connected components of the graph (Tarjan's algorithm, without
// recursion: a plugin list can be long); each component's nodes by position
std::vector<std::vector<int>> stronglyConnected(const std::vector<std::set<int>>& next)
{
  const int n = static_cast<int>(next.size());
  std::vector<int> index(n, -1), low(n, 0);
  std::vector<bool> onStack(n, false);
  std::vector<int> stack;
  std::vector<std::vector<int>> components;
  int counter = 0;

  struct Frame
  {
    int node;
    std::set<int>::const_iterator it;
  };

  for (int start = 0; start < n; ++start) {
    if (index[start] >= 0) {
      continue;
    }
    std::vector<Frame> frames;
    auto enter = [&](int v) {
      index[v] = low[v] = counter++;
      stack.push_back(v);
      onStack[v] = true;
      frames.push_back({v, next[v].begin()});
    };
    enter(start);

    while (!frames.empty()) {
      Frame& f = frames.back();
      const int v = f.node;
      if (f.it != next[v].end()) {
        const int w = *f.it;
        ++f.it;
        if (index[w] < 0) {
          enter(w);
        } else if (onStack[w]) {
          low[v] = std::min(low[v], index[w]);
        }
        continue;
      }

      if (low[v] == index[v]) {
        std::vector<int> component;
        int w = -1;
        do {
          w = stack.back();
          stack.pop_back();
          onStack[w] = false;
          component.push_back(w);
        } while (w != v);
        std::sort(component.begin(), component.end());
        components.push_back(std::move(component));
      }
      frames.pop_back();
      if (!frames.empty()) {
        const int parent = frames.back().node;
        low[parent]      = std::min(low[parent], low[v]);
      }
    }
  }

  return components;
}

}  // namespace

SortResult stableTopologicalSort(const std::vector<int>& nodes,
                                 const std::vector<std::pair<int, int>>& edges)
{
  SortResult result;

  std::map<int, int> position;
  for (int i = 0; i < static_cast<int>(nodes.size()); ++i) {
    position.emplace(nodes[i], i);
  }

  // by position: the successors and predecessors of each node
  std::vector<std::set<int>> next(nodes.size());
  std::vector<std::set<int>> prev(nodes.size());
  for (const auto& [a, b] : edges) {
    auto ia = position.find(a);
    auto ib = position.find(b);
    if (ia == position.end() || ib == position.end() || ia->second == ib->second) {
      continue;
    }
    next[ia->second].insert(ib->second);
    prev[ib->second].insert(ia->second);
  }

  // cycles are broken only inside the components that have them: at the
  // component's earliest node, by dropping the edges into it from the
  // component. Until no component has a cycle left
  for (;;) {
    bool cyclic = false;
    for (const auto& component : stronglyConnected(next)) {
      if (component.size() < 2) {
        continue;
      }
      cyclic        = true;
      const int p   = component.front();
      const std::set<int> members(component.begin(), component.end());
      for (auto it = prev[p].begin(); it != prev[p].end();) {
        if (members.count(*it)) {
          result.broken.emplace_back(nodes[*it], nodes[p]);
          next[*it].erase(p);
          it = prev[p].erase(it);
        } else {
          ++it;
        }
      }
    }
    if (!cyclic) {
      break;
    }
  }

  for (int a = 0; a < static_cast<int>(nodes.size()); ++a) {
    for (int b : next[a]) {
      result.kept.emplace_back(nodes[a], nodes[b]);
    }
  }

  // Kahn's algorithm, always taking the earliest node that is free
  std::set<int> ready;
  for (int i = 0; i < static_cast<int>(nodes.size()); ++i) {
    if (prev[i].empty()) {
      ready.insert(i);
    }
  }

  while (!ready.empty()) {
    const int p = *ready.begin();
    ready.erase(ready.begin());
    result.order.push_back(nodes[p]);

    for (int n : next[p]) {
      prev[n].erase(p);
      if (prev[n].empty()) {
        ready.insert(n);
      }
    }
    next[p].clear();
  }

  return result;
}

SortResult sortModsByRules(const CollectionManifest& manifest,
                           const std::vector<int>& mods)
{
  std::vector<std::pair<int, int>> edges;
  for (const auto& rule : manifest.modRules) {
    const bool before = rule.type.compare("before", Qt::CaseInsensitive) == 0;
    const bool after  = rule.type.compare("after", Qt::CaseInsensitive) == 0;
    if (!before && !after) {
      continue;
    }
    const int s = manifest.findMod(rule.source);
    const int r = manifest.findMod(rule.reference);
    if (s < 0 || r < 0 || s == r) {
      continue;
    }
    // "a before b": a is lower in priority, so b's files win
    if (before) {
      edges.emplace_back(s, r);
    } else {
      edges.emplace_back(r, s);
    }
  }

  // the same rule listed for two versions of a file counts once
  std::sort(edges.begin(), edges.end());
  edges.erase(std::unique(edges.begin(), edges.end()), edges.end());

  return stableTopologicalSort(mods, edges);
}

GroupResult groupByCategory(const SortResult& sorted,
                            const std::function<QString(int)>& category)
{
  GroupResult result;

  // the groups in the order their first mod comes; names compare without case
  std::vector<std::pair<QString, std::vector<int>>> groups;
  for (int node : sorted.order) {
    const QString name = category(node);
    auto it = std::find_if(groups.begin(), groups.end(), [&name](const auto& g) {
      return g.first.compare(name, Qt::CaseInsensitive) == 0;
    });
    if (it == groups.end()) {
      groups.push_back({name, {node}});
    } else {
      it->second.push_back(node);
    }
  }

  std::vector<int> order;
  for (const auto& g : groups) {
    order.insert(order.end(), g.second.begin(), g.second.end());
  }

  // every rule the sort kept still holds, or nothing is grouped
  std::map<int, int> position;
  for (int i = 0; i < static_cast<int>(order.size()); ++i) {
    position[order[i]] = i;
  }
  for (const auto& [a, b] : sorted.kept) {
    auto ia = position.find(a);
    auto ib = position.find(b);
    if (ia != position.end() && ib != position.end() && ia->second > ib->second) {
      result.order = sorted.order;
      return result;
    }
  }

  result.grouped = true;
  result.groups  = std::move(groups);
  result.order   = std::move(order);
  return result;
}

QStringList sortPluginsByRules(const QStringList& plugins,
                               const std::vector<PluginRule>& rules,
                               QStringList* cycles)
{
  std::map<QString, int> byName;
  std::vector<int> nodes;
  for (int i = 0; i < plugins.size(); ++i) {
    byName.emplace(plugins[i].toLower(), i);
    nodes.push_back(i);
  }

  std::vector<std::pair<int, int>> edges;
  for (const auto& rule : rules) {
    auto p = byName.find(rule.name.toLower());
    if (p == byName.end()) {
      continue;
    }
    for (const auto& a : rule.after) {
      auto q = byName.find(a.toLower());
      if (q != byName.end()) {
        edges.emplace_back(q->second, p->second);
      }
    }
  }

  const SortResult sorted = stableTopologicalSort(nodes, edges);

  QStringList result;
  for (int i : sorted.order) {
    result.append(plugins[i]);
  }
  if (cycles) {
    for (const auto& [a, b] : sorted.broken) {
      cycles->append(QStringLiteral("%1 > %2").arg(plugins[a], plugins[b]));
    }
  }
  return result;
}

}  // namespace Collections
