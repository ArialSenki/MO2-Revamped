// MO2 Revamped: tests for parseSteamLibraries (not shipped).
//   cl /nologo /std:c++20 /EHsc /fsanitize=address /I ..\..\games\bethesda\detection
//      steamlibraries_test.cpp ..\..\games\bethesda\detection\steamlibraries.cpp
//   steamlibraries_test [path to a real libraryfolders.vdf, read only]
#include "steamlibraries.h"

#include <cstdio>
#include <fstream>
#include <sstream>

using namespace mo2r::bethesda;

static int failures = 0;
#define CHECK(x)                                                                 \
  do {                                                                           \
    if (!(x)) {                                                                  \
      std::printf("FAIL line %d: %s\n", __LINE__, #x);                           \
      ++failures;                                                                \
    }                                                                            \
  } while (0)

int main(int argc, char** argv)
{
  const char* sample = R"x("libraryfolders"
{
	// a comment
	"0"
	{
		"path"		"C:\\Program Files (x86)\\Steam"
		"label"		""
		"apps"
		{
			"228980"		"123"
		}
	}
	"1"
	{
		"path"		"D:\\SteamLibrary"
		"apps"
		{
			"22380"		"9000"
			"489830"		"1"
		}
	}
})x";
  auto libs = parseSteamLibraries(sample);
  CHECK(libs.size() == 2);
  CHECK(libs.size() == 2 && libs[1].path == "D:\\SteamLibrary");
  CHECK(steamLibraryForApp(libs, "22380") == std::optional<std::string>("D:\\SteamLibrary"));
  CHECK(!steamLibraryForApp(libs, "377160").has_value());

  // damaged input: no crash, nothing found
  CHECK(parseSteamLibraries(R"x("libraryfolders" { "0" { "path" "C:\\x)x").empty());
  CHECK(parseSteamLibraries("}}}}").empty());
  std::string deep = "\"a\"";
  for (int i = 0; i < 100; ++i) deep += " \"b\" {";
  CHECK(parseSteamLibraries(deep).empty());
  CHECK(parseSteamLibraries("").empty());

  if (argc > 1) {
    std::ifstream f(argv[1], std::ios::binary);
    std::stringstream s;
    s << f.rdbuf();
    const auto real = parseSteamLibraries(s.str());
    std::printf("real file: %zu libraries\n", real.size());
    for (const auto& l : real) {
      std::printf("  %s (%zu apps)\n", l.path.c_str(), l.apps.size());
    }
    for (const char* app : {"489830", "377160", "22380", "22370"}) {
      const auto p = steamLibraryForApp(real, app);
      std::printf("  app %s: %s\n", app, p ? p->c_str() : "-");
    }
  }

  std::printf(failures ? "%d failures\n" : "all passed\n", failures);
  return failures ? 1 : 0;
}
