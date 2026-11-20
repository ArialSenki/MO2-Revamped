// MO2 Revamped: the golden contract for plugins.txt and loadorder.txt (not shipped).
// From each golden case's order and active states, the framework must write
// exactly the bytes the current MO2 plugins wrote.
//   loadorderfiles_test <golden folder>   (golden/skyrimse: the cases are Skyrim SE's)
#include "loadorderfiles.h"

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <unordered_set>

using namespace mo2r::bethesda;
namespace fs = std::filesystem;

static std::string slurp(const fs::path& p)
{
  std::ifstream f(p, std::ios::binary);
  std::stringstream s;
  s << f.rdbuf();
  return s.str();
}

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
  if (argc < 2) {
    std::printf("usage: loadorderfiles_test <golden folder>\n");
    return 2;
  }
  const std::vector<std::wstring> primary = {L"Skyrim.esm", L"Update.esm", L"Dawnguard.esm",
                                             L"HearthFires.esm", L"Dragonborn.esm"};
  for (const auto& dir : fs::directory_iterator(argv[1])) {
    if (!dir.is_directory()) {
      continue;
    }
    const std::string pluginsTxt   = slurp(dir.path() / "plugins.txt");
    const std::string loadOrderTxt = slurp(dir.path() / "loadorder.txt");

    const auto names  = readLoadOrderTxt(loadOrderTxt);
    const auto states = readPluginsTxt(pluginsTxt, PluginsTxtStyle::Asterisk);
    std::vector<PluginState> order;
    for (const auto& name : names) {
      bool active = false;
      for (const auto& s : states) {
        if (sameName(s.name, name)) {
          active = s.active;
        }
      }
      for (const auto& p : primary) {
        if (sameName(p, name)) {
          active = true;
        }
      }
      order.push_back({name, active});
    }

    const auto written = writePluginsTxt(order, primary, PluginsTxtStyle::Asterisk);
    const bool samePlugins   = written.bytes == pluginsTxt;
    const bool sameLoadOrder = writeLoadOrderTxt(order) == loadOrderTxt;
    std::printf("%s: %zu plugins, plugins.txt %s, loadorder.txt %s\n",
                dir.path().filename().string().c_str(), order.size(),
                samePlugins ? "same" : "DIFFERENT", sameLoadOrder ? "same" : "DIFFERENT");
    CHECK(samePlugins);
    CHECK(sameLoadOrder);
    CHECK(written.unencodable.empty());
  }

  // reading: comments, blanks, '*' and duplicates
  const auto s = readPluginsTxt("# c\r\n*A.esp\r\n\r\nb.esp\r\n*a.ESP\r\n  *C.esl  \r\n",
                                PluginsTxtStyle::Asterisk);
  CHECK(s.size() == 3);
  CHECK(s.size() == 3 && s[0].name == L"A.esp" && s[0].active);
  CHECK(s.size() == 3 && s[1].name == L"b.esp" && !s[1].active);
  CHECK(s.size() == 3 && s[2].name == L"C.esl" && s[2].active);
  const auto a = readPluginsTxt("x.esp\ny.esp", PluginsTxtStyle::ActiveOnly);
  CHECK(a.size() == 2 && a[0].active && a[1].active);

  // file-time games: only the active ones, no marker
  const auto nv = writePluginsTxt({{L"FalloutNV.esm", true}, {L"Off.esp", false}, {L"On.esp", true}},
                                  {L"FalloutNV.esm"}, PluginsTxtStyle::ActiveOnly);
  CHECK(nv.bytes == std::string(MO2Header) + "FalloutNV.esm\r\nOn.esp\r\n");

  // a name outside the code page: an empty line and a report
  const auto bad = writePluginsTxt({{L"\x4E2D.esp", true}}, {}, PluginsTxtStyle::Asterisk);
  CHECK(bad.unencodable.size() == 1 && bad.bytes == std::string(MO2Header) + "\r\n");

  std::printf(failures ? "%d failures\n" : "all passed\n", failures);
  return failures ? 1 : 0;
}
