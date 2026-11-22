"""The Overwrite cleanup (overwrite.py): old logs before a launch and the whole
folder after the game, with links never followed, files still open kept,
and paths that are not MO2's Overwrite refused."""

from __future__ import annotations

import subprocess
import tempfile
import unittest
from pathlib import Path
from unittest import mock

from fake_organizer import FakeOrganizer, appdata_folder, game_for


def junction(link: Path, target: Path) -> None:
    subprocess.run(["cmd", "/c", "mklink", "/J", str(link), str(target)], check=True, capture_output=True)


class OverwriteCleanup(unittest.TestCase):
    def setUp(self):
        appdata_folder()
        self.organizer = FakeOrganizer()
        self.game = game_for(self.organizer)
        self.overwrite = Path(self.organizer.overwritePath())
        self.outside = Path(tempfile.mkdtemp(prefix="er-outside-"))
        (self.outside / "keep.log").write_text("not MO2's")

    def closed(self, state=False):
        return mock.patch.object(type(self.game), "_elden_ring_process_state", return_value=state)

    def test_old_logs_before_the_game(self):
        (self.overwrite / "a.log").write_text("x")
        (self.overwrite / "deep" / "er").mkdir(parents=True)
        (self.overwrite / "deep" / "er" / "B.LOG").write_text("x")
        (self.overwrite / "deep" / "save.ini").write_text("x")
        junction(self.overwrite / "linked", self.outside)
        try:
            with self.closed():
                self.game._clear_overwrite_logs_before_game()
            self.assertFalse((self.overwrite / "a.log").exists())
            self.assertFalse((self.overwrite / "deep" / "er" / "B.LOG").exists())
            self.assertTrue((self.overwrite / "deep" / "save.ini").is_file())
            self.assertTrue((self.outside / "keep.log").is_file())
        finally:
            subprocess.run(["cmd", "/c", "rmdir", str(self.overwrite / "linked")], capture_output=True)

    def test_logs_stay_while_the_game_runs(self):
        (self.overwrite / "a.log").write_text("x")
        for state in (True, None):
            with self.closed(state):
                self.game._clear_overwrite_logs_before_game()
            self.assertTrue((self.overwrite / "a.log").is_file())

    def test_an_open_log_is_kept(self):
        (self.overwrite / "open.log").write_text("x")
        (self.overwrite / "done.log").write_text("x")
        with open(self.overwrite / "open.log", "rb"), self.closed():
            self.game._clear_overwrite_logs_before_game()
        self.assertTrue((self.overwrite / "open.log").exists())
        self.assertFalse((self.overwrite / "done.log").exists())

    def test_paths_that_are_not_overwrite(self):
        other = Path(tempfile.mkdtemp(prefix="er-notoverwrite-"))
        (other / "a.log").write_text("x")
        cases = {
            "another name": other,
            "missing": self.organizer.base / "gone" / "overwrite",
        }
        file_named_overwrite = other / "Overwrite"
        file_named_overwrite.write_text("x")
        cases["a file"] = file_named_overwrite
        for label, path in cases.items():
            with self.subTest(label), self.closed(), \
                    mock.patch.object(self.organizer, "overwritePath", return_value=str(path)):
                self.game._clear_overwrite_logs_before_game()
                self.game._clear_overwrite_contents(path)
        self.assertTrue((other / "a.log").is_file())
        self.assertTrue(file_named_overwrite.is_file())

    def test_overwrite_that_is_a_link(self):
        base = Path(tempfile.mkdtemp(prefix="er-linked-ow-"))
        junction(base / "overwrite", self.outside)
        try:
            with self.closed(), mock.patch.object(self.organizer, "overwritePath",
                                                  return_value=str(base / "overwrite")):
                self.game._clear_overwrite_logs_before_game()
            self.game._clear_overwrite_contents(base / "overwrite")
            self.assertTrue((self.outside / "keep.log").is_file())
        finally:
            subprocess.run(["cmd", "/c", "rmdir", str(base / "overwrite")], capture_output=True)

    def test_everything_after_the_game(self):
        (self.overwrite / "root.txt").write_text("x")
        (self.overwrite / "a" / "b").mkdir(parents=True)
        (self.overwrite / "a" / "b" / "c.ini").write_text("x")
        junction(self.overwrite / "linked", self.outside)
        self.game._clear_overwrite_contents(self.overwrite)
        self.assertEqual(list(self.overwrite.iterdir()), [])
        # the link went, never what it points to
        self.assertTrue((self.outside / "keep.log").is_file())

    def test_a_file_still_open_keeps_only_itself(self):
        (self.overwrite / "a").mkdir()
        (self.overwrite / "a" / "open.bin").write_text("x")
        (self.overwrite / "a" / "free.bin").write_text("x")
        (self.overwrite / "b.txt").write_text("x")
        with open(self.overwrite / "a" / "open.bin", "rb"):
            self.game._clear_overwrite_contents(self.overwrite)
        self.assertEqual([p.name for p in self.overwrite.iterdir()], ["a"])
        self.assertEqual([p.name for p in (self.overwrite / "a").iterdir()], ["open.bin"])

    def test_without_a_kept_list_the_error_reaches_the_caller(self):
        (self.overwrite / "a").mkdir()
        (self.overwrite / "a" / "open.bin").write_text("x")
        with open(self.overwrite / "a" / "open.bin", "rb"):
            with self.assertRaises(OSError):
                self.game._remove_overwrite_entry(self.overwrite / "a")

    def test_arm_without_a_path(self):
        with mock.patch.object(self.organizer, "overwritePath", side_effect=ValueError("no instance")):
            self.game._arm_overwrite_cleanup()
        self.assertFalse(self.game._overwrite_cleanup_armed)
        self.game._arm_overwrite_cleanup()
        self.assertTrue(self.game._overwrite_cleanup_armed)
        self.assertEqual(self.game._overwrite_cleanup_path, self.overwrite)


if __name__ == "__main__":
    unittest.main()
