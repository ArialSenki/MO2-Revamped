// MO2 Revamped: tests for the download manager's link and file name helpers
// (downloadurl.cpp, not shipped): withoutQuery, fileNameFromDisposition and
// fileNameFromUrl, and uibase's sanitizeFileName as the download manager uses it.
// Build and run: run.ps1 in this folder, or from a Developer prompt with the
// Qt and uibase paths of run.ps1:
//   cl /nologo /std:c++20 /EHsc /permissive- /utf-8 /MD /DUNICODE /D_UNICODE /DNOMINMAX
//      /I<qt>\include /I<qt>\include\QtCore /I<src> /I<uibase>\src download_url_test.cpp
//      <src>\downloadurl.cpp /link <qt>\lib\Qt6Core.lib <uibase build>\uibase.lib
//   download_url_test
#include "downloadurl.h"

#include <filesystemutilities.h>

#include <QString>
#include <QStringList>

#include <cstdio>

using namespace DownloadUrl;

namespace
{
int passed = 0;
int failed = 0;

void check(bool ok, const char* what, int line, const QString& detail = {})
{
  if (ok) {
    ++passed;
  } else {
    ++failed;
    std::printf("FAIL line %d: %s %s\n", line, what, qUtf8Printable(detail));
  }
}

#define CHECK(x) check((x), #x, __LINE__)
#define CHECK_EQ(a, b)                                                                     \
  do {                                                                                     \
    const QString got_  = (a);                                                             \
    const QString want_ = (b);                                                             \
    check(got_ == want_, #a " == " #b, __LINE__,                                           \
          QStringLiteral("(got \"%1\", want \"%2\")").arg(got_, want_));                   \
  } while (false)

QString u8(const char* s)
{
  return QString::fromUtf8(s);
}

void testWithoutQuery()
{
  CHECK_EQ(withoutQuery("https://cf-files.nexusmods.com/cdn/1/2/a.7z?md5=abc&expires=1"),
           "https://cf-files.nexusmods.com/cdn/1/2/a.7z");
  CHECK_EQ(withoutQuery("https://h/a.7z"), "https://h/a.7z");
  CHECK_EQ(withoutQuery("Error transferring https://h/a.7z?key=SECRET - server replied: Gone"),
           "Error transferring https://h/a.7z - server replied: Gone");
  CHECK_EQ(withoutQuery("\"https://h/a?k=1\" and 'https://h/b?k=2'"),
           "\"https://h/a\" and 'https://h/b'");
  CHECK(!withoutQuery("https://h/a.7z?key=SECRET#frag").contains("SECRET"));
  CHECK(!withoutQuery("https://h/a.7z??key=SECRET").contains("SECRET"));
  CHECK_EQ(withoutQuery(""), "");
}

void testDisposition()
{
  CHECK_EQ(fileNameFromDisposition("attachment; filename=\"a b.7z\""), "a b.7z");
  CHECK_EQ(fileNameFromDisposition("attachment; filename=a.7z"), "a.7z");
  CHECK_EQ(fileNameFromDisposition("attachment; filename=a.7z; size=3"), "a.7z");
  CHECK_EQ(fileNameFromDisposition("attachment;filename=  a.7z  "), "a.7z");
  CHECK_EQ(fileNameFromDisposition("ATTACHMENT; FILENAME=\"X.7z\""), "X.7z");
  CHECK_EQ(fileNameFromDisposition("attachment; filename=\"a;b.7z\""), "a;b.7z");
  CHECK_EQ(fileNameFromDisposition("attachment; filename*=UTF-8''%E2%82%AC%20rates.zip"),
           u8("€ rates.zip"));
  CHECK_EQ(fileNameFromDisposition("attachment; filename*=utf-8'en'%C3%B1and%C3%BA.7z"),
           u8("ñandú.7z"));
  CHECK_EQ(fileNameFromDisposition("attachment; filename*=UTF-8''%F0%9F%8E%89.7z"),
           u8("🎉.7z"));
  CHECK_EQ(fileNameFromDisposition("attachment; filename*=ISO-8859-1''%E9t%E9.7z"),
           u8("été.7z"));
  // the extended one wins, wherever it is
  CHECK_EQ(fileNameFromDisposition(
               "attachment; filename=\"fallback.zip\"; filename*=UTF-8''real.zip"),
           "real.zip");
  CHECK_EQ(fileNameFromDisposition(
               "attachment; filename*=UTF-8''real.zip; filename=\"fallback.zip\""),
           "real.zip");
  // decoded once: a double-encoded name keeps one level
  CHECK_EQ(fileNameFromDisposition("attachment; filename*=UTF-8''%2541.7z"), "%41.7z");
  // nothing
  CHECK_EQ(fileNameFromDisposition(""), "");
  CHECK_EQ(fileNameFromDisposition("attachment"), "");
  CHECK_EQ(fileNameFromDisposition("attachment; filename=\"\""), "");
  CHECK_EQ(fileNameFromDisposition("attachment; filename*=UTF-8''"), "");
  CHECK_EQ(fileNameFromDisposition("inline; name=\"field\""), "");
  // traversal is the caller's to remove (sanitizeFileName)
  CHECK_EQ(fileNameFromDisposition("attachment; filename*=UTF-8''..%2F..%2Fevil.dll"),
           "../../evil.dll");
  CHECK_EQ(MOBase::sanitizeFileName(
               fileNameFromDisposition("attachment; filename=\"..\\..\\evil.dll\"")),
           "....evil.dll");
}

void testUrlNames()
{
  CHECK_EQ(fileNameFromUrl("https://h/p/file.7z"), "file.7z");
  CHECK_EQ(fileNameFromUrl("HTTPS://H/p/File.7Z"), "File.7Z");
  CHECK_EQ(fileNameFromUrl("https://h/p/My%20Mod%20v1.7z"), "My Mod v1.7z");
  CHECK_EQ(fileNameFromUrl("https://h/p/%C3%91and%C3%BA.7z"), u8("Ñandú.7z"));
  CHECK_EQ(fileNameFromUrl(u8("https://h/p/Ñandú 🎉.7z")), u8("Ñandú 🎉.7z"));
  CHECK_EQ(fileNameFromUrl("https://h//p///file.7z"), "file.7z");
  // decoded once
  CHECK_EQ(fileNameFromUrl("https://h/p/a%2520b.7z"), "a%20b.7z");

  // the query and the fragment aren't part of the name
  CHECK_EQ(fileNameFromUrl("https://cf-files.nexusmods.com/cdn/1/2/a.7z?md5=x&expires=1"),
           "a.7z");
  CHECK_EQ(fileNameFromUrl("https://h/dl/file.7z?token=ab/cd+ef=="), "file.7z");
  CHECK_EQ(fileNameFromUrl("https://h/dl/file.7z?next=/x/y/z.html"), "file.7z");
  CHECK_EQ(fileNameFromUrl("https://h/dl/file.7z#part/2"), "file.7z");
  CHECK_EQ(fileNameFromUrl("https://h/dl/file.7z#top"), "file.7z");

  // no name in the path
  CHECK_EQ(fileNameFromUrl(""), "unknown");
  CHECK_EQ(fileNameFromUrl("https://h/dl/"), "unknown");
  // a query on a path without a name is left as it was: addDownload() drops
  // it, and the answer's Content-Disposition names the file
  CHECK_EQ(fileNameFromUrl("https://h/download/?id=5"), "?id=5");

  // S3-style signed links name the file in the query
  CHECK_EQ(fileNameFromUrl("https://s3.example/b/obj?response-content-disposition="
                           "attachment%3B%20filename%3D%22Real%20Name.7z%22&X-Amz-Signature=1"),
           "Real Name.7z");
  CHECK_EQ(fileNameFromUrl("https://s3.example/b/obj?response-content-disposition="
                           "attachment%3B%20filename%2A%3DUTF-8%27%27%25C3%25B1.7z"),
           u8("ñ.7z"));
  CHECK_EQ(fileNameFromUrl("https://s3.example/b/obj?response-content-disposition="
                           "attachment%3B%20filename%3D%22..%2F..%2Fevil.dll%22"),
           "....evil.dll");
  CHECK_EQ(fileNameFromUrl("https://s3.example/b/obj?response-content-disposition="
                           "attachment%3B%20filename%3D%22...%22"),
           "unknown");
  CHECK_EQ(fileNameFromUrl("https://s3.example/b/obj.7z?response-content-disposition=inline"),
           "obj.7z");

  // whatever comes out, sanitized as the download manager does, is a file name
  const QStringList urls = {
      "https://h/a%2Fb.7z",      "https://h/a%5Cb.7z",  "https://h/a%3A%2A.7z",
      "https://h/CON",           "https://h/a.7z.",     "https://h/a.7z%20%20",
      "https://h/%00a.7z",       "https://h/..",        "https://h/.",
      "Z:\\home\\user\\a.7z",    "nxm://skyrim/mods/1/files/2?key=a&expires=1",
      "https://h/" + QString(400, 'x') + ".7z",
  };
  for (const QString& url : urls) {
    const QString name = MOBase::sanitizeFileName(fileNameFromUrl(url));
    check(!name.contains(u'/') && !name.contains(u'\\') && !name.contains(u':') &&
              !name.endsWith(u'.') && !name.endsWith(u' ') && !name.contains(QChar(0)),
          "sanitized url name", __LINE__, url + " -> " + name);
  }
}

}  // namespace

int main()
{
  testWithoutQuery();
  testDisposition();
  testUrlNames();
  std::printf("download_url_test: %d passed, %d failed\n", passed, failed);
  return failed == 0 ? 0 : 1;
}
