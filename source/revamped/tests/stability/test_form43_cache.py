"""The Form 43 check on an unchanged plugin list: no stat per plugin at each
check (not shipped).

    py -3.12 -I source/revamped/tests/stability/test_form43_cache.py

MO2_RUNTIME points to an MO2 Revamped folder (default D:\\mo2build\\complete-dev)
whose plugin_python has mobase.
"""

from __future__ import annotations

import os
import sys
import tempfile
import unittest
from pathlib import Path
from unittest import mock

HERE = Path(__file__).resolve().parent
REPO = HERE.parents[3]
RUNTIME = Path(os.environ.get("MO2_RUNTIME", r"D:\mo2build\complete-dev"))
for folder in (RUNTIME, RUNTIME / "dlls", RUNTIME / "plugins/plugin_python/dlls"):
    if folder.is_dir():
        os.add_dll_directory(str(folder))
sys.path.insert(0, str(RUNTIME / "plugins/plugin_python/libs"))
sys.path.insert(0, str(REPO / "source/revamped/plugins"))

import Form43Checker  # noqa: E402


def plugin(path: Path, form: int) -> str:
    path.write_bytes(b"TES4" + bytes(16) + form.to_bytes(2, "little") + bytes(10))
    return str(path)


class Organizer:
    def __init__(self, files):
        self.files = files
        self.refreshed = []
        self.enabled = []

    def onNextRefresh(self, callback, immediate):
        self.refreshed.append(callback)
        return True

    def onPluginEnabled(self, name, callback):
        self.enabled.append(callback)
        return True

    def refresh_while_off(self):
        # MO2 drops the callbacks of a plugin that is off when they're due
        self.refreshed = []

    def turn_on(self):
        for callback in self.enabled:
            callback()

    def refresh(self):
        callbacks, self.refreshed = self.refreshed, []
        for callback in callbacks:
            callback()

    def managedGame(self):
        game = mock.Mock()
        game.gameName.return_value = "Skyrim Special Edition"
        return game

    def findFiles(self, path, pattern):
        return list(self.files)


class Cache(unittest.TestCase):
    def setUp(self):
        self.dir = Path(tempfile.mkdtemp())
        self.files = [plugin(self.dir / f"p{i}.esp", 44) for i in range(50)]
        self.organizer = Organizer(self.files)
        self.check = Form43Checker.Form43Checker()
        self.check.init(self.organizer)

    def stats(self) -> int:
        real = os.stat
        calls = []

        def counting(*args, **kwargs):
            calls.append(args[0])
            return real(*args, **kwargs)

        with mock.patch.object(Form43Checker.os, "stat", counting):
            self.problems = self.check.activeProblems()
        return len(calls)

    def test_unchanged_list_is_not_read_again(self):
        self.assertEqual(self.stats(), 50)
        self.assertEqual(self.problems, [])
        self.assertEqual(self.stats(), 0)
        self.assertEqual(self.problems, [])

    def test_a_new_plugin_is_seen(self):
        self.stats()
        self.organizer.files = self.files + [plugin(self.dir / "old.esp", 43)]
        self.assertGreater(self.stats(), 0)
        self.assertEqual(self.problems, [0])

    def test_read_again_after_being_off_during_a_refresh(self):
        # its refresh callback was dropped while it was off: turned on again,
        # it doesn't keep the answer from before the refresh
        self.stats()
        plugin(self.dir / "p3.esp", 43)
        later = os.stat(self.dir / "p3.esp").st_mtime_ns + 10**9
        os.utime(self.dir / "p3.esp", ns=(later, later))
        self.organizer.refresh_while_off()
        self.organizer.turn_on()
        self.assertGreater(self.stats(), 0)
        self.assertEqual(self.problems, [0])
        self.assertEqual(len(self.organizer.refreshed), 1)

    def test_read_again_after_a_refresh(self):
        # a mod replaced in place: same paths, another form
        self.stats()
        plugin(self.dir / "p3.esp", 43)
        # a later time than setUp's write: written within the same clock tick,
        # the cache would rightly take it for the same file
        later = os.stat(self.dir / "p3.esp").st_mtime_ns + 10**9
        os.utime(self.dir / "p3.esp", ns=(later, later))
        self.assertEqual(self.stats(), 0)
        self.organizer.refresh()
        self.assertGreater(self.stats(), 0)
        self.assertEqual(self.problems, [0])
        # and the next refresh is watched again
        self.assertEqual(len(self.organizer.refreshed), 1)


if __name__ == "__main__":
    unittest.main(verbosity=2)
