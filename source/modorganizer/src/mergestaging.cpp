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

bool writePlan(const QString& stagedDir, const QString& planFile)
{
  const QDir staged(stagedDir);
  QFile file(planFile);
  if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
    return false;
  }
  QDirIterator it(stagedDir, QDir::Files | QDir::Hidden | QDir::System,
                  QDirIterator::Subdirectories);
  while (it.hasNext()) {
    it.next();
    const QByteArray line = staged.relativeFilePath(it.filePath()).toUtf8() + '\n';
    if (file.write(line) != line.size()) {
      return false;
    }
  }
  // on the disk before the first file moves
  return file.flush() && (file.close(), file.error() == QFileDevice::NoError);
}

bool undoInterrupted(const QString& planFile, const QString& stagedDir,
                     const QString& targetDir, const QString& backupDir)
{
  QFile file(planFile);
  if (!file.open(QIODevice::ReadOnly)) {
    return false;
  }
  const QString target = QDir::cleanPath(targetDir);
  bool clean           = true;
  while (!file.atEnd()) {
    QByteArray line = file.readLine();
    while (line.endsWith('\n') || line.endsWith('\r')) {
      line.chop(1);
    }
    const QString rel = QDir::cleanPath(QString::fromUtf8(line));
    // nothing outside the mod
    if (rel.isEmpty() || rel == "." || rel.startsWith("..") || QDir::isAbsolutePath(rel)) {
      continue;
    }
    const QString inMod = target + "/" + rel;
    const QString kept  = backupDir + "/" + rel;
    const bool moved    = !QFileInfo::exists(stagedDir + "/" + rel);
    const bool hadOld   = QFileInfo::exists(kept);
    if (moved && QFileInfo::exists(inMod) && !QFile::remove(inMod)) {
      clean = false;
      continue;
    }
    if (hadOld) {
      // not there when the stop came between setting it aside and the new one
      if (QFileInfo::exists(inMod) || !QFile::rename(kept, inMod)) {
        clean = false;
      }
    } else if (moved) {
      // the folders made for it, as far as they are empty
      QString dir = QFileInfo(inMod).absolutePath();
      while (dir.length() > target.length() && QDir().rmdir(dir)) {
        dir = QFileInfo(dir).absolutePath();
      }
    }
  }
  return clean;
}

}  // namespace MergeStaging
