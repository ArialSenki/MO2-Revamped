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

}  // namespace DownloadUrl

#endif  // DOWNLOADURL_H
