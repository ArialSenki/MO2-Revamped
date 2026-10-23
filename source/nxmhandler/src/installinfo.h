#ifndef NXMHANDLER_INSTALLINFO_H
#define NXMHANDLER_INSTALLINFO_H

// MO2 Revamped: friendly descriptions of the programs in the handler list and
// of the program Windows sends nxm links to. Everything here only reads files
// and version resources.

#include <QString>

struct InstallInfo
{
  enum class Kind
  {
    Revamped,     // MO2 Revamped (MO2-Revamped.install beside the program)
    ModOrganizer, // another Mod Organizer 2
    Vortex,
    Other
  };

  enum class Layout
  {
    Unknown,
    Global,   // instances in the user's AppData
    Portable, // portable.txt or ModOrganizer.ini beside the program
    Isolated  // an MO2 Revamped isolated copy (eldenring-only.portable)
  };

  QString executable;  // native separators
  QString folder;      // native separators
  bool exists = false;
  Kind kind   = Kind::Other;
  Layout layout = Layout::Unknown;
  QString version;     // product version, if any
  QString productName; // "MO2 Revamped", "Mod Organizer 2", or the file's own name

  // e.g. "MO2 Revamped 2.5.2"
  QString title() const;
  // e.g. "Portable"; empty when not a Mod Organizer
  QString layoutText() const;

  static InstallInfo describe(const QString& executable);
};

// a readable name for a Nexus game ID ("skyrimse" -> "SkyrimSE"); IDs that
// are not known are shown as they are
QString gameDisplayName(const QString& id);

// true when both paths name the same file (separators and case ignored)
bool samePath(const QString& a, const QString& b);

#endif  // NXMHANDLER_INSTALLINFO_H
