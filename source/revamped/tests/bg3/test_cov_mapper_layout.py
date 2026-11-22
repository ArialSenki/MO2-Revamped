"""The documents-folder mapper with odd mod layouts (Root/, Mods/<folder>,
wrapper folders, YAML configs) and the installer's checker and Content column
for the layouts the other tests don't cover."""

from __future__ import annotations

import json
from pathlib import Path

import mobase

from basic_games.games.baldursgate3.layout import BG3DataContent, BG3ModDataChecker, Content
from harness import make_tree
from test_plugin import NAME, PluginBase


class MapperTest(PluginBase):
    def mapped(self) -> dict[Path, Path]:
        return {Path(m.source): Path(m.destination) for m in self.game.mapper().mappings()}

    def test_no_active_mods_maps_nothing(self):
        self.install()
        self.organizer.add_mod("Off", ["a.pak"], active=False)
        self.assertEqual(self.game.mapper().mappings(), [])

    def test_odd_layouts(self):
        game_dir = self.install()
        data = game_dir / "Data"
        root = self.organizer.add_mod("Odd", [
            "Root/Data/Public/Odd/x.txt",
            "root/bin/NativeMods/x.dll",
            "Mods/OddFolder/GUI/x.xaml",
            "Mods/Odd.pak",
            "PUBLIC/Odd/y.txt",
            "Wrapper/Generated/Public/x.GR2",
            "Wrapper/readme.txt",
            "Workspace/Mods/Workspace/meta.lsx",
            "Workspace/Public/Workspace/z.txt",
            "docs/readme.md",
        ])
        mapped = self.mapped()
        self.assertEqual(mapped[root / "Root" / "Data"], data)
        self.assertEqual(mapped[root / "Mods" / "OddFolder"], data / "Mods" / "OddFolder")
        self.assertEqual(mapped[root / "Mods" / "Odd.pak"], self.docs / "Mods" / "Odd.pak")
        # a game folder in another case still counts
        self.assertEqual(mapped[root / "PUBLIC"], data / "PUBLIC")
        self.assertEqual(mapped[root / "Wrapper" / "Generated"], data / "Generated")
        self.assertFalse(any("Workspace" in str(s) for s in mapped))
        self.assertFalse(any("docs" in s.parts for s in mapped))

    def test_root_builder_maps_root_itself(self):
        self.install()
        root = self.organizer.add_mod("R", ["Root/Data/Public/x.txt"])
        self.organizer.enabled_plugins.add("RootBuilder")
        self.assertNotIn(root / "Root" / "Data", self.mapped())

    def test_yaml_configs_converted_when_asked(self):
        self.install()
        root = self.organizer.add_mod("Cfg", [])
        files = {
            "Script Extender/Mine/Config.yaml": "Enabled: yes\nLevels: [1, 2]\n",
            "SE_CONFIG/Other/Settings.yml": "﻿Name: 'Ánimo'\n",
            "Script Extender/Bad/Broken.yaml": "a: &anchor 1\n",
            "Script Extender/Mine/Plain.json": '{"kept": true}',
            "bin/ScriptExtenderSettings.yaml": "CreateConsole: true\n",
            "Root/bin/Other.yaml": "x: 1\n",
        }
        for rel, text in files.items():
            (root / rel).parent.mkdir(parents=True, exist_ok=True)
            (root / rel).write_text(text, encoding="utf-8")
        self.organizer.setPluginSetting(NAME, "convert_yamls_to_json", True)
        mapped = self.mapped()
        docs_se = self.docs / "Script Extender"
        config = root / "Script Extender/Mine/Config.json"
        self.assertEqual(json.loads(config.read_text(encoding="utf-8")), {"Enabled": True, "Levels": [1, 2]})
        self.assertEqual(mapped[config], docs_se / "Mine" / "Config.json")
        self.assertEqual(mapped[root / "SE_CONFIG/Other/Settings.json"], docs_se / "Other" / "Settings.json")
        self.assertEqual(mapped[root / "Script Extender/Mine/Plain.json"], docs_se / "Mine" / "Plain.json")
        # the YAML itself isn't mapped; the broken one leaves no JSON
        self.assertNotIn(root / "Script Extender/Mine/Config.yaml", mapped)
        self.assertFalse((root / "Script Extender/Bad/Broken.json").exists())
        # bin/ and Root/bin are converted in place, not mapped from here
        self.assertTrue((root / "bin/ScriptExtenderSettings.json").is_file())
        self.assertTrue((root / "Root/bin/Other.json").is_file())
        self.assertNotIn(root / "bin/ScriptExtenderSettings.json", mapped)

    def test_overwrite_data_goes_to_the_game_and_the_rest_to_documents(self):
        game_dir = self.install()
        self.organizer.add_mod("A", ["a.pak"])
        overwrite = self.organizer.overwrite
        for rel in ("Data/Public/x.txt", "Script Extender/log.json", "Stats/s.txt"):
            (overwrite / rel).parent.mkdir(parents=True, exist_ok=True)
            (overwrite / rel).write_text(rel)
        mapped = self.mapped()
        self.assertEqual(mapped[overwrite / "Data"], game_dir / "Data")
        self.assertEqual(mapped[overwrite / "Stats" / "s.txt"], self.docs / "Stats" / "s.txt")
        self.assertEqual(mapped[overwrite / "Script Extender" / "log.json"], self.docs / "Script Extender" / "log.json")
        self.assertNotIn(overwrite / "Data" / "Public" / "x.txt", mapped)

    def test_folder_that_cant_be_made_is_logged(self):
        self.install()
        self.organizer.add_mod("A", ["a.pak"])
        # a file where the mapper makes the Temp folder
        (self.organizer.overwrite / "Temp").write_text("in the way")
        self.mapped()
        self.assertTrue((self.organizer.overwrite / "Temp").is_file())

    def test_empty_folders_removed_after_programs_and_the_game(self):
        self.install()
        self.organizer.add_mod("A", ["a.pak"])
        mapper = self.game.mapper()
        mapper.mappings()
        made_in_docs = self.docs / "Mods"
        self.assertTrue(made_in_docs.is_dir())
        kept = self.organizer.overwrite / "Stats"
        (kept / "x.txt").write_text("the game's")
        # after another program: Overwrite only
        mapper.remove_empty_folders(self.organizer.overwrite, documents=False)
        self.assertTrue(made_in_docs.is_dir())
        self.assertFalse((self.organizer.overwrite / "Temp").exists())
        self.assertTrue(kept.is_dir())
        mapper.remove_empty_folders(self.organizer.overwrite)
        self.assertFalse(made_in_docs.exists())
        self.assertTrue(self.organizer.overwrite.is_dir())


class LayoutTest(PluginBase):
    def test_checker_odd_layouts(self):
        checker = BG3ModDataChecker()
        valid, fixable, invalid = (mobase.ModDataChecker.VALID, mobase.ModDataChecker.FIXABLE,
                                   mobase.ModDataChecker.INVALID)
        for files, want in (
            (["PAK_FILES/sub/a.pak"], valid),
            (["PAK_FILES/readme.md"], invalid),
            (["SE_CONFIG/Mod/x.json"], valid),
            (["Root/bin/x.dll"], valid),
            (["Video/intro.bk2"], valid),
            # a wrapper around a folder that is valid inside but isn't the game's
            (["Wrap/Video/intro.bk2"], invalid),
            # a pak anywhere is found where it is
            (["Wrap 1.0/x.pak", "info.json"], valid),
            (["x.toml"], fixable),
            (["what.exe", "a.pak"], invalid),
        ):
            self.assertEqual(checker.dataLooksValid(make_tree(files)), want, files)

    def test_content_odd_layouts(self):
        content = BG3DataContent()
        for files, want in (
            (["Root/bin/DWrite.dll", "Root/Data/Public/x"], [Content.LOOSE_FILES, Content.SCRIPT_EXTENDER]),
            (["Root/readme.txt"], []),
            (["PAK_FILES/a.pak"], [Content.PAK]),
            (["SE_CONFIG/Mod/x.json"], [Content.SE_FILES]),
            (["Script Extender/x.json"], [Content.SE_FILES]),
            (["Mods/a.pak", "Mods/Folder/x.xaml"], [Content.PAK, Content.LOOSE_FILES]),
            (["Mods/readme.txt"], []),
            (["bin/ScriptExtenderSettings.json", "bin/Mine.dll", "bin/bink2w64.dll", "bin/sub/x.dll"],
             [Content.NATIVE, Content.SE_FILES]),
            (["Generated/x"], [Content.LOOSE_FILES]),
            (["docs/readme.md"], []),
        ):
            self.assertEqual(content.getContentsFor(make_tree(files)), sorted(want), files)


if __name__ == "__main__":
    import unittest

    unittest.main()
