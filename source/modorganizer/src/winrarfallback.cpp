#include "winrarfallback.h"

#include <log.h>

#include <QCoreApplication>
#include <QDir>
#include <QDateTime>
#include <QDirIterator>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QProgressDialog>
#include <QSettings>
#include <QSet>

using namespace MOBase;

namespace
{

QString tr(const char* text)
{
  return QCoreApplication::translate("WinRARFallback", text);
}

class FolderFileData : public FileData
{
public:
  FolderFileData(std::wstring path, uint64_t size, bool directory)
      : m_Path(std::move(path)), m_Size(size), m_Directory(directory)
  {}

  std::wstring getArchiveFilePath() const override { return m_Path; }
  uint64_t getSize() const override { return m_Size; }
  void addOutputFilePath(std::wstring const& filepath) override
  {
    m_Outputs.push_back(filepath);
  }
  const std::vector<std::wstring>& getOutputFilePaths() const override
  {
    return m_Outputs;
  }
  void clearOutputFilePaths() override { m_Outputs.clear(); }
  uint64_t getCRC() const override { return 0; }
  bool isDirectory() const override { return m_Directory; }

private:
  std::wstring m_Path;
  uint64_t m_Size;
  bool m_Directory;
  std::vector<std::wstring> m_Outputs;
};

// an output path the installers chose must stay inside the output folder
bool insideFolder(const QString& folder, const QString& path)
{
  const QString root = QDir::cleanPath(QDir(folder).absolutePath());
  const QString full = QDir::cleanPath(QDir(folder).absoluteFilePath(path));
  return full.compare(root, Qt::CaseInsensitive) == 0 ||
         full.startsWith(root + "/", Qt::CaseInsensitive);
}

}  // namespace

namespace WinRARFallback
{

QString findWinRAR()
{
  // WinRAR's own key, then the App Paths entry its setup registers
  const QStringList keys = {
      "HKEY_LOCAL_MACHINE\\SOFTWARE\\WinRAR",
      "HKEY_CURRENT_USER\\SOFTWARE\\WinRAR",
      "HKEY_LOCAL_MACHINE\\SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\App "
      "Paths\\WinRAR.exe"};
  for (const QString& key : keys) {
    for (QSettings::Format format :
         {QSettings::Registry64Format, QSettings::Registry32Format}) {
      QSettings settings(key, format);
      for (const QString& value : {QString("exe64"), QString("exe32"), QString(".")}) {
        const QString path = settings.value(value).toString();
        if (!path.isEmpty() &&
            QFileInfo(path).fileName().compare("WinRAR.exe", Qt::CaseInsensitive) == 0 &&
            QFileInfo(path).isFile()) {
          return QDir::toNativeSeparators(QFileInfo(path).absoluteFilePath());
        }
      }
    }
  }
  return {};
}

bool canHandle(const QString& archivePath)
{
  // what MO2 lets the user install as an archive (and WinRAR extracts)
  static const QSet<QString> extensions = {"rar", "zip", "7z", "001"};
  return extensions.contains(QFileInfo(archivePath).suffix().toLower());
}

QString folderPrefix()
{
  return QStringLiteral("MO2 WinRAR ");
}

void removeLeftovers()
{
  QDir temp = QDir::temp();
  const auto old = temp.entryInfoList({folderPrefix() + "*"}, QDir::Dirs | QDir::NoDotAndDotDot);
  for (const QFileInfo& folder : old) {
    if (folder.lastModified().secsTo(QDateTime::currentDateTime()) > 24 * 60 * 60) {
      QDir(folder.absoluteFilePath()).removeRecursively();
    }
  }
}

bool extract(QWidget* parent, const QString& winrar, const QString& archivePath,
             const QString& destination, QString& error, bool& incomplete)
{
  error.clear();
  incomplete = false;
  if (!QDir().mkpath(destination)) {
    error = tr("The folder to extract to could not be created.");
    return false;
  }

  // x: with the folders; -ibck and -inul: in the background, and no message
  // windows (one would wait, hidden, while MO2 shows its progress); -y and
  // -o+: answer yes and overwrite; -p-: never ask for a password (an encrypted
  // archive fails instead of opening a WinRAR prompt behind MO2)
  QProcess process;
  process.setProgram(winrar);
  process.setArguments({"x", "-ibck", "-inul", "-y", "-o+", "-p-", "--",
                        QDir::toNativeSeparators(archivePath),
                        QDir::toNativeSeparators(destination) + "\\"});
  process.start();
  if (!process.waitForStarted(30000)) {
    error = tr("WinRAR could not be started.");
    return false;
  }

  QProgressDialog progress(tr("Extracting %1 with WinRAR…")
                               .arg(QFileInfo(archivePath).fileName()),
                           tr("Cancel"), 0, 0, parent);
  progress.setWindowTitle(tr("WinRAR"));
  progress.setWindowModality(Qt::WindowModal);
  progress.setMinimumDuration(500);

  while (!process.waitForFinished(100)) {
    if (process.state() == QProcess::NotRunning) {
      break;
    }
    QCoreApplication::processEvents();
    if (progress.wasCanceled()) {
      process.kill();
      process.waitForFinished(10000);
      return false;
    }
  }
  progress.close();

  // 0: done; 1: done, with non-fatal errors (some files may be missing)
  if (process.exitStatus() != QProcess::NormalExit ||
      (process.exitCode() != 0 && process.exitCode() != 1)) {
    error = tr("WinRAR stopped with code %1.").arg(process.exitCode());
    return false;
  }
  if (process.exitCode() == 1) {
    log::warn("WinRAR extracted {} with non-fatal errors", archivePath);
    incomplete = true;
  }
  if (QDir(destination).isEmpty(QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden |
                                QDir::System)) {
    error = tr("WinRAR extracted nothing.");
    return false;
  }
  return true;
}

}  // namespace WinRARFallback

FolderArchive::FolderArchive() = default;

FolderArchive::~FolderArchive()
{
  close();
}

bool FolderArchive::open(std::wstring const& folderPath, PasswordCallback)
{
  close();
  const QString root = QString::fromStdWString(folderPath);
  if (!QFileInfo(root).isDir()) {
    m_LastError = Error::ERROR_ARCHIVE_NOT_FOUND;
    return false;
  }
  m_Root   = folderPath;
  m_Cancel = false;
  QDir rootDir(root);
  const QString canonicalRoot = QFileInfo(root).canonicalFilePath();
  QDirIterator it(root,
                  QDir::Files | QDir::Dirs | QDir::Hidden | QDir::System |
                      QDir::NoDotAndDotDot,
                  QDirIterator::Subdirectories);
  while (it.hasNext()) {
    it.next();
    const QFileInfo info = it.fileInfo();
    // links could point outside the extracted folder, and the iterator may
    // still walk into a junction: only what really is below the folder
    const QString canonical = info.canonicalFilePath();
    if (info.isSymLink() || info.isJunction() || canonical.isEmpty() ||
        !canonical.startsWith(canonicalRoot + "/", Qt::CaseInsensitive)) {
      continue;
    }
    const QString relative =
        QDir::toNativeSeparators(rootDir.relativeFilePath(info.absoluteFilePath()));
    m_Files.push_back(new FolderFileData(relative.toStdWString(),
                                         info.isDir() ? 0 : info.size(), info.isDir()));
  }
  m_LastError = Error::ERROR_NONE;
  return true;
}

void FolderArchive::close()
{
  for (FileData* file : m_Files) {
    delete file;
  }
  m_Files.clear();
  m_Root.clear();
}

bool FolderArchive::extract(std::wstring const& outputDirectory,
                            ProgressCallback progressCallback,
                            FileChangeCallback fileChangeCallback,
                            ErrorCallback errorCallback)
{
  const QString root   = QString::fromStdWString(m_Root);
  const QString output = QString::fromStdWString(outputDirectory);

  uint64_t total = 0;
  for (FileData* file : m_Files) {
    if (!file->isDirectory()) {
      total += file->getSize() * file->getOutputFilePaths().size();
    }
  }
  uint64_t done = 0;

  auto fail = [&](const QString& message) {
    m_LastError = Error::ERROR_LIBRARY_ERROR;
    log::error("{}", message);
    if (errorCallback) {
      errorCallback(message.toStdWString());
    }
    return false;
  };

  for (FileData* file : m_Files) {
    for (const std::wstring& outputPath : file->getOutputFilePaths()) {
      if (m_Cancel) {
        m_LastError = Error::ERROR_EXTRACT_CANCELLED;
        return false;
      }
      const QString relative = QString::fromStdWString(outputPath);
      if (!insideFolder(output, relative)) {
        return fail(QString("refusing to write outside %1: %2").arg(output, relative));
      }
      const QString target = QDir(output).absoluteFilePath(relative);
      if (fileChangeCallback) {
        fileChangeCallback(FileChangeType::EXTRACTION_START, outputPath);
      }
      if (file->isDirectory()) {
        if (!QDir().mkpath(target)) {
          return fail(QString("could not create %1").arg(target));
        }
      } else {
        const QString source =
            QDir(root).absoluteFilePath(QString::fromStdWString(file->getArchiveFilePath()));
        QDir().mkpath(QFileInfo(target).absolutePath());
        if (QFileInfo::exists(target)) {
          QFile::setPermissions(target, QFile::ReadOwner | QFile::WriteOwner);
          QFile::remove(target);
        }
        if (!QFile::copy(source, target)) {
          return fail(QString("could not copy %1 to %2").arg(source, target));
        }
        done += file->getSize();
        // MO2 divides by the total: nothing to report for empty files only
        if (progressCallback && total > 0) {
          progressCallback(ProgressType::EXTRACTION, done, total);
        }
      }
      if (fileChangeCallback) {
        fileChangeCallback(FileChangeType::EXTRACTION_END, outputPath);
      }
    }
    // as 7-Zip does: an entry is extracted once; the installers extract
    // previews first (FOMOD's images and XML) and map the final files later
    file->clearOutputFilePaths();
  }
  m_LastError = Error::ERROR_NONE;
  return true;
}
