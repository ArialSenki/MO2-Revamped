#include "collectionmanifest.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QHash>
#include <queue>
#include <QRegularExpression>
#include <memory>
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
    mod.deployType       = m.value("details").toObject().value("type").toString().trimmed();
    mod.choices          = m.value("choices");
    mod.hasPatches       = hasContent(m.value("patches"));
    const QJsonObject patches = m.value("patches").toObject();
    for (auto it = patches.begin(); it != patches.end(); ++it) {
      if (!it.key().isEmpty()) {
        mod.patches.emplace_back(it.key(), it.value().toString());
      }
    }
    for (const auto& v : m.value("hashes").toArray()) {
      const QJsonObject o = v.toObject();
      const QString path  = o.value("path").toString();
      if (!path.isEmpty()) {
        mod.hashes.push_back({path, o.value("md5").toString().toLower()});
      }
    }
    for (const auto& v : m.value("fileOverrides").toArray()) {
      if (!v.toString().isEmpty()) {
        mod.fileOverrides.append(v.toString());
      }
    }
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

  const QJsonObject config = root.value("collectionConfig").toObject();
  c.recommendNewProfile    = config.value("recommendNewProfile").toBool();
  c.excludePluginRules     = config.value("excludePluginRules").toBool();

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

struct ModFinder
{
  // in mods[] order, so the first match is the same as a scan's
  QHash<int, std::vector<int>> byFileId;
  QHash<QString, int> byTag;
  QHash<QString, int> byMd5;
  QHash<QString, int> byExpression;
  QHash<QString, std::vector<int>> byLogicalName;
  QHash<QString, int> byName;
};

const ModFinder& CollectionManifest::finder() const
{
  if (!m_finder || m_finderSize != mods.size()) {
    auto f = std::make_shared<ModFinder>();
    for (const auto& m : mods) {
      f->byFileId[m.fileId].push_back(m.index);
      if (!f->byTag.contains(m.tag)) {
        f->byTag.insert(m.tag, m.index);
      }
      const QString md5 = m.md5.toCaseFolded();
      if (!f->byMd5.contains(md5)) {
        f->byMd5.insert(md5, m.index);
      }
      if (!m.fileExpression.isEmpty()) {
        const QString e = m.fileExpression.toCaseFolded();
        if (!f->byExpression.contains(e)) {
          f->byExpression.insert(e, m.index);
        }
      }
      f->byLogicalName[m.logicalFilename.toCaseFolded()].push_back(m.index);
      const QString name = m.name.toCaseFolded();
      if (!f->byName.contains(name)) {
        f->byName.insert(name, m.index);
      }
    }
    m_finder     = std::move(f);
    m_finderSize = mods.size();
  }
  return *m_finder;
}

int CollectionManifest::findMod(const ModReference& ref) const
{
  const ModFinder& f = finder();

  if (ref.fileId > 0) {
    const auto it = f.byFileId.constFind(ref.fileId);
    if (it != f.byFileId.constEnd()) {
      for (int index : it.value()) {
        if (ref.modId <= 0 || mods[index].modId == ref.modId) {
          return index;
        }
      }
    }
  }

  if (!ref.tag.isEmpty()) {
    const auto it = f.byTag.constFind(ref.tag);
    if (it != f.byTag.constEnd()) {
      return it.value();
    }
  }

  if (!ref.md5.isEmpty()) {
    const auto it = f.byMd5.constFind(ref.md5.toCaseFolded());
    if (it != f.byMd5.constEnd()) {
      return it.value();
    }
  }

  if (!ref.fileExpression.isEmpty()) {
    const auto it = f.byExpression.constFind(ref.fileExpression.toCaseFolded());
    if (it != f.byExpression.constEnd()) {
      return it.value();
    }
  }

  // another version of the same file: the one with the version asked for, or
  // the only one
  if (!ref.logicalFileName.isEmpty()) {
    const auto it = f.byLogicalName.constFind(ref.logicalFileName.toCaseFolded());
    if (it != f.byLogicalName.constEnd() && !it.value().empty()) {
      for (int index : it.value()) {
        if (!ref.versionMatch.isEmpty() && mods[index].version == ref.versionMatch) {
          return index;
        }
      }
      return it.value().front();
    }

    // a bundled mod is referred to by its name
    const auto byName = f.byName.constFind(ref.logicalFileName.toCaseFolded());
    if (byName != f.byName.constEnd()) {
      return byName.value();
    }
  }

  return -1;
}

namespace
{

// the strongly connected components of the graph among the active nodes
// (Tarjan's algorithm, without recursion: a plugin list can be long); each
// component's nodes by position. Successor lists are kept ascending, so the
// walk is the same as over ordered sets
std::vector<std::vector<int>> stronglyConnected(const std::vector<std::vector<int>>& next,
                                                const std::vector<char>& active)
{
  const int n = static_cast<int>(next.size());
  std::vector<int> index(n, -1), low(n, 0);
  std::vector<char> onStack(n, 0);
  std::vector<int> stack;
  std::vector<std::vector<int>> components;
  int counter = 0;

  struct Frame
  {
    int node;
    size_t it;
  };

  std::vector<Frame> frames;
  for (int start = 0; start < n; ++start) {
    if (!active[start] || index[start] >= 0) {
      continue;
    }
    auto enter = [&](int v) {
      index[v] = low[v] = counter++;
      stack.push_back(v);
      onStack[v] = 1;
      frames.push_back({v, 0});
    };
    enter(start);

    while (!frames.empty()) {
      Frame& f    = frames.back();
      const int v = f.node;
      if (f.it < next[v].size()) {
        const int w = next[v][f.it];
        ++f.it;
        if (!active[w]) {
          continue;
        }
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
          onStack[w] = 0;
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

void eraseSorted(std::vector<int>& list, int value)
{
  const auto it = std::lower_bound(list.begin(), list.end(), value);
  if (it != list.end() && *it == value) {
    list.erase(it);
  }
}

}  // namespace

SortResult stableTopologicalSort(const std::vector<int>& nodes,
                                 const std::vector<std::pair<int, int>>& edges)
{
  SortResult result;
  const int n = static_cast<int>(nodes.size());

  QHash<int, int> position;
  position.reserve(n);
  for (int i = 0; i < n; ++i) {
    if (!position.contains(nodes[i])) {
      position.insert(nodes[i], i);
    }
  }

  // by position: the successors and predecessors of each node, ascending and
  // without repeats
  std::vector<std::vector<int>> next(n), prev(n);
  for (const auto& [a, b] : edges) {
    const auto ia = position.constFind(a);
    const auto ib = position.constFind(b);
    if (ia == position.constEnd() || ib == position.constEnd() || ia.value() == ib.value()) {
      continue;
    }
    next[ia.value()].push_back(ib.value());
    prev[ib.value()].push_back(ia.value());
  }
  for (int i = 0; i < n; ++i) {
    std::sort(next[i].begin(), next[i].end());
    next[i].erase(std::unique(next[i].begin(), next[i].end()), next[i].end());
    std::sort(prev[i].begin(), prev[i].end());
    prev[i].erase(std::unique(prev[i].begin(), prev[i].end()), prev[i].end());
  }

  // cycles are broken only inside the components that have them: at the
  // component's earliest node, by dropping the edges into it from the
  // component. Until no component has a cycle left. Breaking edges only
  // splits components, so each pass looks only at the last pass's cyclic ones
  std::vector<char> active(n, 1);
  for (;;) {
    bool cyclic = false;
    std::vector<char> nextActive(n, 0);
    for (const auto& component : stronglyConnected(next, active)) {
      if (component.size() < 2) {
        continue;
      }
      cyclic      = true;
      const int p = component.front();
      for (int m : component) {
        nextActive[m] = 1;
      }
      std::vector<int> keep;
      keep.reserve(prev[p].size());
      for (int from : prev[p]) {
        if (std::binary_search(component.begin(), component.end(), from)) {
          result.broken.emplace_back(nodes[from], nodes[p]);
          eraseSorted(next[from], p);
        } else {
          keep.push_back(from);
        }
      }
      prev[p] = std::move(keep);
    }
    if (!cyclic) {
      break;
    }
    active = std::move(nextActive);
  }

  for (int a = 0; a < n; ++a) {
    for (int b : next[a]) {
      result.kept.emplace_back(nodes[a], nodes[b]);
    }
  }

  // Kahn's algorithm, always taking the earliest node that is free
  std::vector<int> waiting(n);
  std::priority_queue<int, std::vector<int>, std::greater<int>> ready;
  for (int i = 0; i < n; ++i) {
    waiting[i] = static_cast<int>(prev[i].size());
    if (waiting[i] == 0) {
      ready.push(i);
    }
  }

  result.order.reserve(n);
  while (!ready.empty()) {
    const int p = ready.top();
    ready.pop();
    result.order.push_back(nodes[p]);
    for (int s : next[p]) {
      if (--waiting[s] == 0) {
        ready.push(s);
      }
    }
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
                            const std::function<QString(int)>& category,
                            const std::function<int(int)>& position,
                            const std::vector<std::pair<int, int>>* conflictPairs)
{
  GroupResult result;
  const int n = static_cast<int>(sorted.order.size());

  // past this size the analysis is skipped: the order the rules give, flat
  if (n > MaxGroupedMods) {
    result.order = sorted.order;
    return result;
  }

  // nodes by their place in the sorted order, which is a topological order of
  // the kept edges: every kept edge goes forward in it
  QHash<int, int> at;
  at.reserve(n);
  for (int i = 0; i < n; ++i) {
    at.insert(sorted.order[i], i);
  }
  struct Edge
  {
    int u, v;
  };
  std::vector<Edge> kept;
  kept.reserve(sorted.kept.size());
  for (const auto& [a, b] : sorted.kept) {
    const auto ia = at.constFind(a);
    const auto ib = at.constFind(b);
    if (ia != at.constEnd() && ib != at.constEnd()) {
      kept.push_back({ia.value(), ib.value()});
    }
  }

  // only the rules on a path between two mods that share a file change what
  // the game sees: A before B before C, with A and C sharing one, keeps both
  std::vector<Edge> edges;
  if (conflictPairs == nullptr) {
    edges = kept;
  } else {
    // bit sets: what each node reaches (itself included), and the files it
    // shares with nodes it reaches
    const int words = (n + 63) / 64;
    using Bits      = std::vector<quint64>;
    auto test       = [](const quint64* b, int i) {
      return (b[i >> 6] >> (i & 63)) & 1u;
    };
    auto set = [](quint64* b, int i) {
      b[i >> 6] |= quint64(1) << (i & 63);
    };
    Bits desc(static_cast<size_t>(n) * words, 0);
    Bits shares(static_cast<size_t>(n) * words, 0);
    std::vector<std::vector<int>> next(n), prev(n);
    for (const auto& e : kept) {
      next[e.u].push_back(e.v);
      prev[e.v].push_back(e.u);
    }
    for (int i = n - 1; i >= 0; --i) {
      quint64* d = &desc[static_cast<size_t>(i) * words];
      set(d, i);
      for (int j : next[i]) {
        const quint64* s = &desc[static_cast<size_t>(j) * words];
        for (int w = 0; w < words; ++w) {
          d[w] |= s[w];
        }
      }
    }
    for (const auto& [a, b] : *conflictPairs) {
      const auto ia = at.constFind(a);
      const auto ib = at.constFind(b);
      if (ia == at.constEnd() || ib == at.constEnd() || ia.value() == ib.value()) {
        continue;
      }
      const int x = ia.value(), y = ib.value();
      if (test(&desc[static_cast<size_t>(x) * words], y)) {
        set(&shares[static_cast<size_t>(x) * words], y);
      }
      if (test(&desc[static_cast<size_t>(y) * words], x)) {
        set(&shares[static_cast<size_t>(y) * words], x);
      }
    }
    // what the node and every node before it share with nodes after them
    for (int u = 0; u < n; ++u) {
      quint64* c = &shares[static_cast<size_t>(u) * words];
      for (int p : prev[u]) {
        const quint64* s = &shares[static_cast<size_t>(p) * words];
        for (int w = 0; w < words; ++w) {
          c[w] |= s[w];
        }
      }
    }
    for (const auto& e : kept) {
      const quint64* c = &shares[static_cast<size_t>(e.u) * words];
      const quint64* d = &desc[static_cast<size_t>(e.v) * words];
      for (int w = 0; w < words; ++w) {
        if (c[w] & d[w]) {
          edges.push_back(e);
          break;
        }
      }
    }
  }

  // the collection's own order (its phases and mods[]), else the sorted one
  std::vector<int> pos(n);
  for (int i = 0; i < n; ++i) {
    pos[i] = position ? position(sorted.order[i]) : i;
  }

  // each node's group, by name without case
  std::vector<int> groupOf(n);
  std::vector<QString> names(n);
  QHash<QString, int> groupIds;
  int groupCount = 0;
  for (int i = 0; i < n; ++i) {
    names[i]          = category(sorted.order[i]);
    const QString key = names[i].toCaseFolded();
    auto it           = groupIds.constFind(key);
    if (it == groupIds.constEnd()) {
      it = groupIds.insert(key, groupCount++);
    }
    groupOf[i] = it.value();
  }
  std::vector<std::vector<int>> members(groupCount);
  for (int i = 0; i < n; ++i) {
    members[groupOf[i]].push_back(i);
  }
  std::vector<std::vector<std::pair<int, int>>> inside(groupCount);
  for (const auto& e : edges) {
    if (groupOf[e.u] == groupOf[e.v]) {
      inside[groupOf[e.u]].emplace_back(e.u, e.v);
    }
  }

  // each pass moves the one mod a rule puts out of order to the group at the
  // end; only the group that lost it is ordered again
  std::vector<bool> moved(n, false);
  std::vector<std::vector<int>> groupOrder(groupCount);
  std::vector<bool> dirty(groupCount, true);
  std::vector<int> place(n);

  for (;;) {
    // the groups in the order their first mod comes
    std::vector<int> groupsInOrder;
    std::vector<bool> seen(groupCount, false);
    for (int i = 0; i < n; ++i) {
      if (!moved[i] && !seen[groupOf[i]]) {
        seen[groupOf[i]] = true;
        groupsInOrder.push_back(groupOf[i]);
      }
    }

    // in each group the collection's order, moved only by the rules inside it
    for (int g : groupsInOrder) {
      if (!dirty[g]) {
        continue;
      }
      std::vector<int> list;
      for (int i : members[g]) {
        if (!moved[i]) {
          list.push_back(i);
        }
      }
      std::stable_sort(list.begin(), list.end(), [&pos](int a, int b) {
        return pos[a] < pos[b];
      });
      groupOrder[g] = stableTopologicalSort(list, inside[g]).order;
      dirty[g]      = false;
    }

    int k = 0;
    for (int g : groupsInOrder) {
      for (int i : groupOrder[g]) {
        place[i] = k++;
      }
    }
    for (int i = 0; i < n; ++i) {
      if (moved[i]) {
        place[i] = k++;
      }
    }

    // a rule the grouping breaks: the mod that must come later goes to the
    // end, after everything it has to follow
    int toMove = -1;
    for (const auto& e : edges) {
      if (place[e.u] > place[e.v]) {
        // within the end group the sorted order holds every rule
        toMove = moved[e.v] ? e.u : e.v;
        break;
      }
    }
    if (toMove >= 0) {
      moved[toMove]          = true;
      dirty[groupOf[toMove]] = true;
      continue;
    }

    for (int g : groupsInOrder) {
      // a group is named after its first mod that stayed in it
      QString name;
      for (int i : members[g]) {
        if (!moved[i]) {
          name = names[i];
          break;
        }
      }
      std::vector<int> nodes;
      nodes.reserve(groupOrder[g].size());
      for (int i : groupOrder[g]) {
        nodes.push_back(sorted.order[i]);
      }
      result.groups.push_back({name, std::move(nodes)});
    }
    for (int i = 0; i < n; ++i) {
      if (moved[i]) {
        result.overflow.push_back(sorted.order[i]);
      }
    }
    result.order.reserve(n);
    for (const auto& g : result.groups) {
      result.order.insert(result.order.end(), g.second.begin(), g.second.end());
    }
    result.order.insert(result.order.end(), result.overflow.begin(),
                        result.overflow.end());
    result.grouped = true;
    return result;
  }
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
