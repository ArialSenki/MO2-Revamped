#include "installinfo.h"
#include "handlerstorage.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QHash>

#include <windows.h>

#include <vector>

namespace
{

// one string from the version resource, in the first language it lists
QString versionString(const std::vector<BYTE>& block, const wchar_t* name)
{
  struct Translation
  {
    WORD language;
    WORD codePage;
  }* translations = nullptr;
  UINT size       = 0;
  if (!VerQueryValueW(block.data(), L"\\VarFileInfo\\Translation",
                      reinterpret_cast<void**>(&translations), &size) ||
      size < sizeof(Translation)) {
    return {};
  }
  wchar_t path[128];
  swprintf_s(path, L"\\StringFileInfo\\%04x%04x\\%s", translations[0].language,
             translations[0].codePage, name);
  wchar_t* value = nullptr;
  UINT length    = 0;
  if (!VerQueryValueW(block.data(), path, reinterpret_cast<void**>(&value), &length) ||
      length == 0) {
    return {};
  }
  return QString::fromWCharArray(value).trimmed();
}

struct VersionInfo
{
  QString productName;
  QString description;
  QString version;
};

VersionInfo readVersionInfo(const QString& executable)
{
  VersionInfo info;
  const std::wstring path = QDir::toNativeSeparators(executable).toStdWString();
  DWORD ignored           = 0;
  const DWORD size        = GetFileVersionInfoSizeW(path.c_str(), &ignored);
  if (size == 0) {
    return info;
  }
  std::vector<BYTE> block(size);
  if (!GetFileVersionInfoW(path.c_str(), 0, size, block.data())) {
    return info;
  }
  info.productName = versionString(block, L"ProductName");
  info.description = versionString(block, L"FileDescription");

  VS_FIXEDFILEINFO* fixed = nullptr;
  UINT length             = 0;
  if (VerQueryValueW(block.data(), L"\\", reinterpret_cast<void**>(&fixed), &length) &&
      fixed != nullptr) {
    const int major = HIWORD(fixed->dwProductVersionMS);
    const int minor = LOWORD(fixed->dwProductVersionMS);
    const int patch = HIWORD(fixed->dwProductVersionLS);
    info.version    = QStringLiteral("%1.%2.%3").arg(major).arg(minor).arg(patch);
  }
  return info;
}

}  // namespace

QString InstallInfo::title() const
{
  if (version.isEmpty() || kind == Kind::Other) {
    return productName;
  }
  return QStringLiteral("%1 %2").arg(productName, version);
}

QString InstallInfo::layoutText() const
{
  switch (layout) {
  case Layout::Global:
    return QCoreApplication::translate("InstallInfo", "Global instances");
  case Layout::Portable:
    return QCoreApplication::translate("InstallInfo", "Portable");
  case Layout::Isolated:
    return QCoreApplication::translate("InstallInfo", "Isolated copy");
  case Layout::Unknown:
    break;
  }
  return {};
}

InstallInfo InstallInfo::describe(const QString& executable)
{
  // the window repaints often; a program's description does not change while
  // it is open, apart from appearing or disappearing
  static QHash<QString, InstallInfo> cache;

  const QFileInfo file(executable);
  const QString key = QDir::fromNativeSeparators(executable).toLower();
  const bool exists = !executable.isEmpty() && file.isFile();
  if (auto it = cache.constFind(key); it != cache.constEnd() && it->exists == exists) {
    return *it;
  }

  InstallInfo info;
  info.executable = QDir::toNativeSeparators(executable);
  info.folder     = QDir::toNativeSeparators(file.absolutePath());
  info.exists     = exists;

  const QDir dir      = file.absoluteDir();
  const QString name  = file.fileName();
  const bool isMO     = name.compare(QStringLiteral("ModOrganizer.exe"), Qt::CaseInsensitive) == 0;
  const VersionInfo v = exists ? readVersionInfo(executable) : VersionInfo{};

  if (isMO && dir.exists(QStringLiteral("MO2-Revamped.install"))) {
    info.kind        = Kind::Revamped;
    info.productName = QStringLiteral("MO2 Revamped");
  } else if (isMO || v.productName.compare(QStringLiteral("Mod Organizer 2"), Qt::CaseInsensitive) == 0 ||
             v.productName.compare(QStringLiteral("Mod Organizer"), Qt::CaseInsensitive) == 0) {
    info.kind        = Kind::ModOrganizer;
    info.productName = QStringLiteral("Mod Organizer 2");
  } else if (name.compare(QStringLiteral("Vortex.exe"), Qt::CaseInsensitive) == 0 ||
             v.productName.compare(QStringLiteral("Vortex"), Qt::CaseInsensitive) == 0) {
    info.kind        = Kind::Vortex;
    info.productName = QStringLiteral("Vortex");
  } else {
    info.kind = Kind::Other;
    if (!v.description.isEmpty()) {
      info.productName = v.description;
    } else if (!v.productName.isEmpty()) {
      info.productName = v.productName;
    } else {
      info.productName = name.isEmpty() ? info.executable : name;
    }
  }
  info.version = v.version;

  if (info.kind == Kind::Revamped || info.kind == Kind::ModOrganizer) {
    // the same markers MO2 Revamped checks when it starts (GlobalSettings)
    if (dir.exists(QStringLiteral("eldenring-only.portable"))) {
      info.layout = Layout::Isolated;
    } else if (dir.exists(QStringLiteral("portable.txt")) ||
               dir.exists(QStringLiteral("ModOrganizer.ini"))) {
      info.layout = Layout::Portable;
    } else if (exists) {
      info.layout = Layout::Global;
    }
  }

  cache.insert(key, info);
  return info;
}

QString gameDisplayName(const QString& id)
{
  const QString key = id.trimmed();
  if (key.compare(QLatin1String("other"), Qt::CaseInsensitive) == 0) {
    return QCoreApplication::translate("InstallInfo", "Other games");
  }
  for (const auto& game : HandlerStorage::knownGames()) {
    if (key.compare(std::get<1>(game), Qt::CaseInsensitive) == 0 ||
        key.compare(std::get<2>(game), Qt::CaseInsensitive) == 0) {
      return std::get<0>(game);
    }
  }
  // the games Revamped's own plugins add, which upstream's list doesn't know
  if (key.compare(QLatin1String("eldenring"), Qt::CaseInsensitive) == 0) {
    return QStringLiteral("Elden Ring");
  }
  if (key.compare(QLatin1String("nightreign"), Qt::CaseInsensitive) == 0 ||
      key.compare(QLatin1String("eldenringnightreign"), Qt::CaseInsensitive) == 0) {
    return QStringLiteral("Elden Ring Nightreign");
  }
  return key;
}

bool samePath(const QString& a, const QString& b)
{
  if (a.isEmpty() || b.isEmpty()) {
    return false;
  }
  return QDir::cleanPath(QDir::fromNativeSeparators(a))
             .compare(QDir::cleanPath(QDir::fromNativeSeparators(b)), Qt::CaseInsensitive) == 0;
}
