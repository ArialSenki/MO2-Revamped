#include "gamestub.h"

#include <isavegame.h>
#include <log.h>

#include <QBuffer>
#include <QIcon>
#include <QPixmap>
#include <QJsonArray>
#include <QLocale>
#include <QMetaType>

using namespace MOBase;

namespace
{
QJsonArray toArray(const QStringList& list)
{
  QJsonArray a;
  for (const QString& s : list) {
    a.append(s);
  }
  return a;
}

QStringList toList(const QJsonValue& v)
{
  QStringList list;
  for (const auto& e : v.toArray()) {
    list.append(e.toString());
  }
  return list;
}

// a setting's default with its type, so that an int stays an int
QJsonObject settingToJson(const PluginSetting& s)
{
  QJsonObject o;
  o["key"]         = s.key;
  o["description"] = s.description;
  o["default"]     = QJsonValue::fromVariant(s.defaultValue);
  o["type"]        = s.defaultValue.metaType().id();
  return o;
}

PluginSetting settingFromJson(const QJsonObject& o)
{
  QVariant value = o.value("default").toVariant();
  const int type = o.value("type").toInt();
  if (type != 0 && value.metaType().id() != type) {
    value.convert(QMetaType(type));
  }
  return PluginSetting(o.value("key").toString(), o.value("description").toString(), value);
}
}  // namespace

QJsonObject GameStub::describe(IPluginGame* game)
{
  if (game == nullptr || !game->requirements().empty() || !game->master().isEmpty()) {
    return {};
  }
  QJsonObject o;
  o["name"]                = game->name();
  o["localizedName"]       = game->localizedName();
  o["author"]              = game->author();
  o["description"]         = game->description();
  o["language"]            = QLocale().name();
  o["version"]             = game->version().canonicalString();
  o["gameName"]            = game->gameName();
  o["displayGameName"]     = game->displayGameName();
  o["gameShortName"]       = game->gameShortName();
  o["lootGameName"]        = game->lootGameName();
  o["validShortNames"]     = toArray(game->validShortNames());
  o["gameNexusName"]       = game->gameNexusName();
  o["nexusModOrganizerID"] = game->nexusModOrganizerID();
  o["nexusGameID"]         = game->nexusGameID();
  o["steamAPPId"]          = game->steamAPPId();
  o["binaryName"]          = game->binaryName();
  o["launcherName"]        = game->getLauncherName();
  o["supportURL"]          = game->getSupportURL();
  o["gameVariants"]        = toArray(game->gameVariants());
  // the icon MO2 shows beside the game in menus (the Nexus pages of related
  // games, the instance window), as the real plugin drew it
  const QIcon icon = game->gameIcon();
  if (!icon.isNull()) {
    QByteArray png;
    QBuffer buffer(&png);
    if (buffer.open(QIODevice::WriteOnly) && icon.pixmap(32, 32).save(&buffer, "PNG")) {
      o["icon"] = QString::fromLatin1(png.toBase64());
    }
  }
  QJsonArray settings;
  for (const PluginSetting& s : game->settings()) {
    settings.append(settingToJson(s));
  }
  o["settings"] = settings;
  return o;
}

GameStub::GameStub(const QJsonObject& game, const QString& filepath)
    : m_Game(game), m_Filepath(filepath)
{}

GameStub::~GameStub()
{
  if (m_Loader) {
    m_Loader->unload();
  }
}

IPluginGame* GameStub::real() const
{
  if (m_Real != nullptr || m_Failed) {
    return m_Real;
  }
  // a call back into this one while the real plugin initializes gets nothing
  m_Failed        = true;
  m_Loader        = std::make_unique<QPluginLoader>(m_Filepath);
  QObject* object = m_Loader->instance();
  IPluginGame* game = object != nullptr ? qobject_cast<IPluginGame*>(object) : nullptr;
  if (game == nullptr || game->gameName() != gameName()) {
    log::error("the game plugin '{}' could not be loaded: {}", m_Filepath,
               object == nullptr ? m_Loader->errorString() : QStringLiteral("another game"));
    return nullptr;
  }
  log::debug("loaded the game plugin '{}' for '{}' on first use", m_Filepath, gameName());
  if (m_Organizer != nullptr && !game->init(m_Organizer)) {
    log::error("the game plugin '{}' failed to initialize", m_Filepath);
    return nullptr;
  }
  // what MO2 did with this one, in the same order: detection when it was
  // registered, then the path an instance or the user gave
  game->detectGame();
  if (m_PathSet) {
    game->setGamePath(m_GamePath);
  }
  if (m_VariantSet) {
    game->setGameVariant(m_Variant);
  }
  m_Real   = game;
  m_Failed = false;
  return m_Real;
}

bool GameStub::init(IOrganizer* organizer)
{
  m_Organizer = organizer;
  return true;
}

QString GameStub::name() const
{
  return m_Game.value("name").toString();
}

QString GameStub::localizedName() const
{
  // recorded in another language: the real plugin says it in this one
  if (m_Game.value("language").toString() != QLocale().name()) {
    if (IPluginGame* g = real()) {
      return g->localizedName();
    }
  }
  return m_Game.value("localizedName").toString();
}

QString GameStub::author() const
{
  return m_Game.value("author").toString();
}

QString GameStub::description() const
{
  if (m_Game.value("language").toString() != QLocale().name()) {
    if (IPluginGame* g = real()) {
      return g->description();
    }
  }
  return m_Game.value("description").toString();
}

VersionInfo GameStub::version() const
{
  return VersionInfo(m_Game.value("version").toString());
}

QList<PluginSetting> GameStub::settings() const
{
  QList<PluginSetting> list;
  for (const auto& s : m_Game.value("settings").toArray()) {
    list.append(settingFromJson(s.toObject()));
  }
  return list;
}

QString GameStub::gameName() const
{
  return m_Game.value("gameName").toString();
}

QString GameStub::displayGameName() const
{
  return m_Game.value("displayGameName").toString();
}

QString GameStub::gameShortName() const
{
  return m_Game.value("gameShortName").toString();
}

QString GameStub::lootGameName() const
{
  return m_Game.value("lootGameName").toString();
}

QStringList GameStub::validShortNames() const
{
  return toList(m_Game.value("validShortNames"));
}

QString GameStub::gameNexusName() const
{
  return m_Game.value("gameNexusName").toString();
}

int GameStub::nexusModOrganizerID() const
{
  return m_Game.value("nexusModOrganizerID").toInt();
}

int GameStub::nexusGameID() const
{
  return m_Game.value("nexusGameID").toInt();
}

QString GameStub::steamAPPId() const
{
  return m_Game.value("steamAPPId").toString();
}

QString GameStub::binaryName() const
{
  return m_Game.value("binaryName").toString();
}

QString GameStub::getLauncherName() const
{
  return m_Game.value("launcherName").toString();
}

QString GameStub::getSupportURL() const
{
  return m_Game.value("supportURL").toString();
}

QStringList GameStub::gameVariants() const
{
  return toList(m_Game.value("gameVariants"));
}

void GameStub::detectGame()
{
  // where the game is installed is asked of the real plugin, when someone
  // wants to know (isInstalled, gameDirectory...)
  if (m_Real != nullptr) {
    m_Real->detectGame();
  }
}

void GameStub::setGamePath(const QString& path)
{
  m_GamePath = path;
  m_PathSet  = true;
  if (m_Real != nullptr) {
    m_Real->setGamePath(path);
  }
}

void GameStub::setGameVariant(const QString& variant)
{
  m_Variant    = variant;
  m_VariantSet = true;
  if (m_Real != nullptr) {
    m_Real->setGameVariant(variant);
  }
}

void GameStub::initializeProfile(const QDir& directory, ProfileSettings settings) const
{
  if (IPluginGame* g = real()) {
    g->initializeProfile(directory, settings);
  }
}

std::vector<std::shared_ptr<const ISaveGame>> GameStub::listSaves(QDir folder) const
{
  IPluginGame* g = real();
  return g != nullptr ? g->listSaves(folder) : std::vector<std::shared_ptr<const ISaveGame>>{};
}

bool GameStub::isInstalled() const
{
  IPluginGame* g = real();
  return g != nullptr && g->isInstalled();
}

QIcon GameStub::gameIcon() const
{
  if (m_Real == nullptr && m_Game.contains("icon")) {
    QPixmap pixmap;
    if (pixmap.loadFromData(QByteArray::fromBase64(m_Game.value("icon").toString().toLatin1()),
                            "PNG")) {
      return QIcon(pixmap);
    }
  }
  IPluginGame* g = real();
  return g != nullptr ? g->gameIcon() : QIcon();
}

QDir GameStub::gameDirectory() const
{
  IPluginGame* g = real();
  return g != nullptr ? g->gameDirectory() : QDir();
}

QDir GameStub::dataDirectory() const
{
  IPluginGame* g = real();
  return g != nullptr ? g->dataDirectory() : QDir();
}

QMap<QString, QDir> GameStub::secondaryDataDirectories() const
{
  IPluginGame* g = real();
  return g != nullptr ? g->secondaryDataDirectories() : QMap<QString, QDir>{};
}

QDir GameStub::documentsDirectory() const
{
  IPluginGame* g = real();
  return g != nullptr ? g->documentsDirectory() : QDir();
}

QDir GameStub::savesDirectory() const
{
  IPluginGame* g = real();
  return g != nullptr ? g->savesDirectory() : QDir();
}

QList<ExecutableInfo> GameStub::executables() const
{
  IPluginGame* g = real();
  return g != nullptr ? g->executables() : QList<ExecutableInfo>{};
}

QList<ExecutableForcedLoadSetting> GameStub::executableForcedLoads() const
{
  IPluginGame* g = real();
  return g != nullptr ? g->executableForcedLoads() : QList<ExecutableForcedLoadSetting>{};
}

QStringList GameStub::primaryPlugins() const
{
  IPluginGame* g = real();
  return g != nullptr ? g->primaryPlugins() : QStringList{};
}

QStringList GameStub::enabledPlugins() const
{
  IPluginGame* g = real();
  return g != nullptr ? g->enabledPlugins() : QStringList{};
}

QStringList GameStub::primarySources() const
{
  IPluginGame* g = real();
  return g != nullptr ? g->primarySources() : QStringList{};
}

QStringList GameStub::iniFiles() const
{
  IPluginGame* g = real();
  return g != nullptr ? g->iniFiles() : QStringList{};
}

QStringList GameStub::DLCPlugins() const
{
  IPluginGame* g = real();
  return g != nullptr ? g->DLCPlugins() : QStringList{};
}

QStringList GameStub::CCPlugins() const
{
  IPluginGame* g = real();
  return g != nullptr ? g->CCPlugins() : QStringList{};
}

IPluginGame::LoadOrderMechanism GameStub::loadOrderMechanism() const
{
  IPluginGame* g = real();
  return g != nullptr ? g->loadOrderMechanism() : LoadOrderMechanism::None;
}

IPluginGame::SortMechanism GameStub::sortMechanism() const
{
  IPluginGame* g = real();
  return g != nullptr ? g->sortMechanism() : SortMechanism::NONE;
}

bool GameStub::looksValid(QDir const& dir) const
{
  IPluginGame* g = real();
  return g != nullptr && g->looksValid(dir);
}

QString GameStub::gameVersion() const
{
  IPluginGame* g = real();
  return g != nullptr ? g->gameVersion() : QString();
}
