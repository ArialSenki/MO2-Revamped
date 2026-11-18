#include "nolvus.h"
#include "filesystemutilities.h"
#include "fluentdialogs.h"
#include "instancemanager.h"
#include "settings.h"
#include <log.h>
#include <utility.h>

#include <QCheckBox>
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QLabel>
#include <QLocale>
#include <QPushButton>
#include <QSettings>
#include <QVBoxLayout>
#include <QXmlStreamReader>

using namespace MOBase;

namespace Nolvus
{

namespace
{

// the strings' translation context
struct Text
{
  Q_DECLARE_TR_FUNCTIONS(Nolvus)
};

const QString Section = QStringLiteral("Nolvus");

QString clean(const QString& path)
{
  if (path.trimmed().isEmpty()) {
    return {};
  }
  return QDir::cleanPath(QDir::fromNativeSeparators(path.trimmed()));
}

bool samePath(const QString& a, const QString& b)
{
  return !a.isEmpty() && clean(a).compare(clean(b), Qt::CaseInsensitive) == 0;
}

// a value MO2 writes as @ByteArray(...) or as text
QString iniString(const QSettings& s, const QString& key)
{
  const QVariant v = s.value(key);
  if (v.typeId() == QMetaType::QByteArray) {
    return QString::fromUtf8(v.toByteArray());
  }
  return v.toString();
}

int countMods(const QString& modsPath)
{
  if (modsPath.isEmpty()) {
    return 0;
  }
  const QStringList dirs =
      QDir(QDir(modsPath).filePath(QStringLiteral("mods")))
          .entryList(QDir::Dirs | QDir::NoDotAndDotDot);
  int n = 0;
  for (const QString& d : dirs) {
    if (!d.endsWith(QStringLiteral("_separator"), Qt::CaseInsensitive)) {
      ++n;
    }
  }
  return n;
}

// fills what Nolvus' own MO2 ini says; read only
void readMo2Ini(InstanceInfo& info)
{
  const QString ini = QDir(info.mo2Path).filePath(QStringLiteral("ModOrganizer.ini"));
  if (!QFileInfo(ini).isFile()) {
    return;
  }
  const QSettings s(ini, QSettings::IniFormat);
  if (const QString base = clean(iniString(s, QStringLiteral("Settings/base_directory")));
      !base.isEmpty()) {
    info.modsPath = base;
  }
  info.profile = iniString(s, QStringLiteral("selected_profile"));
  if (info.stockGame.isEmpty()) {
    info.stockGame = clean(iniString(s, QStringLiteral("gamePath")));
  }
}

// the Dashboard's list of instances; the comment in the file asks not to
// change it, and nothing here does
std::vector<InstanceInfo> readInstancesData(const QString& root, const QString& xmlPath)
{
  std::vector<InstanceInfo> list;
  QFile file(xmlPath);
  if (!file.open(QIODevice::ReadOnly)) {
    return list;
  }

  QXmlStreamReader xml(&file);
  QStringList path;
  InstanceInfo current;
  QString optionName, optionValue;

  while (!xml.atEnd()) {
    const auto token = xml.readNext();
    if (token == QXmlStreamReader::StartElement) {
      path.push_back(xml.name().toString());
      const QString here = path.join(QLatin1Char('/'));
      if (here == QLatin1String("Instances/Instance")) {
        current      = InstanceInfo();
        current.root = clean(root);
      } else if (here == QLatin1String("Instances/Instance/Options/Option")) {
        optionName.clear();
        optionValue.clear();
      }
    } else if (token == QXmlStreamReader::Characters && !xml.isWhitespace()) {
      const QString here = path.join(QLatin1Char('/'));
      const QString text = xml.text().toString().trimmed();
      if (here == QLatin1String("Instances/Instance/Name")) {
        current.name = text;
      } else if (here == QLatin1String("Instances/Instance/Version")) {
        current.version = text;
      } else if (here == QLatin1String("Instances/Instance/Settings/InstallPath")) {
        current.installPath = clean(text);
      } else if (here == QLatin1String("Instances/Instance/Settings/ArchivePath")) {
        current.archivePath = clean(text);
      } else if (here == QLatin1String("Instances/Instance/Settings/StockGame")) {
        current.stockGame = clean(text);
      } else if (here == QLatin1String("Instances/Instance/Settings/Variant")) {
        current.variant = text;
      } else if (here == QLatin1String("Instances/Instance/Options/Option/Name")) {
        optionName = text;
      } else if (here == QLatin1String("Instances/Instance/Options/Option/Value")) {
        optionValue = text;
      }
    } else if (token == QXmlStreamReader::EndElement) {
      const QString here = path.join(QLatin1Char('/'));
      if (here == QLatin1String("Instances/Instance/Options/Option") &&
          optionName == QLatin1String("AlternateENB")) {
        current.enb = optionValue;
      } else if (here == QLatin1String("Instances/Instance") && !current.name.isEmpty()) {
        list.push_back(current);
      }
      if (!path.isEmpty()) {
        path.pop_back();
      }
    }
  }

  if (xml.hasError()) {
    log::warn("Nolvus: can't read '{}' fully: {}", QDir::toNativeSeparators(xmlPath),
              xml.errorString());
  }
  return list;
}

// the linked instance of each Nolvus install folder
QString findLinkedInstance(const QString& installPath)
{
  for (const QString& dir : InstanceManager::singleton().portableInstancePaths()) {
    const Link link = readLink(InstanceManager::singleton().iniPath(dir));
    if (link.valid && samePath(link.installPath, installPath)) {
      return dir;
    }
  }
  return {};
}

QDateTime modlistTime(const QString& profileDir)
{
  return QFileInfo(QDir(profileDir).filePath(QStringLiteral("modlist.txt"))).lastModified();
}

// the newest modlist.txt of Nolvus' profiles
QDateTime newestSourceProfile(const QString& modsPath)
{
  QDateTime newest;
  const QDir profiles(QDir(modsPath).filePath(QStringLiteral("profiles")));
  for (const QString& p : profiles.entryList(QDir::Dirs | QDir::NoDotAndDotDot)) {
    const QDateTime t = modlistTime(profiles.filePath(p));
    if (t.isValid() && (!newest.isValid() || t > newest)) {
      newest = t;
    }
  }
  return newest;
}

// copies a folder, files and subfolders; links are left out, so a copy never
// reaches outside it
bool copyTree(const QString& from, const QString& to, QString* error)
{
  if (!QDir().mkpath(to)) {
    *error = Text::tr("Can't create %1").arg(QDir::toNativeSeparators(to));
    return false;
  }
  const QDir src(from);
  for (const QFileInfo& fi :
       src.entryInfoList(QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot | QDir::Hidden)) {
    const QString target = QDir(to).filePath(fi.fileName());
    if (fi.isJunction() || fi.isSymLink()) {
      log::warn("Nolvus: not copying the link '{}'",
                QDir::toNativeSeparators(fi.absoluteFilePath()));
    } else if (fi.isDir()) {
      if (!copyTree(fi.absoluteFilePath(), target, error)) {
        return false;
      }
    } else if (!QFile::copy(fi.absoluteFilePath(), target)) {
      *error = Text::tr("Can't copy %1").arg(QDir::toNativeSeparators(fi.absoluteFilePath()));
      return false;
    }
  }
  return true;
}

// copies Nolvus' profiles, saves included
bool copyProfiles(const QString& from, const QString& to, QString* error)
{
  if (!QFileInfo(from).isDir()) {
    *error = Text::tr("Nolvus' profiles folder isn't there: %1")
                 .arg(QDir::toNativeSeparators(from));
    return false;
  }
  return copyTree(from, to, error);
}

void copyGroup(const QSettings& from, QSettings& to, const QString& group)
{
  for (const QString& key : from.allKeys()) {
    if (key.startsWith(group + QLatin1Char('/'))) {
      to.setValue(key, from.value(key));
    }
  }
}

bool fileVersionMatchesDll(const QString& exeVersion, const QString& dllName)
{
  // skse64_1_5_97.dll is for 1.5.97(.0)
  QString v = dllName.mid(QStringLiteral("skse64_").size());
  v.chop(4);
  v.replace(QLatin1Char('_'), QLatin1Char('.'));
  return exeVersion == v || exeVersion.startsWith(v + QLatin1Char('.'));
}

}  // namespace

QString defaultRoot()
{
  return QStringLiteral("C:/Nolvus");
}

QStringList roots()
{
  QStringList list{defaultRoot()};
  for (const QString& r :
       GlobalSettings::value(QStringLiteral("Nolvus/roots")).toStringList()) {
    if (!r.isEmpty() && !list.contains(clean(r), Qt::CaseInsensitive)) {
      list.push_back(clean(r));
    }
  }
  return list;
}

void addRoot(const QString& root)
{
  QStringList added = GlobalSettings::value(QStringLiteral("Nolvus/roots")).toStringList();
  const QString r   = clean(root);
  if (r.isEmpty() || samePath(r, defaultRoot()) || added.contains(r, Qt::CaseInsensitive)) {
    return;
  }
  added.push_back(r);
  GlobalSettings::setValue(QStringLiteral("Nolvus/roots"), added);
}

bool looksLikeRoot(const QString& dir)
{
  const QDir instances(QDir(dir).filePath(QStringLiteral("Instances")));
  if (QFileInfo(instances.filePath(QStringLiteral("InstancesData.xml"))).isFile()) {
    return true;
  }
  for (const QString& d : instances.entryList(QDir::Dirs | QDir::NoDotAndDotDot)) {
    if (QFileInfo(instances.filePath(d + QStringLiteral("/MO2/ModOrganizer.ini")))
            .isFile()) {
      return true;
    }
  }
  return false;
}

std::vector<InstanceInfo> findInstances(const QString& root)
{
  const QDir instances(QDir(root).filePath(QStringLiteral("Instances")));
  std::vector<InstanceInfo> list =
      readInstancesData(root, instances.filePath(QStringLiteral("InstancesData.xml")));

  // an install the Dashboard's list doesn't name (an older Dashboard, or a
  // folder copied elsewhere): its MO2 ini is enough
  for (const QString& d : instances.entryList(QDir::Dirs | QDir::NoDotAndDotDot)) {
    const QString path = clean(instances.filePath(d));
    if (!QFileInfo(path + QStringLiteral("/MO2/ModOrganizer.ini")).isFile()) {
      continue;
    }
    const bool known = std::any_of(list.begin(), list.end(), [&](const InstanceInfo& i) {
      return samePath(i.installPath, path);
    });
    if (!known) {
      InstanceInfo info;
      info.root        = clean(root);
      info.name        = d;
      info.installPath = path;
      list.push_back(info);
    }
  }

  std::vector<InstanceInfo> found;
  for (auto& info : list) {
    if (info.installPath.isEmpty()) {
      info.installPath = clean(instances.filePath(info.name));
    }
    info.mo2Path = clean(QDir(info.installPath).filePath(QStringLiteral("MO2")));
    if (!QFileInfo(info.mo2Path + QStringLiteral("/ModOrganizer.ini")).isFile()) {
      // listed by the Dashboard but not (or no longer) installed
      continue;
    }
    info.modsPath = clean(QDir(info.installPath).filePath(QStringLiteral("MODS")));
    readMo2Ini(info);
    if (info.archivePath.isEmpty() &&
        QFileInfo(instances.filePath(QStringLiteral("ARCHIVE"))).isDir()) {
      info.archivePath = clean(instances.filePath(QStringLiteral("ARCHIVE")));
    }
    if (info.stockGame.isEmpty()) {
      info.stockGame = clean(QDir(info.installPath).filePath(QStringLiteral("STOCK GAME")));
    }
    info.modCount       = countMods(info.modsPath);
    info.linkedInstance = findLinkedInstance(info.installPath);
    found.push_back(info);
  }
  return found;
}

std::vector<InstanceInfo> findInstances()
{
  std::vector<InstanceInfo> all;
  for (const QString& root : roots()) {
    if (!QFileInfo(root).isDir()) {
      continue;
    }
    for (auto& info : findInstances(root)) {
      const bool dup = std::any_of(all.begin(), all.end(), [&](const InstanceInfo& i) {
        return samePath(i.installPath, info.installPath);
      });
      if (!dup) {
        all.push_back(std::move(info));
      }
    }
  }
  return all;
}

Link readLink(const QString& iniPath)
{
  Link link;
  if (!QFileInfo(iniPath).isFile()) {
    return link;
  }
  const QSettings s(iniPath, QSettings::IniFormat);
  const QString installPath = s.value(Section + QStringLiteral("/installPath")).toString();
  if (installPath.isEmpty()) {
    return link;
  }
  link.valid             = true;
  link.installPath       = installPath;
  link.root              = s.value(Section + QStringLiteral("/root")).toString();
  link.name              = s.value(Section + QStringLiteral("/name")).toString();
  link.version           = s.value(Section + QStringLiteral("/version")).toString();
  link.variant           = s.value(Section + QStringLiteral("/variant")).toString();
  link.enb               = s.value(Section + QStringLiteral("/enb")).toString();
  link.archivePath       = s.value(Section + QStringLiteral("/archivePath")).toString();
  link.stockGame         = s.value(Section + QStringLiteral("/stockGame")).toString();
  link.mo2Path           = s.value(Section + QStringLiteral("/mo2Path")).toString();
  link.modsPath          = s.value(Section + QStringLiteral("/modsPath")).toString();
  link.linkedAt          = s.value(Section + QStringLiteral("/linkedAt")).toDateTime();
  link.sourceProfileTime = s.value(Section + QStringLiteral("/sourceProfileTime")).toDateTime();
  return link;
}

bool isLinked(const QString& iniPath)
{
  return readLink(iniPath).valid;
}

QString label(const QString& version, const QString& variant)
{
  QString s = QStringLiteral("Nolvus");
  if (!version.isEmpty()) {
    s += QLatin1Char(' ') + version;
  }
  if (!variant.isEmpty()) {
    s += QStringLiteral(" · ") + variant;
  }
  return s;
}

bool confirmLink(QWidget* parent, const InstanceInfo& info)
{
  const QString linkedFolder =
      QDir::toNativeSeparators(InstanceManager::singleton().portableInstancesRootPath() +
                               QLatin1Char('/') + MOBase::sanitizeFileName(info.name));

  FluentContentDialog dlg(
      parent, QStringLiteral("NolvusLinkDialog"),
      Text::tr("Open %1 in MO2 Revamped?").arg(info.name),
      Text::tr("MO2 Revamped makes an instance of its own for this Nolvus list. "
               "Nolvus' own MO2 folder and profiles are not changed."));

  const QString details =
      Text::tr("<p><b>What MO2 Revamped uses directly:</b> the list's mods and "
               "overwrite folder (%1) and Nolvus' stock game (%2). Installing, "
               "removing or editing a mod here changes those mods, as it would in "
               "Nolvus' MO2.</p>"
               "<p><b>What it copies:</b> Nolvus' profiles (mod order, plugins, "
               "game INIs, saves), into %3. Saves made from now on stay in that "
               "copy.</p>"
               "<p><b>What it keeps apart:</b> its own settings, downloads and web "
               "cache. The Nolvus archives (%4) show in the Downloads tab, read "
               "only; nothing is copied or written there.</p>"
               "<p><b>What it doesn't do:</b> register itself for Nexus links "
               "(nxm://) when this instance starts, or update the list. Keep "
               "using the Nolvus Dashboard to update or repair Nolvus; after an "
               "update, MO2 Revamped tells you when Nolvus' profiles changed, and "
               "can copy them again from Instances.</p>")
          .arg(QDir::toNativeSeparators(info.modsPath).toHtmlEscaped(),
               QDir::toNativeSeparators(info.stockGame).toHtmlEscaped(),
               linkedFolder.toHtmlEscaped(),
               QDir::toNativeSeparators(info.archivePath).toHtmlEscaped());

  auto* text = new QLabel(details, &dlg);
  text->setObjectName(QStringLiteral("nolvusLinkDetails"));
  text->setWordWrap(true);
  text->setTextFormat(Qt::RichText);
  dlg.body()->addWidget(text);

  auto* link   = dlg.addButton(Text::tr("Make the instance"), true);
  auto* cancel = dlg.addButton(Text::tr("Cancel"));
  QObject::connect(link, &QPushButton::clicked, &dlg, &QDialog::accept);
  QObject::connect(cancel, &QPushButton::clicked, &dlg, &QDialog::reject);
  dlg.resize(620, dlg.sizeHint().height());

  return dlg.exec() == QDialog::Accepted;
}

QString createLinkedInstance(const InstanceInfo& info, QString* error)
{
  QString dummy;
  if (!error) {
    error = &dummy;
  }

  auto& manager      = InstanceManager::singleton();
  const QString root = manager.portableInstancesRootPath();
  const QString name = manager.makeUniqueName(info.name, root);
  if (name.isEmpty()) {
    *error = Text::tr("No free folder name for %1 in %2")
                 .arg(info.name, QDir::toNativeSeparators(root));
    return {};
  }

  const QString dir = clean(QDir(root).filePath(name));
  if (!QDir().mkpath(dir)) {
    *error = Text::tr("Can't create %1").arg(QDir::toNativeSeparators(dir));
    return {};
  }

  const auto fail = [&](const QString& why) {
    *error = why;
    QDir(dir).removeRecursively();
    return QString();
  };

  const QString profiles = dir + QStringLiteral("/profiles");
  if (!copyProfiles(QDir(info.modsPath).filePath(QStringLiteral("profiles")), profiles,
                    error)) {
    return fail(*error);
  }
  QDir().mkpath(dir + QStringLiteral("/downloads"));
  QDir().mkpath(dir + QStringLiteral("/webcache"));

  const QSettings source(QDir(info.mo2Path).filePath(QStringLiteral("ModOrganizer.ini")),
                         QSettings::IniFormat);
  {
    QSettings ini(manager.iniPath(dir), QSettings::IniFormat);

    QString gameName = iniString(source, QStringLiteral("gameName"));
    if (gameName.isEmpty()) {
      gameName = QStringLiteral("Skyrim Special Edition");
    }
    ini.setValue(QStringLiteral("gameName"), gameName);
    ini.setValue(QStringLiteral("gamePath"),
                 QDir::toNativeSeparators(info.stockGame).toUtf8());
    if (!info.profile.isEmpty()) {
      ini.setValue(QStringLiteral("selected_profile"), info.profile.toUtf8());
    }
    if (const QString edition = iniString(source, QStringLiteral("game_edition"));
        !edition.isEmpty()) {
      ini.setValue(QStringLiteral("game_edition"), edition);
    }
    ini.setValue(QStringLiteral("first_start"), false);

    // mods and overwrite are Nolvus'; profiles, downloads and the cache are
    // this instance's
    ini.setValue(QStringLiteral("Settings/base_directory"), info.modsPath);
    ini.setValue(QStringLiteral("Settings/profiles_directory"), profiles);
    ini.setValue(QStringLiteral("Settings/download_directory"),
                 dir + QStringLiteral("/downloads"));
    ini.setValue(QStringLiteral("Settings/cache_directory"),
                 dir + QStringLiteral("/webcache"));

    // the programs Nolvus set up (its launcher, SKSE, the tools) and the
    // plugins' settings it chose
    copyGroup(source, ini, QStringLiteral("customExecutables"));
    copyGroup(source, ini, QStringLiteral("Plugins"));
    copyGroup(source, ini, QStringLiteral("pluginBlacklist"));

    ini.setValue(Section + QStringLiteral("/root"), info.root);
    ini.setValue(Section + QStringLiteral("/name"), info.name);
    ini.setValue(Section + QStringLiteral("/version"), info.version);
    ini.setValue(Section + QStringLiteral("/variant"), info.variant);
    ini.setValue(Section + QStringLiteral("/enb"), info.enb);
    ini.setValue(Section + QStringLiteral("/installPath"), info.installPath);
    ini.setValue(Section + QStringLiteral("/archivePath"), info.archivePath);
    ini.setValue(Section + QStringLiteral("/stockGame"), info.stockGame);
    ini.setValue(Section + QStringLiteral("/mo2Path"), info.mo2Path);
    ini.setValue(Section + QStringLiteral("/modsPath"), info.modsPath);
    ini.setValue(Section + QStringLiteral("/linkedAt"), QDateTime::currentDateTime());
    ini.setValue(Section + QStringLiteral("/sourceProfileTime"),
                 newestSourceProfile(info.modsPath));
    ini.sync();
    if (ini.status() != QSettings::NoError) {
      return fail(Text::tr("Can't write %1").arg(QDir::toNativeSeparators(ini.fileName())));
    }
  }

  log::info("Nolvus: linked '{}' ({}) as the instance in '{}'", info.name,
            QDir::toNativeSeparators(info.installPath), QDir::toNativeSeparators(dir));
  return dir;
}

std::vector<std::pair<QString, QString>> archiveSources(const QString& iniPath)
{
  std::vector<std::pair<QString, QString>> sources;
  const Link link = readLink(iniPath);
  if (!link.valid) {
    return sources;
  }

  const QString group = Text::tr("%1 (Nolvus archives)").arg(link.name);
  if (!link.archivePath.isEmpty() && QFileInfo(link.archivePath).isDir()) {
    sources.push_back({link.archivePath, group});
  }
  // the Dashboard's download cache, when archiving is off
  const QString cache = QDir(link.root).filePath(QStringLiteral("Cache/Downloads"));
  if (!link.root.isEmpty() && QFileInfo(cache).isDir() &&
      !QDir(cache).isEmpty(QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot)) {
    sources.push_back({clean(cache), group});
  }
  return sources;
}

QStringList preRunWarnings(const Link& link, const QString& iniPath,
                           const QString& profileDir, const QString& binary)
{
  QStringList warnings;
  if (!link.valid) {
    return warnings;
  }

  const QSettings ini(iniPath, QSettings::IniFormat);

  // the game: the instance plays Nolvus' stock game, at the version its SKSE
  // was made for (Nolvus downgrades the game it copies)
  const QString gamePath = clean(iniString(ini, QStringLiteral("gamePath")));
  if (!link.stockGame.isEmpty() && !samePath(gamePath, link.stockGame)) {
    warnings << Text::tr("This instance plays the game in %1, not in the stock game "
                         "Nolvus prepared (%2). Nolvus only works with its stock game.")
                    .arg(QDir::toNativeSeparators(gamePath),
                         QDir::toNativeSeparators(link.stockGame));
  }

  const QDir game(gamePath.isEmpty() ? link.stockGame : gamePath);
  const QString exe = game.filePath(QStringLiteral("SkyrimSE.exe"));
  if (!QFileInfo(exe).isFile()) {
    warnings << Text::tr("SkyrimSE.exe isn't in %1.")
                    .arg(QDir::toNativeSeparators(game.absolutePath()));
  } else {
    // Bethesda leaves the fixed version at 1.0.0.0: the version strings say
    // which game it is
    QString version = MOBase::getProductVersion(exe).trimmed();
    if (version.isEmpty()) {
      version = MOBase::getFileVersion(exe);
    }
    const QStringList dlls =
        game.entryList({QStringLiteral("skse64_1_*.dll")}, QDir::Files);
    const bool match = std::any_of(dlls.begin(), dlls.end(), [&](const QString& d) {
      return fileVersionMatchesDll(version, d);
    });
    if (dlls.isEmpty()) {
      warnings << Text::tr("SKSE isn't in the stock game (%1). Nolvus needs it; the "
                           "Nolvus Dashboard can repair the stock game.")
                      .arg(QDir::toNativeSeparators(game.absolutePath()));
    } else if (!match) {
      warnings << Text::tr("The game is version %1, but SKSE in the stock game is for "
                           "%2. Nolvus keeps its stock game downgraded; if Steam or "
                           "another tool changed it, the Nolvus Dashboard can put it "
                           "back.")
                      .arg(version, dlls.join(QStringLiteral(", ")));
    }
  }

  // the ENB the list was installed with
  const QString enb = link.enb.trimmed();
  if (!enb.isEmpty() && enb.compare(QStringLiteral("NONE"), Qt::CaseInsensitive) != 0 &&
      enb.compare(QStringLiteral("FALSE"), Qt::CaseInsensitive) != 0) {
    QStringList missing;
    for (const QString& f : {QStringLiteral("enbseries.ini"), QStringLiteral("enblocal.ini")}) {
      if (!QFileInfo(game.filePath(f)).isFile()) {
        missing << f;
      }
    }
    if (!QFileInfo(game.filePath(QStringLiteral("enbseries"))).isDir()) {
      missing << QStringLiteral("enbseries\\");
    }
    if (!QFileInfo(game.filePath(QStringLiteral("d3d11.dll"))).isFile() &&
        !QFileInfo(game.filePath(QStringLiteral("dxgi.dll"))).isFile()) {
      missing << QStringLiteral("d3d11.dll");
    }
    if (!missing.isEmpty()) {
      warnings << Text::tr("The %1 ENB was chosen for this list, but these ENB files "
                           "aren't in the stock game: %2.")
                      .arg(enb, missing.join(QStringLiteral(", ")));
    }
  }

  // the settings Nolvus sets on its profile
  if (!profileDir.isEmpty()) {
    const QSettings profile(QDir(profileDir).filePath(QStringLiteral("settings.ini")),
                            QSettings::IniFormat);
    const auto expect = [&](const char* key, bool value, const QString& what) {
      const QVariant v = profile.value(QLatin1String(key));
      if (v.isValid() && v.toBool() != value) {
        warnings << what;
      }
    };
    expect("LocalSaves", true,
           Text::tr("The profile doesn't use its own saves; Nolvus turns profile-"
                    "specific saves on."));
    expect("LocalSettings", true,
           Text::tr("The profile doesn't use its own game INI files; Nolvus turns "
                    "profile-specific game INIs on, and its INIs are tuned for the "
                    "list."));
    expect("AutomaticArchiveInvalidation", false,
           Text::tr("Automatic archive invalidation is on; Nolvus turns it off."));
  }

  const QString base = clean(iniString(ini, QStringLiteral("Settings/base_directory")));
  if (!link.modsPath.isEmpty() && !samePath(base, link.modsPath)) {
    warnings << Text::tr("The base directory is %1, not Nolvus' %2.")
                    .arg(QDir::toNativeSeparators(base),
                         QDir::toNativeSeparators(link.modsPath));
  }

  // the game itself, without SKSE
  const QString program = QFileInfo(binary).fileName();
  if (program.compare(QStringLiteral("SkyrimSE.exe"), Qt::CaseInsensitive) == 0 ||
      program.compare(QStringLiteral("SkyrimSELauncher.exe"), Qt::CaseInsensitive) == 0) {
    warnings << Text::tr("%1 starts the game without SKSE, and most of Nolvus needs "
                         "it. Nolvus starts the game with its \"Nolvus\" or \"SKSE\" "
                         "program.")
                    .arg(program);
  }

  // a Nolvus update changed its profiles after they were copied
  const QDateTime source = newestSourceProfile(link.modsPath);
  if (source.isValid() && link.sourceProfileTime.isValid() &&
      source > link.sourceProfileTime.addSecs(2)) {
    warnings << Text::tr("Nolvus' profiles changed on %1, after this instance copied "
                         "them (a Nolvus update?). The mod order here may be older. "
                         "To copy them again, open another instance, then right-click "
                         "this one in Instances and choose \"Copy Nolvus' profiles "
                         "again\".")
                    .arg(QLocale().toString(source, QLocale::ShortFormat));
  }

  return warnings;
}

bool confirmLaunch(QWidget* parent, const QString& iniPath, const QString& profileDir,
                   const QString& binary)
{
  const Link link = readLink(iniPath);
  if (!link.valid) {
    return true;
  }

  const QStringList warnings = preRunWarnings(link, iniPath, profileDir, binary);
  for (const QString& w : warnings) {
    log::warn("Nolvus check: {}", w);
  }
  if (warnings.isEmpty()) {
    return true;
  }

  // the same warnings, once dismissed, don't ask again
  const QByteArray hash =
      QCryptographicHash::hash((iniPath + warnings.join(QLatin1Char('\n'))).toUtf8(),
                               QCryptographicHash::Sha1)
          .toHex();
  const QString ackKey = QStringLiteral("Nolvus/acknowledged/") +
                         QString::fromLatin1(QCryptographicHash::hash(
                                                 clean(iniPath).toCaseFolded().toUtf8(),
                                                 QCryptographicHash::Sha1)
                                                 .toHex());
  if (GlobalSettings::value(ackKey).toByteArray() == hash) {
    return true;
  }

  FluentContentDialog dlg(parent, QStringLiteral("NolvusChecksDialog"),
                          Text::tr("Nolvus checks"),
                          Text::tr("Some things Nolvus needs don't look right. The "
                                   "program can still run."));

  QString list;
  for (const QString& w : warnings) {
    list += QStringLiteral("<li>") + w.toHtmlEscaped() + QStringLiteral("</li>");
  }
  auto* text = new QLabel(QStringLiteral("<ul style=\"margin-left:-20px\">") + list +
                              QStringLiteral("</ul>"),
                          &dlg);
  text->setObjectName(QStringLiteral("nolvusChecksList"));
  text->setWordWrap(true);
  text->setTextFormat(Qt::RichText);
  text->setTextInteractionFlags(Qt::TextSelectableByMouse);
  dlg.body()->addWidget(text);

  auto* dontAsk = new QCheckBox(Text::tr("Don't show these again"), &dlg);
  dontAsk->setObjectName(QStringLiteral("nolvusChecksDontAsk"));
  dlg.body()->addWidget(dontAsk);

  auto* run    = dlg.addButton(Text::tr("Run anyway"), true);
  auto* cancel = dlg.addButton(Text::tr("Cancel"));
  QObject::connect(run, &QPushButton::clicked, &dlg, &QDialog::accept);
  QObject::connect(cancel, &QPushButton::clicked, &dlg, &QDialog::reject);
  dlg.resize(600, dlg.sizeHint().height());

  if (dlg.exec() != QDialog::Accepted) {
    return false;
  }
  if (dontAsk->isChecked()) {
    GlobalSettings::setValue(ackKey, hash);
  }
  return true;
}

bool refreshProfiles(const QString& iniPath, QString* error)
{
  QString dummy;
  if (!error) {
    error = &dummy;
  }

  const Link link = readLink(iniPath);
  if (!link.valid) {
    *error = Text::tr("This instance isn't linked to Nolvus.");
    return false;
  }

  const QString dir      = QFileInfo(iniPath).absolutePath();
  const QString profiles = dir + QStringLiteral("/profiles");
  const QString backup =
      dir + QStringLiteral("/profiles-backup-") +
      QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd-HHmmss"));

  if (QFileInfo(profiles).isDir() && !QDir().rename(profiles, backup)) {
    *error = Text::tr("Can't move %1 aside; is a file in it open?")
                 .arg(QDir::toNativeSeparators(profiles));
    return false;
  }

  if (!copyProfiles(QDir(link.modsPath).filePath(QStringLiteral("profiles")), profiles,
                    error)) {
    // back as it was
    QDir(profiles).removeRecursively();
    QDir().rename(backup, profiles);
    return false;
  }

  QSettings ini(iniPath, QSettings::IniFormat);
  ini.setValue(Section + QStringLiteral("/sourceProfileTime"),
               newestSourceProfile(link.modsPath));
  ini.sync();

  log::info("Nolvus: copied the profiles of '{}' again; the previous copy is in '{}'",
            link.name, QDir::toNativeSeparators(backup));
  return true;
}

}  // namespace Nolvus
