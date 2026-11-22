#include "downloadurl.h"

#include <filesystemutilities.h>

#include <QByteArray>
#include <QFileInfo>
#include <QRegularExpression>
#include <QUrl>
#include <QUrlQuery>

namespace DownloadUrl
{

QString withoutQuery(QString text)
{
  static const QRegularExpression query(R"(\?[^\s"']*)");
  return text.replace(query, QString());
}

QString fileNameFromDisposition(const QString& disposition)
{
  static const QRegularExpression extended(R"re(filename\*\s*=\s*([^']*)'[^']*'([^;\s]+))re",
                                           QRegularExpression::CaseInsensitiveOption);
  static const QRegularExpression quoted(R"re(filename\s*=\s*"([^"]*)")re",
                                         QRegularExpression::CaseInsensitiveOption);
  static const QRegularExpression bare(R"re(filename\s*=\s*([^;"\s][^;]*))re",
                                       QRegularExpression::CaseInsensitiveOption);

  auto match = extended.match(disposition);
  if (match.hasMatch()) {
    const QByteArray raw = QByteArray::fromPercentEncoding(match.captured(2).toLatin1());
    const QString name   = match.captured(1).compare("UTF-8", Qt::CaseInsensitive) == 0
                               ? QString::fromUtf8(raw)
                               : QString::fromLatin1(raw);
    if (!name.isEmpty()) {
      return name;
    }
  }

  match = quoted.match(disposition);
  if (match.hasMatch() && !match.captured(1).isEmpty()) {
    return match.captured(1);
  }

  match = bare.match(disposition);
  if (match.hasMatch()) {
    return match.captured(1).trimmed();
  }

  return QString();
}

QString fileNameFromUrl(const QString& url)
{
  QString fileName = QFileInfo(url).fileName();
  if (fileName.isEmpty()) {
    fileName = "unknown";
  } else {
    fileName = QUrl::fromPercentEncoding(fileName.toUtf8());
  }

  // Temporary URLs for S3-compatible storage are signed for a single method, removing
  // the ability to make HEAD requests to such URLs. We can use the
  // response-content-disposition GET parameter, setting the Content-Disposition header,
  // to predetermine intended file name without a subrequest.
  // MO2 Revamped: the parameter is read from the query itself, the same way as
  // the header (see fileNameFromDisposition())
  const QUrlQuery urlQuery{QUrl(url)};
  if (urlQuery.hasQueryItem("response-content-disposition")) {
    const QString dispoName = fileNameFromDisposition(
        urlQuery.queryItemValue("response-content-disposition", QUrl::FullyDecoded));
    if (!dispoName.isEmpty()) {
      fileName = MOBase::sanitizeFileName(dispoName);
      if (fileName.isEmpty()) {
        fileName = "unknown";
      }
    }
  }
  return fileName;
}

}  // namespace DownloadUrl
