"""A launch from MO2 (launch.py): the profile's launch settings in odd
encodings, the bridge log rotation, the checks that block a launch, the
launch outside the VFS, and what runs once the game has closed. The game
never starts: the process list says it is closed, and message boxes and
timers are stand-ins."""

from __future__ import annotations

import json
import os
import time
import unittest
from pathlib import Path
from unittest import mock

from basic_games.games.eldenring import checks, interface, launch, programs, recovery

from fake_organizer import FakeOrganizer, FakeProfile, appdata_folder, game_for
from test_cov_game_fakes import FakeTimer, no_game_running, private_temp, quiet_ui

ACCOUNT = "76561198000000001"


def write_ini(path: Path, text: str, encoding: str = "utf-8") -> Path:
    path.write_bytes(text.encode(encoding))
    return path


class LaunchSettings(unittest.TestCase):
    """eldenring_mo2_startup.ini as the launch reads it."""

    def setUp(self):
        appdata_folder()
        self.organizer = FakeOrganizer()
        self.game = game_for(self.organizer)
        self.ini = self.organizer.profile_dir / "eldenring_mo2_startup.ini"

    def read(self):
        return self.game._read_profile_launch_settings(self.ini)

    def test_missing_file_defaults(self):
        self.assertEqual(self.read(), (False, False, False, False, False, 0, 1))
        self.assertFalse(self.game._read_clear_overwrite_after_game(self.ini))
        self.assertTrue(self.game._read_boot_boost(self.ini))
        self.assertTrue(self.game._read_skip_intro_logos(self.ini))

    def test_all_on_with_bom(self):
        write_ini(self.ini, (
            "[Startup]\nstart_minimized = true\nblack_startup_background = yes\n"
            "boot_boost = off\nskip_intro_logos = 0\n"
            "[Performance]\nexclude_cpu0_after_start = 1\nprocess_priority = 1\n"
            "[Cleanup]\nclear_overwrite_after_game = true\nclear_overwrite_logs_before_game = true\n"
            "[Diagnostics]\nprevious_bridge_sessions = 5\n"), "utf-8-sig")
        self.assertEqual(self.read(), (True, True, True, True, True, 1, 5))
        self.assertTrue(self.game._read_clear_overwrite_after_game(self.ini))
        self.assertFalse(self.game._read_boot_boost(self.ini))
        self.assertFalse(self.game._read_skip_intro_logos(self.ini))

    def test_older_names_of_start_minimized(self):
        write_ini(self.ini, "[Startup]\nprevent_focus_steal = true\n")
        self.assertTrue(self.read()[0])
        write_ini(self.ini, "[Startup]\nstart_in_background = true\n")
        self.assertTrue(self.read()[0])

    def test_out_of_range_numbers(self):
        write_ini(self.ini, "[Performance]\nprocess_priority = 3\n[Diagnostics]\nprevious_bridge_sessions = 4\n")
        self.assertEqual(self.read()[5:], (0, 1))

    def test_bad_values_and_encodings_give_safe_defaults(self):
        for label, data in {
            "not a boolean": b"[Startup]\nstart_minimized = maybe\nboot_boost = maybe\nskip_intro_logos = maybe\n"
                             b"[Cleanup]\nclear_overwrite_after_game = maybe\n",
            "not a number": b"[Performance]\nprocess_priority = high\n",
            "code page": "[Startup]\nstart_minimized = true\n; cañón\n".encode("cp1252"),
            "no section": b"start_minimized = true\n",
        }.items():
            with self.subTest(label):
                self.ini.write_bytes(data)
                self.assertEqual(self.read(), (False, False, False, False, False, 0, 1))
                self.assertFalse(self.game._read_clear_overwrite_after_game(self.ini))
                self.assertTrue(self.game._read_boot_boost(self.ini))
                self.assertTrue(self.game._read_skip_intro_logos(self.ini))


class BridgeLogRotation(unittest.TestCase):
    rotate = staticmethod(launch.LaunchMixin._preserve_previous_bridge_log)

    def test_nothing_to_keep(self):
        with private_temp() as temp:
            self.rotate("TestBridge", 3)
            self.assertEqual(list(temp.iterdir()), [])

    def test_keeps_the_previous_sessions(self):
        with private_temp() as temp:
            (temp / "TestBridge.log").write_text("run 3")
            (temp / "TestBridge.previous.log").write_text("run 2")
            (temp / "TestBridge.previous-2.log").write_text("run 1")
            self.rotate("TestBridge", 3)
            self.assertEqual((temp / "TestBridge.previous.log").read_text(), "run 3")
            self.assertEqual((temp / "TestBridge.previous-2.log").read_text(), "run 2")
            self.assertEqual((temp / "TestBridge.previous-3.log").read_text(), "run 1")
            self.assertFalse(any(p.name.endswith(".tmp") for p in temp.iterdir()))

    def test_unknown_count_keeps_one(self):
        with private_temp() as temp:
            (temp / "TestBridge.log").write_text("now")
            (temp / "TestBridge.previous.log").write_text("before")
            self.rotate("TestBridge", 7)
            self.assertEqual((temp / "TestBridge.previous.log").read_text(), "now")
            self.assertFalse((temp / "TestBridge.previous-2.log").exists())

    def test_a_failed_copy_still_starts_the_game(self):
        with private_temp() as temp:
            (temp / "TestBridge.log").write_text("now")
            with mock.patch.object(launch.shutil, "copyfile", side_effect=PermissionError("locked")):
                self.rotate("TestBridge", 1)
            self.assertFalse((temp / "TestBridge.previous.log").exists())


class LaunchBase(unittest.TestCase):
    """A game with a Steam account folder, its profile route prepared."""

    def setUp(self):
        self.appdata = appdata_folder()
        self.steam = self.appdata / "EldenRing"
        (self.steam / ACCOUNT).mkdir(parents=True)
        (self.steam / ACCOUNT / "ER0000.sl2").write_bytes(b"steam")
        self.organizer = FakeOrganizer()
        self.game = game_for(self.organizer)
        self.app = str(self.organizer.game_dir / "Game" / "eldenring.exe")
        self.temp_context = private_temp()
        self.temp = self.temp_context.__enter__()
        self.addCleanup(self.temp_context.__exit__, None, None, None)
        self.environment = dict(os.environ)
        self.addCleanup(self.restore_environment)

    def restore_environment(self):
        self.game._restore_profile_config_environment()
        for key in set(os.environ) - set(self.environment):
            os.environ.pop(key, None)

    def prepare(self):
        self.game._local_savegames_feature.prepareProfile(self.organizer.profile())

    def saves_mode(self, mode: str, local_saves: bool | None = None, extra: str = ""):
        (self.organizer.profile_dir / "eldenring_mo2_saves.ini").write_text(
            f"[Saves]\nmode = {mode}\n{extra}", encoding="utf-8")
        if local_saves is not None:
            self.organizer._profile = FakeProfile(self.organizer.profile_dir, local_saves)

    def launch(self, app=None, launcher=None, answer=1):
        with quiet_ui(launch, recovery, checks, interface, programs, answer=answer) as box, \
                no_game_running(self.game, launcher), \
                mock.patch.object(launch, "_running_processes", return_value=[]):
            result = self.game._prepare_profile_launch(app or self.app, None, "")
        return result, box.shown


class CheckedLaunch(LaunchBase):
    def test_direct_launch_hides_and_restores_the_steam_saves(self):
        self.prepare()
        route = self.organizer.profile_dir / "saves" / ACCOUNT
        route.mkdir(parents=True)
        (route / "ER0000.sl2").write_bytes(b"route")
        (self.steam / "GraphicsConfig.xml").write_text("steam")
        write_ini(self.organizer.profile_dir / "eldenring_mo2_startup.ini",
                  "[Cleanup]\nclear_overwrite_after_game = true\n")
        (self.temp / "EldenRingMO2Bridge.log").write_text("earlier run")
        result, shown = self.launch()
        self.assertTrue(result, shown)
        session = self.game._active_save_quarantine
        self.assertIsNotNone(session)
        self.assertFalse((self.steam / ACCOUNT).exists())
        # the route got the Steam folder's loose file, and a backup
        self.assertTrue((self.organizer.profile_dir / "saves" / "GraphicsConfig.xml").is_file())
        self.assertTrue(any((self.organizer.profile_dir / "saves Backups" / "auto").iterdir()))
        self.assertEqual(os.environ.get(self.game.ProfileLaunchConfigEnvironment),
                         str(self.organizer.profile_dir / "eldenring_mo2_startup.ini"))
        self.assertTrue((self.temp / "EldenRingMO2Bridge.previous.log").is_file())
        self.assertTrue(self.game._overwrite_cleanup_armed)
        (Path(self.organizer.overwritePath()) / "left.txt").write_text("x")

        with quiet_ui(launch, recovery, interface) as box, no_game_running(self.game), \
                mock.patch.object(launch, "_running_processes", return_value=[]):
            self.game._finish_profile_launch(self.app, 0)
        self.assertEqual((self.steam / ACCOUNT / "ER0000.sl2").read_bytes(), b"steam")
        self.assertIsNone(self.game._active_save_quarantine)
        self.assertNotIn(self.game.ProfileLaunchConfigEnvironment, os.environ)
        self.assertEqual(list(Path(self.organizer.overwritePath()).iterdir()), [])
        # the closed-at-once notice waits a moment to see the game come back
        self.assertEqual(len(FakeTimer.calls), 1)

    def test_global_route_leaves_the_steam_saves(self):
        self.saves_mode("global", local_saves=False)
        self.prepare()
        result, shown = self.launch()
        self.assertTrue(result, shown)
        self.assertIsNone(self.game._active_save_quarantine)
        self.assertTrue((self.steam / ACCOUNT / "ER0000.sl2").is_file())
        backups = self.organizer.base / self.game.GlobalBackupsFolderName / "auto"
        self.assertTrue(any(backups.iterdir()))

    def test_tools_and_other_games_pass(self):
        self.prepare()
        self.assertTrue(self.launch(str(self.organizer.base / "SaveEditor.exe"))[0])
        self.organizer.managed = None
        self.assertTrue(self.launch()[0])
        self.assertIsNone(self.game._active_save_quarantine)

    def test_route_changed_after_mo2_loaded_it(self):
        result, shown = self.launch()
        self.assertFalse(result)
        self.assertIn("Reload", shown[0][1])
        self.assertTrue((self.steam / ACCOUNT).is_dir())

    def test_isolated_route_without_mo2_profile_saves(self):
        self.saves_mode("profile", local_saves=False)
        self.prepare()
        result, shown = self.launch()
        self.assertFalse(result)
        self.assertIn("not active", shown[0][1])

    def test_global_route_with_mo2_profile_saves(self):
        self.saves_mode("global", local_saves=True)
        self.prepare()
        result, shown = self.launch()
        self.assertFalse(result)
        self.assertIn("does not match", shown[0][1])

    def test_missing_named_save_profile(self):
        self.saves_mode("profile", extra="profile_save_profile = " + "a" * 32 + "\n")
        self.prepare()
        result, shown = self.launch()
        self.assertFalse(result)
        self.assertIn("missing", shown[0][1])

    def test_launch_still_waiting_for_its_game(self):
        self.prepare()
        self.game._active_save_quarantine = self.appdata / "session"
        self.game._launch_pending_until = time.monotonic() + 60
        result, shown = self.launch()
        self.assertFalse(result)
        self.assertEqual(shown[0][0], "information")
        # with a launcher open, the message names it
        result, shown = self.launch(launcher="start_protected_game.exe")
        self.assertFalse(result)
        self.assertIn("start_protected_game.exe", shown[0][2])
        self.assertTrue(self.game._recovery_waiting)

    def test_stale_session_of_this_launch_is_restored_first(self):
        self.prepare()
        with quiet_ui(recovery):
            session = self.game._hide_existing_global_save_directories()
        self.assertFalse((self.steam / ACCOUNT).exists())
        # that launch's game never started and its wait is over
        self.game._launch_pending_until = 0.0
        result, shown = self.launch()
        self.assertTrue(result, shown)
        self.assertNotEqual(self.game._active_save_quarantine, session)
        self.assertFalse(session.exists())

    def test_another_install_hides_the_steam_saves(self):
        self.prepare()
        own = self.game._save_quarantine_root()
        other = own.parent / ("f" * 24) / "session"
        (other / ACCOUNT).mkdir(parents=True)
        (other / "session.json").write_text(json.dumps({
            "global_save_root": str(self.steam), "install_root": str(self.organizer.base),
            "directories": [{"name": ACCOUNT}]}), encoding="utf-8")
        result, shown = self.launch()
        self.assertFalse(result)
        self.assertIn(str(other), shown[0][2])

    def test_broken_native_profile(self):
        self.prepare()
        write_ini(self.organizer.profile_dir / "eldenring_mo2_startup.ini",
                  "[RevampedNativeProfile]\nbogus = 1\n")
        result, shown = self.launch()
        self.assertFalse(result)
        self.assertIn("native profile", shown[0][1])
        # a launcher goes ahead without it
        self.assertTrue(self.launch(str(self.organizer.game_dir / "Game" / "start_protected_game.exe"))[0])

    def test_native_profile_with_native_loading_off(self):
        self.organizer.add_mod("A", ["DLLs/a.dll"])
        self.organizer.settings["load_native_dlls"] = False
        self.prepare()
        write_ini(self.organizer.profile_dir / "eldenring_mo2_startup.ini",
                  "[RevampedNativeProfile]\nnative_01 = DLLs/a.dll\nmod_01 = A\n")
        result, shown = self.launch()
        self.assertFalse(result)
        self.assertIn("disabled", shown[0][1])
        self.assertTrue(self.launch(str(self.organizer.game_dir / "Game" / "start_protected_game.exe"))[0])

    def test_an_error_puts_everything_back(self):
        self.prepare()
        hidden = []

        def fail(*_args):
            hidden.append(self.game._hide_existing_global_save_directories())
            self.game._mod_turned_on_for_launch = "Reforged"
            raise RuntimeError("boom")

        self.organizer.add_mod("Reforged", [], active=True)
        with mock.patch.object(type(self.game), "_check_profile_launch", side_effect=fail):
            result, shown = self.launch()
        self.assertFalse(result)
        self.assertEqual(shown[-1][0], "warning")
        self.assertIsNone(self.game._active_save_quarantine)
        self.assertTrue((self.steam / ACCOUNT / "ER0000.sl2").is_file())
        self.assertNotIn("Reforged", self.organizer.active)
        self.assertIsNone(type(self.game)._executable_reads)

    def test_saves_that_cannot_be_hidden(self):
        self.prepare()
        with mock.patch.object(type(self.game), "_hide_existing_global_save_directories",
                               side_effect=PermissionError("in use")):
            result, shown = self.launch()
        self.assertFalse(result)
        self.assertEqual(shown[0][0], "critical")


class SwapLaunch(LaunchBase):
    """A launcher started outside the VFS: the route's saves in the Steam
    folder for the run."""

    def setUp(self):
        super().setUp()
        self.launcher_mod = self.organizer.add_mod(
            "Reforged", ["_ReforgedOnline/internals/launcher/ReforgedLauncher.exe"])
        self.launcher = str(self.launcher_mod / "_ReforgedOnline/internals/launcher/ReforgedLauncher.exe")
        route = self.organizer.profile_dir / "saves" / ACCOUNT
        route.mkdir(parents=True)
        (route / "ER0000.sl2").write_bytes(b"route")
        self.prepare()

    def test_started(self):
        with mock.patch.object(type(self.game), "_start_outside_vfs", return_value=True):
            result, shown = self.launch(self.launcher)
        # MO2's own run is cancelled, the watch waits for the game
        self.assertFalse(result)
        self.assertEqual(shown, [])
        self.assertIsNotNone(self.game._active_save_quarantine)
        online = self.organizer.profile_dir / "saves" / "reforged_online" / ACCOUNT / "ER0000.sl2"
        self.assertEqual(online.read_bytes(), b"route")
        # Reforged plays its .err: Steam's own .sl2 stays where Steam Cloud looks
        self.assertEqual((self.steam / ACCOUNT / "ER0000.sl2").read_bytes(), b"steam")
        self.assertTrue(any(callback for _interval, callback in FakeTimer.calls))

    def test_not_started_puts_the_saves_back(self):
        with mock.patch.object(type(self.game), "_start_outside_vfs", side_effect=OSError("denied")):
            result, shown = self.launch(self.launcher)
        self.assertFalse(result)
        self.assertIn("launcher", shown[-1][1])
        self.assertIsNone(self.game._active_save_quarantine)
        self.assertEqual((self.steam / ACCOUNT / "ER0000.sl2").read_bytes(), b"steam")

    def test_online_mod_switched_off(self):
        self.organizer.active.discard("Reforged")
        with mock.patch.object(type(self.game), "_start_outside_vfs", return_value=True):
            result, shown = self.launch(self.launcher, answer=1)
        self.assertFalse(result)
        self.assertIn("off", shown[0][2])
        self.assertNotIn("Reforged", self.organizer.active)
        # "Turn on and start" turns it on and leaves it on once started
        with mock.patch.object(type(self.game), "_start_outside_vfs", return_value=True):
            self.launch(self.launcher, answer=0)
        self.assertIn("Reforged", self.organizer.active)

    def test_online_mod_turned_on_goes_off_when_blocked(self):
        self.organizer.active.discard("Reforged")
        with mock.patch.object(type(self.game), "_steam_cloud_swap_ready", return_value=False):
            result, _shown = self.launch(self.launcher, answer=0)
        self.assertFalse(result)
        self.assertNotIn("Reforged", self.organizer.active)


class AfterTheGame(LaunchBase):
    """The notices and the restore once the game has closed."""

    def setUp(self):
        super().setUp()
        self.organizer.add_mod("Loose", ["parts/a.dcx"])
        self.log = self.temp / "EldenRingMO2Bridge.log"

    def test_saves_protected_notice(self):
        self.game._last_launch_time = time.time() - 5
        self.assertFalse(self.game._warn_if_saves_were_protected())
        self.log.write_text("Save extension refused: literal not found\n")
        with quiet_ui(launch):
            self.assertTrue(self.game._warn_if_saves_were_protected())
            FakeTimer.run_all()
        self.assertTrue(self.game._early_exit_warning_shown)
        # a log older than the launch is another run's
        self.game._last_launch_time = time.time() + 60
        self.assertFalse(self.game._warn_if_saves_were_protected())

    def test_loose_files_skipped(self):
        self.game._last_launch_time = time.time() - 5
        self.log.write_bytes(b"Loose asset hook skipped \xff\n")
        with quiet_ui(launch) as box:
            self.game._warn_if_loose_files_were_skipped(False, True)
            FakeTimer.run_all()
        self.assertTrue(self.game._loose_files_warning_shown)
        self.assertEqual(box.shown[0][0], "warning")
        with quiet_ui(launch) as box:
            self.game._warn_if_loose_files_were_skipped(False, True)
        self.assertEqual(FakeTimer.calls, [])

    def test_bridge_did_not_load(self):
        self.game._last_launch_time = time.time() - 100
        with quiet_ui(launch) as box:
            # a launcher's run, or a game never seen, says nothing
            self.game._warn_if_loose_files_were_skipped(True, True)
            self.game._warn_if_loose_files_were_skipped(False, False)
            self.assertEqual(FakeTimer.calls, [])
            self.game._warn_if_loose_files_were_skipped(False, True)
            FakeTimer.run_all()
        self.assertIn("did not load", box.shown[0][1])

    def test_closed_at_once(self):
        self.game._last_launch_time = time.time()
        with quiet_ui(launch) as box, no_game_running(self.game):
            self.game._warn_if_game_closed_at_once(True, self.game._game_run)
            self.game._warn_if_game_closed_at_once(False, self.game._game_run + 1)
            self.assertEqual(FakeTimer.calls, [])
            self.game._warn_if_game_closed_at_once(False, self.game._game_run)
            FakeTimer.run_all()
            self.assertEqual(len(box.shown), 1)
            # once is enough
            self.game._show_early_exit_warning(self.game._last_launch_time)
        self.assertEqual(len(box.shown), 1)

    def test_still_running_waits(self):
        with quiet_ui(launch), mock.patch.object(type(self.game), "_elden_ring_process_state", return_value=True), \
                mock.patch.object(launch, "_running_processes", return_value=[]):
            self.game._finish_when_game_closes(False, None, False, None, 0.0, False)
        self.assertEqual(FakeTimer.calls[0][0], self.game.GameWatchInterval)

    def test_unknown_state_gives_up(self):
        with quiet_ui(launch), mock.patch.object(type(self.game), "_elden_ring_process_state", return_value=None), \
                mock.patch.object(launch, "_running_processes", return_value=None):
            self.game._finish_when_game_closes(False, None, False, None, 0.0, False, 0,
                                               self.game.LauncherWaitLimit)
        self.assertEqual(FakeTimer.calls, [])

    def test_launcher_still_open_and_its_notice(self):
        with quiet_ui(recovery):
            session = self.game._hide_existing_global_save_directories()
        self.game._launcher_open_since = time.monotonic() - 60
        with quiet_ui(launch) as box, no_game_running(self.game, "start_protected_game.exe"), \
                mock.patch.object(launch, "_running_processes", return_value=[]):
            self.game._finish_when_game_closes(True, session, False, None, 0.0, True)
            self.assertEqual(len(FakeTimer.calls), 1)
            self.assertEqual(box.shown[0][0], "show")
            self.assertIsNotNone(self.game._launcher_notice)
            # once per session
            self.game._update_launcher_notice(session, False, True, [])
            self.assertEqual(len(box.shown), 1)
        with no_game_running(self.game, None):
            self.game._update_launcher_notice(session, False, True, [])
        self.assertIsNone(self.game._launcher_notice)
        self.assertIsNone(self.game._launcher_open_since)

    def test_restore_failure_is_reported(self):
        with quiet_ui(recovery):
            session = self.game._hide_existing_global_save_directories()
        (session / "session.json").write_text("{broken", encoding="utf-8")
        with quiet_ui(launch) as box, no_game_running(self.game), \
                mock.patch.object(launch, "_running_processes", return_value=[]):
            self.game._finish_when_game_closes(False, session, False, None, 0.0, True)
        self.assertEqual(box.shown[-1][0], "critical")
        self.assertEqual(self.game._active_save_quarantine, session)

    def test_unrouted_output_is_reported(self):
        with quiet_ui(recovery):
            session = self.game._hide_existing_global_save_directories()
        (session / self.game.UnroutedSaveDirectoryName).mkdir()
        with quiet_ui(launch) as box, no_game_running(self.game), \
                mock.patch.object(launch, "_running_processes", return_value=[]):
            self.game._finish_when_game_closes(False, session, False, None, 0.0, True)
        self.assertIn("retained", box.shown[-1][1])

    def test_cleanup_turned_off_while_the_game_ran(self):
        overwrite = Path(self.organizer.overwritePath())
        (overwrite / "keep.txt").write_text("x")
        config = write_ini(self.organizer.profile_dir / "eldenring_mo2_startup.ini",
                           "[Cleanup]\nclear_overwrite_after_game = false\n")
        self.game._overwrite_cleanup_config = config
        with quiet_ui(launch), no_game_running(self.game), \
                mock.patch.object(launch, "_running_processes", return_value=[]):
            self.game._finish_when_game_closes(False, None, True, overwrite, 0.0, True, self.game._game_run)
        self.assertTrue((overwrite / "keep.txt").is_file())

    def test_finished_run_of_a_tool(self):
        self.game._guarded_launches.add(os.path.normcase(self.app))
        with quiet_ui(launch), no_game_running(self.game), \
                mock.patch.object(launch, "_running_processes", return_value=[]):
            self.game._finish_profile_launch(str(self.organizer.base / "tool.exe"), 0)
            self.assertIn(os.path.normcase(self.app), self.game._guarded_launches)
            self.organizer.managed = None
            self.game._finish_profile_launch(self.app, 0)
            self.assertIn(os.path.normcase(self.app), self.game._guarded_launches)

    def test_game_files_in_unreadable_mods(self):
        organizer = FakeOrganizer()
        organizer.mod_order.append("Gone")
        organizer.active.add("Gone")
        (organizer.mods / "Gone").mkdir()
        game = game_for(organizer)
        (organizer.mods / "Gone").rmdir()
        self.assertFalse(game._active_mods_have_game_files())


if __name__ == "__main__":
    unittest.main()
