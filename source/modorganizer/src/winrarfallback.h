#ifndef MODORGANIZER_WINRARFALLBACK_H
#define MODORGANIZER_WINRARFALLBACK_H

// MO2 Revamped: when 7-Zip can't open an archive and the user has WinRAR, MO2
// can extract the archive with the user's own WinRAR (nothing of WinRAR is
// shipped) and install from the extracted folder. FolderArchive presents that
// folder to the installers as if it were the archive.

#include <archive.h>

#include <QString>
#include <atomic>
#include <memory>
#include <string>
#include <vector>

class QWidget;

namespace WinRARFallback
{

// WinRAR.exe of the user's WinRAR installation, or empty
QString findWinRAR();

// whether the file is a format WinRAR extracts (by its extension)
bool canHandle(const QString& archivePath);

// extracts the whole archive into destination (created if missing), showing a
// window the user can cancel from; error is set when it returns false (empty
// when the user cancelled). incomplete is set when WinRAR finished with
// non-fatal errors: some files may be missing
bool extract(QWidget* parent, const QString& winrar, const QString& archivePath,
             const QString& destination, QString& error, bool& incomplete);

// the folders it extracts to, under the system's temporary folder
QString folderPrefix();

// removes extraction folders a crash or a killed MO2 left behind
void removeLeftovers();

}  // namespace WinRARFallback

// a folder on disk behind the Archive interface: open() lists it, extract()
// copies the entries to the output paths the installers chose
class FolderArchive : public Archive
{
public:
  FolderArchive();
  ~FolderArchive() override;

  bool isValid() const override { return true; }
  Error getLastError() const override { return m_LastError; }
  void setLogCallback(LogCallback logCallback) override { m_Log = logCallback; }

  bool open(std::wstring const& folderPath, PasswordCallback passwordCallback) override;
  void close() override;
  const std::vector<FileData*>& getFileList() const override { return m_Files; }

  bool extract(std::wstring const& outputDirectory, ProgressCallback progressCallback,
               FileChangeCallback fileChangeCallback,
               ErrorCallback errorCallback) override;
  void cancel() override { m_Cancel = true; }

private:
  std::wstring m_Root;
  std::vector<FileData*> m_Files;
  Error m_LastError = Error::ERROR_NONE;
  LogCallback m_Log;
  std::atomic<bool> m_Cancel{false};
};

#endif  // MODORGANIZER_WINRARFALLBACK_H
