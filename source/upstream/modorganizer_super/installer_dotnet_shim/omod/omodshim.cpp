#include "omodshim.h"
#include "../common/dotnetplugin.h"
#include "launchhooks.h"

#include <ifiletree.h>
#include <imodinterface.h>
#include <imoinfo.h>
#include <pluginrequirements.h>

#include <QCoreApplication>
#include <QRegularExpression>

using namespace MOBase;

namespace
{
// the real plugin's own translations (installer_omod_<language>.qm)
QString realTr(const char* text)
{
  return QCoreApplication::translate("InstallerOMOD", text);
}
}  // namespace

OmodShim::OmodShim()  = default;
OmodShim::~OmodShim()
{
  // before m_Hooks goes: the real plugin keeps a pointer to it
  if (m_Loader) {
    m_Loader->unload();
  }
}

bool OmodShim::init(IOrganizer* moInfo)
{
  m_MOInfo = moInfo;
  m_Hooks  = std::make_unique<LaunchHooksOrganizer>(moInfo, this, nullptr);
  // the real plugin's own start and end callbacks run from these
  m_MOInfo->onAboutToRun([this](const QString& name, const QDir& dir, const QString& args) {
    return aboutToRun(name, dir, args);
  });
  m_MOInfo->onFinishedRun([this](const QString& name, unsigned int code) {
    if (m_Real != nullptr) {
      m_Hooks->finishedRun(name, code);
    }
  });
  return true;
}

QString OmodShim::localizedName() const
{
  return realTr("Omod Installer");
}

QString OmodShim::description() const
{
  return realTr("Installer for Omod files (including scripted ones)");
}

std::vector<std::shared_ptr<const IPluginRequirement>> OmodShim::requirements() const
{
  return {PluginRequirementFactory::gameDependency("Oblivion")};
}

QList<PluginSetting> OmodShim::settings() const
{
  return {};
}

bool OmodShim::isArchiveSupported(std::shared_ptr<const IFileTree> tree) const
{
  for (const auto file : *tree) {
    // config is the only file guaranteed to be there
    if (file->isFile() && file->name() == "config")
      return true;
  }
  return false;
}

bool OmodShim::isArchiveSupported(const QString& archiveName) const
{
  return archiveName.endsWith(".omod", Qt::CaseInsensitive);
}

void OmodShim::setParentWidget(QWidget* parent)
{
  IPluginInstaller::setParentWidget(parent);
  if (m_Real != nullptr) {
    m_Real->setParentWidget(parent);
  }
}

IPluginInstallerCustom* OmodShim::real()
{
  if (m_Real == nullptr && !m_Failed) {
    // one try: a second init() would register its callbacks again
    m_Failed     = true;
    auto* plugin = RevampedDotNet::loadReal<IPluginInstallerCustom>(
        m_Loader, "installer_omod/dotnet/installer_omod.dll");
    if (plugin != nullptr) {
      m_Hooks->setReal(plugin);
      if (plugin->init(m_Hooks.get())) {
        m_Real   = plugin;
        m_Failed = false;
      } else {
        log::error("the OMOD installer failed to initialize");
      }
    }
  }
  if (m_Real != nullptr) {
    m_Real->setParentWidget(parentWidget());
    m_Real->setInstallationManager(manager());
  }
  return m_Real;
}

void OmodShim::onInstallationStart(QString const& archive, bool reinstallation,
                                   IModInterface* currentMod)
{
  if (m_Real != nullptr) {
    m_Real->onInstallationStart(archive, reinstallation, currentMod);
  }
}

void OmodShim::onInstallationEnd(EInstallResult result, IModInterface* newMod)
{
  // after an OMOD it finishes what the OMOD asked for (plugins to enable...),
  // also for a mod installed into one that still has OMOD actions pending
  if (m_Real == nullptr && result == RESULT_SUCCESS && newMod != nullptr &&
      !newMod->pluginSetting(name(), "omodsPendingPostInstall", QStringList())
           .toStringList()
           .isEmpty()) {
    real();
  }
  if (m_Real != nullptr) {
    m_Real->onInstallationEnd(result, newMod);
  }
}

OmodShim::EInstallResult OmodShim::install(GuessedValue<QString>& modName, QString gameName,
                                           const QString& archiveName,
                                           const QString& version, int nexusID)
{
  IPluginInstallerCustom* plugin = real();
  if (plugin == nullptr) {
    return RESULT_FAILED;
  }
  return plugin->install(modName, gameName, archiveName, version, nexusID);
}

// what InstallerOMOD::buildSDPs works on: numbered folders of shader
// replacements an OMOD installed
bool OmodShim::hasShaderReplacements() const
{
  const QStringList directories = m_MOInfo->listDirectories("Shaders/OMOD");
  return !directories.filter(QRegularExpression("\\d+")).isEmpty();
}

bool OmodShim::aboutToRun(const QString& name, const QDir& dir, const QString& args)
{
  if (m_Real == nullptr && hasShaderReplacements()) {
    real();
  }
  return m_Real == nullptr || m_Hooks->aboutToRun(name, dir, args);
}

MappingType OmodShim::mappings() const
{
  // the combined shader packages the real plugin built for this start
  if (m_Real == nullptr) {
    return {};
  }
  auto* mapper = qobject_cast<IPluginFileMapper*>(m_Loader->instance());
  return mapper != nullptr ? mapper->mappings() : MappingType{};
}
