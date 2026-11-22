"""ELDEN RING Reforged's online launcher as an MO2 program (programs.py): which
launchers are found, the program list kept on it while its mod is on, and
a launch of it with the mod off."""

from __future__ import annotations

import unittest
from unittest import mock

from PyQt6 import sip
from PyQt6.QtWidgets import QComboBox

from basic_games.games.eldenring import programs

from fake_organizer import FakeModList, FakeOrganizer, appdata_folder, game_for
from test_cov_game_fakes import FakeTimer, quiet_ui

LAUNCHER = "_ReforgedOnline/internals/launcher/ReforgedLauncher.exe"
ONLINE = "ELDEN RING Reforged (online)"


class Launchers(unittest.TestCase):
    def setUp(self):
        appdata_folder()
        self.organizer = FakeOrganizer()
        self.organizer.add_mod("Reforged", [LAUNCHER])
        self.game = game_for(self.organizer)

    def test_two_releases(self):
        self.organizer.add_mod("Reforged Beta", [LAUNCHER], active=False)
        titles = [e.title() for e in self.game.executables()]
        self.assertIn(ONLINE, titles)
        self.assertIn(f"{ONLINE} (Reforged Beta)", titles)
        executables = self.game._reforged_online_executables()
        self.assertEqual(self.game._inactive_reforged_online_programs(executables),
                         {f"{ONLINE} (Reforged Beta)"})

    def test_search_only_when_the_mods_change(self):
        self.assertEqual(len(self.game._online_launchers()), 1)
        # a folder that appears without a change to the mod list waits
        (self.organizer.mods / "Copy" / LAUNCHER).parent.mkdir(parents=True)
        (self.organizer.mods / "Copy" / LAUNCHER).write_bytes(b"x")
        self.assertEqual(len(self.game._online_launchers()), 1)
        self.organizer.mod_order.append("Copy")
        self.assertEqual(len(self.game._online_launchers()), 2)

    def test_no_mod_list(self):
        with mock.patch.object(self.organizer, "modList", side_effect=RuntimeError("closing")):
            self.assertEqual(self.game._online_launchers(), [])
            self.assertEqual(self.game._inactive_reforged_online_programs(
                self.game._reforged_online_executables()), set())

    def test_nightreign_has_none(self):
        night = game_for(self.organizer, nightreign=True)
        self.assertEqual(night._online_launchers(), [])

    def test_launch_with_the_mod_off(self):
        launcher = str(self.organizer.mods / "Reforged" / LAUNCHER)
        with quiet_ui(programs, answer=1) as box:
            # other programs, and launchers outside a release, ask nothing
            self.assertTrue(self.game._reforged_online_mod_ready(str(self.organizer.base / "tool.exe")))
            self.assertTrue(self.game._reforged_online_mod_ready("C:/ReforgedLauncher.exe"))
            self.assertTrue(self.game._reforged_online_mod_ready("C:/a/b/c/d/ReforgedLauncher.exe"))
            self.assertTrue(self.game._reforged_online_mod_ready(launcher))
            self.assertEqual(box.shown, [])
            self.organizer.active.discard("Reforged")
            self.assertFalse(self.game._reforged_online_mod_ready(launcher))
        with quiet_ui(programs, answer=0):
            self.assertTrue(self.game._reforged_online_mod_ready(launcher))
        self.assertIn("Reforged", self.organizer.active)
        self.assertEqual(self.game._mod_turned_on_for_launch, "Reforged")

    def test_mod_state_unreadable(self):
        launcher = str(self.organizer.mods / "Reforged" / LAUNCHER)
        with mock.patch.object(FakeModList, "state", side_effect=RuntimeError("gone")):
            self.assertTrue(self.game._reforged_online_mod_ready(launcher))


class ProgramList(unittest.TestCase):
    """The program list stays on Reforged's online launcher while its mod is on."""

    def setUp(self):
        appdata_folder()
        self.organizer = FakeOrganizer()
        self.organizer.add_mod("Reforged", [LAUNCHER])
        self.game = game_for(self.organizer)
        self.selector = QComboBox()
        self.addCleanup(sip.delete, self.selector)
        for title in ("ELDEN RING (MO2)", "Tool", ONLINE, f"{ONLINE} (Removed)"):
            self.selector.addItem(title)
        self.selector.setToolTip("Programs")

    def follow(self):
        with quiet_ui(programs):
            self.game._follow_reforged_online_programs(self.selector)
            self.game._follow_reforged_online_programs(self.selector)
            FakeTimer.run_all()

    def test_locked_on_the_online_program(self):
        self.follow()
        self.assertEqual(self.selector.currentText(), ONLINE)
        self.assertTrue(self.game._online_program_lock.locked)
        self.assertNotEqual(self.selector.toolTip(), "Programs")
        # a release no longer installed is not offered
        self.assertTrue(self.selector.view().isRowHidden(3))

    def test_released_when_the_mod_goes_off(self):
        self.follow()
        self.organizer.active.discard("Reforged")
        self.game._sync_reforged_online_programs()
        self.assertFalse(self.game._online_program_lock.locked)
        self.assertEqual(self.selector.toolTip(), "Programs")
        self.assertTrue(self.selector.view().isRowHidden(2))
        self.assertNotEqual(self.selector.currentText(), ONLINE)

    def test_nothing_to_do_for_another_game(self):
        self.follow()
        self.organizer.managed = None
        self.selector.setCurrentIndex(0)
        self.game._sync_reforged_online_programs()
        self.assertEqual(self.selector.currentIndex(), 0)


if __name__ == "__main__":
    unittest.main()
