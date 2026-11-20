// MO2 Revamped: the native stand-ins for the two installers that are C++/CLI
// (installer_fomod_csharp and installer_omod). Loading a mixed-mode DLL starts
// the .NET runtime, ~30-40 ms and its memory at every start, in every
// instance, for archives most lists never have. The stand-in is the plugin MO2
// loads, under the real one's file name: it answers what MO2 asks of every
// installer (name, settings, priority, whether an archive is its kind) by
// itself, and loads the real plugin, kept in a dotnet\ folder below its own
// (plugins\<name>\dotnet\, deeper than MO2 looks for plugins), only when it
// has something to install.
#pragma once

#include <QCoreApplication>
#include <QDir>
#include <QPluginLoader>
#include <QString>

#include <log.h>

#include <memory>

namespace RevampedDotNet
{

// the real plugin's file, relative to MO2's plugins folder
inline QString pluginsFolder()
{
  return QDir(QCoreApplication::applicationDirPath()).filePath("plugins");
}

// loads the real plugin once and returns it as T, or nullptr (logged)
template <class T>
T* loadReal(std::unique_ptr<QPluginLoader>& loader, const QString& relativePath)
{
  if (!loader) {
    const QString path = QDir(pluginsFolder()).filePath(relativePath);
    loader             = std::make_unique<QPluginLoader>(path);
    MOBase::log::debug("loading the .NET installer {} on first use", relativePath);
  }
  QObject* object = loader->instance();
  if (object == nullptr) {
    MOBase::log::error("the .NET installer {} could not be loaded: {}", relativePath,
                       loader->errorString());
    return nullptr;
  }
  T* plugin = qobject_cast<T*>(object);
  if (plugin == nullptr) {
    MOBase::log::error("{} is not the installer it should be", relativePath);
  }
  return plugin;
}

}  // namespace RevampedDotNet
