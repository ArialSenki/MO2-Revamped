"""The save tool's windows (eldenring_mo2_tools/save_isolation.py) driven by
their own buttons: the Tools window, Transfer a save, Restore a backup, a new
named save profile and Export all saves. exec() runs the test's steps and
returns, so nothing is shown; message boxes, folders and the clipboard are
replaced."""

from __future__ import annotations

import json
import os
import tempfile
import unittest
from pathlib import Path
from unittest import mock

from PyQt6.QtWidgets import (
    QAbstractButton,
    QApplication,
    QComboBox,
    QDialog,
    QLineEdit,
    QListWidget,
    QMenu,
    QPushButton,
)

from eldenring_mo2_tools import save_isolation

from test_cov_tools_save_routes import ACCOUNT, ToolCase, write

Tool = save_isolation.EldenRingMo2SaveIsolation


class DrivenDialog(QDialog):
    """exec() runs the next step the test queued instead of an event loop."""

    steps: list = []

    def exec(self) -> int:
        # no event loop at all: timers other tests queued (the game plugin's
        # message boxes) must not run here
        if DrivenDialog.steps:
            DrivenDialog.steps.pop(0)(self)
        return self.result()


def button(dialog, text: str) -> QAbstractButton:
    for candidate in dialog.findChildren(QPushButton):
        if candidate.text() == text:
            return candidate
    raise AssertionError(f"no button {text!r}")


def choose(combo: QComboBox, data) -> None:
    index = combo.findData(data)
    assert index >= 0, data
    combo.setCurrentIndex(index)


class WindowCase(ToolCase):
    def setUp(self):
        super().setUp()
        DrivenDialog.steps = []
        for name, value in (("QDialog", DrivenDialog), ("QDesktopServices", mock.MagicMock()),
                            ("QFileDialog", mock.MagicMock()), ("QInputDialog", mock.MagicMock())):
            patcher = mock.patch.object(save_isolation, name, value)
            setattr(self, name, patcher.start())
            self.addCleanup(patcher.stop)
        # a route change applies on close: the reload is not offered here
        patcher = mock.patch.object(Tool, "_game_or_launcher_running", return_value=True)
        patcher.start()
        self.addCleanup(patcher.stop)
        self.addCleanup(lambda: setattr(DrivenDialog, "steps", []))

    def run_steps(self, *steps) -> None:
        DrivenDialog.steps = list(steps)

    def assert_steps_done(self) -> None:
        self.assertEqual(DrivenDialog.steps, [])


class ToolsWindow(WindowCase):
    def test_pages_and_route_changes(self):
        write(self.steam / ACCOUNT / "ER0000.sl2")
        write(self.profile / "saves" / ACCOUNT / "ER0000.sl2")
        write(self.organizer.base / "ModOrganizer.ini",
              "[customExecutables]\nsize=2\n1\\title=Elden Ring\n1\\binary=C:/G/eldenring.exe\n"
              "2\\title=Reforged\n2\\binary=C:/G/ReforgedLauncher.exe\n")
        self.organizer.add_mod("Reforged", ["DLLs/reforged.dll"])
        self.answers = [False, True]

        def window(dialog):
            nav = dialog.findChild(QListWidget, "fluentNav")
            for row in range(nav.count()):
                nav.setCurrentRow(row)
            mode = dialog.findChild(QComboBox, "saveIsolationModeCombo")
            choose(mode, Tool.InstanceShared)
            # global: refused once, then accepted
            choose(mode, Tool.GlobalShared)
            choose(mode, Tool.GlobalShared)
            dialog.findChild(QAbstractButton, "instanceHelpButton").click()
            with mock.patch.object(QApplication, "clipboard") as clipboard:
                button(dialog, "Copy").click()
            assert clipboard.return_value.setText.called
            for switch in dialog.findChildren(QAbstractButton):
                if switch.property("fluentSwitch") and switch.isEnabled():
                    switch.click()

        self.run_steps(window)
        self.tool.display()
        self.assert_steps_done()
        self.assertEqual(Tool._read_mode_directory(self.profile), Tool.GlobalShared)
        kinds = [kind for _title, _message, kind in self.notices]
        self.assertIn("help", kinds)
        self.assertIn("success", kinds)
        # the launcher's switch was turned: its method is kept by hand
        self.assertTrue(json.loads((self.organizer.base / "eldenring_mo2_launch_methods.json")
                                   .read_text(encoding="utf-8")))

    def test_named_save_profiles_from_the_menu(self):
        write(self.steam / ACCOUNT / "ER0000.sl2", b"steam")
        self.answers = [True]
        self.QInputDialog.getText.return_value = ("Renamed", True)

        def window(dialog):
            menu = dialog.findChild(QMenu)
            actions = {action.text(): action for action in menu.actions()}
            actions["New save profile…"].trigger()
            combo = dialog.findChild(QComboBox, "saveIsolationGameProfileCombo")
            assert combo.currentData(), "the new profile is selected"
            actions["Rename…"].trigger()
            actions["Delete…"].trigger()

        def new_profile(dialog):
            dialog.findChild(QLineEdit, "saveProfileNameEdit").setText("From Steam")
            source = dialog.findChild(QComboBox, "saveProfileSourceCombo")
            source.setCurrentIndex(source.count() - 1)
            dialog.findChild(QAbstractButton, "saveProfileCreateButton").click()

        self.run_steps(window, new_profile)
        self.tool.display()
        self.assert_steps_done()
        # deleted: moved aside with its saves, the profile back on its standard saves
        deleted = list((self.profile / "save_profiles Backups" / "deleted").iterdir())
        self.assertEqual(len(deleted), 1)
        record = next(deleted[0].glob("*.json"))
        self.assertEqual(json.loads(record.read_text(encoding="utf-8"))["name"], "Renamed")
        self.assertEqual(Tool._read_save_profile_id_directory(self.profile, Tool.ProfileIsolated), "")

    def test_profile_actions_wait_while_the_game_runs(self):
        Tool._save_mode_directory(self.profile, Tool.ProfileIsolated)

        def window(dialog):
            menu = dialog.findChild(QMenu)
            with mock.patch.object(save_isolation, "_running_process_names", return_value={"eldenring.exe"}):
                for action in menu.actions():
                    action.trigger()

        self.run_steps(window)
        self.tool.display()
        self.assertTrue(any(title == "Close the game first" for title, _m, _k in self.notices))

    def test_open_folders_export_and_other_windows(self):
        write(self.steam / ACCOUNT / "ER0000.sl2", b"steam")
        export = Path(tempfile.mkdtemp(prefix="er-cov-export-"))
        self.QFileDialog.getExistingDirectory.return_value = str(export)

        def window(dialog):
            for candidate in dialog.findChildren(QAbstractButton):
                if candidate.objectName() == "instanceIconButton" and candidate.toolTip().startswith("Open"):
                    candidate.click()
            button(dialog, "Export…").click()
            button(dialog, "Transfer…").click()
            button(dialog, "Open…").click()

        self.run_steps(window, lambda transfer: transfer.reject(), lambda restore: restore.reject())
        self.tool.display()
        self.assert_steps_done()
        self.assertTrue(self.QDesktopServices.openUrl.called)
        exported = [p for p in export.rglob("ER0000.sl2")]
        self.assertEqual(len(exported), 1)

    def test_a_broken_route_file_is_shown_as_profile_saves(self):
        write(self.profile / Tool.ConfigFilename, b"[Saves]\nmode = \xff\n")
        self.run_steps(lambda dialog: dialog.reject())
        self.tool.display()
        self.assert_steps_done()


class ExportAll(WindowCase):
    def test_every_route_and_the_set_aside_saves(self):
        write(self.steam / ACCOUNT / "ER0000.sl2", b"steam")
        write(self.steam / ACCOUNT / "ER0000.sl2.bak", b"steam-bak")
        write(self.profile / "saves" / ACCOUNT / "ER0000.co2", b"coop")
        shared = self.organizer.base / "Elden Ring Shared Saves"
        write(shared / "reforged_online" / ACCOUNT / "ER0000.err", b"online")
        recovery = save_isolation.EldenRingMo2SaveDiagnose._recovery_folder()
        write(recovery / "s1" / "unrouted-global" / ACCOUNT / "ER0000.sl2", b"kept")
        export = Path(tempfile.mkdtemp(prefix="er-cov-export-"))
        self.QFileDialog.getExistingDirectory.return_value = str(export)
        self.tool._export_all_saves(None)
        copied = sorted(p.read_bytes() for p in export.rglob("*") if p.is_file())
        self.assertEqual(copied, [b"coop", b"kept", b"online", b"steam", b"steam-bak"])
        self.assertEqual(self.notices[-1][0], "Saves exported")

    def test_nothing_to_export_or_no_folder(self):
        self.QFileDialog.getExistingDirectory.return_value = ""
        self.tool._export_all_saves(None)
        self.assertEqual(self.notices, [])
        self.QFileDialog.getExistingDirectory.return_value = tempfile.mkdtemp(prefix="er-cov-export-")
        self.tool._export_all_saves(None)
        self.assertEqual(self.notices[-1][1], "No saves found.")

    def test_blocked_while_the_game_runs(self):
        with mock.patch.object(save_isolation, "_running_process_names", return_value={"eldenring.exe"}):
            self.tool._export_all_saves(None)
        self.assertEqual(self.notices[-1][0], "Close Elden Ring first")
        self.assertFalse(self.QFileDialog.getExistingDirectory.called)

    def test_a_save_that_cannot_be_copied_is_listed(self):
        write(self.steam / ACCOUNT / "ER0000.sl2", b"steam")
        self.QFileDialog.getExistingDirectory.return_value = tempfile.mkdtemp(prefix="er-cov-export-")
        with mock.patch.object(save_isolation.shutil, "copy2", side_effect=PermissionError("locked")):
            self.tool._export_all_saves(None)
        self.assertEqual(self.notices[-1][0], "Some saves could not be copied")

    def test_folder_names_of_routes_with_the_same_label(self):
        # two save profiles of one name in two routes, and characters Windows refuses
        self.named_profile(self.profile / "save_profiles", "a" * 32, 'Run: "A"?')
        write(self.profile / "save_profiles" / ("a" * 32) / ACCOUNT / "ER0000.sl2", b"a")
        other = self.profile.parent / "Other"
        write(other / "modlist.txt", "")
        self.named_profile(other / "save_profiles", "b" * 32, 'Run: "A"?')
        write(other / "save_profiles" / ("b" * 32) / ACCOUNT / "ER0000.sl2", b"b")
        export = Path(tempfile.mkdtemp(prefix="er-cov-export-"))
        self.QFileDialog.getExistingDirectory.return_value = str(export)
        self.tool._export_all_saves(None)
        saves = sorted(p.read_bytes() for p in export.rglob("ER0000.sl2"))
        self.assertEqual(saves, [b"a", b"b"])
        self.assertFalse(any(set('<>:"|?*') & set(p.name) for p in export.rglob("*")))


class TransferWindow(WindowCase):
    def transfer(self, source_route, destination_route, answers=(True,), pick: int = 0, extra=None):
        self.answers = list(answers)

        def step(dialog):
            choose(dialog.findChild(QComboBox, "saveTransferSourceRoute"), source_route)
            choose(dialog.findChild(QComboBox, "saveTransferDestinationRoute"), destination_route)
            saves = dialog.findChild(QListWidget, "saveTransferList")
            if saves.count():
                saves.setCurrentRow(pick)
            if extra is not None:
                extra(dialog)
            dialog.findChild(QAbstractButton, "saveTransferCopyButton").click()
            dialog.findChild(QAbstractButton, "saveTransferCloseButton").click()

        self.run_steps(step)
        self.tool._display_transfer_dialog(self.profile, self.profile.name, None)
        self.assert_steps_done()

    def test_steam_save_into_the_profile(self):
        write(self.steam / ACCOUNT / "ER0000.sl2", b"steam")
        write(self.steam / ACCOUNT / "ER0000.sl2.bak", b"steam-bak")
        self.transfer(Tool.GlobalShared, Tool._profile_route_id(self.profile))
        saves = self.profile / "saves" / ACCOUNT
        self.assertEqual((saves / "ER0000.sl2").read_bytes(), b"steam")
        self.assertEqual((saves / "ER0000.sl2.bak").read_bytes(), b"steam-bak")
        self.assertEqual(self.notices[-1][0], "Save copied")

    def test_a_different_save_is_moved_aside_first(self):
        write(self.steam / ACCOUNT / "ER0000.sl2", b"steam")
        write(self.profile / "saves" / ACCOUNT / "ER0000.sl2", b"profile")
        write(self.profile / "saves" / ACCOUNT / "ER0000.sl2.bak", b"profile-bak")
        self.transfer(Tool.GlobalShared, Tool._profile_route_id(self.profile))
        self.assertEqual((self.profile / "saves" / ACCOUNT / "ER0000.sl2").read_bytes(), b"steam")
        kept = sorted(p.read_bytes() for p in (self.profile / "saves Backups").rglob("*") if p.is_file())
        self.assertEqual(kept, [b"profile", b"profile-bak"])
        # the Steam route keeps no .bak of its own here: none comes along
        self.assertFalse((self.profile / "saves" / ACCOUNT / "ER0000.sl2.bak").exists())

    def test_into_the_steam_route_backups_stay_in_the_instance(self):
        write(self.profile / "saves" / ACCOUNT / "ER0000.sl2", b"profile")
        write(self.steam / ACCOUNT / "ER0000.sl2", b"steam")
        self.transfer(Tool._profile_route_id(self.profile), Tool.GlobalShared)
        self.assertEqual((self.steam / ACCOUNT / "ER0000.sl2").read_bytes(), b"profile")
        kept = list((self.organizer.base / "Elden Ring Global Save Backups").rglob("*.sl2"))
        self.assertEqual([p.read_bytes() for p in kept], [b"steam"])

    def test_identical_cancelled_and_busy(self):
        write(self.steam / ACCOUNT / "ER0000.sl2", b"same")
        write(self.profile / "saves" / ACCOUNT / "ER0000.sl2", b"same")
        self.transfer(Tool.GlobalShared, Tool._profile_route_id(self.profile))
        self.assertEqual(self.notices[-1][0], "Save already matches")
        (self.profile / "saves" / ACCOUNT / "ER0000.sl2").write_bytes(b"other")
        self.transfer(Tool.GlobalShared, Tool._profile_route_id(self.profile), answers=(False,))
        self.assertEqual((self.profile / "saves" / ACCOUNT / "ER0000.sl2").read_bytes(), b"other")
        with mock.patch.object(save_isolation, "_running_process_names", return_value={"eldenring.exe"}):
            self.transfer(Tool.GlobalShared, Tool._profile_route_id(self.profile))
        self.assertEqual(self.notices[-1][0], "Save not copied")

    def test_destination_folder_in_the_way(self):
        write(self.steam / ACCOUNT / "ER0000.sl2", b"steam")
        (self.profile / "saves" / ACCOUNT / "ER0000.sl2").mkdir(parents=True)
        self.transfer(Tool.GlobalShared, Tool._profile_route_id(self.profile))
        self.assertEqual(self.notices[-1][0], "Destination is not a save file")

    def test_a_copy_that_fails_puts_the_old_save_back(self):
        write(self.steam / ACCOUNT / "ER0000.sl2", b"steam")
        write(self.profile / "saves" / ACCOUNT / "ER0000.sl2", b"profile")
        with mock.patch.object(save_isolation.shutil, "copy2", side_effect=PermissionError("antivirus")):
            self.transfer(Tool.GlobalShared, Tool._profile_route_id(self.profile))
        self.assertEqual(self.notices[-1][0], "Save copy failed")
        self.assertEqual((self.profile / "saves" / ACCOUNT / "ER0000.sl2").read_bytes(), b"profile")

    def test_same_route_and_no_saves(self):
        route = Tool._profile_route_id(self.profile)
        self.transfer(route, route)
        self.transfer(Tool.GlobalShared, route)
        self.assertEqual(self.notices, [])

    def test_a_save_gone_meanwhile(self):
        save = write(self.steam / ACCOUNT / "ER0000.sl2", b"steam")
        self.transfer(Tool.GlobalShared, Tool._profile_route_id(self.profile), extra=lambda _d: save.unlink())
        self.assertEqual(self.notices[-1][0], "Save path could not be resolved")


class RestoreWindow(WindowCase):
    def setUp(self):
        super().setUp()
        self.route = self.profile / "saves"
        self.backup = self.profile / "saves Backups" / "auto" / "20261101-120000"
        write(self.backup / ACCOUNT / "ER0000.sl2", b"backup")
        write(self.route / ACCOUNT / "ER0000.sl2", b"current")

    def restore(self, answers=(True,), route=None):
        self.answers = list(answers)

        def step(dialog):
            backups = dialog.findChild(QListWidget, "saveRestoreBackupList")
            assert backups.count() == 1
            button(dialog, "Open backups folder").click()
            button(dialog, "Restore selected backup").click()
            button(dialog, "Close").click()

        self.run_steps(step)
        self.tool._display_restore_dialog(self.profile, None, route or self.route)
        self.assert_steps_done()

    def test_restore_the_backup(self):
        self.restore()
        self.assertEqual((self.route / ACCOUNT / "ER0000.sl2").read_bytes(), b"backup")
        self.assertEqual(self.notices[-1][0], "Backup restored")
        self.assertTrue(self.QDesktopServices.openUrl.called)

    def test_cancel_or_busy_restores_nothing(self):
        self.restore(answers=(False,))
        self.assertEqual((self.route / ACCOUNT / "ER0000.sl2").read_bytes(), b"current")
        with mock.patch.object(save_isolation, "_running_process_names", return_value={"eldenring.exe"}):
            self.restore()
        self.assertEqual(self.notices[-1][0], "Backup not restored")

    def test_an_unsettled_restore_comes_first(self):
        replaced = self.profile / "saves Backups" / "replaced" / "20261101-110000"
        write(replaced / ACCOUNT / "ER0000.sl2", b"current")
        temporary = write(self.route / ACCOUNT / ".ER0000.sl2.restoring", b"other")
        self.tool._write_restore_marker(replaced, self.route, [(temporary, self.route / ACCOUNT / "ER0000.sl2")], [])
        # "Not now" to finishing it: the new restore is refused
        self.restore(answers=(False,))
        self.assertTrue(any("not settled" in message for _t, message, _k in self.notices))
        self.assertEqual((self.route / ACCOUNT / "ER0000.sl2").read_bytes(), b"current")

    def test_a_restore_that_fails(self):
        with mock.patch.object(Tool, "_restore_backup", side_effect=OSError("disk full")):
            self.restore()
        self.assertEqual(self.notices[-1][0], "Backup not restored")

    def test_no_instance_folder(self):
        Tool()._display_restore_dialog(self.profile)


class CreateSaveProfileWindow(WindowCase):
    def test_duplicate_names_and_cancel(self):
        self.tool._create_named_save_profile(self.profile, Tool.ProfileIsolated, "Taken")

        def step(dialog):
            edit = dialog.findChild(QLineEdit, "saveProfileNameEdit")
            create = dialog.findChild(QAbstractButton, "saveProfileCreateButton")
            edit.setText("TAKEN")
            assert not create.isEnabled()
            edit.setText("")
            assert not create.isEnabled()
            dialog.reject()

        self.run_steps(step)
        self.assertIsNone(self.tool._display_create_save_profile_dialog(self.profile, Tool.ProfileIsolated))
        self.assertIsNone(self.tool._display_create_save_profile_dialog(self.profile, Tool.GlobalShared))

    def test_create_in_the_shared_route(self):
        def step(dialog):
            dialog.findChild(QLineEdit, "saveProfileNameEdit").setText("Coop")
            dialog.findChild(QAbstractButton, "saveProfileCreateButton").click()

        self.run_steps(step)
        profile_id = self.tool._display_create_save_profile_dialog(self.profile, Tool.InstanceShared)
        self.assertEqual(Tool._read_named_save_profiles(self.profile, Tool.InstanceShared), [(profile_id, "Coop")])

    def test_a_failure_is_shown_and_the_window_stays(self):
        def step(dialog):
            dialog.findChild(QLineEdit, "saveProfileNameEdit").setText("Coop")
            with mock.patch.object(Tool, "_create_named_save_profile", side_effect=PermissionError("locked")):
                dialog.findChild(QAbstractButton, "saveProfileCreateButton").click()
            dialog.reject()

        self.run_steps(step)
        self.assertIsNone(self.tool._display_create_save_profile_dialog(self.profile, Tool.ProfileIsolated))
        self.assertEqual(self.notices[-1][0], "Could not create save profile")


if __name__ == "__main__":
    unittest.main()
