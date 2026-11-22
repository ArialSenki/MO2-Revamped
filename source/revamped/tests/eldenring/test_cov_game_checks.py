"""The checks before a launch (checks.py): the game executable read the way
the bridge searches it (built here as small PE files), and the questions
about regulation.bin, Steam Cloud, the save extension and Seamless Co-op's
password, answered by a stand-in message box."""

from __future__ import annotations

import json
import os
import struct
import tempfile
import unittest
from pathlib import Path
from unittest import mock

from basic_games.games import game_eldenring_mo2 as er
from basic_games.games.eldenring import checks

from fake_organizer import FakeOrganizer, appdata_folder, game_for
from test_cov_game_fakes import quiet_ui

CODE = 0x60000020  # code, executable, readable
DATA = 0x40000040  # initialized data, readable


def make_pe(sections: list[tuple[int, bytes]], extra: bytes = b"") -> bytes:
    """A PE image with just what the searches read: e_lfanew, the section
    count, the optional header size and the section table. Section i is at
    address 0x1000 * (i + 1), its raw data at 0x400 * (i + 1)."""
    header = 0x80
    optional = 0xF0
    table = header + 24 + optional
    data = bytearray(0x400 * (len(sections) + 1))
    struct.pack_into("<I", data, 0x3C, header)
    data[header:header + 4] = b"PE\0\0"
    struct.pack_into("<H", data, header + 6, len(sections))
    struct.pack_into("<H", data, header + 20, optional)
    for index, (flags, content) in enumerate(sections):
        raw = 0x400 * (index + 1)
        entry = table + index * 40
        struct.pack_into("<IIII", data, entry + 8, len(content), 0x1000 * (index + 1), len(content), raw)
        struct.pack_into("<I", data, entry + 36, flags)
        if raw + len(content) > len(data):
            data.extend(b"\0" * (raw + len(content) - len(data)))
        data[raw:raw + len(content)] = content
    return bytes(data) + extra


def call_site(target_offset: int, at: int = 16, size: int = 0x300) -> bytes:
    """A code section with the Elden Ring call site at `at` whose call goes to
    `target_offset` in the same section."""
    after = er.EldenRingMo2Game.ResolverCallSites[0][1]
    code = bytearray(size)
    match = at + 5
    code[at] = 0xE8
    struct.pack_into("<i", code, at + 1, target_offset - match)
    code[match:match + len(after)] = after
    return bytes(code)


def write(data: bytes) -> Path:
    path = Path(tempfile.mkdtemp(prefix="er-exe-")) / "eldenring.exe"
    path.write_bytes(data)
    return path


class FileLoaderSearch(unittest.TestCase):
    cls = er.EldenRingMo2Game

    def test_call_site_found(self):
        self.assertTrue(self.cls.file_loader_found(write(make_pe([(CODE, call_site(0x200))]))))

    def test_call_site_through_a_jump(self):
        code = bytearray(call_site(0x200))
        code[0x200] = 0xE9
        struct.pack_into("<i", code, 0x201, 0x280 - 0x205)
        self.assertTrue(self.cls.file_loader_found(write(make_pe([(CODE, bytes(code))]))))

    def test_two_call_sites_disagree(self):
        first = bytearray(call_site(0x200))
        second = call_site(0x2F0, at=0x100)
        first[0x100:0x100 + 5 + len(self.cls.ResolverCallSites[0][1])] = second[0x100:0x100 + 5 + len(self.cls.ResolverCallSites[0][1])]
        self.assertIs(self.cls.file_loader_found(write(make_pe([(CODE, bytes(first))]))), False)

    def test_call_outside_the_image(self):
        self.assertIs(self.cls.file_loader_found(write(make_pe([(CODE, call_site(0x100000))]))), False)

    def test_only_data_sections_are_not_searched(self):
        self.assertIs(self.cls.file_loader_found(write(make_pe([(DATA, call_site(0x200))]))), False)

    def argument_setup(self, calls: list[tuple[int, int]]) -> bytes:
        code = bytearray(0x300)
        setup = b"\x4C\x8B\xC0\x33\xD2\x48\x89\x44\x24\x20\x49\x8B\xC8"
        for at, target in calls:
            code[at:at + len(setup)] = setup
            call = at + len(setup)
            code[call] = 0xE8
            struct.pack_into("<i", code, call + 1, target - (call + 5))
        return bytes(code)

    def test_argument_setup_needs_two_calls(self):
        one = make_pe([(CODE, self.argument_setup([(0x10, 0x200)]))])
        self.assertIs(self.cls.file_loader_found(write(one)), False)
        two = make_pe([(CODE, self.argument_setup([(0x10, 0x200), (0x80, 0x200)]))])
        self.assertTrue(self.cls.file_loader_found(write(two)))
        other = make_pe([(CODE, self.argument_setup([(0x10, 0x200), (0x80, 0x280)]))])
        self.assertIs(self.cls.file_loader_found(write(other)), False)

    def test_unreadable(self):
        missing = Path(tempfile.mkdtemp(prefix="er-exe-")) / "eldenring.exe"
        self.assertIsNone(self.cls.file_loader_found(missing))
        self.assertIsNone(self.cls.file_loader_found(write(b"MZ")))
        self.assertIsNone(self.cls.save_extension_literal_found(missing))
        self.assertIsNone(self.cls.save_extension_literal_found(write(b"MZ" + b"\0" * 10)))
        self.assertEqual(self.cls.game_version_text(missing), "")

    def test_reads_shared_within_a_launch(self):
        path = write(make_pe([(CODE, call_site(0x200))]))
        self.cls._executable_reads = {}
        try:
            first = self.cls._read_game_executable(path)
            self.assertIs(self.cls._read_game_executable(path), first)
            self.assertEqual(len(self.cls._executable_reads), 1)
        finally:
            self.cls._executable_reads = None


class SaveExtensionAndVersion(unittest.TestCase):
    cls = er.EldenRingMo2Game
    literal = ".sl2".encode("utf-16-le")

    def test_one_literal(self):
        data = b"\0" * 8 + self.literal + b"\0" * 8
        self.assertTrue(self.cls.save_extension_literal_found(write(make_pe([(DATA, data)]))))

    def test_two_literals_or_odd_offset(self):
        two = b"\0" * 8 + self.literal + b"\0" * 4 + self.literal
        self.assertIs(self.cls.save_extension_literal_found(write(make_pe([(DATA, two)]))), False)
        odd = b"\0" * 9 + self.literal + b"\0" * 7
        self.assertIs(self.cls.save_extension_literal_found(write(make_pe([(DATA, odd)]))), False)
        # in code or writable data the bridge doesn't look
        self.assertIs(
            self.cls.save_extension_literal_found(write(make_pe([(CODE, b"\0" * 8 + self.literal)]))), False)

    def test_version_text(self):
        fixed = struct.pack("<I", 0xFEEF04BD) + b"\0" * 4 + struct.pack("<II", (1 << 16) | 17, (2 << 16) | 0)
        shown = b"Gr v1.17.2#1131\0dantelion"
        self.assertEqual(self.cls.game_version_text(write(make_pe([], fixed + shown))), "1.17.2 (1.17.2.0)")
        self.assertEqual(self.cls.game_version_text(write(make_pe([], b"Gr v1.16.0#1\0dantelion"))), "1.16")
        self.assertEqual(self.cls.game_version_text(write(make_pe([], fixed))), "1.17.2.0")
        self.assertEqual(self.cls.game_version_text(write(make_pe([]))), "")


class LaunchQuestions(unittest.TestCase):
    def setUp(self):
        appdata_folder()
        self.organizer = FakeOrganizer()

    def game(self):
        return game_for(self.organizer)

    def exe(self, data: bytes) -> str:
        path = self.organizer.game_dir / "Game" / "eldenring.exe"
        path.write_bytes(data)
        return str(path)

    def test_file_loader(self):
        self.organizer.add_mod("Loose", ["parts/a.partsbnd.dcx"])
        game = self.game()
        app = str(self.organizer.game_dir / "Game" / "eldenring.exe")
        # no executable: nothing to say
        self.assertTrue(game._file_loader_ready(app))
        self.exe(make_pe([(CODE, b"\0" * 0x100)], b"Gr v1.99.0#1\0dantelion"))
        with quiet_ui(checks, answer=1) as box:
            self.assertFalse(game._file_loader_ready(app))
        self.assertIn("1.99", box.shown[0][2])
        with quiet_ui(checks, answer=0, check=True) as box:
            self.assertTrue(game._file_loader_ready(app))
        self.assertTrue(self.organizer.persist.get("file_loader_accepted"))
        with quiet_ui(checks, answer=1) as box:
            self.assertTrue(game._file_loader_ready(app))
        self.assertEqual(box.shown, [])
        # a recognized game asks nothing
        self.exe(make_pe([(CODE, call_site(0x200))]))
        with quiet_ui(checks, answer=1) as box:
            self.assertTrue(game._file_loader_ready(app))

    def test_file_loader_without_game_files(self):
        self.organizer.add_mod("Dll only", ["DLLs/a.dll"])
        game = self.game()
        app = self.exe(make_pe([(CODE, b"\0" * 0x100)]))
        with quiet_ui(checks, answer=1) as box:
            self.assertTrue(game._file_loader_ready(app))
        self.assertEqual(box.shown, [])

    def test_regulation_conflict(self):
        self.organizer.add_mod("First", ["regulation.bin"])
        self.organizer.add_mod("Second", ["regulation.bin"])
        self.organizer.add_mod("Off", ["regulation.bin"], active=False)
        game = self.game()
        app = str(self.organizer.game_dir / "Game" / "eldenring.exe")
        with quiet_ui(checks, answer=1) as box:
            self.assertFalse(game._regulation_conflict_ready(app))
        self.assertIn("Second", box.shown[0][2])
        self.assertNotIn("Off", box.shown[0][2])
        with quiet_ui(checks, answer=0, check=True):
            self.assertTrue(game._regulation_conflict_ready(app))
        self.assertEqual(self.organizer.persist["regulation_accepted_Default"], "first|second")
        with quiet_ui(checks, answer=1) as box:
            self.assertTrue(game._regulation_conflict_ready(app))
        self.assertEqual(box.shown, [])

    def test_one_regulation_is_fine(self):
        self.organizer.add_mod("Only", ["regulation.bin"])
        with quiet_ui(checks, answer=1):
            self.assertTrue(self.game()._regulation_conflict_ready("eldenring.exe"))

    def swap_tool(self) -> str:
        tool = str(self.organizer.base / "tools" / "launcher.exe")
        (self.organizer.base / er.EldenRingMo2Game.LaunchMethodsFilename).write_text(
            json.dumps({"methods": {os.path.normcase(os.path.abspath(tool)): "swap"}}), encoding="utf-8")
        return tool

    def test_steam_cloud_swap(self):
        game = self.game()
        tool = self.swap_tool()
        with quiet_ui(checks, answer=1):
            # inside the VFS, and Reforged online, ask nothing
            self.assertTrue(game._steam_cloud_swap_ready("C:/Game/eldenring.exe"))
            self.assertTrue(game._steam_cloud_swap_ready("C:/x/ReforgedLauncher.exe"))
            self.assertFalse(game._steam_cloud_swap_ready(tool))
        with quiet_ui(checks, answer=0, check=True):
            self.assertTrue(game._steam_cloud_swap_ready(tool))
        self.assertIs(self.organizer.persist["steam_cloud_swap_accepted"], True)
        with quiet_ui(checks, answer=1) as box:
            self.assertTrue(game._steam_cloud_swap_ready(tool))
        self.assertEqual(box.shown, [])

    def test_outside_the_vfs_skips_the_mod_checks(self):
        self.organizer.add_mod("A", ["regulation.bin"])
        self.organizer.add_mod("B", ["regulation.bin"])
        game = self.game()
        tool = self.swap_tool()
        with quiet_ui(checks, answer=1) as box:
            self.assertTrue(game._regulation_conflict_ready(tool))
            self.assertTrue(game._file_loader_ready(tool))
            self.assertTrue(game._save_extension_ready(tool))
            self.assertTrue(game._seamless_password_ready(tool))
        self.assertEqual(box.shown, [])

    def test_save_extension(self):
        self.organizer.add_mod("Reforged", ["DLLs/reforged.dll"])
        game = self.game()
        app = str(self.organizer.game_dir / "Game" / "eldenring.exe")
        self.assertTrue(game._save_extension_ready(app))  # no executable
        self.exe(make_pe([(DATA, b"\0" * 32)]))
        with quiet_ui(checks, answer=1) as box:
            self.assertFalse(game._save_extension_ready(app))
        self.assertIn("ER0000.err", box.shown[0][2])
        self.assertFalse(game._save_extension_accepted)
        with quiet_ui(checks, answer=0, check=True):
            self.assertTrue(game._save_extension_ready(app))
        self.assertTrue(game._save_extension_accepted)
        with quiet_ui(checks, answer=1) as box:
            self.assertTrue(game._save_extension_ready(app))
        self.assertTrue(game._save_extension_accepted)
        self.assertEqual(box.shown, [])

    def test_save_extension_found(self):
        self.organizer.add_mod("Reforged", ["DLLs/reforged.dll"])
        game = self.game()
        app = self.exe(make_pe([(DATA, b"\0" * 8 + ".sl2".encode("utf-16-le"))]))
        with quiet_ui(checks, answer=1) as box:
            self.assertTrue(game._save_extension_ready(app))
        self.assertEqual(box.shown, [])

    def test_no_own_save_extension(self):
        self.assertTrue(self.game()._save_extension_ready("eldenring.exe"))


class SeamlessPassword(unittest.TestCase):
    def setUp(self):
        appdata_folder()
        self.organizer = FakeOrganizer()
        self.root = self.organizer.add_mod("Seamless", ["SeamlessCoop/ersc.dll"])
        self.settings = self.root / "SeamlessCoop" / "ersc_settings.ini"
        self.game = game_for(self.organizer)

    def ready(self, answer=1):
        with quiet_ui(checks, answer=answer) as box, \
                mock.patch.object(checks, "QDesktopServices") as desktop:
            result = self.game._seamless_password_ready("eldenring.exe")
        return result, box.shown, desktop

    def test_no_settings_file(self):
        self.assertEqual(self.ready()[:2], (True, []))

    def test_password_set(self):
        self.settings.write_text("[PASSWORD]\ncooppassword = secret\n", encoding="utf-8")
        self.assertTrue(self.ready()[0])

    def test_password_set_with_bom(self):
        self.settings.write_bytes(b"\xef\xbb\xbf[PASSWORD]\ncooppassword = secret\n")
        self.assertTrue(self.ready()[0])

    def test_no_password(self):
        self.settings.write_text("[PASSWORD]\ncooppassword =   \n", encoding="utf-8")
        result, shown, desktop = self.ready(answer=1)
        self.assertFalse(result)
        self.assertEqual(len(shown), 1)
        desktop.openUrl.assert_not_called()
        # "Open the settings file" opens it, and the game still doesn't start
        result, _shown, desktop = self.ready(answer=0)
        self.assertFalse(result)
        desktop.openUrl.assert_called_once()

    def test_no_password_section(self):
        self.settings.write_text("[SAVE]\nsave_file_extension = co2\n", encoding="utf-8")
        self.assertFalse(self.ready()[0])

    def test_unreadable_settings_let_the_game_start(self):
        # a file Notepad saved in the system's code page, or a broken one
        self.settings.write_bytes("[PASSWORD]\ncooppassword = contraseña\n".encode("cp1252"))
        self.assertTrue(self.ready()[0])
        self.settings.write_text("cooppassword = x\n", encoding="utf-8")
        self.assertTrue(self.ready()[0])

    def test_switched_off_mod(self):
        self.settings.write_text("[PASSWORD]\ncooppassword =\n", encoding="utf-8")
        self.organizer.active.discard("Seamless")
        self.assertTrue(self.ready()[0])


if __name__ == "__main__":
    unittest.main()
