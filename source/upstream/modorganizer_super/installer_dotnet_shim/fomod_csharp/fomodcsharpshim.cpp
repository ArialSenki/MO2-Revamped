#include "fomodcsharpshim.h"
#include "../common/dotnetplugin.h"

#include <ifiletree.h>
#include <imoinfo.h>

#include <QCoreApplication>

using namespace MOBase;

namespace
{
// the real plugin's own translations (installer_fomod_csharp_<language>.qm)
QString realTr(const char* text)
{
  return QCoreApplication::translate("InstallerFomodCSharp", text);
}

// InstallerFomodCSharp::findFomodDirectory and findScriptFile, unchanged
std::shared_ptr<const IFileTree> findFomodDirectory(std::shared_ptr<const IFileTree> tree)
{
  auto entry = tree->find("fomod", FileTreeEntry::DIRECTORY);
  if (entry != nullptr) {
    return entry->astree();
  }
  if (tree->empty()) {
    return nullptr;
  }
  // We need at least a directory:
  if (!tree->at(0)->isDir()) {
    return nullptr;
  }
  // But not two:
  if (tree->size() > 1 && tree->at(1)->isDir()) {
    return nullptr;
  }
  return findFomodDirectory(tree->at(0)->astree());
}

bool hasScriptFile(std::shared_ptr<const IFileTree> tree)
{
  auto fomodDirectory = findFomodDirectory(tree);
  if (fomodDirectory == nullptr) {
    return false;
  }
  for (auto e : *fomodDirectory) {
    if (e->isFile() && e->suffix().compare("cs", Qt::CaseInsensitive) == 0) {
      return true;
    }
  }
  return false;
}
}  // namespace

FomodCSharpShim::~FomodCSharpShim()
{
  if (m_Loader) {
    m_Loader->unload();
  }
}

bool FomodCSharpShim::init(IOrganizer* moInfo)
{
  m_MOInfo = moInfo;
  return true;
}

QString FomodCSharpShim::localizedName() const
{
  return realTr("Fomod Installer C#");
}

QString FomodCSharpShim::description() const
{
  return realTr("Installer for C# based FOMOD archives.");
}

QList<PluginSetting> FomodCSharpShim::settings() const
{
  return {PluginSetting("enabled", "check to enable this plugin", QVariant(true)),
          PluginSetting("prefer", "prefer this over the NCC based plugin", QVariant(true))};
}

unsigned int FomodCSharpShim::priority() const
{
  // It's the same priority as the FOMOD installer but those should never conflict:
  return m_MOInfo->pluginSetting(name(), "prefer").toBool() ? 110 : 90;
}

bool FomodCSharpShim::isArchiveSupported(std::shared_ptr<const IFileTree> tree) const
{
  return hasScriptFile(tree);
}

IPluginInstallerSimple* FomodCSharpShim::real()
{
  if (m_Real == nullptr && !m_Failed) {
    // one try: init() can't be run twice on the same object
    m_Failed    = true;
    auto* plugin = RevampedDotNet::loadReal<IPluginInstallerSimple>(
        m_Loader, "installer_fomod_csharp/dotnet/installer_fomod_csharp.dll");
    if (plugin != nullptr && plugin->init(m_MOInfo)) {
      m_Real   = plugin;
      m_Failed = false;
    } else if (plugin != nullptr) {
      log::error("the C# FOMOD installer failed to initialize");
    }
  }
  if (m_Real != nullptr) {
    m_Real->setParentWidget(parentWidget());
    m_Real->setInstallationManager(manager());
  }
  return m_Real;
}

void FomodCSharpShim::onInstallationStart(QString const& archive, bool reinstallation,
                                          IModInterface* currentMod)
{
  // the real plugin keeps nothing between installations; it hears about one
  // only once it is loaded
  if (m_Real != nullptr) {
    m_Real->onInstallationStart(archive, reinstallation, currentMod);
  }
}

void FomodCSharpShim::onInstallationEnd(EInstallResult result, IModInterface* newMod)
{
  if (m_Real != nullptr) {
    m_Real->onInstallationEnd(result, newMod);
  }
}

FomodCSharpShim::EInstallResult FomodCSharpShim::install(GuessedValue<QString>& modName,
                                                         std::shared_ptr<IFileTree>& tree,
                                                         QString& version, int& modID)
{
  IPluginInstallerSimple* plugin = real();
  if (plugin == nullptr) {
    return RESULT_FAILED;
  }
  return plugin->install(modName, tree, version, modID);
}
