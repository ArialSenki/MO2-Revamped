#ifndef MODORGANIZER_MERGESTAGING_H
#define MODORGANIZER_MERGESTAGING_H

#include <QString>

// MO2 Revamped: merging an archive into an installed mod. The files are
// extracted to a folder of their own first and only moved into the mod once all
// of them are there, so an install cancelled (or failed) while extracting
// leaves the mod untouched. The move keeps every file it replaces until the
// last one is in, and puts them back if one can't be moved.
namespace MergeStaging
{

struct Result
{
  bool ok = true;
  // the file in the mod that stopped it (empty when ok)
  QString failed;
  // after a failure: whether the mod is as it was before. When it isn't, the
  // files it had are still in the backup folder
  bool undone = true;
  int moved    = 0;
  int replaced = 0;
};

// Moves every file under staged to the same place under target. A file that is
// already there goes to backup first (same relative path). On a failure,
// everything moved is taken back to staged and the replaced files return.
// staged, target and backup should be on one volume (the moves are renames).
Result apply(const QString& staged, const QString& target, const QString& backup);

}  // namespace MergeStaging

#endif
