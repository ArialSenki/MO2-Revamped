"""Cyberpunk 2077 .archive files: which game files each one holds, and which
archive wins when two hold the same file.

An .archive (RDAR) starts with a header that points to its index; the index
lists each file by the 64-bit hash of its path. Only the header and the index
are read, never the files themselves.

The game loads the archives in archive/pc/mod listed in modlist.txt first, in
that order, then the others in alphabetical order; for a file in several
archives, the archive loaded first wins.
"""

from __future__ import annotations

import struct
from dataclasses import dataclass, field
from pathlib import Path

MAGIC = b"RDAR"
_HEADER = struct.Struct("<4sIQIQIQ")  # magic, version, index pos, index size, debug pos, debug size, file size
_INDEX = struct.Struct("<IIQIII")  # file table offset, size, crc, file entries, segments, dependencies
_ENTRY = struct.Struct("<QqIIIII20s")  # name hash, timestamp, inline segments, segments, deps, sha1
# an index larger than this is not one the game writes
_MAX_ENTRIES = 2_000_000


class ArchiveError(ValueError):
    pass


def read_hashes(path: Path) -> list[int]:
    """The path hashes of the files an archive holds."""
    with open(path, "rb") as file:
        header = file.read(_HEADER.size)
        if len(header) < _HEADER.size:
            raise ArchiveError("too short for an archive")
        magic, _version, index_pos, index_size, _debug_pos, _debug_size, _size = _HEADER.unpack(header)
        if magic != MAGIC:
            raise ArchiveError("not an RDAR archive")
        # MO2 Revamped: an index position past 2^63 (a corrupt header) raised
        # a plain ValueError, which emptied the whole conflicts list
        try:
            file.seek(index_pos)
        except (ValueError, OverflowError) as error:
            raise ArchiveError("index past the end of the file") from error
        index = file.read(_INDEX.size)
        if len(index) < _INDEX.size:
            raise ArchiveError("index past the end of the file")
        _offset, _table_size, _crc, count, _segments, _deps = _INDEX.unpack(index)
        if count > _MAX_ENTRIES or count * _ENTRY.size > max(index_size, 0) + _ENTRY.size:
            raise ArchiveError("index larger than the archive says")
        data = file.read(count * _ENTRY.size)
        if len(data) < count * _ENTRY.size:
            raise ArchiveError("index cut short")
    return [entry[0] for entry in _ENTRY.iter_unpack(data)]


@dataclass
class Archive:
    name: str  # file name, as the game sees it in archive/pc/mod
    path: Path  # the real file the VFS serves
    origin: str  # the mod it comes from ("" for the game folder)
    hashes: set[int] = field(default_factory=set)
    error: str = ""


@dataclass
class Conflict:
    winner: Archive
    loser: Archive
    files: int


def load_order(names: list[str], modlist: list[str]) -> list[str]:
    """The game's order: modlist.txt's archives first, as listed, then the
    others alphabetically (ordinal, as the game compares the names)."""
    present = {name.casefold(): name for name in names}
    ordered: list[str] = []
    seen: set[str] = set()
    for line in modlist:
        key = line.strip().casefold()
        if key in present and key not in seen:
            ordered.append(present[key])
            seen.add(key)
    ordered.extend(sorted((name for name in names if name.casefold() not in seen)))
    return ordered


def conflicts(archives: list[Archive], modlist: list[str]) -> list[Conflict]:
    """For each pair of archives holding the same files, the one the game
    loads first (the winner) and how many files it takes from the other."""
    by_name = {archive.name: archive for archive in archives}
    order = load_order(list(by_name), modlist)
    owner: dict[int, Archive] = {}
    counts: dict[tuple[str, str], int] = {}
    for name in order:
        archive = by_name[name]
        for digest in archive.hashes:
            first = owner.get(digest)
            if first is None:
                owner[digest] = archive
            else:
                key = (first.name, archive.name)
                counts[key] = counts.get(key, 0) + 1
    result = [Conflict(by_name[w], by_name[l], n) for (w, l), n in counts.items()]
    result.sort(key=lambda c: (-c.files, c.winner.name.casefold(), c.loser.name.casefold()))
    return result
