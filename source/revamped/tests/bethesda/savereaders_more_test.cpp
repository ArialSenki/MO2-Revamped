// MO2 Revamped: tests for the Oblivion, Morrowind and Starfield save readers
// (not shipped). Build and run (Developer prompt, from this folder), with
// AddressSanitizer:
//   set B=..\..\games\bethesda
//   set Z=D:\mo2build\mo\build\zlib-v1.3.1
//   cl /nologo /std:c++20 /EHsc /W4 /MD /fsanitize=address /I %B% /I %Z%\include
//      savereaders_more_test.cpp %B%\saves\oblivionsave.cpp %B%\saves\morrowindsave.cpp
//      %B%\saves\starfieldsave.cpp %Z%\lib\zlibstatic.lib
//   savereaders_more_test [--oblivion save.ess] [--morrowind save.ess] [--starfield save.sfs]
// Synthetic saves are read whole, cut at every length and with random bytes
// changed; real saves given on the command line are read (never written) and
// printed, and one that can't be read fails the test.
// Fuzzing: the same files with /DMO2R_FUZZ and /fsanitize=fuzzer build a
// libFuzzer target over the three readers.
#include "saves/morrowindsave.h"
#include "saves/oblivionsave.h"
#include "saves/starfieldsave.h"

#include <zlib.h>

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iterator>
#include <random>
#include <string>
#include <vector>

using namespace mo2r::bethesda;
using Bytes = std::vector<std::byte>;

namespace
{

int failures = 0;

void check(bool ok, const char* what)
{
  if (!ok) {
    std::printf("FAIL: %s\n", what);
    ++failures;
  }
}

struct Writer
{
  Bytes b;
  template <typename T>
  Writer& v(T value)
  {
    const auto* p = reinterpret_cast<const std::byte*>(&value);
    b.insert(b.end(), p, p + sizeof(T));
    return *this;
  }
  Writer& raw(const std::string& s)
  {
    for (char c : s) {
      b.push_back(static_cast<std::byte>(c));
    }
    return *this;
  }
  Writer& raw(const Bytes& more)
  {
    b.insert(b.end(), more.begin(), more.end());
    return *this;
  }
  Writer& zeros(std::size_t n)
  {
    b.insert(b.end(), n, std::byte{0});
    return *this;
  }
  Writer& fixed(const std::string& s, std::size_t n)
  {
    return raw(s.substr(0, n)).zeros(n - std::min(s.size(), n));
  }
  Writer& bz(const std::string& s)  // one length byte, the bytes and a NUL
  {
    return v(static_cast<std::uint8_t>(s.size() + 1)).raw(s).zeros(1);
  }
  Writer& w(const std::string& s)  // wstring
  {
    return v(static_cast<std::uint16_t>(s.size())).raw(s);
  }
};

std::span<const std::byte> view(const Bytes& b)
{
  return {b.data(), b.size()};
}

Bytes systemTime(std::uint16_t y, std::uint16_t mo, std::uint16_t d, std::uint16_t h)
{
  Writer t;
  t.v(y).v(mo).v<std::uint16_t>(0).v(d).v(h).v<std::uint16_t>(5).v<std::uint16_t>(6)
      .v<std::uint16_t>(7);
  return t.b;
}

// --- Oblivion ---

Bytes oblivionSave(bool shot, const std::vector<std::string>& plugins)
{
  Writer f;
  f.raw("TES4SAVEGAME").v<std::uint8_t>(0).v<std::uint8_t>(125).raw(systemTime(2006, 3, 20, 1));
  f.v<std::uint32_t>(1).v<std::uint32_t>(300).v<std::uint32_t>(42).bz("Hero");
  f.v<std::uint16_t>(12).bz("Imperial City").v(12.5f).v<std::uint32_t>(9999);
  f.raw(systemTime(2024, 5, 6, 7));
  if (shot) {
    f.v<std::uint32_t>(8 + 4 * 3 * 3).v<std::uint32_t>(4).v<std::uint32_t>(3);
    for (int i = 0; i < 36; ++i) {
      f.v(static_cast<std::uint8_t>(i));
    }
  } else {
    f.v<std::uint32_t>(0);
  }
  f.v(static_cast<std::uint8_t>(plugins.size()));
  for (const auto& p : plugins) {
    f.bz(p);
  }
  return f.b;
}

bool readOblivion(std::span<const std::byte> bytes)
{
  const auto info = readOblivionSaveInfo(bytes);
  return info && readOblivionSaveData(bytes, *info);
}

void testOblivion()
{
  const std::vector<std::string> plugins = {"Oblivion.esm", "Knights.esp", "MyMod.esp"};
  const Bytes save = oblivionSave(true, plugins);
  const auto info  = readOblivionSaveInfo(view(save));
  check(info.has_value(), "oblivion: header");
  if (info) {
    check(info->saveNumber == 42 && info->playerName == "Hero" && info->playerLevel == 12 &&
              info->playerLocation == "Imperial City" && info->savedAt.year == 2024 &&
              info->savedAt.month == 5 && info->savedAt.day == 6 && info->savedAt.hour == 7,
          "oblivion: header fields");
    const auto data = readOblivionSaveData(view(save), *info);
    check(data && data->plugins == plugins && data->screenshotWidth == 4 &&
              data->screenshotHeight == 3 && data->screenshot.size() == 36,
          "oblivion: screenshot and plugins");
  }
  const Bytes noShot = oblivionSave(false, {"Oblivion.esm"});
  check(readOblivion(view(noShot)), "oblivion: no screenshot");
  // cut at every length: only the whole save reads
  for (std::size_t n = 0; n < save.size(); ++n) {
    check(!readOblivion(std::span(save.data(), n)), "oblivion: a cut save reads");
  }
  // a screenshot of a huge size is damage, not an allocation
  Bytes huge = save;
  const std::size_t sizeAt = info ? info->end + 4 : 0;
  std::memset(huge.data() + sizeAt, 0xFF, 8);
  check(!readOblivion(view(huge)), "oblivion: huge screenshot");
}

// --- Morrowind ---

Bytes morrowindSave(bool playerFirst)
{
  Writer f;
  f.raw("TES3").zeros(12).raw("HEDR").v<std::uint32_t>(300).v(1.3f).v<std::uint32_t>(32);
  f.zeros(32).fixed("My little save", 256).v<std::uint32_t>(1234);
  for (const std::string p : {"Morrowind.esm", "Tribunal.esm", "MyMod.esp"}) {
    f.raw("MAST").v(static_cast<std::uint32_t>(p.size() + 1)).raw(p).zeros(1);
    f.raw("DATA").v<std::uint32_t>(8).v<std::uint64_t>(123456);
  }
  f.raw("GMDT").v<std::uint32_t>(124).v(50.6f).v(100.0f).zeros(16);
  f.fixed("Balmora, Council Club", 64).v(3.6f).fixed("Nerevar", 32);
  f.zeros(28).raw("SCRS").v<std::uint32_t>(65536);
  for (int i = 0; i < 65536; ++i) {
    f.v(static_cast<std::uint8_t>(i * 13));
  }
  f.raw("GLOB").v<std::uint32_t>(4).zeros(8).v<std::uint32_t>(7);
  const auto npc = [&f](const std::string& name, std::uint16_t level) {
    const auto n = static_cast<std::uint32_t>(name.size() + 1);
    f.raw("NPC_").v<std::uint32_t>(4 + 4 + n + 4 + 4 + 3 + 4 + 4 + 12).zeros(8);
    f.raw("NAME").v(n).raw(name).zeros(1);
    f.raw("FNAM").v<std::uint32_t>(3).raw("abc");
    f.raw("NPDT").v<std::uint32_t>(12).v(level).zeros(10);
  };
  if (!playerFirst) {
    npc("guard", 3);
  }
  npc("player", 17);
  return f.b;
}

bool readMorrowind(std::span<const std::byte> bytes)
{
  const auto info = readMorrowindSaveInfo(bytes);
  if (!info) {
    return false;
  }
  const auto data = readMorrowindSaveData(bytes, *info);
  return data && data->playerLevel;
}

void testMorrowind()
{
  for (const bool first : {true, false}) {
    const Bytes save = morrowindSave(first);
    const auto info  = readMorrowindSaveInfo(view(save));
    check(info.has_value(), "morrowind: header");
    if (!info) {
      continue;
    }
    check(info->saveName == "My little save" && info->playerName == "Nerevar" &&
              info->playerLocation == "Balmora, Council Club" &&
              info->plugins == std::vector<std::string>{"Morrowind.esm", "Tribunal.esm",
                                                        "MyMod.esp"} &&
              info->playerHealth > 50.5f && info->gameDays > 3.5f,
          "morrowind: header fields");
    const auto data = readMorrowindSaveData(view(save), *info);
    check(data && data->screenshot.size() == 65536 && data->playerLevel == 17,
          "morrowind: screenshot and level");
    // (the NPDT's 10 bytes after the level aren't read)
    for (std::size_t n = 0; n < save.size() - 10; n += (n < 2000 ? 1 : 97)) {
      check(!readMorrowind(std::span(save.data(), n)), "morrowind: a cut save reads");
    }
  }
  // a record size that points past the end
  Bytes bad = morrowindSave(true);
  const char glob[] = "GLOB";
  for (std::size_t i = 0; i + 8 < bad.size(); ++i) {
    if (std::memcmp(bad.data() + i, glob, 4) == 0) {
      std::memset(bad.data() + i + 4, 0xFF, 4);
      break;
    }
  }
  const auto info = readMorrowindSaveInfo(view(bad));
  check(info && readMorrowindSaveData(view(bad), *info) &&
            !readMorrowindSaveData(view(bad), *info)->playerLevel,
        "morrowind: a huge record size");
}

// --- Starfield ---

Bytes zlib(const Bytes& in)
{
  uLongf size = compressBound(static_cast<uLong>(in.size()));
  Bytes out(size);
  compress2(reinterpret_cast<Bytef*>(out.data()), &size,
            reinterpret_cast<const Bytef*>(in.data()), static_cast<uLong>(in.size()), 6);
  out.resize(size);
  return out;
}

Bytes starfieldBody(std::uint8_t saveVersion)
{
  const auto extra = [saveVersion](Writer& b) {
    if (saveVersion >= StarfieldCreationDataVersion) {
      b.w("A Creation").w("SFBGS00X").v<std::uint16_t>(2).v<std::uint16_t>(0x0101)
          .v<std::uint8_t>(1);
    }
  };
  Writer d;
  d.raw("SFS_SAVEGAME").v<std::uint32_t>(200).v<std::uint32_t>(1).v(saveVersion);
  d.v<std::uint32_t>(77).w("Spacer").v<std::uint32_t>(31).w("New Atlantis").w("12.34.56");
  d.w("HumanRace").v<std::uint16_t>(1).v(1.0f).v(2.0f).v<std::uint64_t>(133500000000000000ULL);
  d.zeros(12).v<std::uint8_t>(109).w("1.14.70.0").w("1.14.70.0").v<std::uint32_t>(500);
  d.v<std::uint8_t>(3).w("Starfield.esm").w("MyMod.esm");
  extra(d);
  d.w("Constellation.esm");
  d.v<std::uint16_t>(1).w("Light.esm");
  extra(d);
  if (saveVersion >= StarfieldCreationDataVersion) {
    d.v<std::uint32_t>(1).w("Medium.esm");
    extra(d);
  }
  d.zeros(300000);  // the change forms: a second and third chunk
  return d.b;
}

// the body in 250 KiB chunks, each zlib stream on a 16-byte boundary
Bytes starfieldFile(const Bytes& body, std::size_t chunk = 250 * 1024)
{
  Writer f;
  f.raw("BCPS").v<std::uint32_t>(1).v<std::uint64_t>(0).v<std::uint64_t>(0);
  f.v<std::uint64_t>(48).v(static_cast<std::uint64_t>(body.size())).zeros(8);
  for (std::size_t at = 0; at < body.size(); at += chunk) {
    while (f.b.size() % 16 != 0) {
      f.zeros(1);
    }
    f.raw(zlib(Bytes(body.begin() + static_cast<std::ptrdiff_t>(at),
                     body.begin() + static_cast<std::ptrdiff_t>(std::min(body.size(), at + chunk)))));
  }
  return f.b;
}

bool isCore(std::string_view name)
{
  return name == "Starfield.esm" || name == "Constellation.esm";
}

std::optional<StarfieldSaveData> readStarfield(std::span<const std::byte> file,
                                               std::size_t limit = 1024 * 1024)
{
  const auto container = readStarfieldContainer(file);
  if (!container) {
    return std::nullopt;
  }
  Bytes body(std::min<std::size_t>(limit, container->uncompressedSize));
  const auto n = inflateStarfieldData(file, *container, body);
  if (!n) {
    return std::nullopt;
  }
  body.resize(*n);
  const auto info = readStarfieldSaveInfo(view(body));
  if (!info) {
    return std::nullopt;
  }
  return readStarfieldSaveData(view(body), *info, &isCore);
}

void testStarfield()
{
  for (const std::uint8_t version : {std::uint8_t{110}, std::uint8_t{122}}) {
    const Bytes body = starfieldBody(version);
    // chunks of 40 bytes: the header itself spans several
    for (const std::size_t chunk : {std::size_t{40}, std::size_t{250 * 1024}}) {
      const Bytes file = starfieldFile(body, chunk);
      const auto data  = readStarfield(view(file));
      check(data.has_value(), "starfield: lists");
      if (!data) {
        continue;
      }
      check(data->plugins == std::vector<std::string>{"Starfield.esm", "MyMod.esm",
                                                      "Constellation.esm"} &&
                data->lightPlugins == std::vector<std::string>{"Light.esm"} &&
                data->gameVersion == "1.14.70.0",
            "starfield: plugins");
      check(version >= 122 ? data->mediumPlugins == std::vector<std::string>{"Medium.esm"}
                           : data->mediumPlugins.empty(),
            "starfield: medium plugins");
      // decoded only as far as asked
      Bytes start(64);
      const auto container = readStarfieldContainer(view(file));
      check(container && inflateStarfieldData(view(file), *container, start) == 64,
            "starfield: a prefix");
      if (chunk == 40) {
        for (std::size_t n = 0; n < 600; ++n) {
          (void)readStarfield(std::span(file.data(), n));
        }
      } else {
        for (std::size_t n = 0; n < file.size(); n += 7) {
          (void)readStarfield(std::span(file.data(), n));
        }
      }
    }
  }
  // the header's size can't ask for more than 4 GB, nor start inside it
  Bytes bad = starfieldFile(starfieldBody(122));
  std::memset(bad.data() + 32, 0xFF, 8);
  check(!readStarfieldContainer(view(bad)), "starfield: huge size");
  bad = starfieldFile(starfieldBody(122));
  std::memset(bad.data() + 24, 0, 8);
  check(!readStarfieldContainer(view(bad)), "starfield: data inside the header");
}

// --- random damage, under ASan: no crash, no read outside the buffer ---

void mutate(const Bytes& good, bool (*read)(std::span<const std::byte>), int rounds,
            std::uint32_t seed)
{
  std::mt19937 rng(seed);
  for (int i = 0; i < rounds; ++i) {
    Bytes b = good;
    const int changes = 1 + static_cast<int>(rng() % 8);
    for (int c = 0; c < changes; ++c) {
      b[rng() % b.size()] = static_cast<std::byte>(rng());
    }
    if (rng() % 4 == 0) {
      b.resize(rng() % b.size());
    }
    (void)read(view(b));
  }
}

bool readStarfieldBool(std::span<const std::byte> b)
{
  return readStarfield(b, 64 * 1024).has_value();
}

Bytes readFile(const char* path)
{
  std::ifstream in(path, std::ios::binary);
  std::vector<char> c((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
  Bytes b(c.size());
  std::memcpy(b.data(), c.data(), c.size());
  return b;
}

}  // namespace

#ifdef MO2R_FUZZ
extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size)
{
  const auto b = std::as_bytes(std::span(data, size));
  (void)readOblivion(b);
  (void)readMorrowind(b);
  (void)readStarfield(b, 64 * 1024);
  return 0;
}
#else
int main(int argc, char** argv)
{
  if (argc == 3 && std::string(argv[1]) == "--write-seeds") {
    // a corpus for the fuzzer: one save of each (Starfield's in small chunks)
    const std::pair<const char*, Bytes> seeds[] = {
        {"oblivion.ess", oblivionSave(true, {"Oblivion.esm", "MyMod.esp"})},
        {"morrowind.ess", morrowindSave(false)},
        {"starfield.sfs", starfieldFile(starfieldBody(122), 64)}};
    for (const auto& [name, bytes] : seeds) {
      std::ofstream out(std::string(argv[2]) + "/" + name, std::ios::binary);
      out.write(reinterpret_cast<const char*>(bytes.data()),
                static_cast<std::streamsize>(std::min<std::size_t>(bytes.size(), 70000)));
    }
    return 0;
  }
  testOblivion();
  testMorrowind();
  testStarfield();
  mutate(oblivionSave(true, {"Oblivion.esm", "MyMod.esp"}), &readOblivion, 20000, 1);
  mutate(morrowindSave(false), &readMorrowind, 3000, 2);
  mutate(starfieldFile(starfieldBody(122), 64), &readStarfieldBool, 5000, 3);

  for (int i = 1; i + 1 < argc; i += 2) {
    const std::string kind = argv[i];
    const Bytes save       = readFile(argv[i + 1]);
    if (kind == "--oblivion") {
      const auto info = readOblivionSaveInfo(view(save));
      const auto data = info ? readOblivionSaveData(view(save), *info) : std::nullopt;
      check(data.has_value(), "real oblivion save");
      if (data) {
        std::printf("%s: %s, level %u, %s, %zu plugins, %ux%u\n", argv[i + 1],
                    info->playerName.c_str(), info->playerLevel, info->playerLocation.c_str(),
                    data->plugins.size(), data->screenshotWidth, data->screenshotHeight);
      }
    } else if (kind == "--morrowind") {
      const auto info = readMorrowindSaveInfo(view(save));
      const auto data = info ? readMorrowindSaveData(view(save), *info) : std::nullopt;
      check(data && data->playerLevel, "real morrowind save");
      if (data && data->playerLevel) {
        std::printf("%s: %s, level %u, %s, %zu plugins\n", argv[i + 1],
                    info->playerName.c_str(), *data->playerLevel, info->playerLocation.c_str(),
                    info->plugins.size());
      }
    } else if (kind == "--starfield") {
      const auto data = readStarfield(view(save), 16 * 1024 * 1024);
      check(data.has_value(), "real starfield save");
      if (data) {
        std::printf("%s: %zu plugins, %zu light, %zu medium\n", argv[i + 1],
                    data->plugins.size(), data->lightPlugins.size(), data->mediumPlugins.size());
      }
    }
  }
  std::printf(failures == 0 ? "all passed\n" : "%d failed\n", failures);
  return failures == 0 ? 0 : 1;
}
#endif
