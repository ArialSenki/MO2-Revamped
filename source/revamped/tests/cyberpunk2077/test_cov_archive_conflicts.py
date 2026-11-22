"""The archive conflicts tool: which archives the game sees, read through the
VFS, and the tool itself (its window replaced, nothing shown)."""

from __future__ import annotations

import shutil
import struct
import tempfile
import unittest
from pathlib import Path
from unittest import mock

import mobase

from basic_games.games.cyberpunk2077 import archives
from basic_games.games.cyberpunk2077.plugins import archive_conflicts, createPlugins
from fake_organizer import FakeOrganizer
from test_archives import write_archive
from test_plugin import make_game_dir

FOLDER = "archive/pc/mod"


class FakeCursor:
    """QApplication's override cursor, without a QApplication."""

    calls: list[str] = []

    @staticmethod
    def setOverrideCursor(cursor):
        FakeCursor.calls.append("set")

    @staticmethod
    def restoreOverrideCursor():
        FakeCursor.calls.append("restore")


class Base(unittest.TestCase):
    def setUp(self) -> None:
        self.tmp = Path(tempfile.mkdtemp(prefix="cp77-cov-arc-"))
        self.organizer = FakeOrganizer(self.tmp / "instance")

    def tearDown(self) -> None:
        shutil.rmtree(self.tmp, ignore_errors=True)

    def mod_archive(self, mod: str, name: str, hashes: list[int]) -> Path:
        root = self.organizer.mods / mod
        if mod not in self.organizer.mod_order:
            self.organizer.add_mod(mod, [])
        path = root / FOLDER / name
        path.parent.mkdir(parents=True, exist_ok=True)
        return write_archive(path, hashes)


class ActiveArchivesTest(Base):
    def test_winners_origins_errors_and_modlist(self):
        game_dir = make_game_dir(self.tmp, [f"{FOLDER}/g.archive"])
        write_archive(game_dir / FOLDER / "g.archive", [3])
        self.organizer.game_dir = game_dir
        self.mod_archive("A", "a.archive", [1, 2, 3])
        self.mod_archive("B", "b.archive", [2, 3])
        bad = self.mod_archive("Bad", "c.archive", [])
        bad.write_bytes(b"RDAR" + b"\xff" * 60)  # an index far past the end
        modlist = self.organizer.overwrite / FOLDER / "modlist.txt"
        modlist.parent.mkdir(parents=True)
        modlist.write_bytes(b"b.archive\r\n\xe9 not UTF-8\r\n")
        found, lines = archive_conflicts.active_archives(self.organizer)
        by_name = {a.name: a for a in found}
        self.assertEqual(sorted(by_name), ["a.archive", "b.archive", "c.archive", "g.archive"])
        self.assertEqual(by_name["g.archive"].origin, "")
        self.assertEqual(by_name["a.archive"].origin, "A")
        self.assertTrue(by_name["c.archive"].error)
        self.assertEqual(lines[0], "b.archive")
        result = archives.conflicts([a for a in found if not a.error], lines)
        self.assertEqual([(c.winner.name, c.loser.name, c.files) for c in result],
                         [("b.archive", "a.archive", 2), ("b.archive", "g.archive", 1)])

    def test_modlist_that_cant_be_read(self):
        self.mod_archive("A", "a.archive", [1])
        # a folder where the list would be
        (self.organizer.overwrite / FOLDER / "modlist.txt").mkdir(parents=True)
        organizer = self.organizer
        real = organizer.findFiles

        def find(path, pattern):
            if pattern == "modlist.txt":
                return [str(organizer.overwrite / FOLDER / "modlist.txt")]
            return real(path, pattern)

        organizer.findFiles = find
        found, lines = archive_conflicts.active_archives(organizer)
        self.assertEqual([a.name for a in found], ["a.archive"])
        self.assertEqual(lines, [])

    def test_archive_without_origin(self):
        self.mod_archive("A", "a.archive", [1])
        self.organizer.getFileOrigins = lambda path: []
        found, _ = archive_conflicts.active_archives(self.organizer)
        self.assertEqual(found[0].origin, "")


class ReadHashesTest(Base):
    def test_header_past_2_63_is_an_archive_error(self):
        # the corrupt header raised ValueError, which active_archives didn't
        # catch: the whole conflicts list came up empty
        path = self.tmp / "x.archive"
        for position in (2**63 + 5, 2**64 - 1):
            path.write_bytes(struct.pack("<4sIQIQIQ", b"RDAR", 12, position, 100, 0, 0, 0))
            with self.assertRaises(archives.ArchiveError):
                archives.read_hashes(path)

    def test_index_bigger_than_it_says(self):
        path = write_archive(self.tmp / "x.archive", [1, 2, 3])
        data = bytearray(path.read_bytes())
        # the index size in the header: smaller than its entries
        struct.pack_into("<I", data, 16, 1)
        path.write_bytes(bytes(data))
        with self.assertRaisesRegex(archives.ArchiveError, "larger"):
            archives.read_hashes(path)

    def test_load_order_names_once_and_any_case(self):
        self.assertEqual(archives.load_order(["b.archive", "A.archive", "c.archive"], [" a.ARCHIVE ", "a.archive", "gone"]),
                         ["A.archive", "b.archive", "c.archive"])


class ToolTest(Base):
    def setUp(self) -> None:
        super().setUp()
        self.tool = [p for p in createPlugins() if isinstance(p, archive_conflicts.ArchiveConflictsTool)][0]
        self.assertTrue(self.tool.init(self.organizer))
        self.shown = []
        test = self

        class Dialog:
            def __init__(self, found, modlist, conflicts, parent=None):
                test.shown.append((found, modlist, conflicts, parent))

            def exec(self):
                test.shown.append("exec")

        FakeCursor.calls = []
        for patcher in (mock.patch.object(archive_conflicts, "ConflictsDialog", Dialog),
                        mock.patch.object(archive_conflicts, "QApplication", FakeCursor)):
            patcher.start()
            self.addCleanup(patcher.stop)

    def test_identity(self):
        tool = self.tool
        self.assertEqual(tool.name(), "Cyberpunk 2077 Archive Conflicts")
        self.assertTrue(tool.localizedName())
        self.assertEqual(tool.author(), "MO2 Revamped")
        self.assertEqual(tool.tooltip(), tool.description())
        self.assertTrue(tool.displayName())
        self.assertEqual(tool.settings(), [])
        self.assertEqual(len(tool.requirements()), 1)
        self.assertIsInstance(tool.version(), mobase.VersionInfo)
        parent = object()
        tool.setParentWidget(parent)
        self.assertIs(tool._parent, parent)

    def test_display(self):
        self.mod_archive("A", "a.archive", [1, 2])
        self.mod_archive("B", "b.archive", [2])
        self.tool.display()
        found, modlist, conflicts, parent = self.shown[0]
        self.assertEqual(len(found), 2)
        self.assertEqual(modlist, [])
        self.assertEqual([(c.winner.name, c.loser.name) for c in conflicts], [("a.archive", "b.archive")])
        self.assertEqual(self.shown[1], "exec")
        self.assertEqual(FakeCursor.calls, ["set", "restore"])

    def test_display_after_an_error_shows_an_empty_list(self):
        def broken(path, pattern):
            raise RuntimeError("VFS not ready")

        self.organizer.findFiles = broken
        self.tool.display()
        self.assertEqual(self.shown[0][:3], ([], [], []))
        self.assertEqual(FakeCursor.calls, ["set", "restore"])


if __name__ == "__main__":
    unittest.main()
