"""Steam's save folders set aside and put back (recovery.py, swap.py): records
that are broken or point elsewhere (Wine's Z: drive and forward slashes
among them), other installs' sessions, the process list, the swap of the
route's saves for a launch outside the VFS and the launch methods file."""

from __future__ import annotations

import json
import os
import tempfile
import time
import unittest
from pathlib import Path
from unittest import mock

from basic_games.games import game_eldenring_mo2 as er
from basic_games.games.eldenring import recovery

from fake_organizer import FakeOrganizer, appdata_folder, game_for
from test_cov_game_fakes import FakeTimer, no_game_running, quiet_ui

ACCOUNT = "76561198000000001"


class Base(unittest.TestCase):
    def setUp(self):
        self.appdata = appdata_folder()
        self.steam = self.appdata / "EldenRing"
        (self.steam / ACCOUNT).mkdir(parents=True)
        (self.steam / ACCOUNT / "ER0000.sl2").write_bytes(b"steam")
        self.organizer = FakeOrganizer()
        self.game = game_for(self.organizer)

    def session(self, manifest=None, folders=(ACCOUNT,)) -> Path:
        session = self.game._save_quarantine_root() / "s1"
        session.mkdir(parents=True)
        for name in folders:
            (session / name).mkdir()
            (session / name / "ER0000.sl2").write_bytes(b"hidden")
        if manifest is not None:
            text = manifest if isinstance(manifest, str) else json.dumps(manifest)
            (session / "session.json").write_text(text, encoding="utf-8")
        return session


class BrokenRecords(Base):
    def manifest(self, **values):
        data = {"global_save_root": str(self.steam), "directories": [{"name": "x"}]}
        data.update(values)
        return data

    def test_records_refused(self):
        cases = {
            "broken json": "{",
            "not a dict": "[]",
            "no save root": self.manifest(global_save_root="  "),
            "other save root": self.manifest(global_save_root="Z:\\home\\user\\EldenRing"),
            "not a list": self.manifest(directories="x"),
            "path in the name": self.manifest(directories=[{"name": "../x"}]),
            "drive in the name": self.manifest(directories=[{"name": "C:x"}]),
            "forward slash": self.manifest(directories=[{"name": "a/b"}]),
            "dot": self.manifest(directories=[{"name": ".."}]),
            "not a string": self.manifest(directories=[{"name": 5}]),
            "duplicate": self.manifest(directories=[{"name": "A"}, {"name": "a"}]),
            "swap without route": self.manifest(swap={"route": "relative/route"}),
        }
        for label, manifest in cases.items():
            with self.subTest(label):
                session = self.session(manifest, folders=())
                with self.assertRaises(OSError):
                    self.game._restore_global_save_quarantine(session, self.steam)
                for child in session.iterdir():
                    child.unlink() if child.is_file() else os.rmdir(child)
                session.rmdir()

    def test_not_utf8_record(self):
        session = self.session(None, folders=())
        (session / "session.json").write_bytes(b'{"global_save_root": "\xff"}')
        with self.assertRaises(OSError):
            self.game._restore_global_save_quarantine(session, self.steam)

    def test_same_root_spelled_another_way(self):
        # forward slashes and another case still name the Steam folder
        spelled = str(self.steam).replace("\\", "/").upper()
        session = self.session({"global_save_root": spelled, "directories": []}, folders=())
        self.assertTrue(self.game._restore_global_save_quarantine(session, self.steam))

    def test_missing_session_is_done(self):
        self.assertTrue(self.game._restore_global_save_quarantine(self.appdata / "nothing", self.steam))

    def test_record_lost_in_a_crash(self):
        # the folder was moved but its record never written
        session = self.session(None)
        (session / self.game.UnroutedSaveDirectoryName).mkdir()
        (self.steam / ACCOUNT / "ER0000.sl2").write_bytes(b"newer")
        os.utime(self.steam / ACCOUNT / "ER0000.sl2", (time.time() + 60, time.time() + 60))
        self.assertFalse(self.game._restore_global_save_quarantine(session, self.steam))
        self.assertEqual((self.steam / ACCOUNT / "ER0000.sl2").read_bytes(), b"hidden")
        # the newer folder in the way is kept, not deleted
        self.assertEqual(len(self.game._displaced_newer_saves), 1)
        self.assertEqual((self.game._displaced_newer_saves[0] / "ER0000.sl2").read_bytes(), b"newer")

    def test_unexpected_older_folder_in_the_way(self):
        session = self.session({"global_save_root": str(self.steam), "directories": [{"name": ACCOUNT}]})
        unrouted = session / self.game.UnroutedSaveDirectoryName / ACCOUNT
        unrouted.mkdir(parents=True)
        os.utime(self.steam / ACCOUNT / "ER0000.sl2", (1, 1))
        self.assertFalse(self.game._restore_global_save_quarantine(session, self.steam))
        self.assertEqual(self.game._displaced_newer_saves, [])
        self.assertEqual(len(list((session / self.game.UnroutedSaveDirectoryName).iterdir())), 2)


class Sessions(Base):
    def other_install(self, manifest: dict, folders=(ACCOUNT,), name="b" * 24) -> Path:
        session = self.game._save_quarantine_root().parent / name / "s"
        session.mkdir(parents=True)
        for folder in folders:
            (session / folder).mkdir()
        (session / "session.json").write_text(json.dumps(manifest), encoding="utf-8")
        return session

    def test_pending(self):
        session = self.session(None, folders=())
        self.assertTrue(self.game._session_is_pending(session))  # empty: still to clean
        (session / self.game.UnroutedSaveDirectoryName).mkdir()
        self.assertFalse(self.game._session_is_pending(session))
        (session / "session.json").write_text(json.dumps({"swap": {"returned": False}}))
        self.assertTrue(self.game._session_is_pending(session))
        (session / "session.json").write_text(json.dumps({"swap": {"returned": True}}))
        self.assertFalse(self.game._session_is_pending(session))
        (session / "session.json").write_text("[]")
        self.assertFalse(self.game._swap_not_returned(session))

    def test_other_installs(self):
        live = self.other_install({"global_save_root": str(self.steam), "install_root": str(self.organizer.base)})
        gone = self.other_install({"global_save_root": str(self.steam), "install_root": "Z:/home/user/gone"},
                                  name="c" * 24)
        self.other_install({"global_save_root": "Z:\\home\\user\\.steam\\EldenRing",
                            "install_root": str(self.organizer.base)}, name="d" * 24)
        self.other_install({"global_save_root": str(self.steam)}, folders=(), name="e" * 24)
        broken = self.game._save_quarantine_root().parent / ("f" * 24) / "s"
        (broken / ACCOUNT).mkdir(parents=True)
        (broken / "session.json").write_text("{", encoding="utf-8")
        self.assertEqual(self.game._foreign_pending_sessions(), [live])
        self.assertEqual(self.game._orphaned_sessions(), [gone])
        # a session just written may belong to a launch that is starting
        self.assertEqual(self.game._stale_foreign_sessions(), [])
        old = time.time() - 3600
        os.utime(live / "session.json", (old, old))
        self.assertEqual(self.game._stale_foreign_sessions(), [live])

    def test_orphaned_session_restored_at_start(self):
        (self.steam / ACCOUNT / "ER0000.sl2").unlink()
        (self.steam / ACCOUNT).rmdir()
        gone = self.other_install({"global_save_root": str(self.steam), "install_root": "",
                                   "directories": [{"name": ACCOUNT}]})
        with no_game_running(self.game):
            self.assertTrue(self.game._recover_stale_global_save_quarantines())
        self.assertEqual(self.game._recovered_sessions, 1)
        self.assertTrue((self.steam / ACCOUNT).is_dir())
        self.assertFalse(gone.exists())

    def test_recovery_waits_for_the_game(self):
        self.session({"global_save_root": str(self.steam), "directories": [{"name": ACCOUNT}]})
        with mock.patch.object(type(self.game), "_elden_ring_process_state", return_value=True), \
                mock.patch.object(type(self.game), "_open_launcher", return_value=None):
            self.assertFalse(self.game._recover_stale_global_save_quarantines())
        self.assertTrue(self.game._recovery_waiting_for_game)

    def test_recovery_of_a_broken_session_fails(self):
        self.session("[]")
        with no_game_running(self.game):
            self.assertFalse(self.game._recover_stale_global_save_quarantines())
        self.assertFalse(self.game._recovery_waiting)

    def test_session_held_by_another_mo2(self):
        # made first: its start would put the session back itself
        holder = game_for(FakeOrganizer())
        session = self.session({"global_save_root": str(self.steam), "directories": [{"name": ACCOUNT}]})
        self.assertTrue(holder._claim_save_quarantine(session))
        try:
            with no_game_running(self.game):
                self.assertFalse(self.game._recover_stale_global_save_quarantines())
            self.assertTrue(self.game._recovery_waiting)
            # the holder itself may take it again
            self.assertTrue(holder._claim_save_quarantine(session))
        finally:
            holder._release_save_quarantine(session)
        self.assertFalse((session / holder.SaveRecoveryLockName).exists())
        self.assertTrue(self.game._claim_save_quarantine(session))
        self.game._release_save_quarantine(session)

    def test_lock_in_a_folder_that_is_gone(self):
        self.assertTrue(self.game._claim_save_quarantine(self.appdata / "no-such-folder"))
        self.game._release_save_quarantine(self.appdata / "no-such-folder")

    def test_account_folders(self):
        check = self.game._save_directory_contains_elden_ring_saves
        folder = Path(tempfile.mkdtemp(prefix="er-acct-"))
        self.assertFalse(check(folder))
        (folder / "notes.txt").write_text("x")
        (folder / "sub").mkdir()
        self.assertFalse(check(folder))
        for name in ("steam_autocloud.vdf", "ER0000.co2.bak", "save.cnvco2"):
            with self.subTest(name):
                (folder / name).write_text("x")
                self.assertTrue(check(folder))
                (folder / name).unlink()
        steam_id = folder / ACCOUNT
        steam_id.mkdir()
        self.assertTrue(check(steam_id))

    def test_newest_save_time_of_a_missing_folder(self):
        self.assertEqual(self.game._newest_save_time(self.appdata / "gone"), 0)


class Processes(Base):
    def test_state_from_a_list(self):
        cls = type(self.game)
        self.assertTrue(cls._elden_ring_process_state([(1, "eldenring.exe")]))
        self.assertFalse(cls._elden_ring_process_state([(1, "nightreign.exe")]))

    def test_state_from_tasklist(self):
        cls = type(self.game)
        result = mock.Mock(returncode=0, stdout='"eldenring.exe","123","Console"\n')
        with mock.patch.object(recovery, "_running_process_names", return_value=None), \
                mock.patch.object(recovery.subprocess, "run", return_value=result):
            self.assertTrue(cls._elden_ring_process_state())
        result = mock.Mock(returncode=0, stdout="INFO: No tasks are running\n")
        with mock.patch.object(recovery, "_running_process_names", return_value=None), \
                mock.patch.object(recovery.subprocess, "run", return_value=result):
            self.assertFalse(cls._elden_ring_process_state())
        with mock.patch.object(recovery, "_running_process_names", return_value=None), \
                mock.patch.object(recovery.subprocess, "run", return_value=mock.Mock(returncode=1, stdout="")):
            self.assertIsNone(cls._elden_ring_process_state())
        with mock.patch.object(recovery, "_running_process_names", return_value=None), \
                mock.patch.object(recovery.subprocess, "run", side_effect=OSError("no tasklist")):
            self.assertIsNone(cls._elden_ring_process_state())

    def test_open_launcher(self):
        game_exe = str(Path(self.game.gameDirectory().absolutePath()) / "start_protected_game.exe")
        other_exe = str(self.appdata / "OtherGame" / "start_protected_game.exe")
        self.assertIsNone(self.game._open_launcher([(1, "eldenring.exe"), (2, "notepad.exe")]))
        self.assertEqual(self.game._open_launcher([(3, "reforgedlauncher.exe")]), "reforgedlauncher.exe")
        with mock.patch.object(recovery, "_process_image_path", return_value=other_exe):
            # Easy Anti-Cheat's launcher of another game
            self.assertIsNone(self.game._open_launcher([(4, "start_protected_game.exe")]))
        with mock.patch.object(recovery, "_process_image_path", return_value=game_exe.upper()):
            self.assertEqual(self.game._open_launcher([(4, "start_protected_game.exe")]),
                             "start_protected_game.exe")
        with mock.patch.object(recovery, "_process_image_path", return_value=None):
            self.assertEqual(self.game._open_launcher([(4, "start_protected_game.exe")]),
                             "start_protected_game.exe")
        with mock.patch.object(recovery, "_running_processes", return_value=None):
            self.assertIsNone(self.game._open_launcher())


class LaunchWatch(Base):
    def setUp(self):
        super().setUp()
        with quiet_ui(recovery):
            self.session = self.game._hide_existing_global_save_directories()

    def watch(self, state, launcher=None, remaining=0.0):
        cls = type(self.game)
        with quiet_ui(recovery), mock.patch.object(recovery, "_running_processes", return_value=[]), \
                mock.patch.object(cls, "_elden_ring_process_state", return_value=state), \
                mock.patch.object(cls, "_open_launcher", return_value=launcher):
            self.game._watch_launch_start(self.session, self.steam, remaining)
            return list(FakeTimer.calls)

    def test_game_seen(self):
        self.assertEqual(self.watch(True), [])
        self.assertEqual(self.game._game_seen_run, self.game._game_run)
        self.assertEqual(self.game._active_save_quarantine, self.session)

    def test_waits_while_time_is_left_or_a_launcher_is_open(self):
        self.assertEqual(len(self.watch(False, remaining=2)), 1)
        self.assertEqual(len(self.watch(False, launcher="reforgedlauncher.exe")), 1)
        self.assertGreater(self.game._launch_pending_until, 0)

    def test_never_started(self):
        self.assertEqual(self.watch(False), [])
        self.assertIsNone(self.game._active_save_quarantine)
        self.assertTrue((self.steam / ACCOUNT / "ER0000.sl2").is_file())

    def test_state_unknown_keeps_them_hidden(self):
        self.watch(None)
        self.assertEqual(self.game._active_save_quarantine, self.session)

    def test_another_launch_took_over(self):
        self.game._active_save_quarantine = None
        self.game._launch_pending_until = 99.0
        self.watch(False)
        self.assertEqual(self.game._launch_pending_until, 0.0)

    def test_restore_after_a_failed_start_that_breaks(self):
        (self.session / "session.json").write_text("{", encoding="utf-8")
        self.watch(False)
        self.assertEqual(self.game._active_save_quarantine, self.session)


class HideErrors(Base):
    def test_nothing_to_hide(self):
        (self.steam / ACCOUNT / "ER0000.sl2").unlink()
        (self.steam / ACCOUNT).rmdir()
        (self.steam / "other").mkdir()
        with quiet_ui(recovery):
            self.assertIsNone(self.game._hide_existing_global_save_directories())

    def test_no_steam_folder(self):
        os.environ["APPDATA"] = str(self.appdata / "nowhere")
        with quiet_ui(recovery):
            self.assertIsNone(self.game._hide_existing_global_save_directories())

    def test_earlier_session_blocks(self):
        self.session("[]")
        with quiet_ui(recovery), no_game_running(self.game), self.assertRaises(OSError):
            self.game._hide_existing_global_save_directories()

    def test_failed_move_is_rolled_back(self):
        real = os.replace

        def replace(source, target):
            # the second account folder is in use; the first has moved
            if Path(source).name == "76561198000000002":
                raise PermissionError("in use")
            return real(source, target)

        (self.steam / "76561198000000002").mkdir()
        with quiet_ui(recovery), no_game_running(self.game), \
                mock.patch.object(recovery.os, "replace", replace), self.assertRaises(OSError):
            self.game._hide_existing_global_save_directories()
        self.assertIsNone(self.game._active_save_quarantine)
        self.assertTrue((self.steam / ACCOUNT / "ER0000.sl2").is_file())

    def test_seed_route_files(self):
        (self.steam / "GraphicsConfig.xml").write_text("steam")
        route = self.appdata / "route"
        self.game._seed_route_root_files(self.steam, route)
        self.assertEqual((route / "GraphicsConfig.xml").read_text(), "steam")
        (route / "GraphicsConfig.xml").write_text("route")
        self.game._seed_route_root_files(self.steam, route)
        self.assertEqual((route / "GraphicsConfig.xml").read_text(), "route")
        self.game._seed_route_root_files(self.appdata / "missing", route)
        with mock.patch.object(recovery.shutil, "copy2", side_effect=PermissionError("denied")):
            self.game._seed_route_root_files(self.steam, self.appdata / "route2")
        self.assertFalse((self.appdata / "route2" / "GraphicsConfig.xml").exists())


class LaunchMethodsFile(unittest.TestCase):
    cls = er.EldenRingMo2Game

    def setUp(self):
        self.base = Path(tempfile.mkdtemp(prefix="er-methods-"))
        self.path = self.cls.launch_methods_path(self.base)

    def test_missing_or_unreadable(self):
        self.assertEqual(self.cls.read_launch_methods(self.base), {})
        self.assertTrue(self.cls.reforged_online_saves_separate(self.base))
        for raw in (b"{", b"[1]", b'{"methods": [1]}', b'{"methods": {"a": "\xff"}}'):
            with self.subTest(raw=raw):
                self.path.write_bytes(raw)
                self.assertEqual(self.cls.read_launch_methods(self.base), {})
                self.assertTrue(self.cls.reforged_online_saves_separate(self.base))

    def test_methods_and_shared_saves(self):
        self.path.write_text(json.dumps({
            "methods": {"z:\\home\\user\\tool.exe": "swap", "c:\\x.exe": "other"},
            "reforged_online_saves": "shared"}), encoding="utf-8")
        self.assertEqual(self.cls.read_launch_methods(self.base), {"z:\\home\\user\\tool.exe": "swap"})
        self.assertFalse(self.cls.reforged_online_saves_separate(self.base))

    def test_a_bom_is_not_read(self):
        # Save Isolation writes it without one; a hand edit that adds one
        # gives the defaults, as in Save Isolation itself
        self.path.write_bytes(b"\xef\xbb\xbf" + json.dumps({"reforged_online_saves": "shared"}).encode())
        self.assertTrue(self.cls.reforged_online_saves_separate(self.base))

    def test_wine_paths_match(self):
        # Proton's drive letter and forward slashes name the same program
        key = os.path.normcase(os.path.abspath("Z:\\home\\user\\Games\\tool.exe"))
        methods = {key: "swap"}
        self.assertEqual(self.cls.launch_method_for("z:/home/user/Games/TOOL.exe", methods), ("swap", True))
        self.assertEqual(self.cls.launch_method_for("", methods), ("vfs", False))

    def test_launch_method_without_an_instance(self):
        appdata_folder()
        organizer = FakeOrganizer()
        game = game_for(organizer)
        with mock.patch.object(organizer, "basePath", side_effect=RuntimeError("closing")):
            self.assertEqual(game._launch_method("C:/x/tool.exe"), "vfs")


class Swap(Base):
    def setUp(self):
        super().setUp()
        self.route = self.organizer.profile_dir / "saves"
        (self.route / ACCOUNT).mkdir(parents=True)
        (self.route / ACCOUNT / "ER0000.sl2").write_bytes(b"route")

    def test_online_route_starts_from_a_copy(self):
        (self.route / "GraphicsConfig.xml").write_text("x")
        (self.route / "save_profiles").mkdir()
        (self.route / "not saves").mkdir()
        online = self.game._reforged_online_route(self.route)
        self.assertEqual((online / ACCOUNT / "ER0000.sl2").read_bytes(), b"route")
        self.assertTrue((online / "GraphicsConfig.xml").is_file())
        self.assertFalse((online / "save_profiles").exists())
        self.assertFalse((online / "not saves").exists())
        # its own afterwards
        (online / ACCOUNT / "ER0000.sl2").write_bytes(b"online")
        self.assertEqual((self.game._reforged_online_route(self.route) / ACCOUNT / "ER0000.sl2").read_bytes(),
                         b"online")

    def test_route_that_does_not_exist_yet(self):
        missing = self.organizer.profile_dir / "empty route"
        with quiet_ui(recovery):
            session = self.game._hide_existing_global_save_directories(missing)
        self.assertEqual(list(self.steam.iterdir()), [])
        self.assertTrue(self.game._restore_global_save_quarantine(session, self.steam))
        self.assertTrue((self.steam / ACCOUNT / "ER0000.sl2").is_file())

    def test_played_saves_go_to_the_route_newer_ones_kept(self):
        (self.steam / "GraphicsConfig.xml").write_text("steam")
        (self.route / "GraphicsConfig.xml").write_text("route")
        with quiet_ui(recovery):
            session = self.game._hide_existing_global_save_directories(self.route)
        self.assertEqual((self.steam / "GraphicsConfig.xml").read_text(), "route")
        played = self.steam / ACCOUNT / "ER0000.sl2"
        played.write_bytes(b"played")
        os.utime(played, (1000, 1000))
        # the route was written meanwhile, later than what was played
        (self.route / ACCOUNT / "ER0000.sl2").write_bytes(b"route newer")
        self.assertTrue(self.game._restore_global_save_quarantine(session, self.steam) is False)
        self.assertEqual((self.route / ACCOUNT / "ER0000.sl2").read_bytes(), b"played")
        self.assertEqual([p.read_bytes() for p in self.game._kept_aside_saves], [b"route newer"])
        self.assertEqual((self.steam / "GraphicsConfig.xml").read_text(), "steam")
        self.assertEqual((self.steam / ACCOUNT / "ER0000.sl2").read_bytes(), b"steam")

    def test_swap_cut_short(self):
        # the swap stopped before the game ran: what is in the Steam folder
        # is the route's own copy (cut short), or Steam's played after a crash
        session = self.session({
            "global_save_root": str(self.steam), "directories": [{"name": ACCOUNT}],
            "swap": {"route": str(self.route), "complete": False, "steam_files_held": True}})
        (self.steam / ACCOUNT / "ER0000.sl2").write_bytes(b"rou")  # cut copy of "route"
        (self.steam / "76561198000000002").mkdir()
        (self.steam / "76561198000000002" / "ER0000.sl2").write_bytes(b"other")
        (self.steam / "loose.txt").write_text("not the route's")
        self.assertFalse(self.game._restore_global_save_quarantine(session, self.steam))
        self.assertEqual((self.route / ACCOUNT / "ER0000.sl2").read_bytes(), b"route")
        self.assertEqual((self.steam / ACCOUNT / "ER0000.sl2").read_bytes(), b"hidden")
        kept = {p.name for p in self.game._kept_aside_saves}
        self.assertEqual(kept, {"76561198000000002", "loose.txt"})

    def test_file_comparisons(self):
        folder = Path(tempfile.mkdtemp(prefix="er-cmp-"))
        (folder / "a").write_bytes(b"abc")
        (folder / "b").write_bytes(b"abcdef")
        (folder / "c").write_bytes(b"xbc")
        held = self.game._file_is_held_by
        self.assertTrue(held(folder / "a", folder / "b"))
        self.assertFalse(held(folder / "b", folder / "a"))
        self.assertFalse(held(folder / "c", folder / "b"))
        self.assertFalse(held(folder / "a", folder / "missing"))

    def test_keep_aside_twice(self):
        session = self.session(None, folders=())
        for _ in range(2):
            path = self.appdata / "thing.txt"
            path.write_text("x")
            self.game._keep_aside(session, path)
        self.assertEqual(len(list((session / self.game.UnroutedSaveDirectoryName).iterdir())), 2)


class ConsoleProgram(unittest.TestCase):
    cls = er.EldenRingMo2Game

    def image(self, subsystem: int, signature: bytes = b"PE\0\0") -> str:
        data = bytearray(0x200)
        data[:2] = b"MZ"
        data[0x3C:0x40] = (0x80).to_bytes(4, "little")
        data[0x80:0x84] = signature
        data[0x80 + 0x5C:0x80 + 0x5E] = subsystem.to_bytes(2, "little")
        path = Path(tempfile.mkdtemp(prefix="er-pe-")) / "launcher.exe"
        path.write_bytes(bytes(data))
        return str(path)

    def test_subsystems(self):
        self.assertTrue(self.cls._is_console_program(self.image(3)))
        self.assertFalse(self.cls._is_console_program(self.image(2)))
        self.assertFalse(self.cls._is_console_program(self.image(3, b"NE\0\0")))
        self.assertFalse(self.cls._is_console_program(str(Path(tempfile.gettempdir()) / "no-such.exe")))


class SeamlessInMods(unittest.TestCase):
    def test_highest_priority_active(self):
        base = Path(tempfile.mkdtemp(prefix="er-sc-"))
        for name in ("low", "high", "off"):
            (base / name / "SeamlessCoop").mkdir(parents=True)
            (base / name / "SeamlessCoop" / "ersc.dll").write_bytes(b"MZ")
        mods = [("low", base / "low", True), ("high", base / "high", True), ("off", base / "off", False)]
        self.assertEqual(er.EldenRingMo2Game.seamless_dll_in_mods(mods)[0], "high")
        self.assertIsNone(er.EldenRingNightreignMo2Game.seamless_dll_in_mods(mods))


if __name__ == "__main__":
    unittest.main()
