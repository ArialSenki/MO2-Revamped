#include "filesystemutilities.h"

#include <QRegularExpression>
#include <QString>

namespace MOBase
{

namespace
{

// MO2 Revamped: a Windows device name, in any case
bool isDeviceName(const QString& name)
{
  static const QString invalidNames[] = {"CON",  "PRN",  "AUX",  "NUL",  "COM1", "COM2",
                                         "COM3", "COM4", "COM5", "COM6", "COM7", "COM8",
                                         "COM9", "LPT1", "LPT2", "LPT3", "LPT4", "LPT5",
                                         "LPT6", "LPT7", "LPT8", "LPT9"};
  for (const QString& device : invalidNames) {
    if (name.compare(device, Qt::CaseInsensitive) == 0) {
      return true;
    }
  }
  return false;
}

}  // namespace

bool fixDirectoryName(QString& name)
{
  // MO2 Revamped: the characters dropped first, then the trailing dots and
  // spaces, then the device names, in any case: dots trimmed before the drop
  // left "J.R.R.?" as "J.R.R." (Explorer can't open it), "CON ?" became "CON",
  // and "con" or "Aux" passed
  QString temp = name;
  temp.replace(QRegularExpression(R"([<>:"/\\|?*])"), "");
  temp = temp.simplified();
  while (temp.endsWith('.')) {
    temp.chop(1);
    temp = temp.trimmed();
  }

  if (isDeviceName(temp)) {
    temp = "";
  }

  if (temp.length() >= 1) {
    name = temp;
    return true;
  } else {
    return false;
  }
}

QString sanitizeFileName(const QString& name, const QString& replacement)
{
  QString new_name = name;

  // Remove characters not allowed by Windows
  new_name.replace(QRegularExpression("[\\x{00}-\\x{1f}\\\\/:\\*\\?\"<>|]"),
                   replacement);

  // Don't end with a period or a space
  // Don't be "." or ".."
  new_name.remove(QRegularExpression("[\\. ]*$"));

  // Recurse until stuff stops changing
  if (new_name != name) {
    return sanitizeFileName(new_name);
  }
  return new_name;
}

bool validFileName(const QString& name)
{
  if (name.isEmpty()) {
    return false;
  }
  if (name == "." || name == "..") {
    return false;
  }

  // MO2 Revamped: device names aren't files, also with an extension or with
  // spaces before it ("CON.txt", "AUX .ini" open the device): the names
  // fixDirectoryName refuses. "CON" passed
  QString stem = name.section(u'.', 0, 0);
  while (stem.endsWith(u' ')) {
    stem.chop(1);
  }
  if (isDeviceName(stem)) {
    return false;
  }

  return (name == sanitizeFileName(name));
}

}  // namespace MOBase
