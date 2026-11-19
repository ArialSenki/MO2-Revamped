#ifndef MODORGANIZER_NOLVUSTRANSFER_INCLUDED
#define MODORGANIZER_NOLVUSTRANSFER_INCLUDED

#include "nolvus.h"
#include <QString>
#include <atomic>
#include <cstdint>
#include <mutex>
#include <vector>

class QWidget;

// MO2 Revamped: turns a Nolvus install (or a linked instance of one) into a
// normal portable instance that owns its folders.
//
//  - Move: the list's folders (mods, overwrite, profiles, stock game, tools,
//    and the archives if chosen) are renamed into the instance's data folder.
//    Same drive only, so it takes seconds. The Nolvus Dashboard can't update
//    that install any more.
//  - Copy: the same folders are copied (in parallel, with progress); Nolvus
//    keeps working and its updates only reach its own copy.
//  - In place: the instance uses the folders where they are and owns them
//    (its profiles are Nolvus' own); nothing is moved.
//
// Every step is written to nolvus-transfer.jsonl in the instance folder
// before it is done, so a failed or interrupted transfer can be put back
// (undoTransfer()), and a finished move can be undone later.
namespace Nolvus
{

struct TransferSource
{
  InstanceInfo info;
  // the linked instance being turned into a normal one, if any; its own
  // profiles are kept
  QString linkedInstance;
};

struct TransferOptions
{
  enum class Mode
  {
    Move,
    Copy,
    InPlace
  };
  enum class Archives
  {
    Move,
    Copy,
    Leave
  };

  Mode mode         = Mode::Copy;
  Archives archives = Archives::Leave;
  bool keepSaves    = true;
  bool registerNxm  = false;
  // where the mods, profiles, stock game and downloads go (Move/Copy); the
  // instance folder when empty
  QString dataDir;
};

// sizes of what a transfer can take, in bytes
struct TransferSizes
{
  std::uint64_t mods = 0, overwrite = 0, profiles = 0, saves = 0, stockGame = 0,
                tools = 0, archives = 0;
  std::uint64_t files = 0;
};

// shared between the worker and the window
struct TransferProgress
{
  std::atomic<std::uint64_t> done{0}, total{0};
  std::atomic_bool cancel{false}, finished{false}, failed{false};
  std::mutex mutex;
  QString step, error, instanceDir;
};

// walks the source folders; slow on a cold disk (hundreds of thousands of
// files), so it runs off the GUI thread and stops when cancel is set
TransferSizes measureTransfer(const TransferSource& source, const std::atomic_bool& cancel);

// the folder a new transferred instance gets (in PortableInstances)
QString transferInstanceDir(const TransferSource& source);

// whether two folders are on the same volume (a move is then a rename); the
// target may not exist yet
bool sameVolume(const QString& a, const QString& b);

// free space where the folder is (or would be)
std::uint64_t freeSpace(const QString& dir);

// runs a transfer; call it on a worker thread. On failure everything done is
// put back before it returns.
void runTransfer(const TransferSource& source, const TransferOptions& options,
                 TransferProgress& progress);

// transfers that neither finished nor were put back (MO2 closed or crashed
// during one): their folders in PortableInstances
std::vector<QString> interruptedTransfers();

// a move the instance's manifest can put back
bool canUndoTransfer(const QString& instanceDir);

// puts a moved transfer back where it came from; the instance stops being one
// (its ini is renamed) unless it was a linked instance, which is restored
// refused for the instance in use; copies go to the Recycle Bin; the record
// stays (and the undo can be tried again) while anything couldn't be put back
bool undoTransfer(const QString& instanceDir, QString* error, QWidget* parent = nullptr);

// the paths of a renamed instance's transfer record
void retargetTransfer(const QString& oldDir, const QString& newDir);

// the wizard: choices, warnings, then the transfer with its progress; returns
// the instance's folder, or an empty string when nothing was done
QString runTransferWizard(QWidget* parent, const TransferSource& source);

}  // namespace Nolvus

#endif  // MODORGANIZER_NOLVUSTRANSFER_INCLUDED
