#ifndef DOWNLOADURL_H
#define DOWNLOADURL_H

// MO2 Revamped: the text helpers of the download manager (downloadmanager.cpp)
// for download links and their file names, on their own so they can be tested
// without it. Qt Core and uibase's sanitizeFileName() only.

#include <QString>

namespace DownloadUrl
{

// download links carry a short-lived key in their query
// (?md5=...&expires=..., ?key=...); it stays out of the log and the messages.
// Also used on errorString(), which quotes the url
QString withoutQuery(QString text);

// the file name of a Content-Disposition value: filename*=UTF-8''...
// (RFC 6266) first, then filename="..." up to its closing quote, or unquoted;
// empty when it has none
QString fileNameFromDisposition(const QString& disposition);

// the file name a download from url starts with: the last part of its path,
// percent-decoded, or the name a response-content-disposition parameter of
// its query gives (S3-style signed links); "unknown" when there is none
QString fileNameFromUrl(const QString& url);

// the name a download's file starts with (DownloadManager::addDownload), from
// the link's or the server's name: without the query a link left in it
// ("?id=5" of https://h/download/?id=5), and "unknown" when no file name is
// left; fallback, when given, tells whether it is that "unknown", which the
// server's Content-Disposition may still replace
QString downloadBaseName(QString name, bool* fallback = nullptr);

// name with "_" in front when Windows would open a device for it: CON, aux.zip,
// COM1.7z, in any case (before Windows 11 a download saved as aux.zip was
// written to the AUX device, not to a file). Other names as they are
QString withoutDeviceName(const QString& name);

}  // namespace DownloadUrl

#endif  // DOWNLOADURL_H
