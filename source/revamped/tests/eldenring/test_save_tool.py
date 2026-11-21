"""The save tool (eldenring_mo2_tools/save_isolation.py) outside MO2: which
mod it takes for The Convergence and the co-op extension it expects, the
same as the game plugin gives Seamless Co-op."""

from __future__ import annotations

import tempfile
import unittest
from pathlib import Path

from eldenring_mo2_tools import save_isolation

Tool = save_isolation.EldenRingMo2SaveIsolation


def mod_folder(files: dict[str, str]) -> Path:
    root = Path(tempfile.mkdtemp(prefix="er-save-tool-"))
    for relative, text in files.items():
        path = root / relative
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(text, encoding="utf-8")
    return root


NEXUS_META = "[General]\ngameName=ELDEN RING\nmodid=3419\nversion=3.0.4\n"


class ConvergenceDetection(unittest.TestCase):
    def test_its_me3_profile_or_its_nexus_page(self):
        self.assertTrue(Tool._is_convergence(mod_folder({"convergence.me3": ""})))
        self.assertTrue(Tool._is_convergence(mod_folder({"me3/convergence.me3": ""})))
        self.assertTrue(Tool._is_convergence(mod_folder({"meta.ini": NEXUS_META})))

    def test_not_a_mod_that_only_has_the_name(self):
        # a patch for Convergence, another game's mod 3419, or no meta.ini
        patch = mod_folder({"meta.ini": "[General]\ngameName=eldenring\nmodid=1234\n"})
        other_game = mod_folder({"meta.ini": "[General]\ngameName=Skyrim\nmodid=3419\n"})
        self.assertFalse(Tool._is_convergence(patch))
        self.assertFalse(Tool._is_convergence(other_game))
        self.assertFalse(Tool._is_convergence(mod_folder({"regulation.bin": ""})))


class ConvergenceCoopExtension(unittest.TestCase):
    def test_from_the_settings_it_ships(self):
        folder = mod_folder({"DLLs/ersc_settings.ini": "[SAVE]\nsave_file_extension = cnvx\n"})
        self.assertEqual(Tool._convergence_coop_extension(folder), "cnvx")

    def test_cnvco2_without_settings_or_with_a_value_the_plugin_refuses(self):
        self.assertEqual(Tool._convergence_coop_extension(mod_folder({})), "cnvco2")
        co2 = mod_folder({"DLLs/ersc_settings.ini": "[SAVE]\nsave_file_extension = co2\n"})
        self.assertEqual(Tool._convergence_coop_extension(co2), "cnvco2")
        odd = mod_folder({"DLLs/ersc_settings.ini": "[SAVE]\nsave_file_extension = a/b\n"})
        self.assertEqual(Tool._convergence_coop_extension(odd), "cnvco2")
        # another save's extension, as the plugin refuses it
        for other in ("sl2", "err", "cnv", "**"):
            folder = mod_folder({"DLLs/ersc_settings.ini": f"[SAVE]\nsave_file_extension = {other}\n"})
            self.assertEqual(Tool._convergence_coop_extension(folder), "cnvco2")

    def test_a_glob_pattern_isnt_an_extension(self):
        settings = mod_folder({"ersc_settings.ini": "[SAVE]\nsave_file_extension = **\n"}) / "ersc_settings.ini"
        self.assertEqual(Tool._settings_extension(settings, "save_file_extension"), "")


if __name__ == "__main__":
    unittest.main()
