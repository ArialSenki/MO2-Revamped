#ifndef WABBAJACKINSTANCE_H
#define WABBAJACKINSTANCE_H

// What an instance made from a Wabbajack list keeps of the list, in its
// ModOrganizer.ini ([Wabbajack]): the list's name, the game version it was
// made for, its guide and the folders its guide has you copy by hand. Read
// when the instance first opens (what the guide still asks of you) and before
// a program in the game's folder starts (a game updated since).

#include <QFileInfo>
#include <QString>
#include <QStringList>

class OrganizerCore;
class QWidget;

namespace Wabbajack
{

struct InstanceInfo
{
  QString list;
  QString gameVersion;
  QString readme;
  QStringList manualFolders;
};

// written when the instance is made; the notice shows on its first opening
void writeInstanceInfo(const QString& iniPath, const InstanceInfo& info);

// once, the first time the instance opens: the guide's manual steps and the
// game version
void showListNotice(QWidget* parent, OrganizerCore& core);

// before a program in the game's folder starts: when the game isn't the
// version the list was made for, asks whether to start it anyway (once per
// session); false when the user cancels
bool confirmGameVersion(QWidget* parent, OrganizerCore& core, const QFileInfo& binary);

}  // namespace Wabbajack

#endif  // WABBAJACKINSTANCE_H
