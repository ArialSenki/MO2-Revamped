#include "importpatch.h"

#include <QByteArray>
#include <QDir>
#include <QtEndian>

#include <algorithm>

namespace ImportPatch
{

namespace
{

using NtQueryInformationProcessFn = LONG(NTAPI*)(HANDLE, ULONG, PVOID, ULONG, PULONG);

struct BasicInformation
{
  PVOID reserved1;
  PVOID pebBaseAddress;
  PVOID reserved2[2];
  ULONG_PTR uniqueProcessId;
  PVOID reserved3;
};

constexpr ULONG ProcessBasicInformationClass = 0;
constexpr ULONG ProcessWow64InformationClass = 26;

bool read(HANDLE process, quint64 address, void* buffer, size_t size)
{
  SIZE_T done = 0;
  return ::ReadProcessMemory(process, reinterpret_cast<LPCVOID>(address), buffer, size,
                             &done) &&
         done == size;
}

bool write(HANDLE process, quint64 address, const void* buffer, size_t size)
{
  SIZE_T done = 0;
  return ::WriteProcessMemory(process, reinterpret_cast<LPVOID>(address), buffer, size,
                              &done) &&
         done == size;
}

bool readString(HANDLE process, quint64 address, QByteArray* out)
{
  out->clear();
  for (int i = 0; i < 512; ++i) {
    char c = 0;
    if (!read(process, address + i, &c, 1)) {
      return false;
    }
    if (c == 0) {
      return true;
    }
    out->append(c);
  }
  return false;
}

// the address the main image is mapped at, from the process' PEB (the 32-bit
// one for a WOW64 process)
bool mainImage(HANDLE process, bool* is32, quint64* base, QString* error)
{
  BOOL wow64 = FALSE;
  if (!::IsWow64Process(process, &wow64)) {
    *error = QStringLiteral("IsWow64Process failed (%1)").arg(::GetLastError());
    return false;
  }
  *is32 = (wow64 != FALSE);

#ifndef _WIN64
  if (!*is32) {
    *error = QStringLiteral("a 32-bit MO2 can't patch a 64-bit process");
    return false;
  }
#endif

  const auto query = reinterpret_cast<NtQueryInformationProcessFn>(::GetProcAddress(
      ::GetModuleHandleW(L"ntdll.dll"), "NtQueryInformationProcess"));
  if (query == nullptr) {
    *error = QStringLiteral("NtQueryInformationProcess isn't there");
    return false;
  }

  if (*is32) {
    ULONG_PTR peb32 = 0;
    if (query(process, ProcessWow64InformationClass, &peb32, sizeof(peb32), nullptr) !=
            0 ||
        peb32 == 0) {
      *error = QStringLiteral("can't find the 32-bit PEB");
      return false;
    }
    quint32 image = 0;
    if (!read(process, quint64(peb32) + 0x08, &image, sizeof(image))) {
      *error = QStringLiteral("can't read the 32-bit PEB");
      return false;
    }
    *base = image;
  } else {
    BasicInformation info = {};
    if (query(process, ProcessBasicInformationClass, &info, sizeof(info), nullptr) != 0 ||
        info.pebBaseAddress == nullptr) {
      *error = QStringLiteral("can't find the PEB");
      return false;
    }
    quint64 image = 0;
    if (!read(process, reinterpret_cast<quint64>(info.pebBaseAddress) + 0x10, &image,
              sizeof(image))) {
      *error = QStringLiteral("can't read the PEB");
      return false;
    }
    *base = image;
  }

  if (*base == 0) {
    *error = QStringLiteral("the PEB has no image base");
    return false;
  }
  return true;
}

// memory after the image, near enough that its RVAs (32 bits, positive) reach it
quint64 allocateAfterImage(HANDLE process, quint64 base, quint32 sizeOfImage,
                           size_t size, bool is32)
{
  const quint64 granularity = 0x10000;
  quint64 address = (base + sizeOfImage + granularity - 1) & ~(granularity - 1);
  const quint64 limit = std::min<quint64>(base + 0x7FFF0000ull,
                                          is32 ? 0xFFFF0000ull : ~0ull);

  while (address + size < limit) {
    MEMORY_BASIC_INFORMATION mbi = {};
    if (::VirtualQueryEx(process, reinterpret_cast<LPCVOID>(address), &mbi,
                         sizeof(mbi)) == 0) {
      return 0;
    }
    const quint64 regionBase = reinterpret_cast<quint64>(mbi.BaseAddress);
    const quint64 regionEnd  = regionBase + mbi.RegionSize;
    if (mbi.State == MEM_FREE && regionEnd - address >= size) {
      if (void* p = ::VirtualAllocEx(process, reinterpret_cast<LPVOID>(address), size,
                                     MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE)) {
        return reinterpret_cast<quint64>(p);
      }
    }
    address = (std::max(regionEnd, address + 1) + granularity - 1) & ~(granularity - 1);
  }
  return 0;
}

void put32(QByteArray& b, int offset, quint32 v)
{
  qToLittleEndian<quint32>(v, reinterpret_cast<uchar*>(b.data()) + offset);
}

void put64(QByteArray& b, int offset, quint64 v)
{
  qToLittleEndian<quint64>(v, reinterpret_cast<uchar*>(b.data()) + offset);
}

int appendData(QByteArray& b, const QByteArray& data, int align = 2)
{
  while (b.size() % align) {
    b.append('\0');
  }
  const int offset = int(b.size());
  b.append(data);
  b.append('\0');
  return offset;
}

}  // namespace

QString importablePath(const QString& path)
{
  const QString nativePath = QDir::toNativeSeparators(path);
  auto ascii               = [](const QString& s) {
    return std::all_of(s.begin(), s.end(), [](QChar c) {
      return c.unicode() > 0 && c.unicode() < 128;
    });
  };
  if (ascii(nativePath)) {
    return nativePath;
  }
  const std::wstring wide = nativePath.toStdWString();
  wchar_t buffer[MAX_PATH * 2];
  const DWORD n = ::GetShortPathNameW(wide.c_str(), buffer, DWORD(std::size(buffer)));
  if (n > 0 && n < std::size(buffer)) {
    const QString shortPath = QString::fromWCharArray(buffer, int(n));
    if (ascii(shortPath)) {
      return shortPath;
    }
  }
  return {};
}

bool patchSuspendedProcess(HANDLE process, const std::vector<Redirect>& redirects,
                           const std::vector<Preload>& preloads, QString* error)
{
  QString dummy;
  if (error == nullptr) {
    error = &dummy;
  }

  bool is32    = false;
  quint64 base = 0;
  if (!mainImage(process, &is32, &base, error)) {
    return false;
  }

  // the headers
  QByteArray headers(0x1000, '\0');
  if (!read(process, base, headers.data(), size_t(headers.size()))) {
    *error = QStringLiteral("can't read the image headers");
    return false;
  }
  const auto* h  = reinterpret_cast<const uchar*>(headers.constData());
  auto u16       = [&](int o) { return qFromLittleEndian<quint16>(h + o); };
  auto u32       = [&](int o) { return qFromLittleEndian<quint32>(h + o); };
  const int nt   = int(u32(0x3C));
  if (u16(0) != 0x5A4D || nt <= 0 || nt > 0x1000 - 0x200 || u32(nt) != 0x00004550) {
    *error = QStringLiteral("the image has no PE headers");
    return false;
  }
  const int opt       = nt + 24;
  const quint16 magic = u16(opt);
  if ((magic == 0x10b) != is32 || (magic != 0x10b && magic != 0x20b)) {
    *error = QStringLiteral("the image's PE type doesn't match the process");
    return false;
  }
  const quint32 sizeOfImage = u32(opt + 56);
  const int dirs            = opt + (is32 ? 96 : 112);
  const quint32 numDirs     = u32(opt + (is32 ? 92 : 108));
  if (numDirs < 2) {
    *error = QStringLiteral("the image has no import directory");
    return false;
  }
  const quint32 importRva = u32(dirs + 8);
  if (importRva == 0) {
    *error = QStringLiteral("the image imports nothing");
    return false;
  }

  // the descriptors as they are
  struct Descriptor
  {
    quint32 fields[5];
    QByteArray name;
  };
  std::vector<Descriptor> originals;
  for (int i = 0; i < 4096; ++i) {
    Descriptor d = {};
    if (!read(process, base + importRva + quint64(i) * 20, d.fields, 20)) {
      *error = QStringLiteral("can't read the import descriptors");
      return false;
    }
    if (d.fields[0] == 0 && d.fields[3] == 0 && d.fields[4] == 0) {
      break;
    }
    if (!readString(process, base + d.fields[3], &d.name)) {
      *error = QStringLiteral("can't read an import name");
      return false;
    }
    originals.push_back(d);
  }

  // the redirected names and the preloads, as the loader reads them
  std::vector<QByteArray> redirectPaths;
  for (const auto& r : redirects) {
    const QString p = importablePath(r.path);
    if (p.isEmpty()) {
      *error = QStringLiteral("%1 has no ASCII path").arg(r.path);
      return false;
    }
    redirectPaths.push_back(p.toLatin1());
    const bool imported =
        std::any_of(originals.begin(), originals.end(), [&](const Descriptor& d) {
          return QString::fromLatin1(d.name).compare(r.dllName, Qt::CaseInsensitive) == 0;
        });
    if (!imported) {
      *error = QStringLiteral("the image doesn't import %1").arg(r.dllName);
      return false;
    }
  }
  std::vector<QByteArray> preloadPaths;
  for (const auto& p : preloads) {
    const QString path = importablePath(p.path);
    if (path.isEmpty()) {
      *error = QStringLiteral("%1 has no ASCII path").arg(p.path);
      return false;
    }
    if (p.exportName.isEmpty() && p.exportOrdinal == 0) {
      *error = QStringLiteral("%1 exports nothing to import").arg(p.path);
      return false;
    }
    preloadPaths.push_back(path.toLatin1());
  }

  // the new block: descriptors (preloads first), the preloads' thunks, names
  const int ptr        = is32 ? 4 : 8;
  const int count      = int(preloads.size() + originals.size() + 1);
  const int descBytes  = count * 20;
  const int thunksAt   = (descBytes + 7) & ~7;
  const int thunkBytes = int(preloads.size()) * 4 * ptr;
  QByteArray block(thunksAt + thunkBytes, '\0');

  struct PreloadOffsets
  {
    int name, hintName;
  };
  std::vector<PreloadOffsets> preloadOffsets;
  for (size_t i = 0; i < preloads.size(); ++i) {
    PreloadOffsets o;
    o.name     = appendData(block, preloadPaths[i]);
    o.hintName = -1;
    if (!preloads[i].exportName.isEmpty()) {
      // IMAGE_IMPORT_BY_NAME: a hint, then the name
      o.hintName = appendData(block, QByteArray(2, '\0') + preloads[i].exportName.toLatin1());
    }
    preloadOffsets.push_back(o);
  }
  std::vector<int> redirectOffsets;
  for (const auto& p : redirectPaths) {
    redirectOffsets.push_back(appendData(block, p));
  }

  const size_t blockSize = size_t(block.size());
  const quint64 address  = allocateAfterImage(process, base, sizeOfImage, blockSize, is32);
  if (address == 0) {
    *error = QStringLiteral("no memory within reach of the image");
    return false;
  }
  const quint64 blockRva = address - base;
  auto rva = [&](int offset) {
    return quint32(blockRva + quint64(offset));
  };

  int desc = 0;
  for (size_t i = 0; i < preloads.size(); ++i, desc += 20) {
    const int ilt = thunksAt + int(i) * 4 * ptr;
    const int iat = ilt + 2 * ptr;
    quint64 thunk = 0;
    if (preloadOffsets[i].hintName >= 0) {
      thunk = rva(preloadOffsets[i].hintName);
    } else {
      thunk = (is32 ? 0x80000000ull : 0x8000000000000000ull) | preloads[i].exportOrdinal;
    }
    if (is32) {
      put32(block, ilt, quint32(thunk));
      put32(block, iat, quint32(thunk));
    } else {
      put64(block, ilt, thunk);
      put64(block, iat, thunk);
    }
    put32(block, desc + 0, rva(ilt));
    put32(block, desc + 12, rva(preloadOffsets[i].name));
    put32(block, desc + 16, rva(iat));
  }
  for (const auto& d : originals) {
    for (int f = 0; f < 5; ++f) {
      put32(block, desc + f * 4, d.fields[f]);
    }
    for (size_t r = 0; r < redirects.size(); ++r) {
      if (QString::fromLatin1(d.name).compare(redirects[r].dllName, Qt::CaseInsensitive) ==
          0) {
        put32(block, desc + 12, rva(redirectOffsets[r]));
      }
    }
    desc += 20;
  }

  auto release = [&] {
    ::VirtualFreeEx(process, reinterpret_cast<LPVOID>(address), 0, MEM_RELEASE);
  };

  if (!write(process, address, block.constData(), blockSize)) {
    *error = QStringLiteral("can't write the new import table (%1)").arg(::GetLastError());
    release();
    return false;
  }

  // the header's import directory points to the new table; the bound imports
  // (addresses resolved at link time against the system DLLs) are dropped so
  // the loader resolves everything
  const quint64 headerAddress = base + quint64(dirs);
  const size_t headerSpan     = size_t(numDirs > 11 ? 12 * 8 : 2 * 8);
  DWORD oldProtect            = 0;
  if (!::VirtualProtectEx(process, reinterpret_cast<LPVOID>(headerAddress), headerSpan,
                          PAGE_READWRITE, &oldProtect) &&
      !::VirtualProtectEx(process, reinterpret_cast<LPVOID>(headerAddress), headerSpan,
                          PAGE_WRITECOPY, &oldProtect)) {
    *error = QStringLiteral("can't unprotect the image headers (%1)").arg(::GetLastError());
    release();
    return false;
  }

  bool ok                    = true;
  const quint32 importDir[2] = {quint32(blockRva), quint32(descBytes)};
  ok = write(process, headerAddress + 8, importDir, sizeof(importDir));
  if (ok && numDirs > 11) {
    const quint32 none[2] = {0, 0};
    ok = write(process, headerAddress + 11 * 8, none, sizeof(none));
    if (!ok) {
      // put the import directory back as it was
      const quint32 old[2] = {importRva, u32(dirs + 12)};
      write(process, headerAddress + 8, old, sizeof(old));
    }
  }

  DWORD ignored = 0;
  ::VirtualProtectEx(process, reinterpret_cast<LPVOID>(headerAddress), headerSpan,
                     oldProtect, &ignored);

  if (!ok) {
    *error = QStringLiteral("can't write the image headers (%1)").arg(::GetLastError());
    release();
    return false;
  }

  return true;
}

}  // namespace ImportPatch
