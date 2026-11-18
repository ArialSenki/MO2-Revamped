#ifndef MODORGANIZER_NOLVUS_INCLUDED
#define MODORGANIZER_NOLVUS_INCLUDED

#include <QDateTime>
#include <QString>
#include <QStringList>
#include <utility>
#include <vector>

class QSettings;
class QWidget;

// MO2 Revamped: Nolvus instances, opened as linked instances.
//
// The Nolvus Dashboard installs each list with its own portable MO2 (in
// <instance>/MO2) and rewrites that folder on every Nolvus update. MO2 Revamped
// never opens that folder: linking a Nolvus instance creates a portable
// instance of its own (PortableInstances/<name>) whose ini points at Nolvus'
// mods, overwrite and stock game, with a copy of Nolvus' profiles, its own
// downloads folder and web cache. Nolvus' MO2 folder and profiles stay as they
// were; only what the user changes in the mods themselves (installing,
// removing, editing a mod) reaches Nolvus' files, as it would in Nolvus' MO2.
//
// Nothing here writes under the Nolvus folder.
namespace Nolvus
{

// a Nolvus instance found in a Nolvus folder
struct InstanceInfo
{
  QString root;         // the Nolvus folder, C:/Nolvus by default
  QString name;         // "Nolvus Awakening"
  QString version;      // "6.0.21", may be empty
  QString variant;      // "Redux", may be empty
  QString enb;          // the ENB option ("CABBAGE"), may be empty
  QString installPath;  // <root>/Instances/<name>
  QString archivePath;  // where the Dashboard keeps the archives
  QString stockGame;    // the game folder Nolvus made for this list
  QString mo2Path;      // Nolvus' own MO2 (never written)
  QString modsPath;     // its base directory (mods, profiles, overwrite)
  QString profile;      // the profile Nolvus' MO2 selects
  int modCount = 0;     // mods, separators not counted

  // the MO2 Revamped instance already linked to it, if any
  QString linkedInstance;
};

// what a linked instance's ini remembers ([Nolvus] section)
struct Link
{
  bool valid = false;
  QString root, name, version, variant, enb, installPath, archivePath, stockGame,
      mo2Path, modsPath;
  // when the Nolvus profiles were copied, and their modlist.txt time then
  QDateTime linkedAt, sourceProfileTime;
  // transferred into a normal instance (nolvustransfer.h): its folders are
  // its own, its profiles too; whether it registers for nxm:// links
  bool transferred = false;
  bool registerNxm = false;
};

// the folder the Dashboard installs to by default
QString defaultRoot();

// the default folder and the ones the user added
QStringList roots();
void addRoot(const QString& root);

// whether the folder holds Nolvus instances (Instances/InstancesData.xml, or
// an Instances/<name>/MO2/ModOrganizer.ini)
bool looksLikeRoot(const QString& dir);

// every Nolvus instance in the known folders, or in one of them; reads only
std::vector<InstanceInfo> findInstances();
std::vector<InstanceInfo> findInstances(const QString& root);

// the [Nolvus] section of an instance's ini; valid is false for any other
Link readLink(const QString& iniPath);
// linked, not transferred
bool isLinked(const QString& iniPath);
// a linked instance, or a transferred one whose user said no to nxm:// links
bool skipsNxmRegistration(const QString& iniPath);

// "Nolvus 6.0.21 · Redux", for captions
QString label(const QString& version, const QString& variant);

// the one-time explanation before linking; true to go on
bool confirmLink(QWidget* parent, const InstanceInfo& info);

// creates the linked instance and returns its folder, or an empty string and
// the reason
QString createLinkedInstance(const InstanceInfo& info, QString* error);

// the archive folders the Downloads tab lists read-only, with the name of
// their group
std::vector<std::pair<QString, QString>> archiveSources(const QString& iniPath);

// known Nolvus requirements that don't look met, in plain words; empty when
// all is well. binary is the program about to run (may be empty).
QStringList preRunWarnings(const Link& link, const QString& iniPath,
                           const QString& profileDir, const QString& binary);

// shows preRunWarnings() before a program runs; true to run it. Nothing is
// shown for an instance that isn't linked to Nolvus, or when all is well.
bool confirmLaunch(QWidget* parent, const QString& iniPath, const QString& profileDir,
                   const QString& binary);

// copies Nolvus' profiles over the linked instance's copy again (after a
// Nolvus update); the previous copy is kept in profiles-backup-<time>
bool refreshProfiles(const QString& iniPath, QString* error);

// shared with nolvustransfer.cpp
QString cleanPath(const QString& path);
bool samePaths(const QString& a, const QString& b);
QString iniValue(const QSettings& s, const QString& key);
void copyIniGroup(const QSettings& from, QSettings& to, const QString& group);

// what an instance made from Nolvus' MO2 ini carries over so its first start
// asks nothing Nolvus already answered: the MO2 version it was made with (no
// category mapping question), the interface settings, and the theme question
// as done
void writeCarriedSettings(const QSettings& source, QSettings& ini);

}  // namespace Nolvus

#endif  // MODORGANIZER_NOLVUS_INCLUDED
