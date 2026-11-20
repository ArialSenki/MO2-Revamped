"""The .archive index reader and the archive conflicts."""

from __future__ import annotations

import shutil
import struct
import tempfile
import unittest
from pathlib import Path

from basic_games.games.cyberpunk2077.archives import (
    Archive,
    ArchiveError,
    conflicts,
    load_order,
    read_hashes,
)


def write_archive(path: Path, hashes: list[int]) -> Path:
    """An RDAR file with an index of the given path hashes."""
    entries = b"".join(struct.pack("<QqIIIII20s", h, 0, 0, 0, 0, 0, 0, b"\0" * 20) for h in hashes)
    index = struct.pack("<IIQIII", 8, len(entries) + 20, 0, len(hashes), 0, 0) + entries
    index_pos = 40 + 64  # some data before the index, as the game's archives have
    header = struct.pack("<4sIQIQIQ", b"RDAR", 12, index_pos, len(index), 0, 0, index_pos + len(index))
    path.write_bytes(header + b"\0" * 64 + index)
    return path


class ArchivesTest(unittest.TestCase):
    def setUp(self) -> None:
        self.tmp = Path(tempfile.mkdtemp(prefix="cp77-arc-"))

    def tearDown(self) -> None:
        shutil.rmtree(self.tmp, ignore_errors=True)

    def archive(self, name: str, hashes: list[int], origin: str = "") -> Archive:
        path = write_archive(self.tmp / name, hashes)
        return Archive(name, path, origin or name, set(read_hashes(path)))

    def test_read_hashes(self) -> None:
        path = write_archive(self.tmp / "a.archive", [1, 2, 0xFFFF_FFFF_FFFF_FFFF])
        self.assertEqual(read_hashes(path), [1, 2, 0xFFFF_FFFF_FFFF_FFFF])
        self.assertEqual(read_hashes(write_archive(self.tmp / "empty.archive", [])), [])

    def test_not_an_archive(self) -> None:
        bad = self.tmp / "bad.archive"
        bad.write_bytes(b"PK\3\4 not an archive at all, but long enough for a header")
        with self.assertRaises(ArchiveError):
            read_hashes(bad)
        short = self.tmp / "short.archive"
        short.write_bytes(b"RDAR")
        with self.assertRaises(ArchiveError):
            read_hashes(short)
        cut = self.tmp / "cut.archive"
        data = write_archive(self.tmp / "full.archive", list(range(50))).read_bytes()
        cut.write_bytes(data[:-100])
        with self.assertRaises(ArchiveError):
            read_hashes(cut)

    def test_load_order(self) -> None:
        names = ["b.archive", "#first.archive", "A.archive", "c.archive"]
        self.assertEqual(load_order(names, []), ["#first.archive", "A.archive", "b.archive", "c.archive"])
        self.assertEqual(
            load_order(names, ["c.archive", "missing.archive", "B.ARCHIVE"]),
            ["c.archive", "b.archive", "#first.archive", "A.archive"],
        )

    def test_conflicts(self) -> None:
        a = self.archive("a.archive", [1, 2, 3])
        b = self.archive("b.archive", [2, 3, 4])
        c = self.archive("c.archive", [3, 9])
        found = conflicts([c, b, a], [])
        self.assertEqual(
            [(x.winner.name, x.loser.name, x.files) for x in found],
            [("a.archive", "b.archive", 2), ("a.archive", "c.archive", 1)],
        )
        # modlist.txt puts c first: it now wins its shared file
        found = conflicts([a, b, c], ["c.archive"])
        self.assertEqual(
            [(x.winner.name, x.loser.name, x.files) for x in found],
            [("a.archive", "b.archive", 1), ("c.archive", "a.archive", 1), ("c.archive", "b.archive", 1)],
        )


if __name__ == "__main__":
    unittest.main()
