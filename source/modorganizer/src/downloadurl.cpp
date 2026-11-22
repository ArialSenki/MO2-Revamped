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

namespace
{

// one parameter of a Content-Disposition value
struct DispositionParameter
{
  QString name;
  QString value;
};

// MO2 Revamped: the parameters of a Content-Disposition value, split at the
// ";" outside quoted-strings, and the quoted-strings with their backslash
// escapes taken (RFC 6266 / 2616: \" is a quote, \\ a backslash). The
// regexes before cut filename="a\"b.7z" at the escaped quote and took the
// filename= inside xfilename= or inside another parameter's quoted value.
// An unterminated quoted-string gives no value
QList<DispositionParameter> dispositionParameters(const QString& text)
{
  QList<DispositionParameter> params;
  const qsizetype n = text.size();
  qsizetype i       = 0;
  while (i < n) {
    // the name, up to "=" or the end of the parameter
    const qsizetype nameStart = i;
    while (i < n && text[i] != u'=' && text[i] != u';') {
      ++i;
    }
    const QString name = text.mid(nameStart, i - nameStart).trimmed();
    if (i >= n || text[i] == u';') {
      // a parameter without a value (the disposition type)
      ++i;
      continue;
    }
    ++i;  // "="
    while (i < n && text[i].isSpace()) {
      ++i;
    }
    QString value;
    bool ok = true;
    if (i < n && text[i] == u'"') {
      ++i;
      bool closed = false;
      while (i < n) {
        const QChar c = text[i++];
        if (c == u'"') {
          closed = true;
          break;
        }
        if (c == u'\\' && i < n) {
          value += text[i++];
        } else {
          value += c;
        }
      }
      ok = closed;
      // whatever follows the closing quote up to the next ";" isn't part of it
      while (i < n && text[i] != u';') {
        ++i;
      }
    } else {
      const qsizetype valueStart = i;
      while (i < n && text[i] != u';') {
        ++i;
      }
      value = text.mid(valueStart, i - valueStart).trimmed();
    }
    ++i;  // ";"
    if (ok) {
      params.append({name, value});
    }
  }
  return params;
}

}  // namespace

QString fileNameFromDisposition(const QString& disposition)
{
  const auto params = dispositionParameters(disposition);

  // filename*=UTF-8''... first (RFC 6266), wherever it is
  static const QRegularExpression extended(R"re(^([^']*)'[^']*'(\S+)$)re");
  for (const auto& p : params) {
    if (p.name.compare(QStringLiteral("filename*"), Qt::CaseInsensitive) != 0) {
      continue;
    }
    const auto match = extended.match(p.value);
    if (!match.hasMatch()) {
      continue;
    }
    const QByteArray raw = QByteArray::fromPercentEncoding(match.captured(2).toLatin1());
    const QString name   = match.captured(1).compare("UTF-8", Qt::CaseInsensitive) == 0
                               ? QString::fromUtf8(raw)
                               : QString::fromLatin1(raw);
    if (!name.isEmpty()) {
      return name;
    }
  }

  for (const auto& p : params) {
    if (p.name.compare(QStringLiteral("filename"), Qt::CaseInsensitive) == 0 &&
        !p.value.isEmpty()) {
      return p.value;
    }
  }

  return QString();
}

QString fileNameFromUrl(const QString& url)
{
  QString fileName = QFileInfo(url).fileName();
  // MO2 Revamped: the last part of the path, not of the whole link: a "/" in
  // the query or the fragment (?token=ab/cd, #part/2) named the file after it,
  // and a fragment stayed in the name. A path without a name keeps the old
  // result ("?id=5"): downloadBaseName() makes it "unknown", which the
  // Content-Disposition of the answer can still replace
  if (const QUrl parsed(url); parsed.isValid() && (parsed.hasQuery() || parsed.hasFragment())) {
    const QString pathName = QFileInfo(parsed.path(QUrl::FullyEncoded)).fileName();
    if (!pathName.isEmpty()) {
      fileName = pathName;
    }
  }
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

QString downloadBaseName(QString name, bool* fallback)
{
  // the name could be a URL at this point so strip out the URL query
  const qsizetype queryIndex = name.indexOf(u'?');
  if (queryIndex >= 0) {
    name.truncate(queryIndex);
  }
  // MO2 Revamped: nothing left (https://h/download/?id=5) was an empty name,
  // and the file ".unfinished", shared by every such download
  const bool none =
      MOBase::sanitizeFileName(name).isEmpty() || name == QStringLiteral("unknown");
  if (none) {
    name = QStringLiteral("unknown");
  }
  if (fallback != nullptr) {
    *fallback = none;
  }
  return name;
}

}  // namespace DownloadUrl
