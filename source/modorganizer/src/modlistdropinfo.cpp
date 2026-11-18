#include "modlistdropinfo.h"

#include "organizercore.h"
#include "downloadlist.h"

ModListDropInfo::ModListDropInfo(const QMimeData* mimeData, OrganizerCore& core)
    : m_rows{}, m_download{-1}, m_localUrls{}, m_url{}
{
  // this only check if the drop is valid, not if the content of the drop
  // matches the target, a drop is valid if either
  // 1. it contains items from another model (drag&drop in modlist or from download
  // list)
  // 2. it contains URLs from MO2 (overwrite or from another mod)
  // 3. it contains a single URL to an external folder
  // 4. it contains a single URL to a valid archive for MO2
  try {
    if (mimeData->hasUrls()) {
      for (auto& url : mimeData->urls()) {
        auto p = relativeUrl(url);
        if (p) {
          m_localUrls.push_back(*p);
        }
      }

      // MO2 Revamped: several archives at once, from Explorer
      if (m_localUrls.empty() && mimeData->urls().size() > 1) {
        const QStringList extensions =
            core.installationManager()->getSupportedExtensions();
        std::vector<QUrl> archives;
        for (const auto& url : mimeData->urls()) {
          const QFileInfo info(url.toLocalFile());
          if (!url.isLocalFile() || !info.isFile() ||
              !extensions.contains(info.suffix(), Qt::CaseInsensitive)) {
            archives.clear();
            break;
          }
          archives.push_back(url);
        }
        m_archives = std::move(archives);
      }

      // external drop
      if (m_localUrls.empty() && mimeData->urls().size() == 1) {
        auto url = mimeData->urls()[0];
        if (url.isLocalFile() && !relativeUrl(url)) {
          QFileInfo info(url.toLocalFile());
          if (info.isDir()) {
            m_url = url;
          } else if (core.installationManager()->getSupportedExtensions().contains(
                         info.suffix(), Qt::CaseInsensitive)) {
            m_url = url;
          }
        }
      }

    } else if (mimeData->hasText()) {
      QByteArray encoded = mimeData->data("application/x-qabstractitemmodeldatalist");
      QDataStream stream(&encoded, QIODevice::ReadOnly);

      while (!stream.atEnd()) {
        int sourceRow, col;
        QMap<int, QVariant> roleDataMap;
        stream >> sourceRow >> col >> roleDataMap;
        if (col == 0) {
          m_rows.push_back(sourceRow);
        }
      }

      if (mimeData->text() != ModListDropInfo::ModText) {
        if (mimeData->text() == ModListDropInfo::DownloadText && m_rows.size() == 1) {
          m_download = m_rows[0];
        }
        // MO2 Revamped: several downloads dragged at once
        if (mimeData->text() == ModListDropInfo::DownloadText && !m_rows.empty()) {
          m_downloads = m_rows;
          m_download  = m_rows[0];
          m_downloadNames =
              QString::fromUtf8(mimeData->data(DownloadList::namesMimeType()))
                  .split(QLatin1Char('\n'), Qt::SkipEmptyParts);
        }
        m_rows = {};
      }
    }
  } catch (std::exception const&) {
    m_rows     = {};
    m_download = -1;
    m_downloads.clear();
    m_downloadNames.clear();
    m_archives.clear();
    m_localUrls.clear();
    m_url = {};
  }
}

std::optional<ModListDropInfo::RelativeUrl>
ModListDropInfo::relativeUrl(const QUrl& url) const
{
  if (!url.isLocalFile()) {
    return {};
  }

  QDir allModsDir(Settings::instance().paths().mods());
  QDir overwriteDir(Settings::instance().paths().overwrite());

  QFileInfo sourceInfo(url.toLocalFile());
  QString sourceFile = sourceInfo.canonicalFilePath();

  QString relativePath;
  QString originName;

  if (sourceFile.startsWith(allModsDir.canonicalPath())) {
    QDir relativeDir(allModsDir.relativeFilePath(sourceFile));
    QStringList splitPath = relativeDir.path().split("/");
    originName            = splitPath[0];
    splitPath.pop_front();
    return {{url, splitPath.join("/"), originName}};
  } else if (sourceFile.startsWith(overwriteDir.canonicalPath())) {
    return {{url, overwriteDir.relativeFilePath(sourceFile),
             ModInfo::getOverwrite()->name()}};
  }

  return {};
}

bool ModListDropInfo::isValid() const
{
  return isLocalFileDrop() || isModDrop() || isDownloadDrop() || m_url.isLocalFile() ||
         !m_archives.empty();
}

bool ModListDropInfo::isLocalFileDrop() const
{
  return !m_localUrls.empty();
}

bool ModListDropInfo::isModDrop() const
{
  return !m_rows.empty();
}

bool ModListDropInfo::isDownloadDrop() const
{
  return m_download != -1;
}

bool ModListDropInfo::isExternalArchiveDrop() const
{
  return !m_archives.empty() ||
         (m_url.isLocalFile() && QFileInfo(m_url.toLocalFile()).isFile());
}

QStringList ModListDropInfo::externalArchives() const
{
  QStringList paths;
  if (!m_archives.empty()) {
    for (const auto& url : m_archives) {
      paths << url.toLocalFile();
    }
  } else if (isExternalArchiveDrop()) {
    paths << m_url.toLocalFile();
  }
  return paths;
}

bool ModListDropInfo::isExternalFolderDrop() const
{
  return m_url.isLocalFile() && QFileInfo(m_url.toLocalFile()).isDir();
}
