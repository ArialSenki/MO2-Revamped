"""The shared parts (common.py) and the smaller ones: the game folder as the
disk spells it (Steam's lower-case paths, Wine's Z: drive, paths with no
drive), log lines that aren't ASCII, the process list, the program list
lock, the save backups and the launch environment."""

from __future__ import annotations

import logging
import os
import tempfile
import unittest
from pathlib import Path
from unittest import mock

from PyQt6 import sip
from PyQt6.QtCore import QEvent, Qt
from PyQt6.QtGui import QKeyEvent

from basic_games.games import game_eldenring_mo2 as er
from basic_games.games.eldenring import common, environment

from fake_organizer import FakeOrganizer, appdata_folder, game_for
from test_cov_game_fakes import private_temp


class TrueCase(unittest.TestCase):
    def test_spelled_as_on_disk(self):
        folder = Path(tempfile.mkdtemp(prefix="er-Case-")) / "Game Folder"
        folder.mkdir()
        lower = str(folder).lower()
        self.assertEqual(common._with_true_case(lower), str(folder))
        self.assertEqual(common._with_true_case(lower.replace("\\", "/")), str(folder))

    def test_left_as_given(self):
        missing = str(Path(tempfile.gettempdir()) / "no such folder" / "x").lower()
        self.assertEqual(common._with_true_case(missing), missing)
        # relative and drive-less paths (a Linux path under Proton)
        self.assertEqual(common._with_true_case("relative/game"), "relative/game")
        self.assertEqual(common._with_true_case("/home/user/.steam/ELDEN RING"), "/home/user/.steam/ELDEN RING")
        # a Wine drive that isn't there
        unused = next(f"{letter}:\\" for letter in "QRSTUVWXYZ" if not os.path.exists(f"{letter}:\\"))
        path = unused + "home\\user\\Game"
        self.assertEqual(common._with_true_case(path), path)

    def test_set_game_path(self):
        appdata_folder()
        organizer = FakeOrganizer()
        game = game_for(organizer)
        game.setGamePath(str(organizer.game_dir).lower())
        self.assertEqual(os.path.normcase(game.gameDirectory().absolutePath()),
                         os.path.normcase(str(organizer.game_dir).replace("\\", "/")))


class Logging(unittest.TestCase):
    def test_non_ascii_goes_to_python_logging(self):
        with self.assertLogs("mo2revamped", level="WARNING") as logs:
            common.qWarning("Elden Ring MO2: ruta con ñ")
        self.assertIn("ruta con ñ", logs.output[0])

    def test_game_name_in_log_lines(self):
        try:
            common.set_log_game_name("Nightreign")
            with self.assertLogs("mo2revamped", level="INFO") as logs:
                common.qInfo("Elden Ring MO2: é")
            self.assertIn("Nightreign MO2", logs.output[0])
        finally:
            common.set_log_game_name("Elden Ring")

    def test_ascii_and_none(self):
        common.qInfo(None)
        common.qInfo("Elden Ring MO2: plain line")
        common.qCritical("Elden Ring MO2: plain line")


class Processes(unittest.TestCase):
    def test_this_process_is_listed(self):
        processes = common._running_processes()
        self.assertIsNotNone(processes)
        self.assertIn(os.getpid(), {pid for pid, _name in processes})
        self.assertIn("python.exe", common._running_process_names())

    def test_image_path(self):
        self.assertEqual(os.path.normcase(common._process_image_path(os.getpid())),
                         os.path.normcase(os.path.realpath(os.sys.executable)))
        self.assertIsNone(common._process_image_path(0x7FFFFFF0))

    def test_unreadable_list(self):
        with mock.patch.object(common.ctypes, "WinDLL", side_effect=OSError("no kernel32")):
            self.assertIsNone(common._running_processes())
            self.assertIsNone(common._running_process_names())
            self.assertIsNone(common._process_image_path(os.getpid()))


class ProgramListLock(unittest.TestCase):
    def test_blocks_only_while_locked(self):
        lock = common._ProgramListLock(None)
        press = QEvent(QEvent.Type.MouseButtonPress)
        key = QKeyEvent(QEvent.Type.KeyPress, Qt.Key.Key_Down, Qt.KeyboardModifier.NoModifier)
        tab = QKeyEvent(QEvent.Type.KeyPress, Qt.Key.Key_Tab, Qt.KeyboardModifier.NoModifier)
        self.assertFalse(lock.eventFilter(None, press))
        lock.locked = True
        self.assertTrue(lock.eventFilter(None, press))
        self.assertTrue(lock.eventFilter(None, key))
        # the keyboard can still leave the list
        self.assertFalse(lock.eventFilter(None, tab))
        self.assertFalse(lock.eventFilter(None, QEvent(QEvent.Type.Show)))

    def test_close_watcher(self):
        from PyQt6.QtWidgets import QWidget

        window = QWidget()
        self.addCleanup(sip.delete, window)
        closed = []
        watcher = common._MainWindowCloseWatcher(window, lambda: closed.append(True))
        self.assertFalse(watcher.eventFilter(window, QEvent(QEvent.Type.Hide)))
        self.assertEqual(closed, [True])
        self.assertFalse(watcher.eventFilter(window, QEvent(QEvent.Type.Show)))
        self.assertEqual(closed, [True])


class Backups(unittest.TestCase):
    def setUp(self):
        appdata_folder()
        self.organizer = FakeOrganizer()
        self.game = game_for(self.organizer)
        self.route = Path(tempfile.mkdtemp(prefix="er-route-"))

    def test_backup_root(self):
        self.assertEqual(self.game.save_backup_root(self.route, self.organizer.base, True),
                         self.organizer.base / "Elden Ring Global Save Backups" / "auto")
        self.assertEqual(self.game.save_backup_root(self.route, self.organizer.base, False),
                         self.route.parent / f"{self.route.name} Backups" / "auto")

    def test_route_files(self):
        (self.route / "ER0000.sl2").write_bytes(b"x")
        (self.route / "notes.txt").write_text("x")
        (self.route / "76561198000000001").mkdir()
        (self.route / "76561198000000001" / "er0000.co2").write_bytes(b"x")
        (self.route / "76561198000000001" / "other.bin").write_bytes(b"x")
        names = sorted(p.name for p in self.game._route_save_files(self.route))
        self.assertEqual(names, ["ER0000.sl2", "er0000.co2"])
        self.assertEqual(self.game._route_save_files(self.route / "missing"), [])

    def test_saves_listed_for_mo2(self):
        (self.route / "76561198000000001").mkdir()
        for name in ("ER0000.sl2", "ER0000.co2", "ER0000.err", "ER0000.sl2.bak"):
            (self.route / "76561198000000001" / name).write_bytes(b"x")
        from PyQt6.QtCore import QDir

        listed = sorted(Path(s.getFilepath()).name for s in self.game.listSaves(QDir(str(self.route))))
        self.assertEqual(listed, ["ER0000.co2", "ER0000.err", "ER0000.sl2"])

    def test_kept_count(self):
        for value, kept in (("7", 7), ("x", 5), (None, 5), (-3, 0), (500, 50)):
            with self.subTest(value=value):
                self.organizer.settings["save_backups_kept"] = value
                if value is None:
                    del self.organizer.settings["save_backups_kept"]
                self.assertEqual(self.game._save_backups_kept(), kept)

    def test_off_empty_and_failed(self):
        backups = self.route.parent / f"{self.route.name} Backups" / "auto"
        self.game._backup_route_saves(self.route, backups, 0)
        self.game._backup_route_saves(self.route, backups, 3)
        self.assertFalse(backups.exists())
        (self.route / "ER0000.sl2").write_bytes(b"x")
        backups.mkdir(parents=True)
        (backups / ".20200101-000000.copying").mkdir()
        with mock.patch("shutil.copy2", side_effect=PermissionError("in use")):
            self.game._backup_route_saves(self.route, backups, 3)
        # a failed copy leaves nothing behind, not even an earlier run's
        self.assertEqual(list(backups.iterdir()), [])

    def test_same_second_and_age_order(self):
        save = self.route / "ER0000.sl2"
        save.write_bytes(b"1")
        backups = self.route.parent / f"{self.route.name} Backups" / "auto"
        with mock.patch("time.strftime", return_value="20240101-120000"):
            self.game._backup_route_saves(self.route, backups, 5)
            save.write_bytes(b"22")
            self.game._backup_route_saves(self.route, backups, 5)
            save.write_bytes(b"333")
            self.game._backup_route_saves(self.route, backups, 5)
        self.assertEqual(sorted(p.name for p in backups.iterdir()),
                         ["20240101-120000", "20240101-120000-2", "20240101-120000-3"])
        key = self.game._backup_age_key
        self.assertEqual(key(backups / "20240101-120000-3")[2], 3)
        self.assertEqual(key(backups / "missing")[0], 0)


class LaunchEnvironment(unittest.TestCase):
    def setUp(self):
        appdata_folder()
        self.organizer = FakeOrganizer()
        self.before = dict(os.environ)

    def tearDown(self):
        for key in set(os.environ) - set(self.before):
            os.environ.pop(key, None)
        for key, value in self.before.items():
            os.environ[key] = value

    def test_previous_values_come_back(self):
        game = game_for(self.organizer)
        os.environ[game.ProfileLaunchConfigEnvironment] = "earlier"
        os.environ[game.BootBoostEnvironment] = "earlier"
        ini = self.organizer.write_profile_ini("[Startup]\n")
        game._activate_profile_config_environment(ini, [{"library": "C:/x/a.dll", "initializer": "Init"},
                                                        {"library": "C:/x/b.dll", "initializer": None}])
        self.assertTrue(os.environ[game.NativeInitializerEnvironment].endswith("a.dll|Init"))
        game._restore_profile_config_environment()
        self.assertEqual(os.environ[game.ProfileLaunchConfigEnvironment], "earlier")
        self.assertEqual(os.environ[game.BootBoostEnvironment], "earlier")
        self.assertNotIn(game.NativeInitializerEnvironment, os.environ)

    def test_missing_ini(self):
        game = game_for(self.organizer)
        os.environ[game.ProfileLaunchConfigEnvironment] = "stale"
        game._activate_profile_config_environment(self.organizer.profile_dir / "missing.ini", None)
        self.assertNotIn(game.ProfileLaunchConfigEnvironment, os.environ)
        self.assertEqual(os.environ[game.BootBoostEnvironment], "1")
        game._restore_profile_config_environment()

    def test_too_long_for_windows(self):
        self.assertFalse(er.EldenRingMo2Game._set_bridge_environment("ER_TEST_LONG", "x" * 32000))
        self.assertNotIn("ER_TEST_LONG", os.environ)
        self.assertTrue(er.EldenRingMo2Game._set_bridge_environment("ER_TEST_LONG", "x"))

    def test_long_profile_path_gets_a_copy(self):
        game = game_for(self.organizer)
        deep = self.organizer.profile_dir
        while len(str(deep)) < 270:
            deep = deep / "a-long-folder-name"
        os.makedirs("\\\\?\\" + str(deep), exist_ok=True)
        ini = deep / "eldenring_mo2_startup.ini"
        with open("\\\\?\\" + str(ini), "w", encoding="utf-8") as stream:
            stream.write("[Startup]\n")
        with private_temp() as temp:
            game._activate_profile_config_environment(ini, [])
            value = os.environ[game.ProfileLaunchConfigEnvironment]
            self.assertLess(len(value), 260)
            self.assertEqual(Path(value).parent, temp)
            game._restore_profile_config_environment()
        self.assertIsNone(environment._bridge_readable_copy(str(deep / "missing.ini")))

    def test_sound_folders(self):
        self.organizer.add_mod("Low", ["sd/a.fsb"])
        self.organizer.add_mod("High", ["sd/b.fsb"])
        self.organizer.add_mod("Off", ["sd/c.fsb"], active=False)
        self.organizer.add_mod("Semi;colon", ["sd/d.fsb"])
        (Path(self.organizer.overwritePath()) / "sd").mkdir()
        game = game_for(self.organizer)
        folders = [Path(folder).name for folder in game._active_sound_folders()]
        self.assertEqual(folders, ["overwrite", "High", "Low"])

    def test_attach_config(self):
        import json

        self.organizer.add_mod("Reforged", ["DLLs/reforged.dll", "DLLs/other.dll"])
        game = game_for(self.organizer)
        natives = game._reforged_natives()
        self.assertEqual([p.name for p in natives], ["other.dll", "reforged.dll"])
        config = json.loads(game._reforged_attach_config(natives, "err"))
        self.assertEqual(config["savefile"], "ER0000.err")
        self.assertEqual(json.loads(game._reforged_attach_config(natives, None))["savefile"], None)
        ini = self.organizer.write_profile_ini("[Startup]\n")
        game._activate_profile_config_environment(ini, [])
        self.assertIn(game.ReforgedAttachEnvironment, os.environ)
        self.assertEqual(os.environ[game.SaveExtensionEnvironment], "err")
        self.assertNotIn(game.SaveExtensionAcceptedEnvironment, os.environ)
        game._restore_profile_config_environment()
        game._save_extension_accepted = True
        game._activate_profile_config_environment(ini, [])
        self.assertEqual(os.environ[game.SaveExtensionAcceptedEnvironment], "1")
        game._restore_profile_config_environment()

    def test_convergence_by_meta_ini(self):
        root = self.organizer.add_mod("Conv", ["regulation.bin"])
        game = game_for(self.organizer)
        for text, found in (
            ("[General]\ngameName=eldenring\nmodid=3419\n", True),
            ("\ufeff[General]\ngameName=Elden Ring\nmodid= 3419 \n", True),
            ("[General]\ngameName=nightreign\nmodid=3419\n", False),
            ("[General]\nmodid=1\n", False),
            ("no section\n", False),
        ):
            with self.subTest(text=text):
                (root / "meta.ini").write_text(text, encoding="utf-8")
                self.assertEqual(game._active_convergence(), found)
        (root / "meta.ini").write_bytes("[General]\ngameName=eldenring\nmodid=3419\nnotes=ñ\n".encode("cp1252"))
        self.assertFalse(game._active_convergence())
        self.assertEqual(game._active_regulation_mod(), "Conv")
        self.assertIsNone(game_for(self.organizer, nightreign=True)._active_regulation_mod())


if __name__ == "__main__":
    unittest.main()
