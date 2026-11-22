// MO2 Revamped: tests for uibase's VersionInfo (versioninfo.cpp; not shipped).
// Parsing, canonical and display strings, and ordering, with odd inputs.
// Build and run: powershell -File run.ps1 -Only versioninfo   (from this folder)
#include "test_common.h"

#include <versioninfo.h>

#include <QVersionNumber>

using MOBase::VersionInfo;

static VersionInfo V(const QString& s)
{
  return VersionInfo(s);
}

// a < b, and every other operator agrees
static bool less(const VersionInfo& a, const VersionInfo& b)
{
  return (a < b) && !(b < a) && (a <= b) && !(a >= b) && (b > a) && !(a > b) &&
         (a != b) && !(a == b);
}

static bool same(const VersionInfo& a, const VersionInfo& b)
{
  return !(a < b) && !(b < a) && (a == b) && !(a != b) && (a <= b) && (a >= b);
}

static void basics()
{
  // empty and default: invalid, empty strings, lower than anything valid
  CHECK(!V("").isValid());
  CHECK(!VersionInfo().isValid());
  CHECK_STR(V("").canonicalString(), "");
  CHECK_STR(V("").displayString(), "");
  CHECK(less(V(""), V("0.0")));
  CHECK(same(V(""), VersionInfo()));

  CHECK(V("1.2.3").isValid());
  CHECK_STR(V("1.2.3").canonicalString(), "1.2.3.0");
  CHECK_STR(V("1.2.3").displayString(), "1.2.3");
  CHECK_STR(V("1.2").displayString(), "1.2");
  CHECK_STR(V("1").displayString(), "1.0");
  CHECK_STR(V("1").displayString(3), "1.0.0");
  CHECK_STR(V("1").displayString(4), "1.0.0.0");
  CHECK_STR(V("1.2.3.4").displayString(), "1.2.3.4");

  CHECK_STR(V("final").displayString(), "1.0");

  // "v" is dropped, in either case
  CHECK(same(V("v2"), V("2")));
  CHECK(same(V("V2.1"), V("2.1")));
  CHECK_STR(V("v2").displayString(), "2.0");

  // a fifth number stays in the rest
  CHECK(V("1.2.3.4.5").isValid());
  CHECK_STR(V("1.2.3.4.5").displayString(), "1.2.3.4.5");
  CHECK(less(V("1.2.3.4"), V("1.2.3.4.5")));

  // ordering
  CHECK(less(V("1.2"), V("1.10")));
  CHECK(less(V("1.9.9"), V("2")));
  CHECK(less(V("1.0.0"), V("1.0.1")));
  CHECK(same(V("1.0"), V("1.0.0.0")));
  CHECK(less(V("1.0a"), V("1.0b")));
  CHECK(less(V("1.0beta"), V("1.0rc")));
  CHECK(less(V("1.0rc1"), V("1.0")));
  CHECK(less(V("1.0rc1"), V("1.0rc2")));
  CHECK(less(V("1.0 alpha"), V("1.0 beta")));

  // the integer constructors
  CHECK(same(VersionInfo(1, 2, 3), V("1.2.3")));
  CHECK(same(VersionInfo(1, 2, 3, 4), V("1.2.3.4")));
  CHECK(less(VersionInfo(1, 0, 0, VersionInfo::RELEASE_BETA), VersionInfo(1, 0, 0)));

  // clear()
  VersionInfo v("3.1");
  v.clear();
  CHECK(!v.isValid());
  CHECK_STR(v.canonicalString(), "");

  CHECK(V("1.2.3").asQVersionNumber() == QVersionNumber(1, 2, 3));
}

static void semverPrereleases()
{
  // MO2 Revamped's own scheme: 1.1.0-beta.15
  CHECK(V("1.1.0-beta.15").isValid());
  CHECK(less(V("1.1.0-beta.15"), V("1.1.0")));
  CHECK(less(V("1.1.0-alpha.3"), V("1.1.0-beta.1")));
  CHECK(less(V("1.1.0-beta.15"), V("1.1.0-beta.16")));
  CHECK(less(V("1.1.0-beta.15"), V("1.1.0-rc.1")));
  CHECK(less(V("1.0.9"), V("1.1.0-beta.1")));

  // the number after a separator is a number: beta.9 is before beta.10
  CHECK(less(V("1.1.0-beta.9"), V("1.1.0-beta.10")));
  CHECK(less(V("1.1.0-rc.2"), V("1.1.0-rc.10")));
  CHECK(same(V("1.1.0-beta.15"), V("1.1.0beta15")));
  // a fifth number too, and a dash revision
  CHECK(less(V("1.2.3.4.9"), V("1.2.3.4.10")));
  CHECK(less(V("1.0-1"), V("1.0-2")));

  // the canonical string parses back to the same version
  for (const char* s : {"1.1.0-beta.15", "1.1.0-beta.9", "1.1.0-rc.2", "2.0b3",
                        "1.0 alpha", "1.2.3.4.5", "v2"}) {
    const VersionInfo a(s);
    const VersionInfo b(a.canonicalString());
    if (!same(a, b)) {
      std::printf("  round trip of \"%s\" via \"%s\"\n", s,
                  a.canonicalString().toUtf8().constData());
    }
    CHECK(same(a, b));
  }
}

static void prealpha()
{
  // "prealpha" is not "alpha" with a "pre" left over
  CHECK(less(V("1.0prealpha"), V("1.0alpha")));
  CHECK(less(V("1.0 pre-alpha"), V("1.0 alpha")));
  CHECK_STR(V("1.0prealpha").displayString(), "1.0 pre-alpha");

  // canonicalString() writes " pre-alpha": it has to parse back as pre-alpha
  const VersionInfo pre(1, 0, 0, VersionInfo::RELEASE_PREALPHA);
  const VersionInfo back(pre.canonicalString());
  CHECK(same(pre, back));
  CHECK_STR(back.canonicalString(), pre.canonicalString());
  CHECK(less(back, VersionInfo(1, 0, 0, VersionInfo::RELEASE_ALPHA)));
}

static void releaseCandidates()
{
  // "rc" is a release candidate as a word of its own: at the start of the
  // rest or after a separator, before a number, a separator or the end
  for (const char* s : {"1.0rc", "1.0rc1", "1.0RC2", "1.0 rc", "1.0-rc.3", "1.0_rc4",
                        "1.0.rc", "1.0 RC 5"}) {
    const bool rc = less(V(s), V("1.0")) && less(V("1.0beta"), V(s));
    if (!rc) {
      std::printf("  \"%s\" not a release candidate\n", s);
    }
    CHECK(rc);
  }
  CHECK(same(V("1.0-rc.3"), V("1.0rc3")));

  // inside a word it isn't: "source", "src", "arc", "rcfix" were candidates
  // (before 1.0, with the "rc" cut out of the text)
  for (const char* s : {"1.0source", "1.0 source", "1.0-src", "1.0 arc", "1.0rcfix",
                        "1.0 forced", "1.0-hotfix-rc_x"}) {
    const bool rc = less(V(s), V("1.0"));
    if (rc) {
      std::printf("  \"%s\" taken as a release candidate\n", s);
    }
    CHECK(!rc);
  }
  CHECK_STR(V("1.0source").displayString(), "1.0source");
  CHECK_STR(V("1.0 arc").displayString(), "1.0arc");
  CHECK_STR(V("1.0-src").displayString(), "1.0-src");
  CHECK(less(V("1.0"), V("1.0source")));

  // after another separator than "-._ " too, as before the word rule
  for (const char* s : {"1.0~rc1", "1.0+rc1", "1.0 (RC1)"}) {
    const bool rc = less(V(s), V("1.0")) && less(V("1.0beta"), V(s));
    if (!rc) {
      std::printf("  \"%s\" not a release candidate\n", s);
    }
    CHECK(rc);
  }

  // the canonical string still parses back, also with text between the
  // number and "rc": "2.0-SE-RC1" was saved as "2.0.0.0rc-SE-1", which read
  // back as a final version newer than the same one from Nexus
  for (const char* s : {"1.0rc1", "1.0-rc.3", "1.0source", "2.0-SE-RC1", "2.0 Full RC1",
                        "1.0-hotfix rc", "1.0 RC 5", "1.0~rc1"}) {
    const VersionInfo a(s);
    const VersionInfo back(a.canonicalString());
    if (!same(a, back)) {
      std::printf("  \"%s\" saved as \"%s\" reads back another version\n", s,
                  a.canonicalString().toUtf8().constData());
    }
    CHECK(same(a, back));
  }
  CHECK(less(VersionInfo(V("2.0-SE-RC1").canonicalString()), V("2.0-SE")));
}

static void schemes()
{
  // decimal mark: "1.05" is 1.05, before 1.1
  CHECK(V("1.05").scheme() == VersionInfo::SCHEME_DECIMALMARK);
  CHECK_STR(V("1.05").canonicalString(), "f1.05");
  CHECK_STR(V("1.05").displayString(), "1.05");
  CHECK(less(V("1.05"), V("1.1")));
  CHECK(V("f1.5").scheme() == VersionInfo::SCHEME_DECIMALMARK);
  CHECK(less(V("f1.25"), V("f1.5")));
  CHECK(same(V("f1.05"), V("1.05")));

  // two dots can't be a decimal mark
  CHECK(V("f1.2.3").scheme() == VersionInfo::SCHEME_REGULAR);

  // numbers and letters
  CHECK(V("n1.0.1").scheme() == VersionInfo::SCHEME_NUMBERSANDLETTERS);

  // dates are before regular versions; a "date" before 1900 is a version
  CHECK(V("d2023.11.5").scheme() == VersionInfo::SCHEME_DATE);
  CHECK(V("d12.1").scheme() == VersionInfo::SCHEME_REGULAR);
  CHECK(less(V("d2023.11.5"), V("0.1")));
  CHECK(less(V("d2023.11.5"), V("d2023.11.6")));

  // manual input doesn't take the scheme letters as hints
  CHECK(VersionInfo("f1.5", VersionInfo::SCHEME_DISCOVER, true).scheme() ==
        VersionInfo::SCHEME_LITERAL);

  // no number at all: kept literally
  CHECK(V("unknown").scheme() == VersionInfo::SCHEME_LITERAL);
  CHECK_STR(V("unknown").displayString(), "unknown");
  CHECK(less(V("abc"), V("abd")));
}

static void reparse()
{
  // parse() on a used VersionInfo, as the download manager and ModInfo do:
  // nothing of the previous version may survive
  VersionInfo v("1.05");
  CHECK_STR(v.canonicalString(), "f1.05");
  v.parse("f1.5");
  CHECK_STR(v.canonicalString(), "f1.5");
  CHECK(same(v, V("f1.5")));
  CHECK(less(V("f1.25"), v));

  v.parse("1.0beta");
  v.parse("1.0");
  CHECK(same(v, V("1.0")));

  v.parse("");
  CHECK(!v.isValid());
}

static void oddInputs()
{
  // negative numbers aren't numbers here: kept as text
  CHECK(V("-1").isValid());
  CHECK_STR(V("-1").displayString(), "-1");
  CHECK(V("1.-2").isValid());

  // numbers past int: no crash, a valid version
  CHECK(V("99999999999").isValid());
  CHECK(V("1.99999999999.3").isValid());
  CHECK(V("2147483647.2147483647").isValid());
  CHECK_STR(V("2147483647.2147483647").displayString(), "2147483647.2147483647");
  CHECK(less(V("2147483646"), V("2147483647")));

  // past int a number isn't read (it was 0: "99999999999" was 0.0): the
  // version is kept as text, as one with no number is
  CHECK(V("99999999999").scheme() == VersionInfo::SCHEME_LITERAL);
  CHECK_STR(V("99999999999").displayString(), "99999999999");
  CHECK(!same(V("99999999999"), V("0")));
  CHECK_STR(V("1.99999999999.3").displayString(), "1.99999999999.3");
  CHECK(!same(V("1.99999999999.3"), V("1.0.3")));
  CHECK(V("1.2.3.99999999999").scheme() == VersionInfo::SCHEME_LITERAL);
  CHECK(V("2147483647.2147483647").scheme() == VersionInfo::SCHEME_REGULAR);
  CHECK(V("2147483648").scheme() == VersionInfo::SCHEME_LITERAL);
  CHECK(V(QString(100000, '9')).scheme() == VersionInfo::SCHEME_LITERAL);
  // the numbers before the one that doesn't fit still order it: a date and
  // time as the build ("1.6.2511221530") isn't a 0.0 older than everything
  CHECK(less(V("1.5"), V("1.6.2511221530")));
  CHECK(less(V("1.6"), V("1.6.2511221530")));
  CHECK(less(V("1.6.2511221530"), V("1.7")));
  CHECK(less(V("1.6.2511221530"), V("1.6.2512011200")));
  CHECK(less(V("1.9.2511221530"), V("1.10.2511221530")));
  CHECK_STR(V("1.6.2511221530").displayString(), "1.6.2511221530");
  CHECK(same(V("1.6.2511221530"), VersionInfo(V("1.6.2511221530").canonicalString())));
  // the same VersionInfo parsed again
  VersionInfo big("1.2.3");
  big.parse("4294967296.1");
  CHECK_STR(big.displayString(), "4294967296.1");
  big.parse("1.2.3");
  CHECK(same(big, V("1.2.3")));

  // whitespace, junk, very long strings
  CHECK(V("   ").isValid());
  CHECK(V("1.0 (fixed)").isValid());
  CHECK(V(QString(100000, '9')).isValid());
  CHECK(V(QString(5000, '.')).isValid());

  // non-ASCII text survives (the rest is trimmed: no space after the number)
  CHECK_STR(V(QString::fromUtf8("1.0 \xC3\xA9t\xC3\xA9")).displayString(),
            QString::fromUtf8("1.0\xC3\xA9t\xC3\xA9"));
  CHECK_STR(V(QString::fromUtf8("\xE3\x83\x90\xE3\x83\xBC\xE3\x82\xB8\xE3\x83\xA7\xE3\x83\xB3")).displayString(),
            QString::fromUtf8("\xE3\x83\x90\xE3\x83\xBC\xE3\x82\xB8\xE3\x83\xA7\xE3\x83\xB3"));

  // digits that aren't ASCII (Arabic-Indic, full-width) are not lost: they
  // aren't read as "0.0" with the text thrown away
  const QString arabic = QString::fromUtf8("\xD9\xA1.\xD9\xA2");  // ١.٢
  const QString wide   = QString::fromUtf8("\xEF\xBC\x91.\xEF\xBC\x92");  // １.２
  CHECK(V(arabic).isValid());
  CHECK(V(arabic).displayString() != "0.0");
  CHECK(V(wide).displayString() != "0.0");
  CHECK(!same(V(arabic), V("0.0")));
  CHECK(!same(V(arabic), V(wide)));
}

int main(int argc, char** argv)
{
  uitest::redirect(argc, argv);
  basics();
  semverPrereleases();
  prealpha();
  releaseCandidates();
  schemes();
  reparse();
  oddInputs();
  return uitest::finish("test_versioninfo");
}
