"""The startup options tool (eldenring_mo2_tools/startup_tool.py), the
stand-ins other games' instances get (stub.py) and the plugin files MO2 loads
(eldenring_mo2_*.py). The window's exec() returns at once and message boxes
and os.startfile are replaced: nothing is shown or opened."""

from __future__ import annotations

import configparser
import importlib.util
import os
import sys
import tempfile
import unittest
from pathlib import Path
from unittest import mock

import mobase
from PyQt6.QtCore import QCoreApplication
from PyQt6.QtWidgets import QAbstractButton, QApplication, QCheckBox, QComboBox, QDialog

from eldenring_mo2_tools import startup_tool, stub

from fake_organizer import FakeOrganizer, appdata_folder, game_for
from harness import PLUGINS

Tool = startup_tool.EldenRingMo2StartupTool


class DrivenDialog(QDialog):
    steps: list = []

    def exec(self) -> int:
        # no event loop at all: timers other tests queued (the game plugin's
        # message boxes) must not run here
        if DrivenDialog.steps:
            DrivenDialog.steps.pop(0)(self)
        return self.result()


class StartupCase(unittest.TestCase):
    def setUp(self):
        self._appdata = os.environ.get("APPDATA")
        appdata_folder()
        self.organizer = FakeOrganizer()
        game_for(self.organizer)
        self.tool = Tool()
        self.assertTrue(self.tool.init(self.organizer))
        self.config = self.organizer.profile_dir / Tool.ConfigFilename
        for name, value in (("QDialog", DrivenDialog), ("QMessageBox", mock.MagicMock())):
            patcher = mock.patch.object(startup_tool, name, value)
            setattr(self, name, patcher.start())
            self.addCleanup(patcher.stop)
        patcher = mock.patch.object(startup_tool.os, "startfile", create=True)
        self.startfile = patcher.start()
        self.addCleanup(patcher.stop)
        DrivenDialog.steps = []
        self.addCleanup(lambda: setattr(DrivenDialog, "steps", []))

    def tearDown(self):
        startup_tool._game_name = "Elden Ring"
        if self._appdata is None:
            os.environ.pop("APPDATA", None)
        else:
            os.environ["APPDATA"] = self._appdata

    def read(self) -> configparser.ConfigParser:
        config = configparser.ConfigParser(interpolation=None)
        config.read(self.config, encoding="utf-8")
        return config

    def accept(self, change=None):
        def step(dialog):
            if change is not None:
                change(dialog)
            dialog.findChild(QAbstractButton, "dialogButtons")  # the box exists
            dialog.accept()

        DrivenDialog.steps = [step]
        self.tool.display()
        self.assertEqual(DrivenDialog.steps, [])


class Identity(StartupCase):
    def test_plugin_answers(self):
        self.assertEqual(self.tool.name(), "Elden Ring MO2 Startup Options")
        self.assertEqual(self.tool.author(), "MO2 Revamped")
        self.assertTrue(self.tool.enabledByDefault())
        self.assertEqual(self.tool.settings(), [])
        self.assertTrue(self.tool.requirements())
        self.assertTrue(self.tool.version())
        self.assertTrue(self.tool.icon().isNull())
        self.assertTrue(self.tool.localizedName())
        self.assertTrue(self.tool.tooltip())
        game_for(self.organizer, nightreign=True)
        self.assertIn("Nightreign", self.tool.description())
        self.assertEqual(self.tool._bridge_log_name(), "NightreignMO2Bridge")
        self.organizer.managed = None
        self.assertEqual(self.tool._bridge_log_name(), "EldenRingMO2Bridge")
        self.assertIsInstance(startup_tool.createPlugin(), Tool)


class Options(StartupCase):
    def test_defaults_written_on_save(self):
        self.accept()
        config = self.read()
        self.assertEqual(config["Startup"]["skip_intro_logos"], "1")
        self.assertEqual(config["Startup"]["boot_boost"], "1")
        self.assertEqual(config["Performance"]["cpu0_delay_seconds"], "30")
        self.assertEqual(config["Diagnostics"]["previous_bridge_sessions"], "1")
        self.assertTrue(self.QMessageBox.information.called)

    def test_old_keys_are_read_and_dropped_and_other_sections_kept(self):
        self.config.write_text(
            "﻿[Startup]\nstart_in_background = 1\nprevent_focus_steal = 0\n"
            "[Performance]\nexclude_cpu0_after_start = 1\ncpu0_delay_seconds = 60\nprocess_priority = 1\n"
            "[Cleanup]\nclear_overwrite_after_game = 1\nclear_overwrite_logs_before_game = 1\n"
            "[Diagnostics]\nprevious_bridge_sessions = 5\n"
            "[Installation]\nskip_txt_files = 1\nskip_md_files = 0\n"
            "[RevampedNativeProfile]\nnative_01 = DLLs/100%.dll\nmod_01 = 100% Mod\n",
            encoding="utf-8",
        )
        seen = {}

        def look(dialog):
            boxes = dialog.findChildren(QCheckBox)
            seen["checked"] = sum(box.isChecked() for box in boxes)
            seen["logs"] = dialog.findChild(QCheckBox, "clearOverwriteLogsBeforeLaunch").isChecked()

        self.accept(look)
        # prevent_focus_steal (0) wins over start_in_background, and the two
        # overwrite cleanups never run together
        self.assertFalse(seen["logs"])
        config = self.read()
        self.assertEqual(config["Startup"]["start_minimized"], "0")
        self.assertNotIn("prevent_focus_steal", config["Startup"])
        self.assertNotIn("start_in_background", config["Startup"])
        self.assertNotIn("Installation", config)
        self.assertEqual(config["Performance"]["cpu0_delay_seconds"], "60")
        self.assertEqual(config["Performance"]["process_priority"], "1")
        self.assertEqual(config["Diagnostics"]["previous_bridge_sessions"], "5")
        self.assertEqual(config["RevampedNativeProfile"]["mod_01"], "100% Mod")

    def test_values_out_of_range_fall_back(self):
        self.config.write_text(
            "[Performance]\ncpu0_delay_seconds = 7\nprocess_priority = 9\n"
            "[Diagnostics]\nprevious_bridge_sessions = 2\n[Installation]\nother = x\n",
            encoding="utf-8",
        )
        self.accept()
        config = self.read()
        self.assertEqual(config["Performance"]["cpu0_delay_seconds"], "30")
        self.assertEqual(config["Performance"]["process_priority"], "0")
        self.assertEqual(config["Diagnostics"]["previous_bridge_sessions"], "1")
        self.assertEqual(config["Installation"]["other"], "x")

    def test_a_bad_value_shows_defaults(self):
        self.config.write_text("[Startup]\nboot_boost = maybe\n", encoding="utf-8")
        self.accept()
        self.assertTrue(self.QMessageBox.warning.called)
        self.assertEqual(self.read()["Startup"]["boot_boost"], "1")

    def test_an_unreadable_file_is_never_replaced(self):
        data = b"[Startup]\nboot_boost = 0\nname = Espa\xf1a\n"
        self.config.write_bytes(data)
        self.accept()
        self.assertEqual(self.config.read_bytes(), data)
        self.assertEqual(self.QMessageBox.warning.call_count, 2)

    def test_a_duplicate_key_is_unreadable(self):
        data = "[Startup]\nboot_boost = 0\nboot_boost = 1\n"
        self.config.write_text(data, encoding="utf-8")
        self.accept()
        self.assertEqual(self.config.read_text(encoding="utf-8"), data)

    def test_switches_and_restore_defaults(self):
        def change(dialog):
            for box in dialog.findChildren(QCheckBox):
                box.setChecked(not box.isChecked())
            for combo in dialog.findChildren(QComboBox):
                combo.setCurrentIndex(combo.count() - 1)
            dialog.findChild(QAbstractButton, "restoreStartupDefaults").click()
            dialog.findChild(QCheckBox, "clearOverwriteAfterGame").setChecked(True)

        self.accept(change)
        config = self.read()
        self.assertEqual(config["Cleanup"]["clear_overwrite_after_game"], "1")
        self.assertEqual(config["Cleanup"]["clear_overwrite_logs_before_game"], "0")

    def test_cancel_writes_nothing(self):
        DrivenDialog.steps = [lambda dialog: dialog.reject()]
        self.tool.display()
        self.assertFalse(self.config.exists())

    def test_a_save_that_fails(self):
        with mock.patch.object(startup_tool.os, "replace", side_effect=PermissionError("locked")):
            self.accept()
        self.assertTrue(self.QMessageBox.critical.called)
        self.assertEqual([p.name for p in self.organizer.profile_dir.glob(".*.saving")], [])

    def test_other_games_and_no_profile(self):
        self.organizer.managed = None
        self.tool.display()
        self.organizer._profile = None
        game_for(self.organizer, nightreign=True)
        self.tool.display()
        self.assertTrue(self.QMessageBox.warning.called)


class LogButtons(StartupCase):
    def test_logs_and_task_manager(self):
        folder = Path(tempfile.mkdtemp(prefix="er-cov-logs-"))
        with mock.patch.object(startup_tool.tempfile, "gettempdir", return_value=str(folder)):
            self.tool._open_latest_bridge_log()
            self.tool._open_previous_bridge_log()
            self.assertEqual(self.QMessageBox.information.call_count, 2)
            self.assertFalse(self.startfile.called)
            (folder / "EldenRingMO2Bridge.log").write_text("x", encoding="utf-8")
            (folder / "EldenRingMO2Bridge.previous.log").write_text("x", encoding="utf-8")
            self.tool._open_latest_bridge_log()
            self.tool._open_previous_bridge_log()
            self.tool._open_bridge_log_folder()
            self.assertEqual(self.startfile.call_count, 3)
            self.startfile.side_effect = OSError("no program")
            self.tool._open_latest_bridge_log()
            self.tool._open_previous_bridge_log()
            self.tool._open_bridge_log_folder()
            self.tool._open_task_manager()
            self.assertEqual(self.QMessageBox.warning.call_count, 4)
        self.startfile.side_effect = None
        with mock.patch.dict(os.environ, {"SystemRoot": str(folder)}):
            self.tool._open_task_manager()
        self.assertEqual(self.QMessageBox.information.call_count, 3)

    def test_buttons_in_the_window(self):
        def step(dialog):
            for name in ("openCurrentBridgeLog", "openPreviousBridgeLog", "openBridgeLogFolder", "openTaskManager"):
                dialog.findChild(QAbstractButton, name).click()
            dialog.reject()

        DrivenDialog.steps = [step]
        self.tool.display()
        self.assertTrue(self.startfile.called)


class Stubs(unittest.TestCase):
    def setUp(self):
        app = QCoreApplication.instance()
        self._game = app.property("revampedInstanceGame")
        self.addCleanup(lambda: app.setProperty("revampedInstanceGame", self._game))

    def set_game(self, name):
        QCoreApplication.instance().setProperty("revampedInstanceGame", name)

    def test_which_instances_need_the_tools(self):
        self.set_game(None)
        self.assertTrue(stub.instance_needs_tools())
        self.set_game("")
        self.assertTrue(stub.instance_needs_tools())
        self.set_game("elden ring nightreign")
        self.assertTrue(stub.instance_needs_tools())
        self.set_game("Skyrim Special Edition")
        self.assertFalse(stub.instance_needs_tools())

    def test_file_version(self):
        self.assertEqual(stub.file_version("save_isolation.py", (0, 0, 0, 1)).displayString(),
                         mobase.VersionInfo(0, 5, 0, 85).displayString())
        self.assertEqual(stub.file_version("missing.py", (0, 5, 0, 7)).displayString(),
                         mobase.VersionInfo(0, 5, 0, 7).displayString())

    def test_tool_stub_answers(self):
        tool = stub.ToolStub("Name", "Context", "Display", "Description", "Tip", mobase.VersionInfo(1, 0, 0, 0))
        self.assertTrue(tool.init(None))
        self.assertEqual(tool.name(), "Name")
        self.assertEqual(tool.localizedName(), "Display")
        self.assertEqual(tool.author(), "MO2 Revamped")
        self.assertEqual(tool.description(), "Description")
        self.assertEqual(tool.tooltip(), "Tip")
        self.assertEqual(tool.settings(), [])
        self.assertTrue(tool.enabledByDefault())
        self.assertTrue(tool.requirements())
        self.assertTrue(tool.icon().isNull())
        self.assertEqual(tool.version().displayString(), mobase.VersionInfo(1, 0, 0, 0).displayString())
        tool.setParentWidget(None)
        tool.display()
        check = stub.DiagnoseStub("D", "Context", "Checks", "Desc", mobase.VersionInfo(1, 0, 0, 0))
        self.assertTrue(check.init(None))
        self.assertEqual((check.name(), check.localizedName(), check.description(), check.author()),
                         ("D", "Checks", "Desc", "MO2 Revamped"))
        self.assertEqual(check.activeProblems(), [])
        self.assertEqual(check.shortDescription(0), "")
        self.assertEqual(check.fullDescription(0), "")
        self.assertFalse(check.hasGuidedFix(0))
        check.startGuidedFix(0)
        self.assertEqual(check.settings(), [])
        self.assertTrue(check.requirements())
        self.assertTrue(check.version())

    def test_tool_module_imports_or_reloads(self):
        import eldenring_mo2_tools as package

        name = "eldenring_mo2_tools.stub"
        original = sys.modules[name]
        try:
            sys.modules.pop(name)
            fresh = stub.tool_module("stub")
            self.assertIsNot(fresh, original)
            # loaded again: the same module, evaluated anew
            self.assertIs(stub.tool_module("stub"), fresh)
        finally:
            # the package keeps the module the other tests patch
            sys.modules[name] = original
            package.stub = original

    def load_plugin_file(self, file_name: str):
        path = PLUGINS / file_name
        spec = importlib.util.spec_from_file_location(f"cov_{path.stem}", path)
        module = importlib.util.module_from_spec(spec)
        saved_path = list(sys.path)
        try:
            spec.loader.exec_module(module)
        finally:
            sys.path[:] = saved_path
        return module

    def test_plugin_files_in_another_game(self):
        self.set_game("Skyrim Special Edition")
        save = self.load_plugin_file("eldenring_mo2_save_isolation.py").createPlugins()
        self.assertEqual([type(p).__name__ for p in save], ["ToolStub", "DiagnoseStub"])
        self.assertEqual(save[0].version().displayString(), mobase.VersionInfo(0, 5, 0, 85).displayString())
        for file_name in ("eldenring_mo2_native_profile_tool.py", "eldenring_mo2_startup_tool.py"):
            plugin = self.load_plugin_file(file_name).createPlugin()
            self.assertIsInstance(plugin, stub.ToolStub)

    def test_plugin_files_in_elden_ring(self):
        self.set_game("ELDEN RING")
        real = mock.Mock()
        with mock.patch.object(stub, "tool_module", return_value=real) as tool_module:
            module = self.load_plugin_file("eldenring_mo2_save_isolation.py")
            self.assertIs(module.createPlugins(), real.createPlugins.return_value)
        tool_module.assert_called_with("save_isolation")


if __name__ == "__main__":
    unittest.main()
