// MO2 Revamped: tests for the Bethesda save readers (not shipped).
// Build and run (Developer prompt, from this folder), with AddressSanitizer:
//   set B=..\..\games\bethesda
//   set Z=D:\mo2build\mo\build\zlib-v1.3.1
//   cl /nologo /std:c++20 /EHsc /W4 /MD /fsanitize=address /I %B% /I %Z%\include savereaders_test.cpp
//      %B%\saves\lz4block.cpp %B%\saves\zlibprefix.cpp %B%\saves\skyrimsave.cpp
//      %B%\saves\fallout4save.cpp %B%\saves\fallout3save.cpp %Z%\lib\zlibstatic.lib
//   savereaders_test [--fo3 save.fos] [--fnv save.fos] [--fo4 save.fos] [--sse save.ess] ...
// Real saves given on the command line are read (never written) and printed;
// one that can't be read fails the test.
// /DMO2R_LZ4_ORACLE (and liblz4.lib, its DLL next to the exe) also checks the
// LZ4 decoder against liblz4's LZ4_decompress_safe_partial.
// Fuzzing: the same files with /DMO2R_FUZZ and /fsanitize=fuzzer build a
// libFuzzer target over every reader; `savereaders_test --write-seeds <dir>`
// writes it a corpus to start from.
#include "saves/fallout3save.h"
#include "saves/fallout4save.h"
#include "saves/lz4block.h"
#include "saves/skyrimsave.h"
#include "saves/zlibprefix.h"

#include <zlib.h>

#ifdef MO2R_LZ4_ORACLE
#include "D:/mo2build/mo/build/lz4-v1.9.4/lib/lz4.h"
#endif

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iterator>
#include <random>
#include <string>
#include <vector>

using namespace mo2r::bethesda;
using Bytes = std::vector<std::byte>;

static std::span<const std::byte> view(const Bytes& b)
{
  return {b.data(), b.size()};
}

// --- an LZ4 block encoder for the tests (greedy, a small window) ---

static void lz4Length(Bytes& out, std::size_t v)
{
  while (v >= 255) {
    out.push_back(std::byte{255});
    v -= 255;
  }
  out.push_back(static_cast<std::byte>(v));
}

static void lz4Sequence(Bytes& out, const Bytes& src, std::size_t anchor, std::size_t literals,
                        std::size_t offset, std::size_t match)
{
  const std::size_t m = match == 0 ? 0 : match - 4;
  out.push_back(static_cast<std::byte>((std::min<std::size_t>(literals, 15) << 4) |
                                       (match == 0 ? 0 : std::min<std::size_t>(m, 15))));
  if (literals >= 15) {
    lz4Length(out, literals - 15);
  }
  out.insert(out.end(), src.begin() + static_cast<std::ptrdiff_t>(anchor),
             src.begin() + static_cast<std::ptrdiff_t>(anchor + literals));
  if (match == 0) {
    return;  // the last sequence
  }
  out.push_back(static_cast<std::byte>(offset & 0xFF));
  out.push_back(static_cast<std::byte>(offset >> 8));
  if (m >= 15) {
    lz4Length(out, m - 15);
  }
}

// follows the format's end rules (the last 5 bytes are literals, no match
// starts in the last 12), so liblz4 accepts what it writes
static Bytes lz4Compress(const Bytes& src, std::size_t window = 64)
{
  Bytes out;
  const std::size_t n       = src.size();
  const std::size_t mflimit = n >= 12 ? n - 12 : 0;
  std::size_t anchor = 0;
  std::size_t i      = 0;
  while (i < mflimit) {
    std::size_t bestLen = 0;
    std::size_t bestOff = 0;
    for (std::size_t j = i > window ? i - window : 0; j < i; ++j) {
      std::size_t l = 0;
      while (i + l < n - 5 && src[j + l] == src[i + l]) {
        ++l;
      }
      if (l > bestLen) {
        bestLen = l;
        bestOff = i - j;
      }
    }
    if (bestLen < 4) {
      ++i;
      continue;
    }
    lz4Sequence(out, src, anchor, i - anchor, bestOff, bestLen);
    i += bestLen;
    anchor = i;
  }
  lz4Sequence(out, src, anchor, n - anchor, 0, 0);
  return out;
}

static Bytes zlibCompress(const Bytes& src)
{
  uLongf size = compressBound(static_cast<uLong>(src.size()));
  Bytes out(size);
  compress2(reinterpret_cast<Bytef*>(out.data()), &size,
            reinterpret_cast<const Bytef*>(src.data()), static_cast<uLong>(src.size()), 6);
  out.resize(size);
  return out;
}

// --- a writer for synthetic saves ---

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
  Writer& w(const std::string& s)  // wstring
  {
    v(static_cast<std::uint16_t>(s.size()));
    return raw(s);
  }
  Writer& f3(const std::string& s)  // .fos string: length, '|', bytes, '|'
  {
    v(static_cast<std::uint16_t>(s.size())).raw("|").raw(s);
    return raw("|");
  }
};

static Bytes pixels(std::size_t n, std::uint8_t seed)
{
  Bytes p(n);
  for (std::size_t i = 0; i < n; ++i) {
    p[i] = static_cast<std::byte>(seed + i * 7);
  }
  return p;
}

static Bytes randomBytes(std::size_t n, std::uint32_t seed, int alphabet = 256)
{
  std::mt19937 rng(seed);
  Bytes p(n);
  for (auto& x : p) {
    x = static_cast<std::byte>(rng() % static_cast<unsigned>(alphabet));
  }
  return p;
}

static const std::vector<std::string> SsePlugins = {"Skyrim.esm", "Update.esm", "Dawnguard.esm",
                                                    "Unofficial Skyrim Special Edition Patch.esp"};
static const std::vector<std::string> SseLight   = {"ccBGSSSE001-Fish.esm", "Light.esp"};

[[maybe_unused]] static Bytes skyrimBody(std::uint8_t formVersion, const std::vector<std::string>& plugins,
                        const std::vector<std::string>& light, std::size_t padding = 64)
{
  Writer lists;
  lists.v(static_cast<std::uint8_t>(plugins.size()));
  for (const auto& p : plugins) {
    lists.w(p);
  }
  if (formVersion >= 78) {
    lists.v(static_cast<std::uint16_t>(light.size()));
    for (const auto& p : light) {
      lists.w(p);
    }
  }
  Writer body;
  body.v(formVersion).v(static_cast<std::uint32_t>(lists.b.size())).raw(lists.b);
  body.raw(randomBytes(padding, 7, 4));  // the change forms that follow
  return body.b;
}

// compression: 0 none, 1 zlib, 2 LZ4; version 9 is Skyrim 2011's (RGB, no field)
[[maybe_unused]] static Bytes skyrimSave(std::uint32_t version, std::uint16_t compression, const Bytes& body,
                        std::uint32_t width = 4, std::uint32_t height = 3)
{
  Writer h;
  h.v(version).v(std::uint32_t{42}).w("Lydia").v(std::uint32_t{31}).w("Whiterun")
      .w("Morndas, 17th of Last Seed, 4E 201").w("NordRace").v(std::uint16_t{1})
      .v(1.5f).v(2.5f).v(std::uint64_t{133500000000000000ULL}).v(width).v(height);
  if (version >= 12) {
    h.v(compression);
  }
  Writer f;
  f.raw("TESV_SAVEGAME").v(static_cast<std::uint32_t>(h.b.size())).raw(h.b);
  const std::size_t bpp = version >= 12 ? 4 : 3;
  f.raw(pixels(std::size_t{width} * height * bpp, 3));
  if (compression == 0 || version < 12) {
    f.raw(body);
  } else {
    const Bytes packed = compression == 2 ? lz4Compress(body) : zlibCompress(body);
    f.v(static_cast<std::uint32_t>(body.size())).v(static_cast<std::uint32_t>(packed.size()));
    f.raw(packed);
  }
  return f.b;
}

[[maybe_unused]] static std::optional<SkyrimSaveData> readSkyrim(const Bytes& file, std::size_t limit = 1 << 20)
{
  const auto info = readSkyrimSaveInfo(view(file));
  if (!info) {
    return std::nullopt;
  }
  const auto layout = readSkyrimBodyLayout(view(file), *info);
  if (!layout) {
    return std::nullopt;
  }
  return readSkyrimSaveData(view(file), *info, *layout, limit);
}

static const std::vector<std::string> Fo4Plugins = {"Fallout4.esm", "DLCRobot.esm",
                                                    "Unofficial Fallout 4 Patch.esp"};
static const std::vector<std::string> Fo4Light   = {"ccBGSFO4044-HellfirePowerArmor.esl"};

[[maybe_unused]] static Bytes fallout4Save(std::uint8_t formVersion, std::uint32_t width = 5,
                          std::uint32_t height = 2, std::size_t lightCount = 1)
{
  Writer h;
  h.v(std::uint32_t{15}).v(std::uint32_t{7}).w("Nora").v(std::uint32_t{12})
      .w("Sanctuary Hills").w("0d.1h.2m.0 days.1 hours.2 minutes").w("HumanRace")
      .v(std::uint16_t{1}).v(0.0f).v(200.0f).v(std::uint64_t{134393902806640000ULL})
      .v(width).v(height);
  Writer lists;
  lists.v(static_cast<std::uint8_t>(Fo4Plugins.size()));
  for (const auto& p : Fo4Plugins) {
    lists.w(p);
  }
  if (formVersion >= 68) {
    lists.v(static_cast<std::uint16_t>(lightCount));
    for (std::size_t i = 0; i < lightCount && i < Fo4Light.size(); ++i) {
      lists.w(Fo4Light[i]);
    }
  }
  Writer f;
  f.raw("FO4_SAVEGAME").v(static_cast<std::uint32_t>(h.b.size())).raw(h.b);
  f.raw(pixels(std::size_t{width} * height * 4, 9));
  f.v(formVersion).w("1.10.163.0").v(static_cast<std::uint32_t>(lists.b.size())).raw(lists.b);
  f.raw(randomBytes(32, 3));
  return f.b;
}

[[maybe_unused]] static Bytes fallout3Save(bool newVegas, std::uint32_t width = 3, std::uint32_t height = 2)
{
  Writer f;
  f.raw("FO3SAVEGAME").v(std::uint32_t{0x53}).v(std::uint32_t{0x30}).raw("|");
  if (newVegas) {
    f.raw("ENGLISH").raw(Bytes(57)).raw("|");
  }
  f.v(width).raw("|").v(height).raw("|").v(std::uint32_t{3}).raw("|");
  f.f3("Courier").f3("Drifter").v(std::int32_t{5}).raw("|").f3("Goodsprings");
  f.f3("000.12.03");
  f.raw(pixels(std::size_t{width} * height * 3, 1));
  f.raw("|").v(std::uint32_t{40});  // a separator and the plugin list's size
  f.v(std::uint8_t{2}).raw("|");
  f.f3("FalloutNV.esm").f3("DeadMoney.esm");
  return f.b;
}

#ifdef MO2R_FUZZ

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size)
{
  if (size < 3) {
    return 0;
  }
  const std::span<const std::byte> in(reinterpret_cast<const std::byte*>(data + 3), size - 3);
  const std::size_t outSize = (std::size_t{data[1]} | (std::size_t{data[2]} << 8)) * 4;
  switch (data[0] % 6) {
  case 0: {
    Bytes out(outSize);
    const auto n = decodeLz4Prefix(in, out);
    if (n && *n > out.size()) {
      std::abort();
    }
#ifdef MO2R_LZ4_ORACLE
    if (!out.empty()) {
      Bytes ref(out.size());
      const int r = LZ4_decompress_safe_partial(
          reinterpret_cast<const char*>(in.data()), reinterpret_cast<char*>(ref.data()),
          static_cast<int>(in.size()), static_cast<int>(ref.size()),
          static_cast<int>(ref.size()));
      // what both decode is the same. They part only on damage: ours refuses
      // an offset of 0 (the format forbids it, liblz4 lets it through),
      // gives the start of a cut block where liblz4 gives an error, and
      // decodes a block that ends in a match (against the format's end
      // rules), where liblz4 stops earlier; valid blocks give the same size
      // (the unit tests check that)
      const std::size_t common = n ? std::min(*n, static_cast<std::size_t>(r)) : 0;
      if (r >= 0 && n && std::memcmp(out.data(), ref.data(), common) != 0) {
        std::abort();
      }
    }
#endif
    break;
  }
  case 1: {
    Bytes out(outSize);
    const auto n = inflatePrefix(in, out);
    if (n && *n > out.size()) {
      std::abort();
    }
    break;
  }
  case 2:
    if (const auto info = readSkyrimSaveInfo(in)) {
      if (const auto layout = readSkyrimBodyLayout(in, *info)) {
        (void)skyrimSaveDataBound(*info, *layout, 1 << 20);
        (void)readSkyrimSaveData(in, *info, *layout, 64 * 1024);
      }
    }
    break;
  case 3:
    (void)readSkyrimBody(in);
    break;
  case 4:
    if (const auto info = readFallout4SaveInfo(in)) {
      (void)readFallout4SaveData(in, *info);
    }
    break;
  case 5:
    for (const auto layout : {Fallout3Layout::Fallout3, Fallout3Layout::NewVegas}) {
      if (const auto info = readFallout3SaveInfo(in, layout)) {
        (void)readFallout3SaveData(in, *info);
      }
    }
    break;
  }
  return 0;
}

#else

static int failures = 0;
#define CHECK(x)                                                                \
  do {                                                                          \
    if (!(x)) {                                                                 \
      std::printf("FAIL line %d: %s\n", __LINE__, #x);                          \
      ++failures;                                                               \
    }                                                                           \
  } while (0)

static bool isPrefix(const Bytes& whole, std::span<const std::byte> part)
{
  return part.size() <= whole.size() && std::equal(part.begin(), part.end(), whole.begin());
}

static void testLz4()
{
  const std::vector<Bytes> inputs = {
      Bytes{},
      randomBytes(1, 1),
      randomBytes(13, 2),
      randomBytes(5000, 3),        // incompressible
      randomBytes(20000, 4, 3),    // three symbols: many matches
      Bytes(70000, std::byte{7}),  // one byte: overlapping matches, long lengths
  };
  for (const Bytes& src : inputs) {
    const Bytes packed = lz4Compress(src);
    // whole
    Bytes out(src.size());
    auto n = decodeLz4Prefix(view(packed), out);
    CHECK(n && *n == src.size() && out == src);
    // every prefix size up to 300 bytes, and some larger: the first bytes,
    // never a byte more
    for (std::size_t target : {std::size_t{0}, std::size_t{1}, std::size_t{4},
                               std::size_t{15}, std::size_t{16}, std::size_t{299},
                               src.size() / 2, src.size()}) {
      target = std::min(target, src.size());
      Bytes part(target);
      n = decodeLz4Prefix(view(packed), part);
      CHECK(n && *n == target && isPrefix(src, part));
    }
    // a larger output than the block: only what the block holds
    Bytes big(src.size() + 100);
    n = decodeLz4Prefix(view(packed), big);
    CHECK(n && *n == src.size());
    // every truncation of the input: a prefix of the output, no read past it
    const std::size_t step = std::max<std::size_t>(1, packed.size() / 400);
    for (std::size_t cut = 0; cut < packed.size(); cut += step) {
      const Bytes in(packed.begin(), packed.begin() + static_cast<std::ptrdiff_t>(cut));
      Bytes o(src.size());
      n = decodeLz4Prefix(view(in), o);
      CHECK(n && isPrefix(src, std::span(o).first(*n)));
    }
#ifdef MO2R_LZ4_ORACLE
    Bytes ref(src.size() + 1);
    CHECK(LZ4_decompress_safe(reinterpret_cast<const char*>(packed.data()),
                              reinterpret_cast<char*>(ref.data()),
                              static_cast<int>(packed.size()),
                              static_cast<int>(ref.size())) == static_cast<int>(src.size()));
#endif
  }
#ifdef MO2R_LZ4_ORACLE
  // liblz4's own blocks decode the same
  for (const Bytes& src : inputs) {
    Bytes packed(static_cast<std::size_t>(LZ4_compressBound(static_cast<int>(src.size()))));
    const int size = LZ4_compress_default(reinterpret_cast<const char*>(src.data()),
                                          reinterpret_cast<char*>(packed.data()),
                                          static_cast<int>(src.size()),
                                          static_cast<int>(packed.size()));
    packed.resize(static_cast<std::size_t>(size));
    Bytes out(src.size());
    const auto n = decodeLz4Prefix(view(packed), out);
    CHECK(n && *n == src.size() && out == src);
  }
#endif

  // damage: a match before the output's start, an offset of 0
  {
    const Bytes before = {std::byte{0x10}, std::byte{'a'}, std::byte{2}, std::byte{0}};
    Bytes o(64);
    CHECK(!decodeLz4Prefix(view(before), o));
    const Bytes zero = {std::byte{0x10}, std::byte{'a'}, std::byte{0}, std::byte{0}};
    CHECK(!decodeLz4Prefix(view(zero), o));
    // at the very start
    const Bytes first = {std::byte{0x00}, std::byte{1}, std::byte{0}};
    CHECK(!decodeLz4Prefix(view(first), o));
  }
  // a literal length of billions, cut short: what's there, nothing more
  {
    Bytes huge = {std::byte{0xF0}};
    huge.insert(huge.end(), 10000, std::byte{255});
    Bytes o(64);
    const auto n = decodeLz4Prefix(view(huge), o);
    CHECK(n && *n == 0);
    // a length that would overflow size_t if trusted
    Bytes overflow = {std::byte{0xF0}};
    overflow.insert(overflow.end(), 100000, std::byte{255});
    overflow.push_back(std::byte{'x'});
    CHECK(decodeLz4Prefix(view(overflow), o).has_value());
  }
  // a match length of millions into a small output: filled, then stop
  {
    Bytes longMatch = {std::byte{0x1F}, std::byte{'z'}, std::byte{1}, std::byte{0}};
    longMatch.insert(longMatch.end(), 5000, std::byte{255});
    longMatch.push_back(std::byte{0});
    Bytes o(1000);
    const auto n = decodeLz4Prefix(view(longMatch), o);
    CHECK(n && *n == 1000 && o[999] == std::byte{'z'});
  }
}

static void testZlib()
{
  const Bytes src    = randomBytes(300000, 5, 5);
  const Bytes packed = zlibCompress(src);
  Bytes out(src.size());
  auto n = inflatePrefix(view(packed), out);
  CHECK(n && *n == src.size() && out == src);
  Bytes part(1000);
  n = inflatePrefix(view(packed), part);
  CHECK(n && *n == 1000 && isPrefix(src, part));
  for (std::size_t cut = 0; cut < packed.size(); cut += packed.size() / 50 + 1) {
    const Bytes in(packed.begin(), packed.begin() + static_cast<std::ptrdiff_t>(cut));
    Bytes o(src.size());
    n = inflatePrefix(view(in), o);
    CHECK(!n || isPrefix(src, std::span(o).first(*n)));
  }
  // not zlib at all
  const Bytes junk = randomBytes(100, 9);
  Bytes o(100);
  CHECK(!inflatePrefix(view(junk), o).has_value());
}

static void testSkyrim()
{
  const Bytes body = skyrimBody(78, SsePlugins, SseLight);
  for (const std::uint16_t compression : {std::uint16_t{0}, std::uint16_t{1}, std::uint16_t{2}}) {
    const Bytes file = skyrimSave(12, compression, body);
    const auto info  = readSkyrimSaveInfo(view(file));
    CHECK(info.has_value());
    if (!info) {
      continue;
    }
    CHECK(info->version == 12 && info->saveNumber == 42 && info->playerName == "Lydia");
    CHECK(info->playerLevel == 31 && info->playerLocation == "Whiterun");
    CHECK(info->playerRace == "NordRace" && info->fileTime == 133500000000000000ULL);
    CHECK(info->screenshotWidth == 4 && info->screenshotHeight == 3 && info->alpha());
    CHECK(static_cast<std::uint16_t>(info->compression) == compression);
    const auto data = readSkyrim(file);
    CHECK(data && data->formVersion == 78 && data->plugins == SsePlugins &&
          data->lightPlugins == SseLight && data->screenshot.size() == 4 * 3 * 4);
    // a limit too small for the lists: can't read, no guess
    CHECK(!readSkyrim(file, 20).has_value());
    // every truncation fails cleanly (or still reads, if only padding went)
    for (std::size_t cut = 0; cut < file.size(); ++cut) {
      const Bytes c(file.begin(), file.begin() + static_cast<std::ptrdiff_t>(cut));
      const auto d = readSkyrim(c);
      CHECK(!d || (d->plugins == SsePlugins && d->lightPlugins == SseLight));
    }
  }

  // a big body read only as far as asked: 1 MB of a 6 MB body
  {
    const Bytes bigBody = skyrimBody(78, SsePlugins, SseLight, 6 << 20);
    Bytes file          = skyrimSave(12, 2, bigBody);
    const auto info     = readSkyrimSaveInfo(view(file));
    const auto layout   = readSkyrimBodyLayout(view(file), *info);
    CHECK(layout && layout->uncompressedSize == bigBody.size());
    const std::size_t bound = skyrimSaveDataBound(*info, *layout, 1 << 20);
    CHECK(bound < file.size());
    const Bytes start(file.begin(), file.begin() + static_cast<std::ptrdiff_t>(bound));
    const auto data = readSkyrimSaveData(view(start), *info, *layout, 1 << 20);
    CHECK(data && data->plugins == SsePlugins);
  }

  // Skyrim 2011 (version 9): RGB, no compression field, the body as is
  {
    const Bytes file = skyrimSave(9, 0, skyrimBody(74, SsePlugins, {}));
    const auto info  = readSkyrimSaveInfo(view(file));
    CHECK(info && !info->alpha() && info->compression == SkyrimCompression::None);
    const auto data = readSkyrim(file);
    CHECK(data && data->plugins == SsePlugins && data->lightPlugins.empty() &&
          data->screenshot.size() == 4 * 3 * 3);
  }
  // before form version 78: no light list
  {
    const auto data = readSkyrim(skyrimSave(12, 2, skyrimBody(77, SsePlugins, SseLight)));
    CHECK(data && data->plugins == SsePlugins && data->lightPlugins.empty());
  }
  // no screenshot
  {
    const auto data = readSkyrim(skyrimSave(12, 2, body, 0, 0));
    CHECK(data && data->screenshot.empty() && data->plugins == SsePlugins);
  }

  // damage
  {
    Bytes file = skyrimSave(12, 2, body);
    file[0]    = std::byte{'X'};
    CHECK(!readSkyrimSaveInfo(view(file)).has_value());
  }
  {
    // a screenshot side of 9000: no body to be found
    const Bytes file = skyrimSave(12, 2, body, 9000, 1);
    const auto info  = readSkyrimSaveInfo(view(file));
    CHECK(info && !readSkyrimBodyLayout(view(file), *info).has_value());
  }
  {
    // compression 3
    Bytes file      = skyrimSave(12, 0, body);
    const auto info = readSkyrimSaveInfo(view(file));
    const std::size_t field = info->screenshotOffset - 2;
    file[field]     = std::byte{3};
    const auto bad  = readSkyrimSaveInfo(view(file));
    CHECK(bad && !readSkyrimBodyLayout(view(file), *bad).has_value());
  }
  {
    // sizes beyond any save
    Bytes file             = skyrimSave(12, 2, body);
    const auto info        = readSkyrimSaveInfo(view(file));
    const std::size_t at   = info->screenshotEnd();
    const std::uint32_t xl = 0xFFFFFFF0u;
    std::memcpy(file.data() + at, &xl, 4);
    CHECK(!readSkyrimBodyLayout(view(file), *info).has_value());
  }
  {
    // a header size beyond any header
    Bytes file             = skyrimSave(12, 2, body);
    const std::uint32_t xl = 0x7FFFFFFFu;
    std::memcpy(file.data() + 13, &xl, 4);
    CHECK(!readSkyrimSaveInfo(view(file)).has_value());
  }
  {
    // a light plugin count of 5000 (the engine's limit is 4096)
    Bytes damaged = skyrimBody(78, SsePlugins, SseLight);
    std::size_t at = 1 + 4 + 1;
    for (const auto& p : SsePlugins) {
      at += 2 + p.size();
    }
    const std::uint16_t many = 5000;
    std::memcpy(damaged.data() + at, &many, 2);
    CHECK(!readSkyrim(skyrimSave(12, 2, damaged)).has_value());
    CHECK(!readSkyrimBody(view(damaged)).has_value());
  }
  {
    // a plugin name longer than the body
    Bytes damaged            = skyrimBody(78, SsePlugins, SseLight, 0);
    const std::uint16_t long_ = 0xFFFF;
    std::memcpy(damaged.data() + 6, &long_, 2);
    CHECK(!readSkyrimBody(view(damaged)).has_value());
  }
  {
    // bad LZ4: a match before the body's start
    Bytes file      = skyrimSave(12, 0, body);
    const auto info = readSkyrimSaveInfo(view(file));
    file.resize(info->screenshotEnd());
    file[info->screenshotOffset - 2] = std::byte{2};
    const Bytes badBlock = {std::byte{0x1F}, std::byte{78}, std::byte{0x40}, std::byte{0},
                            std::byte{40}};
    Writer w;
    w.raw(file).v(std::uint32_t{4096}).v(static_cast<std::uint32_t>(badBlock.size()))
        .raw(badBlock);
    CHECK(!readSkyrim(w.b).has_value());
  }
  {
    // an LZ4 body that says it's larger than it is: what it holds is read
    Bytes file = skyrimSave(12, 2, body);
    const auto info = readSkyrimSaveInfo(view(file));
    const std::uint32_t claimed = 900u << 20;
    std::memcpy(file.data() + info->screenshotEnd(), &claimed, 4);
    const auto data = readSkyrim(file);
    CHECK(data && data->plugins == SsePlugins);
  }
}

static void testFallout4()
{
  for (std::uint8_t formVersion : {std::uint8_t{67}, std::uint8_t{68}, std::uint8_t{69}}) {
    const Bytes file = fallout4Save(formVersion);
    const auto info  = readFallout4SaveInfo(view(file));
    CHECK(info && info->saveNumber == 7 && info->playerName == "Nora" &&
          info->playerLevel == 12 && info->playerLocation == "Sanctuary Hills" &&
          info->playerRace == "HumanRace" && info->fileTime == 134393902806640000ULL);
    if (!info) {
      continue;
    }
    const auto data = readFallout4SaveData(view(file), *info);
    CHECK(data && data->formVersion == formVersion && data->gameVersion == "1.10.163.0" &&
          data->plugins == Fo4Plugins && data->screenshot.size() == 5 * 2 * 4);
    CHECK(data && data->lightPlugins ==
                      (formVersion >= 68 ? Fo4Light : std::vector<std::string>{}));
    CHECK(fallout4SaveDataBound(*info, 1 << 20) >= info->screenshotOffset + 40);
    for (std::size_t cut = 0; cut < file.size(); ++cut) {
      const Bytes c(file.begin(), file.begin() + static_cast<std::ptrdiff_t>(cut));
      const auto i = readFallout4SaveInfo(view(c));
      if (i) {
        const auto d = readFallout4SaveData(view(c), *i);
        CHECK(!d || d->plugins == Fo4Plugins);
      }
    }
  }
  // damage
  {
    const Bytes file = fallout4Save(68, 9000, 9000);
    const auto info  = readFallout4SaveInfo(view(file));
    CHECK(info && !readFallout4SaveData(view(file), *info).has_value());
  }
  {
    // 4097 light plugins (the list holds one)
    const Bytes file = fallout4Save(68, 5, 2, 4097);
    const auto info  = readFallout4SaveInfo(view(file));
    CHECK(info && !readFallout4SaveData(view(file), *info).has_value());
  }
  {
    // 40 light plugins said, one there
    const Bytes file = fallout4Save(68, 5, 2, 40);
    const auto info  = readFallout4SaveInfo(view(file));
    CHECK(info && !readFallout4SaveData(view(file), *info).has_value());
  }
  {
    Bytes file = fallout4Save(68);
    file[3]    = std::byte{'5'};
    CHECK(!readFallout4SaveInfo(view(file)).has_value());
  }
}

static void testFallout3()
{
  for (const bool newVegas : {false, true}) {
    const auto layout = newVegas ? Fallout3Layout::NewVegas : Fallout3Layout::Fallout3;
    const Bytes file  = fallout3Save(newVegas);
    const auto info   = readFallout3SaveInfo(view(file), layout);
    CHECK(info && info->screenshotWidth == 3 && info->screenshotHeight == 2 &&
          info->saveNumber == 3 && info->playerName == "Courier" &&
          info->playerTitle == "Drifter" && info->playerLevel == 5 &&
          info->playerLocation == "Goodsprings");
    if (!info) {
      continue;
    }
    const auto data = readFallout3SaveData(view(file), *info);
    CHECK(data && data->playTime == "000.12.03" && data->screenshot.size() == 18 &&
          data->plugins == (std::vector<std::string>{"FalloutNV.esm", "DeadMoney.esm"}));
    // the other game's layout doesn't give this save's fields
    const auto other = readFallout3SaveInfo(
        view(file), newVegas ? Fallout3Layout::Fallout3 : Fallout3Layout::NewVegas);
    CHECK(!other || other->playerName != "Courier");
    for (std::size_t cut = 0; cut < file.size(); ++cut) {
      const Bytes c(file.begin(), file.begin() + static_cast<std::ptrdiff_t>(cut));
      const auto i = readFallout3SaveInfo(view(c), layout);
      if (i) {
        (void)readFallout3SaveData(view(c), *i);
      }
    }
  }
}

// --- real saves, read only ---

static Bytes slurp(const char* path)
{
  std::ifstream f(path, std::ios::binary);
  const std::vector<char> chars((std::istreambuf_iterator<char>(f)),
                                std::istreambuf_iterator<char>());
  Bytes b(chars.size());
  std::memcpy(b.data(), chars.data(), chars.size());
  return b;
}

static void printList(const char* what, const std::vector<std::string>& list)
{
  std::printf("  %s: %zu", what, list.size());
  for (std::size_t i = 0; i < list.size() && i < 4; ++i) {
    std::printf("%s%s", i == 0 ? " (" : ", ", list[i].c_str());
  }
  std::printf("%s\n", list.empty() ? "" : list.size() > 4 ? ", ...)" : ")");
}

static bool readReal(const std::string& kind, const char* path)
{
  const Bytes file = slurp(path);
  std::printf("%s %s (%zu bytes)\n", kind.c_str(), path, file.size());
  if (kind == "--sse") {
    const auto info = readSkyrimSaveInfo(view(file));
    if (!info) {
      return false;
    }
    std::printf("  v%u #%u %s, level %u, %s, %ux%u, compression %u\n", info->version,
                info->saveNumber, info->playerName.c_str(), info->playerLevel,
                info->playerLocation.c_str(), info->screenshotWidth, info->screenshotHeight,
                static_cast<unsigned>(info->compression));
    const auto layout = readSkyrimBodyLayout(view(file), *info);
    if (!layout) {
      return false;
    }
    const auto data = readSkyrimSaveData(view(file), *info, *layout, 1 << 20);
    if (!data) {
      return false;
    }
    std::printf("  form version %u\n", data->formVersion);
    printList("plugins", data->plugins);
    printList("light plugins", data->lightPlugins);
    return true;
  }
  if (kind == "--fo4") {
    const auto info = readFallout4SaveInfo(view(file));
    if (!info) {
      return false;
    }
    std::printf("  v%u #%u '%s', level %u, %s, %s, %ux%u\n", info->version, info->saveNumber,
                info->playerName.c_str(), info->playerLevel, info->playerLocation.c_str(),
                info->playTime.c_str(), info->screenshotWidth, info->screenshotHeight);
    const auto data = readFallout4SaveData(view(file), *info);
    if (!data) {
      return false;
    }
    std::printf("  form version %u, game %s\n", data->formVersion, data->gameVersion.c_str());
    printList("plugins", data->plugins);
    printList("light plugins", data->lightPlugins);
    return true;
  }
  const auto layout = kind == "--fnv" ? Fallout3Layout::NewVegas : Fallout3Layout::Fallout3;
  const auto info   = readFallout3SaveInfo(view(file), layout);
  if (!info) {
    return false;
  }
  std::printf("  #%u %s (%s), level %u, %s, %ux%u\n", info->saveNumber,
              info->playerName.c_str(), info->playerTitle.c_str(), info->playerLevel,
              info->playerLocation.c_str(), info->screenshotWidth, info->screenshotHeight);
  const auto data = readFallout3SaveData(view(file), *info);
  if (!data) {
    return false;
  }
  std::printf("  play time %s\n", data->playTime.c_str());
  printList("plugins", data->plugins);
  return true;
}

// the fuzzer's seeds: a target byte, the output size / 4 (16 bits), the input
static void writeSeeds(const std::string& dir)
{
  int n          = 0;
  const auto put = [&](std::uint8_t target, std::size_t outSize, const Bytes& input) {
    Writer w;
    w.v(target).v(static_cast<std::uint16_t>(std::min<std::size_t>(outSize / 4, 0xFFFF)));
    w.raw(input);
    std::ofstream f(dir + "/seed" + std::to_string(n++), std::ios::binary);
    f.write(reinterpret_cast<const char*>(w.b.data()), static_cast<std::streamsize>(w.b.size()));
  };
  const Bytes text = randomBytes(3000, 11, 6);
  put(0, text.size(), lz4Compress(text));
  put(0, 512, lz4Compress(Bytes(2000, std::byte{1})));
  put(1, text.size(), zlibCompress(text));
  const Bytes body = skyrimBody(78, SsePlugins, SseLight);
  for (const std::uint16_t c : {std::uint16_t{0}, std::uint16_t{1}, std::uint16_t{2}}) {
    put(2, 0, skyrimSave(12, c, body));
  }
  put(2, 0, skyrimSave(9, 0, skyrimBody(74, SsePlugins, {})));
  put(3, 0, body);
  put(4, 0, fallout4Save(68));
  put(4, 0, fallout4Save(67));
  put(5, 0, fallout3Save(false));
  put(5, 0, fallout3Save(true));
  std::printf("%d seeds in %s\n", n, dir.c_str());
}

int main(int argc, char** argv)
{
  if (argc == 3 && std::string(argv[1]) == "--write-seeds") {
    writeSeeds(argv[2]);
    return 0;
  }
  testLz4();
  testZlib();
  testSkyrim();
  testFallout4();
  testFallout3();
  for (int i = 1; i + 1 < argc; i += 2) {
    if (!readReal(argv[i], argv[i + 1])) {
      std::printf("FAIL: %s can't be read\n", argv[i + 1]);
      ++failures;
    }
  }
  std::printf(failures ? "%d failures\n" : "all passed\n", failures);
  return failures ? 1 : 0;
}

#endif
