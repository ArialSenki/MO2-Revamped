#ifndef MODORGANIZER_ELDENRINGSAVESETTINGS_H
#define MODORGANIZER_ELDENRINGSAVESETTINGS_H

#include <iplugingame.h>

#include <QDir>
#include <QObject>
#include <QSettings>
#include <QString>

namespace EldenRingSaveSettings
{

inline constexpr auto ProfileIsolated = "profile";
inline constexpr auto InstanceShared  = "instance_shared";
inline constexpr auto GlobalShared    = "global";
inline constexpr auto ConfigFilename  = "eldenring_mo2_saves.ini";

// Elden Ring and Nightreign: the Revamped plugins route both games' saves
inline bool supports(const MOBase::IPluginGame* game)
{
  return game != nullptr &&
         (game->gameShortName().compare(QStringLiteral("eldenring"),
                                        Qt::CaseInsensitive) == 0 ||
          game->gameShortName().compare(QStringLiteral("nightreign"),
                                        Qt::CaseInsensitive) == 0);
}

inline QString normalizeMode(const QString& mode,
                             const QString& fallback = QString::fromLatin1(ProfileIsolated))
{
  const QString normalized = mode.trimmed().toLower();
  if (normalized == QLatin1String(ProfileIsolated) ||
      normalized == QLatin1String(InstanceShared) ||
      normalized == QLatin1String(GlobalShared)) {
    return normalized;
  }
  return fallback;
}

inline QString modeName(const QString& mode)
{
  if (mode == QLatin1String(InstanceShared)) {
    return QObject::tr("Share modded saves in this instance");
  }
  if (mode == QLatin1String(GlobalShared)) {
    return QObject::tr("Use the global Steam save");
  }
  // the names the Save Isolation card of the wizard gives the same modes
  return QObject::tr("Separate saves per MO2 profile");
}

inline QString modeDescription(const QString& mode)
{
  if (mode == QLatin1String(InstanceShared)) {
    return QObject::tr(
        "Profiles using this mode share one modded save folder inside this MO2 "
        "instance. Other instances remain separate.");
  }
  if (mode == QLatin1String(GlobalShared)) {
    return QObject::tr(
        "Uses the global Elden Ring save folder. Steam and other instances using "
        "the same global path can share it.");
  }
  return QObject::tr(
      "Each MO2 profile uses its own save folder. Different instances remain "
      "separate when their profile folders are separate.");
}

inline bool hasProfileMode(const QString& profileDirectory)
{
  QSettings config(QDir(profileDirectory).filePath(QLatin1String(ConfigFilename)),
                   QSettings::IniFormat);
  return config.contains(QStringLiteral("Saves/mode"));
}

inline QString readProfileMode(const QString& profileDirectory,
                               const QString& fallback =
                                   QString::fromLatin1(ProfileIsolated))
{
  QSettings config(QDir(profileDirectory).filePath(QLatin1String(ConfigFilename)),
                   QSettings::IniFormat);
  return normalizeMode(config.value(QStringLiteral("Saves/mode")).toString(),
                       fallback);
}

inline bool writeProfileMode(const QString& profileDirectory, const QString& mode)
{
  const QString normalized = normalizeMode(mode, QString());
  if (normalized.isEmpty()) {
    return false;
  }

  QSettings config(QDir(profileDirectory).filePath(QLatin1String(ConfigFilename)),
                   QSettings::IniFormat);
  config.setValue(QStringLiteral("Saves/mode"), normalized);
  config.sync();
  return config.status() == QSettings::NoError;
}

}  // namespace EldenRingSaveSettings

#endif  // MODORGANIZER_ELDENRINGSAVESETTINGS_H
