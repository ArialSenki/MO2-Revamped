#include "envshortcut.h"
#include "env.h"
#include "executableslist.h"
#include "filesystemutilities.h"
#include "instancemanager.h"
#include <log.h>
#include <utility.h>

#include <QDirIterator>

#include <shellapi.h>
#include <vector>

namespace env
{

using namespace MOBase;

class ShellLinkException
{
public:
  ShellLinkException(QString s) : m_what(std::move(s)) {}

  const QString& what() const { return m_what; }

private:
  QString m_what;
};

// just a wrapper around IShellLink operations that throws ShellLinkException
// on errors
//
class ShellLinkWrapper
{
public:
  ShellLinkWrapper()
  {
    m_link = createShellLink();
    m_file = createPersistFile();
  }

  void setPath(const QString& s)
  {
    if (s.isEmpty()) {
      throw ShellLinkException("path cannot be empty");
    }

    const auto r = m_link->SetPath(s.toStdWString().c_str());
    throwOnFail(r, QString("failed to set target path '%1'").arg(s));
  }

  void setArguments(const QString& s)
  {
    const auto r = m_link->SetArguments(s.toStdWString().c_str());
    throwOnFail(r, QString("failed to set arguments '%1'").arg(s));
  }

  void setDescription(const QString& s)
  {
    if (s.isEmpty()) {
      return;
    }

    const auto r = m_link->SetDescription(s.toStdWString().c_str());
    throwOnFail(r, QString("failed to set description '%1'").arg(s));
  }

  void setIcon(const QString& file, int i)
  {
    if (file.isEmpty()) {
      return;
    }

    const auto r = m_link->SetIconLocation(file.toStdWString().c_str(), i);
    throwOnFail(r, QString("failed to set icon '%1' @ %2").arg(file).arg(i));
  }

  void setWorkingDirectory(const QString& s)
  {
    if (s.isEmpty()) {
      return;
    }

    const auto r = m_link->SetWorkingDirectory(s.toStdWString().c_str());
    throwOnFail(r, QString("failed to set working directory '%1'").arg(s));
  }

  void save(const QString& path)
  {
    const auto r = m_file->Save(path.toStdWString().c_str(), TRUE);
    throwOnFail(r, QString("failed to save link '%1'").arg(path));
  }

private:
  COMPtr<IShellLink> m_link;
  COMPtr<IPersistFile> m_file;

  void throwOnFail(HRESULT r, const QString& s)
  {
    if (FAILED(r)) {
      throw ShellLinkException(QString("%1, %2").arg(s).arg(formatSystemMessage(r)));
    }
  }

  COMPtr<IShellLink> createShellLink()
  {
    void* link = nullptr;

    const auto r = CoCreateInstance(CLSID_ShellLink, nullptr, CLSCTX_INPROC_SERVER,
                                    IID_IShellLink, &link);

    throwOnFail(r, "failed to create IShellLink instance");

    if (!link) {
      throw ShellLinkException("creating IShellLink worked, pointer is null");
    }

    return COMPtr<IShellLink>(static_cast<IShellLink*>(link));
  }

  COMPtr<IPersistFile> createPersistFile()
  {
    void* file = nullptr;

    const auto r = m_link->QueryInterface(IID_IPersistFile, &file);
    throwOnFail(r, "failed to get IPersistFile interface");

    if (!file) {
      throw ShellLinkException("querying IPersistFile worked, pointer is null");
    }

    return COMPtr<IPersistFile>(static_cast<IPersistFile*>(file));
  }
};

// MO2 Revamped: the arguments of an existing .lnk file, empty if it can't be
// read
static QString linkArguments(const QString& path)
{
  void* p = nullptr;
  if (FAILED(CoCreateInstance(CLSID_ShellLink, nullptr, CLSCTX_INPROC_SERVER,
                              IID_IShellLink, &p)) ||
      !p) {
    return {};
  }
  COMPtr<IShellLink> link(static_cast<IShellLink*>(p));

  void* f = nullptr;
  if (FAILED(link->QueryInterface(IID_IPersistFile, &f)) || !f) {
    return {};
  }
  COMPtr<IPersistFile> file(static_cast<IPersistFile*>(f));

  if (FAILED(file->Load(path.toStdWString().c_str(), STGM_READ))) {
    return {};
  }

  // a shortcut's arguments can be as long as a command line
  std::vector<wchar_t> buffer(32768, L'\0');
  if (FAILED(link->GetArguments(buffer.data(), static_cast<int>(buffer.size() - 1)))) {
    return {};
  }

  return QString::fromWCharArray(buffer.data());
}

Shortcut::Shortcut() : m_iconIndex(0) {}

Shortcut::Shortcut(const Executable& exe) : Shortcut()
{
  const auto i = *InstanceManager::singleton().currentInstance();

  m_name   = MOBase::sanitizeFileName(exe.title());
  m_target = QFileInfo(qApp->applicationFilePath()).absoluteFilePath();

  m_arguments = QString("\"moshortcut://%1:%2\"")
                    .arg(i.instanceIdentifier())
                    .arg(exe.title());

  // MO2 Revamped: a program with the same title in another instance already
  // has "<title>.lnk"; this one is "<title> (<instance>)" so neither replaces
  // the other
  // (chosen in each location by shortcutPath(): the Desktop and the Start
  // menu can differ)
  m_ours      = QString("moshortcut://%1:").arg(i.instanceIdentifier());
  m_otherName = MOBase::sanitizeFileName(
      QString("%1 (%2)").arg(exe.title()).arg(i.displayName()));

  m_description = QString("Run %1 with ModOrganizer").arg(exe.title());

  if (exe.usesOwnIcon()) {
    m_icon = exe.binaryInfo().absoluteFilePath();
  }

  m_workingDirectory = qApp->applicationDirPath();
}

Shortcut& Shortcut::name(const QString& s)
{
  m_name = MOBase::sanitizeFileName(s);
  return *this;
}

Shortcut& Shortcut::target(const QString& s)
{
  m_target = s;
  return *this;
}

Shortcut& Shortcut::arguments(const QString& s)
{
  m_arguments = s;
  return *this;
}

Shortcut& Shortcut::description(const QString& s)
{
  m_description = s;
  return *this;
}

Shortcut& Shortcut::icon(const QString& s, int index)
{
  m_icon      = s;
  m_iconIndex = index;
  return *this;
}

Shortcut& Shortcut::workingDirectory(const QString& s)
{
  m_workingDirectory = s;
  return *this;
}

bool Shortcut::exists(Locations loc) const
{
  const auto path = shortcutPath(loc);
  if (path.isEmpty()) {
    return false;
  }

  return QFileInfo(path).exists();
}

bool Shortcut::toggle(Locations loc)
{
  if (exists(loc)) {
    return remove(loc);
  } else {
    return add(loc);
  }
}

bool Shortcut::add(Locations loc)
{
  log::debug("adding shortcut to {}:\n"
             "  . name: '{}'\n"
             "  . target: '{}'\n"
             "  . arguments: '{}'\n"
             "  . description: '{}'\n"
             "  . icon: '{}' @ {}\n"
             "  . working directory: '{}'",
             toString(loc), m_name, m_target, m_arguments, m_description, m_icon,
             m_iconIndex, m_workingDirectory);

  if (m_target.isEmpty()) {
    log::error("shortcut: target is empty");
    return false;
  }

  const auto path = shortcutPath(loc);
  if (path.isEmpty()) {
    return false;
  }

  log::debug("shorcut file will be saved at '{}'", path);

  try {
    ShellLinkWrapper link;

    link.setPath(m_target);
    link.setArguments(m_arguments);
    link.setDescription(m_description);
    link.setIcon(m_icon, m_iconIndex);
    link.setWorkingDirectory(m_workingDirectory);

    link.save(path);

    return true;
  } catch (ShellLinkException& e) {
    log::error("{}\nshortcut file was not saved", e.what());
  }

  return false;
}

bool Shortcut::remove(Locations loc)
{
  log::debug("removing shortcut for '{}' from {}", m_name, toString(loc));

  const auto path = shortcutPath(loc);
  if (path.isEmpty()) {
    return false;
  }

  log::debug("path to shortcut file is '{}'", path);

  if (!QFile::exists(path)) {
    log::error("can't remove shortcut '{}', file not found", path);
    return false;
  }

  if (!MOBase::shellDelete({path})) {
    const auto e = ::GetLastError();

    log::error("failed to remove shortcut '{}', {}", path, formatSystemMessage(e));

    return false;
  }

  return true;
}

QString Shortcut::shortcutPath(Locations loc) const
{
  const auto dir = shortcutDirectory(loc);
  if (dir.isEmpty()) {
    return {};
  }

  const auto file = shortcutFilename();
  if (file.isEmpty()) {
    return {};
  }

  const QString plain = dir + QDir::separator() + file;
  if (m_otherName.isEmpty() || m_ours.isEmpty()) {
    return plain;
  }

  // this instance's link under the other name stays where it is
  const QString other = dir + QDir::separator() + m_otherName + ".lnk";
  if (QFileInfo::exists(other) &&
      linkArguments(other).contains(m_ours, Qt::CaseInsensitive)) {
    return other;
  }
  // the plain name taken by another instance's link
  if (QFileInfo::exists(plain)) {
    const auto args = linkArguments(plain);
    if (args.contains("moshortcut://", Qt::CaseInsensitive) &&
        !args.contains(m_ours, Qt::CaseInsensitive)) {
      return other;
    }
  }
  return plain;
}

QString Shortcut::shortcutDirectory(Locations loc) const
{
  QString dir;

  try {
    switch (loc) {
    case Desktop:
      dir = MOBase::getDesktopDirectory();
      break;

    case StartMenu:
      dir = MOBase::getStartMenuDirectory();
      break;

    case None:
    default:
      log::error("shortcut: bad location {}", loc);
      break;
    }
  } catch (std::exception&) {
  }

  return QDir::toNativeSeparators(dir);
}

QString Shortcut::shortcutFilename() const
{
  if (m_name.isEmpty()) {
    log::error("shortcut name is empty");
    return {};
  }

  return m_name + ".lnk";
}

QString toString(Shortcut::Locations loc)
{
  switch (loc) {
  case Shortcut::None:
    return "none";

  case Shortcut::Desktop:
    return "desktop";

  case Shortcut::StartMenu:
    return "start menu";

  default:
    return QString("? (%1)").arg(static_cast<int>(loc));
  }
}

Shortcut instanceShortcut(const Instance& i)
{
  // the root portable instance too, or the shortcut would open whichever
  // instance was used last
  const QString id = i.commandLineIdentifier();
  Shortcut shortcut;
  shortcut.name(QStringLiteral("Mod Organizer - %1").arg(i.displayName()))
      .target(QFileInfo(qApp->applicationFilePath()).absoluteFilePath())
      .arguments(id.isEmpty() ? QString() : QStringLiteral("-i \"%1\"").arg(id))
      .description(QObject::tr("Open the %1 instance in Mod Organizer").arg(i.displayName()))
      .workingDirectory(qApp->applicationDirPath());
  return shortcut;
}

namespace
{

// an existing .lnk file, opened to read its target and arguments and to save new
// arguments; everything else in it stays
class LoadedLink
{
public:
  explicit LoadedLink(const QString& path) : m_path(path)
  {
    void* p = nullptr;
    if (FAILED(CoCreateInstance(CLSID_ShellLink, nullptr, CLSCTX_INPROC_SERVER,
                                IID_IShellLink, &p)) ||
        !p) {
      return;
    }
    m_link.reset(static_cast<IShellLink*>(p));

    void* f = nullptr;
    if (FAILED(m_link->QueryInterface(IID_IPersistFile, &f)) || !f) {
      return;
    }
    m_file.reset(static_cast<IPersistFile*>(f));

    m_loaded = SUCCEEDED(m_file->Load(path.toStdWString().c_str(), STGM_READ));
  }

  bool loaded() const { return m_loaded; }

  QString target() const
  {
    wchar_t buffer[MAX_PATH + 1] = {};
    if (FAILED(m_link->GetPath(buffer, MAX_PATH, nullptr, SLGP_RAWPATH))) {
      return {};
    }
    return QString::fromWCharArray(buffer);
  }

  QString arguments() const
  {
    // as long as a command line can be: a shorter buffer cut them, and the cut
    // arguments were saved back with the new profile name
    std::vector<wchar_t> buffer(32768, L'\0');
    if (FAILED(
            m_link->GetArguments(buffer.data(), static_cast<int>(buffer.size() - 1)))) {
      return {};
    }
    return QString::fromWCharArray(buffer.data());
  }

  bool setArguments(const QString& s)
  {
    return SUCCEEDED(m_link->SetArguments(s.toStdWString().c_str())) &&
           SUCCEEDED(m_file->Save(m_path.toStdWString().c_str(), TRUE));
  }

private:
  QString m_path;
  COMPtr<IShellLink> m_link;
  COMPtr<IPersistFile> m_file;
  bool m_loaded = false;
};

// the arguments as Windows splits them for the program
QStringList splitArguments(const QString& arguments)
{
  QStringList result;
  if (arguments.trimmed().isEmpty()) {
    return result;
  }

  // a program name first, so the arguments are split as the program sees them
  const std::wstring line = L"x " + arguments.toStdWString();
  int count               = 0;
  LPWSTR* argv            = ::CommandLineToArgvW(line.c_str(), &count);
  if (argv == nullptr) {
    return result;
  }
  for (int a = 1; a < count; ++a) {
    result.append(QString::fromWCharArray(argv[a]));
  }
  ::LocalFree(argv);
  return result;
}

// back into one line, quoted the way CommandLineToArgvW() splits it again
QString joinArguments(const QStringList& arguments)
{
  QStringList quoted;
  for (const QString& arg : arguments) {
    const bool plain = !arg.isEmpty() && !arg.contains(QLatin1Char(' ')) &&
                       !arg.contains(QLatin1Char('\t')) && !arg.contains(QLatin1Char('"'));
    if (plain) {
      quoted.append(arg);
      continue;
    }

    QString out = QStringLiteral("\"");
    int backslashes = 0;
    for (const QChar c : arg) {
      if (c == QLatin1Char('\\')) {
        ++backslashes;
        continue;
      }
      if (c == QLatin1Char('"')) {
        out += QString(backslashes * 2 + 1, QLatin1Char('\\'));
      } else {
        out += QString(backslashes, QLatin1Char('\\'));
      }
      out += c;
      backslashes = 0;
    }
    out += QString(backslashes * 2, QLatin1Char('\\'));
    out += QLatin1Char('"');
    quoted.append(out);
  }
  return quoted.join(QLatin1Char(' '));
}

// where an option's value is: "-p name", "--profile name", "--profile=name" or
// "-pname"; the index of the token holding it and where the value starts in it
struct OptionValue
{
  int token  = -1;
  int offset = 0;

  bool found() const { return token >= 0; }
};

OptionValue findOption(const QStringList& args, const QString& shortName,
                       const QString& longName)
{
  for (int a = 0; a < args.size(); ++a) {
    const QString& arg = args[a];
    if (arg == shortName || arg == longName) {
      if (a + 1 < args.size()) {
        return {a + 1, 0};
      }
      return {};
    }
    if (arg.startsWith(longName + QLatin1Char('='))) {
      return {a, static_cast<int>(longName.size()) + 1};
    }
    if (!arg.startsWith(QStringLiteral("--")) && arg.startsWith(shortName) &&
        arg.size() > shortName.size()) {
      return {a, static_cast<int>(shortName.size())};
    }
  }
  return {};
}

}  // namespace

ProfileShortcuts renameProfileInShortcuts(const Instance& i, const QString& oldName,
                                          const QString& newName)
{
  ProfileShortcuts result;

  // the desktop's own links only (folders there can be large); the Start menu
  // with its subfolders
  std::vector<std::pair<QString, QDirIterator::IteratorFlags>> folders;
  try {
    folders.emplace_back(MOBase::getDesktopDirectory(), QDirIterator::NoIteratorFlags);
  } catch (std::exception&) {
  }
  try {
    folders.emplace_back(MOBase::getStartMenuDirectory(), QDirIterator::Subdirectories);
  } catch (std::exception&) {
  }

  const QString appPath = QFileInfo(qApp->applicationFilePath()).absoluteFilePath();
  const QString appName = QFileInfo(appPath).fileName();
  const QString id      = i.instanceIdentifier();
  const QString lineId  = i.commandLineIdentifier();
  // the portable instance beside the program opens when no instance is given
  const bool besideProgram = i.isPortable() && id.isEmpty();

  for (const auto& [folder, flags] : folders) {
    if (folder.isEmpty()) {
      continue;
    }
    QDirIterator it(folder, {QStringLiteral("*.lnk")}, QDir::Files, flags);
    while (it.hasNext()) {
      const QString path = it.next();
      LoadedLink link(path);
      if (!link.loaded()) {
        continue;
      }

      const QString target = link.target();
      if (QFileInfo(target).fileName().compare(appName, Qt::CaseInsensitive) != 0) {
        continue;
      }

      QStringList args         = splitArguments(link.arguments());
      const OptionValue profile = findOption(args, QStringLiteral("-p"),
                                             QStringLiteral("--profile"));
      if (!profile.found() ||
          args[profile.token].mid(profile.offset).compare(oldName, Qt::CaseInsensitive) !=
              0) {
        continue;
      }

      // whose it is: -i, or the instance in a moshortcut:// link
      bool ours = false, known = false;
      const OptionValue instance = findOption(args, QStringLiteral("-i"),
                                              QStringLiteral("--instance"));
      if (instance.found()) {
        known             = true;
        const QString val = args[instance.token].mid(instance.offset);
        ours              = val.compare(lineId, Qt::CaseInsensitive) == 0 ||
               (!id.isEmpty() && val.compare(id, Qt::CaseInsensitive) == 0);
      } else {
        for (const QString& arg : args) {
          if (arg.startsWith(QStringLiteral("moshortcut://"), Qt::CaseInsensitive)) {
            known = true;
            ours  = arg.startsWith(QStringLiteral("moshortcut://%1:").arg(id),
                                   Qt::CaseInsensitive);
            break;
          }
        }
      }
      if (!known && besideProgram &&
          QFileInfo(target).absoluteFilePath().compare(appPath, Qt::CaseInsensitive) ==
              0) {
        known = true;
        ours  = true;
      }

      if (!known) {
        result.unsure.append(QDir::toNativeSeparators(path));
        continue;
      }
      if (!ours) {
        continue;
      }

      args[profile.token] = args[profile.token].left(profile.offset) + newName;
      if (link.setArguments(joinArguments(args))) {
        log::info("shortcut '{}' now opens the profile '{}'", path, newName);
        result.updated.append(QDir::toNativeSeparators(path));
      } else {
        log::error("can't update the profile in the shortcut '{}'", path);
        result.failed.append(QDir::toNativeSeparators(path));
      }
    }
  }

  return result;
}

}  // namespace env
