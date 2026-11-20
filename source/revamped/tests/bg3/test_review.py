"""Second review round: moving needed mods up, the load order window, the
cache saved from two threads, installs read in the background."""

from __future__ import annotations

import json
import threading
import unittest

from basic_games.games.baldursgate3 import loadorder, paks
from basic_games.games.baldursgate3.plugins.order_view import LoadOrderDialog, rows
from test_paks import entry, module
from test_plugin import UA, UB, PluginBase

U = {n: f"30000000-0000-0000-0000-00000000000{i}" for i, n in enumerate("ABCDE", 1)}


class FakeOrder:
    """MO2's mod list priorities: setPriority(name, p) puts the mod at p and
    shifts the ones between."""

    def __init__(self, names):
        self.names = list(names)

    def priority(self, name):
        return self.names.index(name)

    def set_priority(self, name, p):
        self.names.remove(name)
        self.names.insert(p, name)


class FixOrder(unittest.TestCase):
    def test_needed_mods_move_just_above(self):
        order = FakeOrder(["x", "needs", "y", "lib", "z", "lib2"])
        moved = loadorder.fix_order([("needs", "lib"), ("lib", "lib2")], order.priority, order.set_priority)
        self.assertLess(order.priority("lib2"), order.priority("lib"))
        self.assertLess(order.priority("lib"), order.priority("needs"))
        self.assertEqual(order.names[0], "x")
        self.assertEqual(sorted(moved), ["lib", "lib2"])

    def test_cycle_is_left_alone(self):
        order = FakeOrder(["a", "b"])
        loadorder.fix_order([("a", "b"), ("b", "a")], order.priority, order.set_priority)
        self.assertEqual(order.names, ["a", "b"])

    def test_plan_pairs_and_rows(self):
        plan = loadorder.make_plan(
            [
                entry("needs", module("A", U["A"], deps=[("B", U["B"], 0), ("C", U["C"], 0)])),
                entry("lib", module("B", U["B"])),
            ],
            None,
            has_script_extender=False,
        )
        self.assertEqual(plan.late_mods, [("needs", "lib")])
        table = rows(plan)
        self.assertEqual([r[0] for r in table], ["GustavX", "A", "B"])
        self.assertIn("loads before B", table[1][3])
        self.assertIn("needs C, which isn't active", table[1][3])
        self.assertEqual(table[2][3], "")


class CacheSavedFromTwoThreads(unittest.TestCase):
    def test_saves_dont_clash(self):
        import shutil
        import tempfile
        from pathlib import Path

        from paktools import mod_pak

        tmp = Path(tempfile.mkdtemp(prefix="bg3-save-"))
        try:
            cache = paks.PakCache(tmp / "cache.json")
            pak_files = [mod_pak(tmp / f"p{i}.pak", f"M{i}", f"40000000-0000-0000-0000-{i:012d}") for i in range(20)]
            errors = []

            def work(chunk):
                try:
                    for pak in chunk:
                        cache.info(pak, None)
                        cache.save()
                except Exception as e:  # noqa: BLE001
                    errors.append(e)

            threads = [threading.Thread(target=work, args=(pak_files[i::2],)) for i in range(2)]
            for t in threads:
                t.start()
            for t in threads:
                t.join()
            self.assertEqual(errors, [])
            saved = json.loads((tmp / "cache.json").read_text(encoding="utf-8"))
            self.assertEqual(len(saved["paks"]), 20)
        finally:
            shutil.rmtree(tmp, ignore_errors=True)


class Plugin(PluginBase):
    def test_install_reads_in_the_background(self):
        self.install()
        root = self.pak_mod("New", UA)
        launch = self.game.launch()
        self.game._on_mod_installed(self.organizer.modList().getMod("New"))
        self.assertIsNotNone(launch._background)
        launch._background.join(10)
        self.assertIsNotNone(launch.cache.get(root / "New.pak"))

    def test_window_and_its_button(self):
        self.install()
        self.pak_mod("Needs", UA, deps=[("Lib", UB)])
        self.pak_mod("Lib", UB)
        launch = self.game.launch()
        launch.quick_plan()
        launch.read_in_background()
        launch._background.join(10)
        positions = {"Needs": 0, "Lib": 1}
        moves = []

        class ModList:
            def priority(self, name):
                return positions[name]

            def setPriority(self, name, p):
                moves.append((name, p))

        dialog = LoadOrderDialog(self.game, self.organizer)
        self.assertEqual(dialog._table.rowCount(), 3)
        self.assertTrue(dialog._fix.isEnabled())
        self.assertEqual(dialog._plan.late_mods, [("Needs", "Lib")])
        # the button, with its question answered yes
        from PyQt6.QtWidgets import QMessageBox

        real_question = QMessageBox.question
        QMessageBox.question = staticmethod(lambda *a, **k: QMessageBox.StandardButton.Yes)
        real_mod_list = self.organizer.modList
        self.organizer.modList = lambda: ModList()
        try:
            dialog._refresh = lambda: None
            dialog._move_up()
        finally:
            QMessageBox.question = real_question
            self.organizer.modList = real_mod_list
        self.assertEqual(moves, [("Lib", 0)])
        dialog.close()
