#include "moshortcut.h"

MOShortcut::MOShortcut(const QString& link)
    : m_valid(link.startsWith("moshortcut://")), m_hasInstance(false),
      m_hasExecutable(false)
{
  if (m_valid) {
    int start = (int)strlen("moshortcut://");
    int sep   = link.indexOf(':', start);
    if (sep >= 0) {
      m_hasInstance = true;
      m_instance    = link.mid(start, sep - start);
      m_executable  = link.mid(sep + 1);
    } else
      m_executable = link.mid(start);
    if (!(m_executable == ""))
      m_hasExecutable = true;
  }
}

bool MOShortcut::isValid() const
{
  return m_valid;
}

bool MOShortcut::hasInstance() const
{
  return m_hasInstance;
}

bool MOShortcut::hasExecutable() const
{
  return m_hasExecutable;
}

QString MOShortcut::instanceDisplayName() const
{
  if (m_instance.isEmpty()) {
    return QObject::tr("Portable");
  }

  if (m_instance.startsWith(QStringLiteral("portable/"))) {
    return QObject::tr("Portable - %1").arg(m_instance.mid(9));
  }

  return m_instance;
}

const QString& MOShortcut::instanceName() const
{
  return m_instance;
}

const QString& MOShortcut::executableName() const
{
  return m_executable;
}

bool MOShortcut::isForInstance(const Instance& i) const
{
  if (!m_hasInstance) {
    // no instance name was specified, so the current one is fine
    return true;
  }

  return i.instanceIdentifier() == m_instance;
}

QString MOShortcut::toString() const
{
  if (m_hasInstance) {
    return "moshortcut://" + m_instance + ":" + m_executable;
  } else {
    return "moshortcut://" + m_executable;
  }
}
