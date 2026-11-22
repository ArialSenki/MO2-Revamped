"""Fixes from the plugins audit: the problem list off the interface's
thread, logs, the crash check, LSLib's download checks, zlib's cap."""

from __future__ import annotations

import shutil
import struct
import tempfile
import time
import unittest
import zlib
from pathlib import Path

from basic_games.games.baldursgate3 import loadorder, lslib, lspk, paks
from paktools import mod_pak, write_pak
from test_plugin import NAME, UA, UB, PluginBase


class ProblemListTest(PluginBase):
    def test_quick_plan_reads_no_pak_and_the_worker_does(self):
        self.install()
        self.pak_mod("A", UA)
        launch = self.game.launch()
        reads = []
        real = lspk.Package

        class Counting(real):
            def __init__(self, path):
                reads.append(threading_name())
                super().__init__(path)

        lspk.Package = Counting
        try:
            plan = launch.quick_plan()
            self.assertEqual(reads, [])
            self.assertEqual(plan.modules, [])
            self.assertEqual([p.name for p in launch.unread], ["A.pak"])
            self.assertTrue(launch.read_in_background())
            launch._background.join(10)
            self.assertEqual(len(reads), 1)
            self.assertNotEqual(reads[0], "MainThread")
            self.assertEqual([m["uuid"] for m in launch.quick_plan().modules], [UA])
            self.assertFalse(launch.read_in_background())
        finally:
            lspk.Package = real

    def test_an_unreadable_pak_is_read_once_until_it_changes(self):
        # a pak the worker can't read wasn't cached: every check listed it as
        # unread again and started the worker again, about every 0.8 s
        self.install()
        pak = self.pak_mod("A", UA) / "A.pak"
        pak.write_bytes(b"not a pak")
        launch = self.game.launch()
        launch.quick_plan()
        self.assertTrue(launch.read_in_background())
        launch._background.join(10)
        launch.quick_plan()
        self.assertEqual([p.name for p in launch.unread], ["A.pak"])
        self.assertFalse(launch.read_in_background())
        # changed: read again
        time.sleep(0.01)
        pak.write_bytes(b"still not a pak")
        launch.quick_plan()
        self.assertTrue(launch.read_in_background())
        launch._background.join(10)

    def test_logs_moved_without_mod_files_or_overwriting(self):
        overwrite = self.organizer.overwrite
        for rel in ("bin/a.log", "x/a.log", "Script Extender/log.txt", "readme/changelog.txt"):
            (overwrite / rel).parent.mkdir(parents=True, exist_ok=True)
            (overwrite / rel).write_text(rel)
        self.game.launch()._move_logs()
        logs = sorted(p.name for p in self.game.log_dir.iterdir())
        self.assertEqual(logs, ["Script Extender-log.txt", "a-1.log", "a.log"])
        self.assertTrue((overwrite / "readme/changelog.txt").exists())

    def test_crash_check_moved_aside_not_deleted(self):
        self.install()
        marker = self.docs / "ModCrashSanityCheck" / "x.lsx"
        marker.parent.mkdir(parents=True)
        marker.write_text("crash")
        self.game.launch().clear_crash_check()
        self.assertFalse(marker.exists())
        kept = list((self.game.plugin_data_path / "crash-checks").rglob("x.lsx"))
        self.assertEqual([k.read_text() for k in kept], ["crash"])
        self.organizer.setPluginSetting(NAME, "clear_mod_crash_check", False)
        marker.write_text("again")
        self.game.launch().clear_crash_check()
        self.assertTrue(marker.exists())


def threading_name() -> str:
    import threading

    return threading.current_thread().name


class DownloadChecks(unittest.TestCase):
    def test_trusted_urls(self):
        ok = lslib._trusted_url
        self.assertTrue(ok("https://github.com/Norbyte/lslib/releases/download/v1/ExportTool-v1.zip"))
        self.assertTrue(ok("https://objects.githubusercontent.com/x"))
        self.assertTrue(ok("https://release-assets.githubusercontent.com/x"))
        self.assertFalse(ok("http://github.com/x"))
        self.assertFalse(ok("https://github.com.evil.example/x"))
        self.assertFalse(ok("https://evil.example/github.com/x"))

    def test_archives_sorted_by_version(self):
        names = ["ExportTool-v1.9.3.zip", "ExportTool-v1.20.4.zip", "ExportTool-v1.10.0.zip"]
        self.assertEqual(
            sorted(names, key=lslib._version_key),
            ["ExportTool-v1.9.3.zip", "ExportTool-v1.10.0.zip", "ExportTool-v1.20.4.zip"],
        )


class ZlibCap(unittest.TestCase):
    def test_entry_that_unpacks_bigger_than_it_says_is_damaged(self):
        tmp = Path(tempfile.mkdtemp(prefix="bg3-zlib-"))
        try:
            pak = write_pak(tmp / "z.pak", {"meta.lsx": b"x" * 10})
            data = bytearray(pak.read_bytes())
            bomb = zlib.compress(b"\0" * 1_000_000)
            # the entry now says zlib (flag 1), 10 bytes unpacked, and holds
            # a stream that unpacks to a megabyte
            package = lspk.Package(pak)
            entry = package.entries[0]
            entry.flags, entry.size, entry.size_on_disk = 1, 10, len(bomb)
            data[entry.offset : entry.offset + 10] = b""
            data[entry.offset : entry.offset] = bomb
            pak.write_bytes(bytes(data))
            with self.assertRaises(lspk.Damaged):
                package.read(entry)
        finally:
            shutil.rmtree(tmp, ignore_errors=True)
