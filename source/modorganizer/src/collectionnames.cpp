#include "collectionnames.h"

#include "collectionfiles.h"

#include <filesystemutilities.h>

#include <QRegularExpression>
#include <QStringList>

#include <system_error>

namespace Collections
{

namespace
{

// MO2 Revamped: at most max UTF-16 units, not between the two halves of an
// emoji, and without the dots and spaces a cut can leave at the end (a
// folder name can have 255 units; 300 failed to be created)
void cutFolderName(QString& name, qsizetype max)
{
  if (name.size() > max) {
    qsizetype cut = max;
    if (cut > 0 && name.at(cut - 1).isHighSurrogate()) {
      --cut;
    }
    name.truncate(cut);
  }
  while (name.endsWith(u'.') || name.endsWith(u' ')) {
    name.chop(1);
  }
}

}  // namespace

bool makeFolderName(QString& name)
{
  QString fixed = name;
  if (!MOBase::fixDirectoryName(fixed)) {
    // fixDirectoryName() now refuses a device name ("Aux", "con"): such a mod
    // was installed as "_Aux" and must stay so, not become "Mod 3" (its
    // folder would change at the collection's next update)
    // (a name with nothing usable, "..." or "?*", leaves only the "_": refused)
    QString prefixed = QStringLiteral("_") + name;
    if (!MOBase::fixDirectoryName(prefixed) || prefixed == QStringLiteral("_")) {
      name.clear();
      return false;
    }
    fixed = prefixed;
  }
  // fixDirectoryName trims dots before it drops characters: "J.R.R.?" ends in one
  cutFolderName(fixed, MaxFolderName);
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
    cutFolderName(fixed, MaxFolderName);
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
