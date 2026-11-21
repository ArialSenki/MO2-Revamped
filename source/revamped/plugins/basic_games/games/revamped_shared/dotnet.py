"""What a .NET assembly (a BepInEx plugin DLL) says about itself, read from its
metadata without loading it: the GUIDs of its [BepInPlugin] attributes and
the plugins its [BepInDependency] attributes require.

Only the PE headers, the CLI header, the metadata stream headers and the
#~, #Strings and #Blob streams are read (with seeks), never the whole file,
and only the answers are kept, so a list of hundreds of plugins (MMHOOK DLLs
of tens of MB among them) costs little time and memory.

Each custom attribute is followed from its CustomAttribute row to its
constructor (a MemberRef) and that constructor's type (a TypeRef): only
BepInEx.BepInPlugin and BepInEx.BepInDependency count. Blobs alone can't
tell: [GeneratedCode("tool", "1.0")] has the same shape as
[BepInDependency("guid", "1.0")], and library names in #Strings are shared
with other names by the compiler.
"""

from __future__ import annotations

import os
import struct
import threading
from dataclasses import dataclass
from pathlib import Path

# a stream bigger than this isn't read (no real plugin has one; the biggest
# #~ stream of a plugin is a few hundred KB)
_STREAM_LIMIT = 16 * 1024 * 1024
# MO2 Revamped: custom attribute rows looked at (a crafted DLL can claim
# millions; a real plugin has a few thousand)
_ATTRIBUTE_ROWS_LIMIT = 200_000


@dataclass(frozen=True)
class AssemblyInfo:
    plugin_guids: frozenset[str]
    # the plugin GUIDs its [BepInDependency] attributes require
    required: frozenset[str]


EMPTY = AssemblyInfo(frozenset(), frozenset())
_HARD_DEPENDENCY = 1


def _packed(data: bytes, at: int) -> tuple[int, int]:
    """An ECMA-335 compressed unsigned integer: (value, size)."""
    first = data[at]
    if first & 0x80 == 0:
        return first, 1
    if first & 0xC0 == 0x80:
        return ((first & 0x3F) << 8) | data[at + 1], 2
    if first & 0xE0 == 0xC0:
        return ((first & 0x1F) << 24) | (data[at + 1] << 16) | (data[at + 2] << 8) | data[at + 3], 4
    raise ValueError("bad compressed integer")


def _ser_string(blob: bytes, at: int) -> tuple[str | None, int]:
    """A custom attribute SerString at `at`: (text, next offset); None for a
    null string (0xFF)."""
    if blob[at] == 0xFF:
        return None, at + 1
    length, size = _packed(blob, at)
    start = at + size
    end = start + length
    if end > len(blob):
        raise ValueError("string past the blob")
    return blob[start:end].decode("utf-8"), end


class _Tables:
    """A reader of the ECMA-335 metadata tables a custom attribute's type is
    found with: TypeRef, MemberRef and CustomAttribute."""

    # the columns of the tables up to CustomAttribute (0x0C), to step over
    # their rows: "u2"/"u4" fixed sizes, "s" #Strings, "g" #GUID, "b" #Blob,
    # a table number for a table index, or a coded index name
    _COLUMNS = {
        0x00: ("u2", "s", "g", "g", "g"),
        0x01: ("ResolutionScope", "s", "s"),
        0x02: ("u4", "s", "s", "TypeDefOrRef", 0x04, 0x06),
        0x03: (0x04,),
        0x04: ("u2", "s", "b"),
        0x05: (0x06,),
        0x06: ("u4", "u2", "u2", "s", "b", 0x08),
        0x07: (0x08,),
        0x08: ("u2", "u2", "s"),
        0x09: (0x02, "TypeDefOrRef"),
        0x0A: ("MemberRefParent", "s", "b"),
        0x0B: ("u2", "HasConstant", "b"),
        0x0C: ("HasCustomAttribute", "CustomAttributeType", "b"),
    }
    _CODED = {
        "ResolutionScope": (2, (0x00, 0x1A, 0x23, 0x01)),
        "TypeDefOrRef": (2, (0x02, 0x01, 0x1B)),
        "MemberRefParent": (3, (0x02, 0x01, 0x1A, 0x06, 0x1B)),
        "HasConstant": (2, (0x04, 0x08, 0x17)),
        "HasCustomAttribute": (5, (0x06, 0x04, 0x01, 0x02, 0x08, 0x09, 0x0A, 0x00, 0x0E, 0x17, 0x14,
                                   0x11, 0x1A, 0x1B, 0x20, 0x23, 0x26, 0x27, 0x28, 0x2A, 0x2C, 0x2B)),
        # tags 0, 1 and 4 are unused: 2 is MethodDef, 3 is MemberRef
        "CustomAttributeType": (3, (-1, -1, 0x06, 0x0A, -1)),
    }

    def __init__(self, data: bytes) -> None:
        self.data = data
        heap_sizes = data[6]
        self.string_size = 4 if heap_sizes & 1 else 2
        self.guid_size = 4 if heap_sizes & 2 else 2
        self.blob_size = 4 if heap_sizes & 4 else 2
        valid = struct.unpack_from("<Q", data, 8)[0]
        at = 24
        self.rows = {}
        for table in range(64):
            if valid >> table & 1:
                self.rows[table] = struct.unpack_from("<I", data, at)[0]
                at += 4
        # MO2 Revamped: widths and row sizes once, not per row
        self._column_widths = {table: self._widths(table) for table in self._COLUMNS}
        self._row_sizes = {table: sum(w) for table, w in self._column_widths.items()}
        self.offsets = {}
        for table in range(0x0D):
            self.offsets[table] = at
            if table in self.rows:
                at += self.rows[table] * self._row_sizes[table]
                if at > len(data):
                    raise ValueError("table past the metadata stream")

    def _width(self, column) -> int:
        if column == "u2":
            return 2
        if column == "u4":
            return 4
        if column == "s":
            return self.string_size
        if column == "g":
            return self.guid_size
        if column == "b":
            return self.blob_size
        if isinstance(column, str):
            bits, tables = self._CODED[column]
            biggest = max(self.rows.get(t, 0) for t in tables if t >= 0)
            return 2 if biggest < 1 << (16 - bits) else 4
        # a table index
        return 2 if self.rows.get(column, 0) < 1 << 16 else 4

    def _widths(self, table: int) -> list[int]:
        return [self._width(column) for column in self._COLUMNS[table]]

    def row_size(self, table: int) -> int:
        return self._row_sizes[table]

    def read_row(self, table: int, index: int) -> list[int]:
        """Row `index` (1-based) of a table, as its columns' values."""
        at = self.offsets[table] + (index - 1) * self._row_sizes[table]
        values = []
        for width in self._column_widths[table]:
            values.append(struct.unpack_from("<H" if width == 2 else "<I", self.data, at)[0])
            at += width
        return values

    def coded(self, name: str, value: int) -> tuple[int, int]:
        """A coded index: (table, row)."""
        bits, tables = self._CODED[name]
        tag = value & ((1 << bits) - 1)
        if tag >= len(tables) or tables[tag] < 0:
            return -1, 0
        return tables[tag], value >> bits


def _string_at(strings: bytes, index: int) -> str:
    end = strings.find(b"\0", index)
    return strings[index:end if end >= 0 else len(strings)].decode("utf-8", errors="replace")


def _blob_at(blobs: bytes, index: int) -> bytes:
    length, size = _packed(blobs, index)
    return blobs[index + size:index + size + length]


def bepinex_attributes(tables: bytes, strings: bytes, blobs: bytes) -> tuple[set[str], set[str]]:
    """The GUIDs of an assembly's [BepInPlugin] attributes, and those its
    [BepInDependency] attributes require."""
    plugins: set[str] = set()
    required: set[str] = set()
    reader = _Tables(tables)
    for row in range(1, min(reader.rows.get(0x0C, 0), _ATTRIBUTE_ROWS_LIMIT) + 1):
        _parent, kind, value = reader.read_row(0x0C, row)
        table, index = reader.coded("CustomAttributeType", kind)
        if table != 0x0A or index < 1 or index > reader.rows.get(0x0A, 0):
            continue  # an attribute this assembly defines itself
        parent, _name, _signature = reader.read_row(0x0A, index)
        owner, owner_row = reader.coded("MemberRefParent", parent)
        if owner != 0x01 or owner_row < 1 or owner_row > reader.rows.get(0x01, 0):
            continue
        _scope, type_name, type_namespace = reader.read_row(0x01, owner_row)
        if _string_at(strings, type_namespace) != "BepInEx":
            continue
        name = _string_at(strings, type_name)
        if name not in ("BepInPlugin", "BepInDependency"):
            continue
        try:
            blob = _blob_at(blobs, value)
            if blob[:2] != b"\x01\x00":
                continue
            guid, after = _ser_string(blob, 2)
        except (ValueError, IndexError, UnicodeDecodeError):
            continue
        if not guid:
            continue
        if name == "BepInPlugin":
            plugins.add(guid)
        elif after + 4 <= len(blob) - 2 and len(blob) - after == 6:
            # (guid, DependencyFlags): 1 is HardDependency
            if struct.unpack_from("<i", blob, after)[0] & _HARD_DEPENDENCY:
                required.add(guid)
        else:
            # (guid, minimumVersion): a hard dependency
            required.add(guid)
    return plugins, required


def _streams(file) -> dict[str, tuple[int, int]]:
    """The metadata streams' (file offset, size) by name."""
    head = file.read(4096)
    if head[:2] != b"MZ":
        raise ValueError("not a PE file")
    pe = struct.unpack_from("<I", head, 0x3C)[0]
    if head[pe:pe + 4] != b"PE\0\0":
        raise ValueError("no PE header")
    sections, optional_size = struct.unpack_from("<H", head, pe + 6)[0], struct.unpack_from("<H", head, pe + 20)[0]
    optional = pe + 24
    magic = struct.unpack_from("<H", head, optional)[0]
    directories = optional + (96 if magic == 0x10B else 112)
    cli_rva, _cli_size = struct.unpack_from("<II", head, directories + 14 * 8)
    table = optional + optional_size
    mapping = []
    for index in range(sections):
        base = table + index * 40
        virtual_size, virtual_address, raw_size, raw_pointer = struct.unpack_from("<IIII", head, base + 8)
        mapping.append((virtual_address, max(virtual_size, raw_size), raw_pointer))

    def offset(rva: int) -> int:
        for address, size, pointer in mapping:
            if address <= rva < address + size:
                return pointer + rva - address
        raise ValueError("address outside the sections")

    if cli_rva == 0:
        raise ValueError("not a .NET assembly")
    file.seek(offset(cli_rva))
    cli = file.read(72)
    metadata_rva = struct.unpack_from("<I", cli, 8)[0]
    root = offset(metadata_rva)
    file.seek(root)
    header = file.read(4096)
    if struct.unpack_from("<I", header, 0)[0] != 0x424A5342:
        raise ValueError("no metadata")
    version_length = struct.unpack_from("<I", header, 12)[0]
    at = 16 + version_length
    count = struct.unpack_from("<H", header, at + 2)[0]
    at += 4
    streams = {}
    for _ in range(count):
        stream_offset, size = struct.unpack_from("<II", header, at)
        at += 8
        end = header.index(b"\0", at)
        name = header[at:end].decode("ascii", errors="replace")
        at = (end + 4) & ~3  # the name and its terminator, padded to 4
        streams[name] = (root + stream_offset, size)
    return streams


def read_assembly(path: Path) -> AssemblyInfo:
    """What a DLL's metadata says; EMPTY for a file that isn't a readable
    .NET assembly."""
    try:
        with open(path, "rb") as file:
            streams = _streams(file)

            def stream(name: str) -> bytes:
                where = streams.get(name)
                if where is None or where[1] > _STREAM_LIMIT:
                    return b""
                file.seek(where[0])
                return file.read(where[1])

            guids, required = bepinex_attributes(stream("#~"), stream("#Strings"), stream("#Blob"))
    except (OSError, ValueError, struct.error, IndexError, KeyError):
        return EMPTY
    # a plugin requiring itself isn't a dependency
    return AssemblyInfo(frozenset(guids), frozenset(required - guids))


class AssemblyIndex:
    """read_assembly() once per DLL while it stays the same; entries for DLLs
    no longer looked at are dropped by forget_others()."""

    def __init__(self) -> None:
        self._cache: dict[str, tuple[tuple[int, int], AssemblyInfo]] = {}
        self._seen: set[str] = set()
        # the config sync reads in a background thread, the checks in MO2's
        self._lock = threading.Lock()

    def info(self, path: Path) -> AssemblyInfo:
        key = os.path.normcase(str(path))
        try:
            stat = path.stat()
        except OSError:
            return EMPTY
        stamp = (stat.st_mtime_ns, stat.st_size)
        with self._lock:
            self._seen.add(key)
            cached = self._cache.get(key)
        if cached is not None and cached[0] == stamp:
            return cached[1]
        info = read_assembly(path)
        with self._lock:
            self._cache[key] = (stamp, info)
        return info

    def forget_others(self) -> None:
        """Drops the DLLs not looked at since the last call."""
        with self._lock:
            self._cache = {k: v for k, v in self._cache.items() if k in self._seen}
            self._seen = set()


# one index for a game's plugin and its checks
SHARED = AssemblyIndex()


def plugin_dlls(mod_folder: Path) -> list[Path]:
    """A mod's BepInEx plugin DLLs (BepInEx loads them at any depth)."""
    plugins = mod_folder / "BepInEx" / "plugins"
    found = []
    if plugins.is_dir():
        for folder, _dirs, files in os.walk(plugins):
            found.extend(Path(folder) / name for name in files if name.casefold().endswith(".dll"))
    return found
