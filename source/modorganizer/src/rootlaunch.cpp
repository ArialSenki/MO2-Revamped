#include "rootlaunch.h"

#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QMap>
#include <QRegularExpression>
#include <QSet>

#include <algorithm>
#include <optional>

namespace RootLaunch
{

using RootCopy::info;

namespace
{

// one plan reads a DLL's headers and bytes several times (its imports, what
// it names, its companions): each file is read once per plan
struct PlanCache
{
  QHash<QString, RootCopy::PeFile> pe;
  QHash<QString, QByteArray> bytes;
};
thread_local PlanCache* g_cache = nullptr;

RootCopy::PeFile cachedPe(const QString& path)
{
  if (g_cache == nullptr) {
    return RootCopy::readPe(path);
  }
  const QString key = QDir::cleanPath(path).toLower();
  auto it           = g_cache->pe.find(key);
  if (it == g_cache->pe.end()) {
    it = g_cache->pe.insert(key, RootCopy::readPe(path));
  }
  return *it;
}

QStringList cachedMentions(const QString& path, const QStringList& names)
{
  if (names.isEmpty()) {
    return {};
  }
  if (g_cache == nullptr) {
    return RootCopy::mentionedNames(path, names);
  }
  const QString key = QDir::cleanPath(path).toLower();
  auto it           = g_cache->bytes.find(key);
  if (it == g_cache->bytes.end()) {
    it = g_cache->bytes.insert(key, RootCopy::loweredBytes(path));
  }
  return RootCopy::mentionedNames(*it, names);
}

}  // namespace

namespace
{

// DLLs loaded as proxies of system ones; in the copy fallback they go to the
// game folder even when the exe doesn't import them (a SteamStub-wrapped exe's
// import table is the wrapper's, a launcher's game isn't read)
const QStringList KnownProxies = {
    "d3d9.dll",     "d3d10.dll",    "d3d11.dll",     "d3d12.dll",    "dxgi.dll",
    "dinput8.dll",  "version.dll",  "winmm.dll",     "d3dx9_38.dll", "d3dx9_42.dll",
    "xinput1_3.dll", "dsound.dll",  "opengl32.dll",  "dwrite.dll"};

struct Entry
{
  QString name;
  QString source;
  int root = -1;
  bool dir = false;
};

// a folder's entries over all the Root folders, keyed in lower case; the
// highest priority one wins
using Index = QMap<QString, Entry>;

QString join(const QString& dir, const QString& name)
{
  // MO2 Revamped: a drive root ("D:/") already ends in its "/"
  return (dir.isEmpty() || dir == ".") ? name
         : dir.endsWith('/')            ? dir + name
                                        : dir + "/" + name;
}

// MO2 Revamped: a clean folder with one "/" at the end, what the paths under
// it start with: cleanPath keeps a drive root's "/" ("D:/"), and "D:/" + "/"
// was "D://", which no path starts with
QString under(const QString& dir)
{
  const QString clean = QDir::cleanPath(QDir::fromNativeSeparators(dir));
  return clean.endsWith('/') ? clean : clean + "/";
}

QString native(const QString& path)
{
  return QDir::toNativeSeparators(path);
}

Index indexDir(const QStringList& roots, const QString& dir)
{
  Index index;
  for (int i = 0; i < roots.size(); ++i) {
    const QDir d(join(roots[i], dir));
    if (!d.exists()) {
      continue;
    }
    const auto entries = d.entryInfoList(QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot |
                                         QDir::Hidden | QDir::System);
    for (const QFileInfo& fi : entries) {
      index.insert(fi.fileName().toLower(),
                   {fi.fileName(), fi.absoluteFilePath(), i, fi.isDir()});
    }
  }
  return index;
}

QStringList dllNames(const Index& index)
{
  QStringList names;
  for (auto it = index.begin(); it != index.end(); ++it) {
    if (!it->dir && it.key().endsWith(QStringLiteral(".dll"))) {
      names.append(it.key());
    }
  }
  return names;
}

// the mod a Root folder belongs to, for the log
QString modName(const QStringList& roots, int i)
{
  if (i < 0 || i >= roots.size()) {
    return {};
  }
  return QFileInfo(roots[i]).dir().dirName();
}

// the Root DLLs reached from the start files: those they import and, for the
// Root DLLs found, also those they name (a DLL can load another by name from
// DllMain, before usvfs hooks the process); breadth first
QStringList reach(const Index& index, const QStringList& startFiles, bool startMentions,
                  const QStringList& exclude = {})
{
  const QStringList dlls = dllNames(index);
  QStringList found;
  QSet<QString> seen(exclude.begin(), exclude.end());

  struct Item
  {
    QString path;
    bool mentions;
  };
  QList<Item> queue;
  for (const QString& f : startFiles) {
    queue.append({f, startMentions});
  }

  while (!queue.isEmpty()) {
    const Item item = queue.takeFirst();
    QStringList names;
    for (const QString& imported : cachedPe(item.path).imports) {
      if (dlls.contains(imported)) {
        names.append(imported);
      }
    }
    if (item.mentions) {
      QStringList others = dlls;
      others.removeAll(QFileInfo(item.path).fileName().toLower());
      names += cachedMentions(item.path, others);
    }
    for (const QString& n : names) {
      if (!seen.contains(n)) {
        seen.insert(n);
        found.append(n);
        queue.append({index.value(n).source, true});
      }
    }
  }
  return found;
}

// the folders ReShade.ini points to (PresetPath, EffectSearchPaths,
// TextureSearchPaths) that are relative to it
QStringList reshadePaths(const QString& ini)
{
  QStringList paths;
  QFile file(ini);
  if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
    return paths;
  }
  static const QStringList keys = {"presetpath", "effectsearchpaths",
                                   "texturesearchpaths"};
  while (!file.atEnd()) {
    const QString line = QString::fromUtf8(file.readLine()).trimmed();
    const auto eq      = line.indexOf('=');
    if (eq <= 0 || !keys.contains(line.left(eq).trimmed().toLower())) {
      continue;
    }
    for (QString v : line.mid(eq + 1).split(',')) {
      v = QDir::fromNativeSeparators(v.trimmed());
      if (v.endsWith(QStringLiteral("**"))) {
        v.chop(2);
      }
      while (v.endsWith('/')) {
        v.chop(1);
      }
      if (v.startsWith(QStringLiteral("./"))) {
        v = v.mid(2);
      }
      if (v.isEmpty() || v.contains(':') || v.startsWith('/')) {
        continue;
      }
      // .\ or .\** is the folder itself, not a companion of it
      v = QDir::cleanPath(v);
      if (!v.isEmpty() && v != QStringLiteral(".") && !v.startsWith(QStringLiteral(".."))) {
        paths.append(v);
      }
    }
  }
  return paths;
}

// what a proxy DLL reads next to its own module (GetModuleFileName): its
// <name>.ini, and ENB's or ReShade's files when it is one of those
QStringList companions(const Index& index, const QString& dllKey)
{
  const Entry dll = index.value(dllKey);
  QStringList out;

  const QString ini = QFileInfo(dll.name).completeBaseName().toLower() + ".ini";
  if (index.contains(ini)) {
    out.append(ini);
  }

  const QStringList kind = cachedMentions(dll.source, {"enbseries", "reshade"});
  const bool enb     = kind.contains("enbseries");
  const bool reshade = kind.contains("reshade");

  for (auto it = index.begin(); it != index.end(); ++it) {
    const QString& key = it.key();
    if (key == dllKey || key.endsWith(QStringLiteral(".exe")) ||
        key.endsWith(QStringLiteral(".dll"))) {
      continue;
    }
    if ((enb && key.startsWith(QStringLiteral("enb"))) ||
        (reshade && key.startsWith(QStringLiteral("reshade")))) {
      out.append(key);
    }
  }

  if (reshade) {
    for (const QString& file : {QStringLiteral("reshade.ini"), ini}) {
      if (index.contains(file) && !index.value(file).dir) {
        out += reshadePaths(index.value(file).source);
      }
    }
  }

  // MO2 Revamped: a folder next to it, one or two levels down, that it names
  // with its path (NVHR's d3dx9_38.dll loads Data\NVHR\nvhr_avx2.dll from
  // DllMain, before usvfs hooks the game): it comes with it
  {
    const QDir own = QFileInfo(dll.source).absoluteDir();
    QStringList folders;
    QStringList needles;
    for (const QString& a : own.entryList(QDir::Dirs | QDir::NoDotAndDotDot)) {
      const QDir sub(own.absoluteFilePath(a));
      QStringList levels = {a};
      for (const QString& b : sub.entryList(QDir::Dirs | QDir::NoDotAndDotDot)) {
        levels.append(a + u'/' + b);
      }
      for (const QString& rel : levels) {
        if (!rel.contains(u'/')) {
          continue;  // a single name (Data, Plugins) is too common to tell
        }
        folders.append(rel);
        needles.append(QString(rel).replace(u'/', u'\\').toLower() + u'\\');
      }
    }
    const QStringList named = cachedMentions(dll.source, needles);
    for (int i = 0; i < folders.size(); ++i) {
      if (named.contains(needles[i])) {
        out.append(folders[i]);
      }
    }
  }

  // never the Root folder itself, nor outside it
  QStringList kept;
  for (const QString& rel : out) {
    const QString clean = QDir::cleanPath(rel);
    if (!clean.isEmpty() && clean != QStringLiteral(".") &&
        !clean.startsWith(QStringLiteral("..")) && !QDir::isAbsolutePath(clean)) {
      kept.append(clean);
    }
  }
  kept.removeDuplicates();
  return kept;
}

// the Root folders that have rel (in dir), lowest first
QList<int> rootsWith(const QStringList& roots, const QString& dir, const QString& rel)
{
  QList<int> found;
  for (int i = 0; i < roots.size(); ++i) {
    if (QFileInfo::exists(roots[i] + "/" + join(dir, rel))) {
      found.append(i);
    }
  }
  return found;
}

class CopySet
{
public:
  void add(const QString& rel, const QString& source, const QString& reason)
  {
    const QString key = rel.toLower();
    if (!m_files.contains(key)) {
      m_order.append(key);
    }
    m_files.insert(key, {rel, source, reason});
  }

  // a folder's files over all the Root folders, a higher one replacing
  void addFolder(const QStringList& roots, const QString& rel, const QString& reason)
  {
    const QString clean = QDir::cleanPath(rel);
    if (clean.isEmpty() || clean == QStringLiteral(".") ||
        clean.startsWith(QStringLiteral(".."))) {
      return;
    }
    for (const QString& root : roots) {
      const QString dir = root + "/" + rel;
      if (!QFileInfo(dir).isDir()) {
        continue;
      }
      QDirIterator it(dir, QDir::Files | QDir::Hidden | QDir::System,
                      QDirIterator::Subdirectories);
      while (it.hasNext()) {
        const QString path = it.next();
        add(rel + "/" + QDir(dir).relativeFilePath(path), path, reason);
      }
    }
  }

  std::vector<RootCopy::File> files() const
  {
    std::vector<RootCopy::File> out;
    for (const QString& key : m_order) {
      out.push_back(m_files.value(key));
    }
    return out;
  }

private:
  QStringList m_order;
  QMap<QString, RootCopy::File> m_files;
};

// DLLs and what they read next to them, into the copy set
void copyGroup(CopySet& set, const QStringList& roots, const QString& dir,
               const Index& index, const QStringList& keys, const QString& reason)
{
  for (const QString& key : keys) {
    const Entry e = index.value(key);
    set.add(join(dir, e.name), e.source, reason);
    for (const QString& rel : companions(index, key)) {
      const QList<int> having = rootsWith(roots, dir, rel);
      if (having.isEmpty()) {
        continue;
      }
      const QString winner = roots[having.last()] + "/" + join(dir, rel);
      if (QFileInfo(winner).isDir()) {
        set.addFolder(roots, join(dir, rel),
                      QStringLiteral("read by %1 next to itself").arg(e.name));
      } else {
        set.add(join(dir, rel), winner,
                QStringLiteral("read by %1 next to itself").arg(e.name));
      }
    }
  }
}

struct Loader
{
  QString key;
  // skse64, nvse
  QString prefix;
  // SKSE, NVSE: the Start<tag> export
  QString tag;
};

std::optional<Loader> loaderFromName(const QString& key)
{
  static const QRegularExpression re(
      QStringLiteral("^([a-z0-9]*se(?:vr)?(?:64)?)_loader\\.exe$"));
  const auto m = re.match(key);
  if (!m.hasMatch()) {
    return {};
  }
  Loader l;
  l.key    = key;
  l.prefix = m.captured(1);
  l.tag    = l.prefix;
  if (l.tag.endsWith(QStringLiteral("64"))) {
    l.tag.chop(2);
  }
  return l;
}

QList<int> versionParts(const QString& s, QChar separator)
{
  QList<int> parts;
  for (const QString& p : s.split(separator)) {
    bool ok     = false;
    const int v = p.toInt(&ok);
    if (!ok) {
      return {};
    }
    parts.append(v);
  }
  return parts;
}

// the extender's runtime DLL for the game's version: skse64_1_6_1170.dll for
// 1.6.1170.0, obse_1_2_416.dll for 1.2.0.416, nvse_1_4.dll for 1.4.0.525
QString matchRuntime(const Index& index, const QString& prefix, const QString& gameVersion,
                     QString* why, QStringList* mismatched = nullptr)
{
  const QRegularExpression re(QStringLiteral("^%1_(\\d+(?:_\\d+)*)\\.dll$")
                                  .arg(QRegularExpression::escape(prefix)));
  QList<int> version = versionParts(gameVersion, '.');
  QList<int> alternate;
  if (version.size() == 4) {
    alternate = {version[0], version[1], version[3]};
  }

  QString best;
  int bestLength = 0;
  QStringList candidates;
  for (const QString& key : dllNames(index)) {
    const auto m = re.match(key);
    if (!m.hasMatch()) {
      continue;
    }
    candidates.append(index.value(key).name);
    const QList<int> parts = versionParts(m.captured(1), '_');
    // MO2 Revamped: the short names stand for one build only: fose_1_7.dll is
    // for 1.7.0.3 (the Steam 1.7.0.4 needs the Anniversary Patcher first, and
    // FOSE loaded into it fails without a word), nvse_1_4.dll for 1.4.0.525.
    // Its loader, run instead, says so
    static const QHash<QString, QList<int>> exact = {
        {QStringLiteral("fose_1_7.dll"), {1, 7, 0, 3}},
        {QStringLiteral("nvse_1_4.dll"), {1, 4, 0, 525}},
    };
    const auto pinned = exact.constFind(key);
    const bool matches =
        !version.isEmpty() &&
        (pinned != exact.constEnd()
             ? version == *pinned
             : (parts == alternate ||
                (parts.size() <= version.size() && version.mid(0, parts.size()) == parts)));
    if (matches && parts.size() > bestLength) {
      best       = key;
      bestLength = int(parts.size());
    }
  }

  if (best.isEmpty() && version.isEmpty() && candidates.size() == 1) {
    return candidates.first().toLower();
  }
  if (best.isEmpty() && !candidates.isEmpty() && !version.isEmpty() &&
      mismatched != nullptr) {
    *mismatched = candidates;
  }
  if (best.isEmpty()) {
    *why = candidates.isEmpty()
               ? QStringLiteral("no %1_*.dll is in the mods' Root folders").arg(prefix)
               : QStringLiteral("%1 isn't for the game's version %2")
                     .arg(candidates.join(", "), gameVersion);
  }
  return best;
}

}  // namespace

Plan plan(const Input& in)
{
  PlanCache cache;
  g_cache = &cache;
  struct Scope
  {
    ~Scope() { g_cache = nullptr; }
  } scope;
  Plan p;
  if (in.rootFolders.isEmpty()) {
    return p;
  }

  const QStringList& roots  = in.rootFolders;
  const QString gameDir     = QDir::cleanPath(QDir::fromNativeSeparators(in.gameDir));
  const QString gamePrefix  = under(in.gameDir);
  const QString gameBin     = QDir::cleanPath(QDir::fromNativeSeparators(in.gameBinary));
  QString exeDir            = QFileInfo(gameBin).path();
  if (exeDir == ".") {
    exeDir.clear();
  }
  const QString gameExeName = QFileInfo(gameBin).fileName();
  const QString gameExeKey  = gameExeName.toLower();
  const QString gameExeReal = gamePrefix + gameBin;

  const Index index      = indexDir(roots, exeDir);
  const QStringList dlls = dllNames(index);

  // the program, in the game folder's terms: one started from a mod's Root
  // folder runs as the game folder's
  QString run   = QDir::cleanPath(QFileInfo(in.runBinary).absoluteFilePath());
  bool fromRoot = false;
  for (const QString& root : roots) {
    const QString r = under(root);
    if (run.startsWith(r, Qt::CaseInsensitive)) {
      const QString rel = run.mid(r.size());
      info(QStringLiteral("root launch: %1 is in a mod's Root folder, it runs as %2")
               .arg(native(run), native(gamePrefix + rel)));
      run      = gamePrefix + rel;
      fromRoot = true;
      break;
    }
  }
  const bool inGame = run.startsWith(gamePrefix, Qt::CaseInsensitive);
  const QString runRel = inGame ? run.mid(gamePrefix.size()) : QString();
  QString runDir       = QFileInfo(runRel).path();
  if (runDir == ".") {
    runDir.clear();
  }
  const QString runKey  = QFileInfo(runRel).fileName().toLower();
  const bool runIsGame  = inGame && runRel.compare(gameBin, Qt::CaseInsensitive) == 0;
  bool startsGame       = runIsGame;

  CopySet copies;
  bool gameCopy = false;
  QString gameCopyReason;

  // a mod's game exe (FNV4GB's FalloutNV.exe) has no other way in
  const bool exeReplaced = index.contains(gameExeKey) && !index.value(gameExeKey).dir;
  const QString gameExeSource =
      exeReplaced ? index.value(gameExeKey).source : gameExeReal;
  const RootCopy::PeFile gamePe = cachedPe(gameExeSource);
  const bool steamStub          = RootCopy::isSteamStub(gamePe);
  if (exeReplaced) {
    const Entry e = index.value(gameExeKey);
    copies.add(gameBin, e.source, QStringLiteral("replaces the game exe"));
    gameCopy       = true;
    gameCopyReason = QStringLiteral("%1 replaces the game exe").arg(modName(roots, e.root));
  }

  // the script extender
  QList<Loader> loaders;
  for (auto it = index.begin(); it != index.end(); ++it) {
    if (!it->dir) {
      if (auto l = loaderFromName(it.key())) {
        loaders.append(*l);
      }
    }
  }
  std::optional<Loader> extender;
  bool runIsLoader = false;
  if (inGame && runDir.compare(exeDir, Qt::CaseInsensitive) == 0) {
    for (const auto& l : loaders) {
      if (l.key == runKey) {
        extender    = l;
        runIsLoader = true;
      }
    }
  }
  // only when the loader is what runs: the game exe started by itself stays
  // without the extender, as without Root
  if (extender) {
    const Entry loader = index.value(extender->key);
    QString why;
    QString runtime;
    static const QRegularExpression otherProgram(
        QStringLiteral("(^|\\s)[-/](editor|altexe)\\b"),
        QRegularExpression::CaseInsensitiveOption);
    const auto otherMatch = otherProgram.match(in.runArguments);
    if (otherMatch.hasMatch()) {
      // nvse_loader.exe -editor starts GECK, -altexe another exe: not the game
      why = QStringLiteral("%1 is started with %2, it starts another program than "
                           "the game")
                .arg(loader.name, otherMatch.captured(0).trimmed());
    } else if (exeReplaced) {
      why = QStringLiteral("the game exe is replaced by a mod");
    } else if (steamStub) {
      why = QStringLiteral("%1 is SteamStub-wrapped, its code isn't unpacked yet when "
                           "a forced load runs")
                .arg(gameExeName);
    } else {
      QStringList mismatched;
      runtime = matchRuntime(index, extender->prefix, in.gameVersion, &why, &mismatched);
      if (runtime.isEmpty() && !mismatched.isEmpty()) {
        p.extenderMismatchDlls = mismatched;
      }
      if (!runtime.isEmpty() &&
          !cachedMentions(loader.source, {"start" + extender->tag}).isEmpty()) {
        why = QStringLiteral("%1 calls Start%2 after injecting %3, which a forced load "
                             "doesn't do")
                  .arg(loader.name, extender->tag.toUpper(), index.value(runtime).name);
        runtime.clear();
      }
    }

    if (!runtime.isEmpty()) {
      // the mod's own file: usvfs checks and loads it in InitHooks, which
      // then needn't go through its own hooks to find it
      const Entry dll       = index.value(runtime);
      p.forcedLoadProcess   = gameExeName;
      p.forcedLoadLibrary   = native(dll.source);
      info(QStringLiteral("root launch: %1 is loaded into %2 by usvfs before the game's "
                          "entry point (forced load, from %3), instead of starting %4")
               .arg(dll.name, gameExeName, modName(roots, dll.root), loader.name));
      p.runBinary = gameExeReal;
      startsGame  = true;
    } else {
      info(QStringLiteral("root launch: %1 runs, copied into the game folder with its "
                          "DLLs: %2")
               .arg(loader.name, why));
      copies.add(join(exeDir, loader.name), loader.source,
                 QStringLiteral("the script extender's loader, started by MO2"));
      QStringList extenderDlls;
      for (const QString& dll : dlls) {
        if (dll.startsWith(extender->prefix + "_")) {
          extenderDlls.append(dll);
        }
      }
      extenderDlls += reach(index, {loader.source}, false, extenderDlls);
      for (const QString& dll : extenderDlls) {
        copies.add(join(exeDir, index.value(dll).name), index.value(dll).source,
                   QStringLiteral("the script extender's DLL, injected by its loader"));
      }
      if (!gameCopy) {
        gameCopy       = true;
        gameCopyReason = QStringLiteral("%1 starts the game itself").arg(loader.name);
      }
      if (fromRoot) {
        p.runBinary = gamePrefix + join(exeDir, loader.name);
      }
    }
  }

  // the exe MO2 itself starts from the game folder, whose import table is read
  RootCopy::PeFile startedPe;
  QString startedName;
  if (startsGame && !exeReplaced) {
    startedPe   = gamePe;
    startedName = gameExeName;
  }

  // another program in the game folder. One a mod's Root has runs from the
  // mod's file with the game folder as its working folder: MO2 starts a
  // program in the mods folder through the VFS (as a list's Root GECK.exe),
  // nothing is copied. One that is only the game folder's own is started by
  // MO2 directly and gets the game's treatment of its imports. Either, next
  // to the game exe and naming it, may start the game: its proxies are copied
  if (inGame && !runIsGame && !runIsLoader) {
    const bool sameDir   = runDir.compare(exeDir, Qt::CaseInsensitive) == 0;
    const QString name   = QFileInfo(runRel).fileName();
    QString source;
    if (fromRoot) {
      source = QDir::cleanPath(QFileInfo(in.runBinary).absoluteFilePath());
    } else {
      const Index runIndex = sameDir ? index : indexDir(roots, runDir);
      if (runIndex.contains(runKey) && !runIndex.value(runKey).dir) {
        const Entry e  = runIndex.value(runKey);
        source         = e.source;
        p.runBinary    = e.source;
        p.runDirectory = join(gameDir, runDir);
        info(QStringLiteral("root launch: %1 is in %2's Root folder, it runs from there "
                            "with %3 as its working folder")
                 .arg(name, modName(roots, e.root), native(p.runDirectory)));
      } else if (QFileInfo::exists(gamePrefix + runRel)) {
        source = gamePrefix + runRel;
        if (sameDir) {
          startedPe   = cachedPe(source);
          startedName = name;
        }
      }
    }
    if (sameDir && !source.isEmpty() && !gameCopy &&
        !cachedMentions(source, {gameExeKey}).isEmpty()) {
      gameCopy       = true;
      gameCopyReason = QStringLiteral("%1 names %2 and may start it").arg(name, gameExeName);
    }
  }

  // the exe started by MO2: the DLLs its import table names from Root
  if (!startedName.isEmpty() && !gameCopy && startedPe.valid) {
    if (RootCopy::isSteamStub(startedPe)) {
      gameCopy       = true;
      gameCopyReason = QStringLiteral("%1 is SteamStub-wrapped, the import table seen "
                                      "before it runs is the wrapper's")
                           .arg(startedName);
    } else {
      CopySet fallback;
      QStringList preloaded;
      for (const QString& imported : startedPe.imports) {
        if (!dlls.contains(imported)) {
          continue;
        }
        // MO2 Revamped: one the game plugin has usvfs load from the game
        // folder (Cyberpunk's CET version.dll): loaded from the mod's path by
        // the import patch as well, CET found two and stopped ("Failed to get
        // ...\bin\x64\version.dll handle")
        if (in.forcedByGame.contains(join(exeDir, imported).toLower())) {
          info(QStringLiteral("root launch: %1 is left to the game plugin's forced load "
                              "through the VFS")
                   .arg(imported));
          continue;
        }
        const Entry proxy = index.value(imported);
        const QStringList deps = reach(index, {proxy.source}, true, {imported});
        QStringList group      = QStringList{imported} + deps;

        QString problem;
        for (const QString& key : group) {
          const Entry e               = index.value(key);
          const RootCopy::PeFile pe   = cachedPe(e.source);
          if (!pe.valid || pe.is64 != startedPe.is64) {
            problem = QStringLiteral("%1 isn't a DLL for this game").arg(e.name);
          } else if (ImportPatch::importablePath(e.source).isEmpty()) {
            problem = QStringLiteral("the path of %1 can't be given to Windows' loader")
                          .arg(native(e.source));
          } else if (key != imported && !pe.hasExports) {
            problem = QStringLiteral("%1, needed by %2, exports nothing to import it by")
                          .arg(e.name, proxy.name);
          }
          for (const QString& rel : problem.isEmpty() ? companions(index, key)
                                                      : QStringList()) {
            const QList<int> having = rootsWith(roots, exeDir, rel);
            const bool elsewhere =
                std::any_of(having.begin(), having.end(), [&](int r) {
                  return r != e.root;
                });
            if (!having.isEmpty() && (having.last() != e.root ||
                                      (QFileInfo(roots[having.last()] + "/" +
                                                 join(exeDir, rel))
                                           .isDir() &&
                                       elsewhere))) {
              problem = QStringLiteral("%1 reads %2 next to itself, and that comes from "
                                       "%3, not from %4")
                            .arg(e.name, rel, modName(roots, having.last()),
                                 modName(roots, e.root));
              break;
            }
          }
          if (!problem.isEmpty()) {
            break;
          }
        }

        if (problem.isEmpty()) {
          p.redirects.push_back({proxy.name, native(proxy.source)});
          // the deepest first, so a load by name finds each one already there
          for (auto it = deps.crbegin(); it != deps.crend(); ++it) {
            if (preloaded.contains(*it)) {
              continue;
            }
            preloaded.append(*it);
            const Entry e             = index.value(*it);
            const RootCopy::PeFile pe = cachedPe(e.source);
            p.preloads.push_back({native(e.source), pe.exportName, pe.exportOrdinal});
          }
          info(QStringLiteral("root launch: %1 is loaded from %2's Root folder (import "
                              "table patched in memory)%3")
                   .arg(proxy.name, modName(roots, proxy.root),
                        deps.isEmpty()
                            ? QString()
                            : QStringLiteral(", with %1 loaded first").arg(deps.join(", "))));
          copyGroup(fallback, roots, exeDir, index, group,
                    QStringLiteral("imported by %1 (the import patch failed)")
                        .arg(startedName));
        } else {
          info(QStringLiteral("root launch: %1 is copied into the game folder: %2")
                   .arg(proxy.name, problem));
          copyGroup(copies, roots, exeDir, index, group,
                    QStringLiteral("imported by %1").arg(startedName));
        }
      }
      p.patchFallback = fallback.files();
    }
  }

  if (gameCopy) {
    QStringList keys;
    for (const QString& dll : dlls) {
      if (gamePe.imports.contains(dll) || KnownProxies.contains(dll)) {
        keys.append(dll);
      }
    }
    QStringList starts;
    for (const QString& k : keys) {
      starts.append(index.value(k).source);
    }
    keys += reach(index, starts, true, keys);
    if (!keys.isEmpty()) {
      info(QStringLiteral("root launch: %1 copied into the game folder: %2")
               .arg(keys.join(", "), gameCopyReason));
      copyGroup(copies, roots, exeDir, index, keys,
                QStringLiteral("loaded by the game before usvfs hooks it (%1)")
                    .arg(gameCopyReason));
    }
    // what the replaced exe imports or names from Root comes with it (FNV4GB's
    // loads nvse_steam_loader.dll)
    if (exeReplaced) {
      const QStringList deps = reach(index, {gameExeSource}, true, keys);
      for (const QString& dep : deps) {
        copies.add(join(exeDir, index.value(dep).name), index.value(dep).source,
                   QStringLiteral("imported by the replaced %1").arg(gameExeName));
      }
    }
  }

  p.copies = copies.files();
  return p;
}

bool concerns(const QString& binary, const QString& gameDir, const QString& modsDir,
              const QString& overwriteDir)
{
  const QString bin = QDir::cleanPath(QDir::fromNativeSeparators(binary));
  auto inside       = [&](const QString& dir) {
    const QString prefix = under(dir);
    return !dir.isEmpty() && bin.size() > prefix.size() &&
           bin.startsWith(prefix, Qt::CaseInsensitive);
  };
  return inside(gameDir) || inside(modsDir) || inside(overwriteDir);
}

}  // namespace RootLaunch