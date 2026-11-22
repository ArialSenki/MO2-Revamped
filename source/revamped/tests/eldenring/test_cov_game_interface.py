"""What the plugin says in MO2's window (interface.py): the game's name in its
texts, the recovery notices and retries, the saves put back as MO2 closes,
and the run button joined with the shortcut menu. Message boxes and timers
are stand-ins; no window is shown."""

from __future__ import annotations

import unittest
from pathlib import Path
from unittest import mock

from PyQt6 import sip
from PyQt6.QtWidgets import QComboBox, QMenu, QPushButton, QToolButton, QVBoxLayout, QWidget

from basic_games.games.eldenring import interface, programs, recovery

from fake_organizer import FakeOrganizer, appdata_folder, game_for
from test_cov_game_fakes import FakeTimer, no_game_running, quiet_ui

ACCOUNT = "76561198000000001"


class GameText(unittest.TestCase):
    def test_names(self):
        appdata_folder()
        elden = game_for(FakeOrganizer())
        night = game_for(FakeOrganizer(), nightreign=True)
        self.assertEqual(elden._game_text("Elden Ring save"), "Elden Ring save")
        self.assertEqual(night._game_text("Close Elden Ring"), "Close Nightreign")
        self.assertEqual(night._game_text("Elden-Ring-Spielstand"), "Nightreign-Spielstand")
        self.assertEqual(night._game_text("la partie d'Elden Ring"), "la partie de Nightreign")
        self.assertEqual(night._game_text("qu’Elden Ring"), "que Nightreign")


class Notices(unittest.TestCase):
    def setUp(self):
        appdata_folder()
        self.organizer = FakeOrganizer()
        self.game = game_for(self.organizer)

    def test_displaced_kept_and_returned_saves(self):
        self.game._displaced_newer_saves = [Path("C:/a")]
        self.game._kept_aside_saves = [Path("C:/b")]
        self.game._returned_saves = [(Path("C:/route"), 3)]
        with quiet_ui(interface) as box:
            self.game._report_displaced_saves()
            self.game._report_displaced_saves()
        self.assertEqual([kind for kind, _t, _x in box.shown], ["warning", "warning", "information"])
        self.assertIn("3", box.shown[2][2])
        self.assertEqual(self.game._displaced_newer_saves + self.game._kept_aside_saves, [])

    def test_recovery_blocked(self):
        with quiet_ui(interface) as box:
            self.game._recovery_waiting = True
            with no_game_running(self.game, "start_protected_game.exe"):
                self.game._show_launch_recovery_blocked()
            with no_game_running(self.game, None):
                self.game._show_launch_recovery_blocked()
            self.game._recovery_waiting = False
            self.game._show_launch_recovery_blocked()
            self.game._show_recovery_failed()
        self.assertEqual([kind for kind, _t, _x in box.shown], ["warning", "warning", "critical", "critical"])
        self.assertIn("start_protected_game.exe", box.shown[0][2])

    def test_retry_intervals(self):
        with quiet_ui(interface):
            self.game._recovery_waiting_for_game = True
            self.game._retry_recovery_later()
            self.game._retry_recovery_later()  # one chain at a time
            self.assertEqual([i for i, _c in FakeTimer.calls], [self.game.SwapWatchInterval])
            self.game._recovery_retry_pending = False
            self.game._recovery_waiting_for_game = False
            self.game._retry_recovery_later()
            self.assertEqual(FakeTimer.calls[-1][0], self.game.GameWatchInterval)

    def test_retry(self):
        cls = type(self.game)
        with quiet_ui(interface) as box:
            # still waiting for the game: no scan yet
            self.game._recovery_waiting = self.game._recovery_waiting_for_game = True
            with mock.patch.object(cls, "_elden_ring_process_state", return_value=True), \
                    mock.patch.object(cls, "_recover_stale_global_save_quarantines") as recover:
                self.game._retry_recovery()
            recover.assert_not_called()
            self.assertEqual(len(FakeTimer.calls), 1)

            def recovered(game):
                game._recovered_sessions = 1
                return True

            with no_game_running(self.game), \
                    mock.patch.object(cls, "_recover_stale_global_save_quarantines", recovered):
                self.game._retry_recovery()
            self.assertFalse(self.game._recovery_retry_pending)

            def still_waiting(game):
                game._recovery_waiting = True
                return False

            with no_game_running(self.game), \
                    mock.patch.object(cls, "_recover_stale_global_save_quarantines", still_waiting):
                self.game._retry_recovery()
            self.assertTrue(self.game._recovery_retry_pending)

            def failed(game):
                game._recovery_waiting = False
                return False

            self.game._recovery_retry_pending = False
            with no_game_running(self.game), \
                    mock.patch.object(cls, "_recover_stale_global_save_quarantines", failed):
                self.game._retry_recovery()
        self.assertEqual(box.shown[-1][0], "critical")


class ClosingMo2(unittest.TestCase):
    def setUp(self):
        self.appdata = appdata_folder()
        self.steam = self.appdata / "EldenRing"
        (self.steam / ACCOUNT).mkdir(parents=True)
        (self.steam / ACCOUNT / "ER0000.sl2").write_bytes(b"steam")
        self.game = game_for(FakeOrganizer())
        with quiet_ui(recovery):
            self.session = self.game._hide_existing_global_save_directories()

    def test_nothing_hidden(self):
        self.game._active_save_quarantine = None
        with quiet_ui(interface) as box, no_game_running(self.game):
            self.game._restore_saves_on_close()
        self.assertEqual(box.shown, [])

    def test_game_or_launcher_still_open(self):
        cls = type(self.game)
        with quiet_ui(interface) as box:
            with mock.patch.object(cls, "_elden_ring_process_state", return_value=True), \
                    mock.patch.object(cls, "_open_launcher", return_value=None):
                self.game._restore_saves_on_close()
            with no_game_running(self.game, "start_protected_game.exe"):
                self.game._restore_saves_on_close()
            # unknown state and no launcher: nothing to name
            with mock.patch.object(cls, "_elden_ring_process_state", return_value=None), \
                    mock.patch.object(cls, "_open_launcher", return_value=None):
                self.game._restore_saves_on_close()
        self.assertEqual(len(box.shown), 2)
        self.assertIn("eldenring.exe", box.shown[0][2])
        self.assertIn("start_protected_game.exe", box.shown[1][2])
        self.assertFalse((self.steam / ACCOUNT).exists())

    def test_closed(self):
        with quiet_ui(interface), no_game_running(self.game):
            self.game._restore_saves_on_close()
        self.assertIsNone(self.game._active_save_quarantine)
        self.assertTrue((self.steam / ACCOUNT / "ER0000.sl2").is_file())

    def test_restore_fails(self):
        (self.session / "session.json").write_text("[]", encoding="utf-8")
        with quiet_ui(interface) as box, no_game_running(self.game):
            self.game._restore_saves_on_close()
        self.assertEqual(self.game._active_save_quarantine, self.session)
        self.assertEqual(box.shown, [])


class StartUp(unittest.TestCase):
    def setUp(self):
        appdata_folder()
        self.organizer = FakeOrganizer()
        self.game = game_for(self.organizer)

    def test_inactive_instance_keeps_waiting(self):
        self.organizer.managed = None
        self.game._recovery_waiting = True
        with quiet_ui(interface):
            self.game._initialize_elden_ring_ui(None)
            self.assertEqual(len(FakeTimer.calls), 1)
        self.assertIsNone(self.game._main_window)

    def test_active(self):
        with quiet_ui(interface) as box:
            self.game._initialize_elden_ring_ui(None)
            self.assertEqual(box.shown, [])
            self.assertIs(self.organizer.settings[self.game.ArchiveLayoutOverrideSetting], False)
            self.assertTrue(self.game._refined_ui_timer.active)
            # no MO2 window to align: it gives up after 40 tries
            self.game._refined_ui_attempts = 39
            self.game._align_refined_main_window()
            self.assertFalse(self.game._refined_ui_timer.active)

    def test_recovery_waits_or_fails(self):
        cls = type(self.game)

        def waiting(game):
            game._recovery_waiting = True
            return False

        def failed(game):
            game._recovery_waiting = False
            return False

        with quiet_ui(interface) as box:
            with mock.patch.object(cls, "_recover_stale_global_save_quarantines", waiting):
                self.game._initialize_elden_ring_ui(None)
            with mock.patch.object(cls, "_recover_stale_global_save_quarantines", failed):
                self.game._initialize_elden_ring_ui(None)
        self.assertEqual([kind for kind, _t, _x in box.shown], ["information", "critical"])

    def test_message_parent(self):
        class Window:
            minimized = False

            def isMinimized(self):
                return self.minimized

        class Deleted:
            def isMinimized(self):
                raise RuntimeError("deleted")

        window = Window()
        self.game._main_window = window
        self.assertIs(self.game._message_parent(), window)
        window.minimized = True
        self.assertIsNone(self.game._message_parent())
        self.game._main_window = Deleted()
        self.game._message_parent()
        self.assertIsNone(self.game._main_window)
        self.game._main_window = window
        self.game._forget_main_window()
        self.assertIsNone(self.game._main_window)


class RunButton(unittest.TestCase):
    """MO2's Run and shortcut buttons joined into one split button."""

    def setUp(self):
        appdata_folder()
        self.organizer = FakeOrganizer()
        self.game = game_for(self.organizer)
        self.window = QWidget()
        self.addCleanup(sip.delete, self.window)
        layout = QVBoxLayout(self.window)
        layout.setObjectName("runButtonsLayout")
        self.selector = QComboBox(self.window)
        self.selector.setObjectName("executablesListBox")
        self.run = QPushButton("Run", self.window)
        self.run.setObjectName("startButton")
        self.shortcut = QPushButton("Shortcut", self.window)
        self.shortcut.setObjectName("linkButton")
        self.shortcut.setMenu(QMenu(self.shortcut))
        layout.addWidget(self.selector)
        layout.addWidget(self.run)
        layout.addWidget(self.shortcut)

    def test_split_button(self):
        self.assertTrue(self.game._integrate_shortcut_menu(self.window, self.run))
        split = self.window.findChild(QToolButton, "refinedRunButton")
        self.assertIsNotNone(split)
        self.assertIs(split.menu(), self.shortcut.menu())
        clicked = []
        self.run.clicked.connect(lambda: clicked.append(True))
        split.click()
        self.assertEqual(clicked, [True])
        # the enabled state follows MO2's own button
        self.run.setEnabled(False)
        self.assertFalse(split.isEnabled())
        # done once
        self.assertTrue(self.game._integrate_shortcut_menu(self.window, self.run))

    def test_without_shortcut_menu(self):
        self.shortcut.setMenu(None)
        self.assertFalse(self.game._integrate_shortcut_menu(self.window, self.run))
        self.shortcut.setObjectName("other")
        self.assertFalse(self.game._integrate_shortcut_menu(self.window, self.run))

    def test_tool_button_with_menu(self):
        button = QToolButton(self.window)
        button.setMenu(QMenu(button))
        self.assertTrue(self.game._integrate_shortcut_menu(self.window, button))
        self.assertEqual(button.popupMode(), QToolButton.ToolButtonPopupMode.MenuButtonPopup)

    def test_alignment_finds_the_window(self):
        with quiet_ui(interface, programs), \
                mock.patch.object(type(self.game), "_set_native_title_bar") as title_bar:
            self.game._start_refined_ui_alignment()
            self.game._align_refined_main_window()
        self.assertFalse(self.game._refined_ui_timer.active)
        title_bar.assert_called_once()
        self.assertIs(self.game._online_programs_selector, self.selector)
        self.assertFalse(type(self.game)._uses_dark_theme(None, self.window))


if __name__ == "__main__":
    unittest.main()
