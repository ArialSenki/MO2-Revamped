#include "mergestaging.h"

#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QStringList>

#include <vector>

namespace MergeStaging
{

namespace
{
struct Step
{
  QString rel;
  bool hadOld;
};

// the folders of path that don't exist yet are made, and noted outermost first
bool makeFolders(const QString& path, QStringList& made)
{
  QStringList missing;
  QString dir = path;
  while (!dir.isEmpty() && !QFileInfo::exists(dir)) {
    missing.prepend(dir);
    const QString parent = QFileInfo(dir).absolutePath();
    if (parent == dir) {
      break;
    }
    dir = parent;
  }
  if (!dir.isEmpty() && QFileInfo::exists(dir) && !QFileInfo(dir).isDir()) {
    return false;
  }
  for (const QString& folder : missing) {
    if (!QDir().mkdir(folder)) {
      return false;
    }
    made.append(folder);
  }
  return true;
}
}  // namespace

Result apply(const QString& stagedDir, const QString& targetDir, const QString& backupDir)
{
  Result result;
  const QDir staged(stagedDir);
  std::vector<Step> steps;
  QStringList madeFolders;
  QStringList backupFolders;

  // the list first: the folder changes as the files leave it
  QStringList files;
  QStringList emptyFolders;
  {
    QDirIterator it(stagedDir, QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot |
                                   QDir::Hidden | QDir::System,
                    QDirIterator::Subdirectories);
    while (it.hasNext()) {
      it.next();
      const QFileInfo info = it.fileInfo();
      const QString rel    = staged.relativeFilePath(info.absoluteFilePath());
      if (info.isDir() && !info.isSymLink()) {
        if (QDir(info.absoluteFilePath()).isEmpty(QDir::AllEntries | QDir::NoDotAndDotDot |
                                                  QDir::Hidden | QDir::System)) {
          emptyFolders.append(rel);
        }
      } else {
        files.append(rel);
      }
    }
  }

  const auto undo = [&]() {
    bool clean = true;
    for (auto step = steps.rbegin(); step != steps.rend(); ++step) {
      const QString inMod = targetDir + "/" + step->rel;
      // back to where it was extracted, so nothing of the new files is lost either
      if (!QFile::rename(inMod, stagedDir + "/" + step->rel)) {
        clean = false;
        continue;
      }
      if (step->hadOld && !QFile::rename(backupDir + "/" + step->rel, inMod)) {
        clean = false;
      }
    }
    for (auto folder = madeFolders.rbegin(); folder != madeFolders.rend(); ++folder) {
      QDir().rmdir(*folder);
    }
    return clean;
  };

  const auto fail = [&](const QString& path) {
    result.ok     = false;
    result.failed = path;
    result.undone = undo();
    return result;
  };

  for (const QString& rel : files) {
    const QString from  = stagedDir + "/" + rel;
    const QString inMod = targetDir + "/" + rel;
    if (!makeFolders(QFileInfo(inMod).absolutePath(), madeFolders)) {
      return fail(inMod);
    }
    const QFileInfo existing(inMod);
    bool hadOld = false;
    if (existing.exists() || existing.isSymLink()) {
      if (existing.isDir() && !existing.isSymLink()) {
        // a folder where the archive has a file
        return fail(inMod);
      }
      const QString kept = backupDir + "/" + rel;
      if (!makeFolders(QFileInfo(kept).absolutePath(), backupFolders) ||
          !QFile::rename(inMod, kept)) {
        return fail(inMod);
      }
      hadOld = true;
    }
    if (!QFile::rename(from, inMod)) {
      if (hadOld) {
        QFile::rename(backupDir + "/" + rel, inMod);
      }
      return fail(inMod);
    }
    steps.push_back({rel, hadOld});
    ++result.moved;
    if (hadOld) {
      ++result.replaced;
    }
  }

  // folders the archive has with nothing in them
  for (const QString& rel : emptyFolders) {
    if (!makeFolders(targetDir + "/" + rel, madeFolders)) {
      return fail(targetDir + "/" + rel);
    }
  }
  return result;
}

}  // namespace MergeStaging
