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

// an instance whose install left folders for the guide's manual steps
bool hasManualSteps(OrganizerCore& core);

// the folders of the guide's manual steps, what each needs, with the files
// for the game's folder to copy (what they replace backed up) and the list's
// patchers to run (each only when the user asks, with a question first)
void showManualSteps(QWidget* parent, OrganizerCore& core);

}  // namespace Wabbajack

#endif  // WABBAJACKINSTANCE_H
