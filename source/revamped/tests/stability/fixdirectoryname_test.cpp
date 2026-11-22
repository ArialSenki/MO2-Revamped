// MO2 Revamped: MOBase::fixDirectoryName on names that aren't folder names
// (not shipped). Built against uibase.lib and Qt6Core; see the report for the
// command line.
#include <filesystemutilities.h>

#include <QString>
#include <cstdio>

namespace
{
int failures = 0;

// expect: the fixed name, or empty when the name must be refused
void check(const char* input, const char* expect)
{
  QString name     = QString::fromUtf8(input);
  const bool ok    = MOBase::fixDirectoryName(name);
  const bool good  = (expect == nullptr) ? !ok : (ok && name == QString::fromUtf8(expect));
  std::printf("%s: \"%s\" -> %s \"%s\"\n", good ? "ok  " : "FAIL", input,
              ok ? "kept" : "refused", name.toUtf8().constData());
  failures += good ? 0 : 1;
}
}  // namespace

int main()
{
  check("Normal Mod", "Normal Mod");
  check("Existing/sub", "Existingsub");    // no folder inside another mod
  check("..", nullptr);                    // not the instance's folder
  check(".", nullptr);
  check("Odd.", "Odd");                    // Windows drops the dot itself
  check("Name.?", "Name");                 // the dot was kept, then "?" went
  check("Name ?.", "Name");
  check("  Spaces  ", "Spaces");
  check("CON", nullptr);
  check("Ñandú 日本語", "Ñandú 日本語");
  check("a:b*c", "abc");

  std::printf(failures == 0 ? "all passed\n" : "%d failed\n", failures);
  return failures == 0 ? 0 : 1;
}
