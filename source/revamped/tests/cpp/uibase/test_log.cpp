// MO2 Revamped: tests for uibase's logger (log.cpp; not shipped).
// The fallback logger getDefault() returns before createDefault() (commit
// bfa2ccdd: uibase used outside MO2 crashed on its first log line), then
// createDefault() and the default logger: levels, callbacks, patterns, line
// splitting, the blacklist, format errors, files and threads.
// Build and run: powershell -File run.ps1 -Only log   (from this folder)
#include "test_common.h"

#include <log.h>
#include <utility.h>

#include <QFile>

#include <atomic>
#include <mutex>
#include <thread>
#include <vector>

using namespace MOBase;

static std::mutex g_mutex;
static std::vector<log::Entry> g_entries;
static std::atomic<int> g_count{0};

static void collect(log::Entry e)
{
  std::scoped_lock lock(g_mutex);
  g_entries.push_back(std::move(e));
}

static void count(log::Entry)
{
  ++g_count;
}

static std::vector<log::Entry> take()
{
  std::scoped_lock lock(g_mutex);
  return std::exchange(g_entries, {});
}

static QString fileText(const QString& path)
{
  QFile f(path);
  return f.open(QIODevice::ReadOnly) ? QString::fromUtf8(f.readAll()) : QString();
}

static void fallback()
{
  // before createDefault(): one fallback, warnings and up
  log::Logger& a = log::getDefault();
  log::Logger& b = log::getDefault();
  CHECK(&a == &b);
  CHECK(a.level() == log::Warning);

  // logging with no sink at all, every level and kind of argument
  log::debug("debug {}", 1);
  log::info("info {}", QString("q"));
  log::warn("warn {}", std::wstring(L"w"));
  log::error("error {} {}", 1, 2.5);
  log::warn(QString("runtime {}"), 3);
  log::warn(QString("no arguments {}"));
  CHECK(true);

  // the uibase calls that log, as from a process outside MO2
  CHECK_STR(getFileVersion("Z:\\no\\such\\file.exe"), "");
  CHECK(shell::Result::makeFailure(ERROR_ACCESS_DENIED).message().size() > 0);
  { TimeThis t("fallback"); }

  // a callback on the fallback gets warnings and errors only
  a.setCallback(&collect);
  log::debug("not this");
  log::info("nor this");
  log::warn("this {}", 1);
  log::error("and this");
  auto got = take();
  CHECK(got.size() == 2);
  if (got.size() == 2) {
    CHECK(got[0].message == "this 1");
    CHECK(got[0].level == log::Warning);
    CHECK(got[0].formattedMessage == "this 1");  // its pattern is "%v"
    CHECK(got[1].level == log::Error);
  }

  // a null callback turns it off
  a.setCallback(nullptr);
  log::error("off");
  CHECK(take().empty());
}

static void defaultLogger()
{
  log::Logger& fb = log::getDefault();

  log::createDefault(log::LoggerConfiguration{
      .name = "tests", .maxLevel = log::Debug, .pattern = "%l|%v"});
  log::Logger& d = log::getDefault();
  CHECK(&d != &fb);
  CHECK(&d == &log::getDefault());
  CHECK(d.level() == log::Debug);

  d.setCallback(&collect);
  log::debug("dbg {}", 42);
  auto got = take();
  CHECK(got.size() == 1);
  if (got.size() == 1) {
    CHECK(got[0].level == log::Debug);
    CHECK(got[0].message == "dbg 42");
    CHECK(got[0].formattedMessage == "debug|dbg 42");
  }

  // the fallback is still usable and separate
  fb.setCallback(&count);
  log::error("to the default only");
  CHECK(g_count == 0);
  CHECK(take().size() == 1);
  fb.setCallback(nullptr);

  // one entry per line; an empty line is kept, a final newline gives an empty one
  log::info("one\ntwo\n\nfour");
  got = take();
  CHECK(got.size() == 4);
  if (got.size() == 4) {
    CHECK(got[0].message == "one");
    CHECK(got[1].message == "two");
    CHECK(got[2].message == "");
    CHECK(got[3].message == "four");
  }
  log::info("");
  CHECK(take().size() == 1);

  // levels
  d.setLevel(log::Warning);
  CHECK(d.level() == log::Warning);
  log::info("hidden");
  log::warn("shown");
  CHECK(take().size() == 1);
  d.setLevel(log::Debug);

  // the blacklist, case-insensitive
  d.addToBlacklist("C:\\Users\\Someone", "C:\\Users\\USERNAME");
  d.addToBlacklist("", "ignored");
  d.addToBlacklist("ignored", "");
  log::info("path c:\\users\\someone\\x and C:\\Users\\SOMEONE\\y");
  got = take();
  CHECK(got.size() == 1);
  if (got.size() == 1) {
    CHECK(got[0].message == "path C:\\Users\\USERNAME\\x and C:\\Users\\USERNAME\\y");
  }
  d.addToBlacklist("c:\\users\\someone", "<user>");  // replaces the entry
  log::info("c:\\users\\someone");
  got = take();
  CHECK(got.size() == 1 && got[0].message == "<user>");
  d.removeFromBlacklist("C:\\USERS\\SOMEONE");
  log::info("c:\\users\\someone");
  got = take();
  CHECK(got.size() == 1 && got[0].message == "c:\\users\\someone");
  d.addToBlacklist("secret", "***");
  d.resetBlacklist();
  log::info("secret");
  got = take();
  CHECK(got.size() == 1 && got[0].message == "secret");

  // a bad runtime format string: an error entry, no exception
  log::info(QString("{} and {}"), 1);
  log::info(QString("{:d}"), QString("text"));
  got = take();
  CHECK(got.size() == 2);
  for (const auto& e : got) {
    CHECK(e.level == log::Error);
    CHECK(e.message == "format error while logging");
  }

  // braces in arguments are text
  log::info("{}", "{} {0} {");
  got = take();
  CHECK(got.size() == 1 && got[0].message == "{} {0} {");

  // a callback that logs doesn't recurse
  static int depth = 0;
  d.setCallback([](log::Entry) {
    ++depth;
    log::error("from the callback");
  });
  log::info("outer");
  CHECK(depth == 1);
  d.setCallback(&collect);

  // levelToString
  CHECK_STR(log::levelToString(log::Debug), "debug");
  CHECK_STR(log::levelToString(log::Info), "info");
  CHECK_STR(log::levelToString(log::Warning), "warning");
  CHECK_STR(log::levelToString(log::Error), "error");
}

static void files()
{
  log::Logger& d    = log::getDefault();
  const QString dir = uitest::scratchDir("log");

  // a single file, then closed by setting no file
  const QString single = dir + "/single.log";
  d.setFile(log::File::single(single.toStdWString()));
  log::info("to the file");
  d.setFile(log::File());
  log::info("not to the file");
  CHECK(fileText(single).contains("info|to the file"));
  CHECK(!fileText(single).contains("not to the file"));

  // a file that can't be created: no exception, logging goes on
  d.setFile(log::File::single((dir + "/no/such/dir/<>.log").toStdWString()));
  d.setFile(log::File::single(L"Z:\\definitely\\not\\here.log"));
  log::info("still logging");
  CHECK(take().size() >= 1);
  d.setFile(log::File());

  // rotating
  const QString rot = dir + "/rot.log";
  d.setFile(log::File::rotating(rot.toStdWString(), 200, 2));
  for (int i = 0; i < 20; ++i) {
    log::info("rotating line {}", i);
  }
  d.setFile(log::File());
  CHECK(QFile::exists(rot));
  CHECK(QFile::exists(dir + "/rot.1.log"));
  CHECK(fileText(rot).contains("rotating line 19"));

  // a pattern set later is the pattern of sinks added later too
  d.setPattern("P %v");
  const QString later = dir + "/later.log";
  d.setFile(log::File::single(later.toStdWString()));
  log::warn("patterned");
  d.setFile(log::File());
  CHECK(fileText(later).contains("P patterned"));
  {
    auto got = take();
    CHECK(!got.empty() && got.back().formattedMessage == "P patterned");
  }
}

static void threads()
{
  log::Logger& d = log::getDefault();
  d.setCallback(&count);
  g_count = 0;
  std::vector<std::thread> ts;
  for (int t = 0; t < 4; ++t) {
    ts.emplace_back([t] {
      for (int i = 0; i < 500; ++i) {
        log::info("thread {} line {}", t, i);
      }
    });
  }
  for (auto& t : ts) {
    t.join();
  }
  CHECK(g_count == 2000);
  d.setCallback(nullptr);
}

int main(int argc, char** argv)
{
  uitest::redirect(argc, argv);
  fallback();
  defaultLogger();
  files();
  threads();
  return uitest::finish("test_log");
}
