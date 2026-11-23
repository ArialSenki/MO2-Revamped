#include "nexusoauthcore.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRandomGenerator>
#include <QRegularExpression>
#include <QTimeZone>
#include <QUrlQuery>

namespace NexusOAuthCore
{

namespace
{

QByteArray randomBytes(int count)
{
  QByteArray bytes(count, Qt::Uninitialized);
  // the operating system's generator (BCryptGenRandom on Windows), not a
  // seeded one: the verifier and the state must not be guessable
  QRandomGenerator* generator = QRandomGenerator::system();
  for (int i = 0; i < count; ++i) {
    bytes[i] = static_cast<char>(generator->bounded(256));
  }
  return bytes;
}

QByteArray formBody(const QList<std::pair<const char*, QString>>& fields)
{
  QByteArray body;
  for (const auto& [name, value] : fields) {
    if (!body.isEmpty()) {
      body += '&';
    }
    body += name;
    body += '=';
    body += QUrl::toPercentEncoding(value);
  }
  return body;
}

// compares without stopping at the first difference
bool sameSecret(const QString& a, const QString& b)
{
  const QByteArray x = a.toUtf8();
  const QByteArray y = b.toUtf8();
  if (x.size() != y.size()) {
    return false;
  }
  unsigned char diff = 0;
  for (qsizetype i = 0; i < x.size(); ++i) {
    diff |= static_cast<unsigned char>(x[i] ^ y[i]);
  }
  return diff == 0;
}

qint64 toSeconds(const QJsonValue& v)
{
  if (v.isDouble()) {
    return static_cast<qint64>(v.toDouble());
  }
  if (v.isString()) {
    return v.toString().toLongLong();
  }
  return 0;
}

}  // namespace

QByteArray base64Url(const QByteArray& data)
{
  return data.toBase64(QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals);
}

QString makeVerifier()
{
  return QString::fromLatin1(base64Url(randomBytes(32)));
}

QString challengeS256(const QString& verifier)
{
  return QString::fromLatin1(base64Url(
      QCryptographicHash::hash(verifier.toLatin1(), QCryptographicHash::Sha256)));
}

QString makeState()
{
  return QString::fromLatin1(base64Url(randomBytes(16)));
}

QString redirectUri(quint16 port)
{
  QString uri = QStringLiteral("http://127.0.0.1:%1").arg(port);
  const QString path = QString::fromLatin1(RedirectPath);
  if (path != QLatin1String("/")) {
    uri += path;
  }
  return uri;
}

QUrl authorizeUrl(const QString& clientId, const QString& redirectUri,
                  const QString& challenge, const QString& state, const QString& scope)
{
  // every value percent-encoded but the unreserved characters: the redirect's
  // ":" and "/", and the scope's spaces (%20)
  const QByteArray query = formBody({{"response_type", QStringLiteral("code")},
                                     {"client_id", clientId},
                                     {"redirect_uri", redirectUri},
                                     {"scope", scope},
                                     {"code_challenge", challenge},
                                     {"code_challenge_method", QStringLiteral("S256")},
                                     {"state", state}});

  return QUrl::fromEncoded(QByteArray(AuthorizeUrl) + '?' + query, QUrl::StrictMode);
}

QByteArray codeExchangeBody(const QString& clientId, const QString& redirectUri,
                            const QString& code, const QString& verifier)
{
  return formBody({{"grant_type", QStringLiteral("authorization_code")},
                   {"client_id", clientId},
                   {"redirect_uri", redirectUri},
                   {"code", code},
                   {"code_verifier", verifier}});
}

QByteArray refreshBody(const QString& clientId, const QString& refreshToken)
{
  return formBody({{"grant_type", QStringLiteral("refresh_token")},
                   {"client_id", clientId},
                   {"refresh_token", refreshToken}});
}

QByteArray revokeBody(const QString& clientId, const QString& token, const QString& hint)
{
  return formBody({{"client_id", clientId}, {"token", token}, {"token_type_hint", hint}});
}

QString apiBaseOverride(const QString& value, bool devBuild)
{
  const QString v = value.trimmed();
  if (!devBuild || v.isEmpty()) {
    return {};
  }
  const QUrl url(v, QUrl::StrictMode);
  if (!url.isValid() || url.scheme() != QLatin1String("http") ||
      !url.userInfo().isEmpty() ||
      (url.host() != QLatin1String("127.0.0.1") &&
       url.host() != QLatin1String("localhost"))) {
    return {};
  }
  return v.endsWith(QLatin1Char('/')) ? v.chopped(1) : v;
}

QString redirectHost(quint16 port)
{
  return QStringLiteral("127.0.0.1:%1").arg(port);
}

Redirect parseRedirect(const QByteArray& head, const QString& expectedState,
                       const QString& expectedPath, const QString& expectedHost)
{
  // a browser's request head is a few hundred bytes; anything this long
  // without its end isn't one
  constexpr qsizetype MaxHead = 16 * 1024;

  Redirect r;
  const qsizetype end = head.indexOf("\r\n\r\n");
  if (end < 0) {
    r.kind = head.size() > MaxHead ? Redirect::Malformed : Redirect::Incomplete;
    return r;
  }
  if (end > MaxHead) {
    r.kind = Redirect::Malformed;
    return r;
  }

  const qsizetype lineEnd = head.indexOf("\r\n");
  const QList<QByteArray> parts = head.left(lineEnd).split(' ');
  if (parts.size() != 3 || parts[0] != "GET" || !parts[2].startsWith("HTTP/1.") ||
      !parts[1].startsWith('/')) {
    r.kind = Redirect::Malformed;
    return r;
  }

  // the browser came to the address MO2 Revamped gave Nexus: exactly one Host
  // header, naming it (RFC 8252 section 8.3; a page on another name resolved
  // to 127.0.0.1 is refused)
  if (!expectedHost.isEmpty()) {
    int hosts = 0;
    bool same = false;
    for (const QByteArray& line : head.left(end).split('\n').mid(1)) {
      const qsizetype colon = line.indexOf(':');
      if (colon <= 0 || line.left(colon).trimmed().toLower() != "host") {
        continue;
      }
      ++hosts;
      same = QString::fromLatin1(line.mid(colon + 1).trimmed())
                 .compare(expectedHost, Qt::CaseInsensitive) == 0;
    }
    if (hosts != 1 || !same) {
      r.kind = Redirect::Malformed;
      return r;
    }
  }

  const QUrl target(QStringLiteral("http://127.0.0.1") + QString::fromLatin1(parts[1]),
                    QUrl::StrictMode);
  if (!target.isValid()) {
    r.kind = Redirect::Malformed;
    return r;
  }

  if (target.path(QUrl::FullyEncoded) != expectedPath) {
    r.kind = Redirect::WrongPath;
    return r;
  }

  const QUrlQuery query(target);
  const QStringList states = query.allQueryItemValues(QStringLiteral("state"),
                                                      QUrl::FullyDecoded);
  if (states.size() != 1 || expectedState.isEmpty() ||
      !sameSecret(states.front(), expectedState)) {
    r.kind = Redirect::StateMismatch;
    return r;
  }

  if (query.hasQueryItem(QStringLiteral("error"))) {
    r.kind  = Redirect::Error;
    r.error = query.queryItemValue(QStringLiteral("error"), QUrl::FullyDecoded);
    r.errorDescription =
        query.queryItemValue(QStringLiteral("error_description"), QUrl::FullyDecoded)
            .replace(QLatin1Char('+'), QLatin1Char(' '));
    return r;
  }

  const QStringList codes =
      query.allQueryItemValues(QStringLiteral("code"), QUrl::FullyDecoded);
  if (codes.size() != 1 || codes.front().isEmpty()) {
    r.kind  = Redirect::Error;
    r.error = QStringLiteral("invalid_request");
    return r;
  }

  r.kind = Redirect::Code;
  r.code = codes.front();
  return r;
}

QByteArray httpResponse(int status, const QString& title, const QString& message)
{
  const QString html =
      QStringLiteral(
          "<!doctype html><html><head><meta charset=\"utf-8\">"
          "<meta name=\"viewport\" content=\"width=device-width\">"
          "<title>%1</title><style>body{font-family:'Segoe UI',sans-serif;"
          "background:#202020;color:#f0f0f0;display:flex;align-items:center;"
          "justify-content:center;height:100vh;margin:0}main{max-width:32em;"
          "padding:2em;text-align:center}h1{font-size:1.4em;font-weight:600}"
          "</style></head><body><main><h1>%1</h1><p>%2</p></main></body></html>")
          .arg(title.toHtmlEscaped(), message.toHtmlEscaped());
  const QByteArray body = html.toUtf8();

  QByteArray reason = "OK";
  if (status == 400) {
    reason = "Bad Request";
  } else if (status == 404) {
    reason = "Not Found";
  } else if (status == 405) {
    reason = "Method Not Allowed";
  }

  QByteArray out = "HTTP/1.1 " + QByteArray::number(status) + ' ' + reason + "\r\n";
  out += "Content-Type: text/html; charset=utf-8\r\n";
  out += "Content-Length: " + QByteArray::number(body.size()) + "\r\n";
  out += "Cache-Control: no-store\r\n";
  out += "Referrer-Policy: no-referrer\r\n";
  out += "X-Content-Type-Options: nosniff\r\n";
  out += "Content-Security-Policy: default-src 'none'; style-src 'unsafe-inline'\r\n";
  out += "Connection: close\r\n\r\n";
  out += body;
  return out;
}

Tokens parseTokens(const QByteArray& json)
{
  Tokens t;
  const QJsonDocument doc = QJsonDocument::fromJson(json);
  if (!doc.isObject()) {
    t.error = QStringLiteral("invalid_response");
    return t;
  }

  const QJsonObject o = doc.object();
  t.error             = o.value(QStringLiteral("error")).toString();
  t.errorDescription  = o.value(QStringLiteral("error_description")).toString();
  t.accessToken       = o.value(QStringLiteral("access_token")).toString();
  t.refreshToken      = o.value(QStringLiteral("refresh_token")).toString();
  t.expiresIn         = toSeconds(o.value(QStringLiteral("expires_in")));

  if (t.error.isEmpty() && t.accessToken.isEmpty()) {
    t.error = QStringLiteral("invalid_response");
  }
  return t;
}

QDateTime jwtExpiry(const QString& token)
{
  const QStringList parts = token.split(QLatin1Char('.'));
  if (parts.size() != 3) {
    return {};
  }

  const auto decoded = QByteArray::fromBase64Encoding(
      parts[1].toLatin1(),
      QByteArray::Base64UrlEncoding | QByteArray::AbortOnBase64DecodingErrors);
  if (!decoded) {
    return {};
  }

  const QJsonDocument doc = QJsonDocument::fromJson(*decoded);
  if (!doc.isObject()) {
    return {};
  }

  const qint64 exp = toSeconds(doc.object().value(QStringLiteral("exp")));
  if (exp <= 0) {
    return {};
  }
  return QDateTime::fromSecsSinceEpoch(exp, QTimeZone::UTC);
}

QDateTime expiryOf(const Tokens& t, const QDateTime& now)
{
  if (t.expiresIn > 0) {
    return now.addSecs(t.expiresIn);
  }
  const QDateTime exp = jwtExpiry(t.accessToken);
  if (exp.isValid()) {
    return exp;
  }
  return now.addSecs(FallbackLifetimeSeconds);
}

std::optional<UserInfo> parseUserInfo(const QByteArray& json)
{
  const QJsonDocument doc = QJsonDocument::fromJson(json);
  if (!doc.isObject()) {
    return {};
  }

  const QJsonObject o = doc.object();
  const QJsonValue sub = o.value(QStringLiteral("sub"));

  UserInfo u;
  if (sub.isString()) {
    u.id = sub.toString();
  } else if (sub.isDouble()) {
    u.id = QString::number(static_cast<qint64>(sub.toDouble()));
  }
  if (u.id.isEmpty()) {
    return {};
  }

  u.name = o.value(QStringLiteral("name")).toString();

  QStringList roles;
  for (const QJsonValue& role : o.value(QStringLiteral("membership_roles")).toArray()) {
    roles.append(role.toString());
  }
  u.premium = isPremium(roles);
  return u;
}

bool isPremium(const QStringList& membershipRoles)
{
  return membershipRoles.contains(QStringLiteral("premium"), Qt::CaseInsensitive) ||
         membershipRoles.contains(QStringLiteral("lifetimepremium"), Qt::CaseInsensitive);
}

QString packAccess(const QString& token, const QDateTime& expiresUtc)
{
  QJsonObject o;
  o.insert(QStringLiteral("token"), token);
  o.insert(QStringLiteral("expires"), expiresUtc.toSecsSinceEpoch());
  return QString::fromUtf8(QJsonDocument(o).toJson(QJsonDocument::Compact));
}

bool unpackAccess(const QString& packed, QString& token, QDateTime& expiresUtc)
{
  const QJsonDocument doc = QJsonDocument::fromJson(packed.toUtf8());
  if (!doc.isObject()) {
    return false;
  }
  const QJsonObject o = doc.object();
  const QString t     = o.value(QStringLiteral("token")).toString();
  const qint64 e      = toSeconds(o.value(QStringLiteral("expires")));
  if (t.isEmpty() || e <= 0) {
    return false;
  }
  token      = t;
  expiresUtc = QDateTime::fromSecsSinceEpoch(e, QTimeZone::UTC);
  return true;
}

QStringList splitForCredential(const QString& value, int size)
{
  QStringList pieces;
  if (size <= 0) {
    return pieces;
  }
  for (qsizetype i = 0; i < value.size(); i += size) {
    pieces.append(value.mid(i, size));
  }
  return pieces;
}

QString redactSecrets(QString text)
{
  static const QString removed = QStringLiteral("<removed>");
  static const QRegularExpression named(
      QStringLiteral("(?i)(\\b(?:access_token|refresh_token|id_token|code_verifier|"
                     "client_secret|token)[\"']?\\s*[=:]\\s*[\"']?)[^\\s\"'&;,<>]+"));
  static const QRegularExpression code(
      QStringLiteral("(?i)([?&#]code=)[^&\\s\"'<>#]+"));
  static const QRegularExpression bearer(
      QStringLiteral("(?i)\\b(bearer)\\s+[A-Za-z0-9._~+/=-]+"));
  static const QRegularExpression jwt(
      QStringLiteral("\\beyJ[A-Za-z0-9_-]+\\.[A-Za-z0-9_-]+\\.[A-Za-z0-9_-]*"));

  text.replace(named, QStringLiteral("\\1") + removed);
  text.replace(code, QStringLiteral("\\1") + removed);
  text.replace(bearer, QStringLiteral("\\1 ") + removed);
  text.replace(jwt, removed);
  return text;
}

}  // namespace NexusOAuthCore
