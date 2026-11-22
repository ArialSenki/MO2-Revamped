// MO2 Revamped: tests for uibase's forEachLineInFile() (utility.h; not shipped),
// which the Gamebryo plugins read plugins.txt and loadorder.txt with.
// The reader is a template in the header, so it is built here, with
// AddressSanitizer: a read one byte past the file's buffer stops the test with
// a heap-buffer-overflow report.
// Build and run: powershell -File run.ps1 -Only lines   (from this folder)
#include "test_common.h"

#include <utility.h>

#include <QFile>

#include <io.h>

using namespace MOBase;

static QStringList lines(const QString& dir, const QByteArray& data, bool* ok = nullptr)
{
  static int n       = 0;
  const QString path = dir + QString("/f%1.txt").arg(n++);
  QFile f(path);
  f.open(QIODevice::WriteOnly);
  f.write(data);
  f.close();

  QStringList out;
  const bool r = forEachLineInFile(path, [&](QString s) {
    out.append(s);
  });
  if (ok) {
    *ok = r;
  }
  return out;
}

int main(int argc, char** argv)
{
  uitest::redirect(argc, argv);
  // AddressSanitizer reports on stderr: into the same file
  _dup2(_fileno(stdout), _fileno(stderr));
  SetStdHandle(STD_ERROR_HANDLE, reinterpret_cast<HANDLE>(_get_osfhandle(_fileno(stdout))));

  const QString dir = uitest::scratchDir("lines");
  bool ok           = false;

  CHECK(lines(dir, "a\nb\n", &ok) == QStringList({"a", "b"}));
  CHECK(ok);
  CHECK(lines(dir, "a\r\nb") == QStringList({"a", "b"}));
  CHECK(lines(dir, "\r\n\r\n\nonly\n\n") == QStringList({"only"}));
  CHECK(lines(dir, "  x  \r\n# comment\n\n y\t") == QStringList({"x", "y"}));
  CHECK(lines(dir, "   # indented comment\nz") == QStringList({"z"}));
  CHECK(lines(dir, "*Skyrim.esm\nUpdate.esm") == QStringList({"*Skyrim.esm", "Update.esm"}));
  CHECK(lines(dir, "a # not a comment") == QStringList({"a # not a comment"}));

  // UTF-8, and bytes past ASCII next to the trimmed spaces
  CHECK(lines(dir, "  \xC3\xA9t\xC3\xA9.esp  \n") ==
        QStringList({QString::fromUtf8("\xC3\xA9t\xC3\xA9.esp")}));

  // empty and missing files
  CHECK(lines(dir, "", &ok).isEmpty());
  CHECK(ok);
  QStringList none;
  CHECK(!forEachLineInFile(dir + "/missing.txt", [&](QString s) {
    none.append(s);
  }));
  CHECK(none.isEmpty());

  // the file ends in a line of spaces, or in a lone "#": the reader stops at
  // the end of its buffer
  CHECK(lines(dir, "abc\n   ") == QStringList({"abc"}));
  CHECK(lines(dir, "   ").isEmpty());
  CHECK(lines(dir, " ").isEmpty());
  CHECK(lines(dir, "abc\n \t ") == QStringList({"abc"}));
  CHECK(lines(dir, "abc\n#") == QStringList({"abc"}));
  CHECK(lines(dir, "#").isEmpty());
  CHECK(lines(dir, "x").size() == 1);

  return uitest::finish("test_lines");
}
