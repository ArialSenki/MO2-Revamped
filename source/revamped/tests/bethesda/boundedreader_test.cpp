// MO2 Revamped: tests for BoundedReader (not shipped).
// Build and run (Developer prompt), with AddressSanitizer:
//   cl /nologo /std:c++20 /EHsc /fsanitize=address /I ..\..\games\bethesda\util boundedreader_test.cpp && boundedreader_test
// Fuzzing: the same file with /DMO2R_FUZZ and /fsanitize=fuzzer builds a libFuzzer target.
#include "boundedreader.h"

#include <cstdio>
#include <vector>

using mo2r::bethesda::BoundedReader;

// a save-header-like parse: what every format parser built on the reader looks like
static std::size_t parseSample(const std::uint8_t* data, std::size_t size)
{
  BoundedReader r(data, size);
  r.skip(4);
  const auto name    = r.lengthPrefixed<std::uint16_t>(512);
  const auto plugins = r.count<std::uint8_t>(255);
  std::size_t total  = name.size();
  for (std::size_t i = 0; plugins && i < *plugins && r.ok(); ++i) {
    total += r.lengthPrefixed<std::uint16_t>(260).size();
  }
  return r.ok() ? total : 0;
}

#ifdef MO2R_FUZZ
extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size)
{
  parseSample(data, size);
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

int main()
{
  const std::uint8_t good[] = {'T', 'E', 'S', 'V', 3, 0, 'A', 'b', 'c',
                               2,   2, 0, 'x', 'y', 1,   0,   'z'};
  CHECK(parseSample(good, sizeof(good)) == 6);

  // every truncation fails cleanly, never reads past the end
  for (std::size_t n = 0; n < sizeof(good); ++n) {
    std::vector<std::uint8_t> cut(good, good + n);
    CHECK(parseSample(cut.data(), cut.size()) == 0);
  }

  // a huge length is refused, not trusted
  const std::uint8_t huge[] = {0, 0, 0, 0, 0xFF, 0xFF};
  BoundedReader r(huge, sizeof(huge));
  r.skip(4);
  CHECK(r.lengthPrefixed<std::uint16_t>(512).empty());
  CHECK(!r.ok());
  CHECK(r.remaining() == 0);

  // a failed reader stays failed and returns zeros
  BoundedReader e(good, 2);
  CHECK(e.read<std::uint32_t>() == 0);
  CHECK(!e.ok());
  CHECK(e.read<std::uint8_t>() == 0);

  // a count above the limit is refused
  const std::uint8_t many[] = {200};
  BoundedReader c(many, 1);
  CHECK(!c.count<std::uint8_t>(100).has_value());

  // BZString: the trailing NUL is dropped
  const std::uint8_t bz[] = {4, 0, 'a', 'b', 'c', 0};
  BoundedReader b(bz, sizeof(bz));
  CHECK(b.lengthPrefixed<std::uint16_t>(16) == "abc");
  CHECK(b.ok() && b.remaining() == 0);

  std::printf(failures ? "%d failures\n" : "all passed\n", failures);
  return failures ? 1 : 0;
}
#endif
