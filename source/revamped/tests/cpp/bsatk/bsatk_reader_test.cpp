// MO2 Revamped: bsatk, the BSA/BA2 reader, on crafted archives (not shipped).
//
// Complements tests/stability/bsa_crafted_test.cpp (BA2 texture without
// chunks, Morrowind name tables) with every other format and code path:
// BSA 103/104/105, BA2 GNRL/DX10 (and Starfield's LZ4 textures), names,
// counts, offsets, zlib/LZ4 data, and extraction to disk.
//
// Each damaged archive must come back as an error (an exception or an error
// code), never as a crash, a hang, a huge allocation or a write outside the
// target folder; a well-formed one must still read and extract byte-exact.
//
// Every case runs in a child process of its own, so a crash or a hang in one
// is reported as that case's failure and the rest still run:
//   bsatk_reader_test.exe <work folder>                runs all the cases
//   bsatk_reader_test.exe <work folder> --case <n>     runs one (used by the parent)
// Built and run by run.ps1 next to this file.
#include <bsaarchive.h>
#include <bsaexception.h>
#include <bsafile.h>
#include <bsafolder.h>
#include <filehash.h>

#include <lz4.h>
#include <lz4frame.h>
#include <zlib.h>

#include <crtdbg.h>
#include <csignal>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <functional>
#include <malloc.h>
#include <new>
#include <string>
#include <vector>

#include <Windows.h>
#include <werapi.h>

#pragma comment(lib, "wer.lib")

namespace fs = std::filesystem;

// ---------------------------------------------------------------------------
// allocation guard: any single allocation over 256 MB, or more than 768 MB
// live at once, ends the child with exit code 3 ("huge allocation"). A crafted
// archive must never make bsatk ask for that much; none of the cases needs
// more than a few MB. (Under ASan, ASAN_OPTIONS does the same job.)
#if !defined(__SANITIZE_ADDRESS__)
namespace
{
constexpr size_t kMaxSingle = 256ull << 20;
constexpr size_t kMaxLive   = 768ull << 20;
volatile long long g_live   = 0;

[[noreturn]] void hugeAllocation(size_t size)
{
  char message[128];
  std::snprintf(message, sizeof(message), "      huge allocation: %zu bytes (%lld live)\n",
                size, static_cast<long long>(g_live));
  DWORD written = 0;
  ::WriteFile(::GetStdHandle(STD_OUTPUT_HANDLE), message,
              static_cast<DWORD>(std::strlen(message)), &written, nullptr);
  ::TerminateProcess(::GetCurrentProcess(), 3);
  for (;;) {
  }
}

void* guardedAlloc(size_t size)
{
  if (size > kMaxSingle) {
    hugeAllocation(size);
  }
  void* p = std::malloc(size == 0 ? 1 : size);
  if (p == nullptr) {
    throw std::bad_alloc();
  }
  const long long live =
      ::InterlockedExchangeAdd64(&g_live, static_cast<long long>(_msize(p))) +
      static_cast<long long>(_msize(p));
  if (live > static_cast<long long>(kMaxLive)) {
    hugeAllocation(size);
  }
  return p;
}

void guardedFree(void* p) noexcept
{
  if (p != nullptr) {
    ::InterlockedExchangeAdd64(&g_live, -static_cast<long long>(_msize(p)));
    std::free(p);
  }
}
}  // namespace

void* operator new(size_t size) { return guardedAlloc(size); }
void* operator new[](size_t size) { return guardedAlloc(size); }
void* operator new(size_t size, const std::nothrow_t&) noexcept
{
  try {
    return guardedAlloc(size);
  } catch (...) {
    return nullptr;
  }
}
void* operator new[](size_t size, const std::nothrow_t&) noexcept
{
  try {
    return guardedAlloc(size);
  } catch (...) {
    return nullptr;
  }
}
void operator delete(void* p) noexcept { guardedFree(p); }
void operator delete[](void* p) noexcept { guardedFree(p); }
void operator delete(void* p, size_t) noexcept { guardedFree(p); }
void operator delete[](void* p, size_t) noexcept { guardedFree(p); }
void operator delete(void* p, const std::nothrow_t&) noexcept { guardedFree(p); }
void operator delete[](void* p, const std::nothrow_t&) noexcept { guardedFree(p); }
#endif

namespace
{
using Bytes = std::vector<uint8_t>;

// ---------------------------------------------------------------------------
// little-endian writer with patching

struct Writer
{
  Bytes b;
  size_t pos() const { return b.size(); }
  void u8(uint8_t v) { b.push_back(v); }
  void u16(uint16_t v)
  {
    u8(v & 0xFF);
    u8(v >> 8);
  }
  void u32(uint32_t v)
  {
    u16(v & 0xFFFF);
    u16(v >> 16);
  }
  void u64(uint64_t v)
  {
    u32(static_cast<uint32_t>(v));
    u32(static_cast<uint32_t>(v >> 32));
  }
  void raw(const void* data, size_t size)
  {
    const auto* p = static_cast<const uint8_t*>(data);
    b.insert(b.end(), p, p + size);
  }
  void str(const std::string& s) { raw(s.data(), s.size()); }
  void zstr(const std::string& s)
  {
    str(s);
    u8(0);
  }
  void bytes(const Bytes& v) { raw(v.data(), v.size()); }
  void put32(size_t at, uint32_t v) { std::memcpy(&b[at], &v, 4); }
  void put64(size_t at, uint64_t v) { std::memcpy(&b[at], &v, 8); }
  void put16(size_t at, uint16_t v) { std::memcpy(&b[at], &v, 2); }
};

Bytes pattern(size_t size, uint32_t seed)
{
  Bytes out(size);
  uint32_t x = seed * 2654435761u + 1;
  for (size_t i = 0; i < size; ++i) {
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    // half of it compressible, so zlib/LZ4 really pack something
    out[i] = (i / 64) % 2 ? static_cast<uint8_t>('a' + i % 7) : static_cast<uint8_t>(x);
  }
  return out;
}

Bytes zlibPack(const Bytes& in)
{
  uLongf size = compressBound(static_cast<uLong>(in.size()));
  Bytes out(size);
  if (compress2(out.data(), &size, in.data(), static_cast<uLong>(in.size()), 9) != Z_OK) {
    std::abort();
  }
  out.resize(size);
  return out;
}

Bytes lz4Frame(const Bytes& in)
{
  Bytes out(LZ4F_compressFrameBound(in.size(), nullptr));
  const size_t size =
      LZ4F_compressFrame(out.data(), out.size(), in.data(), in.size(), nullptr);
  if (LZ4F_isError(size)) {
    std::abort();
  }
  out.resize(size);
  return out;
}

Bytes lz4Block(const Bytes& in)
{
  Bytes out(LZ4_compressBound(static_cast<int>(in.size())));
  const int size =
      LZ4_compress_default(reinterpret_cast<const char*>(in.data()),
                           reinterpret_cast<char*>(out.data()), static_cast<int>(in.size()),
                           static_cast<int>(out.size()));
  if (size <= 0) {
    std::abort();
  }
  out.resize(size);
  return out;
}

// a name in the system code page, the bytes bsatk keeps
std::string acp(const std::wstring& wide)
{
  const int size = ::WideCharToMultiByte(CP_ACP, 0, wide.data(), static_cast<int>(wide.size()),
                                         nullptr, 0, nullptr, nullptr);
  std::string out(size, '\0');
  ::WideCharToMultiByte(CP_ACP, 0, wide.data(), static_cast<int>(wide.size()), out.data(),
                        size, nullptr, nullptr);
  return out;
}

std::wstring wide(const std::string& narrow)
{
  const int size = ::MultiByteToWideChar(CP_ACP, 0, narrow.data(),
                                         static_cast<int>(narrow.size()), nullptr, 0);
  std::wstring out(size, L'\0');
  ::MultiByteToWideChar(CP_ACP, 0, narrow.data(), static_cast<int>(narrow.size()),
                        out.data(), size);
  return out;
}

// ---------------------------------------------------------------------------
// BSA 103 (Oblivion), 104 (Fallout 3/NV, Skyrim LE), 105 (Skyrim SE)

constexpr uint32_t kDirNames   = 0x1;
constexpr uint32_t kFileNames  = 0x2;
constexpr uint32_t kCompressed = 0x4;
constexpr uint32_t kPrefixed   = 0x100;

struct BsaFile
{
  std::string name;
  Bytes content;
  bool compress = false;
};

struct BsaFolder
{
  std::string name;
  std::vector<BsaFile> files;
};

struct BsaLayout
{
  Writer w;
  size_t folderRecords = 0;           // the first folder record
  std::vector<size_t> folderRecord;   // each folder record
  std::vector<size_t> fileRecord;     // each file record, in order
  std::vector<size_t> data;           // each file's data block
  std::vector<size_t> nameTable;      // each file's name in the name table
};

BsaLayout bsa(uint32_t version, uint32_t flags, const std::vector<BsaFolder>& folders)
{
  BsaLayout l;
  Writer& w             = l.w;
  const bool se         = version == 105;
  const bool prefixed   = version != 103 && (flags & kPrefixed);
  uint32_t fileCount = 0, folderNames = 0, fileNames = 0;
  for (const auto& f : folders) {
    folderNames += static_cast<uint32_t>(f.name.size() + 1);
    for (const auto& file : f.files) {
      ++fileCount;
      fileNames += static_cast<uint32_t>(file.name.size() + 1);
    }
  }

  // file data blocks first, so the records can point at them
  std::vector<Bytes> blocks;
  for (const auto& f : folders) {
    for (const auto& file : f.files) {
      Writer d;
      if (prefixed) {
        const std::string full = f.name + "\\" + file.name;
        d.u8(static_cast<uint8_t>(full.size()));
        d.str(full);
      }
      if (file.compress) {
        d.u32(static_cast<uint32_t>(file.content.size()));
        d.bytes(se ? lz4Frame(file.content) : zlibPack(file.content));
      } else {
        d.bytes(file.content);
      }
      blocks.push_back(d.b);
    }
  }

  w.str(std::string("BSA\0", 4));
  w.u32(version);
  w.u32(36);
  w.u32(flags);
  w.u32(static_cast<uint32_t>(folders.size()));
  w.u32(fileCount);
  w.u32(folderNames);
  w.u32(fileNames);
  w.u32(0);

  l.folderRecords = w.pos();
  for (const auto& f : folders) {
    l.folderRecord.push_back(w.pos());
    w.u64(calculateBSAHash(f.name));
    w.u32(static_cast<uint32_t>(f.files.size()));
    if (se) {
      w.u32(0);
      w.u64(0);
    } else {
      w.u32(0);
    }
  }
  size_t recordBlocks = w.pos();
  size_t size = recordBlocks;
  for (const auto& f : folders) {
    size += 1 + f.name.size() + 1 + 16 * f.files.size();
  }
  size_t dataStart = size + fileNames;

  size_t block = 0, dataPos = dataStart;
  for (size_t i = 0; i < folders.size(); ++i) {
    const auto& f = folders[i];
    const size_t at = w.pos();
    if (se) {
      w.put64(l.folderRecord[i] + 16, at + fileNames);
    } else {
      w.put32(l.folderRecord[i] + 12, static_cast<uint32_t>(at + fileNames));
    }
    w.u8(static_cast<uint8_t>(f.name.size() + 1));
    w.zstr(f.name);
    for (const auto& file : f.files) {
      l.fileRecord.push_back(w.pos());
      w.u64(calculateBSAHash(file.name));
      const bool toggled = file.compress != ((flags & kCompressed) != 0);
      w.u32(static_cast<uint32_t>(blocks[block].size()) | (toggled ? 0x40000000u : 0u));
      w.u32(static_cast<uint32_t>(dataPos));
      l.data.push_back(dataPos);
      dataPos += blocks[block].size();
      ++block;
    }
  }
  for (const auto& f : folders) {
    for (const auto& file : f.files) {
      l.nameTable.push_back(w.pos());
      w.zstr(file.name);
    }
  }
  for (const auto& d : blocks) {
    w.bytes(d);
  }
  return l;
}

// ---------------------------------------------------------------------------
// BA2: GNRL and DX10 (version 1 is Fallout 4; 2 and 3 are Starfield, 3 packs
// texture chunks with LZ4 blocks)

struct Ba2File
{
  std::string name;
  Bytes content;
  bool compress = false;
};

struct Ba2Layout
{
  Writer w;
  size_t nameTableField = 16;
  std::vector<size_t> record;  // each file record
  std::vector<size_t> data;
};

size_t ba2HeaderSize(uint32_t version)
{
  return version == 2 ? 32 : version == 3 ? 36 : 24;
}

void ba2Header(Writer& w, uint32_t version, const char* type, uint32_t files)
{
  w.str("BTDX");
  w.u32(version);
  w.str(type);
  w.u32(files);
  w.u64(0);  // name table, patched
  if (version >= 2) {
    w.u64(1);
  }
  if (version == 3) {
    w.u32(3);  // LZ4 textures
  }
}

void ba2Names(Ba2Layout& l, const std::vector<std::string>& names)
{
  l.w.put64(16, l.w.pos());
  for (const auto& n : names) {
    l.w.u16(static_cast<uint16_t>(n.size()));
    l.w.str(n);
  }
}

Ba2Layout ba2General(const std::vector<Ba2File>& files, uint32_t version = 1)
{
  Ba2Layout l;
  ba2Header(l.w, version, "GNRL", static_cast<uint32_t>(files.size()));
  std::vector<Bytes> blocks;
  for (const auto& f : files) {
    blocks.push_back(f.compress ? zlibPack(f.content) : f.content);
  }
  size_t dataPos = ba2HeaderSize(version) + 36 * files.size();
  for (size_t i = 0; i < files.size(); ++i) {
    l.record.push_back(l.w.pos());
    l.w.u32(0);
    l.w.str(std::string("txt\0", 4));
    l.w.u32(0);
    l.w.u32(0x00100100);
    l.w.u64(dataPos);
    l.w.u32(files[i].compress ? static_cast<uint32_t>(blocks[i].size()) : 0);
    l.w.u32(static_cast<uint32_t>(files[i].content.size()));
    l.w.u32(0xBAADF00D);
    l.data.push_back(dataPos);
    dataPos += blocks[i].size();
  }
  for (const auto& b : blocks) {
    l.w.bytes(b);
  }
  std::vector<std::string> names;
  for (const auto& f : files) {
    names.push_back(f.name);
  }
  ba2Names(l, names);
  return l;
}

struct Chunk
{
  Bytes content;
  bool compress = false;
};

constexpr uint8_t kBC1 = 71;   // DXGI_FORMAT_BC1_UNORM
constexpr uint8_t kBC7 = 98;   // DXGI_FORMAT_BC7_UNORM, written with a DX10 header

// one texture; chunk records at l.record[0] + 24 + 24 * i
Ba2Layout ba2Texture(const std::string& name, const std::vector<Chunk>& chunks,
                     uint8_t format = kBC1, uint32_t version = 1)
{
  Ba2Layout l;
  ba2Header(l.w, version, "DX10", 1);
  std::vector<Bytes> blocks;
  for (const auto& c : chunks) {
    if (!c.compress) {
      blocks.push_back(c.content);
    } else {
      blocks.push_back(version == 3 ? lz4Block(c.content) : zlibPack(c.content));
    }
  }
  size_t dataPos = ba2HeaderSize(version) + 24 + 24 * chunks.size();
  l.record.push_back(l.w.pos());
  l.w.u32(0);
  l.w.str(std::string("dds\0", 4));
  l.w.u32(0);
  l.w.u8(0);
  l.w.u8(static_cast<uint8_t>(chunks.size()));
  l.w.u16(24);
  l.w.u16(64);  // height
  l.w.u16(64);  // width
  l.w.u8(static_cast<uint8_t>(chunks.size()));
  l.w.u8(format);
  l.w.u8(0);
  l.w.u8(0);
  for (size_t i = 0; i < chunks.size(); ++i) {
    l.w.u64(dataPos);
    l.w.u32(chunks[i].compress ? static_cast<uint32_t>(blocks[i].size()) : 0);
    l.w.u32(static_cast<uint32_t>(chunks[i].content.size()));
    l.w.u16(static_cast<uint16_t>(i));
    l.w.u16(static_cast<uint16_t>(i));
    l.w.u32(0xBAADF00D);
    l.data.push_back(dataPos);
    dataPos += blocks[i].size();
  }
  for (const auto& b : blocks) {
    l.w.bytes(b);
  }
  ba2Names(l, {name});
  return l;
}

// ---------------------------------------------------------------------------
// running bsatk

fs::path g_dir;  // this case's folder: the archive and target\ go here

fs::path save(const Bytes& b, const char* ext = "bsa")
{
  const fs::path path = g_dir / (std::string("archive.") + ext);
  std::ofstream(path, std::ios::binary)
      .write(reinterpret_cast<const char*>(b.data()), b.size());
  return path;
}

fs::path target()
{
  return g_dir / "target";
}

struct Opened
{
  std::unique_ptr<BSA::Archive> archive;
  bool refused = false;  // an exception or an error code other than INVALIDHASHES
  BSA::EErrorCode code = BSA::ERROR_NONE;
};

Opened open(const fs::path& path, bool testHashes = false)
{
  Opened o;
  o.archive = std::make_unique<BSA::Archive>();
  try {
    o.code    = o.archive->read(path, testHashes);
    o.refused = o.code != BSA::ERROR_NONE && o.code != BSA::ERROR_INVALIDHASHES;
    if (o.refused) {
      std::printf("      (read: error %d)\n", o.code);
    }
  } catch (const std::exception& e) {
    std::printf("      (read throws: %s)\n", e.what());
    o.refused = true;
  }
  return o;
}

void collect(const BSA::Folder::Ptr& folder, std::vector<BSA::File::Ptr>& out)
{
  for (unsigned int i = 0; i < folder->getNumFiles(); ++i) {
    out.push_back(folder->getFile(i));
  }
  for (unsigned int i = 0; i < folder->getNumSubFolders(); ++i) {
    collect(folder->getSubFolder(i), out);
  }
}

std::vector<BSA::File::Ptr> files(BSA::Archive& a)
{
  std::vector<BSA::File::Ptr> out;
  collect(a.getRoot(), out);
  return out;
}

// true when extract() reports an error
bool extractFails(BSA::Archive& a, const BSA::File::Ptr& f, const fs::path& dir)
{
  fs::create_directories(dir);
  try {
    const auto res = a.extract(f, dir);
    if (res != BSA::ERROR_NONE) {
      std::printf("      (extract: error %d)\n", res);
      return true;
    }
    return false;
  } catch (const std::exception& e) {
    std::printf("      (extract throws: %s)\n", e.what());
    return true;
  }
}

bool progress(int, std::string)
{
  return true;
}

// true when extractAll() reports an error
bool extractAllFails(BSA::Archive& a, const fs::path& dir)
{
  fs::create_directories(dir);
  try {
    const auto res = a.extractAll(dir, &progress, true);
    if (res != BSA::ERROR_NONE) {
      std::printf("      (extractAll: error %d)\n", res);
      return true;
    }
    return false;
  } catch (const std::exception& e) {
    std::printf("      (extractAll throws: %s)\n", e.what());
    return true;
  }
}

Bytes slurp(const fs::path& path)
{
  std::ifstream in(path, std::ios::binary);
  return Bytes(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

int failures = 0;
bool check(bool ok, const char* what)
{
  std::printf("      %s %s\n", ok ? "ok  " : "FAIL", what);
  failures += ok ? 0 : 1;
  return ok;
}

// a damaged archive: refused by read(), or else every way of extracting its
// (single) damaged file reports an error
void damaged(const fs::path& path, bool testHashes = false)
{
  {
    Opened o = open(path, testHashes);
    if (o.refused) {
      check(true, "refused by read()");
      return;
    }
    const auto list = files(*o.archive);
    bool any = list.empty();
    for (const auto& f : list) {
      any = extractFails(*o.archive, f, target() / "single") || any;
    }
    check(any, "extract() reports an error");
  }
  {
    Opened o = open(path, testHashes);
    if (!o.refused) {
      check(extractAllFails(*o.archive, target() / "all"), "extractAll() reports an error");
    }
  }
}

// a path inside the archive without its leading separators (root files are
// "\\name"), '/' as '\\'
std::string trimmed(std::string path)
{
  for (auto& ch : path) {
    ch = ch == '/' ? '\\' : ch;
  }
  while (!path.empty() && path[0] == '\\') {
    path.erase(0, 1);
  }
  return path;
}

struct Expected
{
  std::string path;  // inside the archive, folder\name (as bytes in the code page)
  Bytes content;
};

// a well-formed archive: read() gives ERROR_NONE (hashes checked), and both
// extract() and extractAll() write every file byte-exact
void wellFormed(const fs::path& path, const std::vector<Expected>& expected,
                bool testHashes = true)
{
  {
    Opened o = open(path, testHashes);
    if (!check(!o.refused && o.code == BSA::ERROR_NONE, "read() gives ERROR_NONE")) {
      return;
    }
    const auto list = files(*o.archive);
    check(list.size() == expected.size(), "every file listed");
    for (const auto& f : list) {
      const fs::path dir = target() / "single";
      const bool failed  = extractFails(*o.archive, f, dir);
      const Expected* e  = nullptr;
      for (const auto& x : expected) {
        if (trimmed(x.path) == trimmed(f->getFilePath())) {
          e = &x;
        }
      }
      const Bytes got = slurp(dir / wide(f->getName()));
      check(!failed && e != nullptr && got == e->content,
            ("extract() byte-exact: " + f->getFilePath()).c_str());
    }
  }
  {
    Opened o = open(path, testHashes);
    const fs::path dir = target() / "all";
    check(!o.refused && !extractAllFails(*o.archive, dir), "extractAll() gives ERROR_NONE");
    for (const auto& x : expected) {
      std::string rel = x.path;
      while (!rel.empty() && (rel[0] == '\\' || rel[0] == '/')) {
        rel.erase(0, 1);
      }
      check(slurp(dir / wide(rel)) == x.content, ("extractAll() byte-exact: " + x.path).c_str());
    }
  }
}

// anything but a crash/hang is fine, yet whatever is written stays in target
void survives(const fs::path& path)
{
  {
    Opened o = open(path);
    if (!o.refused) {
      for (const auto& f : files(*o.archive)) {
        extractFails(*o.archive, f, target() / "single");
      }
    }
  }
  {
    Opened o = open(path);
    if (!o.refused) {
      extractAllFails(*o.archive, target() / "all");
    }
  }
  check(true, "no crash");
}

// ---------------------------------------------------------------------------
// the cases

std::vector<BsaFolder> sample(bool compress)
{
  return {{"meshes\\armor", {{"helmet.nif", pattern(3000, 1), compress},
                             {"boots.nif", pattern(1, 2), compress}}},
          {"textures", {{"sky.dds", pattern(70000, 3), compress}}}};
}

std::vector<Expected> expect(const std::vector<BsaFolder>& folders)
{
  std::vector<Expected> out;
  for (const auto& f : folders) {
    for (const auto& file : f.files) {
      out.push_back({f.name + "\\" + file.name, file.content});
    }
  }
  return out;
}

// one-file BSA of the given version, the file compressed or not
BsaLayout single(uint32_t version, uint32_t flags, const Bytes& content, bool compress,
                 const std::string& folder = "data", const std::string& name = "a.txt")
{
  return bsa(version, flags, {{folder, {{name, content, compress}}}});
}

constexpr uint32_t kStd = kDirNames | kFileNames;

Bytes ddsTail(const fs::path& file, size_t headers)
{
  Bytes b = slurp(file);
  return b.size() >= headers ? Bytes(b.begin() + headers, b.end()) : Bytes();
}

struct Case
{
  const char* name;
  std::function<void()> run;
};

std::vector<Case> cases()
{
  std::vector<Case> c;

  // --- well-formed archives, every version -----------------------------------
  c.push_back({"BSA 103 (Oblivion) stored reads and extracts byte-exact", [] {
                 auto f = sample(false);
                 wellFormed(save(bsa(103, kStd, f).w.b), expect(f));
               }});
  c.push_back({"BSA 103 (Oblivion) zlib reads and extracts byte-exact", [] {
                 auto f = sample(true);
                 wellFormed(save(bsa(103, kStd | kCompressed, f).w.b), expect(f));
               }});
  c.push_back({"BSA 104 (Skyrim) stored, names embedded, byte-exact", [] {
                 auto f = sample(false);
                 wellFormed(save(bsa(104, kStd | kPrefixed, f).w.b), expect(f));
               }});
  c.push_back({"BSA 104 (Skyrim) zlib, names embedded, byte-exact", [] {
                 auto f = sample(true);
                 wellFormed(save(bsa(104, kStd | kCompressed | kPrefixed, f).w.b), expect(f));
               }});
  c.push_back({"BSA 104 with per-file compression toggled, byte-exact", [] {
                 std::vector<BsaFolder> f = {
                     {"sound", {{"a.wav", pattern(5000, 4), true}, {"b.wav", pattern(9, 5), false}}}};
                 wellFormed(save(bsa(104, kStd, f).w.b), expect(f));
               }});
  c.push_back({"BSA 105 (Skyrim SE) stored, byte-exact", [] {
                 auto f = sample(false);
                 wellFormed(save(bsa(105, kStd, f).w.b), expect(f));
               }});
  c.push_back({"BSA 105 (Skyrim SE) LZ4 frames, names embedded, byte-exact", [] {
                 auto f = sample(true);
                 wellFormed(save(bsa(105, kStd | kCompressed | kPrefixed, f).w.b), expect(f));
               }});
  c.push_back({"BA2 GNRL stored and zlib files, byte-exact", [] {
                 std::vector<Ba2File> f = {{"interface\\a.swf", pattern(4000, 6), false},
                                           {"interface\\b.txt", pattern(80000, 7), true},
                                           {"scripts\\c.pex", pattern(1, 8), true}};
                 std::vector<Expected> e;
                 for (const auto& x : f) {
                   e.push_back({x.name, x.content});
                 }
                 wellFormed(save(ba2General(f).w.b, "ba2"), e, false);
               }});
  c.push_back({"BA2 GNRL Starfield header (version 2), byte-exact", [] {
                 std::vector<Ba2File> f = {{"meshes\\x.nif", pattern(3000, 9), true}};
                 wellFormed(save(ba2General(f, 2).w.b, "ba2"), {{f[0].name, f[0].content}}, false);
               }});
  c.push_back({"BA2 DX10 stored + zlib chunks extract as one DDS, byte-exact", [] {
                 const Bytes a = pattern(2048, 10), b = pattern(512, 11);
                 const auto p  = save(ba2Texture("textures\\t.dds", {{a, true}, {b, false}}).w.b, "ba2");
                 Bytes all = a;
                 all.insert(all.end(), b.begin(), b.end());
                 Opened o = open(p);
                 if (!check(!o.refused, "read()")) {
                   return;
                 }
                 auto list = files(*o.archive);
                 check(list.size() == 1 && !extractFails(*o.archive, list[0], target()),
                       "extract()");
                 const Bytes head = slurp(target() / "t.dds");
                 check(head.size() == 4 + 124 + all.size() && std::memcmp(head.data(), "DDS ", 4) == 0,
                       "DDS magic and size");
                 check(ddsTail(target() / "t.dds", 128) == all, "chunks byte-exact (extract)");
                 Opened o2 = open(p);
                 check(!extractAllFails(*o2.archive, target() / "all"), "extractAll()");
                 check(ddsTail(target() / "all" / "textures" / "t.dds", 128) == all,
                       "chunks byte-exact (extractAll)");
               }});
  c.push_back({"BA2 DX10 BC7 texture gets the DX10 header, byte-exact", [] {
                 const Bytes a = pattern(4096, 12);
                 const auto p  = save(ba2Texture("t.dds", {{a, true}}, kBC7).w.b, "ba2");
                 Opened o      = open(p);
                 auto list     = files(*o.archive);
                 check(!o.refused && list.size() == 1 && !extractFails(*o.archive, list[0], target()),
                       "extract()");
                 const Bytes all = slurp(target() / "t.dds");
                 check(all.size() == 4 + 124 + 20 + a.size(), "DDS + DX10 header size");
                 check(ddsTail(target() / "t.dds", 148) == a, "chunk byte-exact");
               }});
  c.push_back({"BA2 DX10 Starfield LZ4 chunks (version 3), byte-exact", [] {
                 const Bytes a = pattern(3000, 13), b = pattern(700, 14);
                 const auto p =
                     save(ba2Texture("t.dds", {{a, true}, {b, true}}, kBC1, 3).w.b, "ba2");
                 Bytes all = a;
                 all.insert(all.end(), b.begin(), b.end());
                 Opened o  = open(p);
                 auto list = files(*o.archive);
                 check(!o.refused && list.size() == 1 && !extractFails(*o.archive, list[0], target()),
                       "extract()");
                 check(ddsTail(target() / "t.dds", 128) == all, "chunks byte-exact (extract)");
                 Opened o2 = open(p);
                 check(!extractAllFails(*o2.archive, target() / "all"), "extractAll()");
                 check(ddsTail(target() / "all" / "t.dds", 128) == all,
                       "chunks byte-exact (extractAll)");
               }});
  c.push_back({"Morrowind BSA extracts byte-exact", [] {
                 Writer w;
                 const Bytes a = pattern(300, 15);
                 w.u32(0x100);
                 w.u32(12 + 6 + 8);  // records + "x.txt\0" ... + hashes start
                 w.u32(1);
                 w.u32(static_cast<uint32_t>(a.size()));
                 w.u32(0);
                 w.u32(0);
                 w.zstr("x.txt");
                 w.u64(0);
                 w.bytes(a);
                 // the header's offset is the size of the records and names
                 w.put32(4, 12 + 6);
                 wellFormed(save(w.b), {{"x.txt", a}}, false);
               }});
  c.push_back({"Morrowind BSA whose last name starts past its name table is refused", [] {
                 // the same layout, with the name's offset past the 6 bytes of
                 // the table: a file without a name before, a damaged archive now
                 Writer w;
                 const Bytes a = pattern(30, 15);
                 w.u32(0x100);
                 w.u32(12 + 6 + 8);
                 w.u32(1);
                 w.u32(static_cast<uint32_t>(a.size()));
                 w.u32(0);
                 w.u32(40);
                 w.zstr("x.txt");
                 w.u64(0);
                 w.bytes(a);
                 w.put32(4, 12 + 6);
                 damaged(save(w.b));
               }});
  c.push_back({"empty BSA (no folders, no files) reads and extracts nothing", [] {
                 const auto p = save(bsa(104, kStd, {}).w.b);
                 Opened o     = open(p);
                 check(!o.refused && files(*o.archive).empty(), "read() lists nothing");
                 extractAllFails(*o.archive, target());
                 check(true, "extractAll() returns");
               }});
  c.push_back({"empty BA2 (no files) reads and extracts nothing", [] {
                 const auto p = save(ba2General({}).w.b, "ba2");
                 Opened o     = open(p);
                 check(!o.refused && files(*o.archive).empty(), "read() lists nothing");
                 extractAllFails(*o.archive, target());
                 check(true, "extractAll() returns");
               }});

  // --- names --------------------------------------------------------------
  c.push_back({"names outside ASCII (system code page) extract under their name", [] {
                 const std::string folder = acp(L"m\u00fasica\\\u00e1lbum");
                 const std::string name   = acp(L"caf\u00e9 \u00f1and\u00fa.txt");
                 std::vector<BsaFolder> f = {{folder, {{name, pattern(77, 16), true}}}};
                 wellFormed(save(bsa(105, kStd | kCompressed | kPrefixed, f).w.b), expect(f));
               }});
  c.push_back({"BA2 names outside ASCII extract under their name", [] {
                 std::vector<Ba2File> f = {{acp(L"interface\\\u00e9t\u00e9.txt"), pattern(50, 17), true}};
                 wellFormed(save(ba2General(f).w.b, "ba2"), {{f[0].name, f[0].content}}, false);
               }});
  c.push_back({"embedded name longer than the file's data is an error", [] {
                 auto l = single(104, kStd | kPrefixed, pattern(10, 18), false);
                 // the name's length byte claims 200 bytes, the record holds 16
                 l.w.b[l.data[0]] = 200;
                 damaged(save(l.w.b));
               }});
  c.push_back({"embedded name filling the whole record is an error", [] {
                 // "d\a.txt" is 7 bytes: a record of 7 holds no room for it
                 auto l = single(104, kStd | kPrefixed, pattern(10, 18), false, "d", "a.txt");
                 l.w.put32(l.fileRecord[0] + 8, 7);
                 damaged(save(l.w.b));
               }});
  c.push_back({"embedded name in a compressed SE record leaving < 4 bytes is an error", [] {
                 auto l = single(105, kStd | kPrefixed | kCompressed, pattern(100, 19), true, "d",
                                 "a.txt");
                 l.w.put32(l.fileRecord[0] + 8, 1 + 7 + 2);  // name + 2 bytes
                 damaged(save(l.w.b));
               }});
  c.push_back({"over-long file name (300 bytes) in the name table is an error", [] {
                 auto l = single(104, kStd, pattern(10, 20), false, "d", std::string(300, 'n'));
                 const auto o = open(save(l.w.b));
                 check(o.refused || o.code != BSA::ERROR_NONE, "read() reports it");
               }});
  c.push_back({"long folder and file names (over MAX_PATH together) don't escape", [] {
                 std::string folder;
                 for (int i = 0; i < 25; ++i) {
                   folder += "abcdefghi\\";
                 }
                 folder += "abcd";
                 const std::string name = std::string(240, 'f') + ".txt";
                 survives(save(single(104, kStd, pattern(10, 21), false, folder, name).w.b));
               }});
  c.push_back({"duplicate files in a folder: no crash, the file holds one of them", [] {
                 std::vector<BsaFolder> f = {
                     {"d", {{"same.txt", pattern(100, 22), false}, {"same.txt", pattern(200, 23), false}}}};
                 const auto p = save(bsa(104, kStd, f).w.b);
                 Opened o     = open(p, true);
                 check(!o.refused && files(*o.archive).size() == 2, "both listed");
                 check(!extractAllFails(*o.archive, target()), "extractAll()");
                 const Bytes got = slurp(target() / "d" / "same.txt");
                 check(got == f[0].files[0].content || got == f[0].files[1].content,
                       "holds one whole copy");
               }});
  c.push_back({"duplicate folder records: no crash", [] {
                 std::vector<BsaFolder> f = {{"d", {{"a.txt", pattern(10, 24), false}}},
                                             {"d", {{"b.txt", pattern(10, 25), false}}}};
                 survives(save(bsa(104, kStd, f).w.b));
               }});
  c.push_back({"BA2 duplicate names: no crash", [] {
                 survives(save(ba2General({{"a\\x.txt", pattern(10, 26), false},
                                           {"a\\x.txt", pattern(20, 27), true}})
                                   .w.b,
                               "ba2"));
               }});
  c.push_back({"name hash mismatch gives ERROR_INVALIDHASHES", [] {
                 auto l = single(104, kStd, pattern(10, 28), false);
                 l.w.put64(l.fileRecord[0], 0x1234);
                 const auto o = open(save(l.w.b), true);
                 check(!o.refused && o.code == BSA::ERROR_INVALIDHASHES, "INVALIDHASHES");
               }});

  // --- counts -------------------------------------------------------------
  c.push_back({"BSA folder count 0xFFFFFFFF is refused", [] {
                 auto l = single(104, kStd, pattern(10, 29), false);
                 l.w.put32(16, 0xFFFFFFFF);
                 damaged(save(l.w.b));
               }});
  c.push_back({"BSA SE folder count 0xFFFFFFFF is refused", [] {
                 auto l = single(105, kStd, pattern(10, 29), false);
                 l.w.put32(16, 0xFFFFFFFF);
                 damaged(save(l.w.b));
               }});
  c.push_back({"BSA folder's file count 0x7FFFFFFF is refused", [] {
                 auto l = single(104, kStd, pattern(10, 30), false);
                 l.w.put32(l.folderRecord[0] + 8, 0x7FFFFFFF);
                 damaged(save(l.w.b));
               }});
  c.push_back({"BSA folders' file counts over the header's file count are refused", [] {
                 // 2000 folder records all pointing at one block of 2000 file
                 // records: 4 million File objects from a 100 KB archive
                 Writer w;
                 const uint32_t n = 2000;
                 w.str(std::string("BSA\0", 4));
                 w.u32(104);
                 w.u32(36);
                 w.u32(kStd);
                 w.u32(n);
                 w.u32(n);
                 w.u32(2);
                 w.u32(0);
                 w.u32(0);
                 const size_t block = 36 + 16 * n;
                 for (uint32_t i = 0; i < n; ++i) {
                   w.u64(i);
                   w.u32(n);
                   w.u32(static_cast<uint32_t>(block));
                 }
                 w.u8(2);
                 w.zstr("d");
                 for (uint32_t i = 0; i < n; ++i) {
                   w.u64(0);
                   w.u32(1);
                   w.u32(0);
                 }
                 const auto o = open(save(w.b));
                 check(o.refused ||
                           o.archive->getRoot()->countFiles() <= n,
                       "refused, or no more files than the header says");
               }});
  c.push_back({"BSA header file count smaller than the folders' is refused", [] {
                 auto l = bsa(104, kStd, {{"d", {{"a.txt", pattern(5, 31), false},
                                                 {"b.txt", pattern(5, 32), false}}}});
                 l.w.put32(20, 1);
                 const auto o = open(save(l.w.b));
                 check(o.refused || o.archive->getRoot()->countFiles() <= 1,
                       "refused, or no more files than the header says");
               }});
  c.push_back({"BSA folder with zero files reads", [] {
                 std::vector<BsaFolder> f = {{"empty", {}}, {"d", {{"a.txt", pattern(5, 33), false}}}};
                 wellFormed(save(bsa(104, kStd, f).w.b), expect(f));
               }});
  c.push_back({"BA2 file count 0xFFFFFFFF is refused", [] {
                 auto l = ba2General({{"a.txt", pattern(5, 34), false}});
                 l.w.put32(12, 0xFFFFFFFF);
                 damaged(save(l.w.b, "ba2"));
               }});
  c.push_back({"BA2 file count over its records/names is refused", [] {
                 auto l = ba2General({{"a.txt", pattern(5, 35), false}});
                 l.w.put32(12, 2);
                 damaged(save(l.w.b, "ba2"));
               }});
  c.push_back({"BA2 of an unknown type (not GNRL/DX10) lists nothing", [] {
                 auto l = ba2General({{"a.txt", pattern(5, 36), false}});
                 std::memcpy(&l.w.b[8], "ABCD", 4);
                 const auto o = open(save(l.w.b, "ba2"));
                 check(o.refused || files(*o.archive).empty(), "nothing listed");
               }});
  c.push_back({"unknown BSA version is refused", [] {
                 auto l = single(104, kStd, pattern(5, 37), false);
                 l.w.put32(4, 106);
                 const auto o = open(save(l.w.b));
                 check(o.refused, "refused");
               }});
  c.push_back({"truncated header is refused", [] {
                 auto l = single(104, kStd, pattern(5, 38), false);
                 l.w.b.resize(20);
                 check(open(save(l.w.b)).refused, "BSA refused");
                 auto b = ba2General({{"a.txt", pattern(5, 38), false}});
                 b.w.b.resize(14);
                 check(open(save(b.w.b, "ba2")).refused, "BA2 refused");
               }});

  // --- offsets and sizes --------------------------------------------------
  c.push_back({"BSA data offset past the end is an error", [] {
                 auto l = single(104, kStd, pattern(50, 39), false);
                 l.w.put32(l.fileRecord[0] + 12, 0x7FFFFFF0);
                 damaged(save(l.w.b));
               }});
  c.push_back({"BSA data running past the end is an error", [] {
                 auto l = single(104, kStd, pattern(50, 40), false);
                 l.w.put32(l.fileRecord[0] + 8, 5000);
                 damaged(save(l.w.b));
               }});
  c.push_back({"BSA size 0x3FFFFFFF (1 GB) in a small archive is an error, no huge allocation", [] {
                 auto l = single(104, kStd, pattern(50, 41), false);
                 l.w.put32(l.fileRecord[0] + 8, 0x3FFFFFFF);
                 damaged(save(l.w.b));
               }});
  c.push_back({"BSA SE data offset past the end is an error", [] {
                 auto l = single(105, kStd | kCompressed, pattern(50, 42), true);
                 l.w.put32(l.fileRecord[0] + 12, 0x7FFFFFF0);
                 damaged(save(l.w.b));
               }});
  c.push_back({"BSA folder record offset past the end is refused", [] {
                 auto l = single(104, kStd, pattern(50, 43), false);
                 l.w.put32(l.folderRecord[0] + 12, 0x7FFFFFF0);
                 damaged(save(l.w.b));
               }});
  c.push_back({"BSA folder record offset below the names' length is refused", [] {
                 auto l = single(104, kStd, pattern(50, 44), false);
                 l.w.put32(l.folderRecord[0] + 12, 1);
                 damaged(save(l.w.b));
               }});
  c.push_back({"BSA SE folder record offset 2^63 is refused", [] {
                 auto l = single(105, kStd, pattern(50, 45), false);
                 l.w.put64(l.folderRecord[0] + 16, 0x8000000000000000ull);
                 damaged(save(l.w.b));
               }});
  c.push_back({"two files sharing one data block extract the same bytes", [] {
                 auto l = bsa(104, kStd, {{"d", {{"a.txt", pattern(64, 46), false},
                                                 {"b.txt", pattern(64, 47), false}}}});
                 l.w.put32(l.fileRecord[1] + 12, static_cast<uint32_t>(l.data[0]));
                 const Bytes a = pattern(64, 46);
                 wellFormed(save(l.w.b), {{"d\\a.txt", a}, {"d\\b.txt", a}});
               }});
  c.push_back({"file data overlapping the header: no crash", [] {
                 auto l = single(104, kStd, pattern(64, 48), false);
                 l.w.put32(l.fileRecord[0] + 12, 0);
                 survives(save(l.w.b));
               }});
  c.push_back({"BA2 GNRL data offset past the end is an error", [] {
                 auto l = ba2General({{"a.txt", pattern(50, 49), false}});
                 l.w.put64(l.record[0] + 16, 0xFFFFFFFFFFull);
                 damaged(save(l.w.b, "ba2"));
               }});
  c.push_back({"BA2 GNRL stored size 0xFFFFFFF0 is an error, no huge allocation", [] {
                 auto l = ba2General({{"a.txt", pattern(50, 50), false}});
                 l.w.put32(l.record[0] + 28, 0xFFFFFFF0);
                 damaged(save(l.w.b, "ba2"));
               }});
  c.push_back({"BA2 GNRL packed size 0xFFFFFFF0 is an error, no huge allocation", [] {
                 auto l = ba2General({{"a.txt", pattern(5000, 51), true}});
                 l.w.put32(l.record[0] + 24, 0xFFFFFFF0);
                 damaged(save(l.w.b, "ba2"));
               }});
  c.push_back({"BA2 name table offset past the end is refused", [] {
                 auto l = ba2General({{"a.txt", pattern(5, 52), false}});
                 l.w.put64(16, 0x7FFFFFFFFFFFull);
                 damaged(save(l.w.b, "ba2"));
               }});
  c.push_back({"BA2 name length past the end is refused", [] {
                 auto l = ba2General({{"a.txt", pattern(5, 53), false}});
                 l.w.b.resize(l.w.b.size() - 2);  // the name is cut
                 damaged(save(l.w.b, "ba2"));
               }});

  // --- compressed data ----------------------------------------------------
  c.push_back({"BSA 104 zlib stream truncated is an error", [] {
                 auto l = single(104, kStd | kCompressed, pattern(20000, 54), true);
                 const uint32_t size = static_cast<uint32_t>(l.w.b.size() - l.data[0]);
                 l.w.put32(l.fileRecord[0] + 8, size / 2);
                 l.w.b.resize(l.data[0] + size / 2);
                 damaged(save(l.w.b));
               }});
  c.push_back({"BSA 104 zlib with a declared size larger than its data is an error", [] {
                 auto l = single(104, kStd | kCompressed, pattern(2000, 55), true);
                 l.w.put32(l.data[0], 4000);
                 damaged(save(l.w.b));
               }});
  c.push_back({"BSA 104 zlib with a declared size smaller than its data is an error", [] {
                 auto l = single(104, kStd | kCompressed, pattern(2000, 56), true);
                 l.w.put32(l.data[0], 1000);
                 damaged(save(l.w.b));
               }});
  c.push_back({"BSA 104 zlib bomb (declared 0xFFFFFFF0) is an error, no huge allocation", [] {
                 auto l = single(104, kStd | kCompressed, pattern(2000, 57), true);
                 l.w.put32(l.data[0], 0xFFFFFFF0);
                 damaged(save(l.w.b));
               }});
  c.push_back({"BSA 104 zlib of real 200 MB declared as such still too big for its data", [] {
                 // 600 bytes can't inflate to 200 MB (zlib's ratio is < 1032:1)
                 auto l = single(104, kStd | kCompressed, pattern(2000, 58), true);
                 l.w.put32(l.data[0], 200u << 20);
                 damaged(save(l.w.b));
               }});
  c.push_back({"BSA 104 compressed record shorter than its size field is an error", [] {
                 auto l = single(104, kStd | kCompressed, pattern(2000, 59), true);
                 l.w.put32(l.fileRecord[0] + 8, 2);
                 damaged(save(l.w.b));
               }});
  c.push_back({"BSA 104 compressed record of garbage is an error", [] {
                 auto l = single(104, kStd | kCompressed, pattern(2000, 60), true);
                 for (size_t i = l.data[0] + 4; i < l.w.b.size(); ++i) {
                   l.w.b[i] = static_cast<uint8_t>(i * 7);
                 }
                 damaged(save(l.w.b));
               }});
  c.push_back({"BSA 103 zlib truncated is an error", [] {
                 auto l = single(103, kStd | kCompressed, pattern(20000, 61), true);
                 const uint32_t size = static_cast<uint32_t>(l.w.b.size() - l.data[0]);
                 l.w.put32(l.fileRecord[0] + 8, size / 3);
                 l.w.b.resize(l.data[0] + size / 3);
                 damaged(save(l.w.b));
               }});
  c.push_back({"BSA 105 LZ4 frame truncated is an error", [] {
                 auto l = single(105, kStd | kCompressed, pattern(20000, 62), true);
                 const uint32_t size = static_cast<uint32_t>(l.w.b.size() - l.data[0]);
                 l.w.put32(l.fileRecord[0] + 8, size / 2);
                 l.w.b.resize(l.data[0] + size / 2);
                 damaged(save(l.w.b));
               }});
  c.push_back({"BSA 105 LZ4 with a declared size larger than its data is an error", [] {
                 auto l = single(105, kStd | kCompressed, pattern(2000, 63), true);
                 l.w.put32(l.data[0], 4000);
                 damaged(save(l.w.b));
               }});
  c.push_back({"BSA 105 LZ4 with a declared size smaller than its data is an error", [] {
                 auto l = single(105, kStd | kCompressed, pattern(2000, 64), true);
                 l.w.put32(l.data[0], 1000);
                 damaged(save(l.w.b));
               }});
  c.push_back({"BSA 105 LZ4 bomb (declared 0xFFFFFFF0) is an error, no huge allocation", [] {
                 auto l = single(105, kStd | kCompressed, pattern(2000, 65), true);
                 l.w.put32(l.data[0], 0xFFFFFFF0);
                 damaged(save(l.w.b));
               }});
  c.push_back({"BSA 105 compressed record shorter than its size field is an error", [] {
                 auto l = single(105, kStd | kCompressed, pattern(2000, 66), true);
                 l.w.put32(l.fileRecord[0] + 8, 2);
                 damaged(save(l.w.b));
               }});
  c.push_back({"BSA 105 LZ4 garbage is an error", [] {
                 auto l = single(105, kStd | kCompressed, pattern(2000, 67), true);
                 for (size_t i = l.data[0] + 4; i < l.w.b.size(); ++i) {
                   l.w.b[i] = static_cast<uint8_t>(i * 13);
                 }
                 damaged(save(l.w.b));
               }});
  c.push_back({"BA2 GNRL zlib truncated is an error", [] {
                 auto l = ba2General({{"a.txt", pattern(20000, 68), true}});
                 const uint32_t packed = *reinterpret_cast<uint32_t*>(&l.w.b[l.record[0] + 24]);
                 l.w.put32(l.record[0] + 24, packed / 2);
                 damaged(save(l.w.b, "ba2"));
               }});
  c.push_back({"BA2 GNRL zlib with a wrong unpacked size is an error", [] {
                 auto l = ba2General({{"a.txt", pattern(2000, 69), true}});
                 l.w.put32(l.record[0] + 28, 3000);
                 damaged(save(l.w.b, "ba2"));
               }});
  c.push_back({"BA2 GNRL zlib bomb (unpacked 0xFFFFFFF0) is an error, no huge allocation", [] {
                 auto l = ba2General({{"a.txt", pattern(2000, 70), true}});
                 l.w.put32(l.record[0] + 28, 0xFFFFFFF0);
                 damaged(save(l.w.b, "ba2"));
               }});

  // --- DX10 chunk tables and header fields --------------------------------
  c.push_back({"DX10 zlib chunk of garbage is an error", [] {
                 auto l = ba2Texture("t.dds", {{pattern(4000, 71), true}});
                 for (size_t i = l.data[0]; i < l.data[0] + 40; ++i) {
                   l.w.b[i] = static_cast<uint8_t>(i * 3);
                 }
                 damaged(save(l.w.b, "ba2"));
               }});
  c.push_back({"DX10 zlib chunk truncated is an error", [] {
                 auto l = ba2Texture("t.dds", {{pattern(4000, 72), true}});
                 const size_t chunk = l.record[0] + 24;
                 const uint32_t packed = *reinterpret_cast<uint32_t*>(&l.w.b[chunk + 8]);
                 l.w.put32(chunk + 8, packed / 2);
                 damaged(save(l.w.b, "ba2"));
               }});
  c.push_back({"DX10 chunk unpacked 0xFFFFFFF0 (bomb) is an error, no huge allocation", [] {
                 auto l = ba2Texture("t.dds", {{pattern(4000, 73), true}});
                 l.w.put32(l.record[0] + 24 + 12, 0xFFFFFFF0);
                 damaged(save(l.w.b, "ba2"));
               }});
  c.push_back({"DX10 stored chunk past the end is an error", [] {
                 auto l = ba2Texture("t.dds", {{pattern(4000, 74), false}});
                 l.w.put64(l.record[0] + 24, 0xFFFFFFFFFFull);
                 damaged(save(l.w.b, "ba2"));
               }});
  c.push_back({"DX10 stored chunk 0xFFFFFFF0 bytes is an error, no huge allocation", [] {
                 auto l = ba2Texture("t.dds", {{pattern(4000, 75), false}});
                 l.w.put32(l.record[0] + 24 + 12, 0xFFFFFFF0);
                 damaged(save(l.w.b, "ba2"));
               }});
  c.push_back({"DX10 chunks whose sizes add past 4 GB are an error", [] {
                 auto l = ba2Texture("t.dds", {{pattern(100, 76), false}, {pattern(100, 77), false}});
                 l.w.put32(l.record[0] + 24 + 12, 0x80000000);
                 l.w.put32(l.record[0] + 48 + 12, 0x80000010);
                 damaged(save(l.w.b, "ba2"));
               }});
  c.push_back({"DX10 chunk table cut short is refused", [] {
                 auto l = ba2Texture("t.dds", {{pattern(100, 78), false}});
                 l.w.b[l.record[0] + 13] = 200;  // 200 chunks, one in the file
                 damaged(save(l.w.b, "ba2"));
               }});
  c.push_back({"Starfield LZ4 chunk of garbage is an error", [] {
                 auto l = ba2Texture("t.dds", {{pattern(4000, 79), true}}, kBC1, 3);
                 for (size_t i = l.data[0]; i < l.data[0] + 40; ++i) {
                   l.w.b[i] = 0xFF;
                 }
                 damaged(save(l.w.b, "ba2"));
               }});
  c.push_back({"Starfield LZ4 chunk with a wrong unpacked size is an error", [] {
                 auto l = ba2Texture("t.dds", {{pattern(4000, 80), true}}, kBC1, 3);
                 l.w.put32(l.record[0] + 24 + 12, 5000);
                 damaged(save(l.w.b, "ba2"));
               }});
  c.push_back({"DX10 odd header fields (format, sizes, header size): no crash", [] {
                 auto l = ba2Texture("t.dds", {{pattern(400, 81), false}});
                 l.w.put16(l.record[0] + 14, 0xFFFF);  // chunk header size
                 l.w.put16(l.record[0] + 16, 0xFFFF);  // height
                 l.w.put16(l.record[0] + 18, 0xFFFF);  // width
                 l.w.b[l.record[0] + 20] = 0xFF;       // mips
                 l.w.b[l.record[0] + 21] = 0xFF;       // format
                 l.w.b[l.record[0] + 22] = 0xFF;       // cubemap
                 l.w.put16(l.record[0] + 24 + 16, 9);  // start mip > end mip
                 survives(save(l.w.b, "ba2"));
               }});

  // --- extraction to disk -------------------------------------------------
  auto escape = [](const std::string& folder, const std::string& name) {
    return [folder, name] {
      survives(save(single(104, kStd, pattern(20, 82), false, folder, name).w.b));
    };
  };
  c.push_back({"folder ..\\..\\ doesn't escape the target", escape("..\\..", "mo2r_escape1.txt")});
  c.push_back({"folder with ..\\ in the middle doesn't escape", escape("a\\..\\..\\..", "mo2r_escape2.txt")});
  c.push_back({"folder ... and '.. ' don't escape", escape("...\\.. \\.. .", "mo2r_escape3.txt")});
  c.push_back({"forward slashes ../ don't escape", escape("a/../../..", "mo2r_escape4.txt")});
  c.push_back({"file name ..\\ doesn't escape", escape("d", "..\\..\\mo2r_escape5.txt")});
  c.push_back({"absolute folder C:\\ doesn't escape", escape("C:\\", "mo2r_escape6.txt")});
  c.push_back({"drive-relative C:name doesn't escape", escape("d", "C:mo2r_escape7.txt")});
  c.push_back({"UNC-like \\\\?\\C:\\ doesn't escape", escape("\\\\?\\C:", "mo2r_escape8.txt")});
  c.push_back({"NTFS stream name:stream doesn't escape", escape("d", "mo2r_escape9.txt:x")});
  c.push_back({"BA2 name ..\\..\\x doesn't escape", [] {
                 survives(save(ba2General({{"..\\..\\mo2r_escape10.txt", pattern(5, 83), false},
                                           {"C:\\mo2r_escape11.txt", pattern(5, 84), false}})
                                   .w.b,
                               "ba2"));
               }});
  // a device name opens the device, not a file in the target: either an
  // error, or (where Windows no longer treats it as a device) a plain file of
  // that name inside the target
  auto reserved = [](const std::string& name) {
    return [name] {
      auto p   = save(single(104, kStd, pattern(20, 85), false, "d", name).w.b);
      Opened o = open(p);
      if (o.refused) {
        check(true, "refused");
        return;
      }
      auto list = files(*o.archive);
      auto inTarget = [&](const fs::path& dir) {
        std::error_code ec;
        for (auto it = fs::directory_iterator(dir, ec); !ec && it != fs::directory_iterator();
             it.increment(ec)) {
          if (it->path().filename() == wide(name) && it->is_regular_file()) {
            return true;
          }
        }
        return false;
      };
      const bool single = extractFails(*o.archive, list[0], target());
      check(single || inTarget(target()), "extract(): an error or a file in the target");
      Opened o2      = open(p);
      const bool all = extractAllFails(*o2.archive, target() / "all");
      check(all || inTarget(target() / "all" / "d"),
            "extractAll(): an error or a file in the target");
    };
  };
  c.push_back({"reserved name CON: an error or a file in the target", reserved("CON")});
  c.push_back({"reserved name nul.txt: an error or a file in the target", reserved("nul.txt")});
  c.push_back({"reserved name com1: an error or a file in the target", reserved("com1")});
  c.push_back({"reserved name 'AUX .dds': an error or a file in the target", reserved("AUX .dds")});
  c.push_back({"reserved folder name PRN: an error or a file in the target", [] {
                 auto p   = save(single(104, kStd, pattern(20, 86), false, "PRN", "a.txt").w.b);
                 Opened o = open(p);
                 check(o.refused || extractAllFails(*o.archive, target()) ||
                           slurp(target() / "PRN" / "a.txt") == pattern(20, 86),
                       "extractAll(): an error or the file in the target");
               }});
  c.push_back({"extractAll to a missing folder is an error, no crash", [] {
                 auto p   = save(single(104, kStd, pattern(20, 87), false).w.b);
                 Opened o = open(p);
                 try {
                   const auto r = o.archive->extractAll(target() / "missing" / "deeper", &progress);
                   check(r != BSA::ERROR_NONE, "error reported");
                 } catch (const std::exception&) {
                   check(true, "error reported (exception)");
                 }
               }});
  c.push_back({"extractAll stops when the progress callback says so", [] {
                 std::vector<BsaFolder> f(1);
                 f[0].name = "d";
                 for (int i = 0; i < 300; ++i) {
                   f[0].files.push_back({"f" + std::to_string(i) + ".txt", pattern(3000, i), true});
                 }
                 auto p   = save(bsa(104, kStd | kCompressed, f).w.b);
                 Opened o = open(p);
                 fs::create_directories(target());
                 const auto r =
                     o.archive->extractAll(target(), [](int, std::string) { return false; });
                 (void)r;
                 check(true, "returns");
               }});

  return c;
}

// ---------------------------------------------------------------------------
// parent: one child per case, with a time limit

LONG WINAPI crashed(EXCEPTION_POINTERS* info)
{
  std::printf("      crash: exception 0x%08lX\n", info->ExceptionRecord->ExceptionCode);
  std::fflush(stdout);
  ::TerminateProcess(::GetCurrentProcess(), info->ExceptionRecord->ExceptionCode);
  return EXCEPTION_EXECUTE_HANDLER;
}

void quiet()
{
  // no crash dialogs or WER UI: a failing case must not take the foreground
  ::SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX | SEM_NOOPENFILEERRORBOX);
  ::WerSetFlags(WER_FAULT_REPORTING_NO_UI);
  _set_abort_behavior(0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);
  _CrtSetReportMode(_CRT_ASSERT, 0);
  std::signal(SIGABRT, [](int) {
    std::printf("      abort (std::terminate: an exception left a thread?)\n");
    std::fflush(stdout);
    ::TerminateProcess(::GetCurrentProcess(), 4);
  });
  _set_invalid_parameter_handler(
      [](const wchar_t*, const wchar_t*, const wchar_t*, unsigned int, uintptr_t) {
        std::printf("      CRT invalid parameter\n");
        std::fflush(stdout);
        ::TerminateProcess(::GetCurrentProcess(), 5);
      });
  ::SetUnhandledExceptionFilter(&crashed);
}

// files a case left outside its target folder (or anywhere above it)
std::vector<fs::path> escapes(const fs::path& caseDir)
{
  std::vector<fs::path> out;
  std::error_code ec;
  for (auto it = fs::recursive_directory_iterator(caseDir, ec);
       it != fs::recursive_directory_iterator(); it.increment(ec)) {
    const auto rel = fs::relative(it->path(), caseDir, ec).wstring();
    if (rel.rfind(L"target", 0) != 0 && rel.rfind(L"archive.", 0) != 0) {
      out.push_back(it->path());
    }
  }
  for (fs::path up = caseDir.parent_path();; up = up.parent_path()) {
    for (auto it = fs::directory_iterator(up, ec); !ec && it != fs::directory_iterator();
         it.increment(ec)) {
      if (it->path().filename().wstring().rfind(L"mo2r_escape", 0) == 0) {
        out.push_back(it->path());
      }
    }
    if (up == up.root_path()) {
      break;
    }
  }
  return out;
}

int runParent(const fs::path& work, const wchar_t* self)
{
  const size_t count = cases().size();
  int failed         = 0;
  for (size_t i = 0; i < count; ++i) {
    const fs::path caseDir = work / ("c" + std::to_string(i));
    std::error_code ec;
    fs::remove_all(caseDir, ec);
    fs::create_directories(caseDir / "target");

    std::printf("[%zu] %s\n", i, cases()[i].name);
    std::fflush(stdout);
    std::wstring cmd = L"\"" + std::wstring(self) + L"\" \"" + work.wstring() + L"\" --case " +
                       std::to_wstring(i);
    STARTUPINFOW si        = {sizeof(si)};
    PROCESS_INFORMATION pi = {};
    if (!::CreateProcessW(self, cmd.data(), nullptr, nullptr, TRUE, 0, nullptr, nullptr, &si,
                          &pi)) {
      std::printf("      FAIL can't start the child\n");
      ++failed;
      continue;
    }
    DWORD code = 0;
    if (::WaitForSingleObject(pi.hProcess, 60000) == WAIT_TIMEOUT) {
      ::TerminateProcess(pi.hProcess, 0xDEAD);
      ::WaitForSingleObject(pi.hProcess, INFINITE);
      std::printf("      FAIL hang (over 60 s)\n");
      code = 0xDEAD;
    } else {
      ::GetExitCodeProcess(pi.hProcess, &code);
    }
    ::CloseHandle(pi.hThread);
    ::CloseHandle(pi.hProcess);

    const auto out = escapes(caseDir);
    for (const auto& p : out) {
      std::printf("      FAIL written outside the target: %ls\n", p.c_str());
      fs::remove_all(p, ec);
    }
    if (code != 0 || !out.empty()) {
      if (code == 3) {
        std::printf("      FAIL huge allocation\n");
      } else if (code != 1 && code != 0xDEAD && code != 0) {
        std::printf("      FAIL child died (exit 0x%08lX)\n", code);
      }
      ++failed;
    }
  }
  std::printf("\n%zu cases, %d failed\n", count, failed);
  return failed == 0 ? 0 : 1;
}
}  // namespace

int wmain(int argc, wchar_t** argv)
{
  std::setvbuf(stdout, nullptr, _IONBF, 0);
  if (argc < 2) {
    std::printf("usage: bsatk_reader_test <work folder> [--case n]\n");
    return 2;
  }
  const fs::path work = fs::absolute(argv[1]);
  fs::create_directories(work);
  quiet();
  if (argc >= 4 && std::wcscmp(argv[2], L"--case") == 0) {
    const size_t i = std::wcstoul(argv[3], nullptr, 10);
    const auto all = cases();
    if (i >= all.size()) {
      return 2;
    }
    g_dir = work / ("c" + std::to_string(i));
    fs::create_directories(target());
    all[i].run();
    return failures == 0 ? 0 : 1;
  }
  return runParent(work, argv[0]);
}
