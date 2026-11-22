#include "collectionnames.h"

#include "collectionfiles.h"

#include <filesystemutilities.h>

#include <QRegularExpression>
#include <QStringList>

#include <system_error>

namespace Collections
{

bool makeFolderName(QString& name)
{
  QString fixed = name;
  if (!MOBase::fixDirectoryName(fixed)) {
    name.clear();
    return false;
  }
  // fixDirectoryName trims dots before it drops characters: "J.R.R.?" ends in one
  while (fixed.endsWith(u'.') || fixed.endsWith(u' ')) {
    fixed.chop(1);
  }
  if (fixed.isEmpty() || fixed.contains(u'/') || fixed.contains(u'\\')) {
    name.clear();
    return false;
  }
  // a name shaped like an 8.3 short name ("SKYUI~1") could stand for another
  // mod's folder: its "~" becomes "-"
  static const QRegularExpression shortName(QStringLiteral("^[^.~\\s]{1,6}~[0-9]{1,6}(\\.[^.\\s]{0,3})?$"));
  if (shortName.match(fixed).hasMatch()) {
    fixed.replace(u'~', u'-');
  }
  // what's left that isn't a safe folder starts with a device name ("Aux. Armors")
  if (!isSafeRelativePath(fixed)) {
    fixed.prepend(u'_');
    if (!isSafeRelativePath(fixed)) {
      name.clear();
      return false;
    }
  }
  name = fixed;
  return true;
}

QString versionKey(const QString& version)
{
  static const QRegularExpression nonDigits(QStringLiteral("[^0-9]+"));
  QStringList parts = version.split(nonDigits, Qt::SkipEmptyParts);
  while (parts.size() > 3) {
    parts.removeLast();
  }
  for (QString& p : parts) {
    p = QString::number(p.toLongLong());
  }
  // MO2 Revamped: 2.5.0.0 (a collection's) is 2.5 (Cyberpunk's own)
  while (parts.size() > 1 && parts.last() == QStringLiteral("0")) {
    parts.removeLast();
  }
  return parts.join('.');
}

bool staysInside(const std::filesystem::path& base, const std::filesystem::path& target)
{
  std::error_code ec;
  const auto b = std::filesystem::weakly_canonical(base, ec);
  if (ec) {
    return false;
  }
  const auto t = std::filesystem::weakly_canonical(target, ec);
  if (ec) {
    return false;
  }
  const auto rel = t.lexically_relative(b);
  return !rel.empty() && *rel.begin() != ".." && !rel.is_absolute();
}

}  // namespace Collections
