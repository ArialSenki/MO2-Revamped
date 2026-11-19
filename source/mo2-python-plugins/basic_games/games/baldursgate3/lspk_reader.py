"""MO2 Revamped: a reader for the index of Baldur's Gate 3's .pak files (LSPK
version 18, the one the game and LSLib write) and the small files in them.

Reading a mod's metadata ran Divine.exe twice per pak (meta.lsx out, then the
list of its files), a .NET start each time; with a few hundred paks
"Reparse Pak Metadata" took minutes. The index and meta.lsx are read here in
a few milliseconds. Anything this reader doesn't know (another version, a
solid or split package, Zstandard) raises Unsupported and the caller asks
Divine as before.
"""

import os
import struct
import zlib
from pathlib import Path


class Unsupported(Exception):
    pass


class Damaged(Unsupported):
    """MO2 Revamped: the package is cut short or corrupt, not just a kind this
    reader doesn't know."""


_MAGIC = b"LSPK"
# Version, FileListOffset, FileListSize, Flags, Priority, Md5, NumParts
_HEADER18 = struct.Struct("<IQIBB16sH")
# Name, OffsetInFile1, OffsetInFile2, ArchivePart, Flags, SizeOnDisk,
# UncompressedSize
_ENTRY18 = struct.Struct("<256sIHBBII")
_SOLID = 0x04
_METHOD_NONE, _METHOD_ZLIB, _METHOD_LZ4 = 0, 1, 2


def lz4_block(src: bytes, size: int) -> bytes:
    """An LZ4 block (no frame), as LSLib's LZ4Codec.Decode reads it."""
    dst = bytearray()
    i = 0
    n = len(src)
    try:
        while i < n:
            token = src[i]
            i += 1
            lit = token >> 4
            if lit == 15:
                while True:
                    b = src[i]
                    i += 1
                    lit += b
                    if b != 255:
                        break
            if lit:
                # MO2 Revamped: never past the size the caller expects (a
                # corrupt block could ask for gigabytes before failing)
                if len(dst) + lit > size or i + lit > n:
                    raise Damaged("LZ4 literals past the block")
                dst += src[i : i + lit]
                i += lit
            if i >= n:
                break
            off = src[i] | (src[i + 1] << 8)
            i += 2
            ml = token & 15
            if ml == 15:
                while True:
                    b = src[i]
                    i += 1
                    ml += b
                    if b != 255:
                        break
            ml += 4
            if len(dst) + ml > size:
                raise Damaged("LZ4 match past the block")
            start = len(dst) - off
            if off == 0 or start < 0:
                raise Damaged("bad LZ4 offset")
            if off >= ml:
                dst += dst[start : start + ml]
            else:
                chunk = bytes(dst[start:])
                reps, rest = divmod(ml, off)
                dst += chunk * reps + chunk[:rest]
    except IndexError as e:
        raise Damaged("truncated LZ4 block") from e
    if len(dst) != size:
        raise Damaged("LZ4 size mismatch")
    return bytes(dst)


class Entry:
    __slots__ = ("name", "offset", "part", "flags", "size_on_disk", "size")

    def __init__(self, raw: tuple):
        name, off1, off2, self.part, self.flags, self.size_on_disk, self.size = raw
        self.name = name.split(b"\0", 1)[0].decode("utf-8", "replace")
        self.offset = off1 | (off2 << 32)


class Package:
    def __init__(self, path: Path):
        self.path = path
        try:
            self._read_index(path)
        except (struct.error, OverflowError, MemoryError) as e:
            # MO2 Revamped: a cut download or a corrupt offset; Divine gets it
            raise Damaged(f"damaged package: {e}") from e

    def _read_index(self, path: Path) -> None:
        with open(path, "rb") as f:
            file_size = os.fstat(f.fileno()).st_size
            head = f.read(4 + _HEADER18.size)
            if len(head) < 4 + _HEADER18.size or head[:4] != _MAGIC:
                raise Unsupported("not an LSPK package")
            version, list_offset, _list_size, flags, _prio, _md5, parts = (
                _HEADER18.unpack_from(head, 4)
            )
            if version != 18:
                raise Unsupported(f"LSPK version {version}")
            if flags & _SOLID or parts > 1:
                raise Unsupported("solid or split package")
            if list_offset + 8 > file_size:
                raise Damaged("file list past the end of the package")
            f.seek(list_offset)
            head = f.read(8)
            if len(head) != 8:
                raise Damaged("file list cut short")
            num_files, compressed_size = struct.unpack("<ii", head)
            if (
                num_files < 0
                or compressed_size < 0
                or list_offset + 8 + compressed_size > file_size
                # LZ4 can't expand more than 255 times
                or num_files * _ENTRY18.size > compressed_size * 255 + 16
            ):
                raise Damaged("bad file list")
            data = f.read(compressed_size)
            if len(data) != compressed_size:
                raise Damaged("file list cut short")
            raw = lz4_block(data, num_files * _ENTRY18.size)
        self.entries = [Entry(e) for e in _ENTRY18.iter_unpack(raw)]

    @property
    def names(self) -> list[str]:
        return [e.name for e in self.entries]

    def find(self, file_name: str) -> Entry | None:
        """The first file with that name in any folder (as Divine's
        extract-single-file -f meta.lsx finds it)."""
        wanted = file_name.casefold()
        for e in self.entries:
            if e.name.rsplit("/", 1)[-1].casefold() == wanted:
                return e
        return None

    def read(self, entry: Entry) -> bytes:
        if entry.part != 0:
            raise Unsupported("file in another part")
        with open(self.path, "rb") as f:
            if entry.offset + entry.size_on_disk > os.fstat(f.fileno()).st_size:
                raise Damaged("file past the end of the package")
            f.seek(entry.offset)
            data = f.read(entry.size_on_disk)
        if len(data) != entry.size_on_disk:
            raise Damaged("file past the end of the package")
        method = entry.flags & 0x0F
        if method == _METHOD_NONE:
            return data
        if method == _METHOD_ZLIB:
            try:
                out = zlib.decompress(data)
            except zlib.error as e:
                raise Damaged("bad zlib data") from e
            if len(out) != entry.size:
                raise Damaged("zlib size mismatch")
            return out
        if method == _METHOD_LZ4:
            return lz4_block(data, entry.size)
        raise Unsupported(f"compression method {method}")
