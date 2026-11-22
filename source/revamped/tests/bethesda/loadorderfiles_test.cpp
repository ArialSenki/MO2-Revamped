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

  // the order MO2 is given: primary, listed, then the rest by name; the rest
  // comes out the same whatever order the plugin list has them in
  {
    const std::vector<std::wstring> prim   = {L"Skyrim.esm", L"Update.esm"};
    const std::vector<std::wstring> listed = {L"update.ESM", L"B.esp", L"Gone.esp", L"b.esp",
                                              L"A.esp"};
    const std::vector<std::wstring> one = {L"zeta.esp", L"A.esp", L"Skyrim.esm", L"alpha.esp",
                                           L"B.esp", L"Beta.esl", L"Update.esm", L"Ñandú.esp"};
    std::vector<std::wstring> two(one.rbegin(), one.rend());
    const std::vector<std::wstring> expected = {L"Skyrim.esm", L"Update.esm", L"B.esp",
                                                L"Gone.esp",   L"A.esp",      L"alpha.esp",
                                                L"Beta.esl",   L"zeta.esp",   L"Ñandú.esp"};
    const auto first  = completeLoadOrder(prim, listed, one);
    const auto second = completeLoadOrder(prim, listed, two);
    CHECK(first == expected);
    CHECK(first == second);
    // nothing listed (no plugins.txt): primary, then all by name
    const auto none = completeLoadOrder(prim, {}, {L"c.esp", L"B.esp", L"a.esp"});
    CHECK((none ==std::vector<std::wstring>{L"Skyrim.esm", L"Update.esm", L"a.esp", L"B.esp",
                                          L"c.esp"}));
    CHECK(completeLoadOrder({}, {}, {}).empty());
  }

  // Skyrim SE: plugins.txt read back gives the order it was written from
  // (primary plugins left out of the file, put first again)
  {
    const std::vector<PluginState> order = {{L"Skyrim.esm", true},   {L"Update.esm", true},
                                            {L"Dawnguard.esm", true}, {L"HearthFires.esm", true},
                                            {L"Dragonborn.esm", true}, {L"ccFish.esm", true},
                                            {L"Master.esm", true},   {L"Off.esp", false},
                                            {L"On.esl", true}};
    std::vector<std::wstring> prim = primary;
    prim.push_back(L"ccFish.esm");
    const auto written = writePluginsTxt(order, prim, PluginsTxtStyle::Asterisk);
    CHECK(written.bytes == std::string(MO2Header) + "*Master.esm\r\nOff.esp\r\n*On.esl\r\n");
    std::vector<std::wstring> listed;
    for (const auto& entry : readPluginsTxt(written.bytes, PluginsTxtStyle::Asterisk)) {
      listed.push_back(entry.name);
    }
    std::vector<std::wstring> present;
    for (const auto& p : order) {
      present.push_back(p.name);
    }
    std::vector<std::wstring> names;
    for (const auto& p : order) {
      names.push_back(p.name);
    }
    CHECK(completeLoadOrder(prim, listed, present) == names);
  }

  std::printf(failures ? "%d failures\n" : "all passed\n", failures);
  return failures ? 1 : 0;
}
